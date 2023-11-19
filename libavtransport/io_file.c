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

struct AVTIOCtx {
    FILE *f;
};

static int file_init(AVTContext *ctx, AVTIOCtx **io, AVTAddress *addr)
{
    AVTIOCtx *priv = malloc(sizeof(*priv));
    if (!priv)
        return AVT_ERROR(ENOMEM);

    priv->f = fopen(addr->path, "w+");
    if (!priv->f) {
        free(priv);
        return AVT_ERROR(errno);
    }

    *io = priv;

    return 0;
}

static uint32_t file_max_pkt_len(AVTContext *ctx, AVTIOCtx *io)
{
    return UINT32_MAX;
}

static int file_input(AVTContext *ctx, AVTIOCtx *io, AVTBuffer *buf)
{
    uint8_t header[36];
    fread(header, 36, 1, io->f);

    return 0;
}

static int file_output(AVTContext *ctx, AVTIOCtx *io,
                       uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                       AVTBuffer *payload)
{
    size_t len;
    uint8_t *data = avt_buffer_get_data(payload, &len);

    size_t out = fwrite(hdr, 1, hdr_len, io->f);
    if (out != hdr_len) {
        avt_log(io, AVT_LOG_ERROR, "Error while writing!\n");
        return AVT_ERROR(errno);
    }

    if (payload) {
        out = fwrite(data, 1, len, io->f);
        if (out != len) {
            avt_log(io, AVT_LOG_ERROR, "Error while writing!\n");
            return AVT_ERROR(errno);
        }
    }
    fflush(io->f);

    return 0;
}

static int file_seek(AVTContext *ctx, AVTIOCtx *io,
                     uint64_t off, uint32_t seq,
                     int64_t ts, bool ts_is_dts)
{
    return 0;
}

static int file_close(AVTContext *ctx, AVTIOCtx **_io)
{
    AVTIOCtx *io = *_io;
    int ret = fclose(io->f);
    free(io);
    *_io = NULL;
    return AVT_ERROR(ret);
}

const AVTIO avt_io_file = {
    .name = "file",
    .type = AVT_IO_FILE,
    .init = file_init,
    .get_max_pkt_len = file_max_pkt_len,
    .read_input = file_input,
    .write_output = file_output,
    .seek = file_seek,
    .close = file_close,
};
