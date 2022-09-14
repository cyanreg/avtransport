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
#include <stdbool.h>

#include <libqproto/video.h>
#include <libqproto/utils.h>

typedef struct QprotoContext  QprotoContext;
typedef struct QprotoMetadata QprotoMetadata;

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

    /* Socket */
    QPROTO_CONNECTION_SOCKET,

    /* File descriptor */
    QPROTO_CONNECTION_FD,

    /* Raw reader/writer using callbacks. */
    QPROTO_CONNECTION_CALLBACKS,
};

enum QPLogLevel {
    QP_LOG_QUIET    = -(1 << 0),
    QP_LOG_FATAL    =  (0 << 0),
    QP_LOG_ERROR    = +(1 << 0),
    QP_LOG_WARN     = +(1 << 1),
    QP_LOG_INFO     = +(1 << 2),
    QP_LOG_VERBOSE  = +(1 << 3),
    QP_LOG_DEBUG    = +(1 << 4),
    QP_LOG_TRACE    = +(1 << 5),
};

typedef struct QprotoContextOptions {
    void *log_opaque;
    void (*log_cb)(void *log_opaque, enum QPLogLevel level,
                   const char *format, va_list args, int error);
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

enum QprotoFrameType {
    QPROTO_KEYFRAME = 0x80,
    QPROTO_S_FRAME = 0x40,
};

enum QprotoStreamFlags {
    QPROTO_STREAM_STILL_PICTURE = 1,
};

typedef struct QprotoStream {
    uint32_t id;
    enum QprotoCodecID codec_id;
    QprotoMetadata *meta;

    /* Duration in nanoseconds, if known. */
    uint64_t duration;

    QprotoStreamVideoInfo video_info;
    QprotoBuffer *icc_profile;

    enum QprotoStreamFlags flags;
    QprotoRational timebase;
    int64_t bitrate;

    QprotoBuffer *init_data;

    struct QprotoStream *related_to;
    struct QprotoStream *derived_from;

    /* libqproto private stream data. Do not use. */
    struct PQStreamPriv *private;
} QprotoStream;

typedef struct QprotoPacket {
    QprotoBuffer *data;

    enum QprotoFrameType type;
    int64_t pts;
    int64_t dts;
    int64_t duration;
} QprotoPacket;

/* Allocate a Qproto context with the given context options. */
int qp_init(QprotoContext **qp, QprotoContextOptions *opts);

/* Uninitialize a context, closing all connections and files gracefully,
 * and free all memory used. */
void qp_close(QprotoContext **qp);

#endif
