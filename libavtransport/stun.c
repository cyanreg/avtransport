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

#define _GNU_SOURCE /* getrandom */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/random.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "stun.h"
#include "bytestream.h"
#include "io_utils.h"

/* RFC 8489 constants */
#define STUN_MAGIC_COOKIE            0x2112A442u
#define STUN_HEADER_LEN              20
#define STUN_TXID_LEN                12

#define STUN_BINDING_REQUEST         0x0001
#define STUN_BINDING_SUCCESS         0x0101
#define STUN_BINDING_ERROR           0x0111

#define STUN_ATTR_MAPPED_ADDRESS     0x0001
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020

/* The first four bytes of the magic cookie, big-endian: the XOR mask applied
 * to the port and (for IPv4) the address in XOR-MAPPED-ADDRESS. */
static const uint8_t stun_magic_be[4] = { 0x21, 0x12, 0xA4, 0x42 };

static int64_t stun_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Fill buf with len cryptographically-random bytes. Used for the transaction
 * ID, which both demultiplexes the response and (for IPv6) keys the address
 * XOR, so it must be unpredictable. */
static int stun_random(void *log_ctx, uint8_t *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        ssize_t r = getrandom(buf + got, len - got, 0);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        got += r;
    }
    if (got == len)
        return 0;

    /* Fallback for kernels/sandboxes without getrandom(2). */
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        got = 0;
        while (got < len) {
            ssize_t r = read(fd, buf + got, len - got);
            if (r <= 0) {
                if (r < 0 && errno == EINTR)
                    continue;
                break;
            }
            got += r;
        }
        close(fd);
        if (got == len)
            return 0;
    }

    avt_log(log_ctx, AVT_LOG_ERROR, "STUN: unable to obtain random bytes\n");
    return AVT_ERROR(EIO);
}

static void stun_build_request(uint8_t buf[STUN_HEADER_LEN],
                               const uint8_t txid[STUN_TXID_LEN])
{
    AVTBytestream bs = avt_bs_init(buf, STUN_HEADER_LEN);
    avt_bsw_u16b(&bs, STUN_BINDING_REQUEST);
    avt_bsw_u16b(&bs, 0); /* message length: no attributes */
    avt_bsw_u32b(&bs, STUN_MAGIC_COOKIE);
    avt_bsw_sbuf(&bs, txid, STUN_TXID_LEN);
}

/* Parse a received datagram as a STUN Binding response to our request, and on
 * success store the reflexive address (normalised to IPv6) in *mapped.
 *
 * Returns 0 on success. Returns AVT_ERROR(EAGAIN) for anything that is not our
 * response (too short, bad cookie, foreign transaction ID, non-success type) so
 * the caller keeps waiting rather than failing. Returns another negative
 * AVT_ERROR for a response that is ours but unusable. */
static int stun_parse_response(void *log_ctx,
                               const uint8_t *buf, size_t len,
                               const uint8_t txid[STUN_TXID_LEN],
                               struct sockaddr_in6 *mapped)
{
    if (len < STUN_HEADER_LEN)
        return AVT_ERROR(EAGAIN);

    uint16_t type = AVT_RB16(buf + 0);
    uint16_t mlen = AVT_RB16(buf + 2);
    uint32_t cookie = AVT_RB32(buf + 4);

    if (cookie != STUN_MAGIC_COOKIE)
        return AVT_ERROR(EAGAIN);
    if (memcmp(buf + 8, txid, STUN_TXID_LEN) != 0)
        return AVT_ERROR(EAGAIN); /* a different transaction; not ours */
    if (type == STUN_BINDING_ERROR) {
        avt_log(log_ctx, AVT_LOG_ERROR,
                "STUN: server returned a Binding error response\n");
        return AVT_ERROR(EPROTO);
    }
    if (type != STUN_BINDING_SUCCESS)
        return AVT_ERROR(EAGAIN);

    /* Clamp the attribute span to what actually arrived. */
    if ((size_t)mlen > len - STUN_HEADER_LEN)
        mlen = len - STUN_HEADER_LEN;

    const uint8_t *p = buf + STUN_HEADER_LEN;
    const uint8_t *end = p + mlen;

    bool have_xor = false, have_plain = false;
    uint16_t port = 0;
    uint8_t family = 0;
    uint8_t addr[16] = { 0 };

