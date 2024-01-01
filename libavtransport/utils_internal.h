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

#ifndef AVTRANSPORT_UTILS_H
#define AVTRANSPORT_UTILS_H

#include <assert.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>

#include <avtransport/utils.h>
#include <avtransport/packet_data.h>
#include "buffer.h"

#include "../config.h"

static inline void avt_assert0(int cond)
{
    assert(cond);
}

static inline void avt_assert1(int cond)
{
#if CONFIG_ASSERT_LEVEL > 0
    assert(cond);
#endif
}

static inline void avt_assert2(int cond)
{
#if CONFIG_ASSERT_LEVEL > 1
    assert(cond);
#endif
}

uint64_t avt_get_time_ns(void);

/* Generic macro for creating contexts which need to keep their addresses
 * if another context is created. */
#define FN_CREATING(prefix, ctx, type, shortname, array, num)                  \
static inline type * prefix## _ ## create_ ## shortname(ctx *dctx)             \
{                                                                              \
    type **array, *sctx = calloc(1, sizeof(*sctx));                            \
    if (!sctx)                                                                 \
        return NULL;                                                           \
                                                                               \
    array = realloc(dctx->array, sizeof(*dctx->array) * (dctx->num + 1));      \
    if (!array) {                                                              \
        free(sctx);                                                            \
        return NULL;                                                           \
    }                                                                          \
                                                                               \
    dctx->array = array;                                                       \
    dctx->array[dctx->num++] = sctx;                                           \
                                                                               \
    return sctx;                                                               \
}

/* Zero (usually) alloc FIFO. Payload is ref'd, and leaves with a ref. */
typedef struct AVTOutputPacket {
    union AVTPacketData pkt;
    AVTBuffer pl;
} AVTOutputPacket;

typedef struct AVTPacketFifo {
    AVTOutputPacket *data;
    unsigned int nb;
    unsigned int alloc;
} AVTPacketFifo;

/* Push a packet to the FIFO */
int avt_pkt_fifo_push(AVTPacketFifo *fifo,
                      union AVTPacketData pkt, AVTBuffer *pl);

/* Pop a packet from the FIFO. quick_ref'd into pl */
int avt_pkt_fifo_pop(AVTPacketFifo *fifo,
                     union AVTPacketData *pkt, AVTBuffer *pl);

/* Peek a packet from the FIFO. quick_ref'd into pl */
int avt_pkt_fifo_peek(AVTPacketFifo *fifo,
                      union AVTPacketData *pkt, AVTBuffer *pl);

/* Copy and ref all packets from src to dst */
int avt_pkt_fifo_copy(AVTPacketFifo *dst, AVTPacketFifo *src);

/* Move all packets from src to dst */
int avt_pkt_fifo_move(AVTPacketFifo *dst, AVTPacketFifo *src);

/* Drop packets from the tail.
 * If nb_pkts == 0, the ceiling is a size limit. */
int avt_pkt_fifo_drop(AVTPacketFifo *fifo,
                      unsigned nb_pkts, size_t ceiling);

/* Get the current size of the FIFO */
size_t avt_pkt_fifo_size(AVTPacketFifo *fifo);

/* Clear all packets in the fifo */
void avt_pkt_fifo_clear(AVTPacketFifo *fifo);

/* Free all resources */
void avt_pkt_fifo_free(AVTPacketFifo *fifo);

#endif /* AVTRANSPORT_UTILS_H */
