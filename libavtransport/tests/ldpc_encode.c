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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ldpc_tables.h"

/* Number of nonzero 64-bit words of the syndrome H * c^T for the n-bit
 * codeword in pkt. H is column-major: n columns of (parity_bits / 64) words
 * each. A return of 0 means c is a valid codeword of the code. */
static int syndrome_weight(const uint8_t *pkt, const uint64_t *H,
                           int n, int parity_bits)
{
    int words = parity_bits / 64;
    uint64_t synd[768 / 64] = { 0 };

    for (int c = 0; c < n; c++) {
        if ((pkt[c >> 3] >> (7 - (c & 7))) & 1)
            for (int w = 0; w < words; w++)
                synd[w] ^= H[c * words + w];
    }

    int nz = 0;
    for (int w = 0; w < words; w++)
        nz += synd[w] != 0;
    return nz;
}

/* Encode random messages with the systematic generator W and confirm every
 * resulting codeword satisfies H * c^T = 0 — i.e. W and H agree. */
static int test_code(const char *name, const uint64_t *H, const uint64_t *W,
                      int n, int message_bits, int parity_bits)
{
    uint8_t pkt[2784 / 8];
    const int trials = 10000;

    for (int t = 0; t < trials; t++) {
        for (int i = 0; i < message_bits / 8; i++)
            pkt[i] = rand() & 0xFF;
        memset(pkt + message_bits / 8, 0, parity_bits / 8);

        ldpc_encode(pkt, W, message_bits, parity_bits);

        if (syndrome_weight(pkt, H, n, parity_bits)) {
            printf("%s: codeword %d failed H * c^T = 0\n", name, t);
            return 1;
        }
    }

    printf("%s: %d encoded codewords verified\n", name, trials);
    return 0;
}

int main(void)
{
    srand(time(NULL));

    if (test_code("LDPC(288,224)", ldpc_h_matrix_288_224,
                   ldpc_w_matrix_288_224, 288, 224, 64))
        return 1;

    if (test_code("LDPC(2784,2016)", ldpc_h_matrix_2784_2016,
                   ldpc_w_matrix_2784_2016, 2784, 2016, 768))
        return 1;

    return 0;
}
