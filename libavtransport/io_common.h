/*
 * Copyright © 2023, Lynne
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

#ifndef AVTRANSPORT_IO_COMMON
#define AVTRANSPORT_IO_COMMON

#include "connection_internal.h"

enum AVTIOType {
    AVT_IO_NULL,
    AVT_IO_SOCKET,
    AVT_IO_CALLBACKS,
    AVT_IO_FILE,
    AVT_IO_FD,
};

/* Low level interface */
typedef struct AVTIOCtx AVTIOCtx;
typedef struct AVTIO {
    const char *name;
    enum AVTIOType type;

    int (*init)(AVTContext *ctx, AVTIOCtx **io,
                AVTAddress *addr);

    /* Attempt to add a secondary destination, NULL if unsupported */
    int (*add_dst)(AVTContext *ctx, AVTIOCtx *io, AVTAddress *addr);

    /* Removes a secondary destination, NULL if unsupported */
    int (*rm_dst)(AVTContext *ctx, AVTIOCtx *io, AVTAddress *addr);

    uint32_t (*get_max_pkt_len)(AVTContext *ctx, struct AVTIOCtx *io);

    /* Write. Returns positive offset on success, otherwise negative error */
    int64_t (*write_output)(AVTContext *ctx, AVTIOCtx *io,
                            uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                            AVTBuffer *payload);

    /* Read input from IO. May be called with a non-zero buffer, in which
     * case the data in the buffer will be reallocated to 'len', with the
     * start contents preserved.
     * Returns positive offset after reading on success, otherwise negative error. */
    int64_t (*read_input)(AVTContext *ctx, AVTIOCtx *io,
                          AVTBuffer **buf, size_t len);

    /* If only off is set, get the first packet at/after that offset.
     * If pts is set, get the stream data packet at/after the pts time
     * If seq is set, the get first packet at/after that seq */
    int64_t (*seek)(AVTContext *ctx, AVTIOCtx *io,
                    int64_t off, uint32_t seq,
                    int64_t ts, bool ts_is_dts);

    /* Flush data written */
    int (*flush)(AVTContext *ctx, AVTIOCtx *io);

    /* Close */
    int (*close)(AVTContext *ctx, AVTIOCtx **io);
} AVTIO;

/* Initialize an IO (protocols-use, mainly) */
int avt_io_init(AVTContext *ctx, const AVTIO **io, AVTIOCtx **io_ctx,
                AVTAddress *addr);

#endif /* AVTRANSPORT_IO_COMMON */
