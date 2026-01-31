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

static inline int64_t RENAME(avt_packet_get_duration)(const TYPE p)
{
    int desc = GET(desc);
    if (desc & AVT_PKT_FLAG_LSB_BITMASK)
        desc = (desc & 0xF) << 8;
    switch (desc) {
    case AVT_PKT_STREAM_DATA:
        return GET(stream_data.duration);
    default:
        return INT64_MIN;
    }
}

static inline int64_t RENAME(avt_packet_get_pts)(const TYPE p)
{
    switch (GET(desc)) {
    case AVT_PKT_STREAM_DATA:
        return GET(stream_data.pts);
    default:
        return INT64_MIN;
    }
}

static inline int RENAME(avt_packet_get_tb)(const TYPE p, AVTRational *tb)
{
    switch (GET(desc)) {
    case AVT_PKT_STREAM_REGISTRATION:
        *tb = GET(stream_registration.timebase);
        return 0;
    default:
        return AVT_ERROR(EINVAL);
    }
}

static inline void RENAME(avt_packet_set_compression)(TYPE p,
                                                      enum AVTDataCompression compression)
{
    switch (GET(desc)) {
    case AVT_PKT_STREAM_DATA:
        GET(stream_data).pkt_compression = compression;
        return;
    case AVT_PKT_STREAM_CONFIG: [[fallthrough]];
    case AVT_PKT_METADATA:
        GET(generic_data).generic_data_compression = compression;
        return;
    case AVT_PKT_USER_DATA:
        GET(user_data).userdata_compression = compression;
        return;
    case AVT_PKT_LUT_ICC:
        GET(lut_icc).lut_compression = compression;
        return;
    case AVT_PKT_FONT_DATA:
        GET(font_data).font_compression = compression;
        return;
    default:
        avt_assert1(0);
        unreachable();
    }
}

static inline int RENAME(avt_packet_get_target)(const TYPE p, uint64_t tgt)
{
    switch (GET(desc)) {
    default:

        return 0;
    }
}

static inline int RENAME(avt_packet_series)(const TYPE p, bool *is_parity,
                                            uint32_t *off,
                                            uint32_t *cur,
                                            uint32_t *tot)
{
    switch (GET(desc)) {
    case AVT_PKT_STREAM_DATA:
        *cur = GET(stream_data).data_length;
        *tot = !GET(stream_data).pkt_segmented ? GET(stream_data).data_length : 0;
        *off = 0;
        *is_parity = false;
        return GET(stream_data).pkt_segmented;
    case AVT_PKT_STREAM_CONFIG: [[fallthrough]];
    case AVT_PKT_METADATA:
        *cur = GET(generic_data).payload_length;
        *tot = GET(generic_data).total_payload_length;
        *off = 0;
        *is_parity = false;
        return GET(generic_data).payload_length != GET(generic_data).total_payload_length;
    case AVT_PKT_USER_DATA:
        *cur = GET(user_data).userdata_pl_length;
        *tot = GET(user_data).userdata_length;
        *off = 0;
        *is_parity = false;
        return GET(user_data).userdata_pl_length != GET(user_data).userdata_length;
    case AVT_PKT_LUT_ICC:
        *cur = GET(lut_icc).lut_pl_length;
        *tot = GET(lut_icc).lut_data_length;
        *off = 0;
        *is_parity = false;
        return GET(lut_icc).lut_pl_length != GET(lut_icc).lut_data_length;
    case AVT_PKT_FONT_DATA:
        *cur = GET(font_data).font_pl_length;
        *tot = GET(font_data).font_data_length;
        *off = 0;
        *is_parity = false;
        return GET(font_data).font_pl_length != GET(font_data).font_data_length;
    case AVT_PKT_FEC_GROUP_DATA:
        *cur = GET(fec_group_data).fec_data_length;
        *tot = GET(fec_group_data).fec_total_data_length;
        *off = GET(fec_group_data).fec_data_offset;
        return GET(fec_group_data).fec_data_length != GET(fec_group_data).fec_total_data_length;
    case AVT_PKT_METADATA_SEGMENT:    [[fallthrough]];
    case AVT_PKT_USER_DATA_SEGMENT:   [[fallthrough]];
    case AVT_PKT_LUT_ICC_SEGMENT:     [[fallthrough]];
    case AVT_PKT_FONT_DATA_SEGMENT:   [[fallthrough]];
    case AVT_PKT_STREAM_DATA_SEGMENT:
        *cur = GET(generic_segment).seg_length;
        *tot = GET(generic_segment).pkt_total_data;
        *off = GET(generic_segment).seg_offset;
        *is_parity = false;
        return -1;
    case AVT_PKT_METADATA_PARITY:    [[fallthrough]];
    case AVT_PKT_USER_DATA_PARITY:   [[fallthrough]];
    case AVT_PKT_LUT_ICC_PARITY:     [[fallthrough]];
    case AVT_PKT_FONT_DATA_PARITY:   [[fallthrough]];
    case AVT_PKT_STREAM_DATA_PARITY:
        *cur = GET(generic_parity).parity_data_length;
        *tot = GET(generic_parity).parity_total;
        *off = GET(generic_parity).parity_data_offset;
        *is_parity = true;
        return -1;
    default:
        /* Packet is not a part of a series */
        return 0;
    }
}

