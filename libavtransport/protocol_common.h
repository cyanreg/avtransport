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

#ifndef AVTRANSPORT_PROTOCOL_COMMON
#define AVTRANSPORT_PROTOCOL_COMMON

#include "connection_internal.h"
#include "utils_internal.h"

/* High level interface */
typedef struct AVTProtocolCtx AVTProtocolCtx;
typedef struct AVTProtocol {
    const char *name;
    enum AVTProtocolType type;

    /* Initialize a context */
    int (*init)(AVTContext *ctx, AVTProtocolCtx **p, AVTAddress *addr);

    /* Attempt to add a secondary destination, NULL if unsupported */
    int (*add_dst)(AVTProtocolCtx *p, AVTAddress *addr);
    /* Removes a secondary destination, NULL if unsupported */
    int (*rm_dst)(AVTProtocolCtx *p, AVTAddress *addr);

    /* Return the maximum packet length */
    int64_t (*get_max_pkt_len)(AVTProtocolCtx *p);

    /* Send. Returns positive offset on success, otherwise negative error.
     * Returns offset to which packet was written to. */
    int64_t (*send_packet)(AVTProtocolCtx *p, AVTPktd *pkt, int64_t timeout);

    /* Send a sequence of packets. Returns positive offset on success,
       otherwise negative error */
    int64_t (*send_seq)(AVTProtocolCtx *p, AVTPacketFifo *seq, int64_t timeout);

    /* Overwrite a packet at a given offset */
    int (*update_packet)(AVTProtocolCtx *p,
                         union AVTPacketData pkt, AVTBuffer *pl,
                         void **series, int64_t pos);

    /* Receive a packet. Returns offset after reading. */
    int64_t (*receive_packet)(AVTProtocolCtx *p,
                              union AVTPacketData *pkt, AVTBuffer **pl,
                              int64_t timeout);

    /* Seek to a place in the stream */
    int64_t (*seek)(AVTProtocolCtx *p, int64_t off, uint32_t seq,
                    int64_t ts, bool ts_is_dts);

    /* Flush buffered data */
    int (*flush)(AVTProtocolCtx *p, int64_t timeout);

    /* Close */
    int (*close)(AVTProtocolCtx **p);
} AVTProtocol;

int avt_protocol_init(AVTContext *ctx, const AVTProtocol **_p,
                      AVTProtocolCtx **p_ctx, AVTAddress *addr);

#endif /* AVTRANSPORT_PROTOCOL_COMMON */
