/*
 * Copyright © 2026, Lynne
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

/* LDPC matrix declarations and the reference encoder for AVTransport
 * forward error correction. See Annex W of the specification. */

#ifndef AVTRANSPORT_LDPC_TABLES_H
#define AVTRANSPORT_LDPC_TABLES_H

#include <stdint.h>
#include <stdalign.h>

extern const uint64_t ldpc_h_matrix_288_224[288];

extern const uint64_t ldpc_w_matrix_288_224[224];

extern const uint64_t ldpc_h_matrix_2784_2016[33408];

extern const uint64_t ldpc_w_matrix_2784_2016[24192];

static inline
void ldpc_encode(uint8_t *pkt, const uint64_t *W,
                 int message_bits, int parity_bits)
{
    int j, k, w;
    int words = parity_bits / 64;
    uint64_t parity, data, bits[8];

    /* Parity data goes last */
    uint8_t *dst = pkt + (message_bits / 8);

    /* Compute one 64-row slice of the parity vector at a time */
    for (w = 0; w < words; w++) {
        parity = 0x0; /* Start with zero parity */

        /* Process 8 message bits at a time */
        for (j = 0; j < (message_bits / 8); j++) {
            data = pkt[j];

            /* Duplicate each bit 64 times (reversed, bytestream order) */
            for (k = 0; k < 8; k++)
                bits[k] = ((data >> (7 - k)) & 1) * UINT64_MAX;

            /* XOR in the generator column of every message bit set to 1 */
            for (k = 0; k < 8; k++)
                parity ^= bits[k] & W[(j*8 + k)*words + w];
        }

        /* Write this slice of parity data, in bytestream order */
        for (k = 56; k >= 0; k -= 8)
            *dst++ = (parity >> k) & 0xFF;
    }
}

#endif /* AVTRANSPORT_LDPC_TABLES_H */
