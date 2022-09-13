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

#include "common.h"
#include "buffer.h"

QprotoBuffer *qp_buffer_create(uint8_t *data, size_t len,
                               void *opaque, void (*free_fn)(void *opaque, void *base_data))
{
    QprotoBuffer *buf = malloc(sizeof(*buf));
    if (!buf)
        return NULL;

    buf->refcnt = malloc(sizeof(*buf->refcnt));
    if (!buf->refcnt) {
        free(buf);
        return NULL;
    }

    atomic_init(buf->refcnt, 1);

    buf->base_data = data;
    buf->end_data = data + len;
    buf->data = data;
    buf->len = len;
    buf->opaque = opaque;
    buf->free = free_fn;

    return buf;
}

void qp_buffer_default_free(void *opaque, void *base_data)
{
    free(base_data);
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

QprotoBuffer *qp_buffer_reference(QprotoBuffer *buffer, ptrdiff_t offset, int64_t len)
{
    if (!buffer)
        return NULL;

    if (!len)
        len = buffer->end_data - (buffer->data + offset);
    if (len < 0)
        return NULL;
    if (buffer->base_data + offset > buffer->end_data)
        return NULL;

    QprotoBuffer *ret = malloc(sizeof(*ret));
    if (!ret)
        return NULL;

    atomic_fetch_add_explicit(buffer->refcnt, 1, memory_order_relaxed);

    memcpy(ret, buffer, sizeof(*ret));

    ret->data += offset;
    ret->len = !len ? ret->end_data - ret->data : len;

    return ret;
}

int pq_buffer_quick_ref(QprotoBuffer *dst, QprotoBuffer *buffer,
                        ptrdiff_t offset, int64_t len)
{
    if (!buffer)
        return 0;

    if (!len)
        len = buffer->end_data - (buffer->data + offset);
    if (len < 0)
        return QP_ERROR(EINVAL);
    if (buffer->base_data + offset > buffer->end_data)
        return QP_ERROR(EINVAL);

    atomic_fetch_add_explicit(buffer->refcnt, 1, memory_order_relaxed);

    memcpy(dst, buffer, sizeof(*dst));

    dst->data += offset;
    dst->len = !len ? dst->end_data - dst->data : len;

    return 0;
}

void pq_buffer_quick_unref(QprotoBuffer *buf)
{
    if (!buf)
        return;

    if (atomic_fetch_sub_explicit(buf->refcnt, 1, memory_order_acq_rel) == 1) {
        buf->free(buf->opaque, buf->data);
        free(buf->refcnt);
    }
}

int qp_buffer_get_refcount(QprotoBuffer *buffer)
{
    if (!buffer)
        return 0;

    return atomic_load_explicit(buffer->refcnt, memory_order_relaxed);
}

void *qp_buffer_get_data(QprotoBuffer *buffer, size_t *len)
{
    if (!buffer) {
        *len = 0;
        return NULL;
    }

    *len = buffer->len;
    return buffer->data;
}

size_t qp_buffer_get_data_len(QprotoBuffer *buffer)
{
    if (!buffer)
        return 0;

    return buffer->len;
}

void qp_buffer_unref(QprotoBuffer **buffer)
{
    QprotoBuffer *buf = *buffer;
    if (!buffer)
        return;

    pq_buffer_quick_unref(buf);

    free(buf);
}
