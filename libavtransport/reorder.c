/*
 * Copyright © 2022 Lynne
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the “Software”), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>

#include "reorder.h"
#include "common.h"
#include "buffer.h"

typedef struct PQBufEntry {
    uint32_t seq;
    ptrdiff_t seg_offset;
    size_t    seg_len;

    AVTBuffer data;

    struct PQBufEntry *start;
    struct PQBufEntry *prev;
    struct PQBufEntry *next;
} PQBufEntry;

typedef struct PQReorder {
    PQBufEntry *stream_data;

    PQBufEntry **avail;
    int nb_avail;
    PQBufEntry **used;
    int nb_used;
} PQReorder;

int pq_reorder_init(AVTContext *ctx, size_t max_buffer)
{
    PQReorder *reorder = ctx->src.rctx = calloc(1, sizeof(PQReorder));
    if (!ctx)
        return AVT_ERROR(ENOMEM);

    reorder->nb_avail = 32;

    reorder->avail = calloc(reorder->nb_avail, sizeof(PQBufEntry *));
    if (!reorder->avail)
        return AVT_ERROR(ENOMEM);

    reorder->used = calloc(reorder->nb_avail, sizeof(PQBufEntry *));
    if (!reorder->used)
        return AVT_ERROR(ENOMEM);

    for (int i = 0; i < reorder->nb_avail; i++) {
        reorder->avail[i] = calloc(1, sizeof(PQBufEntry));
        if (!reorder->avail[i])
            return AVT_ERROR(ENOMEM);
    }

    return 0;
}

static int pq_reorder_alloc(PQReorder *reorder)
{
    PQBufEntry **atmp = realloc(reorder->avail, (reorder->nb_avail + 1)*sizeof(PQBufEntry *));
    if (!atmp)
        return AVT_ERROR(ENOMEM);

    reorder->avail = atmp;

    atmp = realloc(reorder->used, (reorder->nb_used + 1)*sizeof(PQBufEntry *));
    if (!atmp)
        return AVT_ERROR(ENOMEM);

    reorder->used = atmp;

    atmp[reorder->nb_avail] = calloc(1, sizeof(PQBufEntry));
    if (!atmp[reorder->nb_avail])
        return AVT_ERROR(ENOMEM);

    reorder->nb_avail++;

    return 0;
}

int pq_reorder_push_pkt(AVTContext *ctx, AVTBuffer *data,
                        ptrdiff_t offset, uint32_t seq,
                        enum PQPacketType type)
{
    int ret;
    PQReorder *reorder = ctx->src.rctx;
    PQBufEntry *ins, *tmp, *entry = NULL;

    if (type == (AVT_PKT_STREAM_DATA & 0xFF00) ||
        type == (AVT_PKT_STREAM_SEG_DATA) ||
        type == (AVT_PKT_STREAM_SEG_END))
        entry = ctx->src.rctx->stream_data;

    while (entry->next && (entry->seq < seq) && (entry->seg_offset < offset))
        entry = entry->next;

    if (!reorder->nb_avail) {
        ret = pq_reorder_alloc(reorder);
        if (ret < 0)
            return ret;
    }

    ins = reorder->avail[reorder->nb_avail - 1];
    reorder->nb_avail--;
    reorder->used[reorder->nb_used] = ins;
    reorder->nb_used++;

    ins->seq = seq;
    ins->seg_offset = offset;
    ins->seg_len = avt_buffer_get_data_len(data);

    tmp = entry->next;
    entry->next = ins;
    ins->next = tmp;
    ins->prev = entry;

    return 0;
}

void pq_reorder_uninit(AVTContext *ctx)
{
    PQReorder *reorder = ctx->src.rctx;

    for (int i = 0; i < reorder->nb_used; i++) {
        pq_buffer_quick_unref(&reorder->used[i]->data);
        free(reorder->used[i]);
    }
    for (int i = 0; i < reorder->nb_avail; i++)
        free(reorder->avail[i]);

    free(reorder->avail);
    free(reorder->used);

    free(ctx->src.rctx);
    ctx->src.rctx = NULL;
}
