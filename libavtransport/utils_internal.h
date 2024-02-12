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
#include "packet_common.h"

#include "../config.h"

#define AVT_SWAP(a, b)         \
    do {                       \
        typeof(a) _a = (a);    \
        typeof(b) _b = (b);    \
        typeof(*_a) tmp = *_a; \
        *_a = *_b;             \
        *_b = tmp;             \
    } while(0)

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

int64_t avt_get_time_ns(void);

/* Zero (usually) alloc FIFO. Payload is ref'd, and leaves with a ref. */
typedef struct AVTPacketFifo {
    AVTPktd *data;
    unsigned int nb;
    unsigned int alloc;
} AVTPacketFifo;

/* Push a packet to the FIFO */
int avt_pkt_fifo_push(AVTPacketFifo *fifo,
                      union AVTPacketData pkt, AVTBuffer *pl);

/* Push a referenced packet to the FIFO (skips a quick ref, takes ownership) */
int avt_pkt_fifo_push_refd_d(AVTPacketFifo *fifo, AVTPktd *p);
int avt_pkt_fifo_push_refd_p(AVTPacketFifo *fifo,
                             union AVTPacketData pkt, AVTBuffer *pl);

#define avt_pkt_fifo_push_refd(f, x, ...)                   \
    _Generic((x),                                           \
             AVTPktd *: avt_pkt_fifo_push_refd_d,           \
             union AVTPacketData: avt_pkt_fifo_push_refd_p  \
    ) (f, x __VA_OPT__(,) __VA_ARGS__)

/* Pop a packet from the FIFO. quick_ref'd into pl */
int avt_pkt_fifo_pop(AVTPacketFifo *fifo,
                     union AVTPacketData *pkt, AVTBuffer *pl);

/* Peek a packet from the FIFO. quick_ref'd into pl */
int avt_pkt_fifo_peek(const AVTPacketFifo *fifo,
                      union AVTPacketData *pkt, AVTBuffer *pl);

/* Copy and ref all packets from src to dst */
int avt_pkt_fifo_copy(AVTPacketFifo *dst, const AVTPacketFifo *src);

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

/* Sliding window */
#define AVT_SLIDING_WINDOW_MAX_ENTRIES (1024*512)
typedef struct AVTSlidingWinCtx {
    int num_entries;
    struct AVTSlidingWinEntry {
        int64_t val;
        int64_t ts;
        AVTRational tb;
    } entries[AVT_SLIDING_WINDOW_MAX_ENTRIES];
} AVTSlidingWinCtx;

/*
 * Run a sliding window calculation and produce an output.
 * val is the value for which to average or sum
 * ts is the timestamp for the value
 * tb is the timebase for the timestamp of the value, as well as the period
 * period is the value in tb units over which to average or sum up
 * do_avg specifies that instead of summing, an average is to be performed
 */
int64_t avt_sliding_win(AVTSlidingWinCtx *ctx, int64_t val, int64_t ts,
                        AVTRational tb, int64_t period, bool do_avg);

#endif /* AVTRANSPORT_UTILS_H */