    while ((size_t)(end - p) >= 4) {
        uint16_t atype = AVT_RB16(p + 0);
        uint16_t alen  = AVT_RB16(p + 2);
        const uint8_t *val = p + 4;
        if ((size_t)alen > (size_t)(end - val))
            break;

        if (atype == STUN_ATTR_XOR_MAPPED_ADDRESS && alen >= 8) {
            /* 1 byte reserved, 1 byte family, 2 bytes X-Port, then X-Address.
             * Undo the XOR with the magic cookie (and, for IPv6, the txid). */
            uint8_t fam = val[1];
            port = (uint16_t)(((val[2] ^ stun_magic_be[0]) << 8) |
                               (val[3] ^ stun_magic_be[1]));
            if (fam == 0x01 && alen >= 8) {          /* IPv4 */
                for (int i = 0; i < 4; i++)
                    addr[i] = val[4 + i] ^ stun_magic_be[i];
                family = fam;
                have_xor = true;
            } else if (fam == 0x02 && alen >= 20) {  /* IPv6 */
                uint8_t key[16];
                memcpy(key + 0, stun_magic_be, 4);
                memcpy(key + 4, txid, 12);
                for (int i = 0; i < 16; i++)
                    addr[i] = val[4 + i] ^ key[i];
                family = fam;
                have_xor = true;
            }
        } else if (atype == STUN_ATTR_MAPPED_ADDRESS && alen >= 8 && !have_xor) {
            /* Legacy, non-XOR form; only used if no XOR-MAPPED-ADDRESS. */
            uint8_t fam = val[1];
            if (fam == 0x01 && alen >= 8) {
                port = AVT_RB16(val + 2);
                memcpy(addr, val + 4, 4);
                family = fam;
                have_plain = true;
            } else if (fam == 0x02 && alen >= 20) {
                port = AVT_RB16(val + 2);
                memcpy(addr, val + 4, 16);
                family = fam;
                have_plain = true;
            }
        }

        p += 4 + ((alen + 3u) & ~3u); /* attributes are padded to 4 bytes */
    }

    if (!have_xor && !have_plain) {
        avt_log(log_ctx, AVT_LOG_ERROR,
                "STUN: success response carried no mapped address\n");
        return AVT_ERROR(ENOENT);
    }

    memset(mapped, 0, sizeof(*mapped));
    mapped->sin6_family = AF_INET6;
    mapped->sin6_port = htons(port);
    if (family == 0x01) {
        uint8_t *d = (uint8_t *)&mapped->sin6_addr;
        memset(d, 0, 10);
        d[10] = 0xff; /* ::ffff:a.b.c.d */
        d[11] = 0xff;
        memcpy(d + 12, addr, 4);
    } else {
        memcpy(&mapped->sin6_addr, addr, 16);
    }

    return 0;
}

int avt_stun_query(void *log_ctx, int fd,
                   const struct sockaddr *server, socklen_t server_len,
                   struct sockaddr_in6 *mapped, int64_t timeout_ms)
{
    int ret;

    if (fd < 0 || !server || !mapped)
        return AVT_ERROR(EINVAL);
    if (timeout_ms <= 0)
        timeout_ms = 3000;

    uint8_t txid[STUN_TXID_LEN];
    ret = stun_random(log_ctx, txid, sizeof(txid));
    if (ret < 0)
        return ret;

    uint8_t req[STUN_HEADER_LEN];
    stun_build_request(req, txid);

    const int64_t deadline = stun_now_ms() + timeout_ms;
    int64_t rto = 500; /* RFC 8489 default initial RTO, doubled per retransmit */

    for (int attempt = 0; attempt < 7; attempt++) {
        int64_t now = stun_now_ms();
        if (now >= deadline)
            break;

        if (sendto(fd, req, sizeof(req), 0, server, server_len) < 0)
            return avt_handle_errno(log_ctx, "STUN: sendto failed: %i %s\n");

        /* Wait for a matching response, bounded by this attempt's RTO and the
         * overall deadline; ignore any non-matching datagrams in the meantime. */
        int64_t wait_until = now + rto;
        if (wait_until > deadline)
            wait_until = deadline;

        for (;;) {
            int64_t left = wait_until - stun_now_ms();
            if (left <= 0)
                break; /* retransmit */

            struct pollfd pfd = { .fd = fd, .events = POLLIN };
            int pr = poll(&pfd, 1, (int)left);
            if (pr < 0) {
                if (errno == EINTR)
                    continue;
                return avt_handle_errno(log_ctx, "STUN: poll failed: %i %s\n");
            }
            if (pr == 0)
                break; /* retransmit */

            uint8_t resp[576];
            ssize_t r = recv(fd, resp, sizeof(resp), 0);
            if (r < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;
                return avt_handle_errno(log_ctx, "STUN: recv failed: %i %s\n");
            }

            ret = stun_parse_response(log_ctx, resp, r, txid, mapped);
            if (ret == AVT_ERROR(EAGAIN))
                continue; /* foreign/stale datagram, keep waiting */
            return ret;   /* success, or a hard error specific to our response */
        }

        rto *= 2;
    }

    avt_log(log_ctx, AVT_LOG_ERROR,
            "STUN: no response from server within %li ms\n", (long)timeout_ms);
    return AVT_ERROR(ETIMEDOUT);
}
