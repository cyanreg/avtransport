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

#ifndef LIBQPROTO_COMMON_HEADER
#define LIBQPROTO_COMMON_HEADER

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <errno.h>

typedef struct QprotoContext QprotoContext;
typedef struct QprotoBuffer QprotoBuffer;
typedef struct QprotoMetadata QprotoMetadata;

/* Rational data type structure. */
typedef struct QprotoRational {
    int num;
    int den;
} QprotoRational;

enum QprotoConnectionType {
    /* Reverse connectivity for senders/receivers.
     * The context MUST already have an output or input initialized. */
    QPROTO_CONNECTION_REVERSE = 1,

    /* URL address in the form of:
     * <protocol>://<address>:<port>
     * <protocol> MUST be "udp", "udplite" OR "quic" */
    QPROTO_CONNECTION_URL,

    /* File path */
    QPROTO_CONNECTION_FILE,

    /* Raw reader/writer using callbacks. */
    QPROTO_CONNECTION_CALLBACKS,
};

typedef struct QprotoContextOptions {
    void *log_opaque;
    void (*log_cb)(void *log_opaque, const char *format, va_list args, int error);
} QprotoContextOptions;

enum QprotoCodecID {
    QPROTO_CODEC_RAW_VIDEO = 1,
    QPROTO_CODEC_AV1,
    QPROTO_CODEC_H264,
    QPROTO_CODEC_DIRAC,

    QPROTO_CODEC_RAW_AUDIO = 32768,
    QPROTO_CODEC_OPUS,
    QPROTO_CODEC_AAC,

    QPROTO_CODEC_ASS = 65536,
};

enum QprotoStreamFlags {
    QPROTO_STREAM_STILL_PICTURE = 1,
};

typedef struct QprotoStream {
    uint32_t id;
    enum QprotoCodecID codec_id;
    QprotoMetadata *meta;

    enum QprotoStreamFlags stream_flags;
    QprotoBuffer *icc_profile;
    QprotoRational timebase;
    int64_t bitrate;

    QprotoBuffer *extradata;

    struct QprotoStream *related_to;
    struct QprotoStream *derived_from;
} QprotoStream;

typedef struct QprotoPacket {
    QprotoBuffer *buffer;

    int64_t pts;
    int64_t dts;
    int64_t duration;
} QprotoPacket;

/* Allocate a Qproto context with the given context options. */
int qp_init(QprotoContext **qp, QprotoContextOptions *opts);

/* Uninitialize a context, closing all connections and files gracefully,
 * and free all memory used. */
void qp_close(QprotoContext **qp);

/* Create a reference counted buffer from existing data. */
QprotoBuffer *qp_buffer_create(void *data, size_t len,
                               void *opaque, void (*free)(void *opaque, void *data));

/* Default freeing callback for buffers that simply calls free(data) */
void qp_buffer_default_free(void *opaque, void *data);

/* Create and allocate a reference counted buffer. */
QprotoBuffer *qp_buffer_alloc(size_t len);

/* References the buffer. Returns the total references. */
int qp_buffer_reference(QprotoBuffer *buffer);

/* Access the data in a buffer. Does not reference it. */
void *qp_buffer_get_data(QprotoBuffer *buffer, size_t *len);

/* Unreference a reference counted buffer. */
void qp_buffer_unref(QprotoBuffer **buffer);

/* All functions return negative values for errors.
 * This is a wrapper that's used to convert standard stderr values into
 * negatives. */
#define QP_ERROR(err) (-(err))

#endif
