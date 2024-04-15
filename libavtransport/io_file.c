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

#include "os_compat.h"

#include <stdio.h>
#include <string.h>
#include <uchar.h>

#include "io_common.h"
#include "io_utils.h"
#include "utils_internal.h"

struct AVTIOCtx {
    FILE *f;
    avt_pos rpos;
    avt_pos wpos;
    bool is_write;
};

static int file_handle_error(AVTIOCtx *io, const char *msg)
{
    char8_t err_info[256];
    strerror_safe(err_info, sizeof(err_info), errno);
    avt_log(io, AVT_LOG_ERROR, msg, err_info);
    return AVT_ERROR(errno);
}

static COLD int file_close(AVTIOCtx **_io)
{
    AVTIOCtx *io = *_io;
    int ret = fclose(io->f);
    if (ret)
        ret = file_handle_error(io, "Error closing: %s\n");

    free(io);
    *_io = NULL;

    return ret;
}

static COLD int file_init(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    int ret;
    AVTIOCtx *io = calloc(1, sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    io->f = fopen(addr->path, "w+");
    if (!io->f) {
        ret = file_handle_error(io, "Error opening: %s\n");
        free(io);
        return ret;
    }

    *_io = io;

    return 0;
}

static inline avt_pos file_seek_to(AVTIOCtx *io, avt_pos pos)
{
    return fseeko(io->f, pos, SEEK_SET);
}

static inline avt_pos file_read(AVTIOCtx *io, uint8_t *dst, size_t len,
                                int64_t timeout)
{
    return fread(dst, 1, len, io->f);
}

static inline avt_pos file_write(AVTIOCtx *io, uint8_t *src, size_t len,
                                 int64_t timeout)
{
    return fwrite(src, 1, len, io->f);
}

static inline avt_pos file_offset(AVTIOCtx *io)
{
    return ftello(io->f);
}

static int file_flush(AVTIOCtx *io, int64_t timeout)
{
    int ret = fflush(io->f);
    if (ret)
        ret = file_handle_error(io, "Error flushing: %s\n");

    return ret;
}

#define RENAME(x) file_ ## x

#include "io_template.c"

const AVTIO avt_io_file = {
    .name = "file",
    .type = AVT_IO_FILE,
    .init = file_init,
    .get_max_pkt_len = file_max_pkt_len,
    .read_input = file_read_input,
    .write_vec = file_write_vec,
    .write_pkt = file_write_pkt,
    .rewrite = file_rewrite,
    .seek = file_seek,
    .flush = file_flush,
    .close = file_close,
};
