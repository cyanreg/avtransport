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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <avtransport/avtransport.h>

#include "reorder.h"
#include "mem.h"
#include "utils_packet.h"
#include "protocol_common.h"
#include "buffer.h"

FN_CREATING(avt_reorder, AVTReorder, AVTPacketFifo,
            bucket, buckets, nb_buckets)

/* Forward */
static int process_one_packet(AVTReorder *r, AVTPktd *p);

int avt_reorder_init(AVTContext *ctx, AVTReorder *r, size_t max_size)
{
    r->ctx = ctx;
    r->max_size = max_size;
    return 0;
}

/* Get a bucket: take from the available pool or allocate a new one. */
static AVTPacketFifo *reorder_get_bucket(AVTReorder *r)
{
    if (r->nb_avail_buckets > 0)
        return r->avail_buckets[--r->nb_avail_buckets];
    return avt_reorder_create_bucket(r);
}

/* Push a single completed packet into the staging bucket. */
static int push_to_staging(AVTReorder *r, AVTPktd *p)
{
    if (!r->staging) {
        r->staging = reorder_get_bucket(r);
        if (!r->staging)
            return AVT_ERROR(ENOMEM);
    }
    return avt_pkt_fifo_push_refd(r->staging, p);
}

/* Push a packet which is just a reference (no payload ownership transfer). */
static int push_ref_to_staging(AVTReorder *r, AVTPktd *p)
{
    if (!r->staging) {
        r->staging = reorder_get_bucket(r);
        if (!r->staging)
            return AVT_ERROR(ENOMEM);
    }
    return avt_pkt_fifo_push(r->staging, p);
}

/* Try to drain a merger that just became complete, pushing the assembled
 * packet to staging. force=1 emits whatever data we have, useful when evicting. */
static int try_emit_merger(AVTReorder *r, AVTReorderStream *rs, AVTMerger *m,
                           int force)
{
    AVTPktd out = { };
    int ret = avt_pkt_merge_out(r->ctx, m, &out, force);
    if (ret == AVT_ERROR(EAGAIN) || ret == AVT_ERROR(ENOENT))
        return ret;
    if (ret < 0) {
        avt_pkt_merge_done(m);
        rs->nb_groups--;
        return ret;
    }

    int push_ret = push_to_staging(r, &out);
    if (push_ret < 0) {
        avt_buffer_quick_unref(&out.pl);
        avt_pkt_merge_done(m);
        rs->nb_groups--;
        return push_ret;
    }

    /* merge_out already deactivated the merger */
    rs->nb_groups--;
    return 0;
}

/* Pick a merger slot for a given target.
 * Returns the matching merger if one exists, or an inactive slot. If all slots
 * are occupied, evicts the oldest one (force-outputs it) and returns its slot.
 * Returns NULL on allocation failure during eviction.
 *
 * Note: AVTMerger stores target as uint32_t; we match on that width. */
static AVTMerger *pick_merger(AVTReorder *r, AVTReorderStream *rs,
                              uint32_t target)
{
    int free_slot = -1;
    int oldest_slot = 0;
    uint32_t oldest_last = UINT32_MAX;

    for (int j = 0; j < AVT_REORDER_GROUP_NB; j++) {
        AVTMerger *m = &rs->m[j];
        if (m->active) {
            if (m->target == target)
                return m;
            if (m->last < oldest_last) {
                oldest_last = m->last;
                oldest_slot = j;
            }
        } else if (free_slot < 0) {
            free_slot = j;
        }
    }

    if (free_slot >= 0)
        return &rs->m[free_slot];

    /* All slots full: force out the oldest one to make room. */
    AVTMerger *evict = &rs->m[oldest_slot];
    AVTPktd out = { };
    int ret = avt_pkt_merge_out(r->ctx, evict, &out, 1);
    if (ret >= 0) {
        if (push_to_staging(r, &out) < 0) {
            avt_buffer_quick_unref(&out.pl);
            return NULL;
        }
    } else {
        avt_pkt_merge_done(evict);
    }
    rs->nb_groups--;

    return &rs->m[oldest_slot];
}

/* Add a group_id to the active-groups list if not already there. */
static void fec_mark_active(AVTReorder *r, uint16_t group_id)
{
    for (uint16_t i = 0; i < r->nb_active_fec_groups; i++) {
        if (r->active_fec_groups[i] == group_id)
            return;
    }
    r->active_fec_groups[r->nb_active_fec_groups++] = group_id;
}

