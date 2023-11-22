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

#include <avtransport/output.h>

#include "common.h"
#include "connection_internal.h"

#include "../config.h"

#ifdef CONFIG_HAVE_LIBZSTD
#include <zstd.h>
#endif

typedef struct AVTOutput {
    AVTContext *ctx;

    AVTConnection **conn;
    uint32_t nb_conn;

    AVTStream **stream;
    int nb_stream;

    atomic_uint_least64_t seq;
    atomic_uint_least64_t epoch;

#ifdef CONFIG_HAVE_LIBZSTD
    ZSTD_CCtx *zstd_ctx;
#endif
} AVTOutput;

int avt_packet_send(AVTOutput *out,
                    uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                    AVTBuffer *buf);

size_t avt_packet_get_max_size(AVTOutput *out);

#endif
