/*
 * Copyright © 2023, Lynne
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

#ifndef AVTRANSPORT_CONNECTION_SCHEDULER
#define AVTRANSPORT_CONNECTION_SCHEDULER

#include "output_internal.h"

typedef struct AVTSchedulerBucket {
    union AVTPacketData *pkt;
    AVTBuffer **pl;
    int nb_packets;

    /* Private entries */
    int nb_packets_alloc;
    int avail;
} AVTSchedulerBucket;

typedef struct AVTScheduler {
    AVTOutput *out;

    uint64_t max_buffered; /* In bytes */
    uint64_t rx_bandwidth; /* Bits per second for transmission */
    uint64_t tx_bandwidth; /* Bits per second for the receiver */
    uint64_t max_pkt_size;

    /* One per stream. Value in bytes. 0 means in perfect realtime sync,
     * negative values mean the stream needs more data, positive values mean
     * stream is good for now. */
    int64_t realtime[UINT16_MAX];

    AVTSchedulerBucket *last_avail;
    AVTSchedulerBucket **buckets;
    int nb_buckets;
} AVTScheduler;

int avt_scheduler_init(AVTScheduler *s);

int avt_scheduler_set_props(AVTScheduler *s,
                            uint64_t rx_bandwidth, uint64_t tx_bandwidth,
                            uint64_t max_pkt_size, uint64_t max_buffered);

int avt_scheduler_push(AVTScheduler *s,
                       union AVTPacketData pkt, AVTBuffer *pl);

int avt_scheduler_pop(AVTScheduler *s, AVTSchedulerBucket **bkt);
void avt_scheduler_done(AVTScheduler *s, AVTSchedulerBucket *bkt);

void avt_scheduler_free(AVTScheduler *s);

#endif /* AVTRANSPORT_CONNECTION_SCHEDULER */
