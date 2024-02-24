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

#include <avtransport/avtransport.h>
#include "address.h"

typedef struct AVTSocketCommon {
    int socket;

    struct ifreq ifr;
    struct ip6_mtuinfo mtu6;
    struct sockaddr_in6 local_addr;

    struct sockaddr_in6 remote_addr;
} AVTSocketCommon;

int avt_handle_errno(void *log_ctx, const char *msg);

/* Open a socket with a specific address */
int avt_socket_open(void *log_ctx, AVTSocketCommon *sc, AVTAddress *addr);

/* Get MTU */
uint32_t avt_socket_get_mtu(void *log_ctx, AVTSocketCommon *sc);

/* Close a socket */
int avt_socket_close(void *log_ctx, AVTSocketCommon *sc);

#endif /* AVTRANSPORT_IO_SOCKET_COMMON_H */
