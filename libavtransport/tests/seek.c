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
#include <inttypes.h>

#include <avtransport/avtransport.h>
#include "address.h"
#include "io_common.h"
#include "protocol_common.h"
#include "buffer.h"
#include "packet_common.h"
#include "packet_encode.h"

extern const AVTIO avt_io_file;
extern const AVTProtocol avt_protocol_stream;

#define NB_PKTS 5

/* Build a small avt file with five packets: SESSION_START (72-byte header),
 * STREAM_REGISTRATION (72-byte header), two STREAM_DATA packets (36-byte header
 * + payload) with distinct pts/seq, and a STREAM_INDEX referencing the two data
 * packets. The byte offset at which each packet starts is returned via
 * `offsets`. */
static int build_test_file(AVTContext *avt, const char *path,
                           int64_t offsets[NB_PKTS])
{
    const AVTIO *io = &avt_io_file;
    AVTIOCtx *io_ctx = NULL;
    AVTAddress addr = { .path = (char *)path };
    int ret = io->init(avt, &io_ctx, &addr);
    if (ret < 0)
        return ret;

    AVTPktd pkts[NB_PKTS] = { };

    /* Packet 0: SESSION_START */
    {
        AVTBytestream bs = avt_bs_init(pkts[0].hdr, sizeof(pkts[0].hdr));
        avt_encode_session_start(&bs, (AVTSessionStart) {
            .session_start_descriptor = AVT_PKT_SESSION_START,
            .global_seq    = 1,
            .session_seq   = 1,
            .producer_name = "test",
        });
        pkts[0].hdr_len = bs.ptr - pkts[0].hdr;
    }
    /* Packet 1: STREAM_REGISTRATION */
    {
        AVTBytestream bs = avt_bs_init(pkts[1].hdr, sizeof(pkts[1].hdr));
        avt_encode_stream_registration(&bs, (AVTStreamRegistration) {
            .stream_registration_descriptor = AVT_PKT_STREAM_REGISTRATION,
            .global_seq  = 2,
            .stream_id   = 0x10,
            .codec_id    = AVT_CODEC_ID_RAW_VIDEO,
        });
        pkts[1].hdr_len = bs.ptr - pkts[1].hdr;
    }
    /* Packets 2 and 3: STREAM_DATA, pts {0, 1000} / seq {3, 4}. */
    for (int k = 0; k < 2; k++) {
        AVTPktd *pk = &pkts[2 + k];
        AVTBytestream bs = avt_bs_init(pk->hdr, sizeof(pk->hdr));
        avt_encode_stream_data(&bs, (AVTStreamData) {
            .stream_data_descriptor = AVT_PKT_STREAM_DATA,
            .stream_id   = 0x10,
            .global_seq  = 3 + k,
            .frame_type  = AVT_FRAME_TYPE_KEY,
            .pts         = 1000 * k,
            .duration    = 1,
            .data_length = 16,
        });
        pk->hdr_len = bs.ptr - pk->hdr;

        uint8_t *pl = avt_buffer_quick_alloc(&pk->pl, 16);
        if (!pl) {
            ret = AVT_ERROR(ENOMEM);
            goto fail;
        }
        for (int i = 0; i < 16; i++)
            pl[i] = (uint8_t)(0xA0 + 0x10 * k + i);
    }

    /* Write packets 0..3 first so we know the data packet offsets to index.
     * write_pkt returns the byte offset the packet was written at. */
    for (int i = 0; i < 4; i++) {
        int64_t r = io->write_pkt(io_ctx, &pkts[i], INT64_MAX);
        if (r < 0) {
            ret = (int)r;
            goto fail;
        }
        offsets[i] = r;
    }

    /* Packet 4: STREAM_INDEX with one entry per data packet. The index entries
     * trail the header as plain (non-LDPC) payload bytes. */
    {
        AVTBytestream bs = avt_bs_init(pkts[4].hdr, sizeof(pkts[4].hdr));
        avt_encode_stream_index(&bs, (AVTStreamIndex) {
            .stream_index_descriptor = AVT_PKT_STREAM_INDEX,
            .stream_id   = 0x10,
            .global_seq  = 5,
            .prev_idx    = 0,
            .next_idx    = 0,
            .nb_indices  = 2,
        });
        pkts[4].hdr_len = bs.ptr - pkts[4].hdr;

        uint8_t *ipl = avt_buffer_quick_alloc(&pkts[4].pl,
                                              2 * AVT_PKT_INDEX_ENTRY_SIZE);
        if (!ipl) {
            ret = AVT_ERROR(ENOMEM);
            goto fail;
        }
        AVTBytestream ebs = avt_bs_init(ipl, 2 * AVT_PKT_INDEX_ENTRY_SIZE);
        for (int k = 0; k < 2; k++)
            avt_encode_index_entry(&ebs, (AVTIndexEntry) {
                .index_entry_descriptor = AVT_PKT_STREAM_INDEX,
                .pts        = 1000 * k,
                .target_seq = 3 + k,
                .pkt_offset = (int32_t)offsets[2 + k],
            });
    }
    {
        int64_t r = io->write_pkt(io_ctx, &pkts[4], INT64_MAX);
        if (r < 0) {
            ret = (int)r;
            goto fail;
        }
        offsets[4] = r;
    }

    io->flush(io_ctx, INT64_MAX);
    ret = 0;

fail:
    for (int i = 0; i < NB_PKTS; i++)
        avt_buffer_quick_unref(&pkts[i].pl);
    io->close(&io_ctx);
    return ret;
}

