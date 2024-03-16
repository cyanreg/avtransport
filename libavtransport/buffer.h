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

#include <avtransport/utils.h>

struct AVTBuffer {
    uint8_t *data;      /* Current ref's view of the buffer */
    size_t len;         /* Current ref's size of the view of the buffer */

    uint8_t *base_data; /* Buffer's actual start data */
    uint8_t *end_data;  /* Buffer's end of data */

    avt_free_cb free;
    void *opaque;
    atomic_int *refcnt;
};

void avt_buffer_update(AVTBuffer *buf, void *data, size_t len);
int avt_buffer_resize(AVTBuffer *buf, size_t len);

#define AVT_BUFFER_REF_ALL (0)
int avt_buffer_quick_ref(AVTBuffer *dst, AVTBuffer *buffer,
                         ptrdiff_t offset, size_t len);

void avt_buffer_quick_unref(AVTBuffer *buf);

int avt_buffer_offset(AVTBuffer *buf, ptrdiff_t offset);

#endif
