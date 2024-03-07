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

#include "os_compat.h"
#include "address.h"

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <limits.h>

#include <avtransport/avtransport.h>

#include "config.h"

int avt_addr_get_scope(void *log_ctx, const uint8_t ip[16],
                       uint32_t *scope, const char *iface)
{
    if (!((IN6_IS_ADDR_LINKLOCAL(ip) ||
          (IN6_IS_ADDR_MC_LINKLOCAL(ip))) && iface))
        return 0;

    *scope = if_nametoindex(iface);
    if (!(*scope) && errno) {
        char8_t err_info[256];
        avt_log(log_ctx, AVT_LOG_ERROR, "Unable to get interface \"%s\" index: %s\n",
                iface, strerror_safe(err_info, sizeof(err_info), errno));
        return AVT_ERROR(errno);
    }

    return 0;
}

int avt_addr_4to6(uint8_t ip6[16], uint32_t ip4)
{
    memset(ip6, 0, 16);

    /* Map IPv4's binary form to IPv6 */
    if (ip4 == 0x00000000) {
        ip6[10] = 0xFF; /* Any address */
        ip6[11] = 0xFF;
    } else if (ip4 == 0xFFFFFFFF) {
        ip6[10] = 0xFF; /* No address */
        ip6[11] = 0xFF;
        ip6[12] = 0xFF;
        ip6[13] = 0xFF;
        ip6[14] = 0xFF;
        ip6[15] = 0xFF;
    } else {
        ip6[10] = 0xFF; /* Any address + IPv4 */
        ip6[11] = 0xFF;
        ip6[12] = ((uint8_t *)&ip4)[0];
        ip6[13] = ((uint8_t *)&ip4)[1];
        ip6[14] = ((uint8_t *)&ip4)[2];
        ip6[15] = ((uint8_t *)&ip4)[3];
    }

    return 0;
}

static int parse_settings(void *log_ctx, AVTAddress *addr, char *next)
{
    char *tmp;
    char *tmp2;
    char *end;
    char *def_sid = NULL;

    if (next[0] != '?') {
        def_sid = next;
        next = NULL;

        int len = strlen(def_sid);
        for (auto i = 0; i < len; i++) {
            if (def_sid[i] == '?') {
                def_sid[i] = 0;
                next = &def_sid[i + 1];
                break;
            }
        }
    }

    if (def_sid) {
        def_sid = strtok_r(def_sid, "+", &tmp);
        do {
            uint64_t res = strtoul(def_sid, &end, 10);

            if (end == def_sid) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Invalid stream ID: %s\n", def_sid);
                return AVT_ERROR(EINVAL);
            } else if (((res == ULONG_MAX) && (errno == ERANGE)) || (res > UINT16_MAX)) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Stream ID value too high: %s\n", def_sid);
                return AVT_ERROR(ERANGE);
            } else if (res == UINT16_MAX) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Stream ID value reserved: %s\n", def_sid);
                return AVT_ERROR(EINVAL);
            }

            if (!addr->default_sid) {
                addr->default_sid = calloc(UINT16_MAX, sizeof(*addr->default_sid));
                if (!addr->default_sid)
                    return AVT_ERROR(ENOMEM);
            }

            for (auto i = 0; i < addr->nb_default_sid; i++) {
                if (addr->default_sid[i] == res) {
                    avt_log(log_ctx, AVT_LOG_ERROR, "Stream ID value already specified: %" PRIu64 "\n", res);
                    return AVT_ERROR(EINVAL);
                }
            }

            addr->default_sid[addr->nb_default_sid++] = res;
        } while ((def_sid = strtok_r(NULL, "+", &tmp)));
    }

    if (next && next[0] != '\0') {
        char *option = strtok_r(next, "?", &tmp);
        do {
        char *key = strtok_r(option, "=", &tmp2);
            char *val = strtok_r(NULL, "=", &tmp2);
            if (!val) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Key %s has no value!\n", key);
                return AVT_ERROR(EINVAL);
            }

            if (!strcmp(key, "rx_buf")) {
                uint64_t res = strtoul(val, &end, 10);
                if (end == val) {
                    avt_log(log_ctx, AVT_LOG_ERROR, "Invalid option %s value: %s\n", key, val);
                    return AVT_ERROR(EINVAL);
                } else if (((res == ULONG_MAX) && (errno == ERANGE)) || res > INT32_MAX) {
                    avt_log(log_ctx, AVT_LOG_ERROR, "Option %s value too high: %s\n", key, val);
                    return AVT_ERROR(ERANGE);
                }

                addr->opts.rx_buf = res;
            } else if (!strcmp(key, "tx_buf")) {
                uint64_t res = strtoul(val, &end, 10);
                if (end == val) {
                    avt_log(log_ctx, AVT_LOG_ERROR, "Invalid option %s value: %s\n", key, val);
                    return AVT_ERROR(EINVAL);
                } else if (((res == ULONG_MAX) && (errno == ERANGE)) || res > INT32_MAX) {
                    avt_log(log_ctx, AVT_LOG_ERROR, "Option %s value too high: %s\n", key, val);
                    return AVT_ERROR(ERANGE);
                }

                addr->opts.tx_buf = res;
            } else {
                avt_log(log_ctx, AVT_LOG_ERROR, "Option %s not recognized!\n", key);
                return AVT_ERROR(EINVAL);
            }
        } while ((option = strtok_r(NULL, "?", &tmp)));
    }

    return 0;
}

