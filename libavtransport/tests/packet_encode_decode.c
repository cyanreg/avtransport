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
#include <time.h>

#include "utils_packet.h"
#include "packet_decode.h"

int main(void)
{
#if 0
    int ret;
    AVTPktd in = { };
    AVTPktd out = { };
    size_t buf_size;
    AVTBuffer *buf = avt_buffer_alloc(AVT_MAX_HEADER_LEN);
    uint8_t *buf_data = avt_buffer_get_data(buf, &buf_size);

    {
        in.hdr_off = 0;
        in.pkt = AVT_SESSION_START_HDR();
        avt_packet_encode_header(&in);
        memcpy(buf_data, in.hdr, in.hdr_len);
        ret = avt_decode_session_start(buf, &out.pkt.session_start);
        if (ret < 0) {
            printf("Error decoding\n");
            return AVT_ERROR(ret);
        }

        if (memcmp(&in.pkt, &out.pkt, in.hdr_len))
            return EINVAL;
    }

    {
        in.hdr_off = 0;
        in.pkt = AVT_STREAM_REGISTRATION_HDR();
        avt_packet_encode_header(&in);
        memcpy(buf_data, in.hdr, in.hdr_len);
        ret = avt_decode_stream_registration(buf, &out.pkt.stream_registration);
        if (ret < 0) {
            printf("Error decoding\n");
            return AVT_ERROR(ret);
        }

        if (memcmp(&in.pkt, &out.pkt, in.hdr_len))
            return EINVAL;
    }

    {
        in.hdr_off = 0;
        in.pkt = AVT_VIDEO_INFO_HDR();
        avt_packet_encode_header(&in);
        memcpy(buf_data, in.hdr, in.hdr_len);
        ret = avt_decode_video_info(buf, &out.pkt.video_info);
        if (ret < 0) {
            printf("Error decoding\n");
            return AVT_ERROR(ret);
        }

        if (memcmp(&in.pkt, &out.pkt, in.hdr_len))
            return EINVAL;
    }

    avt_buffer_unref(&buf);
#endif
    return 0;
}
