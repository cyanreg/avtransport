/*
 * Copyright © 2023, Lynne
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>

#include <libavtransport/common.h>

#include "common.h"
#include "reorder.h"

int avt_init(AVTContext **ctx, AVTContextOptions *opts)
{
    AVTContext *tmp = malloc(sizeof(*tmp));
    if (!tmp)
        return AVT_ERROR(ENOMEM);

    if (opts)
        tmp->opts = *opts;

    atomic_init(&tmp->dst.seq, 0);
    atomic_init(&tmp->src.seq, 0);

    *ctx = tmp;
    return 0;
}

void avt_close(AVTContext **ctx)
{
    if (ctx) {
        avt_reorder_uninit(*ctx);

        free(*ctx);
        *ctx = NULL;
    }
}

int avt_parse_address(const char *path, enum AVTProtocolType *proto,
                      uint8_t dst_ip[16], uint16_t *dst_port)
{
    int native_uri = 0;

    /* Zero out output */
    *proto = AVT_UNKNOWN;
    dst_ip = (uint8_t [16]){ 0 };
    *dst_port = 0;

    char *dup = strdup(path);
    if (!dup)
        return AVT_ERROR(ENOMEM);

    char *tmp = NULL;
    char *scheme = strtok_r(dup, ":", &tmp);
    if (!strcmp(scheme, "avt")) {
        native_uri = 1;
    } else if (!strcmp(scheme, "udp")) { /* Unofficial and conflicting, but let's accept it */
        *proto = AVT_UDP;
    } else if (!strcmp(scheme, "quic")) { /* Unofficial, but let's accept it */
        *proto = AVT_QUIC;
    } else {
        free(dup);
        return AVT_ERROR(ENOTSUP);
    }

    char *addr_pre = strtok_r(scheme, ":", &tmp);
    if (!strcmp(addr_pre, "//") && (addr_pre[2])) {
        addr_pre += 2;
    } else { /* Error out if there's no :// */
        free(dup);
        return AVT_ERROR(ENOTSUP);
    }

    if (native_uri) {
        char *transport = strtok_r(addr_pre, "@", &tmp);
        char *serv = strtok_r(transport, "@", &tmp);
        if (!serv)
            addr_pre = transport;
    }

    if (addr_pre[0]) {
    } else if (addr_pre[0] == '[') {
        tmp = NULL;
        char *endb = strtok_r(addr_pre, "]", &tmp);

        struct in6_addr dst_6 = { 0 };
        int ret = inet_pton(AF_INET6, endb, &dst_6);
        if (!ret)
            endb = strtok_r(addr_pre, "]", &tmp);

        if (ret || !endb) {
            free(dup);
            return AVT_ERROR(EINVAL);
        }

        memcpy(dst_ip, dst_6.s6_addr, 16);

        if (addr_pre[0] == ':') {
            char *res = NULL;
            *dst_port = strtol(addr_pre + 1, &res, 10);
            if (res) {
                free(dup);
                return AVT_ERROR(EINVAL);
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
            return AVT_ERROR(EINVAL);
        }

        memcpy(dst_ip, res->ai_addr, res->ai_addrlen);
    } else {
        memcpy(dst_ip, dst_4.s6_addr, 16);
    }

    free(dup);

    return 0;
}

AVTStream *avt_alloc_stream(AVTContext *ctx, uint16_t id)
{
    if (!ctx->dst.ctx)
        return NULL;

    AVTStream *ret = NULL;
    AVTStream **new = realloc(ctx->stream, sizeof(*new)*(ctx->nb_stream + 1));
    if (!new)
        return NULL;
    ctx->stream = new;

    ret = calloc(1, sizeof(**new));
    if (!ret)
        return NULL;

    ret->private = calloc(1, sizeof(*ret->private));
    if (!ret->private) {
        free(ret);
        return NULL;
    }

    new[ctx->nb_stream] = ret;
    ctx->nb_stream++;

    return ret;
}

AVTStream *avt_find_stream(AVTContext *ctx, uint16_t id)
{
    int i;
    for (i = 0; i < ctx->nb_stream; i++) {
        if (ctx->stream[i]->id == id)
            break;
    }
    if (i == ctx->nb_stream)
        return NULL;
    return ctx->stream[i];
}

void avt_log(void *ctx, enum AVTLogLevel level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
