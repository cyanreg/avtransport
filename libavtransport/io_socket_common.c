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

#define _GNU_SOURCE // SO_BINDTODEVICE
#define _XOPEN_SOURCE 700 // pwrite, IOV_MAX

#include "os_compat.h"

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <netinet/udp.h>

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

#include <ifaddrs.h>

#include "io_socket_common.h"
#include "utils_internal.h"

int avt_handle_errno(void *log_ctx, const char *msg)
{
    char8_t err_info[256];
    avt_log(log_ctx, AVT_LOG_ERROR, msg, errno,
            strerror_safe(err_info, sizeof(err_info), errno));
    return AVT_ERROR(errno);
}

static int get_socket_opt(void *log_ctx, AVTSocketCommon *sc,
                          int lvl, int opt, void *data, int len,
                          const char *errmsg)
{
    int ret_len = len;
    int ret = getsockopt(sc->socket, lvl, opt, data, &ret_len);
    if (ret < 0) {
        ret = avt_handle_errno(log_ctx, errmsg);
        return ret;
    }
    return 0;
}

#define SET_SOCKET_OPT(log_ctx, socket_common, lvl, opt, val)                  \
    do {                                                                       \
        [[maybe_unused]] auto tempval = val;                                   \
        ret = setsockopt(socket_common->socket, lvl, opt,                      \
                         _Generic(val,                                         \
                             int: &(tempval),                                  \
                             default: val),                                    \
                         sizeof(val));                                         \
        if (ret < 0) {                                                         \
            ret = avt_handle_errno(log_ctx,                                    \
                                   "setsockopt(" #lvl ", " #opt ") "           \
                                   "failed: %i %s\n");                         \
            return ret;                                                        \
        }                                                                      \
    } while (0)


int avt_socket_open(void *log_ctx, AVTSocketCommon *sc, AVTAddress *addr)
{
    int ret;
    int proto = addr->proto == AVT_PROTOCOL_UDP_LITE ? IPPROTO_UDPLITE : IPPROTO_UDP;

	if ((sc->socket = socket(AF_INET6, SOCK_DGRAM, proto)) < 0) {
        ret = avt_handle_errno(log_ctx, "Failed to open socket: %i %s\n");
        sc->socket = -1;
        return ret;
    }

    if (addr->interface) {
        memcpy(sc->ifr.ifr_name, addr->interface,
               AVT_MIN(sizeof(sc->ifr.ifr_name) - 1, strlen(addr->interface)));
#ifdef SO_BINDTODEVICE
        SET_SOCKET_OPT(log_ctx, sc, SOL_SOCKET, SO_BINDTODEVICE, sc->ifr.ifr_name);
#else
        avt_log(log_ctx, AVT_LOG_ERROR, "Unable to bind socket to interface, not supported!\n");
        ret = AVT_ERROR(EOPNOTSUPP);
        goto fail;
#endif
    }

    /* Turn on address sharing */
//    SET_SOCKET_OPT(log_ctx, sc, SOL_SOCKET, SO_REUSEADDR, (int)1);

#ifdef SO_RXQ_OVFL
    /* Turn on dropped packet counter */
    SET_SOCKET_OPT(log_ctx, sc, SOL_SOCKET, SO_RXQ_OVFL, (int)0);
#endif

    /* Turn CORK, SEGMENT and GRO very very off */
#ifdef UDP_CORK
    SET_SOCKET_OPT(log_ctx, sc, proto, UDP_CORK, (int)0);
#endif
#ifdef UDP_SEGMENT
    SET_SOCKET_OPT(log_ctx, sc, proto, UDP_SEGMENT, (int)0);
#endif
#ifdef UDP_GRO
    SET_SOCKET_OPT(log_ctx, sc, proto, UDP_GRO, (int)0);
#endif

    /* Adjust UDP-Lite checksum coverage */
    if (proto == IPPROTO_UDPLITE) {
        /* Minimum valid transmit value */
        SET_SOCKET_OPT(log_ctx, sc, IPPROTO_UDPLITE, UDPLITE_SEND_CSCOV, (int)8);

        /* Setup receive coverage too */
        SET_SOCKET_OPT(log_ctx, sc, IPPROTO_UDPLITE, UDPLITE_RECV_CSCOV, (int)8);
    }

    /* Disable IPv6 only */
    SET_SOCKET_OPT(log_ctx, sc, IPPROTO_IPV6, IPV6_V6ONLY, (int)0);

    /* Disable fragmentation */
    SET_SOCKET_OPT(log_ctx, sc, IPPROTO_IPV6, IPV6_DONTFRAG, (int)1);

    /* Enable MTU monitoring */
//    SET_SOCKET_OPT(log_ctx, sc, IPPROTO_IPV6, IPV6_RECVPATHMTU, (int)1);

    /* Setup binding */
    sc->local_addr.sin6_family = AF_INET6;
    sc->local_addr.sin6_port = addr->port;
    sc->local_addr.sin6_flowinfo = 0;
    memset(sc->local_addr.sin6_addr.s6_addr, 0, 16);
    sc->local_addr.sin6_scope_id = 0;

    if (addr->listen) {
        memcpy(sc->local_addr.sin6_addr.s6_addr, addr->ip, 16);
        sc->local_addr.sin6_scope_id = addr->scope;

        if (bind(sc->socket, &sc->local_addr, sizeof(sc->local_addr)) < 0) {
            ret = avt_handle_errno(log_ctx, "Unable to bind socket to address: %i %s\n");
            goto fail;
        }
    }

    /* Setup connecting */
    sc->remote_addr.sin6_family = AF_INET6;
    sc->remote_addr.sin6_port = addr->port;
    sc->remote_addr.sin6_flowinfo = 0;
    memset(sc->local_addr.sin6_addr.s6_addr, 0, 16);
    sc->remote_addr.sin6_scope_id = 0;

    if (!addr->listen) {
        memcpy(sc->remote_addr.sin6_addr.s6_addr, addr->ip, 16);
        sc->remote_addr.sin6_scope_id = addr->scope;

        if (connect(sc->socket, &sc->remote_addr, sizeof(sc->remote_addr)) < 0) {
            ret = avt_handle_errno(log_ctx, "Unable to connect socket to address: %i %s\n");
            goto fail;
        }
    }

    return 0;

fail:
    close(sc->socket);
    sc->socket = -1;
    return ret;
}

uint32_t avt_socket_get_mtu(void *log_ctx, AVTSocketCommon *sc)
{
    int ret = get_socket_opt(log_ctx, sc, IPPROTO_IPV6, IPV6_PATHMTU,
                             &sc->mtu6, sizeof(sc->mtu6),
                             "Unable to get MTU: %i %s\n");
    if (ret < 0)
        return ret;

    return sc->mtu6.ip6m_mtu;

#if 0
    struct ifreq ifr = sc->ifr;
    int ret = ioctl(sc->socket, SIOCGIFMTU, &ifr);
    if (ret < 0) {
        ret = avt_handle_errno(log_ctx, "Unable to find MTU: %i %s\n");
        return 0;
    }

    return ifr.ifr_mtu;
#endif
}

int avt_socket_close(void *log_ctx, AVTSocketCommon *sc)
{
    int ret = 0;

    if (sc->socket >= 0) {
        ret = close(sc->socket);
        if (ret < 0)
            ret = avt_handle_errno(log_ctx, "Error closing socket: %i %s\n");
    }

    return ret;
}
