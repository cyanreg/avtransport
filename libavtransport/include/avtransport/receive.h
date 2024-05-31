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

#ifndef AVTRANSPORT_RECEIVE_H
#define AVTRANSPORT_RECEIVE_H

#include "connection.h"
#include "stream.h"
#include "utils.h"

typedef struct AVTReceiveOptions {
    ;
} AVTReceiveOptions;

/* List of callbacks. All are optional. */
typedef struct AVTReceiveCallbacks {
    /**
     * Stream registration/updating callbacks.
     */
    int  (*stream_register_cb)(void *opaque, AVTStream *st);
    void (*stream_update_cb)(void *opaque, AVTStream *st);

    /**
     * Font data
     * st can be NULL, in which case the font applies to all streams.
     */
    void (*font_register_cb)(void *opaque, AVTStream *st,
                             AVTBuffer *data, const char name[252]);

    void (*time_sync_cb)(void *opaque, uint64_t epoch);

    void (*metadata_cb)(void *opaque, AVTStream *st, AVTMetadata *meta);
    int  (*user_pkt_cb)(void *opaque, AVTBuffer *data, uint16_t descriptor,
                        uint16_t user, uint32_t seq);

    void (*duration_cb)(void *opaque, int64_t duration_nsec);
    void (*stream_close_cb)(void *opaque, AVTStream *st);

    /**
     * ============== Callbacks for complete packets ==============
     * This provides a full packet, continuous, error corrected and
     * called in incremental order.
     */
    int  (*stream_pkt_cb)(void *opaque, AVTStream *st, AVTPacket pkt);

#if 0
    /*
     * ============== Callbacks for incomplete packets ==============
     * This API provides users the ability to receive and present packets
     * with minimal latency. Error correction will not be performed
     * (unless error correction for a segment is available immediately),
     * nor will any concatenation of segments be done.
     *
     * Users can use AVTPacket->total_size to know the total finished size
     * of the packet, and using the offset argument, can determine the
     * position of the segment.
     *
     * stream_pkt_cb will still be called with final, assembled, corrected
     * and incrementing packets.
     */
    int (*stream_pkt_start_cb)(void *opaque, AVTStream *st, AVTPacket pkt,
                               int present);
    int (*stream_pkt_seg_cb)(void *opaque, AVTStream *st, AVTPacket pkt,
                             AVTBuffer *seg, size_t offset);
#endif

    /* Reports a timeout, with the number of nanoseconds since the last packet */
    void (*timeout)(void *opaque, uint64_t last_received);
} AVTReceiveCallbacks;

/* Open an AVTransport stream or a file for reading. */
AVT_API int avt_receive_open(AVTContext *ctx, AVTConnection *conn,
                             AVTReceiveCallbacks *cb, void *cb_opaque,
                             AVTReceiveOptions *opts);

/* Adjusts input options on the fly. */
AVT_API int avt_receive_set_options(AVTContext *ctx, AVTReceiveOptions *opts);

/* Close input and free all associated data with it. */
AVT_API int avt_receive_close(AVTContext *ctx);

#endif /* AVTRANSPORT_RECEIVE_H */
