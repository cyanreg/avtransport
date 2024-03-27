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
#include "utils_internal.h"

int avt_parse_uuid(uint8_t out[16], const char *src)
{
    if (strlen(src) != 36)
        return AVT_ERROR(EINVAL);

    for (int i = 0; i < 16; i++) {
        /* Skip dashes */
        if (i == 4 || i == 6 || i == 8 || i == 10)
            src++;

        int hi = avt_ascii_to_int(*src++);
        int lo = avt_ascii_to_int(*src++);

        if (hi == -1 || lo == -1)
            return AVT_ERROR(EINVAL);

        out[i] = (hi << 4) + lo;
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

static int parse_host_addr(void *log_ctx, AVTAddress *addr, char *host,
                           bool listen, bool *pure_ipv6)
{
    int ret;
    char *port_str;
    char *end = NULL, *tmp = NULL;

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
            return AVT_ERROR(EINVAL);
        }
        if (((res == ULONG_MAX) && (errno == ERANGE)) || (res > UINT16_MAX)) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Port value too high: %s\n", port_str);
            return AVT_ERROR(ERANGE);
        }
        addr->port = res;
    }

    /* Parse device name */
    char *iface;
    host = strtok_r(host, "%", &tmp);
    iface = strtok_r(NULL, "%", &tmp);
    if (iface) {
        addr->interface = strdup(iface);

        /* Immediately get the interface index */
        addr->interface_idx = if_nametoindex(iface);
        if (!(addr->interface_idx) && errno) {
            char8_t err_info[256];
            avt_log(log_ctx, AVT_LOG_WARN, "Unable to get interface \"%s\" index: %s\n",
                    iface, strerror_safe(err_info, sizeof(err_info), errno));
        }
    }

    /* Try IPv4 */
    struct in_addr ipv4_addr;
    ret = inet_pton(AF_INET, host, &ipv4_addr);
    if (ret < 1) {
        if (ret < 0) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Invalid address family: %s\n", host);
            return AVT_ERROR(EINVAL);
        }

        /* IPv6? */
        ret = inet_pton(AF_INET6, host, addr->ip);
        if (ret < 1) {
            if (ret < 0) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Invalid address family: %s\n", host);
                return AVT_ERROR(EINVAL);
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
                return AVT_ERROR(EINVAL);
            }

            memcpy(addr->ip, res[0].ai_addr, 16);
            freeaddrinfo(res);
        } else {
            *pure_ipv6 = true;
        }
    } else {
        avt_addr_4to6(addr->ip, ipv4_addr.s_addr);
    }

    /* Set the scope for an IP address */
    if ((IN6_IS_ADDR_LINKLOCAL(addr->ip)    ||
         IN6_IS_ADDR_MC_LINKLOCAL(addr->ip) ||
         IN6_IS_ADDR_MULTICAST(addr->ip)) && addr->interface) {
        addr->scope = addr->interface_idx;
    } else {
        addr->scope = 0;
    }

    return 0;
}

static int parse_default_streams(void *log_ctx, AVTAddress *addr, char *next)
{
    char *end;
    char *tmp;

    free(addr->opts.default_sid);
    addr->opts.default_sid = NULL;

    char *val = strtok_r(next, ",", &tmp);
    do {
        uint64_t res = strtoul(val, &end, 10);

        if (end == val) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Invalid stream ID: %s\n", val);
            return AVT_ERROR(EINVAL);
        } else if (((res == ULONG_MAX) && (errno == ERANGE)) || (res > UINT16_MAX)) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Stream ID value too high: %s\n", val);
            return AVT_ERROR(ERANGE);
        } else if (res == UINT16_MAX) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Stream ID value reserved: %s\n", val);
            return AVT_ERROR(EINVAL);
        }

        if (!addr->opts.default_sid) {
            addr->opts.default_sid = calloc(UINT16_MAX, sizeof(*addr->opts.default_sid));
            if (!addr->opts.default_sid)
                return AVT_ERROR(ENOMEM);
        }

        for (auto i = 0; i < addr->opts.nb_default_sid; i++) {
            if (addr->opts.default_sid[i] == res) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Stream ID value already specified: %" PRIu64 "\n", res);
                return AVT_ERROR(EINVAL);
            }
        }

        addr->opts.default_sid[addr->opts.nb_default_sid++] = res;
    } while ((val = strtok_r(NULL, ",", &tmp)));

    return 0;
}

