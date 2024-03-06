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

    /* Rescale payload */
    seg_pl_size = seg_size_lim - hdr_size;
    err = avt_buffer_quick_ref(&state->start.pl, pl, 0, seg_pl_size);
    if (err < 0)
        return err;

    /* Modify packet */
    avt_packet_change_size(pkt, 0, seg_pl_size, pl_size);
    pkt.seq = get_seq(s);

    /* Encode packet */
    state->start.pkt = pkt;
    avt_packet_encode_header(&state->start);

    /* Enqueue start packet */
    err = avt_pkt_fifo_push_refd(dst, &state->start);
    if (err < 0) {
        avt_buffer_quick_unref(&state->start.pl);
        return err;
    }
    out_acc += avt_pkt_hdr_size(state->start.pkt) + seg_pl_size;

    /* Setup segmentation context */
    state->seg_offset = seg_pl_size;
    state->pl_left = pl_size - state->seg_offset;
    state->seg_hdr_size = avt_pkt_hdr_size(avt_packet_create_segment(&state->start, 0, 0, 0, 0));

    if (out_acc >= out_limit)
        return out_acc;

    /* Process segments */
resume:
    hdr_size = state->seg_hdr_size;
    if (out_limit < (hdr_size + 1))
        return AVT_ERROR(EAGAIN);

    while (state->pl_left) {
        seg_pl_size = seg_size_lim - hdr_size;
        p.pkt = avt_packet_create_segment(&state->start, get_seq(s),
                                          state->seg_offset, seg_pl_size, pl_size);

        /* Create segment buffer */
        err = avt_buffer_quick_ref(&p.pl, pl, state->seg_offset, seg_pl_size);
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

    if (!out_acc)
        memset(state, 0, sizeof(*state));

    return out_acc;
}

#if 0
static int compare_streams_pts(const void *s1, const void *s2)
{
    const AVTSchedulerStream *st1 = s1;
    const AVTSchedulerStream *st2 = s2;
    union AVTPacketData p1;
    union AVTPacketData p2;
    int e1 = avt_pkt_fifo_peek(&st1->fifo, &p1, NULL);
    int e2 = avt_pkt_fifo_peek(&st2->fifo, &p2, NULL);

    if (e1 < 0 && e2 < 0)
        return 0;
    else if (e1 < 0)
        return -1;
    else
        return 1;

    int64_t pts1 = avt_packet_get_pts(p1);
    int64_t pts2 = avt_packet_get_pts(p2);

    return avt_compare_ts(pts1, st1->tb, pts2, st2->tb);
}
#endif

static int scheduler_process(AVTScheduler *s,
                             union AVTPacketData pkt, AVTBuffer *pl)
{
    int err;
    bool buffered = false;
    union AVTPacketData *cp = &pkt;
    AVTBuffer *cpl = pl;

    int64_t slice = 0;
    int64_t avail = INT64_MAX;
    uint16_t nb_active_streams = s->nb_active_stream_indices;
    for (auto i = 0; i < nb_active_streams; i++) {
        AVTSchedulerStream *st = &s->streams[s->active_stream_indices[i]];

        slice += s->min_pkt_size;

        /* If the top packet is not a data packet, just pass them through */
        while (st->fifo.nb && !avt_buffer_get_data_len(&st->fifo.data[0].pl)) {
            err = scheduler_push_internal(s, &st->pc, s->staging,
                                          st->fifo.data[0].pkt,
                                          &st->fifo.data[0].pl,
                                          s->max_pkt_size, slice);
            if (err > 0)
                slice -= err;

            avt_pkt_fifo_pop(&st->fifo, NULL, NULL);
            if (!st->fifo.nb) {
                memmove(&s->active_stream_indices[i],
                        &s->active_stream_indices[i + 1],
                        (nb_active_streams - i - 1)*sizeof(*s->active_stream_indices));
                nb_active_streams--;
                s->nb_active_stream_indices = nb_active_streams;
            }
        }


    }







#if 0
    /* Gather streams to a temp */
    uint16_t nb_active_streams = s->nb_active_stream_indices;
    for (auto i = 0; i < nb_active_streams; i++) {
        uint16_t idx = s->active_stream_indices[i];
        AVTSchedulerStream *stmp = &s->streams[idx];
        s->streams_tmp[i] = stmp;

        /* Keep track of minimum PTS */
        const AVTPktd *p = &stmp->fifo.data[0];
        AVTRational tb;
        int64_t pts = avt_packet_get_pts(p);
        err = avt_packet_get_tb(p, &tb);
        if (err >= 0 &&
            avt_compare_ts(stmp->last_pts, stmp->last_pts_tb, pts, tb) == 1) {
            stmp->last_pts = pts;
            stmp->last_pts_tb = tb;
        }
    }

