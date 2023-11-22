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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <avtransport/avtransport.h>

#include "common.h"

int avt_init(AVTContext **ctx, AVTContextOptions *opts)
{
    AVTContext *tmp = malloc(sizeof(*tmp));
    if (!tmp)
        return AVT_ERROR(ENOMEM);

    if (opts)
        tmp->opts = *opts;

    atomic_init(&tmp->output.seq, 0);
    atomic_init(&tmp->input.seq, 0);

    *ctx = tmp;
    return 0;
}

void avt_close(AVTContext **ctx)
{
    if (ctx) {
        free(*ctx);
        *ctx = NULL;
    }
}

AVTStream *avt_alloc_stream(AVTContext *ctx, uint16_t id, AVTStream **stl, int *nb_st)
{
    if (!ctx->output.ctx)
        return NULL;

    AVTStream *ret = NULL;
    AVTStream **new = realloc(ctx->stream, sizeof(*new)*(ctx->nb_stream + 1));
    if (!new)
        return NULL;
    ctx->stream = new;

    ret = calloc(1, sizeof(**new));
    if (!ret)
        return NULL;

    ret->priv = calloc(1, sizeof(*ret->priv));
    if (!ret->priv) {
        free(ret);
        return NULL;
    }

    new[ctx->nb_stream] = ret;
    ctx->nb_stream++;

    return ret;
}

AVTStream *avt_find_stream(AVTContext *ctx, uint16_t id)
{
    int i;
    for (i = 0; i < ctx->nb_stream; i++) {
        if (ctx->stream[i]->id == id)
            break;
    }
    if (i == ctx->nb_stream)
        return NULL;
    return ctx->stream[i];
}

void avt_log(void *ctx, enum AVTLogLevel level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