static int parse_settings(void *log_ctx, AVTAddress *addr,
                          char *next, char *opts_buf, size_t opts_buf_size)
{
    int ret;
    char *tmp;
    char *tmp2;
    char *end;

    if (!next || next[0] == '\0') {
        return 0;
    } else if (next[0] != '#') {
        char uuid_tmp[36 + 1];
        strncpy(uuid_tmp, next, sizeof(uuid_tmp) - 1);
        uuid_tmp[36] = '\0';

        ret = avt_parse_uuid(addr->uuid, uuid_tmp);
        if (ret < 0) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Invalid UUID: %s\n", uuid_tmp);
            return ret;
        }

        next += 36;

        snprintf(&opts_buf[strlen(opts_buf)], opts_buf_size - strlen(opts_buf),
                 "      UUID: %s\n", uuid_tmp);
    }

    if (next[0] == '\0') {
        return 0;
    } else if (next[0] != '#') {
        avt_log(log_ctx, AVT_LOG_ERROR, "Invalid settings string: %s\n", next);
        return AVT_ERROR(EINVAL);
    }

    next++;

    char *option = strtok_r(next, "&", &tmp);
    do {
        char *key = strtok_r(option, "=", &tmp2);
        char *val = strtok_r(NULL, "=", &tmp2);
        if (!val) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Key %s has no value!\n", key);
            return AVT_ERROR(EINVAL);
        }

        if (!strcmp(key, "t")) {
            double res = strtod(val, &end);
            if (end == val) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Invalid option %s value: %s\n", key, val);
                return AVT_ERROR(EINVAL);
            }

            addr->opts.start_time = res * 1000 * 1000 * 1000;
        } else if (!strcmp(key, "default")) {
            ret = parse_default_streams(log_ctx, addr, val);
            if (ret < 0)
                return ret;
        } else if (!strcmp(key, "tx_buf") || !strcmp(key, "rx_buf")) {
            uint64_t res = strtoul(val, &end, 10);
            if (end == val) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Invalid option %s value: %s\n", key, val);
                return AVT_ERROR(EINVAL);
            } else if (((res == ULONG_MAX) && (errno == ERANGE)) || res > INT32_MAX) {
                avt_log(log_ctx, AVT_LOG_ERROR, "Option %s value too high: %s\n", key, val);
                return AVT_ERROR(ERANGE);
            }

            if (!strcmp(key, "tx_buf"))
                addr->opts.tx_buf = res;
            else if (!strcmp(key, "rx_buf"))
                addr->opts.rx_buf = res;
        } else {
            avt_log(log_ctx, AVT_LOG_ERROR, "Option %s not recognized!\n", key);
            return AVT_ERROR(EINVAL);
        }
    } while ((option = strtok_r(NULL, "&", &tmp)));

    /* Write parsed settings to the logging buffer */
    if (addr->opts.start_time)
        snprintf(&opts_buf[strlen(opts_buf)], opts_buf_size - strlen(opts_buf),
                 "      start time: %" PRIi64 "\n", addr->opts.start_time);
    if (addr->opts.rx_buf)
        snprintf(&opts_buf[strlen(opts_buf)], opts_buf_size - strlen(opts_buf),
                 "      rx_buf: %i\n", addr->opts.rx_buf);
    if (addr->opts.tx_buf)
        snprintf(&opts_buf[strlen(opts_buf)], opts_buf_size - strlen(opts_buf),
                 "      tx_buf: %i\n", addr->opts.tx_buf);
    if (addr->opts.nb_default_sid) {
        snprintf(&opts_buf[strlen(opts_buf)], opts_buf_size - strlen(opts_buf),
                 "      default streams: ");
        for (int i = 0; i < addr->opts.nb_default_sid; i++)
            snprintf(&opts_buf[strlen(opts_buf)], opts_buf_size - strlen(opts_buf),
                     "%i%s", addr->opts.default_sid[i],
                     (i == (addr->opts.nb_default_sid - 1) ? "\0" : ", "));
        snprintf(&opts_buf[strlen(opts_buf)], opts_buf_size - strlen(opts_buf),
                 "\n");
    }

    return 0;
}

