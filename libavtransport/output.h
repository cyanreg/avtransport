/*
 * Copyright © 2022 Lynne
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the “Software”), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef LIBAVTRANSPORT_OUTPUT
#define LIBAVTRANSPORT_OUTPUT

#include <libavtransport/output.h>

#include "common.h"

typedef struct PQOutputContext PQOutputContext;

typedef struct PQOutput {
    const char *name;
    enum AVTConnectionType type;

    int (*init)(AVTContext *ctx, PQOutputContext **pc,
                AVTOutputDestination *dst, AVTOutputOptions *opts);

    uint32_t (*max_pkt_len)(AVTContext *ctx, PQOutputContext *pc);

    int (*output)(AVTContext *ctx, PQOutputContext *pc,
                  uint8_t *hdr, size_t hdr_len, AVTBuffer *buf);

    int (*close)(AVTContext *ctx, PQOutputContext **pc);
} PQOutput;

uint32_t pq_unlim_pkt_len(AVTContext *ctx, PQOutputContext *pc);

#endif