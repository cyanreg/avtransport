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
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <limits.h>

#include "io_common.h"
#include "io_utils.h"
#include "io_socket_common.h"
#include "utils_internal.h"

#ifndef IOV_MAX
#warning "IOV_MAX not defined!"
#define IOV_MAX 0
#endif

struct AVTIOCtx {
    AVTSocketCommon sc;
    struct iovec *iov;

    bool listen;
    int fd;

    avt_pos rpos;
    avt_pos wpos;
    bool is_write;
};

static int unix_close(AVTIOCtx **_io)
{
    int ret;
    AVTIOCtx *io = *_io;

    close(io->fd);
    ret = avt_socket_close(io, &io->sc);

    free(io->iov);
    free(io);
    *_io = NULL;
    return ret;
}

static int unix_init(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    int ret;
    AVTIOCtx *io = calloc(1, sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    io->fd = -1;
    io->listen = addr->listen;

#if IOV_MAX > 4
    io->iov = calloc(IOV_MAX + 1, sizeof(*io->iov));
    if (!io->iov) {
        free(io);
        return AVT_ERROR(ENOMEM);
    }
#endif

    ret = avt_socket_open(io, &io->sc, addr);
    if (ret < 0) {
        free(io->iov);
        free(io);
        return ret;
    }

    if (!io->listen) {
        io->fd = dup(io->sc.socket);

        if ((fcntl(io->fd, F_SETFD, FD_CLOEXEC) == -1) ||
            (fcntl(io->fd, F_SETFD, O_NONBLOCK) == -1)) {
            ret = avt_handle_errno(io, "Error in fcntl(FD_CLOEXEC && O_NONBLOCK): %i %s\n");
            unix_close(&io);
            return ret;
        }
    }

    *_io = io;

    return 0;
}

static int unix_seek_to(AVTIOCtx *ctx, int64_t pos) { return 0; } /* unused */

static inline avt_pos unix_offset(AVTIOCtx *io)
{
    if (io->is_write)
        return io->wpos;
    else
        return io->rpos;
}

static inline avt_pos server_state_check(AVTIOCtx *io, bool *emulate)
{
    if (!io->listen)
        return 0;

    if (io->fd >= 0)
        return 0;

    io->fd = accept(io->sc.socket, NULL, NULL);
    if (io->fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            *emulate = true;
            return 0;
        }
        return errno;
    }

    if ((fcntl(io->fd, F_SETFD, FD_CLOEXEC) == -1) ||
        (fcntl(io->fd, F_SETFD, O_NONBLOCK) == -1)) {
        avt_handle_errno(io, "Error in fcntl(FD_CLOEXEC && O_NONBLOCK): %i %s\n");
        *emulate = true;
        close(io->fd);
        io->fd = -1;
        return 0;
    }

    return 0;
}

static inline avt_pos unix_read(AVTIOCtx *io, uint8_t *dst, size_t len,
                                int64_t timeout)
{
    avt_pos ret = server_state_check(io, NULL);
    if (ret < 0)
        return ret;

    ret = recv(io->fd, dst, len, 0);
    if (ret > 0)
        io->rpos += ret;

    return ret;
}

static inline avt_pos unix_write(AVTIOCtx *io, uint8_t *src, size_t len,
                                 int64_t timeout)
{
    bool emulate = false;
    avt_pos ret = server_state_check(io, &emulate);
    if (ret < 0)
        return ret;

    if (emulate)
        return (io->wpos += len);

    ret = write(io->fd, src, len);
    if (ret > 0)
        io->wpos += ret;

    return ret;
}

#if IOV_MAX > 4
static avt_pos unix_write_vec_native(AVTIOCtx *io, AVTPktd *pkt, uint32_t nb_pkt,
                                     int64_t timeout)
{
    avt_pos ret;
    avt_pos off = io->wpos;
    int nb_iov = 0;

    bool emulate = false;
    ret = server_state_check(io, &emulate);
    if (ret < 0)
        return ret;

    if (emulate) {
        for (auto i = 0; i < nb_pkt; i++)
            off += pkt[i].hdr_len + avt_buffer_get_data_len(&pkt[i].pl);

        AVT_SWAP(io->wpos, off);
        return off;
    }

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
        if (ret < 0)
            return avt_handle_errno(io, "Error flushing: %i %s\n");

        off += ret;
    }

    AVT_SWAP(io->wpos, off);
    return off;
}
#endif

#define RENAME(x) unix_ ## x

#include "io_template.c"

const AVTIO avt_io_unix = {
    .name = "unix",
    .type = AVT_IO_UNIX,
    .init = unix_init,
    .get_max_pkt_len = unix_max_pkt_len,
    .read_input = unix_read_input,
#if IOV_MAX > 4
    .write_vec = unix_write_vec_native,
#else
    .write_vec = unix_write_vec,
#endif
    .write_pkt = unix_write_pkt,
    .rewrite = NULL,
    .seek = NULL,
    .flush = NULL,
    .close = unix_close,
};
