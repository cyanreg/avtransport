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
#include "protocol_common.h"
#include "io_common.h"

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;
};

static COLD int datagram_proto_close(AVTProtocolCtx **p)
{
    AVTProtocolCtx *priv = *p;
    free(priv);
    *p = NULL;
    return 0;
}

static COLD int datagram_proto_init(AVTContext *ctx, AVTProtocolCtx **_p, AVTAddress *addr,
                                    const AVTIO *io, AVTIOCtx *io_ctx)
{
    AVTProtocolCtx *p = malloc(sizeof(*p));
    if (!p)
        return AVT_ERROR(ENOMEM);

    p->io = io;
    p->io_ctx = io_ctx;
    *_p = p;

    return 0;
}

static int datagram_proto_add_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->add_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->add_dst(p->io_ctx, addr);
}

static int datagram_proto_rm_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->del_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->del_dst(p->io_ctx, addr);
}

static int datagram_proto_send_packet(AVTProtocolCtx *p, AVTPktd *pkt,
                                      int64_t timeout)
{
    int64_t ret = p->io->write_pkt(p->io_ctx, pkt, timeout);
    if (ret < 0)
        return ret;
    return 0;
}

static int datagram_proto_send_seq(AVTProtocolCtx *p, AVTPacketFifo *seq,
                                   int64_t timeout)
{
    int64_t ret = p->io->write_vec(p->io_ctx, seq->data, seq->nb, timeout);
    if (ret < 0)
        return ret;
    return 0;
}

static int datagram_proto_receive_packet(AVTProtocolCtx *p,
                                         union AVTPacketData *pkt, AVTBuffer **pl,
                                         int64_t timeout)
{
#if 0
    AVTBuffer *buf;
    int64_t err = p->io->read_input(p->io_ctx, &buf, 0, timeout);
    if (err < 0)
        return err;

    size_t size;
    uint8_t *data = avt_buffer_get_data(buf, &size);



    size_t pl_bytes = 0;
    AVTBytestream bs = avt_bs_init(data, size);

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



    return err;
#endif
    return 0;
}

static int datagram_proto_max_pkt_len(AVTProtocolCtx *p, size_t *mtu)
{
    size_t tmp;
    const size_t udp_hdr_size = 8;

    int ret = p->io->get_max_pkt_len(p->io_ctx, &tmp);
    if (ret < 0 || (tmp < (AVT_MIN_HEADER_LEN - udp_hdr_size)))
        return ret;

    *mtu = tmp - udp_hdr_size;

    return 0;
}

static int datagram_proto_flush(AVTProtocolCtx *p, int64_t timeout)
{
    if (p->io->flush)
        return p->io->flush(p->io_ctx, timeout);
    return AVT_ERROR(ENOTSUP);
}

const AVTProtocol avt_protocol_datagram = {
    .name = "datagram",
    .type = AVT_PROTOCOL_DATAGRAM,
    .init = datagram_proto_init,
    .add_dst = datagram_proto_add_dst,
    .rm_dst = datagram_proto_rm_dst,
    .get_max_pkt_len = datagram_proto_max_pkt_len,
    .send_packet = datagram_proto_send_packet,
    .send_seq = datagram_proto_send_seq,
    .update_packet = NULL,
    .receive_packet = datagram_proto_receive_packet,
    .seek = NULL,
    .flush = datagram_proto_flush,
    .close = datagram_proto_close,
};
