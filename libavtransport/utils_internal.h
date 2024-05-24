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
#include <stdbit.h>

#include <sys/socket.h>

#include <avtransport/utils.h>
#include <avtransport/packet_data.h>
#include "buffer.h"
#include "packet_common.h"

#include "config.h"
#include "attributes.h"

#define AVT_SWAP(a, b)           \
    do {                         \
        typeof(a) swaptmp = (a); \
        a = b;                   \
        b = swaptmp;             \
    } while (0)

int64_t avt_get_time_ns(void);

static inline int avt_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c ^ 0x20;
    return c;
}

static inline int avt_ascii_to_int(int c)
{
    c = avt_tolower(c);
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= '0' && c <= '9')
        return c - '0';
    return -1;
}

#define avt_hamming_dist(a, b) \
    stdc_count_ones((a) ^ (b))

/* Zero (usually) alloc FIFO. Payload is ref'd, and leaves with a ref. */
typedef struct AVTPacketFifo {
    AVTPktd *data;
    unsigned int nb;
    unsigned int alloc;
} AVTPacketFifo;

/* Push a packet to the FIFO */
int avt_pkt_fifo_push_d(AVTPacketFifo *fifo, AVTPktd *p);
int avt_pkt_fifo_push_p(AVTPacketFifo *fifo,
                        union AVTPacketData pkt, AVTBuffer *pl);
#define avt_pkt_fifo_push(f, x, ...)                   \
    _Generic((x),                                      \
             AVTPktd *: avt_pkt_fifo_push_d,           \
             union AVTPacketData: avt_pkt_fifo_push_p  \
    ) (f, x __VA_OPT__(,) __VA_ARGS__)

/* Push a referenced packet to the FIFO (skips a quick ref, takes ownership) */
int avt_pkt_fifo_push_refd_d(AVTPacketFifo *fifo, AVTPktd *p);
int avt_pkt_fifo_push_refd_p(AVTPacketFifo *fifo,
                             union AVTPacketData pkt, AVTBuffer *pl);
#define avt_pkt_fifo_push_refd(f, x, ...)                   \
    _Generic((x),                                           \
             AVTPktd *: avt_pkt_fifo_push_refd_d,           \
             union AVTPacketData: avt_pkt_fifo_push_refd_p  \
    ) (f, x __VA_OPT__(,) __VA_ARGS__)

/* Returns a blank AVTPktd or NULL on error.
 * pl is an optional parameter which is quick_ref'd
 * within the provided parameters */
AVTPktd *avt_pkt_fifo_push_new(AVTPacketFifo *fifo, AVTBuffer *pl,
                               ptrdiff_t offset, size_t len);

/* Pop a packet from the FIFO. quick_ref'd into pl */
int avt_pkt_fifo_pop_d(AVTPacketFifo *fifo, AVTPktd *p);
int avt_pkt_fifo_pop_p(AVTPacketFifo *fifo,
                       union AVTPacketData *pkt, AVTBuffer *pl);
#define avt_pkt_fifo_pop(f, x, ...)                    \
    _Generic((x),                                      \
             AVTPktd *: avt_pkt_fifo_pop_d,            \
             union AVTPacketData *: avt_pkt_fifo_pop_p \
    ) (f, x __VA_OPT__(,) __VA_ARGS__)

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

/* Free all resources in/for a fifo */
void avt_pkt_fifo_free(AVTPacketFifo *fifo);

/* Sliding window */
#define AVT_SLIDING_WINDOW_MAX_ENTRIES (1024*512)

typedef struct AVTSlidingWinCtx {
    int num_entries;
    PACKED(struct AVTSlidingWinEntry {
        int64_t val;
        int64_t ts;
        AVTRational tb;
    }) entries[AVT_SLIDING_WINDOW_MAX_ENTRIES];
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
