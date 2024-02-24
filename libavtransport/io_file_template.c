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

#include <stdckdint.h>
#include <inttypes.h>
#include <string.h>

static uint32_t RENAME(max_pkt_len)(AVTContext *ctx, AVTIOCtx *io)
{
    return UINT32_MAX;
}

static int64_t RENAME(seek)(AVTContext *ctx, AVTIOCtx *io, int64_t off)
{
    int ret = RENAME(seek_to)(io, (int64_t)off);
    if (ret < 0) {
        ret = RENAME(handle_error)(io, "Error seeking: %s\n");
        return ret;
    }

    io->is_write = false;

    return (io->rpos = RENAME(offset)(io));
}

static int64_t RENAME(read_input)(AVTContext *ctx, AVTIOCtx *io,
                                  AVTBuffer **_buf, size_t len,
                                  int64_t timeout)
{
    int ret;
    uint8_t *data;
    size_t buf_len, off = 0;
    AVTBuffer *buf = *_buf;

    if (io->is_write) {
        ret = RENAME(seek_to)(io, io->rpos);
        if (ret < 0) {
            ret = RENAME(handle_error)(io, "Error seeking: %s\n");
            return ret;
        }
        io->is_write = true;
    }

    if (!buf) {
        buf = avt_buffer_alloc(AVT_MAX(len, len));
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
    len = RENAME(read)(io, data + off, len, timeout);

    /* Adjust new size in case of underreads */
    ret = avt_buffer_resize(buf, off + len);
    avt_assert2(ret >= 0);

    return (io->rpos = RENAME(offset)(io));
}

static int64_t RENAME(write_pkt)(AVTContext *ctx, AVTIOCtx *io, AVTPktd *p,
                                 int64_t timeout)
{
    int ret;

    if (!io->is_write) {
        ret = RENAME(seek_to)(io, io->wpos);
        if (ret < 0) {
            ret = RENAME(handle_error)(io, "Error seeking: %s\n");
            return ret;
        }
        io->is_write = true;
    }

    size_t out = RENAME(write)(io, p->hdr, p->hdr_len, timeout);
    if (out != p->hdr_len) {
        ret = RENAME(handle_error)(io, "Error writing: %s\n");
        io->wpos = RENAME(offset)(io);
        return ret;
    }

    size_t pl_len;
    uint8_t *data = avt_buffer_get_data(&p->pl, &pl_len);
    if (data) {
        out = RENAME(write)(io, data, pl_len, timeout);
        if (out != pl_len) {
            ret = RENAME(handle_error)(io, "Error writing: %s\n");
            io->wpos = RENAME(offset)(io);
            return ret;
        }
    }

    return (io->wpos = RENAME(offset)(io));
}

[[maybe_unused]] static int64_t RENAME(write_vec)(AVTContext *ctx, AVTIOCtx *io,
                                                  AVTPktd *pkt, uint32_t nb_pkt,
                                                  int64_t timeout)
{
    int ret;

    if (!io->is_write) {
        ret = RENAME(seek_to)(io, io->wpos);
        if (ret < 0) {
            ret = RENAME(handle_error)(io, "Error seeking: %s\n");
            return ret;
        }
        io->is_write = true;
    }

    size_t out;
    size_t pl_len;
    uint8_t *pl_data;
    AVTPktd *v;
    for (auto i = 0; i < nb_pkt; i++) {
        v = &pkt[i];

        out = RENAME(write)(io, v->hdr, v->hdr_len, timeout);
        if (out != v->hdr_len) {
            ret = RENAME(handle_error)(io, "Error writing: %s\n");
            io->wpos = RENAME(offset)(io);
            return ret;
        }

        pl_data = avt_buffer_get_data(&v->pl, &pl_len);
        if (pl_data) {
            out = RENAME(write)(io, pl_data, pl_len, timeout);
            if (out != pl_len) {
                ret = RENAME(handle_error)(io, "Error writing: %s\n");
                io->wpos = RENAME(offset)(io);
                return ret;
            }
        }
    }

    return (io->wpos = RENAME(offset)(io));
}

[[maybe_unused]] static int64_t RENAME(rewrite)(AVTContext *ctx, AVTIOCtx *io,
                                                AVTPktd *p, int64_t off,
                                                int64_t timeout)
{
    int ret, ret2;

    if (off > io->wpos) {
        avt_log(io, AVT_LOG_ERROR, "Error rewriting: out of range: "
                "%" PRIu64 " req vs %" PRIu64 " max\n",
                off, io->wpos);
        return AVT_ERROR(EOF);
    }

    if (!io->is_write || (io->wpos != off)) {
        ret = RENAME(seek_to)(io, off);
        if (ret < 0) {
            ret = RENAME(handle_error)(io, "Error seeking: %s\n");
            return ret;
        }
        io->is_write = true;
    }

    size_t out = RENAME(write)(io, p->hdr, p->hdr_len, timeout);
    off += out;
    if (out != p->hdr_len) {
        ret = RENAME(handle_error)(io, "Error writing: %s\n");
        ret2 = RENAME(seek_to)(io, io->wpos);
        if (ret2 < 0) {
            ret = RENAME(handle_error)(io, "Error seeking: %s\n");
            io->wpos = off; /* Stuck with this */
            return ret;
        }
        return ret;
    }

    size_t pl_len;
    uint8_t *data = avt_buffer_get_data(&p->pl, &pl_len);
    if (data) {
        out = RENAME(write)(io, data, pl_len, timeout);
        off += out;
        if (out != pl_len) {
            ret = RENAME(handle_error)(io, "Error writing: %s\n");
            ret2 = RENAME(seek_to)(io, io->wpos);
            if (ret2 < 0) {
                ret = RENAME(handle_error)(io, "Error seeking: %s\n");
                io->wpos = off; /* Stuck with this */
                return ret;
            }
            return ret;
        }
    }

    /* Restore */
    ret = RENAME(seek_to)(io, io->wpos);
    if (ret < 0) {
        ret = RENAME(handle_error)(io, "Error seeking: %s\n");
        io->wpos = off; /* Stuck with this */
        return ret;
    }

    return off;
}
