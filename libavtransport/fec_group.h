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

/*
 * Cross-stream FEC grouping helpers (spec sections 0x0030 / 0x0031).
 *
 * These primitives sit one level above fec.h: they assemble the source
 * object from individual packet byte-streams, plumb the wire OTI through
 * AVTFecGrouping / AVTFecGroupData packets, and on the receive side
 * gather source + parity into a single recovery attempt.
 *
 * The fec.c primitives are RaptorQ-only and nanorq-aware; everything
 * here is pure C and lives in the main libavtransport, so any code that
 * already includes packet_data.h can use it.
 */

#ifndef AVTRANSPORT_FEC_GROUP_H
#define AVTRANSPORT_FEC_GROUP_H

#include <stddef.h>
#include <stdint.h>

#include <avtransport/packet_data.h>

/* Maximum number of FEC sources (source-packet descriptors) that fit in
 * a single AVTFecGroupData (0x0031) packet — four 64-bit slots. */
#define AVT_FEC_GROUP_SOURCES_PER_DATA 4

/* Maximum number of streams a single FEC group may cover, per the spec
 * fec_grouping_streams field (and the size of AVTFecGrouping arrays). */
#define AVT_FEC_GROUP_MAX_STREAMS 16

/* Description of a single source packet that participates in an FEC
 * group. `bytes` is the serialized (header + payload) byte stream of
 * the protected packet — what the receiver would reconstruct on a
 * successful FEC pass. `seq` and `stream_id` match the packet's wire
 * header. */
typedef struct AVTFecGroupSource {
    uint64_t       seq;       /* fec_source_seq — the packet's global_seq */
    uint16_t       stream_id; /* protected packet's stream id */
    const uint8_t *bytes;     /* serialized header+payload of the packet */
    uint32_t       size;      /* length of `bytes` in bytes */
} AVTFecGroupSource;

/* Pack/unpack a single FECSource descriptor (the wire-format 64-bit
 * tuple of seq | block | symbol-id). The wire ordering is big-endian
 * 32+16+16 bits, matching how the spec lays out the field. */
static inline uint64_t avt_fec_source_pack(uint32_t seq, uint16_t blk,
                                           uint16_t sym)
{
    return ((uint64_t)seq << 32) |
           ((uint64_t)blk << 16) |
            (uint64_t)sym;
}

static inline void avt_fec_source_unpack(uint64_t v, uint32_t *seq,
                                         uint16_t *blk, uint16_t *sym)
{
    *seq = (uint32_t)(v >> 32);
    *blk = (uint16_t)((v >> 16) & 0xFFFF);
    *sym = (uint16_t)(v & 0xFFFF);
}

/* Sender output: the wire structures and the raw parity blob produced
 * by avt_fec_group_sender_build(). The caller takes ownership of every
 * pointer and must free them via avt_fec_group_sender_free(). */
typedef struct AVTFecGroupBuild {
    AVTFecGrouping  grouping;        /* the 0x0030 packet header */
    AVTFecGroupData *data;           /* array of 0x0031 packet headers */
    uint32_t         nb_data;        /* number of entries in `data` */
    uint8_t         *parity;         /* full parity blob */
    size_t           parity_len;
    uint16_t         symbol_size;    /* T, the chosen symbol size */
    uint8_t          z_blocks;       /* number of source blocks (Z) */
} AVTFecGroupBuild;

/* Build an FEC group covering `nb_sources` source packets.
 *
 * Picks T = max(source.size), rounded up to the alignment chosen by
 * fec.c (AVT_FEC_GROUP_OTI_AL = 4). Concatenates each source packet's
 * `bytes` into a single source object, zero-padding short packets to T;
 * then asks avt_fec_group_encode() for `repair_per_block` repair
 * symbols per source block.
 *
 * The resulting `out->grouping` packet describes the group (with
 * fec_grouping_streams up to AVT_FEC_GROUP_MAX_STREAMS, and the
 * per-stream packet counts / starting seqs derived from `sources`),
 * and `out->data[]` is split into roughly equal-sized chunks of the
 * parity blob. Each `out->data[i]` carries up to four FECSource
 * descriptors so the receiver can rebuild the seq → (block, symbol)
 * mapping if the 0x0030 packet itself is lost; descriptors wrap once
 * every source has been described once.
 *
 * Returns 0 on success, or a negative AVT_ERROR(). On error nothing in
 * `out` is allocated. */
