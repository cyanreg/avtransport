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

#ifndef LIBAVTRANSPORT_COMMON_HEADER
#define LIBAVTRANSPORT_COMMON_HEADER

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <errno.h>
#include <stdbool.h>

#include <libavtransport/utils.h>
#include <libavtransport/rational.h>
#include <libavtransport/packet_enums.h>
#include <libavtransport/packet_data.h>

enum AVTCodecID {
    AVT_CODEC_RAW_VIDEO = 1,
    AVT_CODEC_FFV1,
    AVT_CODEC_AV1,
    AVT_CODEC_VP9,
    AVT_CODEC_H264,
    AVT_CODEC_H265,
    AVT_CODEC_DIRAC,
    AVT_CODEC_TIFF,
    AVT_CODEC_PNG,
    AVT_CODEC_JPEG,
    AVT_CODEC_SVG,

    AVT_CODEC_RAW_AUDIO = 32768,
    AVT_CODEC_OPUS,
    AVT_CODEC_AAC,
    AVT_CODEC_AC3,
    AVT_CODEC_FLAC,

    AVT_CODEC_ASS = 65536,
};

typedef struct AVTStream {
    uint32_t id;
    enum AVTCodecID codec_id;
    AVTMetadata *meta;

    /* Duration in nanoseconds, if known. */
    uint64_t duration;

    AVTVideoInfo video_info;
    AVTBuffer *icc_profile;

    enum AVTStreamFlags flags;
    AVTRational timebase;
    int64_t bitrate;

    AVTBuffer *init_data;

    struct AVTStream *related_to;
    struct AVTStream *derived_from;

    /* libavtransport private stream data. Do not touch or use. */
    struct AVTStreamPriv *private;
} AVTStream;

typedef struct AVTPacket {
    AVTBuffer *data;

    enum AVTFrameType type;
    int64_t pts;
    int64_t dts;
    int64_t duration;
} AVTPacket;

#endif
