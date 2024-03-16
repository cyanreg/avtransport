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

#define _XOPEN_SOURCE 700 // pwrite, IOV_MAX

#include "os_compat.h"

#include <stdio.h>
#include <string.h>
#include <uchar.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <limits.h>

#include "io_common.h"
#include "utils_internal.h"

#ifndef IOV_MAX
#warning "IOV_MAX not defined!"
#define IOV_MAX 0
#endif

struct AVTIOCtx {
    int fd;
    struct iovec *iov;

    off_t rpos;
    off_t wpos;
    bool is_write;
};

static int fd_handle_error(AVTIOCtx *io, const char *msg)
{
    char8_t err_info[256];
    strerror_safe(err_info, sizeof(err_info), errno);
    avt_log(io, AVT_LOG_ERROR, msg, err_info);
    return AVT_ERROR(errno);
}

static int fd_init(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    AVTIOCtx *io = calloc(1, sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    io->iov = calloc(IOV_MAX + 1, sizeof(*io->iov));
    if (!io->iov) {
        free(io);
        return AVT_ERROR(ENOMEM);
    }

    io->fd = dup(addr->fd);
    if (io->fd < 0) {
        int ret = fd_handle_error(io, "Error duplicating fd: %s\n");
        free(io->iov);
        free(io);
        return ret;
    }

    *_io = io;

    return 0;
}

static int fd_init_path(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    int ret;
    AVTIOCtx *io = calloc(1, sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    io->iov = calloc(IOV_MAX, sizeof(*io->iov));
    if (io->iov < 0) {
        free(io);
        return AVT_ERROR(ENOMEM);
    }

    io->fd = open(addr->path, O_CREAT | O_RDWR, 0666);
    if (!io->fd) {
        ret = fd_handle_error(io, "Error opening: %s\n");
        free(io->iov);
        free(io);
        return ret;
    }

    *_io = io;

    return 0;
}

static int fd_close(AVTContext *ctx, AVTIOCtx **_io)
{
    AVTIOCtx *io = *_io;

    int ret = close(io->fd);
    if (ret)
        ret = fd_handle_error(io, "Error closing: %s\n");

    free(io->iov);
    free(io);
    *_io = NULL;

    return ret;
}

static inline int fd_seek_to(AVTIOCtx *io, int64_t pos)
{
    return lseek(io->fd, pos, SEEK_SET);
}

static inline size_t fd_read(AVTIOCtx *io, uint8_t *dst, size_t len,
                             int64_t timeout)
{
    return read(io->fd, dst, len);
}

static inline size_t fd_write(AVTIOCtx *io, uint8_t *src, size_t len,
                              int64_t timeout)
{
    return write(io->fd, src, len);
}

static inline int64_t fd_offset(AVTIOCtx *io)
{
    return lseek(io->fd, 0, SEEK_CUR);
}

static int fd_flush(AVTContext *ctx, AVTIOCtx *io, int64_t timeout)
{
    int ret = fsync(io->fd);
    if (ret)
        ret = fd_handle_error(io, "Error flushing: %s\n");

    return ret;
}

#if IOV_MAX > 4
static int64_t fd_write_vec_native(AVTContext *ctx, AVTIOCtx *io,
                                   AVTPktd *pkt, uint32_t nb_pkt, int64_t timeout)
{
    int64_t ret;
    int nb_iov = 0;

    while (nb_pkt) {
        nb_iov = 0;
        do {
            io->iov[nb_iov + 0].iov_base = pkt->hdr;
            io->iov[nb_iov + 0].iov_len  = pkt->hdr_len;
            io->iov[nb_iov + 1].iov_base = avt_buffer_get_data(&pkt->pl,
                                                               &io->iov[nb_iov + 1].iov_len);
            nb_iov += 1 + !!io->iov[nb_iov + 1].iov_base;

            pkt++;
            nb_pkt--;
        } while (nb_pkt && ((nb_iov + 2) > IOV_MAX));
        ret = writev(io->fd, io->iov, nb_iov);
        if (ret < 0) {
            ret = fd_handle_error(io, "Error flushing: %s\n");
            io->wpos = fd_offset(io);
            return ret;
        }
    }

    return (io->wpos = fd_offset(io));
}
#endif

static int64_t fd_rewrite_native(AVTContext *ctx, AVTIOCtx *io,
                                 AVTPktd *p, int64_t off, int64_t timeout)
{
    int64_t ret;
    ret = pwrite(io->fd, p->hdr, p->hdr_len, off);
    if (ret < 0) {
        ret = fd_handle_error(io, "Error writing: %s\n");
        io->wpos = fd_offset(io);
        return ret;
    }

    size_t pl_len;
    uint8_t *data = avt_buffer_get_data(&p->pl, &pl_len);
    if (data) {
        ret = pwrite(io->fd, data, pl_len, off);
        if (ret < 0) {
            ret = fd_handle_error(io, "Error writing: %s\n");
            io->wpos = fd_offset(io);
            return ret;
        }
    }

    return off + p->hdr_len + pl_len;
}

#define RENAME(x) fd_ ## x

#include "io_file_template.c"

const AVTIO avt_io_fd = {
    .name = "fd",
    .type = AVT_IO_FD,
    .init = fd_init,
    .get_max_pkt_len = fd_max_pkt_len,
    .read_input = fd_read_input,
#if IOV_MAX > 4
    .write_vec = fd_write_vec_native,
#else
    .write_vec = fd_write_vec,
#endif
    .write_pkt = fd_write_pkt,
    .rewrite = fd_rewrite,
    .seek = fd_seek,
    .flush = fd_flush,
    .close = fd_close,
};

const AVTIO avt_io_fd_path = {
    .name = "fd_path",
    .type = AVT_IO_FILE,
    .init = fd_init_path,
    .get_max_pkt_len = fd_max_pkt_len,
    .read_input = fd_read_input,
#if IOV_MAX > 4
    .write_vec = fd_write_vec_native,
#else
    .write_vec = fd_write_vec,
#endif
    .write_pkt = fd_write_pkt,
    .rewrite = fd_rewrite_native,
    .seek = fd_seek,
    .flush = fd_flush,
    .close = fd_close,
};