int main(void)
{
    int ret;
    AVTContext *avt = NULL;
    const char *path = "seek_test.avt";

    ret = avt_init(&avt, NULL);
    if (ret < 0)
        return 1;

    int64_t offs[NB_PKTS];
    ret = build_test_file(avt, path, offs);
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "build_test_file failed: %d\n", ret);
        goto end;
    }
    avt_log(avt, AVT_LOG_INFO,
            "seek test file: pkt0@%" PRIi64 " pkt1@%" PRIi64 " pkt2@%" PRIi64
            " pkt3@%" PRIi64 " idx@%" PRIi64 "\n",
            offs[0], offs[1], offs[2], offs[3], offs[4]);

    /* Now reopen via protocol_stream + file io. */
    const AVTIO *io = &avt_io_file;
    AVTIOCtx *io_ctx = NULL;
    AVTAddress addr = { .path = (char *)path };
    ret = io->init(avt, &io_ctx, &addr);
    if (ret < 0)
        goto end;

    const AVTProtocol *p = &avt_protocol_stream;
    AVTProtocolCtx *p_ctx = NULL;
    AVTProtocolOpts popts = { 0 };
    ret = p->init(avt, &p_ctx, &addr, io, io_ctx, &popts);
    if (ret < 0) {
        io->close(&io_ctx);
        goto end;
    }

    /* Seek to each known packet boundary - all must succeed. */
    for (int i = 0; i < NB_PKTS; i++) {
        ret = p->seek(p_ctx, offs[i], 0, INT64_MIN, false);
        if (ret < 0) {
            avt_log(avt, AVT_LOG_ERROR,
                    "seek to offset %" PRIi64 " (pkt %d) failed: %d\n",
                    offs[i], i, ret);
            goto cleanup;
        }
    }
    avt_log(avt, AVT_LOG_INFO, "byte-offset seek to valid packets OK\n");

    /* Seek to a mid-packet offset must fail. SESSION_START is 72 bytes;
     * offset 50 is mid-header and decodes to a bogus descriptor. */
    ret = p->seek(p_ctx, 50, 0, INT64_MIN, false);
    if (ret >= 0) {
        avt_log(avt, AVT_LOG_ERROR,
                "seek to mid-packet offset 50 unexpectedly succeeded\n");
        ret = AVT_ERROR(EINVAL);
        goto cleanup;
    }
    avt_log(avt, AVT_LOG_INFO, "byte-offset seek to mid-packet correctly rejected\n");

    /* Before the index is parsed, ts/seq seeks must fail with EINVAL. */
    ret = p->seek(p_ctx, 0, 0, 1000, false);
    if (ret != AVT_ERROR(EINVAL)) {
        avt_log(avt, AVT_LOG_ERROR,
                "ts seek without index: expected EINVAL, got %d\n", ret);
        ret = AVT_ERROR(EINVAL);
        goto cleanup;
    }
    ret = p->seek(p_ctx, 0, 42, INT64_MIN, false);
    if (ret != AVT_ERROR(EINVAL)) {
        avt_log(avt, AVT_LOG_ERROR,
                "seq seek without index: expected EINVAL, got %d\n", ret);
        ret = AVT_ERROR(EINVAL);
        goto cleanup;
    }
    avt_log(avt, AVT_LOG_INFO,
            "ts / seq seek without index correctly rejected\n");

    /* Parse the on-disk index by pumping receive() through the whole file.
     * The STREAM_INDEX is the last packet; once it is parsed the protocol
     * populates its index context (and returns EAGAIN for that packet). */
    ret = p->seek(p_ctx, offs[0], 0, INT64_MIN, false);
    if (ret < 0)
        goto cleanup;

    AVTPacketFifo fifo = { };
    for (int i = 0; i < 4 * NB_PKTS; i++) {
        int rr = p->receive(p_ctx, &fifo, INT64_MAX);
        if (rr < 0 && rr != AVT_ERROR(EAGAIN))
            break; /* hit EOF; the index packet has already been consumed */
    }
    avt_pkt_fifo_free(&fifo);

    /* Timestamp seek: pick the latest entry with pts <= ts. */
    ret = p->seek(p_ctx, 0, 0, 500, false); /* -> pts 0 entry (offs[2]) */
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "ts seek (500) with index failed: %d\n", ret);
        goto cleanup;
    }
    ret = p->seek(p_ctx, 0, 0, 5000, false); /* -> pts 1000 entry (offs[3]) */
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "ts seek (5000) with index failed: %d\n", ret);
        goto cleanup;
    }
    /* A timestamp before the first entry has no at-or-before match. */
    ret = p->seek(p_ctx, 0, 0, -1, false);
    if (ret != AVT_ERROR(EINVAL)) {
        avt_log(avt, AVT_LOG_ERROR,
                "ts seek before first entry: expected EINVAL, got %d\n", ret);
        ret = AVT_ERROR(EINVAL);
        goto cleanup;
    }
    avt_log(avt, AVT_LOG_INFO, "timestamp seek against index OK\n");

    /* Sequence seek: exact match, then a miss. */
    ret = p->seek(p_ctx, 0, 4, INT64_MIN, false); /* -> offs[3] */
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "seq seek (4) with index failed: %d\n", ret);
        goto cleanup;
    }
    ret = p->seek(p_ctx, 0, 99, INT64_MIN, false);
    if (ret != AVT_ERROR(EINVAL)) {
        avt_log(avt, AVT_LOG_ERROR,
                "seq seek miss: expected EINVAL, got %d\n", ret);
        ret = AVT_ERROR(EINVAL);
        goto cleanup;
    }
    avt_log(avt, AVT_LOG_INFO, "sequence seek against index OK\n");

    ret = 0;

cleanup:
    p->close(&p_ctx);
    io->close(&io_ctx);

end:
    avt_close(&avt);
    remove(path);
    return ret < 0 ? 1 : 0;
}
