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
        if (ret < 0)
            goto end;

        /* Output generated, have to free it */
        avt_buffer_quick_unref(&hdr.pl);
    }

    {
        fprintf(stderr, "Testing packet + segment merger...\n");
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
        avt_packet_change_size(&hdr, 0, 64, 128);

        ret = avt_pkt_merge_seg(NULL, &s, &hdr);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* No output yet */

        seg.pkt = avt_packet_create_segment(&hdr, 1, 64, 64, 128);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;

        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        avt_buffer_quick_unref(&seg.pl);
        if (ret < 0)
            goto end;

        ret = 0;
    }

    {
        fprintf(stderr, "Testing packet + segment + segment merger...\n");
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
        avt_packet_change_size(&hdr, 0, 64, 192);

        ret = avt_pkt_merge_seg(NULL, &s, &hdr);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* No output yet */
        seg.pkt = avt_packet_create_segment(&hdr, 1, 64, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;

        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* Output */
        seg.pkt = avt_packet_create_segment(&hdr, 2, 128, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;

        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        avt_buffer_quick_unref(&seg.pl);
        if (ret < 0)
            goto end;

        ret = 0;
    }

    {
        fprintf(stderr, "Testing packet + segment + segment merger + reorder...\n");
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
        avt_packet_change_size(&hdr, 0, 64, 192);

        ret = avt_pkt_merge_seg(NULL, &s, &hdr);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* No output yet */
        seg.pkt = avt_packet_create_segment(&hdr, 1, 128, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;

        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* Output */
        seg.pkt = avt_packet_create_segment(&hdr, 2, 64, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;

        ret = avt_pkt_merge_seg(NULL, &s, &seg);        avt_buffer_quick_unref(&seg.pl);
        if (ret < 0)
            goto end;

        ret = 0;
    }

    {
        fprintf(stderr, "Testing packet + segment + segment merger + reorder + reorder...\n");
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
        avt_packet_change_size(&hdr, 0, 64, 192);

        /* No output yet */
        seg.pkt = avt_packet_create_segment(&hdr, 1, 128, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;

        ret = avt_pkt_merge_seg(NULL, &s, &seg);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        ret = avt_pkt_merge_seg(NULL, &s, &hdr);
        if (ret != AVT_ERROR(EAGAIN))
            goto end;

        /* Output */
        seg.pkt = avt_packet_create_segment(&hdr, 2, 64, 64, 192);
        hdr_data = avt_buffer_quick_alloc(&seg.pl, 64);
        if (!hdr_data)
            return ENOMEM;

        ret = avt_pkt_merge_seg(NULL, &s, &seg);        avt_buffer_quick_unref(&seg.pl);
        if (ret < 0)
            goto end;

        ret = 0;
    }


end:
    avt_pkt_merge_free(&s);
    return AVT_ERROR(ret);
}
