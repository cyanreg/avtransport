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
#include <time.h>

#include "ldpc_tables.h"

#define DATA_LEN 1024
#define PARITY_LEN 1024

int main(void)
{
    uint8_t data[DATA_LEN + PARITY_LEN];

    /* Get random data */
    srand(time(NULL));
    for (int i = 0; i < DATA_LEN; i++)
        data[i] = rand() & 0xFF;

    /* Make a matrix filled with 1s.
     * Encoding with this is equivalent to summing all bits in GF(2). */
    uint64_t *parity_matrix = malloc(((DATA_LEN + PARITY_LEN) * PARITY_LEN) * 8);
    for (int i = 0; i < ((DATA_LEN + PARITY_LEN) * PARITY_LEN); i++)
        parity_matrix[i] = UINT64_MAX;

    uint8_t parity = 0;
    for (int i = 0; i < DATA_LEN; i++) {
        uint8_t p = data[i];

        parity ^= (p >> 7) & 1;
        parity ^= (p >> 6) & 1;
        parity ^= (p >> 5) & 1;
        parity ^= (p >> 4) & 1;
        parity ^= (p >> 3) & 1;
        parity ^= (p >> 2) & 1;
        parity ^= (p >> 1) & 1;
        parity ^= (p >> 0) & 1;
    }
    parity *= 0xFF;

    ldpc_encode(data, parity_matrix, DATA_LEN * 8, PARITY_LEN * 8);
    free(parity_matrix);

    for (int i = 0; i < PARITY_LEN; i++) {
        if (data[DATA_LEN + i] != parity) {
            printf("Mismatch, 0x%x vs 0x%x\n", data[DATA_LEN + i], parity);
            return 1;
        }
    }

    return 0;
}
