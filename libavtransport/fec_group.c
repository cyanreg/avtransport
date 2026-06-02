/*
 * Copyright © 2026, Lynne
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <avtransport/avtransport.h>

#include "fec.h"
#include "fec_group.h"
#include "utils_internal.h"

/* The FEC source-object alignment used by avt_fec_group_encode(); also
 * the wire alignment carried in the scheme OTI. */
#define FEC_GROUP_AL 4

/* Round `v` up to the nearest multiple of `m`, with `m` a power of two. */
static inline uint32_t round_up_pow2(uint32_t v, uint32_t m)
{
    return (v + (m - 1)) & ~(m - 1);
}

void avt_fec_group_sender_free(AVTFecGroupBuild *b)
{
    if (!b)
        return;
    free(b->data);
    free(b->parity);
    memset(b, 0, sizeof(*b));
}

int avt_fec_group_sender_build(void *log_ctx,
                               const AVTFecGroupSource *sources,
                               uint16_t nb_sources,
                               uint16_t group_id,
                               uint16_t repair_per_block,
                               uint32_t max_data_pkt_payload,
                               AVTFecGroupBuild *out)
{
    if (!sources || nb_sources < 1 || !out || !max_data_pkt_payload)
        return AVT_ERROR(EINVAL);

    memset(out, 0, sizeof(*out));

    /* Pick T from the largest input packet, rounded up to alignment. */
    uint32_t max_size = 0;
    for (uint16_t i = 0; i < nb_sources; i++) {
        if (!sources[i].bytes || !sources[i].size)
            return AVT_ERROR(EINVAL);
        if (sources[i].size > max_size)
            max_size = sources[i].size;
    }
    uint32_t T = round_up_pow2(max_size, FEC_GROUP_AL);
    if (T > UINT16_MAX)
        return AVT_ERROR(EINVAL);

    const size_t F = (size_t)T * nb_sources;
    uint8_t *src = calloc(1, F);
    if (!src)
        return AVT_ERROR(ENOMEM);

    /* Source-object packing: each packet's serialized bytes go into one
     * symbol slot, zero-padded to T. */
    for (uint16_t i = 0; i < nb_sources; i++)
        memcpy(src + (size_t)i * T, sources[i].bytes, sources[i].size);

    uint64_t common = 0; uint32_t scheme = 0;
    uint8_t *parity = NULL; size_t parity_len = 0;
    int err = avt_fec_group_encode(log_ctx, src, F, (uint16_t)T,
                                   repair_per_block,
                                   &common, &scheme,
                                   &parity, &parity_len);
    free(src);
    if (err < 0)
        return err;

    uint64_t Fv; uint16_t Tv, Nv; uint8_t Zv, Alv;
    avt_fec_oti_unpack(common, scheme, &Fv, &Tv, &Zv, &Nv, &Alv);

    /* Build the 0x0030 packet. Streams are aggregated by stream_id in
     * first-seen order; per-stream packet counts and starting seqs are
     * computed by a linear pass over `sources`.  */
    AVTFecGrouping g = (AVTFecGrouping){
        .fec_grouping_descriptor = AVT_PKT_FEC_GROUPING,
        .group_id                = group_id,
        .fec_common_oti          = common,
        .fec_scheme_oti          = scheme,
        .fec_start_global_seq    = sources[0].seq,
    };

    uint16_t streams[AVT_FEC_GROUP_MAX_STREAMS];
    uint32_t nb_pkts[AVT_FEC_GROUP_MAX_STREAMS] = { 0 };
    uint64_t first_seq[AVT_FEC_GROUP_MAX_STREAMS];
    uint8_t  nb_streams = 0;

    for (uint16_t i = 0; i < nb_sources; i++) {
        int slot = -1;
        for (uint8_t s = 0; s < nb_streams; s++) {
            if (streams[s] == sources[i].stream_id) {
                slot = s;
                break;
            }
        }
        if (slot < 0) {
            if (nb_streams >= AVT_FEC_GROUP_MAX_STREAMS) {
                free(parity);
                return AVT_ERROR(EINVAL);
            }
            slot = nb_streams++;
            streams[slot] = sources[i].stream_id;
            first_seq[slot] = sources[i].seq;
        }
        nb_pkts[slot]++;
    }

    g.fec_grouping_streams = nb_streams;
    for (uint8_t s = 0; s < nb_streams; s++) {
        g.fec_nb_packets[s] = nb_pkts[s];
        g.fec_seq_number[s] = first_seq[s];
    }
    /* Unused entries already zero. */

    /* Compute how many 0x0031 packets we need: enough to carry every
     * parity byte AND enough to describe every source packet at least
     * once (four descriptors per data packet). */
    const uint32_t per_slice = max_data_pkt_payload;
    uint32_t nb_data_by_par = (uint32_t)((parity_len + per_slice - 1) /
                                         per_slice);
    if (!nb_data_by_par)
        nb_data_by_par = 1;
    const uint32_t nb_data_by_src =
        (nb_sources + AVT_FEC_GROUP_SOURCES_PER_DATA - 1) /
        AVT_FEC_GROUP_SOURCES_PER_DATA;
    uint32_t nb_data = nb_data_by_par > nb_data_by_src ? nb_data_by_par
                                                        : nb_data_by_src;

    AVTFecGroupData *data = calloc(nb_data, sizeof(*data));
    if (!data) {
        free(parity);
        return AVT_ERROR(ENOMEM);
    }

    /* Compute per-packet block & symbol indices by repartitioning Kt
     * into Z source blocks using the same rule nanorq uses (long/short
     * partition). With nb_sources up to AVT_FEC_MAX_SYMBOLS * 256 we
     * still keep it simple and walk symbol-by-symbol. */
    const uint16_t Kt = nb_sources;
    const uint32_t Z = Zv;
    const uint32_t IL = (Kt + Z - 1) / Z;
    const uint32_t IS = Kt / Z;
    const uint32_t JL = Kt - IS * Z;
    /* Block b is "long" (IL symbols) iff b < JL, otherwise "short" (IS). */

    uint64_t *src_pack = malloc((size_t)Kt * sizeof(*src_pack));
    if (!src_pack) {
        free(data);
        free(parity);
        return AVT_ERROR(ENOMEM);
    }
    {
        uint32_t walked = 0;
        for (uint32_t b = 0; b < Z; b++) {
            const uint32_t kb = (b < JL) ? IL : IS;
            for (uint32_t e = 0; e < kb; e++) {
                src_pack[walked] = avt_fec_source_pack(
                    (uint32_t)sources[walked].seq,
                    (uint16_t)b, (uint16_t)e);
                walked++;
            }
        }
    }

    /* Now fill in each 0x0031 packet. Slice the parity blob linearly;
     * cycle the FECSource descriptors so each source gets named at least
     * once even if there are more data packets than source-quartets. */
    uint32_t src_cursor = 0;
    for (uint32_t i = 0; i < nb_data; i++) {
        const uint64_t off = (uint64_t)i * per_slice;
        uint32_t len;
        if (off >= parity_len) {
            len = 0;
        } else {
            const uint64_t remain = parity_len - off;
            len = remain > per_slice ? per_slice : (uint32_t)remain;
        }

        data[i] = (AVTFecGroupData){
            .fec_group_data_descriptor = AVT_PKT_FEC_GROUP_DATA,
            .group_id                  = group_id,
            .fec_data_offset           = (uint32_t)off,
            .fec_data_length           = len,
            .fec_total_data_length     = (uint32_t)parity_len,
        };

        data[i].fec_source_1 = src_pack[src_cursor];
        src_cursor = (src_cursor + 1) % Kt;
        for (int k = 0; k < 3; k++) {
            data[i].fec_source_234[k] = src_pack[src_cursor];
            src_cursor = (src_cursor + 1) % Kt;
        }
    }

    free(src_pack);
    out->grouping    = g;
    out->data        = data;
    out->nb_data     = nb_data;
    out->parity      = parity;
    out->parity_len  = parity_len;
    out->symbol_size = (uint16_t)T;
    out->z_blocks    = (uint8_t)Z;

    avt_log(log_ctx, AVT_LOG_DEBUG,
            "FEC group sender: %u sources, T=%u, Z=%u, %u data pkts, "
            "%zu B parity\n",
            nb_sources, T, Z, nb_data, parity_len);

    return 0;
}