/* Compute Kt = total source packets in the group = sum of per-stream
 * fec_nb_packets[]. */
static uint32_t fec_group_kt(const AVTFecGrouping *g)
{
    uint32_t total = 0;
    for (uint8_t i = 0; i < g->fec_grouping_streams &&
                        i < AVT_FEC_GROUP_MAX_STREAMS; i++)
        total += g->fec_nb_packets[i];
    return total;
}

/* Take a serialized packet (header bytes followed by payload) from the
 * recovered source object and feed it back into the reorder pipeline so
 * it joins the rest of the stream as if it had never been lost. */
static int reinject_recovered(AVTReorder *r, const uint8_t *bytes, uint32_t T)
{
    /* Strip leading zeros — recovered slots are always at least 2 bytes
     * (the descriptor), and any all-zero slot is a no-op. */
    uint16_t desc = ((uint16_t)bytes[0] << 8) | bytes[1];
    if (desc == 0)
        return 0;

    /* Stream-data and time-sync packets carry low-byte flag fields. */
    if (((desc & 0xFF00) == (AVT_PKT_TIME_SYNC & 0xFF00)) ||
        ((desc & 0xFF00) == (AVT_PKT_STREAM_DATA & 0xFF00)))
        desc &= 0xFF00;

    /* FEC meta-packets are never part of a source object — reject them
     * defensively so a malformed recovered slot can't infinitely
     * recurse into the FEC dispatch path. */
    if (desc == AVT_PKT_FEC_GROUPING || desc == AVT_PKT_FEC_GROUP_DATA)
        return AVT_ERROR(EINVAL);

    const int hdr_size = avt_pkt_hdr_size(desc);
    if (hdr_size <= 0 || (uint32_t)hdr_size > T)
        return AVT_ERROR(EINVAL);

    AVTPktd p = { };
    if ((size_t)hdr_size > sizeof(p.hdr))
        return AVT_ERROR(EINVAL);
    memcpy(p.hdr, bytes, hdr_size);
    p.hdr_len = hdr_size;

    size_t pl_bytes = 0;
    int dret = avt_packet_decode(&p, desc, &pl_bytes);
    if (dret < 0)
        return dret;

    if (pl_bytes) {
        if ((uint32_t)hdr_size + pl_bytes > T)
            return AVT_ERROR(EINVAL);
        if (!avt_buffer_quick_alloc(&p.pl, pl_bytes))
            return AVT_ERROR(ENOMEM);
        uint8_t *dst = avt_buffer_get_data(&p.pl, NULL);
        memcpy(dst, bytes + hdr_size, pl_bytes);
    }

    int ret = process_one_packet(r, &p);
    avt_buffer_quick_unref(&p.pl);
    return ret;
}

/* After a successful avt_fec_group_rx_try_recover, walk the source
 * object and re-inject every slot that was missing at recovery time.
 * The slot is marked in `src_was_present` *before* the recursive
 * reinject_recovered call so deep re-entry through the broadcast path
 * cannot re-process the same slot. */
static int fec_reinject_recovered(AVTReorder *r, AVTFecGroupRx *rx)
{
    int ret = 0;
    for (uint32_t i = 0; i < rx->Kt; i++) {
        if (rx->src_was_present[i])
            continue;
        rx->src_was_present[i] = 1;
        const uint8_t *bytes = rx->src + (size_t)i * rx->T;
        int rret = reinject_recovered(r, bytes, rx->T);
        if (rret < 0 && rret != AVT_ERROR(EINVAL)) {
            ret = rret;
            break;
        }
    }
    return ret;
}

/* Handle a 0x0030 FEC grouping packet: initialise (or re-initialise)
 * the matching rx slot and start tracking the group as active. */
static int dispatch_fec_grouping(AVTReorder *r, AVTPktd *p)
{
    const AVTFecGrouping *g = &p->pkt.fec_grouping;
    const uint32_t Kt = fec_group_kt(g);
    if (!Kt || Kt > UINT16_MAX)
        return 0; /* malformed — drop silently */

    AVTFecGroupRx *rx = &r->fg[g->group_id];
    int err = avt_fec_group_rx_init(rx, g, (uint16_t)Kt);
    if (err < 0)
        return err;

    fec_mark_active(r, g->group_id);
    return 0;
}