int avt_addr_from_url(void *log_ctx, AVTAddress *addr,
                      bool listen, const char *path)
{
    int ret = 0;
    int native_uri = 0;
    char *end;
    char *tmp2;
    char *tmp = NULL;
    char *next = NULL;

    /* Zero out output */
    memset(addr, 0, sizeof(*addr));
    addr->port = CONFIG_DEFAULT_PORT;
    addr->proto = CONFIG_DEFAULT_TYPE;
    addr->mode = AVT_MODE_DEFAULT;

    addr->listen = listen;

    char *dup = next = strdup(path);
    if (!dup)
        return AVT_ERROR(ENOMEM);

    if (!strncmp(next, "avt://", strlen("avt://"))) {
        native_uri = 1;
        next += strlen("avt://");
    } else if (!strncmp(next, "udp://", strlen("udp://"))) {
        /* Unofficial and conflicting, but let's accept it */
        addr->proto = AVT_PROTOCOL_UDP;
        next += strlen("udp://");
    } else if (!strncmp(next, "udplite://", strlen("udplite://"))) {
        /* Unofficial, but let's accept it */
        addr->proto = AVT_PROTOCOL_UDP_LITE;
        next += strlen("udplite://");
    } else if (!strncmp(next, "quic://", strlen("quic://"))) {
        /* Unofficial, but let's accept it */
        addr->proto = AVT_PROTOCOL_QUIC;
        next += strlen("quic://");
    } else if (!strncmp(next, "file://", strlen("file://"))) {
        /* Whatever */
        addr->proto = AVT_PROTOCOL_FILE;
        next += strlen("file://");
    } else {
        avt_log(log_ctx, AVT_LOG_ERROR, "Invalid URI scheme\n");
        ret = AVT_ERROR(EINVAL);
        goto end;
    }

    if (native_uri) {
        char *transport, *mode;
        char *tm = strtok_r(next, "@", &tmp);
        next = strtok_r(NULL, "@", &tmp);

        if (next) {
            transport = strtok_r(tm, ":", &tmp2);
            mode = strtok_r(NULL, ":", &tmp2);
        }

        if (!next) { /* Default, nothing given */
            addr->proto = CONFIG_DEFAULT_TYPE;
        } else if (!strcmp(transport, "udp")) {
            addr->proto = AVT_PROTOCOL_UDP;
        } else if (!strcmp(transport, "udplite")) {
            addr->proto = AVT_PROTOCOL_UDP_LITE;
        } else if (!strcmp(transport, "quic")) {
            addr->proto = AVT_PROTOCOL_QUIC;
        } else if (!strcmp(transport, "file")) {
            addr->proto = AVT_PROTOCOL_FILE;
        } else {
            avt_log(log_ctx, AVT_LOG_ERROR, "Invalid transport: %s\n", transport);
            ret = AVT_ERROR(EINVAL);
            goto end;
        }

        if (!next) {
            next = tm;
        } else if (mode && !strcmp(mode, "passive")) {
            addr->mode = AVT_MODE_PASSIVE;
        } else if (mode && !strcmp(mode, "active")) {
            addr->mode = AVT_MODE_ACTIVE;
        } else if (mode && !strcmp(mode, "default")) {
            addr->mode = AVT_MODE_DEFAULT;
        } else if (mode) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Invalid mode: %s\n", mode);
            ret = AVT_ERROR(EINVAL);
            goto end;
        }
    }

    /* If protocol is a file, skip everything */
    if (addr->proto == AVT_PROTOCOL_FILE) {
        addr->path = strdup(next);
        goto end;
    }

    char *host = NULL;
    char *port_str = NULL;

    /* Back to front parsing. First, the settings, if any exist */
    host = strtok_r(next, "/", &tmp);
    next = strtok_r(NULL, "/", &tmp);

    /* Check if address is IPv6 or IPv4 */
    if (host[0] == '[') {
        host = strtok_r(host, "[]", &tmp);
        port_str = strtok_r(NULL, "[:]", &tmp);
    } else {
        host = strtok_r(host, ":", &tmp);
        port_str = strtok_r(NULL, ":", &tmp);
    }

    /* Parse port, if specified */
    if (port_str) {
        uint64_t res = strtoul(port_str, &end, 10);
        if (!res || (end == port_str)) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Invalid port: %s\n", port_str);
            ret = AVT_ERROR(EINVAL);
            goto end;
        }
        if (((res == ULONG_MAX) && (errno == ERANGE)) || (res > UINT16_MAX)) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Port value too high: %s\n", port_str);
            ret = AVT_ERROR(ERANGE);
            goto end;
        }
        addr->port = res;
    }

    /* Parse device name */
    char *iface;
    host = strtok_r(host, "%", &tmp);
    iface = strtok_r(NULL, "%", &tmp);
    if (iface)
        addr->interface = strdup(iface);

    /* Try IPv4 */
    struct in_addr ipv4_addr;
    ret = inet_pton(AF_INET, host, &ipv4_addr);
    if (ret < 1) {
        if (ret < 0) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Invalid address family: %s\n", host);
            ret = AVT_ERROR(EINVAL);
            goto end;
        }

        /* IPv6? */
        ret = inet_pton(AF_INET6, host, addr->ip);
        if (ret < 1) {
            if (ret < 0) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Invalid address family: %s\n", host);
                ret = AVT_ERROR(EINVAL);
                goto end;
            }

            /* Is it a hostname? */
            struct addrinfo hints = { 0 }, *res;
            hints.ai_socktype = SOCK_DGRAM;
            hints.ai_family   = AF_INET6;
            hints.ai_protocol = (addr->proto == AVT_PROTOCOL_UDP_LITE) ?
                                IPPROTO_UDP : IPPROTO_UDPLITE;
            hints.ai_flags    = AI_V4MAPPED |
                                AI_ADDRCONFIG |
                                (listen ? AI_PASSIVE : 0x0);

            ret = getaddrinfo(host, port_str ? port_str : CONFIG_DEFAULT_PORT_STR,
                              &hints, &res);
            if (ret) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Invalid host %s: %s\n",
                        host, gai_strerror(ret));
                ret = AVT_ERROR(EINVAL);
                goto end;
            }

            memcpy(addr->ip, res[0].ai_addr, 16);
            freeaddrinfo(res);
        }
    } else {
        avt_addr_4to6(addr->ip, ipv4_addr.s_addr);
    }

    if (next && next[0] != '\0') {
        ret = parse_settings(log_ctx, addr, next);
        if (ret < 0)
            goto end;
    }

    ret = avt_addr_get_scope(log_ctx, addr->ip, &addr->scope, addr->interface);
    if (ret < 0)
        goto end;

    avt_log(log_ctx, AVT_LOG_VERBOSE,
            "URL parsed:\n"
            "    Address: %s (%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x)\n"
            "    Port: %u\n"
            "    Interface: %s\n"
            "    Protocol: %s\n"
            "    Mode: %s\n",
            host,
            addr->ip[ 0], addr->ip[ 1], addr->ip[ 2], addr->ip[ 3],
            addr->ip[ 4], addr->ip[ 5], addr->ip[ 6], addr->ip[ 7],
            addr->ip[ 8], addr->ip[ 9], addr->ip[10], addr->ip[11],
            addr->ip[12], addr->ip[13], addr->ip[14], addr->ip[15],
            addr->port,
            addr->interface,
            addr->proto == AVT_PROTOCOL_NOOP     ? "noop" :
            addr->proto == AVT_PROTOCOL_PIPE     ? "pipe" :
            addr->proto == AVT_PROTOCOL_FILE     ? "file" :
            addr->proto == AVT_PROTOCOL_FD       ? "fd" :
            addr->proto == AVT_PROTOCOL_PACKET   ? "packet" :
            addr->proto == AVT_PROTOCOL_QUIC     ? "quic" :
            addr->proto == AVT_PROTOCOL_UDP_LITE ? "udplite" :
            addr->proto == AVT_PROTOCOL_UDP      ? "udp" :
                                                   "unknown",
            addr->mode == AVT_MODE_DEFAULT ? "default" :
            addr->mode == AVT_MODE_ACTIVE  ? "active" :
            addr->mode == AVT_MODE_PASSIVE ? "passive" :
                                             "unknown"
            );

