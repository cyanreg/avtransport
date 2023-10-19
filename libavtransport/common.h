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

#ifndef LIBAVTRANSPORT_COMMON
#define LIBAVTRANSPORT_COMMON

#include <libavtransport/common.h>
#include <libavtransport/input.h>
#include <libavtransport/output.h>
#include <stdatomic.h>

enum PQProtocolType {
    PQ_UNKNOWN,
    PQ_UDP,
    PQ_QUIC,
};

typedef struct PQStreamPriv {
    enum AVTCodecID codec_id;
} PQStreamPriv;

struct AVTContext {
    struct {
        AVTOutputDestination dst;
        const struct PQOutput *cb;
        struct PQOutputContext *ctx;
        atomic_uint seq;
        uint64_t epoch;
    } dst;

    struct {
        AVTInputSource src;
        const struct PQInput *cb;
        struct PQInputContext *ctx;
        AVTInputCallbacks proc;
        void *cb_opaque;
        atomic_uint seq;
        uint64_t epoch;
        struct PQReorder *rctx;
    } src;

    AVTStream **stream;
    int nb_stream;

    AVTContextOptions opts;
};

enum PQPacketType {
    /* Indicates the 8 least significant bits are a mask */
    AVT_PKT_FLAG_LSB_MASK = (1 << 31),

    AVT_PKT_SESSION_START = 0x4156,

    AVT_PKT_STREAM_REG = 0x2,

    AVT_PKT_STREAM_INIT     = 0x3,
    AVT_PKT_STREAM_INIT_PRT = 0x4,
    AVT_PKT_STREAM_INIT_SEG = 0x5,
    AVT_PKT_STREAM_INIT_END = 0x6,
    AVT_PKT_STREAM_INIT_FEC = 0x7,

    AVT_PKT_VIDEO_INFO = 0x8,
    AVT_PKT_INDEX      = 0x9,
    AVT_PKT_TIME_SYNC = 0,

    AVT_PKT_METADATA = 0xA,
    AVT_PKT_METADATA_PRT = 0xB,
    AVT_PKT_METADATA_SEG = 0xC,
    AVT_PKT_METADATA_END = 0xD,
    AVT_PKT_METADATA_FEC = 0xE,

    AVT_PKT_ICC = 0x10,
    AVT_PKT_ICC_PRT = 0x11,
    AVT_PKT_ICC_SEG = 0x12,
    AVT_PKT_ICC_END = 0x13,
    AVT_PKT_ICC_FEC = 0x14,

    AVT_PKT_FONT = 0x20,
    AVT_PKT_FONT_PRT = 0x21,
    AVT_PKT_FONT_SEG = 0x22,
    AVT_PKT_FONT_END = 0x23,
    AVT_PKT_FONT_FEC = 0x24,

    AVT_PKT_FEC_GROUP_REG = 0x30,
    AVT_PKT_FEC_GROUP_DATA = 0x31,

    AVT_PKT_VIDEO_ORIENTATION = 0x40,

    AVT_PKT_STREAM_FEC_DATA = 0xFC,
    AVT_PKT_STREAM_FEC_END = 0xFD,

    AVT_PKT_STREAM_SEG_DATA = 0xFE,
    AVT_PKT_STREAM_SEG_END = 0xFF,

    AVT_PKT_STREAM_DATA = 0x01FF | AVT_PKT_FLAG_LSB_MASK,
    AVT_PKT_USER_DATA = 0x40FF | AVT_PKT_FLAG_LSB_MASK,

    AVT_PKT_STREAM_DURATION = 0xF000,
    AVT_PKT_EOS = 0xFFFF,

    AVT_PKT_REV_USER_DATA = 0x50FF | AVT_PKT_FLAG_LSB_MASK,
    AVT_PKT_REV_CONTROL = 0x8001,
    AVT_PKT_REV_FEEDBACK = 0x8002,
    AVT_PKT_REV_RESEND = 0x8003,
    AVT_PKT_REV_STREAM_CONTROL = 0x8004,
};

int pq_parse_address(const char *path, enum PQProtocolType *proto,
                     uint8_t dst_ip[16], uint16_t *dst_port);

AVTStream *avt_alloc_stream(AVTContext *ctx, uint16_t id);
AVTStream *avt_find_stream(AVTContext *ctx, uint16_t id);

#if defined(__GNUC__) || defined(__clang__)
#define pq_printf_format(fmtpos, attrpos) __attribute__((__format__(__printf__, fmtpos, attrpos)))
#else
#define pq_printf_format(fmtpos, attrpos)
#endif

void pq_log(void *ctx, enum AVTLogLevel level, const char *fmt, ...) pq_printf_format(3, 4);

#endif