/* Handle a 0x0031 FEC group data packet: copy its parity slice into
 * the group's rx, record its FECSource entries, and try recovery.
 * Reinjection of decoder-filled slots is deferred to end-of-batch. */
static int dispatch_fec_group_data(AVTReorder *r, AVTPktd *p)
{
    const AVTFecGroupData *d = &p->pkt.fec_group_data;
    AVTFecGroupRx *rx = &r->fg[d->group_id];
    if (!rx->active) {
        /* The matching 0x0030 hasn't arrived yet — without OTI we can't
         * even allocate parity. Drop for now; spec says 0x0030 should
         * arrive first. */
        return 0;
    }

    size_t pl_size = 0;
    uint8_t *pl_data = avt_buffer_get_data(&p->pl, &pl_size);

    int err = avt_fec_group_rx_add_data(rx, d, pl_data, pl_size);
    if (err < 0)
        return err;

    avt_fec_group_rx_try_recover(r->ctx, rx);
    return 0;
}

/* For every active FEC group, attempt to feed this source packet (its
 * full serialized form) into that group's rx. Per group: success places
 * the bytes in the right slot and may make recovery possible; EAGAIN
 * stashes the bytes for later mapping; other errors are ignored.
 *
 * Reinjection of decoder-filled slots is intentionally NOT done here —
 * it is deferred to end-of-batch in avt_reorder_push, so source packets
 * that arrive later in the same batch don't get re-delivered. */
static int fec_broadcast_source(AVTReorder *r, AVTPktd *p)
{
    if (!r->nb_active_fec_groups)
        return 0;

    size_t pl_size = 0;
    const uint8_t *pl_data = avt_buffer_get_data(&p->pl, &pl_size);
    const size_t total = p->hdr_len + pl_size;
    if (!total)
        return 0;

    uint8_t *bytes = malloc(total);
    if (!bytes)
        return AVT_ERROR(ENOMEM);
    memcpy(bytes, p->hdr, p->hdr_len);
    if (pl_size)
        memcpy(bytes + p->hdr_len, pl_data, pl_size);

    for (uint16_t i = 0; i < r->nb_active_fec_groups; i++) {
        AVTFecGroupRx *rx = &r->fg[r->active_fec_groups[i]];
        if (!rx->active || total > rx->T)
            continue;
        int err = avt_fec_group_rx_add_source(rx, p->pkt.seq, bytes,
                                              (uint32_t)total);
        (void)err;
        avt_fec_group_rx_try_recover(r->ctx, rx);
    }

    free(bytes);
    return 0;
}

static int process_one_packet(AVTReorder *r, AVTPktd *p)
{
    int ret;

    /* FEC meta-packets are consumed by the reorder's FEC group state —
     * they never appear in the output stream. */
    switch (p->pkt.desc) {
    case AVT_PKT_FEC_GROUPING:
        return dispatch_fec_grouping(r, p);
    case AVT_PKT_FEC_GROUP_DATA:
        return dispatch_fec_group_data(r, p);
    default:
        break;
    }

    bool is_parity = false;
    uint32_t seg_off = 0, seg_size = 0, tot_size = 0;
    int series = avt_packet_series(p, &is_parity, &seg_off, &seg_size, &tot_size);

    /* Standalone packets: pass-through.
     * This covers session start, registration, time sync, hash, video info,
     * video orientation, stereo video, stream end, and non-segmented
     * stream/user/font/lut/metadata/config packets. */
    if (series == 0) {
        ret = push_ref_to_staging(r, p);
        if (ret < 0)
            return ret;
        return fec_broadcast_source(r, p);
    }

    /* Determine which stream this segment belongs to and the target seq.
     * The merger stores target as uint32_t, so we truncate to match. */
    const uint16_t sid = p->pkt.stream_id;
    uint32_t target;
    if (series > 0)
        target = (uint32_t)p->pkt.seq;
    else if (is_parity)
        target = (uint32_t)p->pkt.generic_parity.target_seq;
    else
        target = (uint32_t)p->pkt.generic_segment.target_seq;

    AVTReorderStream *rs = &r->st[sid];

    AVTMerger *m = pick_merger(r, rs, target);
    if (!m)
        return AVT_ERROR(ENOMEM);

    bool was_active = m->active;

    ret = avt_pkt_merge_seg(r->ctx, m, p);
    if (!was_active && m->active)
        rs->nb_groups++;

    if (ret == AVT_ERROR(EAGAIN)) {
        /* Need more segments to complete this packet */
        return fec_broadcast_source(r, p);
    } else if (ret == AVT_ERROR(EBUSY)) {
        /* Should not happen since we matched the target ahead of time */
        avt_log(r->ctx, AVT_LOG_WARN,
                "Reorder: unexpected EBUSY for stream 0x%X target %u\n",
                sid, target);
        return 0;
    } else if (ret < 0) {
        return ret;
    }

    /* ret == 0: a packet is available for output */
    ret = try_emit_merger(r, rs, m, 0);
    if (ret < 0 && ret != AVT_ERROR(EAGAIN) && ret != AVT_ERROR(ENOENT))
        return ret;

    return fec_broadcast_source(r, p);
}

