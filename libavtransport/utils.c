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
#include <stdlib.h>
#include <stdckdint.h>
#include <arpa/inet.h>

#include <avtransport/avtransport.h>

#include "utils_internal.h"
#include "mem.h"

int64_t avt_get_time_ns(void)
{
    struct timespec ts;

    if (!timespec_get(&ts, TIME_UTC)) {
        avt_log(NULL, AVT_LOG_WARN, "Unable to get current time, assuming zero!\n");
        return 0;
    }

    return ((int64_t)ts.tv_sec * 1000000000) + (int64_t)ts.tv_nsec;
}

void avt_pkt_fifo_clear(AVTPacketFifo *fifo)
{
    for (int i = 0; i < fifo->nb; i++) {
        AVTPktd *data = &fifo->data[i];
        avt_buffer_quick_unref(&data->pl);
    }
    fifo->nb = 0;
}

void avt_pkt_fifo_free(AVTPacketFifo *fifo)
{
    avt_pkt_fifo_clear(fifo);
    free(fifo->data);
    memset(fifo, 0, sizeof(*fifo));
}

static int fifo_resize(AVTPacketFifo *fifo, unsigned int alloc_new)
{
    alloc_new = AVT_MAX(alloc_new, 1);

    AVTPktd *alloc_pkt = avt_reallocarray(fifo->data, alloc_new,
                                          sizeof(*fifo->data));
    if (!alloc_pkt)
        return AVT_ERROR(ENOMEM);
    fifo->data = alloc_pkt;

    fifo->alloc = alloc_new;

    return 0;
}

int avt_pkt_fifo_push(AVTPacketFifo *fifo,
                      union AVTPacketData pkt, AVTBuffer *pl)
{
    if ((fifo->nb + 1) >= fifo->alloc) {
        /* Ptwo allocations */
        if (fifo_resize(fifo, fifo->alloc << 1))
            return AVT_ERROR(ENOMEM);
    }

    AVTPktd *data = &fifo->data[fifo->nb];
    int err = avt_buffer_quick_ref(&data->pl, pl, 0, 0);
    data->pkt = pkt;
    if (err >= 0)
        fifo->nb++;

    return err;
}

int avt_pkt_fifo_push_refd_d(AVTPacketFifo *fifo, AVTPktd *p)
{
    if ((fifo->nb + 1) >= fifo->alloc) {
        /* Ptwo allocations */
        if (fifo_resize(fifo, fifo->alloc << 1))
            return AVT_ERROR(ENOMEM);
    }

    fifo->data[fifo->nb++] = *p;

    /* Zero to prevent leaks */
    p->pl = (AVTBuffer){ };

    return 0;
}

int avt_pkt_fifo_push_refd_p(AVTPacketFifo *fifo,
                             union AVTPacketData pkt, AVTBuffer *pl)
{
    if ((fifo->nb + 1) >= fifo->alloc) {
        /* Ptwo allocations */
        if (fifo_resize(fifo, fifo->alloc << 1))
            return AVT_ERROR(ENOMEM);
    }

    AVTPktd *data = &fifo->data[fifo->nb++];
    data->pkt = pkt;
    if (pl)
        data->pl = *pl;

    /* Zero to prevent leaks */
    *pl = (AVTBuffer){ };

    return 0;
}

int avt_pkt_fifo_copy(AVTPacketFifo *dst, const AVTPacketFifo *src)
{
    if ((dst->nb + src->nb) >= dst->alloc) {
        if (fifo_resize(dst, dst->alloc + src->nb))
            return AVT_ERROR(ENOMEM);
    }

    for (int i = 0; i < src->nb; i++) {
        AVTPktd *pdst = &dst->data[dst->nb + i];
        AVTPktd *psrc = &src->data[i];
        *psrc = *pdst;
        int err = avt_buffer_quick_ref(&pdst->pl, &psrc->pl, 0, 0);
        if (err < 0) {
            dst->nb += 1;
            return err;
        }
    }

    dst->nb += src->nb;

    return 0;
}

