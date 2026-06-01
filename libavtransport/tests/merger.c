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
#include <string.h>

#include "merger.h"
#include "buffer.h"
#include "utils_packet.h"
#include "fec.h"

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
        if (ret != 512)
            goto end;
        avt_buffer_quick_unref(&out.pl);
        ret = 0;
    }

    {
        fprintf(stderr, "Testing FEC stream-data-parity recovery...\n");
        AVTMerger fs = { };
        const uint32_t plen = 8192;
        const uint32_t seg_sz = 512;         /* bytes per data piece */
        const uint32_t nseg = plen / seg_sz; /* a header plus 15 segments */
        uint8_t ref[8192];
        for (uint32_t i = 0; i < plen; i++)
            ref[i] = (uint8_t)(i * 37 + 11);

        /* Generate RaptorQ parity covering the whole payload. */
        AVTBuffer ref_buf = { };
        uint8_t *rbd = avt_buffer_quick_alloc(&ref_buf, plen);
        if (!rbd)
            return ENOMEM;
        memcpy(rbd, ref, plen);
        AVTBuffer *parity = avt_fec_gen_parity(NULL, &ref_buf, 50);
        avt_buffer_quick_unref(&ref_buf);
        if (!parity) {
            fprintf(stderr, "FAIL: no parity was generated\n");
            ret = AVT_ERROR(EINVAL);
            goto end;
        }
        size_t par_len;
        uint8_t *par_data = avt_buffer_get_data(parity, &par_len);

        /* The data header packet carries [0, seg_sz); build and encode it
         * up front so the segment and parity header_7 slices agree. */
        AVTPktd dh = {
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
        avt_packet_change_size(&dh, 0, seg_sz, plen);
        avt_packet_encode_header(&dh);
        uint8_t *dhd = avt_buffer_quick_alloc(&dh.pl, seg_sz);
        if (!dhd)
            return ENOMEM;
        memcpy(dhd, ref, seg_sz);

        /* Push segments 1..7, dropping segment 3, then the header packet.
         * Beginning with a segment sizes the merger's payload buffer from
         * the segment's signalled total length. */
        for (uint32_t k = 1; k < nseg; k++) {
            if (k == 3)
                continue; /* the lost segment, to be recovered by FEC */
            AVTPktd sp = { };
            sp.pkt = avt_packet_create_segment(&dh, k, k * seg_sz,
                                               seg_sz, plen);
            uint8_t *spd = avt_buffer_quick_alloc(&sp.pl, seg_sz);
            if (!spd)
                return ENOMEM;
            memcpy(spd, ref + k * seg_sz, seg_sz);
            ret = avt_pkt_merge_seg(NULL, &fs, &sp);
            avt_buffer_quick_unref(&sp.pl);
            if (ret < 0 && ret != AVT_ERROR(EAGAIN))
                goto end;
        }
        ret = avt_pkt_merge_seg(NULL, &fs, &dh);
        avt_buffer_quick_unref(&dh.pl);
        if (ret < 0 && ret != AVT_ERROR(EAGAIN))
            goto end;

        /* One parity packet (seq 8) carrying the whole repair blob. */
        AVTPktd pp = { };
        const uint8_t *h7 = &dh.hdr[dh.hdr_off + (8 % 7) * 4];
        pp.pkt = AVT_GENERIC_PARITY_HDR(AVT_PKT_STREAM_DATA_PARITY,
            .global_seq = 8,
            .stream_id = 0,
            .target_seq = dh.pkt.seq,
            .parity_data_offset = 0,
            .parity_data_length = (uint32_t)par_len,
            .parity_total = (uint32_t)par_len,
            .header_7 = { h7[0], h7[1], h7[2], h7[3] },
        );
        uint8_t *ppd = avt_buffer_quick_alloc(&pp.pl, par_len);
        if (!ppd)
            return ENOMEM;
        memcpy(ppd, par_data, par_len);
        ret = avt_pkt_merge_seg(NULL, &fs, &pp);
        avt_buffer_quick_unref(&pp.pl);
        avt_buffer_unref(&parity);
        if (ret < 0) {
            fprintf(stderr, "FAIL: merge after parity returned %d\n", ret);
            goto end;
        }

        /* The merger should have recovered the lost segment via FEC. */
        ret = avt_pkt_merge_out(NULL, &fs, &out, 0);
        if (ret < 0) {
            fprintf(stderr, "FAIL: merge_out after recovery returned %d\n", ret);
            goto end;
        }
        size_t out_len;
        uint8_t *out_data = avt_buffer_get_data(&out.pl, &out_len);
        if (out_len != plen || memcmp(out_data, ref, plen) != 0) {
            fprintf(stderr, "FAIL: recovered payload does not match\n");
            avt_buffer_quick_unref(&out.pl);
            avt_pkt_merge_free(&fs);
            ret = AVT_ERROR(EINVAL);
            goto end;
        }
        avt_buffer_quick_unref(&out.pl);
        avt_pkt_merge_free(&fs);
        fprintf(stderr, "FEC recovery OK\n");
        ret = 0;
    }

end:
    avt_pkt_merge_free(&s);
    return AVT_ERROR(ret);
}
