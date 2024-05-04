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

#ifndef AVTRANSPORT_UTILS_PACKET_H
#define AVTRANSPORT_UTILS_PACKET_H

#include <avtransport/packet_enums.h>
#include <avtransport/packet_data.h>
#include "utils_internal.h"
#include "packet_encode.h"

static inline union AVTPacketData avt_packet_create_segment(AVTPktd *p,
                                                            uint64_t seq,
                                                            uint32_t seg_offset,
                                                            uint32_t seg_length,
                                                            uint32_t tot_pl_size)
{
    switch (p->pkt.desc) {
    case AVT_PKT_STREAM_DATA:
        uint8_t *os = &p->hdr[p->hdr_off + (seq % 7)*4];
        return AVT_GENERIC_SEGMENT_HDR(AVT_PKT_STREAM_DATA_SEGMENT,
            .global_seq = seq,
            .stream_id = p->pkt.stream_id,
            .target_seq = p->pkt.seq,
            .pkt_total_data = tot_pl_size,
            .seg_offset = seg_offset,
            .seg_length = seg_length,
            .header_7 = { os[0], os[1], os[2], os[3] },
        );
    default:
        avt_assert1(0);
        unreachable();
    }
}

static inline void avt_packet_encode_header(AVTPktd *p)
{
    AVTBytestream bs = avt_bs_init(&p->hdr[p->hdr_off], (AVT_MAX_HEADER_LEN - p->hdr_off));

    switch (p->pkt.desc) {
    case AVT_PKT_SESSION_START:
        avt_encode_session_start(&bs, p->pkt.session_start);
        break;
    case AVT_PKT_STREAM_REGISTRATION:
        avt_encode_stream_registration(&bs, p->pkt.stream_registration);
        break;
    case AVT_PKT_VIDEO_INFO:
        avt_encode_video_info(&bs, p->pkt.video_info);
        break;
    case AVT_PKT_LUT_ICC:
        avt_encode_lut_icc(&bs, p->pkt.lut_icc);
        break;
    case AVT_PKT_FONT_DATA:
        avt_encode_font_data(&bs, p->pkt.font_data);
        break;
    case AVT_PKT_STREAM_DATA:
        avt_encode_stream_data(&bs, p->pkt.stream_data);
        break;
    case AVT_PKT_USER_DATA:
        avt_encode_user_data(&bs, p->pkt.user_data);
        break;
    case AVT_PKT_STREAM_INDEX:
        avt_encode_stream_index(&bs, p->pkt.stream_index);
        break;
    case AVT_PKT_METADATA_SEGMENT:    [[fallthrough]];
    case AVT_PKT_FONT_DATA_SEGMENT:   [[fallthrough]];
    case AVT_PKT_STREAM_DATA_SEGMENT: [[fallthrough]];
    case AVT_PKT_USER_DATA_SEGMENT:
        avt_encode_generic_segment(&bs, p->pkt.generic_segment);
        break;
    default:
        avt_assert1(0);
        unreachable();
    }

    p->hdr_len = avt_bs_offs(&bs);
}

#define RENAME(x) x ## _d
#define GET(x) p->pkt.x
#define TYPE AVTPktd *
#include "utils_packet_template.h"
#undef RENAME
#undef GET
#undef TYPE

#define RENAME(x) x ## _p
#define GET(x) p.x
#define TYPE union AVTPacketData
#include "utils_packet_template.h"
#undef RENAME
#undef GET
#undef TYPE

#define RENAME(x) x ## _pp
#define GET(x) p->x
#define TYPE union AVTPacketData *
#include "utils_packet_template.h"
#undef RENAME
#undef GET
#undef TYPE

#define avt_packet_get_duration(x, ...)                                \
    _Generic((x),                                                      \
             AVTPktd *: avt_packet_get_duration_d,                     \
             const AVTPktd *: avt_packet_get_duration_d,               \
             union AVTPacketData: avt_packet_get_duration_p,           \
             union AVTPacketData *: avt_packet_get_duration_pp,        \
             const union AVTPacketData *: avt_packet_get_duration_pp   \
    ) (x __VA_OPT__(,) __VA_ARGS__)

#define avt_packet_get_pts(x, ...)                                \
    _Generic((x),                                                 \
             AVTPktd *: avt_packet_get_pts_d,                     \
             const AVTPktd *: avt_packet_get_pts_d,               \
             union AVTPacketData: avt_packet_get_pts_p,           \
             union AVTPacketData *: avt_packet_get_pts_pp,        \
             const union AVTPacketData *: avt_packet_get_pts_pp   \
    ) (x __VA_OPT__(,) __VA_ARGS__)

#define avt_packet_series(x, ...)                                 \
    _Generic((x),                                                 \
             AVTPktd *: avt_packet_series_d,                      \
             const AVTPktd *: avt_packet_series_d,                \
             union AVTPacketData: avt_packet_series_p,            \
             union AVTPacketData *: avt_packet_series_pp,         \
             const union AVTPacketData *: avt_packet_series_pp    \
    ) (x __VA_OPT__(,) __VA_ARGS__)

#define avt_packet_get_tb(x, ...)                                 \
    _Generic((x),                                                 \
             AVTPktd *: avt_packet_get_tb_d,                      \
             const AVTPktd *: avt_packet_get_tb_d,                \
             union AVTPacketData: avt_packet_get_tb_p,            \
             union AVTPacketData *: avt_packet_get_tb_pp,         \
             const union AVTPacketData *: avt_packet_get_tb_pp    \
    ) (x __VA_OPT__(,) __VA_ARGS__)

#define avt_packet_set_compression(x, ...)                        \
    _Generic((x),                                                 \
             AVTPktd *: avt_packet_set_compression_d,             \
             union AVTPacketData: avt_packet_set_compression_p,   \
             union AVTPacketData *: avt_packet_set_compression_pp \
    ) (x __VA_OPT__(,) __VA_ARGS__)

#define avt_packet_change_size(x, ...)                        \
    _Generic((x),                                             \
             AVTPktd *: avt_packet_change_size_d,             \
             union AVTPacketData: avt_packet_change_size_p,   \
             union AVTPacketData *: avt_packet_change_size_pp \
    ) (x __VA_OPT__(,) __VA_ARGS__)

#endif /* AVTRANSPORT_UTILS_PACKET_H */
