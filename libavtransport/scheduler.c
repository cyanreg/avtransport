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

#include <string.h>
#include <avtransport/avtransport.h>

#include "scheduler.h"
#include "mem.h"
#include "utils_internal.h"
#include "utils_packet.h"

FN_CREATING(avt_scheduler, AVTScheduler, AVTPacketFifo,
            bucket, buckets, nb_buckets)

int avt_scheduler_init(AVTScheduler *s, uint32_t max_pkt_size,
                       size_t buffer_limit, int64_t bandwidth)
{
    s->max_pkt_size = max_pkt_size;
    s->bandwidth = bandwidth;
    s->min_pkt_size = UINT32_MAX;

    atomic_init(&s->seq, 0);

    return 0;
}

static inline uint64_t get_seq(AVTScheduler *s)
{
    return atomic_fetch_add(&s->seq, 1ULL);
}

static int64_t scheduler_push_internal(AVTScheduler *s,
                                       AVTSchedulerPacketContext *state,
                                       AVTPacketFifo *dst,
                                       union AVTPacketData pkt, AVTBuffer *pl,
                                       uint32_t seg_size_lim, int64_t out_limit)
{
    int err;
    uint32_t hdr_size;
    size_t out_acc = 0;
    uint32_t pl_size = avt_buffer_get_data_len(pl);
    uint32_t seg_pl_size;
    AVTPktd p;

    if (state->seg_offset)
        goto resume;

    hdr_size = avt_pkt_hdr_size(pkt);

    /* Signal we need more bytes to output something coherent */
    if (out_limit < (hdr_size + 1))
        return AVT_ERROR(EAGAIN);

    /* Reference full payload */
    err = avt_buffer_quick_ref(&state->pl, pl, 0, AVT_BUFFER_REF_ALL);
    if (err < 0)
        return err;

    /* Rescale payload */
    seg_pl_size = seg_size_lim - hdr_size;
    err = avt_buffer_quick_ref(&state->start.pl, &state->pl, 0, seg_pl_size);
    if (err < 0) {
        avt_buffer_quick_unref(&state->pl);
        return err;
    }

    /* Modify packet */
    avt_packet_change_size(pkt, 0, seg_pl_size, pl_size);
    pkt.seq = get_seq(s);

    /* Encode packet */
    state->start.pkt = pkt;
    avt_packet_encode_header(&state->start);

    /* Enqueue start packet */
    err = avt_pkt_fifo_push_refd(dst, &state->start);
    if (err < 0) {
        avt_buffer_quick_unref(&state->pl);
        avt_buffer_quick_unref(&state->start.pl);
        return err;
    }
    out_acc += avt_pkt_hdr_size(state->start.pkt) + seg_pl_size;

    /* Setup segmentation context */
    state->seg_offset = seg_pl_size;
    state->pl_left = pl_size - state->seg_offset;
    state->seg_hdr_size = avt_pkt_hdr_size(avt_packet_create_segment(&state->start, 0, 0, 0, 0));

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
        seg_pl_size = seg_size_lim - hdr_size;
        p.pkt = avt_packet_create_segment(&state->start, get_seq(s),
                                          state->seg_offset, seg_pl_size, pl_size);

        /* Create segment buffer */
        err = avt_buffer_quick_ref(&p.pl, &state->pl, state->seg_offset, seg_pl_size);
        if (err < 0)
            return err;

        /* Encode packet */
        avt_packet_encode_header(&p);

        /* Enqueue packet */
        err = avt_pkt_fifo_push_refd(dst, &p);
        if (err < 0) {
            avt_buffer_quick_unref(&p.pl);
            return err;
        }
        out_acc += avt_pkt_hdr_size(p.pkt) + seg_pl_size;

        state->seg_offset += seg_pl_size;
        state->pl_left -= seg_pl_size;

        if (out_acc >= out_limit)
            return out_acc;
    }

    if (!out_acc) {
        avt_buffer_quick_unref(&state->pl);
        avt_buffer_quick_unref(&state->start.pl);
        memset(state, 0, sizeof(*state));
    }

    return out_acc;
}

