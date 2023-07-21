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

#ifndef LIBAVTRANSPORT_OUTPUT_HEADER
#define LIBAVTRANSPORT_OUTPUT_HEADER

#include <libavtransport/common.h>

typedef struct AVTOutputDestination {
    enum AVTConnectionType type;
    union {
        int reverse;
        const char *path;
        int fd;
        int (*write_data)(void *opaque, AVTBuffer *buf);
    };

    void *opaque;
} AVTOutputDestination;

typedef struct AVTOutputOptions {
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
} AVTOutputOptions;

/* Open an output and immediately send a stream session packet */
int avt_output_open(AVTContext *ctx, AVTOutputDestination *dst,
                    AVTOutputOptions *opts);

/* Send an epoch packet and set the epoch to use. */
int avt_output_set_epoch(AVTContext *ctx, uint64_t epoch);

/* Register a stream and allocate internal state for it.
 * To automatically assign a stream ID, set id to 65536.
 * If there's an existing stream with the same ID, will return NULL. */
AVTStream *avt_output_add_stream(AVTContext *ctx, uint16_t id);

/* Update a stream, (re-)emmitting a stream registration packet.
 * The id MUST match the one from avt_output_add_stream(). */
int avt_output_update_stream(AVTContext *ctx, AVTStream *st);

int avt_output_add_font(AVTContext *ctx, AVTBuffer *data, const char *name);

/* Write data to output. Can be called from multiple threads at once.
 * If compiled with threads, actual output happens in a different thread. */
int avt_output_write_stream_data(AVTContext *ctx, AVTStream *st,
                                 AVTPacket *pkt);

/* Write user data packets */
int avt_output_write_user_data(AVTContext *ctx, AVTBuffer *data,
                               uint8_t descriptor_flags, uint16_t user,
                               int prioritize);

int avt_output_close_stream(AVTContext *ctx, AVTStream *st);

int avt_output_control(AVTContext *ctx, void *opaque, int cease,
                       int resend_init, int error, uint8_t redirect[16],
                       uint16_t redirect_port, int seek_requested,
                       int64_t seek_offset, uint32_t seek_seq);

int avt_output_feedback(AVTContext *ctx, void *opaque, AVTStream *st,
                        uint64_t epoch_offset, uint64_t bandwidth,
                        uint32_t fec_corrections, uint32_t corrupt_packets,
                        uint32_t missing_packets);

int avt_output_close(AVTContext *ctx);

#endif
