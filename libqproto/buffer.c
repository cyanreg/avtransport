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
#include <string.h>
#include <stdatomic.h>

#include "common.h"

struct QprotoBuffer {
    void *data;
    size_t len;
    void (*free)(void *opaque, void *data);
    void *opaque;
    atomic_int refcnt;
};

QprotoBuffer *qp_buffer_create(void *data, size_t len,
                               void *opaque, void (*free)(void *opaque, void *data))
{
    QprotoBuffer *buf = malloc(sizeof(*buf));
    if (!buf)
        return NULL;

    atomic_init(&buf->refcnt, 1);

    buf->data = data;
    buf->len = len;
    buf->opaque = opaque;
    buf->free = free;

    return buf;
}

void qp_buffer_default_free(void *opaque, void *data)
{
    free(data);
}

QprotoBuffer *qp_buffer_alloc(size_t len)
{
    void *alloc = malloc(len);
    if (!alloc)
        return NULL;

    QprotoBuffer *buf = qp_buffer_create(alloc, len, NULL, qp_buffer_default_free);
    if (!buf) {
        free(alloc);
        return NULL;
    }

    return buf;
}

int qp_buffer_reference(QprotoBuffer *buffer)
{
    int old = atomic_fetch_add_explicit(&buffer->refcnt, 1, memory_order_relaxed);
    return old + 1;
}

void *qp_buffer_get_data(QprotoBuffer *buffer, size_t *len)
{
    *len = buffer->len;
    return buffer->data;
}

size_t qp_buffer_get_data_len(QprotoBuffer *buffer)
{
    return buffer->len;
}

void qp_buffer_unref(QprotoBuffer **buffer)
{
    QprotoBuffer *buf = *buffer;
    if (atomic_fetch_sub_explicit(&buf->refcnt, 1, memory_order_acq_rel) == 1) {
        buf->free(buf->opaque, buf->data);
        free(buf);
        *buffer = NULL;
    }
}