/* ---- Receive side --------------------------------------------------- */

int avt_fec_group_rx_init(AVTFecGroupRx *rx, const AVTFecGrouping *grouping,
                          uint16_t nb_sources)
{
    avt_fec_group_rx_free(rx);

    if (!grouping || !nb_sources || grouping->fec_grouping_streams >
                                    AVT_FEC_GROUP_MAX_STREAMS)
        return AVT_ERROR(EINVAL);

    uint64_t F; uint16_t T, N; uint8_t Z, Al;
    avt_fec_oti_unpack(grouping->fec_common_oti, grouping->fec_scheme_oti,
                       &F, &T, &Z, &N, &Al);
    if (!T)
        return AVT_ERROR(EINVAL);

    /* Kt must equal nb_sources (one symbol per source packet). */
    const size_t Kt_from_oti = F / T;
    if (Kt_from_oti != nb_sources || F != (size_t)nb_sources * T)
        return AVT_ERROR(EINVAL);

    rx->grouping        = *grouping;
    rx->common_oti      = grouping->fec_common_oti;
    rx->scheme_oti      = grouping->fec_scheme_oti;
    rx->T               = T;
    rx->Kt              = nb_sources;
    rx->src             = calloc(1, (size_t)nb_sources * T);
    rx->src_present     = calloc(1, nb_sources);
    rx->src_was_present = calloc(1, nb_sources);
    rx->seq_table       = calloc(nb_sources, sizeof(*rx->seq_table));
    rx->seq_table_set   = calloc(1, nb_sources);
    rx->pending         = calloc(nb_sources, sizeof(*rx->pending));
    if (!rx->src || !rx->src_present || !rx->src_was_present ||
        !rx->seq_table || !rx->seq_table_set || !rx->pending) {
        avt_fec_group_rx_free(rx);
        return AVT_ERROR(ENOMEM);
    }

    rx->active    = true;
    rx->recovered = false;
    return 0;
}

