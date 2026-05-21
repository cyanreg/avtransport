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

#include "reorder.h"
#include "buffer.h"
#include "utils_packet.h"

/* Sequence numbers used for testing. The merger uses uint32_t for target,
 * so we keep them small (< 2^32) and use stride > AVT_REORDER_GROUP_NB. */
static uint32_t next_seq = 100;

/* Build a header packet with the given pts/total/seg lengths.
 * Allocates the next 16 seq numbers for the packet's group (header + up to 15
 * segments/parity). */
static int build_header(AVTPktd *hdr, int64_t pts, uint32_t tot, uint32_t seg)
{
    *hdr = (AVTPktd) {
        .pkt = AVT_STREAM_DATA_HDR(
            .frame_type = AVT_FRAME_TYPE_KEY,
            .pkt_in_fec_group = 0,
            .field_id = 0,
            .pkt_compression = AVT_DATA_COMPRESSION_NONE,
            .stream_id = 0,
            .pts = pts,
            .duration = 1,
        ),
    };
    uint8_t *data = avt_buffer_quick_alloc(&hdr->pl, seg);
    if (!data)
        return AVT_ERROR(ENOMEM);
    avt_packet_change_size(hdr, 0, seg, tot);
    hdr->pkt.seq = next_seq;
    next_seq += 16;
    /* Fill with a pattern so we can later verify reassembly */
    for (uint32_t i = 0; i < seg; i++)
        data[i] = (uint8_t)i;
    return 0;
}

static int build_segment(AVTPktd *seg, const AVTPktd *hdr,
                         uint64_t seq, uint32_t off, uint32_t len, uint32_t tot)
{
    seg->pkt = avt_packet_create_segment((AVTPktd *)hdr, seq, off, len, tot);
    uint8_t *data = avt_buffer_quick_alloc(&seg->pl, len);
    if (!data)
        return AVT_ERROR(ENOMEM);
    for (uint32_t i = 0; i < len; i++)
        data[i] = (uint8_t)(off + i);
    return 0;
}

#define CHECK(cond, msg)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            fprintf(stderr, "FAIL: " msg " (line %d)\n", __LINE__);   \
            ret = AVT_ERROR(EINVAL);                                  \
            goto end;                                                 \
        }                                                             \
    } while (0)