int avt_pkt_fifo_move(AVTPacketFifo *dst, AVTPacketFifo *src)
{
    if ((dst->nb + src->nb) >= dst->alloc) {
        if (fifo_resize(dst, dst->alloc + src->nb))
            return AVT_ERROR(ENOMEM);
    }

    memcpy(&dst->data[dst->nb], src->data, src->nb*sizeof(*dst->data));
    dst->nb += src->nb;
    avt_pkt_fifo_clear(src);

    return 0;
}

int avt_pkt_fifo_peek(const AVTPacketFifo *fifo,
                      union AVTPacketData *pkt, AVTBuffer *pl)
{
    if (!fifo->nb)
        return AVT_ERROR(ENOENT);

    AVTPktd *data = &fifo->data[0];

    *pkt = data->pkt;
    if (pl)
        return avt_buffer_quick_ref(pl, &data->pl, 0, 0);
    else
        return 0;
}

int avt_pkt_fifo_pop(AVTPacketFifo *fifo,
                     union AVTPacketData *pkt, AVTBuffer *pl)
{
    if (!fifo->nb)
        return AVT_ERROR(ENOENT);

    AVTPktd *data = &fifo->data[0];
    if (pkt)
        *pkt = data->pkt;

    if (pl)
        *pl = data->pl;
    else
        avt_buffer_quick_unref(&data->pl);

    fifo->nb--;
    memmove(fifo->data, fifo->data + 1, fifo->nb*sizeof(*fifo->data));

    return 0;
}

static inline size_t avt_pkt_fifo_get_entry_size(AVTPktd *e)
{
    return sizeof(*e) + avt_buffer_get_data_len(&e->pl);
}

int avt_pkt_fifo_drop(AVTPacketFifo *fifo, unsigned int nb_pkts, size_t ceiling)
{
    unsigned int idx = 0;

    if (!nb_pkts) {
        size_t acc = 0;
        for (int i = 0; i < fifo->nb; i++) {
            acc += avt_pkt_fifo_get_entry_size(&fifo->data[i]);
            if (acc > ceiling) {
                idx = i;
                break;
            }
        }
    } else {
        if (ckd_sub(&idx, fifo->nb, nb_pkts))
            return AVT_ERROR(EINVAL);
    }

    for (; idx < fifo->nb; idx++) {
        AVTPktd *data = &fifo->data[idx];
        avt_buffer_quick_unref(&data->pl);
    }

    fifo->nb = idx;

    return 0;
}

size_t avt_pkt_fifo_size(AVTPacketFifo *fifo)
{
    size_t acc = fifo->nb * sizeof(*fifo->data);

    for (int i = 0; i < fifo->nb; i++)
        acc += avt_buffer_get_data_len(&fifo->data[i].pl);

    return acc;
}

int64_t avt_sliding_win(AVTSlidingWinCtx *ctx, int64_t val, int64_t ts,
                        AVTRational tb, int64_t period, bool do_avg)
{
    struct AVTSlidingWinEntry *top, *last;
    int64_t sum = 0;

    if (!ctx->num_entries)
        goto add;

    for (int i = 0; i < ctx->num_entries; i++) {
        last = &ctx->entries[i];
        int64_t test = avt_add_stable(last->tb, last->ts, tb, period);

        /* If the number of values is about to overread, or the value is
         * too old to consider, throw it away. */
        if (((ctx->num_entries + 1) > AVT_SLIDING_WINDOW_MAX_ENTRIES) ||
            avt_compare_ts(test, last->tb, ts, tb) < 0) {
            ctx->num_entries--;
            memmove(last, last + 1, sizeof(*last) * ctx->num_entries);
        }
    }

add:
    top = &ctx->entries[ctx->num_entries++];
    top->val = val;
    top->ts = ts;
    top->tb  = tb;

    /* Calculate the average */
    for (int i = 0; i < ctx->num_entries; i++)
        sum += ctx->entries[i].val;

    if (do_avg && ctx->num_entries)
        sum /= ctx->num_entries;

    return sum;
}
