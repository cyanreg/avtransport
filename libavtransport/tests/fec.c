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

#include "fec.h"
#include "fec_group.h"
#include "buffer.h"
#include "utils_packet.h"
#include "packet_decode.h"

#define CHECK(cond, msg)                                                \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "FAIL: " msg " (line %d)\n", __LINE__);     \
            ret = 1;                                                    \
            goto end;                                                   \
        }                                                               \
    } while (0)

/* Fill with a non-constant pattern so the repair symbols are meaningfully
 * non-zero (a linear code maps a zero object to zero repair data). */
static void fill_pattern(uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
        data[i] = (uint8_t)(i * 37 + 11);
}

/* Encode `plen` bytes of pattern data at `pct`% parity, erase `drop_src`
 * source symbols and `drop_par` repair symbols, then attempt recovery.
 *
 * Returns the avt_fec_recover() result; on a zero (success) result it sets
 * `*matched` according to whether the recovered payload is byte-identical
 * to the original. Returns -100 on a test-harness setup failure. */
static int run_recovery(size_t plen, int pct, size_t drop_src,
                        size_t drop_par, bool *matched)
{
    int result = -100;
    AVTBuffer src = { };
    AVTBuffer *par = NULL;
    uint8_t *work = NULL, *dp = NULL, *pp = NULL;

    *matched = false;

    uint8_t *sd = avt_buffer_quick_alloc(&src, plen);
    if (!sd)
        goto out;
    fill_pattern(sd, plen);

    par = avt_fec_gen_parity(NULL, &src, pct);
    if (!par)
        goto out;
    size_t par_len;
    uint8_t *pd = avt_buffer_get_data(par, &par_len);

    const size_t ndata = (plen + AVT_FEC_SYMBOL_SIZE - 1) / AVT_FEC_SYMBOL_SIZE;
    const size_t nrep  = par_len / AVT_FEC_SYMBOL_SIZE;
    if (drop_src > ndata || drop_par > nrep)
        goto out;

    work = malloc(plen);
    dp = malloc(ndata);
    pp = malloc(nrep);
    if (!work || !dp || !pp)
        goto out;
    memcpy(work, sd, plen);
    memset(dp, 1, ndata);
    memset(pp, 1, nrep);

    /* Erase a run of source symbols, scribbling over them so a no-op
     * "recovery" could not pass the byte comparison. */
    for (size_t e = 0; e < drop_src; e++) {
        size_t off = e * AVT_FEC_SYMBOL_SIZE;
        size_t n = plen - off;
        if (n > AVT_FEC_SYMBOL_SIZE)
            n = AVT_FEC_SYMBOL_SIZE;
        memset(work + off, 0xA5, n);
        dp[e] = 0;
    }
    for (size_t j = 0; j < drop_par; j++)
        pp[nrep - 1 - j] = 0;

    result = avt_fec_recover(NULL, work, plen, dp, pd, par_len, pp);
    if (result == 0)
        *matched = (memcmp(work, sd, plen) == 0);

out:
    free(work);
    free(dp);
    free(pp);
    if (par)
        avt_buffer_unref(&par);
    avt_buffer_quick_unref(&src);
    return result;
}

