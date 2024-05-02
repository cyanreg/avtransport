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

#include <inttypes.h>
#include <string.h>
#include <avtransport/avtransport.h>

#include "scheduler.h"
#include "mem.h"
#include "utils_internal.h"
#include "utils_packet.h"

FN_CREATING(avt_scheduler, AVTScheduler, AVTPacketFifo,
            bucket, buckets, nb_buckets)

int avt_scheduler_init(AVTScheduler *s,
                       size_t max_pkt_size, int64_t bandwidth)
{
    s->seq = 0;
    s->bandwidth = bandwidth;
    s->avail = bandwidth;

    /* AVTransport packets simply don't support bigger sizes */
    s->max_pkt_size = AVT_MIN(max_pkt_size, UINT32_MAX);

    return 0;
}

static inline uint64_t get_seq(AVTScheduler *s)
{
    return s->seq++;
}

static inline void update_sw(AVTScheduler *s, size_t size)
{
    size *= 8;

    static const AVTRational target_tb = (AVTRational){ 1, 1000000000 };
    int64_t duration = avt_rescale(size, s->bandwidth, target_tb.den);
    int64_t sum = avt_sliding_win(&s->sw, size, s->time, target_tb,
                                  target_tb.den, 0);

    s->avail = s->bandwidth - sum;
    avt_log(s, AVT_LOG_TRACE, "Updating bw: %" PRIi64 " bits in, "
                              "%" PRIi64 " t, %" PRIi64 " dt, "
                              "%" PRIi64 " bps, %" PRIi64 " avail\n",
            size, s->time, duration, sum, s->avail);

    s->time += duration;
}

static inline int64_t scheduler_push_internal(AVTScheduler *s,
                                              AVTSchedulerPacketContext *state,
                                              AVTPacketFifo *dst,
                                              size_t seg_size_lim,
                                              size_t out_limit)
{
    uint32_t hdr_size;
    size_t acc;
    size_t out_acc = 0;
    uint32_t pl_size = avt_buffer_get_data_len(&state->pl);
    uint32_t seg_pl_size;
    const size_t lim = AVT_MIN(seg_size_lim, out_limit);
    AVTPktd *p;

    if (state->seg_offset)
        goto resume;

    /* Header size of the encoded packet */
    hdr_size = avt_pkt_hdr_size(state->start.pkt.desc);

    /* Signal we need more bytes to output something coherent.
     * If there's payload, make sure we can send off at least a byte of it. */
    if (out_limit < (hdr_size + !!pl_size))
        return AVT_ERROR(EAGAIN);

    if (!pl_size) {
        p = avt_pkt_fifo_push_new(dst, NULL, 0, 0);
        if (!p)
            return AVT_ERROR(ENOMEM);

        p->pkt = state->start.pkt;
        avt_packet_encode_header(p);
        out_acc += hdr_size;
        update_sw(s, hdr_size);
        state->seg_offset = 0;
        state->present = 0;

        return 0;
    }

    /* Reserve new packet in the output bucket FIFO */
    seg_pl_size = AVT_MIN(lim - hdr_size, pl_size);
    p = avt_pkt_fifo_push_new(dst, &state->pl, 0, seg_pl_size);
    if (!p)
        return AVT_ERROR(ENOMEM);

    /* Modify packet */
    avt_packet_change_size(state->start.pkt, 0, seg_pl_size, pl_size);
    state->start.pkt.seq = get_seq(s);

    /* Encode packet */
    avt_packet_encode_header(&state->start);

    /* Update accumulated output */
    acc = avt_pkt_hdr_size(state->start.pkt.desc) + seg_pl_size;
    out_acc += acc;
    update_sw(s, acc);

    /* Update packet in FIFO */
    p->pkt = state->start.pkt;
    p->hdr_len = state->start.hdr_len;
    memcpy(p->hdr, state->start.hdr, state->start.hdr_len);

    /* Setup segmentation context */
    state->seg_offset = seg_pl_size;
    state->pl_left = pl_size - state->seg_offset;
    state->seg_hdr_size = avt_pkt_hdr_size(avt_packet_create_segment(&state->start, 0, 0, 0, 0).desc);

    /* Return now with what we wrote if there are not enough bytes */
    if (out_acc >= out_limit)
        return out_acc;

    /* Process segments */
resume:
    hdr_size = state->seg_hdr_size;

    /* Return if there are no packets we can output in the limit */
    if (out_limit < (hdr_size + 1))
        return AVT_ERROR(EAGAIN);

    while (state->pl_left) {
        seg_pl_size = AVT_MIN(lim - hdr_size, state->pl_left);

        p = avt_pkt_fifo_push_new(dst, &state->pl, state->seg_offset, seg_pl_size);
        if (!p)
            return AVT_ERROR(ENOMEM);

        p->pkt = avt_packet_create_segment(&state->start, get_seq(s),
                                           state->seg_offset, seg_pl_size, pl_size);

        /* Encode packet */
        avt_packet_encode_header(p);

        /* Enqueue packet */
        acc = avt_pkt_hdr_size(p->pkt.desc) + seg_pl_size;
        out_acc += acc;
        update_sw(s, acc);

        state->seg_offset += seg_pl_size;
        state->pl_left -= seg_pl_size;

        /* Exit if we run out */
        if ((out_acc + hdr_size + 1) > out_limit)
            return out_acc;
    }

    if (!out_acc) {
        state->seg_offset = 0;
        state->present = 0;
    }

    return out_acc;
}