end:
    free(dup);
    return ret;
}

int avt_addr_from_info(void *log_ctx, AVTAddress *addr, AVTConnectionInfo *info)
{
    int err;
    memset(addr, 0, sizeof(*addr));

    if (info->type < 0 || info->type > AVT_CONNECTION_CALLBACK)
        return AVT_ERROR(EINVAL);

    switch (info->type) {
    case AVT_CONNECTION_NULL:
        addr->proto = AVT_PROTOCOL_NOOP;
        break;
    case AVT_CONNECTION_URL:
        err = avt_addr_from_url(log_ctx, addr, info->url.listen, info->url.url);
        if (err < 0)
            return err;
        break;
    case AVT_CONNECTION_SOCKET:
        break;
    case AVT_CONNECTION_FILE:
        addr->path = strdup(info->path);
        addr->proto = AVT_PROTOCOL_FILE;
        break;
    case AVT_CONNECTION_CALLBACK:
        addr->proto = AVT_PROTOCOL_NOOP;
        break;
    case AVT_CONNECTION_PACKET:
        addr->proto = AVT_PROTOCOL_PACKET;
        break;
    };

    return 0;
}

void avt_addr_free(AVTAddress *addr)
{
    free(addr->default_sid);
    free(addr->path);
    free(addr->interface);
    memset(addr, 0, sizeof(*addr));
}
