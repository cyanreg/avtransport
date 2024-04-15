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

#include "utils_internal.h"

static int RENAME(max_pkt_len)(AVTIOCtx *io, size_t *mtu)
{
    *mtu = SIZE_MAX;
    return 0;
}

[[maybe_unused]] static avt_pos RENAME(seek)(AVTIOCtx *io, avt_pos off)
{
    avt_pos ret = RENAME(seek_to)(io, off);
    if (ret < 0)
        return avt_handle_errno(io, "Error seeking: %i %s\n");

    io->is_write = false;

    return (io->rpos = off);
}

static avt_pos RENAME(read_input)(AVTIOCtx *io, AVTBuffer *buf, size_t len,
                                  int64_t timeout, enum AVTIOReadFlags flags)
{
    int64_t ret;

    if (io->is_write) {
        ret = RENAME(seek_to)(io, io->rpos);
        if (ret < 0)
            return avt_handle_errno(io, "Error seeking: %i %s\n");
        io->is_write = false;
    }

    /* Read data */
    size_t buf_len;
    uint8_t *data = avt_buffer_get_data(buf, &buf_len);

    ret = RENAME(read)(io, data, len, timeout);
    if (ret < 0)
        return avt_handle_errno(io, "Error reading: %i %s\n");

    /* Adjust new size in case of underreads */
    [[maybe_unused]] int tmp = avt_buffer_resize(buf, ret);
    avt_assert2(tmp >= 0);

    ret = io->rpos + ret;
    AVT_SWAP(io->rpos, ret);
    return ret;
}

static avt_pos RENAME(write_pkt)(AVTIOCtx *io, AVTPktd *p, int64_t timeout)
{
    int64_t ret;

    if (!io->is_write) {
        ret = RENAME(seek_to)(io, io->wpos);
        if (ret < 0)
            return avt_handle_errno(io, "Error seeking: %i %s\n");
        io->is_write = true;
    }

    /* Write header */
    size_t out = RENAME(write)(io, p->hdr, p->hdr_len, timeout);
    if (out != p->hdr_len) {
        ret = avt_handle_errno(io, "Error writing: %i %s\n");
        io->wpos = RENAME(offset)(io);
        return ret;
    }

    /* Write payload */
    size_t pl_len;
    uint8_t *data = avt_buffer_get_data(&p->pl, &pl_len);
    if (data) {
        out = RENAME(write)(io, data, pl_len, timeout);
        if (out != pl_len) {
            ret = avt_handle_errno(io, "Error writing: %i %s\n");
            io->wpos = RENAME(offset)(io);
            return ret;
        }
    }

    ret = io->wpos + p->hdr_len + pl_len;
    AVT_SWAP(io->wpos, ret);
    return ret;
}

[[maybe_unused]] static avt_pos RENAME(write_vec)(AVTIOCtx *io, AVTPktd *pkt,
                                                  uint32_t nb_pkt, int64_t timeout)
{
    avt_pos ret;

    if (!io->is_write) {
        ret = RENAME(seek_to)(io, io->wpos);
        if (ret < 0)
            return avt_handle_errno(io, "Error seeking: %i %s\n");
        io->is_write = true;
    }

    avt_pos out;
    size_t pl_len;
    uint8_t *pl_data;
    AVTPktd *v;

    for (auto i = 0; i < nb_pkt; i++) {
        v = &pkt[i];

        /* Header */
        out = RENAME(write)(io, v->hdr, v->hdr_len, timeout);
        if (out != v->hdr_len) {
            ret = avt_handle_errno(io, "Error writing: %i %s\n");
            io->wpos = RENAME(offset)(io);
            return ret;
        }

        /* Payload */
        pl_data = avt_buffer_get_data(&v->pl, &pl_len);
        if (pl_data) {
            out = RENAME(write)(io, pl_data, pl_len, timeout);
            if (out != pl_len) {
                ret = avt_handle_errno(io, "Error writing: %i %s\n");
                io->wpos = RENAME(offset)(io);
                return ret;
            }
        }
    }

    out = RENAME(offset)(io);
    AVT_SWAP(io->wpos, out);
    return out;
}

[[maybe_unused]] static avt_pos RENAME(rewrite)(AVTIOCtx *io, AVTPktd *p,
                                                avt_pos off, int64_t timeout)
{
    int ret;
    const avt_pos backup_pos = io->is_write ? io->wpos : io->rpos;

    if (off > io->wpos) {
        avt_log(io, AVT_LOG_ERROR, "Error rewriting: out of range: "
                "%" PRIu64 " req vs %" PRIu64 " max\n",
                off, io->wpos);
        return AVT_ERROR(EOF);
    }

    if (backup_pos != off) {
        ret = RENAME(seek_to)(io, off);
        if (ret < 0) {
            ret = avt_handle_errno(io, "Error seeking: %i %s\n");
            return ret;
        }
    }

    avt_pos out = RENAME(write)(io, p->hdr, p->hdr_len, timeout);
    off += out;
    if (out != p->hdr_len) {
        ret = avt_handle_errno(io, "Error writing: %i %s\n");
        goto restore;
    }

    size_t pl_len;
    uint8_t *data = avt_buffer_get_data(&p->pl, &pl_len);
    if (data) {
        out = RENAME(write)(io, data, pl_len, timeout);
        off += out;
        if (out != pl_len) {
            ret = avt_handle_errno(io, "Error writing: %i %s\n");
            goto restore;
        }
    }

restore:

    /* Restore */
    ret = RENAME(seek_to)(io, backup_pos);
    if (ret < 0) {
        ret = avt_handle_errno(io, "Error seeking: %i %s\n");
        io->is_write = true;
        io->wpos = off; /* Stuck with this */
        return ret;
    }

    return off;
}
