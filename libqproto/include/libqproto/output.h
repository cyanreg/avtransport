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

#ifndef LIBQPROTO_OUTPUT_HEADER
#define LIBQPROTO_OUTPUT_HEADER

#include <libqproto/common.h>

typedef struct QprotoOutputDestination {
    enum QprotoConnectionType type;
    union {
        int reverse;
        const char *path;
        int fd;
        int (*write_output)(void *opaque, QprotoBuffer *buf);
    };

    void *opaque;
} QprotoOutputDestination;

typedef struct QprotoOutputOptions {
    /**
     * If the context has an input, include its packets into the output.
     */
    int dump_all;

    /**
     * How often to repeat all stream initialization data, in nanoseconds.
     * If output is a file, 0 means never.
     * If output is a stream, 0 means every 5 seconds.
     * Use -1 to completely disable.
     */
    int64_t repeat_init_rate;

    /**
     * Override the MTU size.
     * If output is a file, 0 means infinite.
     * If output is a stream, 0 means 378 until MTU discovery succeeds,
     * or 1280 if it fails. Must be more than or equal to 378.
     */
    size_t mtu_override;

    /**
     * Buffer to allow receivers to seek and request old packets.
     * 0 means none (default). Otherwise, the value is in total bytes.
     */
    size_t buffer_size;

    /**
     * Time in nanoseconds to wait on transmission before returning
     * a timeout.
     *
     * Default: infinite.
     */
    uint64_t timeout;
} QprotoOutputOptions;

/* Open an output and immediately send a stream session packet */
int qp_output_open(QprotoContext *qp, QprotoOutputDestination *dst,
                   QprotoOutputOptions *opts);

/* Send an epoch packet and set the epoch to use. */
int qp_output_set_epoch(QprotoContext *qp, uint64_t epoch);

/* Register a stream and allocate internal state for it.
 * To automatically assign a stream ID, set id to 65536.
 * If there's an existing stream with the same ID, will return NULL. */
QprotoStream *qp_output_add_stream(QprotoContext *qp, uint16_t id);

/* Update a stream, (re-)emmitting a stream registration packet.
 * The id MUST match the one from qp_output_add_stream(). */
int qp_output_update_stream(QprotoContext *qp, QprotoStream *st);

int qp_output_add_font(QprotoContext *qp, QprotoBuffer *data, const char *name);

/* Write data to output. Can be called from multiple threads at once.
 * If compiled with threads, actual output happens in a different thread. */
int qp_output_write_stream_data(QprotoContext *qp, QprotoStream *st,
                                QprotoPacket *pkt);
int qp_output_write_user_data(QprotoContext *qp, QprotoBuffer *data,
                              uint16_t descriptor, int prioritize);

int qp_output_close_stream(QprotoContext *qp, QprotoStream *st);

int qp_output_control(QprotoContext *qp, void *opaque, int cease,
                      int resend_init, int error, uint8_t redirect[16],
                      uint16_t redirect_port, int seek_requested,
                      int64_t seek_offset, uint32_t seek_seq);

int qp_output_feedback(QprotoContext *qp, void *opaque, QprotoStream *st,
                       uint64_t epoch_offset, uint64_t bandwidth,
                       uint32_t fec_corrections, uint32_t corrupt_packets,
                       uint32_t missing_packets);

int qp_output_close(QprotoContext *qp);

#endif
