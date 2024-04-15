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

#include "bytestream.h"
#include "utils_internal.h"
#include "io_common.h"

/* High level interface */
typedef struct AVTProtocolCtx AVTProtocolCtx;
typedef struct AVTProtocol {
    const char *name;
    enum AVTProtocolType type;

    /* Initialize a context */
    int (*init)(AVTContext *ctx, AVTProtocolCtx **p, AVTAddress *addr,
                const AVTIO *io, AVTIOCtx *io_ctx);

    /* Attempt to add a secondary destination, NULL if unsupported */
    int (*add_dst)(AVTProtocolCtx *p, AVTAddress *addr);
    /* Removes a secondary destination, NULL if unsupported */
    int (*rm_dst)(AVTProtocolCtx *p, AVTAddress *addr);

    /* Return the maximum packet length */
    int (*get_max_pkt_len)(AVTProtocolCtx *p, size_t *mtu);

    /* Send. Returns positive offset on success, otherwise negative error.
     * Returns offset to which packet was written to. */
    int (*send_packet)(AVTProtocolCtx *p, AVTPktd *pkt, int64_t timeout);

    /* Send a sequence of packets. Returns positive offset on success,
       otherwise negative error */
    int (*send_seq)(AVTProtocolCtx *p, AVTPacketFifo *seq, int64_t timeout);

    /* Overwrite a packet at a given offset */
    int (*update_packet)(AVTProtocolCtx *p,
                         union AVTPacketData pkt, AVTBuffer *pl,
                         void **series, int64_t pos);

    /* Receive a packet. Returns offset after reading. */
    int (*receive_packet)(AVTProtocolCtx *p,
                          union AVTPacketData *pkt, AVTBuffer **pl,
                          int64_t timeout);

    /* Seek to a place in the stream */
    int (*seek)(AVTProtocolCtx *p, int64_t off, uint32_t seq,
                int64_t ts, bool ts_is_dts);

    /* Flush buffered data */
    int (*flush)(AVTProtocolCtx *p, int64_t timeout);

    /* Close */
    int (*close)(AVTProtocolCtx **p);
} AVTProtocol;

COLD int avt_protocol_init(AVTContext *ctx, const AVTProtocol **_p,
                           AVTProtocolCtx **p_ctx, AVTAddress *addr,
                           const AVTIO *io, AVTIOCtx *io_ctx);

typedef struct AVTIndexContext {
    AVTIndexEntry *index;
    int nb_index;
    int nb_alloc_index;
    uint64_t nb_index_total;
    uint64_t nb_index_max;
} AVTIndexContext;

int avt_index_list_config(AVTIndexContext *ic, uint64_t nb_index_max);
int avt_index_list_parse(AVTIndexContext *ic, AVTBytestream *bs,
                         AVTStreamIndex *pkt);

#endif /* AVTRANSPORT_PROTOCOL_COMMON */
