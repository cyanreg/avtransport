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
    atomic_uint_least64_t seq;
    atomic_uint_least64_t pos_r;
    atomic_uint_least64_t pos_w;
};

static int null_init(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    AVTIOCtx *io = malloc(sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    atomic_store(&io->seq, 0);
    atomic_store(&io->pos_r, 0);
    atomic_store(&io->pos_w, 0);

    *_io = io;
    return 0;
}

static uint32_t null_max_pkt_len(AVTContext *ctx, AVTIOCtx *io)
{
    return UINT32_MAX;
}

static int null_add_dst(AVTContext *ctx, AVTIOCtx *io, AVTAddress *addr)
{
    return 0;
}

static int null_del_dst(AVTContext *ctx, AVTIOCtx *io, AVTAddress *addr)
{
    return 0;
}

static int64_t null_input(AVTContext *ctx, AVTIOCtx *io,
                          AVTBuffer **buf, size_t len)
{
    avt_assert0(!(*buf));
    AVTBuffer *hdr_buf = avt_buffer_alloc(AVT_MAX_HEADER_LEN);
    if (!hdr_buf)
        return AVT_ERROR(ENOMEM);

    uint8_t *hdr = avt_buffer_get_data(hdr_buf, NULL);
    AVTBytestream bs = avt_bs_init(hdr, AVT_MAX_HEADER_LEN);

    union AVTPacketData pkt = { .session_start = {
        .global_seq = atomic_fetch_add(&io->seq, 1ULL) & UINT32_MAX,
        .session_flags = 0x0,
        .producer_major = PROJECT_VERSION_MAJOR,
        .producer_minor = PROJECT_VERSION_MINOR,
        .producer_micro = PROJECT_VERSION_MICRO,
    }};
    memcpy(pkt.session_start.producer_name, PROJECT_NAME, strlen(PROJECT_NAME));

    avt_encode_session_start(&bs, pkt.session_start);

    *buf = hdr_buf;

    return atomic_fetch_add(&io->pos_r, avt_bs_offs(&bs)) + avt_bs_offs(&bs);
}

static int64_t null_output(AVTContext *ctx, AVTIOCtx *io, AVTPktd *p)
{
    return atomic_fetch_add(&io->pos_w,
                            p->hdr_len + avt_buffer_get_data_len(&p->pl));
}

static int64_t null_write_vec(AVTContext *ctx, AVTIOCtx *io,
                              AVTPktd *iov, uint32_t nb_iov)
{
    int64_t acc = 0;
    for (auto i = 0; i < nb_iov; i++)
        acc += iov[i].hdr_len + avt_buffer_get_data_len(&iov[i].pl);
    return atomic_fetch_add(&io->pos_w, acc);
}

static int64_t null_seek(AVTContext *ctx, AVTIOCtx *io, int64_t off)
{
    return atomic_load(&io->pos_r);
}

static int null_flush(AVTContext *ctx, AVTIOCtx *io)
{
    return 0;
}

static int null_close(AVTContext *ctx, AVTIOCtx **_io)
{
    AVTIOCtx *io = *_io;
    free(io);
    *_io = NULL;
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
    .write_output = null_output,
    .write_vec = null_write_vec,
    .seek = null_seek,
    .flush = null_flush,
    .close = null_close,
};
