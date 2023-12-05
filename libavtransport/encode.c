/*
 * Copyright © 2023, Lynne
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

#include "encode.h"

#include "../packet_encode.h"

int avt_encode_header(uint8_t hdr[AVT_MAX_HEADER_LEN], size_t *hdr_len,
                      enum AVTPktDescriptors desc, union AVTPacketData pkt,
                      const uint8_t first[AVT_MAX_HEADER_LEN])
{
    AVTBytestream bs = avt_bs_init(hdr, AVT_MAX_HEADER_LEN);

    switch (desc & 0xFFFF) {
    case AVT_PKT_SESSION_START: {
        avt_encode_session_start(&bs, pkt.session_start);
        return 0;
    }
    case AVT_PKT_STREAM_REGISTRATION: {
        avt_encode_stream_registration(&bs, pkt.stream_registration);
        return 0;
    }
    case AVT_PKT_VIDEO_INFO: {
        avt_encode_video_info(&bs, pkt.video_info);
        return 0;
    }
    case AVT_PKT_LUT_ICC: {
        avt_encode_lut_icc(&bs, pkt.lut_icc);
        return 0;
    }
    case AVT_PKT_FONT_DATA: {
        avt_encode_font_data(&bs, pkt.font_data);
        return 0;
    }
    case AVT_PKT_STREAM_DATA: {
        avt_encode_stream_data(&bs, pkt.stream_data);
        return 0;
    }
    case AVT_PKT_USER_DATA: {
        avt_encode_user_data(&bs, pkt.user_data);
        return 0;
    }
    case AVT_PKT_STREAM_INDEX: {
        avt_encode_stream_index(&bs, pkt.stream_index);
        return 0;
    }
    case AVT_PKT_METADATA_SEGMENT:
    case AVT_PKT_FONT_DATA_SEGMENT:
    case AVT_PKT_STREAM_DATA_SEGMENT:
    case AVT_PKT_USER_DATA_SEGMENT: {
        avt_encode_generic_segment(&bs, pkt.generic_segment);
        return 0;
    }
    default:
        avt_assert0(0);
    }

    return 0;
}
