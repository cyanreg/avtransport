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

/* Live STUN reflection test.
 *
 * Requires a reachable STUN server, so it is a no-op (reported to meson as
 * "skipped") unless AVT_STUN_SERVER is set in the environment. To run it
 * against a real server, e.g. the project's coturn:
 *
 *     AVT_STUN_SERVER=stun.example.org AVT_STUN_PORT=3478 ./tests/stun
 *
 * It binds a UDP socket, asks the server for that socket's public address, and
 * prints it -- which is exactly the value to hand the other peer when pairing
 * by hand over a shell. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "stun.h"

int main(void)
{
    const char *host = getenv("AVT_STUN_SERVER");
    const char *port = getenv("AVT_STUN_PORT");
    if (!port || !port[0])
        port = "3478";

    if (!host || !host[0]) {
        fprintf(stderr,
                "STUN test skipped: set AVT_STUN_SERVER (and optionally "
                "AVT_STUN_PORT, default 3478) to run against a live server.\n");
        return 77; /* meson: test skipped */
    }

    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
    };
    struct addrinfo *res = NULL;
    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "STUN test: cannot resolve %s:%s: %s\n",
                host, port, gai_strerror(gai));
        return 1;
    }

    int rc = 1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;

        struct sockaddr_in6 mapped;
        int ret = avt_stun_query(NULL, fd, ai->ai_addr, ai->ai_addrlen,
                                 &mapped, 3000);
        close(fd);
        if (ret < 0) {
            fprintf(stderr, "STUN query failed via one server address: %d\n", ret);
            continue;
        }

        const uint8_t *a = (const uint8_t *)&mapped.sin6_addr;
        unsigned p = ntohs(mapped.sin6_port);
        char ipbuf[INET6_ADDRSTRLEN];
        static const uint8_t v4_mapped_prefix[12] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff
        };
        if (!memcmp(a, v4_mapped_prefix, 12)) {
            inet_ntop(AF_INET, a + 12, ipbuf, sizeof(ipbuf));
            printf("Reflexive address: %s:%u\n", ipbuf, p);
        } else {
            inet_ntop(AF_INET6, a, ipbuf, sizeof(ipbuf));
            printf("Reflexive address: [%s]:%u\n", ipbuf, p);
        }

        rc = 0;
        break;
    }

    freeaddrinfo(res);
    if (rc != 0)
        fprintf(stderr, "STUN test: no server address yielded a response\n");
    return rc;
}
