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

#ifndef LIBAVTRANSPORT_BUFFER
#define LIBAVTRANSPORT_BUFFER

#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>

#include <avtransport/utils.h>
#include "attributes.h"

enum AVTBufferFlags {
    AVT_BUFFER_FLAG_READ_ONLY = 1 << 0,
};

struct AVTBuffer {
    uint8_t *data;      /* Current ref's view of the buffer */
    size_t len;         /* Current ref's size of the view of the buffer */

    uint8_t *base_data; /* Buffer's actual start data */
    uint8_t *end_data;  /* Buffer's end of data */

    avt_free_cb free;
    void *opaque;
    enum AVTBufferFlags flags;
    atomic_int *refcnt;
};

void avt_buffer_update(AVTBuffer *buf, void *data, size_t len);
int avt_buffer_resize(AVTBuffer *buf, size_t len);

static inline int avt_buffer_quick_create(AVTBuffer *buf, uint8_t *data,
                                          size_t len, void *opaque,
                                          avt_free_cb free_cb,
                                          enum AVTBufferFlags flags)
{
    buf->refcnt = malloc(sizeof(*buf->refcnt));
    if (!buf->refcnt)
        return AVT_ERROR(ENOMEM);

    atomic_init(buf->refcnt, 1);

    buf->base_data = data;
    buf->end_data = data + len;
    buf->data = data;
    buf->len = len;
    buf->opaque = opaque;
    if (!free_cb)
        buf->free = avt_buffer_default_free;
    else
        buf->free = free_cb;

    return 0;
}

static inline uint8_t *avt_buffer_quick_alloc(AVTBuffer *buf, size_t len)
{
    void *alloc = malloc(len);
    if (!alloc)
        return NULL;

    int err = avt_buffer_quick_create(buf, alloc, len, NULL,
                                      avt_buffer_default_free , 0);
    if (err < 0) {
        free(alloc);
        return NULL;
    }

    return alloc;
}

static inline void avt_buffer_quick_unref(AVTBuffer *buf)
{
    if (!buf || !buf->refcnt)
        return;

    if (atomic_fetch_sub_explicit(buf->refcnt, 1, memory_order_acq_rel) <= 1) {
        buf->free(buf->opaque, buf->data, buf->end_data - buf->base_data);
        free(buf->refcnt);
    }

    /* Zero out to avoid leaks */
    memset(buf, 0, sizeof(*buf));
}

static inline void avt_buffer_quick_ref(AVTBuffer *dst, AVTBuffer *buf,
                                        ptrdiff_t offset, size_t len)
{
    avt_buffer_quick_unref(dst);

    if (!buf || !buf->refcnt)
        return;

    avt_assert0(buf->base_data + offset < buf->end_data);

    atomic_fetch_add_explicit(buf->refcnt, 1, memory_order_relaxed);
    memcpy(dst, buf, sizeof(*dst));

    dst->data += offset;
    dst->len = (len == AVT_BUFFER_REF_ALL) ? (dst->end_data - dst->data) : len;
}

int avt_buffer_offset(AVTBuffer *buf, ptrdiff_t offset);

#endif