static inline void update_stream_ctx(AVTScheduler *s, AVTSchedulerStream *pctx)
{
    AVTRational s_tb;
    avt_packet_get_tb(pctx->reg, &s_tb);

    static const AVTRational target_tb = (AVTRational){ 1, 1000000000 };

    /* TODO: take into account index packets having larger length */
    const size_t size = (avt_pkt_hdr_size(pctx->cur.start.pkt.desc) +
                         avt_buffer_get_data_len(&pctx->cur.pl)) * 8;

    int64_t duration = avt_packet_get_duration(&pctx->cur.start.pkt);
    if (duration == INT64_MIN) {
        /* How many nanoseconds would take to transmit this number of bits */
        duration = avt_rescale(size, s->bandwidth, target_tb.den);
    } else {
        duration = avt_rescale_rational(duration, s_tb, target_tb);
    }

    int64_t pts = avt_packet_get_pts(&pctx->cur.start.pkt);
    if (pts != INT64_MIN)
        pts = avt_rescale_rational(pts, s_tb, target_tb);

    pctx->cur.pts = pts;
    pctx->cur.duration = duration;
    pctx->cur.present = true;
    pctx->cur.size = size;
}

static inline int preload_pkt(AVTScheduler *s, AVTSchedulerStream *pctx)
{
    if (pctx->cur.present)
        return 0;

    int ret = avt_pkt_fifo_pop(&pctx->fifo, &pctx->cur.start.pkt, &pctx->cur.pl);
    if (ret < 0)
        return ret;

    update_stream_ctx(s, pctx);

    return ret;
}

static void remove_stream(AVTScheduler *s, int active_id, int overlap_id)
{
    if (overlap_id >= 0) {
        memcpy(&s->tmp.overlap[overlap_id], &s->tmp.overlap[overlap_id + 1],
               (s->tmp.nb_overlap - overlap_id)*sizeof(*s->tmp.overlap));
        s->tmp.nb_overlap--;
    }

    if (active_id >= 0) {
        s->streams[s->active_stream_indices[active_id]].active = false;
        memcpy(&s->active_stream_indices[active_id],
               &s->active_stream_indices[active_id + 1],
               (s->nb_active_stream_indices - active_id) *
               sizeof(*s->active_stream_indices));
        s->nb_active_stream_indices--;
    }
}

static int direct_push(AVTScheduler *s, uint16_t id)
{
    int ret;
    AVTSchedulerStream *pctx = &s->streams[id];
    avt_log(s, AVT_LOG_TRACE, "Pushing stream 0x%X: 0x%X pkt, "
                              "%" PRIi64 " avail bits\n",
            id, pctx->cur.start.pkt.desc, s->avail);
    do {
        ret = scheduler_push_internal(s, &pctx->cur, s->staging,
                                      s->max_pkt_size, s->avail >> 3);
    } while (ret > 0);

    if (ret < 0) {
        avt_buffer_quick_unref(&pctx->cur.pl);
        return ret;
    } else if (!ret) {
        avt_buffer_quick_unref(&pctx->cur.pl);
        ret = preload_pkt(s, &s->streams[id]);
        if (ret == AVT_ERROR(ENOENT)) {
            remove_stream(s, 0, -1);
            ret = 0;
        } else if (ret < 0) {
            return ret;
        }
    }

    return ret;
}