static int scheduler_process(AVTScheduler *s,
                             union AVTPacketData pkt, AVTBuffer *pl)
{
    int err;

    /* If stream already has a packet staged, put it in fifo */
    if (s->streams[pkt.stream_id].cur.present) {
        err = avt_pkt_fifo_push_refd(&s->streams[pkt.stream_id].fifo, pkt, pl);
        if (err < 0)
            return err;
    }





    return 0;
}

int avt_scheduler_push(AVTScheduler *s,
                       union AVTPacketData pkt, AVTBuffer *pl)
{
    int err;

    /* Allocate a staging buffer if one doesn't exist */
    if (!s->staging) {
        s->staging = avt_scheduler_create_bucket(s);
        if (!s->staging)
            return AVT_ERROR(ENOMEM);
    }

    /* Bypass if interleaving is turned off */
    if (s->bandwidth == INT64_MAX) {
        AVTSchedulerPacketContext state = { };
        int64_t ret = scheduler_push_internal(s, &state, s->staging, pkt, pl,
                                              s->max_pkt_size, INT64_MAX);
        return ret;
    }

    /* Keep track of timebases for all streams */
    if (pkt.desc == AVT_PKT_STREAM_REGISTRATION)
        s->streams[pkt.stream_id].reg = pkt;

    /* Keep track of active streams */
    if (!s->streams[pkt.stream_id].active) {
        s->active_stream_indices[s->nb_active_stream_indices++] = pkt.stream_id;
        s->streams[pkt.stream_id].active = true;
    }

    /* Keep track of the minimum packet size for round-robin quantum */
    const size_t payload_size = avt_buffer_get_data_len(pl);
    s->min_pkt_size = AVT_MIN(s->min_pkt_size,
                              avt_pkt_hdr_size(pkt) + payload_size);

    return scheduler_process(s, pkt, pl);
}

int avt_scheduler_pop(AVTScheduler *s, AVTPacketFifo **seq)
{
    if (!s->staging || (!s->staging->nb))
        return AVT_ERROR(EAGAIN);

    AVTPacketFifo *bkt = s->staging;
    avt_assert0(bkt != NULL);
    s->staging = NULL;
    *seq = bkt;

    if (s->bandwidth == INT64_MAX)
        return 0;

//    s->staged_size = 0;

    /* Redo round-robin quantum calculation */
    s->min_pkt_size = UINT32_MAX;
    for (auto i = 0; i < s->nb_active_stream_indices; i++) {
        const AVTPktd *p;
        size_t payload_size;
        const AVTSchedulerStream *st = &s->streams[s->active_stream_indices[i]];
        for (auto j = 0; j < st->fifo.nb; j++) {
            p = &st->fifo.data[j];
            payload_size = avt_buffer_get_data_len(&p->pl);
            s->min_pkt_size = AVT_MIN(s->min_pkt_size,
                                      avt_pkt_hdr_size(p->pkt) + payload_size);
        }
    }

    return 0;
}

int avt_scheduler_flush(AVTScheduler *s, AVTPacketFifo **seq)
{
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

#if 0
    for (auto i = 0; i < s->nb_active_stream_indices; i++) {
        AVTSchedulerStream *st = s->streams_tmp[s->active_stream_indices[i]];
        avt_pkt_fifo_free(&st->fifo);
    }
    s->nb_active_stream_indices = 0;
#endif
}


/* Rust */
#if 0
    size_t payload_size = avt_buffer_get_data_len(pl);
    /* Normalized average bitrate */
    int64_t pkt_ts = avt_packet_get_pts(pkt);
    int64_t pkt_duration = avt_packet_get_duration(pkt);
    if (ret >= 0 && pkt_ts != INT64_MIN && pkt_duration != INT64_MIN) {
        /* payload_size / avt_r2d(duration) */
        int64_t norm_size = avt_rescale(8*payload_size, tb.den, pkt_duration * tb.num);
        s->streams[pkt.stream_id].bitrate = avt_sliding_win(&s->streams[pkt.stream_id].sw,
                                                            norm_size, pkt_ts,
                                                            tb, tb.den, 1);
    }
#endif
