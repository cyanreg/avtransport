/*
 * Copyright © 2024, Lynne
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <string.h>

#include "ldpc_decode.h"
#include "ldpc_tables.h"

/*
 * Working-set bounds, sized for the two parity-check matrices in the spec's
 * Annex W. LDPC(2784,2016) — n = 2784, m = 768, 8688 edges — sets every
 * maximum; if the matrices are ever re-optimised these must be revisited
 * (a denser matrix is rejected at run time rather than overrunning a buffer).
 */
#define LDPC_MAX_N      2784
#define LDPC_MAX_M       768
#define LDPC_MAX_EDGES  8688

/*
 * Fixed-point scale. The channel LLR is the only input magnitude, and min-sum
 * is scale-invariant, so its value is a free choice. Per-variable totals are
 * saturated to LDPC_CLAMP, which bounds every message comfortably inside the
 * int16_t range. The 0.8 normalisation factor is applied as 205/256.
 */
#define LDPC_LLR        64
#define LDPC_CLAMP      2048
#define LDPC_NORM_NUM   205
#define LDPC_NORM_SH    8

/* Returns 1 if the n-bit packed word in pkt is a codeword of the column-major
 * parity-check matrix H, i.e. the syndrome H * c^T is zero. */
static int ldpc_is_codeword(const uint8_t *pkt, const uint64_t *H,
                            int n, int words)
{
    uint64_t synd[LDPC_MAX_M / 64] = { 0 };

    for (int v = 0; v < n; v++) {
        if (!((pkt[v >> 3] >> (7 - (v & 7))) & 1))
            continue;
        for (int w = 0; w < words; w++)
            synd[w] ^= H[(size_t)v * words + w];
    }

    for (int w = 0; w < words; w++)
        if (synd[w])
            return 0;
    return 1;
}

/*
 * Hard-input LDPC decoder: normalised min-sum belief propagation on the
 * parity-check matrix H, flooding schedule — the optimizer's reference
 * decoder reformulated in fixed-point integer arithmetic. No floating point
 * and no dynamic allocation: the whole working set is fixed-size and
 * stack-resident (~30 KiB; make it _Thread_local static if a small call
 * stack ever matters more than resident memory).
 *
 * The n-bit codeword in pkt is corrected in place; H is column-major with
 * parity_bits / 64 uint64 words per column. The result is written back only
 * on a converged decode, so a failed decode never leaves the received data
 * more corrupted than it arrived.
 *
 * The Tanner graph is never materialised — every pass walks the column
 * bitsets of H directly, numbering edges in column-major order, and the
 * check-node update accumulates into small per-check aggregates rather than
 * an explicit edge list.
 */