static int scheduler_process(AVTScheduler *s)
{
    int ret;
    AVTSchedulerStream *pctx;

    for (auto i = 0; i < s->nb_active_stream_indices; i++) {
        const uint16_t id = s->active_stream_indices[i];
        ret = preload_pkt(s, &s->streams[id]);
        if (ret < 0)
            return ret;
    }

 repeat:
    if (!s->nb_active_stream_indices)
        return 0;
    else if (s->nb_active_stream_indices == 1)
        return direct_push(s, s->active_stream_indices[0]);

    /* Get the first (time-wise) ending timestamp of a packet */
    int64_t min_end = INT64_MAX;
    uint16_t min_end_id = 0;
    for (int i = 0; i < s->nb_active_stream_indices; i++) {
        const uint16_t id = s->active_stream_indices[i];
        pctx = &s->streams[id];
        if ((pctx->cur.pts + pctx->cur.duration) < min_end) {
            min_end = (pctx->cur.pts + pctx->cur.duration);
            min_end_id = id;
        }
    }

    /* Get a list of packets whose start time overlaps with the first end */
    s->tmp.nb_overlap = 0;
    s->tmp.overlap[s->tmp.nb_overlap++] = min_end_id;
    size_t overlap_size = s->streams[min_end_id].cur.size;
    size_t min_overlap_size = AVT_MIN(s->streams[min_end_id].cur.size,
                                      s->max_pkt_size);
    for (int i = 0; i < s->nb_active_stream_indices; i++) {
        const uint16_t id = s->active_stream_indices[i];
        if (id == min_end_id)
            continue;
        pctx = &s->streams[id];
        if (pctx->cur.pts < min_end) {
            if (pctx->cur.size < min_overlap_size)
                min_overlap_size = pctx->cur.size;
            overlap_size += pctx->cur.size;
            s->tmp.overlap[s->tmp.nb_overlap++] = id;
        }
    }

    /* No overlaps, nothing to schedule */
    if (s->tmp.nb_overlap == 1) {
        ret = direct_push(s, s->active_stream_indices[0]);
        if (ret < 0)
            return ret;
        goto repeat;
    }

    /* We should really sort overlaps by start time here */

    /* We need to do a while loop for each time we call
     * scheduler_push_internal, as it needs a flush.
     *
     * So, rather than using a for loop, and then a while loop,
     * which would output all segments of a single packet sequentially,
     * just do a do/while loop, iterating over all overlapping streams,
     * until either we run out of bits to spare,
     * or packet starts have all moved on past the point of min_end.
     *
     * This essentially interleaves segments of all streams, giving
     * some amount of resilience towards packet drops, which happen in
     * bursts. */
    avt_log(s, AVT_LOG_DEBUG, "Interleaving: %" PRIu16 " streams, "
                              "%" PRIu16 " overlaps, %" PRIi64 " end ts, "
                              "%" PRIi64 "/%" PRIi64 " left/avail bits\n",
            s->nb_active_stream_indices, s->tmp.nb_overlap, min_end,
            overlap_size, s->avail);

    /* Per-stream limit */
    size_t local_limit = (s->avail / overlap_size) * s->max_pkt_size;
    unsigned int idx = 0;
    do {
        const int i = (idx++) % s->tmp.nb_overlap;
        const uint16_t id = s->tmp.overlap[i];
        pctx = &s->streams[id];

        avt_log(s, AVT_LOG_TRACE, "Pushing stream 0x%X: 0x%X pkt, "
                                  "%" PRIi64 " limit, "
                                  "%" PRIi64 "/%" PRIi64 " left/avail bits\n",
                id, pctx->cur.start.pkt.desc, local_limit, overlap_size, s->avail);

        ret = scheduler_push_internal(s, &pctx->cur, s->staging,
                                      s->max_pkt_size, local_limit);
        if (ret == AVT_ERROR(EAGAIN)) {
            /* No bits left at all. Just return. */
            return 0;
        } else if (ret < 0) {
            avt_buffer_quick_unref(&pctx->cur.pl);
            return ret;
        } else if (ret > 0) {
            s->avail -= ret;
            overlap_size -= ret;
        } else if (ret == 0) {
            avt_buffer_quick_unref(&pctx->cur.pl);
            /* Preload next */
            ret = preload_pkt(s, pctx);
            if (ret == 0) {
                /* Remove from list if we don't have enough bits to fit
                 * it into, or if it doesn't overlap */
                if (((overlap_size + pctx->cur.size) < s->avail) ||
                    (pctx->cur.pts >= min_end))
                    remove_stream(s, -1, i);
                else
                    overlap_size += pctx->cur.size;
            } else if (ret == AVT_ERROR(ENOENT)) {
                remove_stream(s, pctx->active_id, i);
            } else if (ret < 0) {
                return ret;
            }
        }
    } while ((overlap_size < s->avail) && s->tmp.nb_overlap);

    goto repeat;

    return 0;
}

