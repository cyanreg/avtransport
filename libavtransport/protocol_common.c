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

#include <avtransport/avtransport.h>
#include "packet_decode.h"
#include "protocol_common.h"
#include "mem.h"

extern const AVTProtocol avt_protocol_datagram;
extern const AVTProtocol avt_protocol_stream;
extern const AVTProtocol avt_protocol_pkt;

#ifdef CONFIG_HAVE_OPENSSL
extern const AVTProtocol avt_protocol_quic;
#endif

static const AVTProtocol *avt_protocol_list[AVT_PROTOCOL_MAX] = {
    [AVT_PROTOCOL_DATAGRAM]     = &avt_protocol_datagram,
    [AVT_PROTOCOL_UDP]          = &avt_protocol_datagram,
    [AVT_PROTOCOL_STREAM]       = &avt_protocol_stream,
    [AVT_PROTOCOL_FILE]         = &avt_protocol_stream,
#ifdef CONFIG_HAVE_OPENSSL
    [AVT_PROTOCOL_QUIC]         = &avt_protocol_quic,
#endif
//    [AVT_PROTOCOL_CALLBACK_PKT] = &avt_protocol_pkt,
};

/* For connections to call */
int avt_protocol_init(AVTContext *ctx, const AVTProtocol **_p,
                      AVTProtocolCtx **p_ctx, AVTAddress *addr,
                      const AVTIO *io, AVTIOCtx *io_ctx, AVTProtocolOpts *opts)
{
    const AVTProtocol *p = avt_protocol_list[addr->proto];
    if (!p) {
        avt_log(ctx, AVT_LOG_ERROR, "No support for protocol #%i\n",
                addr->proto);
        return AVT_ERROR(ENOTSUP);
    }

    int err = p->init(ctx, p_ctx, addr, io, io_ctx, opts);
    if (err >= 0)
        *_p = p;

    return err;
}

int avt_packet_decode(AVTPktd *p, uint16_t desc, size_t *pl_bytes)
{
    AVTBytestream bs = avt_bs_init(p->hdr, sizeof(p->hdr));
    *pl_bytes = 0;

    switch (desc) {
    case AVT_PKT_SESSION_START:
        avt_decode_session_start(&bs, &p->pkt.session_start);
        return 0;
    case AVT_PKT_TIME_SYNC & ~(AVT_PKT_FLAG_LSB_BITMASK):
        avt_decode_time_sync(&bs, &p->pkt.time_sync);
        return 0;
    case AVT_PKT_VIDEO_INFO:
        avt_decode_video_info(&bs, &p->pkt.video_info);
        return 0;
    case AVT_PKT_VIDEO_ORIENTATION:
        avt_decode_video_orientation(&bs, &p->pkt.video_orientation);
        return 0;
    case AVT_PKT_STREAM_REGISTRATION:
        avt_decode_stream_registration(&bs, &p->pkt.stream_registration);
        return 0;
    case AVT_PKT_STEREO_VIDEO:
        avt_decode_stereo_video(&bs, &p->pkt.stereo_video);
        return 0;
    case AVT_PKT_STREAM_END:
        avt_decode_stream_end(&bs, &p->pkt.stream_end);
        return 0;
    case AVT_PKT_STREAM_INDEX:
        avt_decode_stream_index(&bs, &p->pkt.stream_index);
        *pl_bytes = p->pkt.stream_index.nb_indices * AVT_PKT_INDEX_ENTRY_SIZE;
        return 1;
    case AVT_PKT_STREAM_DATA & ~(AVT_PKT_FLAG_LSB_BITMASK):
        avt_decode_stream_data(&bs, &p->pkt.stream_data);
        *pl_bytes = p->pkt.stream_data.data_length;
        return 0;
    case AVT_PKT_FEC_GROUPING:
        avt_decode_fec_grouping(&bs, &p->pkt.fec_grouping);
        return 0;
    case AVT_PKT_FEC_GROUP_DATA:
        avt_decode_fec_group_data(&bs, &p->pkt.fec_group_data);
        *pl_bytes = p->pkt.fec_group_data.fec_data_length;
        return 0;
    case AVT_PKT_LUT_ICC:
        avt_decode_lut_icc(&bs, &p->pkt.lut_icc);
        *pl_bytes = p->pkt.lut_icc.lut_pl_length;
        return 0;
    case AVT_PKT_FONT_DATA:
        avt_decode_font_data(&bs, &p->pkt.font_data);
        *pl_bytes = p->pkt.font_data.font_pl_length;
        return 0;
    case AVT_PKT_USER_DATA:
        avt_decode_user_data(&bs, &p->pkt.user_data);
        *pl_bytes = p->pkt.user_data.userdata_pl_length;
        return 0;
    case AVT_PKT_STREAM_CONFIG: [[fallthrough]];
    case AVT_PKT_METADATA:
        avt_decode_generic_data(&bs, &p->pkt.generic_data);
        *pl_bytes = p->pkt.generic_data.payload_length;
        return 0;
    case AVT_PKT_LUT_ICC_SEGMENT:       [[fallthrough]];
    case AVT_PKT_FONT_DATA_SEGMENT:     [[fallthrough]];
    case AVT_PKT_METADATA_SEGMENT:      [[fallthrough]];
    case AVT_PKT_USER_DATA_SEGMENT:     [[fallthrough]];
    case AVT_PKT_STREAM_DATA_SEGMENT:   [[fallthrough]];
    case AVT_PKT_STREAM_CONFIG_SEGMENT:
        avt_decode_generic_segment(&bs, &p->pkt.generic_segment);
        *pl_bytes = p->pkt.generic_segment.seg_length;
        return 0;
    case AVT_PKT_LUT_ICC_PARITY:       [[fallthrough]];
    case AVT_PKT_FONT_DATA_PARITY:     [[fallthrough]];
    case AVT_PKT_METADATA_PARITY:      [[fallthrough]];
    case AVT_PKT_USER_DATA_PARITY:     [[fallthrough]];
    case AVT_PKT_STREAM_DATA_PARITY:   [[fallthrough]];
    case AVT_PKT_STREAM_CONFIG_PARITY:
        avt_decode_generic_parity(&bs, &p->pkt.generic_parity);
        *pl_bytes = p->pkt.generic_parity.parity_data_length;
        return 0;
    default:
        return AVT_ERROR(ENOTSUP);
    }
}

int avt_index_list_parse(AVTIndexContext *ic, AVTBytestream *bs,
                         AVTStreamIndex *pkt)
{
    for (auto i = 0; i < pkt->nb_indices; i++) {
        auto dst = ic->nb_index;
        if (ic->nb_index >= ic->nb_index_max) {
            dst = ic->nb_index_total % ic->nb_index_max;
        } else if ((ic->nb_index + 1) > ic->nb_alloc_index) {
            AVTIndexEntry *tmp = avt_reallocarray(ic->index,
                                                  ic->nb_alloc_index + 1,
                                                  sizeof(*tmp));
            if (!tmp)
                return AVT_ERROR(ENOMEM);

            ic->index = tmp;
            ic->nb_index++;
            ic->nb_alloc_index++;
        }

        avt_decode_index_entry(bs, &ic->index[dst]);
        ic->nb_index_total++;
    }

    return 0;
}
