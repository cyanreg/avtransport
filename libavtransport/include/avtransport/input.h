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

#ifndef AVTRANSPORT_RECEIVE_H
#define AVTRANSPORT_RECEIVE_H

#include "stream.h"
#include "utils.h"

typedef struct AVTInputOptions {
    /**
     * Time in nanoseconds to wait on new data before giving up.
     * Default: infinite.
     */
    uint64_t timeout;

    struct {
        /**
         * Whether to always check and correct using Raptor codes in the headers.
         * Default: false
         */
        bool always_test_headers;

        /**
         * Number of decoding steps. Higher values increase performance overhead,
         * but allow for better correction of errors.
         * Default: 1
         */
        uint8_t iterations;
    } ldpc;
} AVTInputOptions;

/* List of callbacks. All are optional. */
typedef struct AVTInputCallbacks {
    int  (*stream_register_cb)(void *opaque, AVTStream *st);
    void (*stream_update_cb)(void *opaque, AVTStream *st);
    void (*font_register_cb)(void *opaque, AVTBuffer *data, const char *name);
    void (*epoch_cb)(void *opaque, uint64_t epoch);

    void (*metadata_cb)(void *opaque, AVTStream *st, AVTMetadata *meta);
    int  (*user_pkt_cb)(void *opaque, AVTBuffer *data, uint16_t descriptor,
                        uint16_t user, uint32_t seq);

    void (*duration_cb)(void *opaque, int64_t duration_nsec);
    void (*stream_close_cb)(void *opaque, AVTStream *st);

    /*
     * ============== Callbacks for complete packets ==============
     * This provides a full packet, continuous, error corrected and
     * called in incremental order.
     */
    int  (*stream_pkt_cb)(void *opaque, AVTStream *st, AVTPacket pkt);

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
    int (*stream_pkt_start_cb)(void *opaque, AVTStream *st, AVTPacket pkt, int present);
    int (*stream_pkt_seg_cb)(void *opaque, AVTStream *st, AVTPacket pkt,
                             AVTBuffer *seg, size_t offset);

    /* Reports a timeout, with the number of nanoseconds since the last packet */
    void (*timeout)(void *opaque, uint64_t last_received);

    /**
     * Reverse signalling callbacks. May be hooked up directly to
     * the output context.
     */
    int (*control_cb)(AVTContext *ctx, void *opaque, int cease,
                      int resend_init, int error, uint8_t redirect[16],
                      uint16_t redirect_port, int seek_requested,
                      int64_t seek_offset, uint32_t seek_seq);

    int (*feedback_cb)(AVTContext *ctx, void *opaque, AVTStream *st,
                       uint64_t epoch_offset, uint64_t bandwidth,
                       uint32_t fec_corrections, uint32_t corrupt_packets,
                       uint32_t missing_packets);
} AVTInputCallbacks;

/* Open an AVTransport stream or a file for reading. */
AVT_API int avt_input_open(AVTContext *ctx, AVTConnection *conn,
                           AVTInputCallbacks *cb, void *cb_opaque,
                           AVTInputOptions *opts);

/* Adjusts input options on the fly. */
AVT_API int avt_input_set_options(AVTContext *ctx, AVTInputOptions *opts);

/* Seek into the stream, if possible. */
AVT_API int avt_input_seek(AVTContext *ctx, AVTStream *st, int64_t offset, int absolute);

/* Process a single packet and call its relevant callback. If no input is
 * available within the timeout duration (nanoseconds),
 * will return AVT_ERROR(EAGAIN).
 * Can be called multiple times from different threads. */
AVT_API int avt_input_process(AVTContext *ctx, int64_t timeout);

/* Start a thread that will call avt_input_process as data becomes available.
 * Otherwise, avt_input_process() may be called manually. */
AVT_API int avt_input_start_thread(AVTContext *ctx);

/* Close input and free all associated data with it. */
AVT_API int avt_input_close(AVTContext *ctx);

#endif /* AVTRANSPORT_RECEIVE_H */