int avt_scheduler_push(AVTScheduler *s, AVTPktd *p)
{
    int ret;

    /* Allocate a staging buffer if one doesn't exist */
    if (!s->staging) {
        s->staging = avt_scheduler_create_bucket(s);
        if (!s->staging)
            return AVT_ERROR(ENOMEM);
    }

    /* Bypass everything if interleaving is turned off */
    if (s->bandwidth == INT64_MAX) {
        AVTSchedulerPacketContext state = {
            .start = *p,
            .pl = p->pl,
        };
        return scheduler_push_internal(s, &state, s->staging,
                                       s->max_pkt_size, INT64_MAX);
    }

    /* Keep track of timebases for all streams */
    if (p->pkt.desc == AVT_PKT_STREAM_REGISTRATION)
        s->streams[p->pkt.stream_id].reg = p->pkt;

    uint16_t sid = p->pkt.stream_id;
    if (p->pkt.desc == AVT_PKT_SESSION_START || p->pkt.desc == AVT_PKT_TIME_SYNC)
        sid = 0xFFFF;

    /* Keep track of active streams */
    if (!s->streams[sid].active) {
        s->streams[sid].active = true;
        s->streams[sid].active_id = s->nb_active_stream_indices;
        s->active_stream_indices[s->nb_active_stream_indices++] = sid;
    }

    if (!s->streams[sid].cur.present && !s->streams[sid].fifo.nb) {
        s->streams[sid].cur.start.pkt = p->pkt;
        avt_buffer_quick_ref(&s->streams[sid].cur.pl, &p->pl, 0, AVT_BUFFER_REF_ALL);
        update_stream_ctx(s, &s->streams[sid]);
    } else {
        /* Add packet to stream FIFO */
        ret = avt_pkt_fifo_push(&s->streams[sid].fifo, p);
        if (ret < 0)
            return ret;
    }

    return scheduler_process(s);
}

int avt_scheduler_pop(AVTScheduler *s, AVTPacketFifo **seq)
{
    if (!s->staging || (!s->staging->nb))
        return AVT_ERROR(EAGAIN);

    AVTPacketFifo *bkt = s->staging;
    avt_assert0(bkt != NULL);
    s->staging = NULL;
    *seq = bkt;

    return 0;
}

int avt_scheduler_flush(AVTScheduler *s, AVTPacketFifo **seq)
{
    AVTPacketFifo *bkt = s->staging;
    s->staging = NULL;
    *seq = bkt;
    return 0;
}

int avt_scheduler_done(AVTScheduler *s, AVTPacketFifo *seq)
{
    avt_assert1(seq->nb); /* Scheduler FIFO was not fully consumed */

    if (!s->staging) {
        s->staging = seq;
    } else {
        if ((s->nb_avail_buckets + 1) > s->nb_alloc_avail_buckets) {
            AVTPacketFifo **tmp = avt_reallocarray(s->avail_buckets,
                                                   s->nb_avail_buckets + 1,
                                                   sizeof(AVTPacketFifo *));
            if (!tmp)
                return AVT_ERROR(ENOMEM);

            s->avail_buckets = tmp;
            s->nb_alloc_avail_buckets = s->nb_avail_buckets + 1;
        }

        s->avail_buckets[s->nb_avail_buckets++] = seq;
    }

    return 0;
}

void avt_scheduler_free(AVTScheduler *s)
{
    s->staging = NULL;

    free(s->avail_buckets);
    s->avail_buckets = NULL;
    s->nb_alloc_avail_buckets = 0;
    s->nb_avail_buckets = 0;

    for (auto i = 0; i < s->nb_buckets; i++)
        avt_pkt_fifo_free(s->buckets[i]);
    free(s->buckets);
    s->buckets = NULL;
    s->nb_buckets = 0;

    for (auto i = 0; i < s->nb_active_stream_indices; i++) {
        AVTSchedulerStream *st = &s->streams[s->active_stream_indices[i]];
        avt_buffer_quick_unref(&st->cur.pl);
        avt_pkt_fifo_free(&st->fifo);
    }
    s->nb_active_stream_indices = 0;
}
