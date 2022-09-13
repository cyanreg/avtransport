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

#ifndef LIBQPROTO_UTILS_HEADER
#define LIBQPROTO_UTILS_HEADER

#include <stddef.h>
#include <stdint.h>

/* Rational data type structure. */
typedef struct QprotoRational {
    int num;
    int den;
} QprotoRational;

typedef struct QprotoBuffer QprotoBuffer;

/* Create a reference counted buffer from existing data. */
QprotoBuffer *qp_buffer_create(uint8_t *data, size_t len,
                               void *opaque, void (*free_fn)(void *opaque, void *base_data));

/* Default freeing callback for buffers that simply calls free(base_data) */
void qp_buffer_default_free(void *opaque, void *base_data);

/* Create and allocate a reference counted buffer. */
QprotoBuffer *qp_buffer_alloc(size_t len);

/* References the buffer. Returns a new reference at the offset and length requested.
 * If offset AND length are 0, references the whole buffer. */
QprotoBuffer *qp_buffer_reference(QprotoBuffer *buffer, ptrdiff_t offset, int64_t len);

/* Returns the current numer of references */
int qp_buffer_get_refcount(QprotoBuffer *buffer);

/* Access the data in a buffer. Does not reference it. */
void *qp_buffer_get_data(QprotoBuffer *buffer, size_t *len);

/* Get the data length */
size_t qp_buffer_get_data_len(QprotoBuffer *buffer);

/* Unreference a reference counted buffer. */
void qp_buffer_unref(QprotoBuffer **buffer);

/* All functions return negative values for errors.
 * This is a wrapper that's used to convert standard stderr values into
 * negatives. */
#define QP_ERROR(err) (-(err))

#define QPMIN(v1, v2) ((v1) < (v2) ? (v1) : (v2))
#define QPMAX(v1, v2) ((v1) > (v2) ? (v1) : (v2))

#endif
