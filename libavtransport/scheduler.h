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

#ifndef AVTRANSPORT_CONNECTION_SCHEDULER_H
#define AVTRANSPORT_CONNECTION_SCHEDULER_H

#include <avtransport/rational.h>
#include "utils_internal.h"

typedef struct AVTSchedulerPacketContext {
    AVTBuffer pl;
    AVTPktd  start;
    uint32_t seg_offset;
    uint32_t pl_left;
    uint32_t seg_hdr_size;
    bool present;
} AVTSchedulerPacketContext;

typedef struct AVTSchedulerStream {
    /* For timebase keeping */
    union AVTPacketData reg;

    /* Top packet */
    AVTSchedulerPacketContext cur;
    /* Rest of packets, in a FIFO */
    AVTPacketFifo fifo;

    /* Stream has had packets without a closure */
    bool active;
} AVTSchedulerStream;

typedef struct AVTScheduler {
    /* Master packet sequence value */
    atomic_uint_least64_t seq;

    /* Settings */
    uint32_t max_pkt_size;
    int64_t bandwidth;

    /* Scheduling state */
    AVTSlidingWinCtx sw;   /* Sliding window state */
    uint32_t min_pkt_size; /* RR quantum, in bits */

    /* Streams state */
    AVTSchedulerStream streams[UINT16_MAX];
    uint16_t active_stream_indices[UINT16_MAX];
    uint16_t nb_active_stream_indices;

    /* Staging bucket, next for output */
    AVTPacketFifo *staging;

    /* Available output buckets */
    AVTPacketFifo **avail_buckets;
    int nb_avail_buckets;
    int nb_alloc_avail_buckets;

    /* All allocated buckets */
    AVTPacketFifo **buckets;
    int nb_buckets;
} AVTScheduler;

/* Initialization function. If max_pkt_size changes, everything must
 * be torn down and recreated. */
int avt_scheduler_init(AVTScheduler *s, uint32_t max_pkt_size,
                       size_t buffer_limit, int64_t bandwidth);

int avt_scheduler_push(AVTScheduler *s,
                       union AVTPacketData pkt, AVTBuffer *pl);

int avt_scheduler_pop(AVTScheduler *s, AVTPacketFifo **seq);

int avt_scheduler_flush(AVTScheduler *s, AVTPacketFifo **seq);

int avt_scheduler_done(AVTScheduler *s, AVTPacketFifo *seq);

void avt_scheduler_free(AVTScheduler *s);

#endif /* AVTRANSPORT_CONNECTION_SCHEDULER_H */
