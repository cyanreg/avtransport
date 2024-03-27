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

#define _GNU_SOURCE // SO_BINDTODEVICE + ipv6_mtuinfo
#define _XOPEN_SOURCE 700 // pwrite, IOV_MAX

#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/udp.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>

/* Fallback */
#if __has_include(<net/udplite.h>)
#include <net/udplite.h>
#else
#ifndef IPPROTO_UDPLITE
#define IPPROTO_UDPLITE     136
#endif
#ifndef UDPLITE_SEND_CSCOV
#define UDPLITE_SEND_CSCOV  10
#endif
#ifndef UDPLITE_RECV_CSCOV
#define UDPLITE_RECV_CSCOV  11
#endif
#endif

#if __has_include(<linux/net_tstamp.h>)
#include <linux/net_tstamp.h>
#endif

#include <ifaddrs.h>

#include "io_socket_common.h"
#include "io_utils.h"
#include "attributes.h"

int avt_socket_get_opt(void *log_ctx, int socket,
                       int lvl, int opt, void *data, int len,
                       const char *errmsg)
{
    int ret_len = len;
    int ret = getsockopt(socket, lvl, opt, data, &ret_len);
    if (ret < 0) {
        ret = avt_handle_errno(log_ctx, errmsg);
        return ret;
    }
    return 0;
}

int64_t avt_socket_get_mtu(void *log_ctx, AVTSocketCommon *sc)
{
    int64_t ret = 1280;

    if (sc->addr_size != sizeof(sc->ip.local_addr))
        return AVT_ERROR(EINVAL);

#ifdef IPV6_RECVPATHMTU
    struct ip6_mtuinfo mtu6;

    memcpy(&mtu6.ip6m_addr, sc->remote_addr, sc->addr_size);
    ret = avt_socket_get_opt(log_ctx, sc->socket, IPPROTO_IPV6, IPV6_PATHMTU,
                             &mtu6, sizeof(mtu6),
                             "Unable to get MTU: %i %s\n");
    if (ret >= 0)
        ret = mtu6.ip6m_mtu;
#endif

    return ret;
}

static int setup_unix_socket(void *log_ctx, AVTSocketCommon *sc, AVTAddress *addr)
{
    int ret;

    if ((fcntl(sc->socket, F_SETFD, FD_CLOEXEC) == -1) ||
        (fcntl(sc->socket, F_SETFD, O_NONBLOCK) == -1))
        return avt_handle_errno(log_ctx, "Error in fcntl(FD_CLOEXEC && O_NONBLOCK): %i %s\n");

    /* Set receive/send buffer */
    if (addr->opts.rx_buf)
        SET_SOCKET_OPT(log_ctx, sc->socket, SOL_SOCKET, SO_RCVBUF, (int)addr->opts.rx_buf);
    if (addr->opts.tx_buf)
        SET_SOCKET_OPT(log_ctx, sc->socket, SOL_SOCKET, SO_SNDBUF, (int)addr->opts.tx_buf);

    /* Setup addresses */
    sc->local_addr = (struct sockaddr *)&sc->un.local_addr;
    sc->remote_addr = (struct sockaddr *)&sc->un.remote_addr;
    sc->addr_size = sizeof(sc->un.local_addr);

    memset(sc->local_addr, 0, sc->addr_size);
    memset(sc->remote_addr, 0, sc->addr_size);

    sc->un.local_addr.sun_family = AF_UNIX;
    sc->un.remote_addr.sun_family = AF_UNIX;

    int path_size = sizeof(sc->un.local_addr.sun_path);
    int len = strlen(addr->path);
    if (len >= path_size) {
        avt_log(log_ctx, AVT_LOG_ERROR, "Path too long: %s\n", addr->path);
        return AVT_ERROR(EINVAL);
    }

    if (addr->listen) {
        strncpy(sc->un.local_addr.sun_path, addr->path, path_size - 1);
        sc->un.local_addr.sun_path[path_size - 1] = 0;
        unlink(sc->un.local_addr.sun_path);
    }

    if (!addr->listen) {
        strncpy(sc->un.remote_addr.sun_path, addr->path, path_size - 1);
        sc->un.remote_addr.sun_path[path_size - 1] = 0;
    }

    return 0;
}

