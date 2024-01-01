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
#include <string.h>

#include "buffer.h"
#include "utils_internal.h"

AVTBuffer *avt_buffer_create(uint8_t *data, size_t len,
                             void *opaque, void (*free_fn)(void *opaque, void *base_data))
{
    AVTBuffer *buf = malloc(sizeof(*buf));
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

int avt_buffer_realloc(AVTBuffer *buf, size_t len)
{
    avt_assert2(avt_buffer_get_refcount(buf) == 1);
    avt_assert2(buf->free == avt_buffer_default_free);

    uint8_t *newdata = realloc(buf->base_data, len);
    if (!newdata)
        return AVT_ERROR(ENOMEM);

    buf->base_data = newdata;
    buf->end_data = newdata + len;
    buf->len = len;

    return 0;
}

void avt_buffer_default_free(void *opaque, void *base_data)
{
    free(base_data);
}

AVTBuffer *avt_buffer_alloc(size_t len)
{
    void *alloc = malloc(len);
    if (!alloc)
        return NULL;

    AVTBuffer *buf = avt_buffer_create(alloc, len, NULL, avt_buffer_default_free);
    if (!buf) {
        free(alloc);
        return NULL;
    }

    return buf;
}

AVTBuffer *avt_buffer_reference(AVTBuffer *buffer, ptrdiff_t offset, int64_t len)
{
    if (!buffer)
        return NULL;

    if (!len)
        len = buffer->end_data - (buffer->data + offset);
    if (len < 0)
        return NULL;
    if (buffer->base_data + offset > buffer->end_data)
        return NULL;

    AVTBuffer *ret = malloc(sizeof(*ret));
    if (!ret)
        return NULL;

    atomic_fetch_add_explicit(buffer->refcnt, 1, memory_order_relaxed);

    memcpy(ret, buffer, sizeof(*ret));

    ret->data += offset;
    ret->len = !len ? ret->end_data - ret->data : len;

    return ret;
}

int avt_buffer_offset(AVTBuffer *buf, ptrdiff_t offset)
{
    if (buf->base_data + offset > buf->end_data)
        return AVT_ERROR(EINVAL);

    buf->data += offset;

    return 0;
}

int avt_buffer_quick_ref(AVTBuffer *dst, AVTBuffer *buffer,
                         ptrdiff_t offset, size_t len)
{
    if (!buffer)
        return 0;
    else if (buffer->base_data + offset > buffer->end_data)
        return AVT_ERROR(EINVAL);

    atomic_fetch_add_explicit(buffer->refcnt, 1, memory_order_relaxed);

    memcpy(dst, buffer, sizeof(*dst));

    dst->data += offset;
    dst->len = !len ? dst->end_data - dst->data : len;

    return 0;
}

void avt_buffer_quick_unref(AVTBuffer *buf)
{
    if (!buf)
        return;

    if (atomic_fetch_sub_explicit(buf->refcnt, 1, memory_order_acq_rel) == 1) {
        buf->free(buf->opaque, buf->data);
        free(buf->refcnt);
    }

    /* Zero out to avoid leaks */
    memset(buf, 0, sizeof(*buf));
}

int avt_buffer_get_refcount(AVTBuffer *buffer)
{
    if (!buffer)
        return 0;

    return atomic_load_explicit(buffer->refcnt, memory_order_relaxed);
}

void *avt_buffer_get_data(AVTBuffer *buffer, size_t *len)
{
    if (!buffer) {
        *len = 0;
        return NULL;
    }

    *len = buffer->len;
    return buffer->data;
}

size_t avt_buffer_get_data_len(AVTBuffer *buffer)
{
    if (!buffer)
        return 0;

    return buffer->len;
}

void avt_buffer_unref(AVTBuffer **buffer)
{
    AVTBuffer *buf = *buffer;
    if (!buffer)
        return;

    avt_buffer_quick_unref(buf);

    free(buf);
}
