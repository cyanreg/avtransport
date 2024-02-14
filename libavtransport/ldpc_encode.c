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

#include "ldpc_encode.h"
#include "ldpc_tables.h"

static inline void ldpc_encode(uint64_t *dst, uint8_t *src, const uint64_t *H,
                               int message_bits, int parity_bits)
{
    uint64_t data, bits[8];

    for (int i = 0; i < (parity_bits / 64); i++) {
        dst[i] = 0x0;

        for (int j = 0; j < (message_bits / 8); j++) {
            data = src[j];

            for (int k = 0; k < 8; k++)
                bits[k] = ((data >> k) & 1) * UINT64_MAX;

            for (int k = 0; k < 8; k++)
                dst[i] ^= bits[k] & (*H++);
        }

        H += parity_bits; // Padding
    }
}

void avt_ldpc_encode_288_224(uint8_t *src)
{
    ldpc_encode((uint64_t *)(src + 28), src, (const uint64_t *)ldpc_h_matrix_288_224, 224, 64);
}

void avt_ldpc_encode_2784_2016(uint8_t *src)
{
    ldpc_encode((uint64_t *)(src + 252), src, (const uint64_t *)ldpc_h_matrix_2784_2016, 2016, 768);
}
