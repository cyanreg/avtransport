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
#include <string.h>

#include "reorder.h"
#include "buffer.h"
#include "utils_packet.h"
#include "fec_group.h"
#include "scheduler.h"

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

    /* Test 8: cross-stream FEC group recovery via reorder.
     * Build N standalone stream-data packets, manufacture their 0x0030 +
     * 0x0031 wire packets via the FEC sender, push everything *except*
     * one source packet through the reorder, and verify the dropped
     * source packet reappears in the output via FEC recovery. */
    {
        fprintf(stderr, "Test 8: FEC group recovery via reorder...\n");
#define FEC_N 8
        const uint16_t N = FEC_N;
        const uint16_t dropped = 3;       /* index of the source packet to drop */
        AVTPktd src[FEC_N];
        memset(src, 0, sizeof(src));

        AVTFecGroupSource group_src[FEC_N];
        uint8_t *src_bytes_buf[FEC_N] = { 0 };
        uint32_t src_byte_sizes[FEC_N];
        uint8_t  src_pattern[FEC_N];      /* first payload byte, for verify */
        uint64_t src_seq[FEC_N];          /* original seqs, for verify */

        /* Build N small stream-data packets and serialize each. */
        for (uint16_t i = 0; i < N; i++) {
            const uint32_t pl_size = 80 + (i * 31) % 64;
            ret = build_header(&src[i], 2000 + i, pl_size, pl_size);
            if (ret < 0) goto end;
            src[i].pkt.stream_id = (i & 1) ? 7 : 5;
            avt_packet_encode_header(&src[i]);

            size_t pls;
            const uint8_t *pld = avt_buffer_get_data(&src[i].pl, &pls);
            src_byte_sizes[i] = src[i].hdr_len + (uint32_t)pls;
            src_bytes_buf[i]  = malloc(src_byte_sizes[i]);
            CHECK(src_bytes_buf[i], "src bytes alloc");
            memcpy(src_bytes_buf[i], src[i].hdr, src[i].hdr_len);
            if (pls)
                memcpy(src_bytes_buf[i] + src[i].hdr_len, pld, pls);
            src_pattern[i] = pls ? pld[0] : 0;
            src_seq[i] = src[i].pkt.seq;

            group_src[i] = (AVTFecGroupSource){
                .seq       = src[i].pkt.seq,
                .stream_id = src[i].pkt.stream_id,
                .bytes     = src_bytes_buf[i],
                .size      = src_byte_sizes[i],
            };
        }

        /* Run the FEC sender to produce the wire 0x0030 + 0x0031 packets. */
        AVTFecGroupBuild build = { 0 };
        int rc = avt_fec_group_sender_build(NULL, group_src, N,
                                            /*group_id=*/0x1234,
                                            /*repair=*/4,
                                            /*max_data_payload=*/1024, &build);
        CHECK(rc == 0, "FEC group sender build");

        /* Materialize the 0x0030 grouping packet as an AVTPktd. */
        AVTPktd grouping = { 0 };
        grouping.pkt.fec_grouping = build.grouping;
        avt_packet_encode_header(&grouping);

        /* Materialize each 0x0031 data packet, allocating a payload that
         * is a copy of the matching slice of the parity blob. */
        AVTPktd *dpkts = calloc(build.nb_data, sizeof(*dpkts));
        CHECK(dpkts, "dpkts alloc");
        for (uint32_t i = 0; i < build.nb_data; i++) {
            dpkts[i].pkt.fec_group_data = build.data[i];
            avt_packet_encode_header(&dpkts[i]);
            if (build.data[i].fec_data_length) {
                uint8_t *dst = avt_buffer_quick_alloc(&dpkts[i].pl,
                                                      build.data[i].fec_data_length);
                CHECK(dst, "0x0031 pl alloc");
                memcpy(dst, build.parity + build.data[i].fec_data_offset,
                       build.data[i].fec_data_length);
            }
        }

        /* Push: 0x0030 first, then all 0x0031s, then sources EXCEPT dropped. */
        ret = avt_pkt_fifo_push(&in, &grouping);
        if (ret < 0) goto end;
        for (uint32_t i = 0; i < build.nb_data; i++) {
            ret = avt_pkt_fifo_push(&in, &dpkts[i]);
            if (ret < 0) goto end;
        }
        for (uint16_t i = 0; i < N; i++) {
            if (i == dropped)
                continue;
            ret = avt_pkt_fifo_push(&in, &src[i]);
            if (ret < 0) goto end;
        }

        /* Release local refs — the fifo now holds its own. */
        for (uint32_t i = 0; i < build.nb_data; i++)
            avt_buffer_quick_unref(&dpkts[i].pl);
        for (uint16_t i = 0; i < N; i++)
            avt_buffer_quick_unref(&src[i].pl);

        ret = avt_reorder_push(r, &in);
        if (ret < 0) goto end;
        CHECK(in.nb == 0, "input fifo not cleared");

        ret = avt_reorder_pop(r, &out);
        CHECK(ret == 0, "FEC test pop should succeed");
        /* Expect every source packet: N-1 directly + 1 recovered. */
        CHECK(out && out->nb == N, "should have N packets out");

        /* Verify each output packet matches one of the original source
         * packets — including the dropped one which must have been
         * reconstructed via FEC. */
        bool seen[FEC_N] = { false };
        for (unsigned int i = 0; i < out->nb; i++) {
            const uint64_t s = out->data[i].pkt.seq;
            uint16_t idx;
            for (idx = 0; idx < N; idx++)
                if (src_seq[idx] == s)
                    break;
            CHECK(idx < N, "output seq matches one of the source packets");
            CHECK(!seen[idx], "output seq duplicated");
            seen[idx] = true;
            size_t l;
            const uint8_t *d = avt_buffer_get_data(&out->data[i].pl, &l);
            CHECK(d && l > 0, "output payload present");
            CHECK(d[0] == src_pattern[idx], "output payload pattern matches");
        }
        for (uint16_t i = 0; i < N; i++)
            CHECK(seen[i], "every source packet present in output");

        ret = avt_reorder_done(r, out);
        CHECK(ret == 0, "FEC test done failed");
        out = NULL;

        /* Cleanup */
        free(dpkts);
        for (uint16_t i = 0; i < N; i++)
            free(src_bytes_buf[i]);
        avt_fec_group_sender_free(&build);
    }

    /* Test 9: sender → receiver round trip through the scheduler's
     * avt_scheduler_emit_fec_group helper. Mirrors Test 8 but uses the
     * production-side wrapper instead of calling the fec_group sender
     * builder directly. */
    {
        fprintf(stderr, "Test 9: FEC group round trip via scheduler hook...\n");
#define FEC2_N 8
        const uint16_t N = FEC2_N;
        const uint16_t dropped = 5;
        AVTPktd src[FEC2_N];
        AVTPktd *src_refs[FEC2_N];
        memset(src, 0, sizeof(src));
        uint8_t src_pat[FEC2_N];
        uint64_t src_seqv[FEC2_N];

        for (uint16_t i = 0; i < N; i++) {
            const uint32_t pl_size = 96 + (i * 19) % 48;
            ret = build_header(&src[i], 3000 + i, pl_size, pl_size);
            if (ret < 0) goto end;
            src[i].pkt.stream_id = (i & 1) ? 9 : 11;
            avt_packet_encode_header(&src[i]);
            size_t pls;
            const uint8_t *pld = avt_buffer_get_data(&src[i].pl, &pls);
            src_pat[i] = pls ? pld[0] : 0;
            src_seqv[i] = src[i].pkt.seq;
            src_refs[i] = &src[i];
        }

        AVTScheduler *sched = calloc(1, sizeof(*sched));
        CHECK(sched, "scheduler alloc");
        ret = avt_scheduler_init(sched, /*max_pkt_size=*/4096,
                                 /*bandwidth=*/INT64_MAX,
                                 /*fec_parity_pct=*/0);
        CHECK(ret == 0, "scheduler init");

        AVTPacketFifo wire = { 0 };
        ret = avt_scheduler_emit_fec_group(sched, &wire, src_refs, N,
                                           /*group_id=*/0xBEEF,
                                           /*repair=*/4,
                                           /*max_data_pkt_payload=*/1024);
        CHECK(ret == 0, "scheduler_emit_fec_group");
        CHECK(wire.nb >= 2, "FEC group must produce at least 2 wire packets");

        /* Push the wire FEC packets first, then sources except dropped,
         * into the reorder. */
        for (unsigned int i = 0; i < wire.nb; i++) {
            ret = avt_pkt_fifo_push(&in, &wire.data[i]);
            if (ret < 0) goto end;
        }
        for (uint16_t i = 0; i < N; i++) {
            if (i == dropped)
                continue;
            ret = avt_pkt_fifo_push(&in, &src[i]);
            if (ret < 0) goto end;
        }
        for (uint16_t i = 0; i < N; i++)
            avt_buffer_quick_unref(&src[i].pl);
        avt_pkt_fifo_free(&wire);

        ret = avt_reorder_push(r, &in);
        if (ret < 0) goto end;

        ret = avt_reorder_pop(r, &out);
        CHECK(ret == 0, "FEC scheduler-emit pop");
        CHECK(out && out->nb == N, "scheduler-emit FEC: N packets out");

        bool seen2[FEC2_N] = { false };
        for (unsigned int i = 0; i < out->nb; i++) {
            const uint64_t s = out->data[i].pkt.seq;
            uint16_t idx;
            for (idx = 0; idx < N; idx++)
                if (src_seqv[idx] == s)
                    break;
            CHECK(idx < N, "scheduler-emit FEC: output seq match");
            CHECK(!seen2[idx], "scheduler-emit FEC: duplicate");
            seen2[idx] = true;
            size_t l;
            const uint8_t *d = avt_buffer_get_data(&out->data[i].pl, &l);
            CHECK(d && l > 0, "scheduler-emit FEC: payload present");
            CHECK(d[0] == src_pat[idx], "scheduler-emit FEC: pattern matches");
        }
        for (uint16_t i = 0; i < N; i++)
            CHECK(seen2[i], "scheduler-emit FEC: every src delivered");

        ret = avt_reorder_done(r, out);
        CHECK(ret == 0, "scheduler-emit FEC: done");
        out = NULL;

        avt_scheduler_free(sched);
        free(sched);
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
