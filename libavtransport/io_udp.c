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

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdckdint.h>

#include <unistd.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>

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

    int64_t wpos;
    int64_t rpos;
};

static int udp_init(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    int ret;
    AVTIOCtx *io = calloc(1, sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    io->iov = calloc(IOV_MAX + 1, sizeof(*io->iov));
    if (!io->iov) {
        free(io);
        return AVT_ERROR(ENOMEM);
    }

    ret = avt_socket_open(io, &io->sc, addr);
    if (ret < 0) {
        free(io->iov);
        free(io);
        return ret;
    }

    *_io = io;

    return 0;
}

static uint32_t udp_max_pkt_len(AVTContext *ctx, AVTIOCtx *io)
{
    return avt_socket_get_mtu(io, &io->sc);
}

static int64_t udp_write_pkt(AVTContext *ctx, AVTIOCtx *io, AVTPktd *p,
                             int64_t timeout)
{
    int64_t ret;

    size_t pl_len;
    uint8_t *pl_data = avt_buffer_get_data(&p->pl, &pl_len);

    struct iovec vdata[2] = {
        { .iov_base = p->hdr, .iov_len = p->hdr_len },
        { .iov_base = pl_data, .iov_len = pl_len },
    };

    struct msghdr pm = {
        .msg_name = io->sc.remote_addr,
        .msg_namelen = io->sc.addr_size,
        .msg_iov = vdata,
        .msg_iovlen = 1 + !!pl_len,
        .msg_control = NULL,
        .msg_flags = 0,
    };

    ret = sendmsg(io->sc.socket, &pm, !timeout ? MSG_DONTWAIT : 0);
    if (ret < 0)
        return avt_handle_errno(io, "Unable to send message: %i %s");

    return (io->wpos += ret);
}

static int64_t udp_write_vec(AVTContext *ctx, AVTIOCtx *io,
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

        struct msghdr pm = {
            .msg_name = io->sc.remote_addr,
            .msg_namelen = io->sc.addr_size,
            .msg_iov = io->iov,
            .msg_iovlen = nb_iov,
            .msg_control = NULL,
            .msg_flags = 0,
        };

        ret = sendmsg(io->sc.socket, &pm, !timeout ? MSG_DONTWAIT : 0);
        if (ret < 0)
            return avt_handle_errno(io, "Unable to send message: %i %s");

        io->wpos += ret;
    }

    return io->wpos;
}

static int64_t udp_read_input(AVTContext *ctx, AVTIOCtx *io,
                              AVTBuffer **_buf, size_t len, int64_t timeout)
{
    int ret;
    [[maybe_unused]] int err;

    uint8_t *data;
    size_t buf_len, off = 0;
    AVTBuffer *buf = *_buf;

    if (!buf) {
        buf = avt_buffer_alloc(len);
        if (!buf)
            return AVT_ERROR(ENOMEM);

        *_buf = buf;
    } else {
        off = avt_buffer_get_data_len(buf);

        if (ckd_add(&buf_len, off, len))
            return AVT_ERROR(EINVAL);

        ret = avt_buffer_resize(buf, buf_len);
        if (ret < 0)
            return ret;
    }

    /* Read data */
    data = avt_buffer_get_data(buf, &buf_len);

    struct sockaddr_in6 remote_addr = { };
    socklen_t remote_addr_len = sizeof(remote_addr);

    ret = recvfrom(io->sc.socket, data + off, len,
                   !timeout ? MSG_DONTWAIT : 0,
                   (struct sockaddr *)&remote_addr, &remote_addr_len);
    if (ret < 0)
        return avt_handle_errno(io, "Unable to receive message: %s");

    /* Adjust new size in case of underreads */
    err = avt_buffer_resize(buf, off + len);
    avt_assert2(err >= 0);

    return (io->rpos += ret);
}

static int udp_close(AVTContext *ctx, AVTIOCtx **_io)
{
    AVTIOCtx *io = *_io;
    int ret = avt_socket_close(io, &io->sc);
    free(io->iov);
    free(io);
    *_io = NULL;
    return ret;
}

const AVTIO avt_io_udp = {
    .name = "udp",
    .type = AVT_IO_UDP,
    .init = udp_init,
    .get_max_pkt_len = udp_max_pkt_len,
    .read_input = udp_read_input,
    .write_vec = udp_write_vec,
    .write_pkt = udp_write_pkt,
    .rewrite = NULL,
    .seek = NULL,
    .flush = NULL,
    .close = udp_close,
};

const AVTIO avt_io_udp_lite = {
    .name = "udp_lite",
    .type = AVT_IO_UDP_LITE,
    .init = udp_init,
    .get_max_pkt_len = udp_max_pkt_len,
    .read_input = udp_read_input,
    .write_vec = udp_write_vec,
    .write_pkt = udp_write_pkt,
    .rewrite = NULL,
    .seek = NULL,
    .flush = NULL,
    .close = udp_close,
};
