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

#ifndef AVTRANSPORT_IO_SOCKET_COMMON_H
#define AVTRANSPORT_IO_SOCKET_COMMON_H

#include <netinet/in.h>
#include <net/if.h>
#include <sys/un.h>

#include <avtransport/avtransport.h>
#include "address.h"

typedef struct AVTSocketCommon {
    int socket;

    struct sockaddr *local_addr;
    struct sockaddr *remote_addr;
    socklen_t addr_size;

    union {
        struct {
            struct sockaddr_in6 local_addr;
            struct sockaddr_in6 remote_addr;
        } ip;
        struct {
            struct sockaddr_un local_addr;
            struct sockaddr_un remote_addr;
        } un;
    };
} AVTSocketCommon;

/* Open a socket with a specific address */
int avt_socket_open(void *log_ctx, AVTSocketCommon *sc, AVTAddress *addr);

/* Get MTU */
uint32_t avt_socket_get_mtu(void *log_ctx, AVTSocketCommon *sc);

/* Close a socket context */
int avt_socket_close(void *log_ctx, AVTSocketCommon *sc);

/* Macro to set socket options */
#define SET_SOCKET_OPT(log_ctx, socket, lvl, opt, val)                         \
    do {                                                                       \
        [[maybe_unused]] auto tempval = (val);                                 \
        ret = setsockopt(socket, lvl, opt,                                     \
                         _Generic((val),                                       \
                             int32_t: &(tempval),                              \
                             uint32_t: &(tempval),                             \
                             int64_t: &(tempval),                              \
                             uint64_t: &(tempval),                             \
                             default: (val)),                                  \
                         sizeof(val));                                         \
        if (ret < 0) {                                                         \
            ret = avt_handle_errno(log_ctx,                                    \
                                   "setsockopt(" #lvl ", " #opt ") "           \
                                   "failed: %i %s\n");                         \
            return ret;                                                        \
        }                                                                      \
    } while (0)

/* Read a socket option */
int avt_get_socket_opt(void *log_ctx, int socket,
                       int lvl, int opt, void *data, int len,
                       const char *errmsg);

#endif /* AVTRANSPORT_IO_SOCKET_COMMON_H */
