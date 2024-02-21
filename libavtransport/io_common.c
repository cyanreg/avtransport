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

#include "io_common.h"

extern const AVTIO avt_io_null;

extern const AVTIO avt_io_file;

#ifndef _WIN32
extern const AVTIO avt_io_fd;
extern const AVTIO avt_io_fd_path;
#endif

#define MAX_NB_BACKENDS 4

/* In order of preference */
static const AVTIO *avt_io_list[][MAX_NB_BACKENDS] = {
    [AVT_IO_NULL] = {
        &avt_io_null,
    },
    [AVT_IO_FILE] = {
#ifndef _WIN32
        &avt_io_fd_path,
#endif
        &avt_io_file,
    },
    [AVT_IO_FD] = {
#ifndef _WIN32
        &avt_io_fd,
#endif
    },
};

/* For protocols to call */
int avt_io_init(AVTContext *ctx, const AVTIO **_io, AVTIOCtx **io_ctx,
                AVTAddress *addr)
{
    const AVTIO *io = avt_io_list[AVT_IO_FILE][0];
    int err = io->init(ctx, io_ctx, addr);
    *_io = io;
    return err;
}