void avt_fec_group_rx_free(AVTFecGroupRx *rx)
{
    if (!rx)
        return;
    free(rx->src);
    free(rx->src_present);
    free(rx->src_was_present);
    free(rx->parity);
    free(rx->parity_present);
    free(rx->seq_table);
    free(rx->seq_table_set);
    if (rx->pending) {
        for (uint16_t i = 0; i < rx->nb_pending; i++)
            free(rx->pending[i].bytes);
        free(rx->pending);
    }
    memset(rx, 0, sizeof(*rx));
}

/* Look up the source-symbol index a seq lives at, walking the mapping
 * we've accumulated from received FECSource entries. Returns -1 if
 * no 0x0031 has mentioned this seq yet. */
static int rx_lookup_seq(const AVTFecGroupRx *rx, uint32_t seq32)
{
    for (uint32_t i = 0; i < rx->Kt; i++) {
        if (!rx->seq_table_set[i])
            continue;
        uint32_t s; uint16_t b, sy;
        avt_fec_source_unpack(rx->seq_table[i], &s, &b, &sy);
        if (s == seq32)
            return (int)i;
    }
    return -1;
}

/* Copy `bytes`/`size` into the source-object slot `idx`, zero-padding to T.
 *
 * Both `src_present` (FEC's "we have this symbol") and `src_was_present`
 * (caller's "we have delivered this packet") are set, since direct
 * receipt counts as delivery without further reinjection. */
static void rx_place_source(AVTFecGroupRx *rx, int idx,
                            const uint8_t *bytes, uint32_t size)
{
    uint8_t *slot = rx->src + (size_t)idx * rx->T;
    memcpy(slot, bytes, size);
    if (size < rx->T)
        memset(slot + size, 0, (size_t)rx->T - size);
    rx->src_present[idx]     = 1;
    rx->src_was_present[idx] = 1;
}

int avt_fec_group_rx_add_source(AVTFecGroupRx *rx,
                                uint64_t seq,
                                const uint8_t *bytes, uint32_t size)
{
    if (!rx || !rx->active || !bytes || !size || size > rx->T)
        return AVT_ERROR(EINVAL);

    int idx = rx_lookup_seq(rx, (uint32_t)seq);
    if (idx >= 0) {
        rx_place_source(rx, idx, bytes, size);
        return 0;
    }

    /* No 0x0031 has named this seq yet — defer until one does. We
     * silently dedup so a retransmit doesn't grow `pending` unbounded. */
    if (rx->nb_pending >= rx->Kt)
        return AVT_ERROR(EAGAIN);
    for (uint16_t i = 0; i < rx->nb_pending; i++) {
        if (rx->pending[i].seq == seq)
            return AVT_ERROR(EAGAIN);
    }

    uint8_t *copy = malloc(size);
    if (!copy)
        return AVT_ERROR(ENOMEM);
    memcpy(copy, bytes, size);

    rx->pending[rx->nb_pending++] = (AVTFecGroupPendingSrc){
        .seq   = seq,
        .bytes = copy,
        .size  = size,
    };
    return AVT_ERROR(EAGAIN);
}

/* After new (seq → slot) mappings have been recorded by add_data(), try
 * to place any deferred source packets whose seq is now resolvable. */
static void rx_flush_pending(AVTFecGroupRx *rx)
{
    for (uint16_t i = 0; i < rx->nb_pending; ) {
        int idx = rx_lookup_seq(rx, (uint32_t)rx->pending[i].seq);
        if (idx < 0) {
            i++;
            continue;
        }
        rx_place_source(rx, idx, rx->pending[i].bytes, rx->pending[i].size);
        free(rx->pending[i].bytes);
        rx->pending[i] = rx->pending[--rx->nb_pending];
    }
}

/* Make sure the lazily-allocated parity buffer can hold `len` bytes,
 * sized for `len / T` symbol slots. */
