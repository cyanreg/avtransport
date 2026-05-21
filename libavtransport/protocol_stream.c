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

#include <avtransport/avtransport.h>
#include "packet_decode.h"
#include "protocol_common.h"
#include "io_common.h"
#include "bytestream.h"
#include "ldpc_decode.h"

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;
    AVTProtocolOpts opts;

    AVTIndexContext ic;
};

static COLD int stream_proto_close(AVTProtocolCtx **_p)
{
    AVTProtocolCtx *p = *_p;
    free(p);
    *_p = NULL;
    return 0;
}

static COLD int stream_init(AVTContext *ctx, AVTProtocolCtx **_p, AVTAddress *addr,
                            const AVTIO *io, AVTIOCtx *io_ctx, AVTProtocolOpts *opts)
{
    AVTProtocolCtx *p = malloc(sizeof(*p));
    if (!p)
        return AVT_ERROR(ENOMEM);

    p->io = io;
    p->io_ctx = io_ctx;
    p->opts = *opts;

    *_p = p;

    return 0;
}

static int stream_add_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->add_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->add_dst(p->io_ctx, addr);
}

static int stream_rm_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->del_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->del_dst(p->io_ctx, addr);
}

static int stream_proto_send_packet(AVTProtocolCtx *p, AVTPktd *pkt,
                                    int64_t timeout)
{
    int ret = p->io->write_pkt(p->io_ctx, pkt, timeout);
    if (ret < 0)
        return ret;
    return 0;
}

static int stream_send_seq(AVTProtocolCtx *p,
                           AVTPacketFifo *seq, int64_t timeout)
{
    int ret = p->io->write_vec(p->io_ctx, seq->data, seq->nb, timeout);
    if (ret < 0)
        return ret;
    return 0;
}

static int stream_receive(AVTProtocolCtx *s, AVTPacketFifo *fifo,
                          int64_t timeout)
{
    int64_t err = 0;
    AVTPktd *p = avt_pkt_fifo_push_new(fifo, NULL, 0, AVT_BUFFER_REF_ALL);
    if (!p)
        return AVT_ERROR(ENOMEM);

    AVTBuffer buf = {
        .base_data = p->hdr,
        .data = p->hdr,
        .end_data = &p->hdr[sizeof(p->hdr)],
        .len = sizeof(p->hdr),
    };

    /* Get the minimum header size */
    err = s->io->read_input(s->io_ctx, &buf, AVT_MIN_HEADER_LEN,
                            timeout, AVT_IO_READ_MUTABLE);
    if (err < 0)
        return err;

    /* Check LDPC codes */
    avt_ldpc_decode_288_224(buf.data, s->opts.ldpc_iterations);

    uint16_t desc = AVT_RB16(&buf.data[0]);

    /* For identification purposes, zero out any bitmask bits.
     * Stream data and time sync descriptors carry low-byte flag fields. */
    if (((desc & 0xFF00) == (AVT_PKT_TIME_SYNC & 0xFF00)) ||
        ((desc & 0xFF00) == (AVT_PKT_STREAM_DATA & 0xFF00)))
        desc &= 0xFF00;

    /* Advance past the minimum header */
    buf.data += AVT_MIN_HEADER_LEN;
    buf.len -= AVT_MIN_HEADER_LEN;

    /* Get the rest of the header, if any */
    const int left_size = avt_pkt_hdr_size(desc) - AVT_MIN_HEADER_LEN;
    if (left_size > 0) {
        err = s->io->read_input(s->io_ctx, &buf, left_size,
                                timeout, AVT_IO_READ_MUTABLE);
        if (err < 0)
            return err;

        /* Check LDPC codes */
        switch (left_size) {
        case 36: avt_ldpc_decode_288_224(buf.data, s->opts.ldpc_iterations);
            break;
        case 384: avt_ldpc_decode_2784_2016(buf.data, s->opts.ldpc_iterations);
            break;
        default:
            break;
        }

        buf.data += left_size;
        buf.len -= left_size;
    }

    p->hdr_len = buf.data - buf.base_data;

    size_t pl_bytes = 0;
    int dret = avt_packet_decode(p, desc, &pl_bytes);
    if (dret < 0) {
        if (dret == AVT_ERROR(ENOTSUP))
            avt_log(p, AVT_LOG_ERROR, "Unknown descriptor 0x%x received\n", desc);
        return dret;
    }

    if (dret == 1) {
        /* Stream index: read and parse the index entries. */
        // TODO: store buffer in state
        AVTBuffer *tmp = avt_buffer_alloc(pl_bytes);
        if (!tmp)
            return AVT_ERROR(ENOMEM);

        err = s->io->read_input(s->io_ctx, tmp, pl_bytes, timeout, 0x0);
        if (err < 0) {
            avt_buffer_unref(&tmp);
            return err;
        }

        size_t index_size;
        uint8_t *index_data = avt_buffer_get_data(tmp, &index_size);
        AVTBytestream bs = avt_bs_init(index_data, index_size);

        err = avt_index_list_parse(&s->ic, &bs, &p->pkt.stream_index);
        avt_buffer_unref(&tmp);
        if (err < 0)
            return err;

        /* Bypass reordering */
        return AVT_ERROR(EAGAIN);
    }

    if (!pl_bytes)
        return 0;

    // TODO: pool buffer
    if (!avt_buffer_quick_alloc(&p->pl, pl_bytes))
        return AVT_ERROR(ENOMEM);

    err = s->io->read_input(s->io_ctx, &p->pl, pl_bytes, timeout, 0x0);
    if (err < 0)
        return err;

    /* If there's not enough data, we'll try to do what we can with what
     * we get down the road */

    return 0;
}

static int stream_proto_max_pkt_len(AVTProtocolCtx *p, size_t *mtu)
{
    return p->io->get_max_pkt_len(p->io_ctx, mtu);
}

static int stream_proto_seek(AVTProtocolCtx *p,
                             int64_t off, uint32_t seq,
                             int64_t ts, bool ts_is_dts)
{
    if (!p->io->seek)
        return AVT_ERROR(ENOTSUP);

    /* TODO: verify there's a packet there, use the indices in ts/seq mode */
    p->io->seek(p->io_ctx, off);
    /* Patch out the index if we succeed with the returned value */

    return AVT_ERROR(ENOTSUP);
}

static int stream_proto_flush(AVTProtocolCtx *p, int64_t timeout)
{
    if (p->io->flush)
        return p->io->flush(p->io_ctx, timeout);
    return AVT_ERROR(ENOTSUP);
}

const AVTProtocol avt_protocol_stream = {
    .name = "stream",
    .type = AVT_PROTOCOL_STREAM,
    .init = stream_init,
    .add_dst = stream_add_dst,
    .rm_dst = stream_rm_dst,
    .get_max_pkt_len = stream_proto_max_pkt_len,
    .send_packet = stream_proto_send_packet,
    .send_seq = stream_send_seq,
    .update_packet = NULL,
    .receive = stream_receive,
    .seek = stream_proto_seek,
    .flush = stream_proto_flush,
    .close = stream_proto_close,
};