int avt_addr_from_url(void *log_ctx, AVTAddress *addr,
                      bool listen, const char *path)
{
    int ret = 0;
    int native_uri = 0;
    char *tmp2;
    char *tmp = NULL;
    char *next = NULL;
    bool pure_ipv6 = false;
    char opts_buf[1024] = { };

    /* Zero out output */
    memset(addr, 0, sizeof(*addr));
    addr->fd = -1;

    addr->type = AVT_ADDRESS_URL;
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
        addr->type = AVT_ADDRESS_FILE;
        addr->proto = AVT_PROTOCOL_FILE;
        next += strlen("file://");
    } else if (!strncmp(next, "socket://", strlen("socket://"))) {
        /* Why? */
        addr->type = AVT_ADDRESS_UNIX;
        addr->proto = AVT_PROTOCOL_STREAM;
        next += strlen("socket://");
    } else if (!strncmp(next, "fd://", strlen("fd://"))) {
        /* I give up */
        addr->type = AVT_ADDRESS_FD;
        addr->proto = AVT_PROTOCOL_FILE;
        next += strlen("fd://");
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
            addr->type = AVT_ADDRESS_FILE;
            addr->proto = AVT_PROTOCOL_FILE;
        } else if (!strcmp(transport, "fd")) {
            addr->type = AVT_ADDRESS_FD;
            addr->proto = AVT_PROTOCOL_FILE;
        } else if (!strcmp(transport, "socket")) {
            addr->type = AVT_ADDRESS_UNIX;
            addr->proto = AVT_PROTOCOL_STREAM;
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

    /* If type is a file, skip everything */
    if (addr->type == AVT_ADDRESS_FILE) {
        addr->path = strdup(next);
        if (!addr->path)
            ret = AVT_ERROR(ENOMEM);
        goto end;
    } else if (addr->type == AVT_ADDRESS_UNIX) {
        char *location = next;
        int len = strlen(next);
        bool found_opts = false;

        /* Scan for options */
        for (int i = (len - 1); i >= 0; i--) {
            if (next[i] == '#' && i && (next[i - 1] != '\\')) {
                next[i] = '\0';
                next += i + 1;
                found_opts = true;
                break;
            }
        }
        if (location[0] == '\0') {
            avt_log(log_ctx, AVT_LOG_ERROR, "No path to socket specified!\n");
            ret = AVT_ERROR(EINVAL);
            goto end;
        }

        addr->path = strdup(location);
        if (!addr->path) {
            ret = AVT_ERROR(ENOMEM);
            goto end;
        }

        if (found_opts) {
            next--;
            next[0] = '#';
            /* Parse settings */
            ret = parse_settings(log_ctx, addr, next, opts_buf, sizeof(opts_buf));
                if (ret < 0)
                    goto end;
        }

        goto end;
    } else if (addr->type == AVT_ADDRESS_FD) {
        char *end;
        int64_t res = strtol(next, &end, 10);
        if (!res || (end == next)) {
            avt_log(log_ctx, AVT_LOG_ERROR, "Invalid fd: %s\n", next);
            ret = AVT_ERROR(EINVAL);
            goto end;
        }
        if ((((res == LONG_MAX) || (res == LONG_MIN)) && (errno == ERANGE)) ||
            (res > INT32_MAX) || (res < 0)) {
            avt_log(log_ctx, AVT_LOG_ERROR, "FD value invalid: %s\n", next);
            ret = AVT_ERROR(ERANGE);
            goto end;
        }
        addr->fd = res;
        goto end;
    }

    /* Separate out the host from settings */
    char *host = strtok_r(next, "/", &tmp);
    next = strtok_r(NULL, "/", &tmp);

    /* Parse host IP, address, and potentially a device */
    ret = parse_host_addr(log_ctx, addr, host, listen, &pure_ipv6);
    if (ret < 0)
        goto end;

    /* Parse settings */
    ret = parse_settings(log_ctx, addr, next, opts_buf, sizeof(opts_buf));
    if (ret < 0)
        goto end;

    avt_log(log_ctx, AVT_LOG_VERBOSE,
            "URL parsed:\n"
            "    %s: %s%s (%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x)\n"
            "    Port: %u\n"
            "    Interface: %s\n"
            "    Protocol: %s\n"
            "    Mode: %s\n"
            "%s%s",
            addr->listen ? "Listening" : "Transmitting",
            host, pure_ipv6 ? "]" : "\0",
            addr->ip[ 0], addr->ip[ 1], addr->ip[ 2], addr->ip[ 3],
            addr->ip[ 4], addr->ip[ 5], addr->ip[ 6], addr->ip[ 7],
            addr->ip[ 8], addr->ip[ 9], addr->ip[10], addr->ip[11],
            addr->ip[12], addr->ip[13], addr->ip[14], addr->ip[15],
            addr->port,
            addr->interface ? addr->interface : "default",
            addr->proto == AVT_PROTOCOL_QUIC     ? "quic" :
            addr->proto == AVT_PROTOCOL_UDP_LITE ? "udplite" :
            addr->proto == AVT_PROTOCOL_UDP      ? "udp" :
                                                   "unknown",
            addr->mode == AVT_MODE_DEFAULT ? "default" :
            addr->mode == AVT_MODE_ACTIVE  ? "active" :
            addr->mode == AVT_MODE_PASSIVE ? "passive" :
                                             "unknown",
            strlen(opts_buf) ? "    Settings:\n" : "",
            strlen(opts_buf) ? opts_buf : "");

 end:
    if (ret >= 0 && addr->type == AVT_ADDRESS_FILE) {
        avt_log(log_ctx, AVT_LOG_VERBOSE,
                "File path:\n    %s\n", addr->path);
    } else if (ret >= 0 && addr->type == AVT_ADDRESS_FD) {
        avt_log(log_ctx, AVT_LOG_VERBOSE,
                "FD:\n    %i\n", addr->fd);
    } else if (ret >= 0 && addr->type == AVT_ADDRESS_UNIX) {
        avt_log(log_ctx, AVT_LOG_VERBOSE,
                "Socket path:\n    %s: %s\n"
                "%s%s",
                addr->listen ? "Listening" : "Transmitting", addr->path,
                strlen(opts_buf) ? "    Settings:\n" : "",
                strlen(opts_buf) ? opts_buf : "");
    }

    free(dup);
    return ret;
}