int main(void)
{
    int ret = 0;
    AVTBuffer pl = { };
    AVTBuffer big = { };
    AVTBuffer tiny = { };
    AVTBuffer *p10 = NULL, *p100 = NULL, *p0 = NULL, *ptiny = NULL;
    bool matched;

    /* A payload comfortably within a single RaptorQ source block. */
    const size_t pl_len = 8192;
    uint8_t *pd = avt_buffer_quick_alloc(&pl, pl_len);
    if (!pd) {
        fprintf(stderr, "payload alloc failed\n");
        return 1;
    }
    fill_pattern(pd, pl_len);

    /* Test 1: a small percentage yields a non-empty, symbol-aligned blob. */
    fprintf(stderr, "Test 1: basic parity generation...\n");
    p10 = avt_fec_gen_parity(NULL, &pl, 10);
    CHECK(p10, "10 pct parity should be produced");
    size_t l10 = avt_buffer_get_data_len(p10);
    CHECK(l10 > 0 && (l10 % AVT_FEC_SYMBOL_SIZE) == 0,
          "parity must be a positive multiple of the symbol size");

    /* Repair data of a varied payload must not come back all-zero. */
    {
        size_t n;
        uint8_t *rd = avt_buffer_get_data(p10, &n);
        bool nonzero = false;
        for (size_t i = 0; i < n; i++)
            if (rd[i]) { nonzero = true; break; }
        CHECK(nonzero, "repair symbols should not be all-zero");
    }

    /* Test 2: more parity percent yields strictly more repair data. */
    fprintf(stderr, "Test 2: parity amount scales with percentage...\n");
    p100 = avt_fec_gen_parity(NULL, &pl, 100);
    CHECK(p100, "100 pct parity should be produced");
    CHECK(avt_buffer_get_data_len(p100) > l10,
          "100 pct parity must exceed 10 pct parity");

    /* Test 3: a zero percentage disables FEC. */
    fprintf(stderr, "Test 3: zero percent disables parity...\n");
    p0 = avt_fec_gen_parity(NULL, &pl, 0);
    CHECK(!p0, "zero percent must produce no parity");

    /* Test 4: a payload smaller than one symbol produces no parity. */
    fprintf(stderr, "Test 4: undersized payload...\n");
    uint8_t *td = avt_buffer_quick_alloc(&tiny, 2);
    CHECK(td, "tiny alloc failed");
    ptiny = avt_fec_gen_parity(NULL, &tiny, 50);
    CHECK(!ptiny, "sub-symbol payload must produce no parity");

    /* Test 5: a multi-block payload — larger than one RaptorQ source block —
     * is encoded and recovered across block boundaries. */
    fprintf(stderr, "Test 5: multi-block FEC recovery...\n");
    {
        /* Over the ~6.9 MiB single-block cap, and one symbol past a clean
         * multiple so the two source blocks come out unequal in size. */
        const size_t mb_len = 7 * 1024 * 1024 + AVT_FEC_SYMBOL_SIZE;
        const size_t mb_sym = mb_len / AVT_FEC_SYMBOL_SIZE;
        uint8_t *mbd = avt_buffer_quick_alloc(&big, mb_len);
        CHECK(mbd, "multi-block payload alloc");
        fill_pattern(mbd, mb_len);

        AVTBuffer *mbp = avt_fec_gen_parity(NULL, &big, 25);
        CHECK(mbp, "multi-block payload must produce parity");
        size_t mbp_len;
        uint8_t *mbp_d = avt_buffer_get_data(mbp, &mbp_len);
        size_t mb_nrep = mbp_len / AVT_FEC_SYMBOL_SIZE;

        uint8_t *mb_work = malloc(mb_len);
        uint8_t *mb_dp = malloc(mb_sym);
        uint8_t *mb_pp = malloc(mb_nrep);
        CHECK(mb_work && mb_dp && mb_pp, "multi-block scratch alloc");
        memcpy(mb_work, mbd, mb_len);
        memset(mb_dp, 1, mb_sym);
        memset(mb_pp, 1, mb_nrep);

        /* Erase a run of symbols near each end, so the losses fall in
         * different source blocks. */
        for (size_t e = 1000; e < 4000; e++) {
            memset(mb_work + e * AVT_FEC_SYMBOL_SIZE, 0xA5, AVT_FEC_SYMBOL_SIZE);
            mb_dp[e] = 0;
        }
        for (size_t e = mb_sym - 4000; e < mb_sym - 1000; e++) {
            memset(mb_work + e * AVT_FEC_SYMBOL_SIZE, 0xA5, AVT_FEC_SYMBOL_SIZE);
            mb_dp[e] = 0;
        }

        int rr = avt_fec_recover(NULL, mb_work, mb_len, mb_dp,
                                 mbp_d, mbp_len, mb_pp);
        bool ok = (rr == 0) && (memcmp(mb_work, mbd, mb_len) == 0);
        free(mb_work);
        free(mb_dp);
        free(mb_pp);
        avt_buffer_unref(&mbp);
        CHECK(ok, "multi-block payload must recover correctly");
    }

    /* Test 6: encode, erase a run of source symbols, recover them. */
    fprintf(stderr, "Test 6: FEC recovery round-trip...\n");
    {
        int rr = run_recovery(65536, 25, 80, 0, &matched);
        CHECK(rr == 0, "recovery should succeed with ample parity");
        CHECK(matched, "recovered payload must match the original");
    }

    /* Test 7: more loss than the parity budget fails gracefully. */
    fprintf(stderr, "Test 7: recovery short of symbols fails cleanly...\n");
    {
        int rr = run_recovery(65536, 5, 80, 0, &matched);
        CHECK(rr != 0, "recovery must not claim success without enough symbols");
    }

    /* Test 8: recovery when both source and repair symbols were lost. */
    fprintf(stderr, "Test 8: recovery with partial parity loss...\n");
    {
        int rr = run_recovery(65536, 50, 80, 150, &matched);
        CHECK(rr == 0, "recovery should succeed despite some lost parity");
        CHECK(matched, "recovered payload must match the original");
    }

    /* Test 9: a payload whose length is not a symbol multiple. */
    fprintf(stderr, "Test 9: recovery of a non-symbol-aligned payload...\n");
    {
        int rr = run_recovery(65535, 25, 80, 0, &matched);
        CHECK(rr == 0, "recovery should succeed for an unaligned payload");
        CHECK(matched, "recovered unaligned payload must match the original");
    }

    /* Test 10: OTI pack/unpack round trip across the wire encoding. */
    fprintf(stderr, "Test 10: OTI pack/unpack round trip...\n");
    {
        const struct {
            uint64_t F;
            uint16_t T;
            uint8_t  Z;
            uint16_t N;
            uint8_t  Al;
        } cases[] = {
            {     8192, 128,   1,   1, 4 },
            { 16777215, 256,   4,   1, 8 },
            { 65536000, 512,  32,   1, 4 },
            {       64,  16,   1,   1, 4 },
        };
        for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
            uint64_t common; uint32_t scheme;
            avt_fec_oti_pack(cases[i].F, cases[i].T, cases[i].Z,
                             cases[i].N, cases[i].Al, &common, &scheme);
            uint64_t F2; uint16_t T2, N2; uint8_t Z2, Al2;
            avt_fec_oti_unpack(common, scheme, &F2, &T2, &Z2, &N2, &Al2);
            CHECK(F2  == cases[i].F  && T2  == cases[i].T &&
                  Z2  == cases[i].Z  && N2  == cases[i].N &&
                  Al2 == cases[i].Al, "OTI round trip must preserve fields");
        }
    }

    /* Test 11: FEC group primitive — encode parity, drop a source symbol,
     * recover. Models the single-block (Z=1) case used for typical groups. */
    fprintf(stderr, "Test 11: FEC group primitive single-block recovery...\n");
    {
        const uint16_t T = 256;
        const uint16_t K = 12;
        const size_t F = (size_t)K * T;
        uint8_t *src = malloc(F);
        uint8_t *work = malloc(F);
        CHECK(src && work, "group source alloc");
        fill_pattern(src, F);
        memcpy(work, src, F);

        uint64_t common; uint32_t scheme;
        uint8_t *parity = NULL; size_t parity_len = 0;
        int rc = avt_fec_group_encode(NULL, src, F, T, /*repair=*/6,
                                      &common, &scheme,
                                      &parity, &parity_len);
        CHECK(rc == 0 && parity && parity_len > 0,
              "group encode must produce parity");
        /* OTI must describe what we asked for. */
        uint64_t Fv; uint16_t Tv, Nv; uint8_t Zv, Alv;
        avt_fec_oti_unpack(common, scheme, &Fv, &Tv, &Zv, &Nv, &Alv);
        CHECK(Fv == F && Tv == T && Zv == 1 && Nv == 1 && Alv == 4,
              "group OTI must reflect chosen parameters");

        /* Lose two whole source symbols (packets #3 and #7). */
        const uint16_t drop[] = { 3, 7 };
        uint8_t *src_pres = malloc(K);
        memset(src_pres, 1, K);
        for (size_t i = 0; i < sizeof(drop)/sizeof(drop[0]); i++) {
            memset(work + (size_t)drop[i] * T, 0xAB, T);
            src_pres[drop[i]] = 0;
        }
        uint8_t *par_pres = malloc(parity_len / T);
        memset(par_pres, 1, parity_len / T);

        rc = avt_fec_group_decode(NULL, common, scheme, work, F,
                                  src_pres, parity, parity_len, par_pres);
        CHECK(rc == 0, "group recovery must succeed");
        CHECK(memcmp(work, src, F) == 0,
              "recovered group source must match original");

        free(src); free(work); free(src_pres); free(par_pres); free(parity);
    }

    /* Test 12: FEC group primitive across multiple source blocks. */
    fprintf(stderr, "Test 12: FEC group primitive multi-block recovery...\n");
    {
        const uint16_t T = 128;
        /* Push K past AVT_FEC_MAX_SYMBOLS so the encoder picks Z >= 2. */
        const size_t K = 100000;
        const size_t F = K * T;
        uint8_t *src = malloc(F);
        uint8_t *work = malloc(F);
        CHECK(src && work, "multi-block group alloc");
        fill_pattern(src, F);
        memcpy(work, src, F);

        uint64_t common; uint32_t scheme;
        uint8_t *parity = NULL; size_t parity_len = 0;
        int rc = avt_fec_group_encode(NULL, src, F, T, /*repair=*/200,
                                      &common, &scheme,
                                      &parity, &parity_len);
        CHECK(rc == 0 && parity && parity_len > 0,
              "multi-block group encode");

        uint64_t Fv; uint16_t Tv, Nv; uint8_t Zv, Alv;
        avt_fec_oti_unpack(common, scheme, &Fv, &Tv, &Zv, &Nv, &Alv);
        CHECK(Zv >= 2, "expected multi-block partition");

        /* Erase a run of source symbols on each side of the block boundary,
         * each well within the per-block repair budget (200 symbols). */
        uint8_t *src_pres = malloc(K);
        memset(src_pres, 1, K);
        for (size_t e = 49900; e < 50000; e++) { /* 100 in block 0 */
            memset(work + e * T, 0xCD, T);
            src_pres[e] = 0;
        }
        for (size_t e = 50000; e < 50150; e++) { /* 150 in block 1 */
            memset(work + e * T, 0xCD, T);
            src_pres[e] = 0;
        }
        uint8_t *par_pres = malloc(parity_len / T);
        memset(par_pres, 1, parity_len / T);

        rc = avt_fec_group_decode(NULL, common, scheme, work, F,
                                  src_pres, parity, parity_len, par_pres);
        CHECK(rc == 0, "multi-block recovery must succeed");
        CHECK(memcmp(work, src, F) == 0,
              "multi-block recovered source must match");

        free(src); free(work); free(src_pres); free(par_pres); free(parity);
    }

    /* Test 13: FEC group decode bails cleanly when too many symbols are
     * missing for the available parity. */
    fprintf(stderr, "Test 13: FEC group insufficient symbols bail...\n");
    {
        const uint16_t T = 256;
        const uint16_t K = 10;
        const size_t F = (size_t)K * T;
        uint8_t *src = malloc(F);
        uint8_t *work = malloc(F);
        fill_pattern(src, F);
        memcpy(work, src, F);

        uint64_t common; uint32_t scheme;
        uint8_t *parity = NULL; size_t parity_len = 0;
        int rc = avt_fec_group_encode(NULL, src, F, T, /*repair=*/2,
                                      &common, &scheme,
                                      &parity, &parity_len);
        CHECK(rc == 0, "small group encode");

        uint8_t *src_pres = malloc(K);
        memset(src_pres, 1, K);
        /* Drop 3 symbols, but only 2 repair symbols exist — recovery
         * must report EAGAIN rather than success or a hard error. */
        for (uint16_t e = 0; e < 3; e++) {
            memset(work + (size_t)e * T, 0xEF, T);
            src_pres[e] = 0;
        }
        uint8_t *par_pres = malloc(parity_len / T);
        memset(par_pres, 1, parity_len / T);

        rc = avt_fec_group_decode(NULL, common, scheme, work, F,
                                  src_pres, parity, parity_len, par_pres);
        CHECK(rc == AVT_ERROR(EAGAIN),
              "group recovery without enough symbols must return EAGAIN");

        free(src); free(work); free(src_pres); free(par_pres); free(parity);
    }

    /* Test 14: end-to-end FEC group round trip via 0x0030 / 0x0031 wire
     * packet structures. Builds a synthetic group, serializes its header
     * packets, decodes them, drives them through the receiver, drops a
     * source packet, and verifies recovery. */
    fprintf(stderr, "Test 14: FEC group end-to-end wire round trip...\n");
    {
        const uint16_t nb_sources = 8;
        AVTFecGroupSource sources[8];
        uint8_t *sbufs[8] = { 0 };
        uint32_t ssizes[8] = { 0 };

        for (uint16_t i = 0; i < nb_sources; i++) {
            /* Vary sizes to exercise the per-symbol padding logic. */
            const uint32_t sz = 240 + (i * 17) % 80;
            sbufs[i] = malloc(sz);
            ssizes[i] = sz;
            CHECK(sbufs[i], "source buffer alloc");
            for (uint32_t k = 0; k < sz; k++)
                sbufs[i][k] = (uint8_t)(0xC0 + i * 13 + k);
            sources[i] = (AVTFecGroupSource){
                .seq       = 1000 + i,
                .stream_id = (uint16_t)(i & 1), /* alternate two streams */
                .bytes     = sbufs[i],
                .size      = sz,
            };
        }

        AVTFecGroupBuild build = { 0 };
        int rc = avt_fec_group_sender_build(NULL, sources, nb_sources,
                                            /*group_id=*/0xA1FE,
                                            /*repair=*/4,
                                            /*max_data_payload=*/1024,
                                            &build);
        CHECK(rc == 0, "sender build must succeed");
        CHECK(build.parity_len > 0, "non-empty parity");
        CHECK(build.nb_data >= 2, "expect multiple 0x0031 packets");
        CHECK(build.grouping.fec_grouping_streams == 2,
              "expect two streams in the group");

        /* Encode/decode the 0x0030 packet, verifying every interesting
         * field round-trips through the wire format. */
        AVTPktd grouping_pkt = { 0 };
        grouping_pkt.pkt.fec_grouping = build.grouping;
        avt_packet_encode_header(&grouping_pkt);
        AVTFecGrouping g2 = { 0 };
        AVTBytestream bsg = avt_bs_init(grouping_pkt.hdr,
                                        sizeof(grouping_pkt.hdr));
        avt_decode_fec_grouping(&bsg, &g2);
        CHECK(g2.group_id == 0xA1FE, "group_id round-trips");
        CHECK(g2.fec_common_oti == build.grouping.fec_common_oti,
              "common OTI round-trips");
        CHECK(g2.fec_scheme_oti == build.grouping.fec_scheme_oti,
              "scheme OTI round-trips");
        CHECK(g2.fec_grouping_streams == build.grouping.fec_grouping_streams,
              "stream count round-trips");
        CHECK(g2.fec_start_global_seq == build.grouping.fec_start_global_seq,
              "start seq round-trips");
        for (uint8_t s = 0; s < g2.fec_grouping_streams; s++) {
            CHECK(g2.fec_nb_packets[s] == build.grouping.fec_nb_packets[s],
                  "per-stream packet count round-trips");
            CHECK(g2.fec_seq_number[s] == build.grouping.fec_seq_number[s],
                  "per-stream start seq round-trips");
        }

        /* Initialize the receiver state from the decoded 0x0030 packet. */
        AVTFecGroupRx rx = { 0 };
        rc = avt_fec_group_rx_init(&rx, &g2, nb_sources);
        CHECK(rc == 0, "rx init");

        /* Round-trip each 0x0031 packet through wire encode/decode, and
         * feed both header and (real) parity bytes into the receiver. */
        for (uint32_t i = 0; i < build.nb_data; i++) {
            AVTPktd dp = { 0 };
            dp.pkt.fec_group_data = build.data[i];
            avt_packet_encode_header(&dp);

            AVTFecGroupData d2 = { 0 };
            AVTBytestream bsd = avt_bs_init(dp.hdr, sizeof(dp.hdr));
            avt_decode_fec_group_data(&bsd, &d2);
            CHECK(d2.group_id == build.data[i].group_id,
                  "0x0031 group_id round-trips");
            CHECK(d2.fec_data_offset == build.data[i].fec_data_offset,
                  "0x0031 fec_data_offset round-trips");
            CHECK(d2.fec_data_length == build.data[i].fec_data_length,
                  "0x0031 fec_data_length round-trips");
            CHECK(d2.fec_total_data_length ==
                  build.data[i].fec_total_data_length,
                  "0x0031 fec_total_data_length round-trips");
            CHECK(d2.fec_source_1 == build.data[i].fec_source_1,
                  "fec_source_1 round-trips");
            for (int k = 0; k < 3; k++)
                CHECK(d2.fec_source_234[k] ==
                      build.data[i].fec_source_234[k],
                      "fec_source_234[k] round-trips");

            const uint8_t *fec_chunk = build.parity + d2.fec_data_offset;
            rc = avt_fec_group_rx_add_data(&rx, &d2, fec_chunk,
                                           d2.fec_data_length);
            CHECK(rc == 0, "rx add data");
        }

        /* Feed seven of the eight source packets — packet `dropped` is
         * the one that must be reconstructed by FEC. */
        const uint16_t dropped = 3;
        for (uint16_t i = 0; i < nb_sources; i++) {
            if (i == dropped)
                continue;
            rc = avt_fec_group_rx_add_source(&rx, sources[i].seq,
                                             sources[i].bytes,
                                             sources[i].size);
            CHECK(rc == 0, "rx add source");
        }

        rc = avt_fec_group_rx_try_recover(NULL, &rx);
        CHECK(rc == 0, "recovery succeeds with parity + N-1 sources");

        /* The recovered source object's symbol for the dropped packet
         * must contain the original bytes (zero-padded to T). */
        const uint8_t *slot = rx.src + (size_t)dropped * rx.T;
        CHECK(memcmp(slot, sbufs[dropped], ssizes[dropped]) == 0,
              "recovered packet bytes match the original");
        for (uint32_t k = ssizes[dropped]; k < rx.T; k++)
            CHECK(slot[k] == 0, "recovered packet's padding is zero");

        avt_fec_group_rx_free(&rx);
        avt_fec_group_sender_free(&build);
        for (uint16_t i = 0; i < nb_sources; i++)
            free(sbufs[i]);
    }

    /* Test 15: a receiver fed only parity and one source must request
     * more data rather than claim success. */
    fprintf(stderr, "Test 15: FEC group receiver bails on too few symbols...\n");
    {
        const uint16_t nb_sources = 4;
        uint8_t *sbufs[4];
        AVTFecGroupSource sources[4];
        for (uint16_t i = 0; i < nb_sources; i++) {
            sbufs[i] = malloc(200);
            for (uint32_t k = 0; k < 200; k++)
                sbufs[i][k] = (uint8_t)(i * 17 + k);
            sources[i] = (AVTFecGroupSource){
                .seq       = 100 + i,
                .stream_id = 0,
                .bytes     = sbufs[i],
                .size      = 200,
            };
        }
        AVTFecGroupBuild build = { 0 };
        int rc = avt_fec_group_sender_build(NULL, sources, nb_sources,
                                            7, /*repair=*/1,
                                            /*max_data_payload=*/64, &build);
        CHECK(rc == 0, "build for under-budget test");

        AVTFecGroupRx rx = { 0 };
        rc = avt_fec_group_rx_init(&rx, &build.grouping, nb_sources);
        CHECK(rc == 0, "rx init for under-budget");

        for (uint32_t i = 0; i < build.nb_data; i++) {
            const uint8_t *fec = build.parity + build.data[i].fec_data_offset;
            rc = avt_fec_group_rx_add_data(&rx, &build.data[i], fec,
                                           build.data[i].fec_data_length);
            CHECK(rc == 0, "under-budget add data");
        }
        /* Only feed one source (drop three): with only one repair symbol
         * per block, recovery is impossible. */
        rc = avt_fec_group_rx_add_source(&rx, sources[0].seq, sources[0].bytes,
                                         sources[0].size);
        CHECK(rc == 0, "under-budget add source");

        rc = avt_fec_group_rx_try_recover(NULL, &rx);
        CHECK(rc == AVT_ERROR(EAGAIN),
              "under-budget recovery must yield EAGAIN");

        avt_fec_group_rx_free(&rx);
        avt_fec_group_sender_free(&build);
        for (uint16_t i = 0; i < nb_sources; i++)
            free(sbufs[i]);
    }

    fprintf(stderr, "All FEC tests passed.\n");

end:
    if (p10)   avt_buffer_unref(&p10);
    if (p100)  avt_buffer_unref(&p100);
    if (p0)    avt_buffer_unref(&p0);
    if (ptiny) avt_buffer_unref(&ptiny);
    avt_buffer_quick_unref(&pl);
    avt_buffer_quick_unref(&tiny);
    avt_buffer_quick_unref(&big);
    return ret;
}
