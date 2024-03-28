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

#ifndef AVTRANSPORT_ADDRESS_H
#define AVTRANSPORT_ADDRESS_H

#include <uchar.h>

#include <avtransport/connection.h>

#define AVT_PROTOCOL_DATAGRAM      (0)
#define AVT_PROTOCOL_STREAM        (AVT_PROTOCOL_QUIC + 1)
#define AVT_PROTOCOL_FILE          (AVT_PROTOCOL_QUIC + 2)
#define AVT_PROTOCOL_CALLBACK_PKT  (AVT_PROTOCOL_QUIC + 3)
#define AVT_PROTOCOL_MAX           (AVT_PROTOCOL_QUIC + 4)

enum AVTAddressConnection {
    AVT_ADDRESS_NULL,
    AVT_ADDRESS_FILE,
    AVT_ADDRESS_FD,
    AVT_ADDRESS_UNIX,
    AVT_ADDRESS_URL,
    AVT_ADDRESS_SOCKET,
    AVT_ADDRESS_CALLBACK,
};

typedef struct AVTCallbacksPacket {
    void *opaque;
    int (*out)(void *opaque, union AVTPacketData pkt, AVTBuffer *buf);
    int (*in)(void *opaque, union AVTPacketData *pkt, AVTBuffer **buf, uint64_t seq);
} AVTCallbacksPacket;

typedef struct AVTCallbacksData {
    void *opaque;
    int64_t (*write)(void *opaque, uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                     AVTBuffer *payload);
    int64_t (*read)(void *opaque, AVTBuffer **data, size_t len, int64_t offset);
} AVTCallbacksData;

typedef struct AVTAddress {
    enum AVTAddressConnection type;

    /** FILE PATH/SOCKET PATH **/
    char8_t *path;

    /** FILE DESCRIPTOR/SOCKET/NETWORK SOCKET **/
    int fd;

    /** NETWORK **/
    uint8_t host[256];
    uint8_t uuid[16];
    bool listen; /* Server or client */

    /* Interface */
    char *interface;
    uint32_t interface_idx;

    /* Address */
    uint8_t ip[16]; /* Always mapped to IPv6 */
    uint16_t port;
    uint32_t scope; /* sin6_scope_id */

    enum AVTProtocolType proto;
    enum AVTProtocolMode mode;

    /* Options */
    struct {
        /* UDP receive/transmit buffer sizes */
        int rx_buf;
        int tx_buf;

        /* Default stream IDs */
        uint16_t *default_sid;
        int nb_default_sid;

        int64_t start_time; // 1ns timebase
    } opts;

    /** CALLBACKS **/
    AVTCallbacksPacket pcb;
    AVTCallbacksData dcb;
} AVTAddress;

/* Utilities */
int avt_parse_uuid(uint8_t out[16], const char *src);

int avt_addr_4to6(uint8_t ip6[16], uint32_t ip4);

int avt_addr_from_url(void *log_ctx, AVTAddress *addr,
                      bool server, const char *path);

int avt_addr_from_info(void *log_ctx, AVTAddress *addr, AVTConnectionInfo *info);

void avt_addr_free(AVTAddress *addr);

#endif /* AVTRANSPORT_ADDRESS_H */
