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

#include <libqproto/output.h>

#include <linux/io_uring.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libqproto/output.h>

#include "common.h"
#include "libqproto/common.h"
#include "output.h"


struct PQOutputContext {
    int fd;
};

static int sock_init(QprotoContext *ctx, PQOutputContext **pc,
                     QprotoOutputDestination *dst, QprotoOutputOptions *opts)
{
    PQOutputContext *priv = malloc(sizeof(*priv));
    if (!priv)
        return QP_ERROR(ENOMEM);


    *pc = priv;

    return 0;
}

static int sock_output(QprotoContext *ctx, PQOutputContext *pc,
                       uint8_t *hdr, size_t hdr_len, QprotoBuffer *buf)
{
    size_t len;
    void *data = qp_buffer_get_data(buf, &len);


    return 0;
}

static int sock_close(QprotoContext *ctx, PQOutputContext **pc)
{

    return 0;
}

const PQOutput pq_output_socket = {
    .name = "file",
    .type = QPROTO_CONNECTION_SOCKET,
    .init = sock_init,
    .output = sock_output,
    .close = sock_close,
};
