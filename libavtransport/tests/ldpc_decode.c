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
#include "ldpc_decode.h"

/* Encode a random message into the n-bit codeword buf. */
static void encode_random(uint8_t *buf, const uint64_t *W,
                          int message_bits, int parity_bits)
{
    for (int i = 0; i < message_bits / 8; i++)
        buf[i] = rand() & 0xFF;
    memset(buf + message_bits / 8, 0, parity_bits / 8);
    ldpc_encode(buf, W, message_bits, parity_bits);
}

static int test_code(const char *name, const uint64_t *W,
                      void (*decode)(uint8_t *, int),
                      int n, int message_bits, int parity_bits)
{
    uint8_t clean[2784 / 8], rx[2784 / 8];
    const int nbytes = n / 8;

    /* Every single-bit error must be corrected — exhaustively. */
    encode_random(clean, W, message_bits, parity_bits);
    for (int p = 0; p < n; p++) {
        memcpy(rx, clean, nbytes);
        rx[p >> 3] ^= 1 << (7 - (p & 7));

        decode(rx, 50);

        if (memcmp(rx, clean, nbytes)) {
            printf("%s: single-bit error at bit %d not corrected\n", name, p);
            return 1;
        }
    }
    printf("%s: all %d single-bit errors corrected\n", name, n);

    /* A clean codeword must be returned untouched. */
    memcpy(rx, clean, nbytes);
    decode(rx, 50);
    if (memcmp(rx, clean, nbytes)) {
        printf("%s: clean codeword was modified\n", name);
        return 1;
    }

    /* Informational: correction rate on heavier random error patterns. */
    for (int errs = 2; errs <= 8; errs += 2) {
        const int trials = 1000;
        int ok = 0;
        for (int t = 0; t < trials; t++) {
            encode_random(clean, W, message_bits, parity_bits);
            memcpy(rx, clean, nbytes);
            for (int e = 0; e < errs; e++) {
                int p = rand() % n;
                rx[p >> 3] ^= 1 << (7 - (p & 7));
            }
            decode(rx, 50);
            ok += memcmp(rx, clean, nbytes) == 0;
        }
        printf("%s: %d-bit errors corrected in %d/%d trials\n",
               name, errs, ok, trials);
    }
    return 0;
}

int main(void)
{
    srand(time(NULL));

    if (test_code("LDPC(288,224)", ldpc_w_matrix_288_224,
                   avt_ldpc_decode_288_224, 288, 224, 64))
        return 1;

    if (test_code("LDPC(2784,2016)", ldpc_w_matrix_2784_2016,
                   avt_ldpc_decode_2784_2016, 2784, 2016, 768))
        return 1;

    return 0;
}
