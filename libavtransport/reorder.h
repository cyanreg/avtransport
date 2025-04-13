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

#ifndef AVTRANSPORT_REORDER_H
#define AVTRANSPORT_REORDER_H

#include "merger.h"
#include "utils_internal.h"

/* Maximum number of rejections for a single stream ID */
#define AVT_REORDER_GROUP_NB 8

typedef struct AVTReorderStream {
    AVTMerger m[AVT_REORDER_GROUP_NB];
    int nb_groups;
} AVTReorderStream;

typedef struct AVTReorder {
    AVTContext *ctx;

    /* One context per stream */
    AVTReorderStream st[UINT16_MAX];

    /* One context per FEC group */
    AVTReorderStream fg[UINT16_MAX];

    AVTPacketFifo *staging; /* Staging bucket, next for output */

    /* If a stream has no outstanding packets, it is considered inactive.
     * State is managed by an upper layer */
    uint16_t active_stream_indices[UINT16_MAX];
    uint16_t nb_active_stream_indices;

    /* Available output buckets */
    AVTPacketFifo **avail_buckets;
    int nb_avail_buckets;
    int nb_alloc_avail_buckets;

    /* All allocated buckets */
    AVTPacketFifo **buckets;
    int nb_buckets;
} AVTReorder;

/* Initialize a reorder buffer with a given max_size which
 * is the approximate bound of all packets and their payloads
 * contained within. */
int avt_reorder_init(AVTContext *ctx, AVTReorder *r, size_t max_size);

/* Push data to the reorder */
int avt_reorder_push(AVTReorder *r, AVTPacketFifo *in);

/* Pop data from the reorder */
int avt_reorder_pop(AVTReorder *r, AVTPacketFifo *out);

/* Mark a bucket as being available to use again */
int avt_reorder_done(AVTPacketFifo *out);

/* Free all data. All buckets become invalid */
int avt_reorder_free(AVTReorder *r);

#endif /* AVTRANSPORT_REORDER_H */