    /* Sort them by PTS */
    qsort(s->streams_tmp, nb_active_streams,
          sizeof(*s->streams_tmp), compare_streams_pts);

    /* Round-Robin scheduling */
    int64_t accum_out = 0;

    uint16_t st_id = 0;
    AVTSchedulerStream *st = s->streams_tmp[st_id];

    int next_st_id = 0;
    AVTSchedulerStream *next_st = NULL;
#define NEXT_STREAM()       \
    do {                    \
        st = next_st;       \
        st_id = next_st_id; \
        next_st = NULL;     \
        next_st_id = -1;    \
    } while (0)

    /* Schedule and output */
    while (1) {
        accum_out += s->min_pkt_size;

        for (auto i = 0; i < nb_active_streams; i++) {
            if (i == st_id)
                continue;

            AVTSchedulerStream *stmp = s->streams_tmp[i];


        }

        /* Ready to push a packet or a slice of it for output.
         * Only done when the current packet's timeslice ends */
        while ((accum_out > 0) && next_st) {
            AVTPktd *p = &st->fifo.data[0];
            int64_t pr = scheduler_push_internal(s, &st->pc, s->staging,
                                                 p->pkt, &p->pl,
                                                 s->max_pkt_size, accum_out);
            if (pr == AVT_ERROR(EAGAIN)) { /* Need more bytes to output something */
                /* TODO: test here if we'll overrun the budget on other streams
                 * if we don't switch at this point */
                NEXT_STREAM();
                break;
            } else if (pr == 0) { /* Packet fully finished */
                accum_out -= pr;

                err = avt_pkt_fifo_pop(&st->fifo, NULL, NULL);

                /* We're done with this stream, get rid of it */
                if (err == AVT_ERROR(ENOENT)) {
                    memmove(&s->streams_tmp[st_id],
                            &s->streams_tmp[st_id + 1],
                            (nb_active_streams - st_id - 1)*sizeof(*s->streams_tmp));
                    nb_active_streams--;
                    s->nb_active_stream_indices = nb_active_streams;
                    NEXT_STREAM();
                    break;
                } else if (err < 0) {
                    return err; /* Actual error */
                }
                /* Fallthrough, continue with this stream */
            } else if (pr < 0) {
                return pr; /* Actual error */
            } else /* ret > 0, e.g. Packets were generated and scheduled */ {
                s->staged_size += pr;
                accum_out -= pr; /* Done for now */
                NEXT_STREAM();
                break;
            }
        }
    }
#endif
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
        if (ret >= 0)
            s->staged_size += ret;
        return ret;
    }

    /* Keep track of timebases for all streams */
    if (pkt.desc == AVT_PKT_STREAM_REGISTRATION)
        s->streams[pkt.stream_id].reg = pkt;


    if (!s->streams[pkt.stream_id].active) {
        s->active_stream_indices[s->nb_active_stream_indices++] = pkt.stream_id;
        s->streams[pkt.stream_id].active = true;
    }

    /* Keep track of the minimum packet size for round-robin quantum */
    const size_t payload_size = avt_buffer_get_data_len(pl);
    s->min_pkt_size = AVT_MIN(s->min_pkt_size,
                              avt_pkt_hdr_size(pkt) + payload_size);

    /* Keep track of fully active streams */
    if (pl)
        s->streams[pkt.stream_id].pl_bytes += payload_size;

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

    s->staged_size = 0;

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

    for (auto i = 0; i < s->nb_active_stream_indices; i++) {
        AVTSchedulerStream *st = s->streams_tmp[s->active_stream_indices[i]];
        avt_pkt_fifo_free(&st->fifo);
    }
    s->nb_active_stream_indices = 0;
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
