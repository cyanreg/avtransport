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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <libqproto/input.h>

#include "common.h"
#include "libqproto/common.h"
#include "input.h"

struct PQInputContext {
    FILE *f;
};

static int file_init(QprotoContext *ctx, PQInputContext **pc,
                     QprotoInputSource *src, QprotoInputOptions *opts)
{
    PQInputContext *priv = malloc(sizeof(*priv));
    if (!priv)
        return QP_ERROR(ENOMEM);

    priv->f = fopen(src->path, "r");
    if (!priv->f) {
        free(priv);
        return QP_ERROR(errno);
    }

    *pc = priv;

    return 0;
}

static int file_process(QprotoContext *qp, PQInputContext *pc)
{


    return 0;
}

static int file_close(QprotoContext *ctx, PQInputContext **pc)
{
    int ret = fclose((*pc)->f);
    free(*pc);
    *pc = NULL;
    if (ret)
        return QP_ERROR(errno);

    return 0;
}

const PQInput pq_input_file = {
    .name = "file",
    .type = QPROTO_CONNECTION_FILE,
    .init = file_init,
    .process = file_process,
    .close = file_close,
};