static int setup_ip_socket(void *log_ctx, AVTSocketCommon *sc, AVTAddress *addr,
                           int proto)
{
    int ret;
    bool mcast = IN6_IS_ADDR_MULTICAST(addr->ip);

    if (fcntl(sc->socket, F_SETFD, FD_CLOEXEC) == -1)
        return avt_handle_errno(log_ctx, "Error in fcntl(FD_CLOEXEC): %i %s\n");

    if (addr->interface && !mcast) {
#ifdef SO_BINDTODEVICE
        SET_SOCKET_OPT(log_ctx, sc->socket, SOL_SOCKET, SO_BINDTODEVICE, addr->interface);
#else
        avt_log(log_ctx, AVT_LOG_ERROR, "Unable to bind socket to interface, not supported!\n");
        return AVT_ERROR(EOPNOTSUPP);
#endif
    }

    /* Set receive/send buffer */
    if (addr->opts.rx_buf)
        SET_SOCKET_OPT(log_ctx, sc->socket, SOL_SOCKET, SO_RCVBUF, (int)addr->opts.rx_buf);
    if (addr->opts.tx_buf)
        SET_SOCKET_OPT(log_ctx, sc->socket, SOL_SOCKET, SO_SNDBUF, (int)addr->opts.tx_buf);

#ifdef SOF_TIMESTAMPING_TX_RECORD_MASK
    {
        int ts_flags = SOF_TIMESTAMPING_RX_SOFTWARE |
                       SOF_TIMESTAMPING_SOFTWARE;
        SET_SOCKET_OPT(log_ctx, sc->socket, SOL_SOCKET, SO_TIMESTAMPING, (int)ts_flags);
    }
#endif

#ifdef SO_RXQ_OVFL
    /* Turn on dropped packet counter */
    SET_SOCKET_OPT(log_ctx, sc->socket, SOL_SOCKET, SO_RXQ_OVFL, (int)0);
#endif

    /* Turn CORK, SEGMENT and GRO very very off */
#ifdef UDP_CORK
    SET_SOCKET_OPT(log_ctx, sc->socket, proto, UDP_CORK, (int)0);
#endif
#ifdef UDP_SEGMENT
    SET_SOCKET_OPT(log_ctx, sc->socket, proto, UDP_SEGMENT, (int)0);
#endif
#ifdef UDP_GRO
    SET_SOCKET_OPT(log_ctx, sc->socket, proto, UDP_GRO, (int)0);
#endif

    /* Adjust UDP-Lite checksum coverage */
    if (proto == IPPROTO_UDPLITE) {
        /* Minimum valid transmit value */
        SET_SOCKET_OPT(log_ctx, sc->socket, IPPROTO_UDPLITE, UDPLITE_SEND_CSCOV, (int)8);

        /* Setup receive coverage too */
        SET_SOCKET_OPT(log_ctx, sc->socket, IPPROTO_UDPLITE, UDPLITE_RECV_CSCOV, (int)8);
    }

    /* Disable IPv6 only */
    SET_SOCKET_OPT(log_ctx, sc->socket, IPPROTO_IPV6, IPV6_V6ONLY, (int)0);

    /* Disable fragmentation */
    SET_SOCKET_OPT(log_ctx, sc->socket, IPPROTO_IPV6, IPV6_DONTFRAG, (int)1);

#ifdef IPV6_RECVPATHMTU
    /* Enable MTU monitoring */
    SET_SOCKET_OPT(log_ctx, sc->socket, IPPROTO_IPV6, IPV6_RECVPATHMTU, (int)1);
#endif

#ifdef IPV6_FREEBIND
    /* Enable free binding when no interface is given.
     * This lets us not have to deal with dynamic reconfiguration, new
     * devices being added, and so on */
    if (addr->listen && !addr->interface)
        SET_SOCKET_OPT(log_ctx, sc->socket, IPPROTO_IPV6, IPV6_FREEBIND, (int)1);
#endif

    /* Set multicast interface */
    if (addr->interface && mcast)
        SET_SOCKET_OPT(log_ctx, sc->socket, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                       (uint32_t)addr->interface_idx);

    /* Setup addresses */
    sc->local_addr = (struct sockaddr *)&sc->ip.local_addr;
    sc->remote_addr = (struct sockaddr *)&sc->ip.remote_addr;
    sc->addr_size = sizeof(sc->ip.local_addr);

    memset(sc->local_addr, 0, sc->addr_size);
    memset(sc->remote_addr, 0, sc->addr_size);

    /* Setup binding */
    sc->ip.local_addr.sin6_family = AF_INET6;
    sc->ip.local_addr.sin6_port = addr->port;
    if (addr->listen) {
        memcpy(sc->ip.local_addr.sin6_addr.s6_addr, addr->ip, 16);
        sc->ip.local_addr.sin6_scope_id = addr->scope;
    }

    /* Setup connecting */
    sc->ip.remote_addr.sin6_family = AF_INET6;
    sc->ip.remote_addr.sin6_port = addr->port;
    if (!addr->listen) {
        memcpy(sc->ip.remote_addr.sin6_addr.s6_addr, addr->ip, 16);
        sc->ip.remote_addr.sin6_scope_id = addr->scope;
    }

    return 0;
}

