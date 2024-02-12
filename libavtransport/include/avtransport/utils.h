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

#ifndef LIBAVTRANSPORT_UTILS_H
#define LIBAVTRANSPORT_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

typedef struct AVTContext AVTContext;
typedef struct AVTMetadata AVTMetadata;
typedef struct AVTBuffer AVTBuffer;

/* All functions return negative values for errors.
 * This is a wrapper that's used to convert standard stderr values into
 * negatives. */
#define AVT_ERROR(err) (-(err))

#define AVT_MIN(v1, v2) ((v1) < (v2) ? (v1) : (v2))
#define AVT_MAX(v1, v2) ((v1) > (v2) ? (v1) : (v2))
#define AVT_ARRAY_ELEMS(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Any function flagged with AVT_API is exported as a symbol */
#ifndef AVT_API
    #if defined _WIN32
        #if defined AVT_BUILDING_DLL
            #define AVT_API __declspec(dllexport)
        #else
            #define AVT_API
        #endif
    #else
        #if __GNUC__ >= 4
            #define AVT_API __attribute__ ((visibility ("default")))
        #else
            #define AVT_API
        #endif
    #endif
#endif

#if defined(__GNUC__) || defined(__clang__)
#define avt_printf_format(fmtpos, attrpos) __attribute__((__format__(__printf__, fmtpos, attrpos)))
#else
#define avt_printf_format(fmtpos, attrpos)
#endif

/* Probe if the sequence of 36 bytes represents a valid AVT packet, or
 * the start of one. Returns 'true' if so. Result is trustworthy. */
AVT_API bool avt_data_probe(uint8_t data[36]);

/* Create a reference counted buffer from existing data. */
AVT_API AVTBuffer *avt_buffer_create(uint8_t *data, size_t len,
                                     void *opaque, void (*free_fn)(void *opaque, void *base_data));

/* Default freeing callback for buffers that simply calls free(base_data) */
AVT_API void avt_buffer_default_free(void *opaque, void *base_data);

/* Create and allocate a reference counted buffer. */
AVT_API AVTBuffer *avt_buffer_alloc(size_t len);

/* References the buffer. Returns a new reference at the offset and length requested.
 * If offset AND length are 0, references the whole buffer. */
AVT_API AVTBuffer *avt_buffer_reference(AVTBuffer *buffer, ptrdiff_t offset, int64_t len);

/* Returns the current numer of references */
AVT_API int avt_buffer_get_refcount(AVTBuffer *buffer);

/* Access the data in a buffer. Does not reference it. */
AVT_API void *avt_buffer_get_data(AVTBuffer *buffer, size_t *len);

/* Get the data length */
AVT_API size_t avt_buffer_get_data_len(const AVTBuffer *buffer);

/* Unreference a reference counted buffer. */
AVT_API void avt_buffer_unref(AVTBuffer **buffer);

#endif