int avt_reorder_push(AVTReorder *r, AVTPacketFifo *in)
{
    for (unsigned int i = 0; i < in->nb; i++) {
        AVTPktd *p = &in->data[i];
        int ret = process_one_packet(r, p);
        if (ret < 0)
            return ret;
    }

    /* End-of-batch FEC reinjection: each recovered group now contains
     * source bytes the decoder produced for slots that nothing in this
     * batch delivered. Reinjecting late means later source packets in
     * the same batch couldn't have been overtaken by an early recovery,
     * so there are no duplicates. */
    for (uint16_t i = 0; i < r->nb_active_fec_groups; i++) {
        AVTFecGroupRx *rx = &r->fg[r->active_fec_groups[i]];
        if (rx->active && rx->recovered) {
            int ret = fec_reinject_recovered(r, rx);
            if (ret < 0)
                return ret;
        }
    }

    /* Caller owns the in fifo; clear it now that we've consumed all packets */
    avt_pkt_fifo_clear(in);

    return 0;
}

int avt_reorder_pop(AVTReorder *r, AVTPacketFifo **out)
{
    if (!r->staging || !r->staging->nb)
        return AVT_ERROR(EAGAIN);

    *out = r->staging;
    r->staging = NULL;
    return 0;
}

int avt_reorder_done(AVTReorder *r, AVTPacketFifo *out)
{
    if (!out)
        return 0;

    avt_pkt_fifo_clear(out);

    if (!r->staging) {
        r->staging = out;
        return 0;
    }

    if ((r->nb_avail_buckets + 1) > r->nb_alloc_avail_buckets) {
        AVTPacketFifo **tmp = avt_reallocarray(r->avail_buckets,
                                               r->nb_avail_buckets + 1,
                                               sizeof(*tmp));
        if (!tmp)
            return AVT_ERROR(ENOMEM);
        r->avail_buckets = tmp;
        r->nb_alloc_avail_buckets = r->nb_avail_buckets + 1;
    }
    r->avail_buckets[r->nb_avail_buckets++] = out;

    return 0;
}

int avt_reorder_free(AVTReorder *r)
{
    /* Free all merger state in all streams, and any active FEC-group
     * receiver contexts that were lazily initialised in `fg`. */
    for (int i = 0; i < UINT16_MAX; i++) {
        AVTReorderStream *rs = &r->st[i];
        if (rs->nb_groups || rs->m[0].ranges_allocated ||
            rs->m[0].parity_ranges_allocated) {
            for (int j = 0; j < AVT_REORDER_GROUP_NB; j++)
                avt_pkt_merge_free(&rs->m[j]);
            rs->nb_groups = 0;
        }
        if (r->fg[i].active)
            avt_fec_group_rx_free(&r->fg[i]);
    }

    /* Free all buckets */
    r->staging = NULL;
    for (int i = 0; i < r->nb_buckets; i++) {
        avt_pkt_fifo_free(r->buckets[i]);
        free(r->buckets[i]);
    }
    free(r->buckets);
    free(r->avail_buckets);

    r->buckets = NULL;
    r->avail_buckets = NULL;
    r->nb_buckets = 0;
    r->nb_avail_buckets = 0;
    r->nb_alloc_avail_buckets = 0;
    r->nb_active_stream_indices = 0;
    r->nb_active_fec_groups = 0;

    return 0;
}
