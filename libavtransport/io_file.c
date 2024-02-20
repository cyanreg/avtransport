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

#include "os_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <stdckdint.h>

#include "io_common.h"
#include "utils_internal.h"

struct AVTIOCtx {
    FILE *f;
    off_t rpos;
    off_t wpos;
    bool is_write;
};

static int handle_error(AVTIOCtx *io, const char *msg)
{
    char8_t err_info[256];
    strerror_s(err_info, sizeof(err_info), errno);
    avt_log(io, AVT_LOG_ERROR, msg, err_info);
    return AVT_ERROR(errno);
}

static int file_init(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    int ret;
    AVTIOCtx *io = calloc(1, sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    io->f = fopen(addr->path, "w+");
    if (!io->f) {
        ret = handle_error(io, "Error opening: %s\n");
        free(io);
        return ret;
    }

    *_io = io;

    return 0;
}

static uint32_t file_max_pkt_len(AVTContext *ctx, AVTIOCtx *io)
{
    return UINT32_MAX;
}

static int64_t file_read_input(AVTContext *ctx, AVTIOCtx *io,
                               AVTBuffer **_buf, size_t len)
{
    int ret;
    uint8_t *data;
    size_t buf_len, off = 0;
    AVTBuffer *buf = *_buf;

    if (io->is_write) {
        ret = fseeko(io->f, io->rpos, SEEK_SET);
        if (ret < 0) {
            ret = handle_error(io, "Error seeking: %s\n");
            return ret;
        }
        io->is_write = true;
    }

    if (!buf) {
        buf = avt_buffer_alloc(AVT_MAX(len, AVT_MAX_HEADER_BUF));
        if (!buf)
            return AVT_ERROR(ENOMEM);

        *_buf = buf;
    } else {
        off = avt_buffer_get_data_len(buf);

        if (ckd_add(&buf_len, off, len))
            return AVT_ERROR(EINVAL);

        ret = avt_buffer_resize(buf, buf_len);
        if (ret < 0)
            return ret;
    }

    /* Read data */
    data = avt_buffer_get_data(buf, &buf_len);
    len = fread(data + off, 1, len, io->f);

    /* Adjust new size in case of underreads */
    ret = avt_buffer_resize(buf, off + len);
    avt_assert2(ret >= 0);

    return (int64_t)(io->rpos = ftello(io->f));
}

static int64_t file_write_output(AVTContext *ctx, AVTIOCtx *io, AVTPktd *p)
{
    int ret;

    if (!io->is_write) {
        ret = fseeko(io->f, io->wpos, SEEK_SET);
        if (ret < 0) {
            ret = handle_error(io, "Error seeking: %s\n");
            return ret;
        }
        io->is_write = true;
    }

    size_t out = fwrite(p->hdr, 1, p->hdr_len, io->f);
    if (out != p->hdr_len) {
        ret = handle_error(io, "Error writing: %s\n");
        return ret;
    }

    size_t pl_len;
    uint8_t *data = avt_buffer_get_data(&p->pl, &pl_len);
    if (data) {
        out = fwrite(data, 1, pl_len, io->f);
        if (out != pl_len) {
            ret = handle_error(io, "Error writing: %s\n");
            return ret;
        }
    }

    return (int64_t)(io->wpos = ftello(io->f));
}

static int64_t file_write_vec(AVTContext *ctx, AVTIOCtx *io,
                              AVTPktd *iov, uint32_t nb_iov)
{
    int ret;

    if (!io->is_write) {
        ret = fseeko(io->f, io->wpos, SEEK_SET);
        if (ret < 0) {
            ret = handle_error(io, "Error seeking: %s\n");
            return ret;
        }
        io->is_write = true;
    }

    size_t out;
    size_t pl_len;
    uint8_t *pl_data;
    AVTPktd *v;
    for (auto i = 0; i < nb_iov; i++) {
        v = &iov[i];

        out = fwrite(v->hdr, 1, v->hdr_len, io->f);
        if (out != v->hdr_len) {
            ret = AVT_ERROR(errno);
            break;
        }

        pl_data = avt_buffer_get_data(&v->pl, &pl_len);
        if (pl_data) {
            out = fwrite(pl_data, 1, pl_len, io->f);
            if (out != pl_len) {
                ret = AVT_ERROR(errno);
                break;
            }
        }
    }

    return (int64_t)(io->wpos = ftello(io->f));
}

static int64_t file_seek(AVTContext *ctx, AVTIOCtx *io, int64_t off)
{
    int ret = fseeko(io->f, (off_t)off, SEEK_SET);
    if (ret < 0) {
        ret = handle_error(io, "Error seeking: %s\n");
        return ret;
    }

    io->is_write = false;

    return (int64_t)(io->rpos = ftello(io->f));
}

static int file_flush(AVTContext *ctx, AVTIOCtx *io)
{
    int ret = fflush(io->f);
    if (ret)
        ret = handle_error(io, "Error flushing: %s\n");

    return ret;
}

static int file_close(AVTContext *ctx, AVTIOCtx **_io)
{
    AVTIOCtx *io = *_io;
    int ret = fclose(io->f);
    if (ret)
        ret = handle_error(io, "Error closing: %s\n");

    free(io);
    *_io = NULL;

    return ret;
}

const AVTIO avt_io_file = {
    .name = "file",
    .type = AVT_IO_FILE,
    .init = file_init,
    .get_max_pkt_len = file_max_pkt_len,
    .read_input = file_read_input,
    .write_output = file_write_output,
    .write_vec = file_write_vec,
    .seek = file_seek,
    .flush = file_flush,
    .close = file_close,
};
