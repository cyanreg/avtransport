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

#ifndef AVTRANSPORT_PROTOCOL_COMMON
#define AVTRANSPORT_PROTOCOL_COMMON

#include "connection_internal.h"
#include "connection_scheduler.h"

/* High level interface */
typedef struct AVTProtocolCtx AVTProtocolCtx;
typedef struct AVTProtocol {
    const char *name;
    enum AVTProtocolType type;

    /* Initialize a context */
    int (*init)(AVTContext *ctx, AVTProtocolCtx **p, AVTAddress *addr);

    /* Attempt to add a secondary destination, NULL if unsupported */
    int (*add_dst)(AVTContext *ctx, AVTProtocolCtx *p, AVTAddress *addr);

    /* Removes a secondary destination, NULL if unsupported */
    int (*rm_dst)(AVTContext *ctx, AVTProtocolCtx *p, AVTAddress *addr);

    /* Return the maximum packet length */
    uint32_t (*get_max_pkt_len)(AVTContext *ctx, AVTProtocolCtx *p);

    /* Send. Returns positive offset on success, otherwise negative error.
     * Returns offset to which packet was written to. */
    int64_t (*send_packet)(AVTContext *ctx, AVTProtocolCtx *p,
                           union AVTPacketData pkt, AVTBuffer *pl);

    /* Send a bucket of packets. Returns positive offset on success,
       otherwise negative error */
    int64_t (*send_packets)(AVTContext *ctx, AVTProtocolCtx *p,
                            AVTPacketFifo *seq);

    /* Overwrite a packet at a given offset */
    int (*update_packet)(AVTContext *ctx, AVTProtocolCtx *p,
                         union AVTPacketData pkt, AVTBuffer *pl,
                         void **series, int64_t pos);

    /* Receive a packet. Returns offset after reading. */
    int (*receive_packet)(AVTContext *ctx, AVTProtocolCtx *p,
                          union AVTPacketData *pkt, AVTBuffer **pl);

    /* Seek to a place in the stream */
    int64_t (*seek)(AVTContext *ctx, AVTProtocolCtx *p,
                    int64_t off, uint32_t seq,
                    int64_t ts, bool ts_is_dts);

    /* Flush buffered data */
    int (*flush)(AVTContext *ctx, AVTProtocolCtx *p);

    /* Close */
    int (*close)(AVTContext *ctx, AVTProtocolCtx **p);
} AVTProtocol;

int avt_protocol_init(AVTContext *ctx, const AVTProtocol **_p,
                      AVTProtocolCtx **p_ctx, AVTAddress *addr);

#endif /* AVTRANSPORT_PROTOCOL_COMMON */
