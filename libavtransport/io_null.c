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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "connection_internal.h"

static int null_init(AVTContext *ctx, AVTIOCtx **io, AVTAddress *addr)
{
    *io = NULL;
    return 0;
}

static uint32_t null_max_pkt_len(AVTContext *ctx, AVTIOCtx *io)
{
    return UINT32_MAX;
}

static int null_input(AVTContext *ctx, AVTIOCtx *io, AVTBuffer *buf)
{
    return 0;
}

static int null_output(AVTContext *ctx, AVTIOCtx *io,
                       uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                       AVTBuffer *payload)
{
    return 0;
}

static int null_seek(AVTContext *ctx, AVTIOCtx *io,
                     uint64_t off, uint32_t seq,
                     int64_t ts, bool ts_is_dts)
{
    return 0;
}

static int null_close(AVTContext *ctx, AVTIOCtx **_io)
{
    return 0;
}

const AVTIO avt_io_null = {
    .name = "null",
    .type = AVT_IO_NULL,
    .init = null_init,
    .get_max_pkt_len = null_max_pkt_len,
    .read_input = null_input,
    .write_output = null_output,
    .seek = null_seek,
    .close = null_close,
};
