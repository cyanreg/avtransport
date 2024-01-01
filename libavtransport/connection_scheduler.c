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

#include "connection_scheduler.h"
#include "utils_internal.h"

FN_CREATING(avt_scheduler, AVTScheduler, AVTPacketFifo,
            bucket, buckets, nb_buckets)

int avt_scheduler_init(AVTScheduler *s, AVTOutput *out)
{
    return 0;
}

int avt_scheduler_set_props(AVTScheduler *s,
                            uint64_t rx_bandwidth, uint64_t tx_bandwidth,
                            uint64_t max_pkt_size, uint64_t max_buffered)
{
    return 0;
}

int avt_scheduler_push(AVTScheduler *s,
                       union AVTPacketData pkt, AVTBuffer *pl)
{


    return 0;
}

int avt_scheduler_pop(AVTScheduler *s, AVTPacketFifo **seq)
{
    AVTPacketFifo *bkt = s->last_avail;
    if (!bkt) {
        bkt = avt_scheduler_create_bucket(s);
        if (!bkt)
            return AVT_ERROR(ENOMEM);
    }

    s->last_avail = NULL;
    *seq = bkt;

    return 0;
}

void avt_scheduler_done(AVTScheduler *s, AVTPacketFifo *seq)
{
    if (!seq)
        return;

    s->last_avail = seq;
}

void avt_scheduler_free(AVTScheduler *s)
{
}