COLD int avt_socket_open(void *log_ctx, AVTSocketCommon *sc, AVTAddress *addr)
{
    int ret;

    /* Import fd into our context */
    if (addr->type == AVT_ADDRESS_UNIX && (addr->fd >= 0)) {
        sc->socket = dup(addr->fd);
        if (sc->socket == -1)
            return avt_handle_errno(log_ctx, "Failed to dup() socket: %i %s\n");

        if ((fcntl(sc->socket, F_SETFD, FD_CLOEXEC) == -1) ||
            (fcntl(sc->socket, F_SETFD, O_NONBLOCK) == -1)) {
            ret = avt_handle_errno(log_ctx, "Error in fcntl(FD_CLOEXEC && O_NONBLOCK): %i %s\n");
            close(sc->socket);
        }

        return 0;
    } else if (addr->type == AVT_ADDRESS_SOCKET) {
        if (addr->fd == -1) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Socket not given\n");
            return AVT_ERROR(EINVAL);
        }

        sc->socket = dup(addr->fd);
        if (sc->socket == -1)
            return avt_handle_errno(log_ctx, "Failed to dup() socket: %i %s\n");

        if (fcntl(sc->socket, F_SETFD, FD_CLOEXEC) == -1) {
            ret = avt_handle_errno(log_ctx, "Error in fcntl(FD_CLOEXEC): %i %s\n");
            close(sc->socket);
            return ret;
        }

        /* Setup addresses */
        sc->local_addr = (struct sockaddr *)&sc->ip.local_addr;
        sc->remote_addr = (struct sockaddr *)&sc->ip.remote_addr;
        sc->addr_size = sizeof(sc->ip.local_addr);

        memset(sc->local_addr, 0, sc->addr_size);
        memset(sc->remote_addr, 0, sc->addr_size);

        /* Setup binding */
        sc->ip.local_addr.sin6_family = AF_INET6;
        sc->ip.local_addr.sin6_port = addr->port;
        if (addr->listen) {
            memcpy(sc->ip.local_addr.sin6_addr.s6_addr, addr->ip, 16);
            sc->ip.local_addr.sin6_scope_id = addr->scope;
        }

        /* Setup connecting */
        sc->ip.remote_addr.sin6_family = AF_INET6;
        sc->ip.remote_addr.sin6_port = addr->port;
        if (!addr->listen) {
            memcpy(sc->ip.remote_addr.sin6_addr.s6_addr, addr->ip, 16);
            sc->ip.remote_addr.sin6_scope_id = addr->scope;
        }

        return 0;
    }

    int domain = addr->type == AVT_ADDRESS_UNIX ? AF_UNIX : AF_INET6;
    int type   = addr->type == AVT_ADDRESS_UNIX ? SOCK_STREAM : SOCK_DGRAM;
    int proto  = addr->proto == AVT_PROTOCOL_UDP_LITE ? IPPROTO_UDPLITE :
                 addr->proto == AVT_PROTOCOL_UDP      ? IPPROTO_UDP :
                                                        0;

    if ((sc->socket = socket(domain, type, proto)) < 0) {
        ret = avt_handle_errno(log_ctx, "Failed to open socket: %i %s\n");
        sc->socket = -1;
        return ret;
    }

    if (addr->type == AVT_ADDRESS_UNIX)
        ret = setup_unix_socket(log_ctx, sc, addr);
    else
        ret = setup_ip_socket(log_ctx, sc, addr, proto);
    if (ret < 0) {
        close(sc->socket);
        return ret;
    }

    if (addr->listen) {
        if (bind(sc->socket, sc->local_addr, sc->addr_size) < 0) {
            ret = avt_handle_errno(log_ctx, "Unable to bind socket: %i %s\n");
            goto fail;
        }

        /* For UNIX sockets, listen */
        if (type == SOCK_STREAM && (listen(sc->socket, 0) < 0)) {
            ret = avt_handle_errno(log_ctx, "Unable to listen to socket: %i %s\n");
            goto fail;
        }
    }

    if (!addr->listen) {
        if (connect(sc->socket, sc->remote_addr, sc->addr_size) < 0) {
            ret = avt_handle_errno(log_ctx, "Unable to connect socket: %i %s\n");
            goto fail;
        }
    }

    return 0;

fail:
    close(sc->socket);
    sc->socket = -1;
    return ret;
}

COLD int avt_socket_close(void *log_ctx, AVTSocketCommon *sc)
{
    int ret = 0;

    if (sc->socket >= 0) {
        ret = close(sc->socket);
        if (ret < 0)
            ret = avt_handle_errno(log_ctx, "Error closing socket: %i %s\n");
    }

    if ((sc->addr_size == sizeof(sc->un.local_addr) &&
        (strlen(sc->un.local_addr.sun_path))))
        unlink(sc->un.local_addr.sun_path);

    return ret;
}
