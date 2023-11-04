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
#include <errno.h>
#include <libavtransport/input.h>
#include <libavtransport/common.h>

#include "common.h"
#include "input.h"

struct PQInputContext {
    FILE *f;
};

static int file_init(AVTContext *ctx, PQInputContext **pc,
                     AVTInputSource *src, AVTInputOptions *opts)
{
    PQInputContext *priv = malloc(sizeof(*priv));
    if (!priv)
        return AVT_ERROR(ENOMEM);

    priv->f = fopen(src->path, "r");
    if (!priv->f) {
        free(priv);
        return AVT_ERROR(errno);
    }

    *pc = priv;

    return 0;
}

static int file_process(AVTContext *ctx, PQInputContext *pc)
{


    return 0;
}

static int file_close(AVTContext *ctx, PQInputContext **pc)
{
    int ret = fclose((*pc)->f);
    free(*pc);
    *pc = NULL;
    if (ret)
        return AVT_ERROR(errno);

    return 0;
}

const PQInput pq_input_file = {
    .name = "file",
    .type = AVT_CONNECTION_FILE,
    .init = file_init,
    .process = file_process,
    .close = file_close,
};
