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
#include <errno.h>

#include "connection_internal.h"
#include "bytestream.h"

typedef struct AVTConnectionContext {
    FILE *f;
} AVTConnectionContext;

static int file_init(AVTContext *ctx, AVTConnection *conn,
                     AVTConnectionInfo *info)
{
    AVTConnectionContext *priv = malloc(sizeof(*priv));
    if (!priv)
        return AVT_ERROR(ENOMEM);

    priv->f = fopen(info->path, "w+");
    if (!priv->f) {
        free(priv);
        return AVT_ERROR(errno);
    }

    conn->ctx = priv;

    return 0;
}

static uint32_t file_max_pkt_len(AVTContext *ctx, AVTConnection *conn)
{
    return UINT32_MAX;
}

static int file_input(AVTContext *ctx, AVTConnection *conn)
{
    uint8_t header[36];
    fread(header, 36, 1, conn->ctx->f);

    return 0;
}

static int file_output(AVTContext *ctx, AVTConnection *conn,
                       uint8_t *hdr, size_t hdr_len, AVTBuffer *buf)
{
    size_t len;
    uint8_t *data = avt_buffer_get_data(buf, &len);

    /* TODO: use io_uring */
    size_t out = fwrite(hdr, hdr_len, 1, conn->ctx->f);
    if (out != hdr_len)
        return AVT_ERROR(errno);

    if (buf) {
        out = fwrite(data, len, 1, conn->ctx->f);
        if (out != len)
            return AVT_ERROR(errno);
    }

    return 0;
}

static int file_seek(AVTContext *ctx, AVTConnection *conn,
                     int64_t min_offset, uint32_t target_seq)
{
    return 0;
}

static int file_close(AVTContext *ctx, AVTConnection *conn)
{
    int ret = fclose(conn->ctx->f);
    if (ret != 0)
        ret = AVT_ERROR(errno);

    free(conn->ctx);
    conn->ctx = NULL;

    return 0;
}

const AVTConnectionPrototype avt_output_file = {
    .name = "file",
    .type = AVT_CONNECTION_FILE,
    .init = file_init,
    .get_max_pkt_len = file_max_pkt_len,
    .process_input = file_input,
    .send_output = file_output,
    .seek = file_seek,
    .close = file_close,
};