static void ldpc_decode(uint8_t *pkt, const uint64_t *H,
                        int n, int parity_bits, int iterations)
{
    const int m     = parity_bits;
    const int words = parity_bits / 64;

    int16_t Q[LDPC_MAX_N];          /* per-variable saturated total LLR     */
    int16_t c2v[LDPC_MAX_EDGES];    /* per-edge check-to-variable message   */
    int16_t cmin1[LDPC_MAX_M];      /* per-check smallest |v2c|             */
    int16_t cmin2[LDPC_MAX_M];      /* per-check second smallest |v2c|      */
    int     cmin1e[LDPC_MAX_M];     /* edge index that holds cmin1          */
    int16_t cneg[LDPC_MAX_M];       /* per-check count of v2c < 0           */

    if (n > LDPC_MAX_N || m > LDPC_MAX_M || iterations < 1)
        return;

    /* Common case: the header arrived intact — nothing to correct. */
    if (ldpc_is_codeword(pkt, H, n, words))
        return;

    /* Reject a matrix denser than this build is sized for, rather than
     * overrunning c2v[]. */
    {
        int edges = 0;
        for (size_t i = 0; i < (size_t)n * words; i++)
            edges += __builtin_popcountll(H[i]);
        if (edges > LDPC_MAX_EDGES)
            return;
    }

    /* Initial state: no check messages yet, totals are the channel LLRs. */
    memset(c2v, 0, sizeof(c2v));
    for (int v = 0; v < n; v++)
        Q[v] = ((pkt[v >> 3] >> (7 - (v & 7))) & 1) ? -LDPC_LLR : LDPC_LLR;

    int converged = 0;

    for (int it = 0; it < iterations && !converged; it++) {
        int e;

        /* Check-node update, scan 1: over each check's incident messages
         * (v2c = Q[v] - c2v[edge]), gather the two smallest magnitudes, the
         * edge holding the smallest, and the count of negative messages. */
        for (int r = 0; r < m; r++) {
            cmin1[r]  = INT16_MAX;
            cmin2[r]  = INT16_MAX;
            cmin1e[r] = -1;
            cneg[r]   = 0;
        }
        e = 0;
        for (int v = 0; v < n; v++) {
            const uint64_t *col = H + (size_t)v * words;
            for (int w = 0; w < words; w++) {
                uint64_t bits = col[w];
                while (bits) {
                    int r = w * 64 + 63 - __builtin_ctzll(bits);
                    bits &= bits - 1;

                    int v2c = Q[v] - c2v[e];
                    int a   = v2c < 0 ? -v2c : v2c;
                    if (v2c < 0)
                        cneg[r]++;
                    if (a < cmin1[r]) {
                        cmin2[r]  = cmin1[r];
                        cmin1[r]  = a;
                        cmin1e[r] = e;
                    } else if (a < cmin2[r]) {
                        cmin2[r] = a;
                    }
                    e++;
                }
            }
        }

        /* Check-node update, scan 2: each outgoing message is the
         * alpha-scaled minimum magnitude over the *other* incident edges,
         * signed by their parity. */
        e = 0;
        for (int v = 0; v < n; v++) {
            const uint64_t *col = H + (size_t)v * words;
            for (int w = 0; w < words; w++) {
                uint64_t bits = col[w];
                while (bits) {
                    int r = w * 64 + 63 - __builtin_ctzll(bits);
                    bits &= bits - 1;

                    int v2c       = Q[v] - c2v[e];
                    int neg_other = cneg[r] - (v2c < 0);
                    int mag       = (e == cmin1e[r]) ? cmin2[r] : cmin1[r];
                    mag           = (mag * LDPC_NORM_NUM) >> LDPC_NORM_SH;
                    c2v[e]        = (neg_other & 1) ? -mag : mag;
                    e++;
                }
            }
        }

        /* Variable-node update: refresh each total Q[v] = llr[v] + sum of
         * incoming check messages, and fold in the syndrome of the hard
         * decision sign(Q[v]). */
        uint64_t synd[LDPC_MAX_M / 64] = { 0 };
        e = 0;
        for (int v = 0; v < n; v++) {
            const uint64_t *col = H + (size_t)v * words;
            int acc = ((pkt[v >> 3] >> (7 - (v & 7))) & 1) ? -LDPC_LLR
                                                           :  LDPC_LLR;
            for (int w = 0; w < words; w++) {
                uint64_t bits = col[w];
                while (bits) {
                    bits &= bits - 1;
                    acc += c2v[e];
                    e++;
                }
            }
            if (acc < -LDPC_CLAMP) acc = -LDPC_CLAMP;
            if (acc >  LDPC_CLAMP) acc =  LDPC_CLAMP;
            Q[v] = acc;

            if (acc < 0)
                for (int w = 0; w < words; w++)
                    synd[w] ^= col[w];
        }

        converged = 1;
        for (int w = 0; w < words; w++)
            if (synd[w])
                converged = 0;
    }

    /* Commit the corrected codeword only on a successful decode. */
    if (converged) {
        for (int v = 0; v < n; v++) {
            const uint8_t bit = 1u << (7 - (v & 7));
            if (Q[v] < 0)
                pkt[v >> 3] |=  bit;
            else
                pkt[v >> 3] &= ~bit;
        }
    }
}

void avt_ldpc_decode_288_224(uint8_t *dst, int iterations)
{
    ldpc_decode(dst, ldpc_h_matrix_288_224, 288, 64, iterations);
}

void avt_ldpc_decode_2784_2016(uint8_t *dst, int iterations)
{
    ldpc_decode(dst, ldpc_h_matrix_2784_2016, 2784, 768, iterations);
}
