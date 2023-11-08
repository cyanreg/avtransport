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

#ifndef LIBAVTRANSPORT_COMMON
#define LIBAVTRANSPORT_COMMON

#include <stdatomic.h>

#include <libavtransport/avtransport.h>

enum AVTProtocolType {
    AVT_UNKNOWN,
    AVT_UDP,
    AVT_QUIC,
};

typedef struct AVTStreamPriv {
    enum AVTCodecID codec_id;
} AVTStreamPriv;

struct AVTContext {
    struct {
        AVTConnection **conn;
        int nb_conn;
        struct AVTOutputContext *ctx;
        atomic_uint seq;
        uint64_t epoch;
    } output;

    struct {
        AVTConnection *conn;
        struct AVTInputContext *ctx;
        AVTInputCallbacks proc;
        void *cb_opaque;
        atomic_uint seq;
        uint64_t epoch;
    } input;

    AVTStream **stream;
    int nb_stream;

    AVTContextOptions opts;
};

int avt_parse_address(const char *path, enum AVTProtocolType *proto,
                      uint8_t dst_ip[16], uint16_t *dst_port);

AVTStream *avt_alloc_stream(AVTContext *ctx, uint16_t id);
AVTStream *avt_find_stream(AVTContext *ctx, uint16_t id);

#if defined(__GNUC__) || defined(__clang__)
#define avt_printf_format(fmtpos, attrpos) __attribute__((__format__(__printf__, fmtpos, attrpos)))
#else
#define avt_printf_format(fmtpos, attrpos)
#endif

void avt_log(void *ctx, enum AVTLogLevel level, const char *fmt, ...) avt_printf_format(3, 4);

#endif
