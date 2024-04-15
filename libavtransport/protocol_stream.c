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

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;
    AVTBuffer *hdr;

    AVTIndexContext ic;
};

static COLD int stream_proto_close(AVTProtocolCtx **_p)
{
    AVTProtocolCtx *p = *_p;
    avt_buffer_unref(&p->hdr);
    free(p);
    *_p = NULL;
    return 0;
}

static COLD int stream_init(AVTContext *ctx, AVTProtocolCtx **_p, AVTAddress *addr,
                            const AVTIO *io, AVTIOCtx *io_ctx)
{
    AVTProtocolCtx *p = malloc(sizeof(*p));
    if (!p)
        return AVT_ERROR(ENOMEM);

    p->io = io;
    p->io_ctx = io_ctx;
    p->hdr = avt_buffer_alloc(AVT_MAX_HEADER_LEN);
    if (!p->hdr) {
        stream_proto_close(&p);
        return AVT_ERROR(ENOMEM);
    }

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

static int stream_receive_packet(AVTProtocolCtx *p,
                                 union AVTPacketData *pkt, AVTBuffer **pl,
                                 int64_t timeout)
{
    int64_t err = 0;

    uint8_t hdr_buf[AVT_MAX_HEADER_LEN];
    AVTBuffer buf = {
        .base_data = hdr_buf,
        .data = hdr_buf,
        .end_data = &hdr_buf[AVT_MAX_HEADER_LEN],
        .len = AVT_MAX_HEADER_LEN,
    };

    /* Get the minimum header size */
    err = p->io->read_input(p->io_ctx, &buf, AVT_MIN_HEADER_LEN,
                            timeout, AVT_IO_READ_MUTABLE);
    if (err < 0)
        return err;

    /* TODO: part 1 of header FEC would happen here */

    const uint16_t desc = AVT_RB16(&hdr_buf[0]);

    /* Get the rest of the header */
    const int left_size = avt_pkt_hdr_size(desc) - AVT_MIN_HEADER_LEN;
    if (left_size) {
        buf.data += AVT_MIN_HEADER_LEN;
        buf.len -= AVT_MIN_HEADER_LEN;

        err = p->io->read_input(p->io_ctx, &buf, left_size,
                                timeout, AVT_IO_READ_MUTABLE);
        if (err < 0)
            return err;

        /* TODO: part 2 of header FEC would happen here */

        buf.data += AVT_MIN_HEADER_LEN;
        buf.len -= AVT_MIN_HEADER_LEN;
    }

    size_t pl_bytes = 0;
    AVTBytestream bs = avt_bs_init(buf.base_data, buf.data - buf.base_data);

    switch (desc) {
    case AVT_PKT_SESSION_START:
        avt_decode_session_start(&bs, &pkt->session_start);
        return 0;
    case AVT_PKT_TIME_SYNC & ~(AVT_PKT_FLAG_LSB_BITMASK):
        avt_decode_time_sync(&bs, &pkt->time_sync);
        return 0;
    case AVT_PKT_VIDEO_INFO:
        avt_decode_video_info(&bs, &pkt->video_info);
        return 0;
    case AVT_PKT_VIDEO_ORIENTATION:
        avt_decode_video_orientation(&bs, &pkt->video_orientation);
        return 0;
    case AVT_PKT_STREAM_REGISTRATION:
        avt_decode_stream_registration(&bs, &pkt->stream_registration);
        return 0;
    case AVT_PKT_STREAM_END:
        avt_decode_stream_end(&bs, &pkt->stream_end);
        return 0;
    case AVT_PKT_STREAM_INDEX:
        avt_decode_stream_index(&bs, &pkt->stream_index);
        pl_bytes = pkt->stream_index.nb_indices * AVT_PKT_INDEX_ENTRY_SIZE;

        /* Read index entries */
        buf.data = buf.base_data;
        buf.len = buf.end_data - buf.base_data;
        err = p->io->read_input(p->io_ctx, &buf, pl_bytes,
                                timeout, AVT_IO_READ_MUTABLE);
        if (err < 0)
            return err;

        bs = avt_bs_init(buf.base_data, buf.data - buf.base_data);

        /* Parse */
        err = avt_index_list_parse(&p->ic, &bs, &pkt->stream_index);
        if (err < 0)
            return err;

        return AVT_ERROR(EAGAIN);
    case AVT_PKT_STREAM_DATA & ~(AVT_PKT_FLAG_LSB_BITMASK):
        avt_decode_stream_data(&bs, &pkt->stream_data);
        pl_bytes = pkt->stream_data.data_length;
        break;
    case AVT_PKT_LUT_ICC:       [[fallthrough]];
    case AVT_PKT_FONT_DATA:     [[fallthrough]];
    case AVT_PKT_METADATA:      [[fallthrough]];
    case AVT_PKT_USER_DATA:     [[fallthrough]];
    case AVT_PKT_STREAM_CONFIG:
        avt_decode_generic_data(&bs, &pkt->generic_data);
        pl_bytes = pkt->generic_data.payload_length;
        break;
    case AVT_PKT_LUT_ICC_SEGMENT:       [[fallthrough]];
    case AVT_PKT_FONT_DATA_SEGMENT:     [[fallthrough]];
    case AVT_PKT_METADATA_SEGMENT:      [[fallthrough]];
    case AVT_PKT_USER_DATA_SEGMENT:     [[fallthrough]];
    case AVT_PKT_STREAM_DATA_SEGMENT:   [[fallthrough]];
    case AVT_PKT_STREAM_CONFIG_SEGMENT:
        avt_decode_generic_segment(&bs, &pkt->generic_segment);
        pl_bytes = pkt->generic_segment.seg_length;
        break;
    case AVT_PKT_LUT_ICC_PARITY:       [[fallthrough]];
    case AVT_PKT_FONT_DATA_PARITY:     [[fallthrough]];
    case AVT_PKT_METADATA_PARITY:      [[fallthrough]];
    case AVT_PKT_USER_DATA_PARITY:     [[fallthrough]];
    case AVT_PKT_STREAM_DATA_PARITY:   [[fallthrough]];
    case AVT_PKT_STREAM_CONFIG_PARITY:
        avt_decode_generic_parity(&bs, &pkt->generic_parity);
        pl_bytes = pkt->generic_parity.parity_data_length;
        break;
    default:
        avt_log(p, AVT_LOG_ERROR, "Unknown descriptor 0x%x received\n", desc);
        return AVT_ERROR(ENOTSUP);
    };

//    err = p->io->read_input(p->io_ctx, pl, pl_bytes, timeout, 0x0);
//    if (err < 0)
//        return err;

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
    .receive_packet = stream_receive_packet,
    .seek = stream_proto_seek,
    .flush = stream_proto_flush,
    .close = stream_proto_close,
};
