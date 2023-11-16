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

#ifndef LIBAVTRANSPORT_CONNECTION_INTERNAL
#define LIBAVTRANSPORT_CONNECTION_INTERNAL

#include "common.h"
#include "reorder.h"

#include <avtransport/packet_enums.h>

enum AVTProtocolMode {
    AVT_MODE_DEFAULT,
    AVT_MODE_PASSIVE,
    AVT_MODE_ACTIVE,
};

typedef struct AVTAddress {
    enum AVTProtocolType proto;
    enum AVTProtocolMode mode;
    uint8_t ip[16]; /* Always mapped to IPv6 */
    uint16_t port;
    const char *path;
    AVTMetadata *params;
} AVTAddress;

enum AVTIOType {
    /* Framing guaranteed on inputs, simple IOs */
    AVT_IO_NULL,
    AVT_IO_SOCKET,
    AVT_IO_CALLBACKS,

    /* Will perform framing on inputs */
    AVT_IO_FILE,
    AVT_IO_FD,
};

/* Low level interface */
typedef struct AVTIOCtx AVTIOCtx;
typedef struct AVTIO {
    const char *name;
    enum AVTIOType type;

    int (*init)(AVTContext *ctx, AVTIOCtx **io,
                AVTAddress *addr);

    uint32_t (*get_max_pkt_len)(AVTContext *ctx, struct AVTIOCtx *io);

    /* If only off is set, get the first packet at/after that offset.
     * If pts is set, get the stream data packet at/after the pts time
     * If seq is set, the get first packet at/after that seq */
    int (*seek)(AVTContext *ctx, AVTIOCtx *io,
                uint64_t off, uint32_t seq,
                int64_t ts, bool ts_is_dts);

    int (*read_input)(AVTContext *ctx, AVTIOCtx *io,
                      AVTBuffer *buf);

    int (*write_output)(AVTContext *ctx, AVTIOCtx *io,
                        uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                        AVTBuffer *payload);

    /* Flush data written */
    int (*flush)(AVTContext *ctx, AVTIOCtx *io);

    int (*close)(AVTContext *ctx, AVTIOCtx **io);
} AVTIO;

#define AVT_PROTOCOL_NOOP 0
#define AVT_PROTOCOL_PIPE (AVT_PROTOCOL_QUIC + 1)
#define AVT_PROTOCOL_FILE (AVT_PROTOCOL_PIPE + 1)
#define AVT_PROTOCOL_FD   (AVT_PROTOCOL_FILE + 1)

/* High level interface */
typedef struct AVTProtocolCtx AVTProtocolCtx;
typedef struct AVTProtocol {
    const char *name;
    enum AVTProtocolType type;

    /* Initialize a context */
    int (*init)(AVTContext *ctx, AVTProtocolCtx **p, AVTAddress *addr);

    /* Attempt to add a secondary destination, if the underlying implementation supports that */
    int (*add_dest)(AVTContext *ctx, AVTProtocolCtx *p, AVTAddress *addr);

    /* Return the maximum packet length */
    uint32_t (*get_max_pkt_len)(AVTContext *ctx, AVTProtocolCtx *p);

    /* Receive a packet */
    int (*receive_packet)(AVTContext *ctx, AVTProtocolCtx *p,
                          AVTBuffer *buf);

    /* Send */
    int (*send_packet)(AVTContext *ctx, AVTProtocolCtx *p,
                       uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                       AVTBuffer *buf);

    /* Seek to a place in the stream */
    int (*seek)(AVTContext *ctx, AVTProtocolCtx *p,
                uint64_t off, uint32_t seq,
                int64_t ts, bool ts_is_dts);

    /* Close */
    int (*close)(AVTContext *ctx, AVTProtocolCtx **p);
} AVTProtocol;

struct AVTConnection {
    const AVTProtocol *p;
    AVTProtocolCtx *p_ctx;

    AVTReorderBuffer *in_buffer;
};

/* Initialize an IO (protocols-use, mainly) */
int avt_init_io(AVTContext *ctx, const AVTIO **io, AVTIOCtx **io_ctx,
                AVTAddress *addr);



#endif
