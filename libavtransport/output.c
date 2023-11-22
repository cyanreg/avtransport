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
#include <time.h>

#include "utils_internal.h"
#include "output_internal.h"
#include "encode.h"

#include "../config.h"

int avt_output_open(AVTContext *ctx, AVTOutput **_out,
                    AVTConnection *conn, AVTOutputOptions *opts)
{
    AVTOutput *out = calloc(1, sizeof(*out));

    atomic_store(&out->seq, 0);
    atomic_store(&out->epoch, avt_get_time_ns());

    out->conn = calloc(1, sizeof(*out->conn));
    if (!out->conn) {
        free(out);
        return AVT_ERROR(ENOMEM);
    }

    out->nb_conn = 1;
    out->conn[0] = conn;

#ifdef CONFIG_HAVE_LIBZSTD
    out->zstd_ctx = ZSTD_createCCtx();
    if (!out->zstd_ctx)
        return AVT_ERROR(ENOMEM);
#endif

    avt_send_session_start(out);

    return 0;
}

AVTStream *avt_output_stream_add(AVTOutput *out, uint16_t id)
{

//    return avt_alloc_stream(out->ctx, id);
    return NULL;
}

int avt_output_stream_update(AVTOutput *out, AVTStream *st)
{
    return avt_send_stream_register(out, st);
}

int avt_output_stream_data(AVTStream *st, AVTPacket *pkt)
{
    return avt_send_stream_data(st->priv->out, st, pkt);
}

size_t avt_packet_get_max_size(AVTOutput *out)
{
    return out->conn[0]->p->get_max_pkt_len(out->ctx, out->conn[0]->p_ctx);
}

int avt_packet_send(AVTOutput *out,
                    uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len, AVTBuffer *buf)
{
    return out->conn[0]->p->send_packet(out->ctx, out->conn[0]->p_ctx, hdr, hdr_len, buf);
}

int avt_output_close(AVTOutput **_out)
{
    AVTOutput *out = *_out;

#ifdef CONFIG_HAVE_LIBZSTD
    ZSTD_freeCCtx(out->zstd_ctx);
#endif
    free(out);

    *_out = NULL;

    return 0;
}
