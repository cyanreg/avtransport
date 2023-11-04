/*
 * Copyright © 2023, Lynne
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

#ifndef LIBAVTRANSPORT_OUTPUT
#define LIBAVTRANSPORT_OUTPUT

#include <libavtransport/output.h>

#include "common.h"

typedef struct AVTOutputContext AVTOutputContext;

typedef struct AVTOutput {
    const char *name;
    enum AVTConnectionType type;

    int (*init)(AVTContext *ctx, AVTOutputContext **pc,
                AVTOutputDestination *dst, AVTOutputOptions *opts);

    uint32_t (*max_pkt_len)(AVTContext *ctx, AVTOutputContext *pc);

    int (*output)(AVTContext *ctx, AVTOutputContext *pc,
                  uint8_t *hdr, size_t hdr_len, AVTBuffer *buf);

    int (*close)(AVTContext *ctx, AVTOutputContext **pc);
} AVTOutput;

uint32_t avt_unlim_pkt_len(AVTContext *ctx, AVTOutputContext *pc);

#endif
