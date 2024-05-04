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
#include <stdckdint.h>

#include "buffer.h"
#include "utils_internal.h"

AVT_API AVTBuffer *avt_buffer_create(uint8_t *data, size_t len,
                                     void *opaque, avt_free_cb free_cb)
{
    AVTBuffer *buf = malloc(sizeof(*buf));
    if (!buf)
        return NULL;

    int err = avt_buffer_quick_create(buf, data, len, opaque, free_cb, 0);
    if (err < 0) {
        free(buf);
        return NULL;
    }

    return buf;
}

void avt_buffer_update(AVTBuffer *buf, void *data, size_t len)
{
    avt_assert0(avt_buffer_get_refcount(buf) == 1);
    buf->data      = ((uint8_t *)data) + (buf->data - buf->base_data);
    buf->base_data = data;
    buf->end_data  = ((uint8_t *)data) + len;
    buf->len       = len;
}

int avt_buffer_resize(AVTBuffer *buf, size_t len)
{
    /* Simple downsize, or there's enough allocated already */
    if ((buf->len >= len) ||
        (len < (buf->end_data - buf->data))) {
        buf->len = len;
        return 0;
    }

    /* Sanity checking */
    avt_assert0(avt_buffer_get_refcount(buf) == 1);
    avt_assert0(buf->free == avt_buffer_default_free);

    /* Account for data before the current ref's slice */
    size_t tmp;
    size_t pre_data = buf->data - buf->base_data;
    if (ckd_add(&tmp, len, pre_data))
        return AVT_ERROR(EINVAL);

    uint8_t *newdata = realloc(buf->base_data, tmp);
    if (!newdata)
        return AVT_ERROR(ENOMEM);

    buf->base_data = newdata;
    buf->data      = newdata + pre_data;
    buf->end_data  = newdata + tmp;
    buf->len       = len;

    return 0;
}

void avt_buffer_default_free(void *opaque, void *base_data, size_t len)
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

AVTBuffer *avt_buffer_ref(AVTBuffer *buf, ptrdiff_t offset, size_t len)
{
    if (!buf || !buf->refcnt)
        return NULL;

    if (!len)
        len = buf->end_data - (buf->data + offset);
    if (len < 0)
        return NULL;
    if (buf->base_data + offset > buf->end_data)
        return NULL;

    AVTBuffer *ret = malloc(sizeof(*ret));
    if (!ret)
        return NULL;

    atomic_fetch_add_explicit(buf->refcnt, 1, memory_order_relaxed);

    memcpy(ret, buf, sizeof(*ret));

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

int avt_buffer_get_refcount(AVTBuffer *buf)
{
    if (!buf || !buf->refcnt)
        return 0;

    return atomic_load_explicit(buf->refcnt, memory_order_relaxed);
}

void *avt_buffer_get_data(AVTBuffer *buf, size_t *len)
{
    if (!buf || !buf->refcnt) {
        if (len)
            *len = 0;
        return NULL;
    }

    if (len)
        *len = buf->len;
    return buf->data;
}

size_t avt_buffer_get_data_len(const AVTBuffer *buf)
{
    if (!buf || !buf->refcnt)
        return 0;

    return buf->len;
}

bool avt_buffer_read_only(AVTBuffer *buffer)
{
    return !!(buffer->flags & AVT_BUFFER_FLAG_READ_ONLY);
}

void avt_buffer_unref(AVTBuffer **_buf)
{
    AVTBuffer *buf = *_buf;
    if (!buf || !buf->refcnt)
        return;

    avt_buffer_quick_unref(buf);

    free(buf);

    *_buf = NULL;
}
