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

#ifndef LIBQPROTO_COMMON
#define LIBQPROTO_COMMON

#include <libqproto/common.h>
#include <libqproto/input.h>
#include <libqproto/output.h>
#include <stdatomic.h>

enum PQProtocolType {
    PQ_UNKNOWN,
    PQ_UDP,
    PQ_QUIC,
};

typedef struct PQStreamPriv {
    enum QprotoCodecID codec_id;
} PQStreamPriv;

struct QprotoContext {
    struct {
        QprotoOutputDestination dst;
        const struct PQOutput *cb;
        struct PQOutputContext *ctx;
        atomic_uint seq;
        uint64_t epoch;
    } dst;

    struct {
        QprotoInputSource src;
        const struct PQInput *cb;
        struct PQInputContext *ctx;
        QprotoInputCallbacks proc;
        void *cb_opaque;
        atomic_uint seq;
        uint64_t epoch;
    } src;

    QprotoStream **stream;
    int nb_stream;

    QprotoContextOptions opts;
};

enum PQPacketType {
    /* Indicates the 8 least significant bits are a mask */
    QP_PKT_FLAG_LSB_MASK = (1 << 31),

    QP_PKT_SESSION_START = 0x5170,
    QP_PKT_TIME_SYNC = 0x1,
    QP_PKT_STREAM_REG = 0x2,

    QP_PKT_STREAM_INIT = 0x3,
    QP_PKT_STREAM_INIT_PRT = 0x4,
    QP_PKT_STREAM_INIT_SEG = 0x5,
    QP_PKT_STREAM_INIT_END = 0x6,
    QP_PKT_STREAM_INIT_FEC = 0x7,

    QP_PKT_VIDEO_INFO = 0x8,
    QP_PKT_INDEX = 0x9,

    QP_PKT_METADATA = 0xA,
    QP_PKT_METADATA_PRT = 0xB,
    QP_PKT_METADATA_SEG = 0xC,
    QP_PKT_METADATA_END = 0xD,
    QP_PKT_METADATA_FEC = 0xE,

    QP_PKT_ICC = 0x10,
    QP_PKT_ICC_PRT = 0x11,
    QP_PKT_ICC_SEG = 0x12,
    QP_PKT_ICC_END = 0x13,
    QP_PKT_ICC_FEC = 0x14,

    QP_PKT_FONT = 0x20,
    QP_PKT_FONT_PRT = 0x21,
    QP_PKT_FONT_SEG = 0x22,
    QP_PKT_FONT_END = 0x23,
    QP_PKT_FONT_FEC = 0x24,

    QP_PKT_FEC_GROUP_REG = 0x30,
    QP_PKT_FEC_GROUP_DATA = 0x31,

    QP_PKT_VIDEO_ORIENTATION = 0x40,

    QP_PKT_STREAM_FEC_DATA = 0xFC,
    QP_PKT_STREAM_FEC_END = 0xFD,

    QP_PKT_STREAM_SEG_DATA = 0xFE,
    QP_PKT_STREAM_SEG_END = 0xFF,

    QP_PKT_STREAM_DATA = 0x01FF | QP_PKT_FLAG_LSB_MASK,
    QP_PKT_USER_DATA = 0x40FF | QP_PKT_FLAG_LSB_MASK,

    QP_PKT_STREAM_DURATION = 0xF000,
    QP_PKT_EOS = 0xFFFF,

    QP_PKT_REV_USER_DATA = 0x50FF | QP_PKT_FLAG_LSB_MASK,
    QP_PKT_REV_CONTROL = 0x8001,
    QP_PKT_REV_FEEDBACK = 0x8002,
    QP_PKT_REV_RESEND = 0x8003,
    QP_PKT_REV_STREAM_CONTROL = 0x8004,
};

int pq_parse_address(const char *path, enum PQProtocolType *proto,
                     uint8_t dst_ip[16], uint16_t *dst_port);

QprotoStream *qp_alloc_stream(QprotoContext *qp, uint16_t id);

#if defined(__GNUC__) || defined(__clang__)
#define pq_printf_format(fmtpos, attrpos) __attribute__((__format__(__printf__, fmtpos, attrpos)))
#else
#define pq_printf_format(fmtpos, attrpos)
#endif

void pq_log(void *ctx, enum QPLogLevel level, const char *fmt, ...) pq_printf_format(3, 4);

#endif
