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

#include <stdlib.h>

#include "reorder.h"
#include "common.h"
#include "buffer.h"

typedef struct AVTBufEntry {
    uint32_t seq;
    ptrdiff_t seg_offset;
    size_t    seg_len;

    AVTBuffer data;

    struct AVTBufEntry *start;
    struct AVTBufEntry *prev;
    struct AVTBufEntry *next;
} AVTBufEntry;

typedef struct AVTReorder {
    AVTBufEntry *stream_data;

    AVTBufEntry **avail;
    int nb_avail;
    AVTBufEntry **used;
    int nb_used;
} AVTReorder;

int avt_reorder_init(AVTContext *ctx, size_t max_buffer)
{
    AVTReorder *reorder = ctx->src.rctx = calloc(1, sizeof(AVTReorder));
    if (!ctx)
        return AVT_ERROR(ENOMEM);

    reorder->nb_avail = 32;

    reorder->avail = calloc(reorder->nb_avail, sizeof(AVTBufEntry *));
    if (!reorder->avail)
        return AVT_ERROR(ENOMEM);

    reorder->used = calloc(reorder->nb_avail, sizeof(AVTBufEntry *));
    if (!reorder->used)
        return AVT_ERROR(ENOMEM);

    for (int i = 0; i < reorder->nb_avail; i++) {
        reorder->avail[i] = calloc(1, sizeof(AVTBufEntry));
        if (!reorder->avail[i])
            return AVT_ERROR(ENOMEM);
    }

    return 0;
}

static int avt_reorder_alloc(AVTReorder *reorder)
{
    AVTBufEntry **atmp = realloc(reorder->avail, (reorder->nb_avail + 1)*sizeof(AVTBufEntry *));
    if (!atmp)
        return AVT_ERROR(ENOMEM);

    reorder->avail = atmp;

    atmp = realloc(reorder->used, (reorder->nb_used + 1)*sizeof(AVTBufEntry *));
    if (!atmp)
        return AVT_ERROR(ENOMEM);

    reorder->used = atmp;

    atmp[reorder->nb_avail] = calloc(1, sizeof(AVTBufEntry));
    if (!atmp[reorder->nb_avail])
        return AVT_ERROR(ENOMEM);

    reorder->nb_avail++;

    return 0;
}

int avt_reorder_push_pkt(AVTContext *ctx, AVTBuffer *data,
                        ptrdiff_t offset, uint32_t seq,
                        enum AVTPktDescriptors pkt)
{
    int ret;
    AVTReorder *reorder = ctx->src.rctx;
    AVTBufEntry *ins, *tmp, *entry = NULL;

    if (pkt == (AVT_PKT_STREAM_DATA & 0xFF00) ||
        pkt == (AVT_PKT_STREAM_DATA_SEGMENT))
        entry = ctx->src.rctx->stream_data;

    while (entry->next && (entry->seq < seq) && (entry->seg_offset < offset))
        entry = entry->next;

    if (!reorder->nb_avail) {
        ret = avt_reorder_alloc(reorder);
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

void avt_reorder_uninit(AVTContext *ctx)
{
    AVTReorder *reorder = ctx->src.rctx;

    for (int i = 0; i < reorder->nb_used; i++) {
        avt_buffer_quick_unref(&reorder->used[i]->data);
        free(reorder->used[i]);
    }
    for (int i = 0; i < reorder->nb_avail; i++)
        free(reorder->avail[i]);

    free(reorder->avail);
    free(reorder->used);

    free(ctx->src.rctx);
    ctx->src.rctx = NULL;
}