int main(void)
{
    int ret = 0;
    AVTReorder *r = calloc(1, sizeof(*r));
    AVTPacketFifo in = { };
    AVTPacketFifo *out = NULL;

    if (!r)
        return ENOMEM;

    ret = avt_reorder_init(NULL, r, 1024 * 1024);
    if (ret < 0) {
        fprintf(stderr, "init failed: %d\n", ret);
        free(r);
        return -ret;
    }

    /* Test 1: passthrough of a single non-segmented packet */
    {
        fprintf(stderr, "Test 1: standalone packet passthrough...\n");
        AVTPktd hdr = { };
        ret = build_header(&hdr, 0, 64, 64);
        if (ret < 0) goto end;

        ret = avt_pkt_fifo_push(&in, &hdr);
        if (ret < 0) goto end;
        avt_buffer_quick_unref(&hdr.pl);

        ret = avt_reorder_push(r, &in);
        if (ret < 0) goto end;
        CHECK(in.nb == 0, "input fifo not cleared");

        ret = avt_reorder_pop(r, &out);
        CHECK(ret == 0, "pop should succeed");
        CHECK(out && out->nb == 1, "should have 1 packet out");
        CHECK(out->data[0].pkt.desc == AVT_PKT_STREAM_DATA, "wrong desc");

        ret = avt_reorder_done(r, out);
        CHECK(ret == 0, "done failed");
        out = NULL;
    }

    /* Test 2: in-order segmented packet (header + 1 segment) */
    {
        fprintf(stderr, "Test 2: in-order segmented (header + segment)...\n");
        AVTPktd hdr = { }, seg = { };
        ret = build_header(&hdr, 100, 128, 64);
        if (ret < 0) goto end;
        ret = build_segment(&seg, &hdr, hdr.pkt.seq + 1, 64, 64, 128);
        if (ret < 0) goto end;

        ret = avt_pkt_fifo_push(&in, &hdr);
        if (ret < 0) goto end;
        ret = avt_pkt_fifo_push(&in, &seg);
        if (ret < 0) goto end;
        avt_buffer_quick_unref(&hdr.pl);
        avt_buffer_quick_unref(&seg.pl);

        ret = avt_reorder_push(r, &in);
        if (ret < 0) goto end;

        ret = avt_reorder_pop(r, &out);
        CHECK(ret == 0, "pop should succeed");
        CHECK(out && out->nb == 1, "should have 1 packet out");

        size_t out_len;
        uint8_t *out_data = avt_buffer_get_data(&out->data[0].pl, &out_len);
        CHECK(out_len == 128, "output length mismatch");
        for (uint32_t i = 0; i < 128; i++)
            CHECK(out_data[i] == (uint8_t)i, "output data mismatch");

        ret = avt_reorder_done(r, out);
        CHECK(ret == 0, "done failed");
        out = NULL;
    }

    /* Test 3: out-of-order segmented packet (segment arrives before header) */
    {
        fprintf(stderr, "Test 3: out-of-order (segment before header)...\n");
        AVTPktd hdr = { }, seg = { };
        ret = build_header(&hdr, 200, 192, 64);
        if (ret < 0) goto end;
        ret = build_segment(&seg, &hdr, hdr.pkt.seq + 1, 64, 128, 192);
        if (ret < 0) goto end;

        /* Push segment first */
        ret = avt_pkt_fifo_push(&in, &seg);
        if (ret < 0) goto end;
        ret = avt_pkt_fifo_push(&in, &hdr);
        if (ret < 0) goto end;
        avt_buffer_quick_unref(&hdr.pl);
        avt_buffer_quick_unref(&seg.pl);

        ret = avt_reorder_push(r, &in);
        if (ret < 0) goto end;

        ret = avt_reorder_pop(r, &out);
        CHECK(ret == 0, "pop should succeed");
        CHECK(out && out->nb == 1, "should have 1 packet out");

        size_t out_len;
        uint8_t *out_data = avt_buffer_get_data(&out->data[0].pl, &out_len);
        CHECK(out_len == 192, "output length mismatch");
        for (uint32_t i = 0; i < 192; i++)
            CHECK(out_data[i] == (uint8_t)i, "output data mismatch");

        ret = avt_reorder_done(r, out);
        CHECK(ret == 0, "done failed");
        out = NULL;
    }

    /* Test 4: interleaved packets across two stream IDs */
    {
        fprintf(stderr, "Test 4: interleaved two streams...\n");
        AVTPktd hdr_a = { }, seg_a = { };
        AVTPktd hdr_b = { }, seg_b = { };

        ret = build_header(&hdr_a, 300, 96, 32);
        if (ret < 0) goto end;
        hdr_a.pkt.stream_id = 1;
        ret = build_segment(&seg_a, &hdr_a, hdr_a.pkt.seq + 1, 32, 64, 96);
        if (ret < 0) goto end;
        seg_a.pkt.stream_id = 1;

        ret = build_header(&hdr_b, 301, 96, 32);
        if (ret < 0) goto end;
        hdr_b.pkt.stream_id = 2;
        ret = build_segment(&seg_b, &hdr_b, hdr_b.pkt.seq + 1, 32, 64, 96);
        if (ret < 0) goto end;
        seg_b.pkt.stream_id = 2;

        /* Push in interleaved order: hdr_a, hdr_b, seg_b, seg_a */
        ret = avt_pkt_fifo_push(&in, &hdr_a); if (ret < 0) goto end;
        ret = avt_pkt_fifo_push(&in, &hdr_b); if (ret < 0) goto end;
        ret = avt_pkt_fifo_push(&in, &seg_b); if (ret < 0) goto end;
        ret = avt_pkt_fifo_push(&in, &seg_a); if (ret < 0) goto end;
        avt_buffer_quick_unref(&hdr_a.pl);
        avt_buffer_quick_unref(&seg_a.pl);
        avt_buffer_quick_unref(&hdr_b.pl);
        avt_buffer_quick_unref(&seg_b.pl);

        ret = avt_reorder_push(r, &in);
        if (ret < 0) goto end;

        ret = avt_reorder_pop(r, &out);
        CHECK(ret == 0, "pop should succeed");
        CHECK(out && out->nb == 2, "should have 2 packets out");

        /* Validate both packets - order is completion order */
        bool seen_1 = false, seen_2 = false;
        for (unsigned int i = 0; i < out->nb; i++) {
            size_t l;
            uint8_t *d = avt_buffer_get_data(&out->data[i].pl, &l);
            CHECK(l == 96, "wrong length");
            for (uint32_t j = 0; j < 96; j++)
                CHECK(d[j] == (uint8_t)j, "data mismatch");
            if (out->data[i].pkt.stream_id == 1) seen_1 = true;
            if (out->data[i].pkt.stream_id == 2) seen_2 = true;
        }
        CHECK(seen_1 && seen_2, "missing stream output");

        ret = avt_reorder_done(r, out);
        CHECK(ret == 0, "done failed");
        out = NULL;
    }

    /* Test 5: three-segment packet (header + 2 segments) in scrambled order */
    {
        fprintf(stderr, "Test 5: three-segment scrambled (seg2, hdr, seg1)...\n");
        AVTPktd hdr = { }, s1 = { }, s2 = { };
        ret = build_header(&hdr, 400, 192, 64);
        if (ret < 0) goto end;
        ret = build_segment(&s1, &hdr, hdr.pkt.seq + 1, 64, 64, 192);
        if (ret < 0) goto end;
        ret = build_segment(&s2, &hdr, hdr.pkt.seq + 2, 128, 64, 192);
        if (ret < 0) goto end;

        ret = avt_pkt_fifo_push(&in, &s2); if (ret < 0) goto end;
        ret = avt_pkt_fifo_push(&in, &hdr); if (ret < 0) goto end;
        ret = avt_pkt_fifo_push(&in, &s1); if (ret < 0) goto end;
        avt_buffer_quick_unref(&hdr.pl);
        avt_buffer_quick_unref(&s1.pl);
        avt_buffer_quick_unref(&s2.pl);

        ret = avt_reorder_push(r, &in);
        if (ret < 0) goto end;

        ret = avt_reorder_pop(r, &out);
        CHECK(ret == 0, "pop should succeed");
        CHECK(out && out->nb == 1, "should have 1 packet out");

        size_t out_len;
        uint8_t *out_data = avt_buffer_get_data(&out->data[0].pl, &out_len);
        CHECK(out_len == 192, "wrong length");
        for (uint32_t i = 0; i < 192; i++)
            CHECK(out_data[i] == (uint8_t)i, "data mismatch");

        ret = avt_reorder_done(r, out);
        CHECK(ret == 0, "done failed");
        out = NULL;
    }

    /* Test 6: empty pop returns EAGAIN */
    {
        fprintf(stderr, "Test 6: empty pop...\n");
        ret = avt_reorder_pop(r, &out);
        CHECK(ret == AVT_ERROR(EAGAIN), "expected EAGAIN on empty");
        ret = 0;
    }

    /* Test 7: bucket reuse — push, pop, done, push, pop again */
    {
        fprintf(stderr, "Test 7: bucket reuse cycle...\n");
        const int nb_buckets_before = r->nb_buckets;

        for (int cycle = 0; cycle < 3; cycle++) {
            AVTPktd hdr = { };
            ret = build_header(&hdr, 500 + cycle, 64, 64);
            if (ret < 0) goto end;

            ret = avt_pkt_fifo_push(&in, &hdr);
            if (ret < 0) goto end;
            avt_buffer_quick_unref(&hdr.pl);

            ret = avt_reorder_push(r, &in);
            if (ret < 0) goto end;

            ret = avt_reorder_pop(r, &out);
            CHECK(ret == 0, "cycle pop failed");
            ret = avt_reorder_done(r, out);
            CHECK(ret == 0, "cycle done failed");
            out = NULL;
        }

        /* No additional buckets allocated across cycles */
        CHECK(r->nb_buckets == nb_buckets_before + 1 || r->nb_buckets == nb_buckets_before,
              "bucket pool grew unexpectedly");
    }

    fprintf(stderr, "All reorder tests passed.\n");
    ret = 0;

end:
    avt_pkt_fifo_free(&in);
    if (out)
        avt_reorder_done(r, out);
    avt_reorder_free(r);
    free(r);
    return -ret;
}
