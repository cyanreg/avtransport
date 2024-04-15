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
#include <errno.h>

#include "io_common.h"
#include "utils_internal.h"
#include "bytestream.h"
#include "packet_encode.h"

#include "config.h"

struct AVTIOCtx {
    uint64_t seq;
    avt_pos rpos;
    avt_pos wpos;
};

static int null_close(AVTIOCtx **_io)
{
    AVTIOCtx *io = *_io;
    free(io);
    *_io = NULL;
    return 0;
}

static int null_init(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    AVTIOCtx *io = calloc(1, sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    *_io = io;
    return 0;
}

static int null_max_pkt_len(AVTIOCtx *io, size_t *mtu)
{
    *mtu = SIZE_MAX;
    return 0;
}

static int null_add_dst(AVTIOCtx *io, AVTAddress *addr)
{
    return 0;
}

static int null_del_dst(AVTIOCtx *io, AVTAddress *addr)
{
    return 0;
}

static avt_pos null_input(AVTIOCtx *io, AVTBuffer *buf, size_t len,
                          int64_t timeout, enum AVTIOReadFlags flags)
{
    avt_assert1(len == AVT_MAX_HEADER_LEN);
    uint8_t *data = avt_buffer_get_data(buf, NULL);
    AVTBytestream bs = avt_bs_init(data, AVT_MAX_HEADER_LEN);

    union AVTPacketData pkt = { .session_start = {
        .global_seq = io->seq++,
        .session_flags = 0x0,
        .producer_major = PROJECT_VERSION_MAJOR,
        .producer_minor = PROJECT_VERSION_MINOR,
        .producer_micro = PROJECT_VERSION_MICRO,
    }};
    memcpy(pkt.session_start.producer_name, PROJECT_NAME, strlen(PROJECT_NAME));

    avt_encode_session_start(&bs, pkt.session_start);

    avt_pos tmp = io->rpos + avt_bs_offs(&bs);
    AVT_SWAP(io->rpos, tmp);
    return tmp;
}

static avt_pos null_write_vec(AVTIOCtx *io, AVTPktd *iov, uint32_t nb_iov,
                              int64_t timeout)
{
    avt_pos acc = 0;
    for (auto i = 0; i < nb_iov; i++)
        acc += iov[i].hdr_len + avt_buffer_get_data_len(&iov[i].pl);

    acc = io->wpos + acc;
    AVT_SWAP(io->wpos, acc);
    return acc;
}

static avt_pos null_write_pkt(AVTIOCtx *io, AVTPktd *p, int64_t timeout)
{
    avt_pos prev = io->wpos;
    io->wpos += p->hdr_len + avt_buffer_get_data_len(&p->pl);
    return prev;
}

static avt_pos null_rewrite(AVTIOCtx *io, AVTPktd *p, avt_pos off,
                            int64_t timeout)
{
    return off;
}

static avt_pos null_seek(AVTIOCtx *io, avt_pos off)
{
    io->rpos = off;
    return off;
}

static int null_flush(AVTIOCtx *io, int64_t timeout)
{
    return 0;
}

const AVTIO avt_io_null = {
    .name = "null",
    .type = AVT_IO_NULL,
    .init = null_init,
    .get_max_pkt_len = null_max_pkt_len,
    .add_dst = null_add_dst,
    .del_dst = null_del_dst,
    .read_input = null_input,
    .write_vec = null_write_vec,
    .write_pkt = null_write_pkt,
    .rewrite = null_rewrite,
    .seek = null_seek,
    .flush = null_flush,
    .close = null_close,
};