static inline void RENAME(avt_packet_change_size)(TYPE p,
                                                  uint32_t seg_offset,
                                                  uint32_t seg_length,
                                                  uint32_t tot_pl_size)
{
    switch (GET(desc)) {
    case AVT_PKT_STREAM_DATA:
        GET(stream_data).data_length = seg_length;
        GET(stream_data).pkt_segmented = seg_length < tot_pl_size;
        return;
    case AVT_PKT_STREAM_CONFIG: [[fallthrough]];
    case AVT_PKT_METADATA:
        GET(generic_data).payload_length = seg_length;
        GET(generic_data).total_payload_length = tot_pl_size;
        return;
    case AVT_PKT_USER_DATA:
        GET(user_data).userdata_pl_length = seg_length;
        GET(user_data).userdata_length = tot_pl_size;
        return;
    case AVT_PKT_LUT_ICC:
        GET(lut_icc).lut_pl_length = seg_length;
        GET(lut_icc).lut_data_length = tot_pl_size;
        return;
    case AVT_PKT_FONT_DATA:
        GET(font_data).font_pl_length = seg_length;
        GET(font_data).font_data_length = tot_pl_size;
        return;
    case AVT_PKT_FEC_GROUP_DATA:
        GET(fec_group_data).fec_data_length = seg_length;
        GET(fec_group_data).fec_total_data_length = tot_pl_size;
        GET(fec_group_data).fec_data_offset = seg_offset;
        return;
    case AVT_PKT_METADATA_SEGMENT:    [[fallthrough]];
    case AVT_PKT_USER_DATA_SEGMENT:   [[fallthrough]];
    case AVT_PKT_LUT_ICC_SEGMENT:     [[fallthrough]];
    case AVT_PKT_FONT_DATA_SEGMENT:   [[fallthrough]];
    case AVT_PKT_STREAM_DATA_SEGMENT:
        GET(generic_segment).seg_length = seg_length;
        GET(generic_segment).seg_offset = seg_offset;
        GET(generic_segment).pkt_total_data = tot_pl_size;
        return;
    case AVT_PKT_METADATA_PARITY:    [[fallthrough]];
    case AVT_PKT_USER_DATA_PARITY:   [[fallthrough]];
    case AVT_PKT_LUT_ICC_PARITY:     [[fallthrough]];
    case AVT_PKT_FONT_DATA_PARITY:   [[fallthrough]];
    case AVT_PKT_STREAM_DATA_PARITY:
        GET(generic_parity).parity_data_length = seg_length;
        GET(generic_parity).parity_data_offset = seg_offset;
        GET(generic_parity).parity_total = tot_pl_size;
        return;
    default:
        avt_assert1(0);
        unreachable();
    }
}
