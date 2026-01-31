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

#include <stdlib.h>

#include "reorder.h"

int avt_reorder_init(AVTContext *ctx, AVTReorder *r, size_t max_size)
{
    return 0;
}

int avt_reorder_push(AVTReorder *r, AVTPacketFifo *in)
{
    int ret;

    for (auto i = 0; i < in->nb; i++) {
        AVTPktd *p = &in->data[i];
        uint32_t pkt_target = 0;
        AVTReorderStream *rs = &r->st[p->pkt.stream_id];

        int group_idx = 0;
        do {
            AVTMerger *m = &rs->m[group_idx];
            if (m->active && (pkt_target != m->target))
                continue;

            ret = avt_pkt_merge_seg(r->ctx, &rs->m[0], p);
        } while (group_idx++ < AVT_REORDER_GROUP_NB);

        if (!rs->nb_groups) {
            ret = avt_pkt_merge_seg(r->ctx, &rs->m[0], p);
            if (ret == AVT_ERROR(EAGAIN)) {
                rs->nb_groups++;
                continue;
            } else if (ret < 0) {
                return ret;
            }

            /* ret == 0 -> standalone packet */
            ret = avt_pkt_fifo_push(r->staging, p);
            if (ret < 0)
                return ret;
        }

        for (auto j = 0; j < rs->nb_groups; j++) {
            ret = avt_pkt_merge_seg(r->ctx, &rs->m[j], p);
            /* Need more packets */
            if (ret == AVT_ERROR(EAGAIN))
                goto next;
            else if (ret == AVT_ERROR(EBUSY))
                continue;
            else if (ret < 0)
                return ret;

            /* ret == 1 -> done */
        }
    next:
    }

    return 0;
}

int avt_reorder_pop(AVTReorder *r, AVTPacketFifo *out)
{
    return 0;
}

int avt_reorder_done(AVTPacketFifo *out)
{
    return 0;
}

int avt_reorder_free(AVTReorder *r)
{
    return 0;
}
