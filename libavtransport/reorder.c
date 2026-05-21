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

FN_CREATING(avt_reorder, AVTReorder, AVTPacketFifo,
            bucket, buckets, nb_buckets)

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

int avt_reorder_push(AVTReorder *r, AVTPacketFifo *in)
{
    int ret;

    for (unsigned int i = 0; i < in->nb; i++) {
        AVTPktd *p = &in->data[i];

        bool is_parity = false;
        uint32_t seg_off = 0, seg_size = 0, tot_size = 0;
        int series = avt_packet_series(p, &is_parity, &seg_off, &seg_size, &tot_size);

        /* Standalone packets: pass-through.
         * This covers session start, registration, time sync, hash, video info,
         * fec grouping, video orientation, stereo video, stream end, and
         * non-segmented stream/user/font/lut/metadata/config packets. */
        if (series == 0) {
            ret = push_ref_to_staging(r, p);
            if (ret < 0)
                return ret;
            continue;
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
            continue;
        } else if (ret == AVT_ERROR(EBUSY)) {
            /* Should not happen since we matched the target ahead of time */
            avt_log(r->ctx, AVT_LOG_WARN,
                    "Reorder: unexpected EBUSY for stream 0x%X target %u\n",
                    sid, target);
            continue;
        } else if (ret < 0) {
            return ret;
        }

        /* ret == 0: a packet is available for output */
        ret = try_emit_merger(r, rs, m, 0);
        if (ret < 0 && ret != AVT_ERROR(EAGAIN) && ret != AVT_ERROR(ENOENT))
            return ret;
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
    /* Free all merger state in all streams */
    for (int i = 0; i < UINT16_MAX; i++) {
        AVTReorderStream *rs = &r->st[i];
        if (rs->nb_groups || rs->m[0].ranges_allocated ||
            rs->m[0].parity_ranges_allocated) {
            for (int j = 0; j < AVT_REORDER_GROUP_NB; j++)
                avt_pkt_merge_free(&rs->m[j]);
            rs->nb_groups = 0;
        }
        AVTReorderStream *fg = &r->fg[i];
        if (fg->nb_groups || fg->m[0].ranges_allocated ||
            fg->m[0].parity_ranges_allocated) {
            for (int j = 0; j < AVT_REORDER_GROUP_NB; j++)
                avt_pkt_merge_free(&fg->m[j]);
            fg->nb_groups = 0;
        }
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

    return 0;
}
