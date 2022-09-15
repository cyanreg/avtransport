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

#ifndef LIBQPROTO_BUFFER
#define LIBQPROTO_BUFFER

#include <stdatomic.h>

#include <libqproto/utils.h>

struct QprotoBuffer {
    uint8_t *data;
    size_t len;

    uint8_t *base_data;
    uint8_t *end_data;

    void (*free)(void *opaque, void *data);
    void *opaque;
    atomic_int *refcnt;
};

int pq_buffer_quick_ref(QprotoBuffer *dst, QprotoBuffer *buffer,
                        ptrdiff_t offset, int64_t len);

void pq_buffer_quick_unref(QprotoBuffer *buf);

int pq_buffer_offset(QprotoBuffer *buf, ptrdiff_t offset);

#endif