int avt_addr_from_info(void *log_ctx, AVTAddress *addr, AVTConnectionInfo *info)
{
    int err;
    memset(addr, 0, sizeof(*addr));
    addr->fd = -1;

    if (info->type < 0 || info->type > AVT_CONNECTION_PACKET)
        return AVT_ERROR(EINVAL);

    switch (info->type) {
    case AVT_CONNECTION_NULL:
        addr->type = AVT_ADDRESS_NULL;
        addr->proto = AVT_PROTOCOL_DATAGRAM;
        break;
    case AVT_CONNECTION_URL:
        addr->type = AVT_ADDRESS_URL;
        err = avt_addr_from_url(log_ctx, addr, info->url.listen, info->url.url);
        if (err < 0)
            return err;
        break;
    case AVT_CONNECTION_FILE:
        addr->type = AVT_ADDRESS_FILE;
        addr->proto = AVT_PROTOCOL_FILE;
        addr->path = strdup(info->path);
        if (!addr->path)
            return AVT_ERROR(ENOMEM);
        break;
    case AVT_CONNECTION_SOCKET:
        addr->type = AVT_ADDRESS_SOCKET;
        addr->fd = info->socket.socket;
        memcpy(addr->ip, info->socket.dst, 16);
        addr->port = info->socket.port;
        addr->proto = info->socket.protocol;
        addr->mode = info->socket.mode;
        break;
    case AVT_CONNECTION_FD:
        addr->type = AVT_ADDRESS_FILE;
        addr->proto = AVT_PROTOCOL_FILE;
        addr->fd = info->fd;
        break;
    case AVT_CONNECTION_UNIX:
        addr->type = AVT_ADDRESS_UNIX;
        addr->proto = AVT_PROTOCOL_STREAM;
        addr->fd = info->fd;
        break;
    case AVT_CONNECTION_DATA:
        addr->type = AVT_ADDRESS_CALLBACK;
        addr->proto = AVT_PROTOCOL_DATAGRAM;
        addr->dcb.opaque = info->cb.opaque;
        addr->dcb.write = info->cb.write;
        addr->dcb.read = info->cb.read;
        break;
    case AVT_CONNECTION_PACKET:
        addr->type = AVT_ADDRESS_CALLBACK;
        addr->proto = AVT_PROTOCOL_CALLBACK_PKT;
        addr->pcb.opaque = info->pkt.opaque;
        addr->pcb.out = info->pkt.out;
        addr->pcb.in = info->pkt.in;
        break;
    };

    return 0;
}

void avt_addr_free(AVTAddress *addr)
{
    free(addr->opts.default_sid);
    free(addr->path);
    free(addr->interface);
    memset(addr, 0, sizeof(*addr));
}
