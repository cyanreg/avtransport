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

#include "merger.h"
#include "buffer.h"
#include "utils_packet.h"

int main(void)
{
    int ret;
    AVTMerger s = { };
    AVTPktd hdr = { };
    AVTPktd seg = { };
    uint8_t *hdr_data;
    AVTPktd out;

    {
        fprintf(stderr, "Testing noop merge...\n");
        hdr = (AVTPktd) {
            .pkt = AVT_STREAM_DATA_HDR(
                .frame_type = AVT_FRAME_TYPE_KEY,
                .pkt_in_fec_group = 0,
                .field_id = 0,
                .pkt_compression = AVT_DATA_COMPRESSION_NONE,
                .stream_id = 0,
                .pts = 0,
                .duration = 1,
            ),
        };

        hdr_data = avt_buffer_quick_alloc(&hdr.pl, 64);
        if (!hdr_data)
            return ENOMEM;
        avt_packet_change_size(&hdr, 0, 64, 64);

        /* One full packet in, one full packet out */
        ret = avt_pkt_merge_seg(NULL, &s, &hdr);
        /* Make sure the code tells us early it can output something */
        if (ret < 0)
            goto end;

        /* Output the packet */
        ret = avt_pkt_merge_out(NULL, &s, &out, 0);
        if (ret < 0)
            goto end;

        /* Output generated, have to free it */
        avt_buffer_quick_unref(&out.pl);
        ret = 0;
    }

    {
        fprintf(stderr, "Testing packet(0) + segment(1) merger...\n");
        hdr = (AVTPktd) {
            .pkt = AVT_STREAM_DATA_HDR(
                .frame_type = AVT_FRAME_TYPE_KEY,
                .pkt_in_fec_group = 0,
                .field_id = 0,
                .pkt_compression = AVT_DATA_COMPRESSION_NONE,
                .stream_id = 0,
                .pts = 0,
                .duration = 1,
            ),
        };

        /* Start packet */
        hdr_data = avt_buffer_quick_alloc(&hdr.pl, 64);
        if (!hdr_data)
            return ENOMEM;
        avt_packet_change_size(&hdr, 0, 64, 128);
        ret = avt_pkt_merge_seg(NULL, &s, &hdr);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* Segment */
        seg.pkt = avt_packet_create_segment(&hdr, 1, 64, 64, 128);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;
        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        avt_buffer_quick_unref(&seg.pl);
        if (ret < 0)
            goto end;

        /* Output the packet */
        ret = avt_pkt_merge_out(NULL, &s, &out, 0);
        if (ret != 128)
            goto end;
        avt_buffer_quick_unref(&out.pl);
        ret = 0;
    }

    {
        fprintf(stderr, "Testing packet(0) + segment(2) + segment(1) merger...\n");
        hdr = (AVTPktd) {
            .pkt = AVT_STREAM_DATA_HDR(
                .frame_type = AVT_FRAME_TYPE_KEY,
                .pkt_in_fec_group = 0,
                .field_id = 0,
                .pkt_compression = AVT_DATA_COMPRESSION_NONE,
                .stream_id = 0,
                .pts = 0,
                .duration = 1,
            ),
        };

        /* Start packet */
        hdr_data = avt_buffer_quick_alloc(&hdr.pl, 64);
        if (!hdr_data)
            return ENOMEM;
        avt_packet_change_size(&hdr, 0, 64, 192);
        ret = avt_pkt_merge_seg(NULL, &s, &hdr);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* Segment */
        seg.pkt = avt_packet_create_segment(&hdr, 2, 128, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;
        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* Segment */
        seg.pkt = avt_packet_create_segment(&hdr, 1, 64, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;
        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        avt_buffer_quick_unref(&seg.pl);
        if (ret != 0)
            goto end;

        /* Output the packet */
        ret = avt_pkt_merge_out(NULL, &s, &out, 0);
        if (ret != 192)
            goto end;
        avt_buffer_quick_unref(&out.pl);
        ret = 0;
    }

    {
        fprintf(stderr, "Testing segment(2) + segment(1) + packet(0) merger...\n");
        hdr = (AVTPktd) {
            .pkt = AVT_STREAM_DATA_HDR(
                .frame_type = AVT_FRAME_TYPE_KEY,
                .pkt_in_fec_group = 0,
                .field_id = 0,
                .pkt_compression = AVT_DATA_COMPRESSION_NONE,
                .stream_id = 0,
                .pts = 0,
                .duration = 1,
            ),
        };

        /* Segment */
        seg.pkt = avt_packet_create_segment(&hdr, 2, 128, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;
        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* Segment */
        seg.pkt = avt_packet_create_segment(&hdr, 1, 64, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;
        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        avt_buffer_quick_unref(&seg.pl);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* Start packet */
        hdr_data = avt_buffer_quick_alloc(&hdr.pl, 64);
        if (!hdr_data)
            return ENOMEM;
        avt_packet_change_size(&hdr, 0, 64, 192);
        ret = avt_pkt_merge_seg(NULL, &s, &hdr);
        if (ret != 0)
            goto end;

        /* Output the packet */
        ret = avt_pkt_merge_out(NULL, &s, &out, 0);
        if (ret != 192)
            goto end;
        avt_buffer_quick_unref(&out.pl);
        ret = 0;
    }

    {
        fprintf(stderr, "Testing segment(6/5/4/3/2/1/0) partial recovery...\n");
        hdr = (AVTPktd) {
            .pkt = AVT_STREAM_DATA_HDR(
                .frame_type = AVT_FRAME_TYPE_KEY,
                .pkt_in_fec_group = 0,
                .field_id = 0,
                .pkt_compression = AVT_DATA_COMPRESSION_NONE,
                .stream_id = 0,
                .pts = 0,
                .duration = 1,
            ),
        };
        avt_packet_encode_header(&hdr);

        /* Segment */
        for (int i = 6; i >= 0; i--) {
            seg.pkt = avt_packet_create_segment(&hdr, i + 1, 512 - (i + 1)*64, 64, 512);
            hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
            if (!hdr_data)
                return ENOMEM;
            ret = avt_pkt_merge_seg(NULL, &s, &seg);
            if (ret != AVT_ERROR(EAGAIN))
                goto end;
        }

        /* Output the packet */
        ret = avt_pkt_merge_out(NULL, &s, &out, 1);
        if (ret != 192)
            goto end;
        avt_buffer_quick_unref(&out.pl);
        ret = 0;
    }

end:
    avt_pkt_merge_free(&s);
    return AVT_ERROR(ret);
}
