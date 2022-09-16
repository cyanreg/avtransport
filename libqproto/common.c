/*
 * Copyright © 2022 Lynne
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the “Software”), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>

#include "common.h"
#include "reorder.h"

int qp_init(QprotoContext **qp, QprotoContextOptions *opts)
{
    QprotoContext *ctx = malloc(sizeof(*ctx));
    if (!ctx)
        return QP_ERROR(ENOMEM);

    if (opts)
        ctx->opts = *opts;

    atomic_init(&ctx->dst.seq, 0);
    atomic_init(&ctx->src.seq, 0);

    *qp = ctx;
    return 0;
}

void qp_close(QprotoContext **qp)
{
    if (qp) {
        pq_reorder_uninit(*qp);

        free(*qp);
        *qp = NULL;
    }
}

int pq_parse_address(const char *path, enum PQProtocolType *proto,
                     uint8_t dst_ip[16], uint16_t *dst_port)
{
    /* Zero out output */
    *proto = PQ_UNKNOWN;
    dst_ip = (uint8_t [16]){ 0 };
    *dst_port = 0;

    char *dup = strdup(path);
    if (!dup)
        return QP_ERROR(ENOMEM);

    char *tmp = NULL;
    char *protocol = strtok_r(dup, ":", &tmp); /* Protocol */
    if (!strcmp(protocol, "udp")) {
        *proto = PQ_UDP;
    } else if (!strcmp(protocol, "quic")) {
        *proto = PQ_QUIC;
    } else {
        free(dup);
        return QP_ERROR(ENOTSUP);
    }

    char *addr_pre = strtok_r(protocol, ":", &tmp);
    if (!strcmp(addr_pre, "//") && (addr_pre[2])) {
        addr_pre += 2;
    } else {
        free(dup);
        return QP_ERROR(ENOTSUP);
    }

    if (addr_pre[0] == '[') {
        tmp = NULL;
        char *endb = strtok_r(addr_pre, "]", &tmp);

        struct in6_addr dst_6 = { 0 };
        int ret = inet_pton(AF_INET6, endb, &dst_6);
        if (!ret)
            endb = strtok_r(addr_pre, "]", &tmp);

        if (ret || !endb) {
            free(dup);
            return QP_ERROR(EINVAL);
        }

        memcpy(dst_ip, dst_6.s6_addr, 16);

        if (addr_pre[0] == ':') {
            char *res = NULL;
            *dst_port = strtol(addr_pre + 1, &res, 10);
            if (res) {
                free(dup);
                return QP_ERROR(EINVAL);
            }
        }

        free(dup);
        return 0;
    }

    /* Attempt to convert and parse the IP address as IPv4 in IPv6 */
    char *addr = strtok_r(addr_pre, ":", &tmp);
    char addr_temp[32] = { "::FFFF:" };
    memcpy(addr_temp + strlen(addr_temp), addr, strlen(addr));
    addr_temp[31] = '\0';

    struct in6_addr dst_4 = { 0 };
    int ret = inet_pton(AF_INET6, addr_temp, &dst_4);
    if (ret) {
        int error;
        struct addrinfo hints = { 0 }, *res = NULL;
        const char *service = "0";

        char *sport = strtok_r(addr_pre, ":", &tmp);
        if (sport[0])
            sport += 1; /* Skip : */

        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_family   = AF_INET6;
        hints.ai_flags    = 0;
        if ((error = getaddrinfo(addr, service, &hints, &res))) {
            free(dup);
            return QP_ERROR(EINVAL);
        }

        memcpy(dst_ip, res->ai_addr, res->ai_addrlen);
    } else {
        memcpy(dst_ip, dst_4.s6_addr, 16);
    }

    free(dup);

    return 0;
}

QprotoStream *qp_alloc_stream(QprotoContext *qp, uint16_t id)
{
    if (!qp->dst.ctx)
        return NULL;

    QprotoStream *ret = NULL;
    QprotoStream **new = realloc(qp->stream, sizeof(*new)*(qp->nb_stream + 1));
    if (!new)
        return NULL;
    qp->stream = new;

    ret = calloc(1, sizeof(**new));
    if (!ret)
        return NULL;

    ret->private = calloc(1, sizeof(*ret->private));
    if (!ret->private) {
        free(ret);
        return NULL;
    }

    new[qp->nb_stream] = ret;
    qp->nb_stream++;

    return ret;
}

QprotoStream *qp_find_stream(QprotoContext *qp, uint16_t id)
{
    int i;
    for (i = 0; i < qp->nb_stream; i++) {
        if (qp->stream[i]->id == id)
            break;
    }
    if (i == qp->nb_stream)
        return NULL;
    return qp->stream[i];
}

void pq_log(void *ctx, enum QPLogLevel level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
