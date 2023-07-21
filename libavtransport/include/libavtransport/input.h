/*
 * Copyright © 2022 Lynne
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the “Software”), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef LIBAVTRANSPORT_INPUT_HEADER
#define LIBAVTRANSPORT_INPUT_HEADER

#include <libavtransport/common.h>

typedef struct AVTInputSource {
    enum AVTConnectionType type;
    union {
        int reverse;
        const char *path;
        int fd;
        int (*read_data)(void *opaque, AVTBuffer *data, int64_t offset);
    };

    void *opaque;
} AVTInputSource;

/* List of callbacks. All are optional. */
typedef struct AVTInputCallbacks {
    int  (*stream_register_cb)(void *opaque, AVTStream *st);
    void (*stream_update_cb)(void *opaque, AVTStream *st);
    void (*font_register_cb)(void *opaque, AVTBuffer *data, const char *name);
    void (*epoch_cb)(void *opaque, uint64_t epoch);

    void (*metadata_cb)(void *opaque, AVTStream *st, AVTMetadata *meta);
    int  (*stream_pkt_cb)(void *opaque, AVTStream *st, AVTPacket pkt, int present);
    int  (*user_pkt_cb)(void *opaque, AVTBuffer *data, uint16_t descriptor,
                        uint16_t user, uint32_t seq);

    void (*duration_cb)(void *opaque, int64_t duration_nsec);
    void (*stream_close_cb)(void *opaque, AVTStream *st);

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

typedef struct AVTInputOptions {
    /**
     * Whether to always check and correct using Raptor codes in the headers.
     */
    bool always_test_headers;

    /**
     * Time in nanoseconds to wait on new data before calling the callback.
     * Default: infinite.
     */
    uint64_t timeout;
} AVTInputOptions;

/* Open an AVTransport stream or a file for reading. */
int avt_input_open(AVTContext *ctx, AVTInputSource *src,
                   AVTInputCallbacks *cb, void *cb_opaque,
                   AVTInputOptions *opts);

/* Adjusts input options on the fly. */
int avt_input_set_options(AVTContext *ctx, AVTInputOptions *opts);

/* Seek into the stream, if possible. */
int avt_input_seek(AVTContext *ctx, AVTStream *st, int64_t offset, int absolute);

/* Process a single packet and call its relevant callback. If no input is
 * available within the timeout duration (nanoseconds),
 * will return AVT_ERROR(EAGAIN).
 * Can be called multiple times from different threads. */
int avt_input_process(AVTContext *ctx, int64_t timeout);

/* Start a thread that will call avt_input_process as data becomes available.
 * Otherwise, avt_input_process() may be called manually. */
int avt_input_start_thread(AVTContext *ctx);

/* Close input and free all associated data with it. */
int avt_input_close(AVTContext *ctx);

#endif
