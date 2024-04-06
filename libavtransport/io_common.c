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

#include <avtransport/avtransport.h>
#include "io_common.h"
#include "attributes.h"

extern const AVTIO avt_io_null;
extern const AVTIO avt_io_dcb;

extern const AVTIO avt_io_file;
#ifndef _WIN32
extern const AVTIO avt_io_fd;
extern const AVTIO avt_io_fd_path;
extern const AVTIO avt_io_mmap;
extern const AVTIO avt_io_mmap_path;
#endif

extern const AVTIO avt_io_udp;
extern const AVTIO avt_io_udp_lite;

extern const AVTIO avt_io_unix;

#define MAX_NB_BACKENDS 4

/* In order of preference */
static const AVTIO *avt_io_list[AVT_IO_INVALID][MAX_NB_BACKENDS + 1] = {
    [AVT_IO_NULL] = {
        &avt_io_null,
    },
    [AVT_IO_CALLBACK] = {
        &avt_io_dcb,
    },
    [AVT_IO_FILE] = {
#ifndef _WIN32
        &avt_io_mmap_path,
        &avt_io_fd_path,
#endif
        &avt_io_file,
    },
    [AVT_IO_FD] = {
#ifndef _WIN32
        &avt_io_mmap,
        &avt_io_fd,
#endif
    },
    [AVT_IO_UDP] = {
        &avt_io_udp,
    },
    [AVT_IO_UNIX] = {
        &avt_io_unix,
    },
    [AVT_IO_UDP_LITE] = {
        &avt_io_udp_lite,
    },
};

static inline enum AVTIOType map_addr_to_io(AVTAddress *addr)
{
    switch (addr->type) {
    case AVT_ADDRESS_NULL:
        return AVT_IO_NULL;
    case AVT_ADDRESS_FILE:
        return AVT_IO_FILE;
    case AVT_ADDRESS_FD:
        return AVT_IO_FD;
    case AVT_ADDRESS_CALLBACK:
        return AVT_IO_CALLBACK;
    case AVT_ADDRESS_UNIX:
        return AVT_IO_UNIX;
    case AVT_ADDRESS_URL:
        [[fallthrough]];
    case AVT_ADDRESS_SOCKET:
        if (addr->proto == AVT_PROTOCOL_UDP || addr->proto == AVT_PROTOCOL_QUIC)
            return AVT_IO_UDP;
        else if (addr->proto == AVT_PROTOCOL_UDP_LITE)
            return AVT_IO_UDP_LITE;
        [[fallthrough]];
    default:
        return AVT_IO_INVALID;
    }
}

/* For protocols to call */
COLD int avt_io_init(AVTContext *ctx, const AVTIO **_io, AVTIOCtx **io_ctx,
                     AVTAddress *addr)
{
    enum AVTIOType io_type = map_addr_to_io(addr);
    if (io_type == AVT_IO_INVALID)
        return AVT_ERROR(EINVAL);

    int err;
    const AVTIO *io, **io_list = avt_io_list[io_type];
    while ((io = *io_list)) {
        err = io->init(ctx, io_ctx, addr);
        if (err == AVT_ERROR(ENOMEM)) {
            return err;
        } else if (err < 0) {
            avt_log(ctx, AVT_LOG_TRACE, "Unable to open with I/O \"%s\": %i\n",
                    io->name, err);
            continue;
        }
        avt_log(ctx, AVT_LOG_VERBOSE, "Using I/O \"%s\"\n", io->name);
        *_io = io;
        return err;
    }

    return AVT_ERROR(EINVAL);
}
