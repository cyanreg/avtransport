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

#ifndef AVTRANSPORT_MEM_H
#define AVTRANSPORT_MEM_H

#include <stdlib.h>
#include <stdckdint.h>

#if defined(__GNUC__)
    #define avt_alloc_attrib(...) __attribute__((__malloc__, alloc_size(__VA_ARGS__)))
#else
    #define avt_alloc_attrib(...)
#endif

avt_alloc_attrib(2, 3) static inline void *avt_reallocarray(void *ptr,
                                                            size_t nmemb, size_t size)
{
    size_t nbytes;

    if (ckd_sub(&nbytes, nmemb, size))
        return NULL;

    return realloc(ptr, nbytes);
}

/* Generic macro for creating contexts which need to keep their addresses
 * if another context is created. */
#define FN_CREATING(prefix, ctx, type, shortname, array, num)                 \
    static inline type *prefix##_##create_##shortname(ctx *dctx)              \
    {                                                                         \
        type **array, *sctx = calloc(1, sizeof(*sctx));                       \
        if (!sctx)                                                            \
            return NULL;                                                      \
                                                                              \
        array = avt_reallocarray(dctx->array, dctx->num + 1, sizeof(type *)); \
        if (!array) {                                                         \
            free(sctx);                                                       \
            return NULL;                                                      \
        }                                                                     \
                                                                              \
        dctx->array = array;                                                  \
        dctx->array[dctx->num++] = sctx;                                      \
                                                                              \
        return sctx;                                                          \
    }

#endif /* AVTRANSPORT_MEM_H */