static int rx_ensure_parity(AVTFecGroupRx *rx, size_t len)
{
    if (rx->parity && rx->parity_len == len)
        return 0;
    if (rx->parity)
        return rx->parity_len == len ? 0 : AVT_ERROR(EINVAL);

    if (len % rx->T)
        return AVT_ERROR(EINVAL);

    rx->parity = calloc(1, len);
    rx->parity_slots = len / rx->T;
    rx->parity_present = calloc(1, rx->parity_slots);
    if (!rx->parity || !rx->parity_present) {
        free(rx->parity);
        free(rx->parity_present);
        rx->parity = NULL;
        rx->parity_present = NULL;
        return AVT_ERROR(ENOMEM);
    }
    rx->parity_len = len;
    return 0;
}

/* Record one fec_source uint64 into the seq→slot mapping. Returns 0
 * regardless of whether the entry was new or already present. */
static int rx_record_source(AVTFecGroupRx *rx, uint64_t v)
{
    uint32_t seq; uint16_t blk, sym;
    avt_fec_source_unpack(v, &seq, &blk, &sym);

    /* Find the source-object index for (blk, sym) by walking nanorq's
     * fill_partition rule. Build IL/IS/JL on-the-fly from rx->Kt and
     * the OTI's Z. */
    uint64_t F; uint16_t T, N; uint8_t Z, Al;
    avt_fec_oti_unpack(rx->common_oti, rx->scheme_oti, &F, &T, &Z, &N, &Al);
    if (Z == 0)
        return AVT_ERROR(EINVAL);

    const uint32_t Kt = rx->Kt;
    const uint32_t IL = (Kt + Z - 1) / Z;
    const uint32_t IS = Kt / Z;
    const uint32_t JL = Kt - IS * Z;

    if (blk >= Z)
        return AVT_ERROR(EINVAL);

    uint32_t idx;
    if (blk < JL)
        idx = blk * IL + sym;
    else
        idx = JL * IL + (blk - JL) * IS + sym;
    if (idx >= Kt)
        return AVT_ERROR(EINVAL);

    rx->seq_table[idx] = v;
    rx->seq_table_set[idx] = 1;
    return 0;
}

int avt_fec_group_rx_add_data(AVTFecGroupRx *rx,
                              const AVTFecGroupData *data,
                              const uint8_t *fec_bytes, size_t fec_len)
{
    if (!rx || !rx->active || !data)
        return AVT_ERROR(EINVAL);

    /* The signalled total parity length pins down the symbol slot count;
     * once seen it must be consistent across all 0x0031 packets. */
    if (data->fec_total_data_length) {
        int err = rx_ensure_parity(rx, data->fec_total_data_length);
        if (err < 0)
            return err;
    }

    /* Drop the FEC parity bytes into the right place and mark the
     * fully-covered symbol slots as present. */
    if (fec_bytes && fec_len) {
        if (data->fec_data_offset > rx->parity_len ||
            (size_t)data->fec_data_offset + fec_len > rx->parity_len)
            return AVT_ERROR(EINVAL);
        memcpy(rx->parity + data->fec_data_offset, fec_bytes, fec_len);

        const uint32_t s_lo =
            (data->fec_data_offset + rx->T - 1) / rx->T;
        const uint32_t s_hi =
            (uint32_t)((data->fec_data_offset + fec_len) / rx->T);
        for (uint32_t s = s_lo; s < s_hi; s++) {
            if (s < rx->parity_slots)
                rx->parity_present[s] = 1;
        }
    }

    /* Record the four source mappings. */
    int rret = rx_record_source(rx, data->fec_source_1);
    if (rret < 0)
        return rret;
    for (int k = 0; k < 3; k++) {
        rret = rx_record_source(rx, data->fec_source_234[k]);
        if (rret < 0)
            return rret;
    }

    /* New seq→slot mappings may have just become known: place any
     * deferred sources whose seq matches. */
    rx_flush_pending(rx);

    return 0;
}

int avt_fec_group_rx_try_recover(void *log_ctx, AVTFecGroupRx *rx)
{
    if (!rx || !rx->active)
        return AVT_ERROR(EINVAL);
    if (rx->recovered)
        return 0;
    if (!rx->parity)
        return AVT_ERROR(EAGAIN);

    int err = avt_fec_group_decode(log_ctx, rx->common_oti, rx->scheme_oti,
                                   rx->src, (size_t)rx->Kt * rx->T,
                                   rx->src_present,
                                   rx->parity, rx->parity_len,
                                   rx->parity_present);
    if (err == 0) {
        rx->recovered = true;
        for (uint32_t i = 0; i < rx->Kt; i++)
            rx->src_present[i] = 1;
        /* src_was_present is left alone — callers use it to find slots
         * the decoder filled in that haven't been delivered yet. */
    }
    return err;
}
