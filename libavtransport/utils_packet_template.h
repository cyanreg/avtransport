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
    switch (GET(desc)) {
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

static inline int64_t RENAME(avt_packet_get_size)(const TYPE p)
{
    switch (GET(desc)) {
    case AVT_PKT_STREAM_DATA:
        return GET(stream_data.data_length);
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
    case AVT_PKT_METADATA:   [[fallthrough]];
    case AVT_PKT_USER_DATA:  [[fallthrough]];
    case AVT_PKT_LUT_ICC:    [[fallthrough]];
    case AVT_PKT_FONT_DATA:
        GET(generic_data).payload_length = seg_length;
        GET(generic_data).total_payload_length = tot_pl_size;
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
    default:
        avt_assert1(0);
        unreachable();
    }
}
