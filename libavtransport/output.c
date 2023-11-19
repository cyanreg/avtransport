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

#include <stdio.h>
#include <stdlib.h>

#include "output_internal.h"
#include "encode.h"

#include "../config.h"

int avt_output_open(AVTContext *ctx, AVTConnection *conn)
{
    AVTOutput *out = calloc(1, sizeof(*out));
    ctx->out = out;

    atomic_store(&out->seq, 0);
    atomic_store(&out->epoch, 0);
    out->conn = conn;

#ifdef CONFIG_HAVE_LIBZSTD
    out->zstd_ctx = ZSTD_createCCtx();
    if (!out->zstd_ctx)
        return AVT_ERROR(ENOMEM);
#endif

    avt_output_session_start(ctx, out);

    return 0;
}

AVT_API AVTStream *avt_output_add_stream(AVTContext *ctx, uint16_t id)
{
    return avt_alloc_stream(ctx, id);
}

AVT_API int avt_output_update_stream(AVTContext *ctx, AVTStream *st)
{
    return avt_output_stream_register(ctx, ctx->out, st);
}

AVT_API int avt_output_write_stream_data(AVTContext *ctx, AVTStream *st,
                                         AVTPacket *pkt)
{
    return avt_output_stream_data(ctx, ctx->out, st, pkt);
}

size_t avt_packet_get_max_size(AVTContext *ctx, AVTOutput *out)
{
    return out->conn->p->get_max_pkt_len(ctx, out->conn->p_ctx);
}

int avt_packet_send(AVTContext *ctx, AVTOutput *out,
                    uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len, AVTBuffer *buf)
{
    return out->conn->p->send_packet(ctx, out->conn->p_ctx, hdr, hdr_len, buf);
}