int avt_fec_group_sender_build(void *log_ctx,
                               const AVTFecGroupSource *sources,
                               uint16_t nb_sources,
                               uint16_t group_id,
                               uint16_t repair_per_block,
                               uint32_t max_data_pkt_payload,
                               AVTFecGroupBuild *out);

void avt_fec_group_sender_free(AVTFecGroupBuild *b);

/* Receive-side FEC group state. One per group_id in flight. Callers
 * call avt_fec_group_rx_init() once a 0x0030 packet has been received,
 * then feed in source packets and 0x0031 parity fragments until either
 * the group is complete or avt_fec_group_rx_try_recover() succeeds. */
/* A source packet that arrived before any 0x0031 named its seq. Kept
 * inside the AVTFecGroupRx until a later add_data() establishes the
 * (seq → slot) mapping, at which point the bytes are copied into the
 * rx->src scratch and the pending entry cleared. */
typedef struct AVTFecGroupPendingSrc {
    uint64_t  seq;
    uint8_t  *bytes;     /* owned by the rx; freed at flush or rx_free */
    uint32_t  size;
} AVTFecGroupPendingSrc;

typedef struct AVTFecGroupRx {
    AVTFecGrouping grouping;
    uint64_t       common_oti;
    uint32_t       scheme_oti;
    uint16_t       T;              /* symbol size */
    uint16_t       Kt;             /* total source symbols == nb_sources */
    uint8_t       *src;            /* Kt * T bytes, source object scratch */
    uint8_t       *src_present;    /* Kt bytes */
    uint8_t       *src_was_present;/* snapshot taken at recover time */

    /* Parity blob is filled lazily as 0x0031 packets arrive. */
    uint8_t       *parity;
    uint8_t       *parity_present; /* one byte per T-sized slot */
    size_t         parity_len;     /* fec_total_data_length */
    size_t         parity_slots;   /* parity_len / T */

    /* seq → (block, symbol) lookup; populated from the FECSource
     * entries inside received 0x0031 packets. */
    uint64_t      *seq_table;      /* Kt entries, packed avt_fec_source_pack */
    uint8_t       *seq_table_set;  /* Kt bytes, flags */

    /* Sources that arrived before their seq → slot mapping was known.
     * Bounded by Kt (one entry per source). */
    AVTFecGroupPendingSrc *pending;
    uint16_t               nb_pending;

    bool           active;
    bool           recovered;
} AVTFecGroupRx;

/* Initialize a receiver group context from a received 0x0030 packet.
 * Wipes any previous state — destroy via avt_fec_group_rx_free(). */
int avt_fec_group_rx_init(AVTFecGroupRx *rx, const AVTFecGrouping *grouping,
                          uint16_t nb_sources);

void avt_fec_group_rx_free(AVTFecGroupRx *rx);

/* Place a received source packet into the right symbol slot, using the
 * seq → slot mapping built from 0x0031 packets. Returns 0 on success,
 * AVT_ERROR(EAGAIN) if the slot is not yet known (no 0x0031 with that
 * seq seen), or a negative AVT_ERROR() otherwise. */
int avt_fec_group_rx_add_source(AVTFecGroupRx *rx,
                                uint64_t seq,
                                const uint8_t *bytes, uint32_t size);

/* Absorb one 0x0031 packet: copies its parity bytes into the right
 * place in the blob and records the FECSource entries. */
int avt_fec_group_rx_add_data(AVTFecGroupRx *rx,
                              const AVTFecGroupData *data,
                              const uint8_t *fec_bytes, size_t fec_len);

/* Attempt FEC recovery from the current state. Returns 0 on success
 * (rx->src holds the full source object), AVT_ERROR(EAGAIN) if more
 * symbols are needed, or another negative AVT_ERROR(). On success
 * `rx->recovered` is set to true; subsequent calls return 0 directly.
 *
 * `rx->src_was_present` is maintained continuously: it is set to 1
 * every time a packet's bytes were placed via add_source. After
 * recovery, the caller can walk slots with `src_was_present[i] == 0`
 * to find slots the decoder filled in that haven't been delivered
 * upstream yet. The caller sets that flag to 1 once it has dispatched
 * the recovered packet so subsequent recovery sweeps don't redeliver. */
int avt_fec_group_rx_try_recover(void *log_ctx, AVTFecGroupRx *rx);

#endif /* AVTRANSPORT_FEC_GROUP_H */
