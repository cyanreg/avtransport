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

#ifndef LIBAVTRANSPORT_COMMON_HEADER
#define LIBAVTRANSPORT_COMMON_HEADER

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <errno.h>
#include <stdbool.h>

#include <libavtransport/video.h>
#include <libavtransport/utils.h>

typedef struct AVTContext  AVTContext;
typedef struct AVTMetadata AVTMetadata;

enum AVTConnectionType {
    /* Reverse connectivity for senders/receivers.
     * The context MUST already have an output or input initialized. */
    AVT_CONNECTION_REVERSE = 1,

    /* URL address in the form of:
     * <protocol>://<address>:<port>
     * <protocol> MUST be "udp", "udplite" OR "quic" */
    AVT_CONNECTION_URL,

    /* File path */
    AVT_CONNECTION_FILE,

    /* Socket */
    AVT_CONNECTION_SOCKET,

    /* File descriptor */
    AVT_CONNECTION_FD,

    /* Raw reader/writer using callbacks. */
    AVT_CONNECTION_CALLBACKS,
};

enum AVTLogLevel {
    AVT_LOG_QUIET    = -(1 << 0),
    AVT_LOG_FATAL    =  (0 << 0),
    AVT_LOG_ERROR    = +(1 << 0),
    AVT_LOG_WARN     = +(1 << 1),
    AVT_LOG_INFO     = +(1 << 2),
    AVT_LOG_VERBOSE  = +(1 << 3),
    AVT_LOG_DEBUG    = +(1 << 4),
    AVT_LOG_TRACE    = +(1 << 5),
};

typedef struct AVTContextOptions {
    void *log_opaque;
    void (*log_cb)(void *log_opaque, enum AVTLogLevel level,
                   const char *format, va_list args, int error);
} AVTContextOptions;

enum AVTCodecID {
    AVT_CODEC_RAW_VIDEO = 1,
    AVT_CODEC_AV1,
    AVT_CODEC_H264,
    AVT_CODEC_DIRAC,

    AVT_CODEC_RAW_AUDIO = 32768,
    AVT_CODEC_OPUS,
    AVT_CODEC_AAC,

    AVT_CODEC_ASS = 65536,
};

enum AVTFrameType {
    AVT_KEYFRAME = 0x80,
    AVT_S_FRAME = 0x40,
};

enum AVTStreamFlags {
    AVT_STREAM_STILL_PICTURE = 1,
};

typedef struct AVTStream {
    uint32_t id;
    enum AVTCodecID codec_id;
    AVTMetadata *meta;

    /* Duration in nanoseconds, if known. */
    uint64_t duration;

    AVTStreamVideoInfo video_info;
    AVTBuffer *icc_profile;

    enum AVTStreamFlags flags;
    AVTRational timebase;
    int64_t bitrate;

    AVTBuffer *init_data;

    struct AVTStream *related_to;
    struct AVTStream *derived_from;

    /* libavtransport private stream data. Do not use. */
    struct PQStreamPriv *private;
} AVTStream;

typedef struct AVTPacket {
    AVTBuffer *data;

    enum AVTFrameType type;
    int64_t pts;
    int64_t dts;
    int64_t duration;
} AVTPacket;

/* Allocate an AVTransport context with the given context options. */
int avt_init(AVTContext **ctx, AVTContextOptions *opts);

/* Uninitialize a context, closing all connections and files gracefully,
 * and free all memory used. */
void avt_close(AVTContext **ctx);

#endif
