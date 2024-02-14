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

#include "utils_internal.h"
#include "output_internal.h"
#include "output_packet.h"

#include "../config.h"

int avt_output_open(AVTContext *ctx, AVTOutput **_out,
                    AVTConnection *conn, AVTOutputOptions *opts)
{
    AVTOutput *out;

    /* Allocate state, if not already existing */
    if (!(*_out)) {
        out = calloc(1, sizeof(*out));

        atomic_store(&out->seq, 0);
        atomic_store(&out->epoch, avt_get_time_ns());

        out->conn = calloc(1, sizeof(*out->conn));
        if (!out->conn) {
            free(out);
            return AVT_ERROR(ENOMEM);
        }

#ifdef CONFIG_HAVE_LIBZSTD
        out->zstd_ctx = ZSTD_createCCtx();
        if (!out->zstd_ctx)
            return AVT_ERROR(ENOMEM);
#endif

        *_out = out;
    } else {
        out = *_out;
    }

    /* Register connection for output */
    out->conn[out->nb_conn++] = conn;

    /* Immediately emit a session start packet */
    int err = avt_send_session_start(out);

    /* If unsuccessful, revert all changes */
    if (err < 0)
        out->nb_conn--;

    return err;
}

AVTStream *avt_output_stream_add(AVTOutput *out, uint16_t id)
{
    if (id == UINT16_MAX) {
        avt_log(out, AVT_LOG_ERROR, "Invalid stream ID: 0x%X is reserved!\n", id);
        return NULL;
    }

    AVTStream *st = &out->streams[id];
    if (st->priv && st->priv->active) {
        avt_log(out, AVT_LOG_ERROR, "Stream 0x%X is already active!\n", id);
        return NULL;
    }

    st->id = id;
    if (!st->priv) {
        st->priv = calloc(1, sizeof(*st->priv));
        if (!st->priv)
            return NULL;
    }

    st->priv->active = true;
    st->priv->out = out;
    out->active_stream_idx[out->nb_streams++] = id;

    return st;
}

int avt_output_stream_close(AVTStream **_st)
{
    if (!_st)
        return 0;

    AVTStream *st = *_st;
    if (!st)
        return 0;

    if (!st->priv || !st->priv->active) {
        avt_log(st->priv->out, AVT_LOG_ERROR, "Stream 0x%X is not active!\n", st->id);
        return AVT_ERROR(EINVAL);
    }

    st->priv->active = false;

    return 0;
}

int avt_output_stream_update(AVTOutput *out, AVTStream *st)
{
    return avt_send_stream_register(out, st);
}

int avt_output_stream_data(AVTStream *st, AVTPacket *pkt)
{
    return avt_send_stream_data(st->priv->out, st, pkt);
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
