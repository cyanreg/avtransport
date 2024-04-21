/*
 * Copyright © 2024, Lynne
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

#include <string.h>

#include "output_packet.h"

#include "config.h"
#include "packet_common.h"

#ifdef CONFIG_HAVE_LIBBROTLIENC
#include <brotli/encode.h>
#endif

static inline enum AVTDataCompression compress_method(enum AVTPktDescriptors desc,
                                                      AVTStream *st,
                                                      AVTOutputOptions *opts)
{
    switch (desc) {
    case AVT_PKT_STREAM_DATA:
        switch (st->codec_id) {
        case AVT_CODEC_ID_SVG: [[fallthrough]];
        case AVT_CODEC_ID_TIFF:
            if (!opts->compress ||
                (opts->compress & AVT_SENDER_COMPRESS_VIDEO))
                return AVT_DATA_COMPRESSION_ZSTD;
            break;
        case AVT_CODEC_ID_SRT: [[fallthrough]];
        case AVT_CODEC_ID_WEBVTT: [[fallthrough]];
        case AVT_CODEC_ID_ASS:
            if (!opts->compress ||
                (opts->compress & AVT_SENDER_COMPRESS_SUBS))
                return AVT_DATA_COMPRESSION_BROTLI;
            break;
        case AVT_CODEC_ID_OPUS: [[fallthrough]];
        case AVT_CODEC_ID_AAC: [[fallthrough]];
        case AVT_CODEC_ID_AC3: [[fallthrough]];
        case AVT_CODEC_ID_ATRAC1: [[fallthrough]];
        case AVT_CODEC_ID_ATRAC9: [[fallthrough]];
        case AVT_CODEC_ID_TAK: [[fallthrough]];
        case AVT_CODEC_ID_FLAC: [[fallthrough]];
        case AVT_CODEC_ID_RAW_AUDIO:
            if ((opts->compress & AVT_SENDER_COMPRESS_FORCE) &&
                ((!opts->compress) ||
                 (opts->compress & AVT_SENDER_COMPRESS_AUDIO)))
                return AVT_DATA_COMPRESSION_ZSTD;
            break;
        case AVT_CODEC_ID_THEORA: [[fallthrough]];
        case AVT_CODEC_ID_VP9: [[fallthrough]];
        case AVT_CODEC_ID_AV1: [[fallthrough]];
        case AVT_CODEC_ID_H264: [[fallthrough]];
        case AVT_CODEC_ID_H265: [[fallthrough]];
        case AVT_CODEC_ID_DIRAC: [[fallthrough]];
        case AVT_CODEC_ID_FFV1: [[fallthrough]];
        case AVT_CODEC_ID_PRORES_SD: [[fallthrough]];
        case AVT_CODEC_ID_PRORES_HQ: [[fallthrough]];
        case AVT_CODEC_ID_PRORES_LT: [[fallthrough]];
        case AVT_CODEC_ID_PRORES_PROXY: [[fallthrough]];
        case AVT_CODEC_ID_PRORES_4444: [[fallthrough]];
        case AVT_CODEC_ID_PRORES_4444_XQ: [[fallthrough]];
        case AVT_CODEC_ID_PRORES_RAW_HQ: [[fallthrough]];
        case AVT_CODEC_ID_PRORES_RAW_SD: [[fallthrough]];
        case AVT_CODEC_ID_JPEG: [[fallthrough]];
        case AVT_CODEC_ID_JPEG2000: [[fallthrough]];
        case AVT_CODEC_ID_JPEG2000_HT: [[fallthrough]];
        case AVT_CODEC_ID_PNG: [[fallthrough]];
        case AVT_CODEC_ID_RAW_VIDEO:
            if ((opts->compress & AVT_SENDER_COMPRESS_FORCE) &&
                ((!opts->compress) ||
                 (opts->compress & AVT_SENDER_COMPRESS_VIDEO)))
                return AVT_DATA_COMPRESSION_ZSTD;
            break;
        }
        break;
    case AVT_PKT_FONT_DATA:
        /* TODO: check if Woff2 and disable it, unless COMPRESS_FORCE */
        if (!opts->compress || opts->compress & AVT_SENDER_COMPRESS_AUX)
            return AVT_DATA_COMPRESSION_ZSTD;
        break;
    case AVT_PKT_LUT_ICC:
        if (!opts->compress || opts->compress & AVT_SENDER_COMPRESS_AUX)
            return AVT_DATA_COMPRESSION_ZSTD;
        break;
    case AVT_PKT_METADATA:
        if (!opts->compress || opts->compress & AVT_SENDER_COMPRESS_META)
            return AVT_DATA_COMPRESSION_BROTLI;
        break;
    default:
        break;
    };
    return AVT_DATA_COMPRESSION_NONE;
}

static int payload_process(AVTOutput *out, AVTPktd *p, AVTStream *st,
                           AVTBuffer *in, enum AVTPktDescriptors desc,
                           enum AVTDataCompression *data_compression)
{
    int err = 0;
    AVTBuffer *target;

    /* TODO: clip to supported levels */
    [[maybe_unused]] int lvl = out->opts.compress_level;
    enum AVTDataCompression method = compress_method(desc, st, &out->opts);

    uint8_t *src;
    size_t src_len;
    [[maybe_unused]] uint8_t *dst;
    [[maybe_unused]] size_t dst_len;
    [[maybe_unused]] size_t dst_size;

    /* Reset here just in case it OOMs */
    if (out->opts.hash) {
        XXH_errorcode ret = XXH3_128bits_reset(out->xxh_state);
        if (ret != XXH_OK)
            return AVT_ERROR(ENOMEM);
    }

    /* If a compression method is missing, fall back or just disable it */
#ifndef CONFIG_HAVE_LIBBROTLIENC
    if (method == AVT_DATA_COMPRESSION_BROTLI)
        method = AVT_DATA_COMPRESSION_ZSTD;
#endif

#ifndef CONFIG_HAVE_LIBZSTD
    if (method == AVT_DATA_COMPRESSION_ZSTD)
#ifdef CONFIG_HAVE_LIBBROTLIENC
        method = AVT_DATA_COMPRESSION_BROTLI;
#else
        method = AVT_DATA_COMPRESSION_NONE;
#endif
#endif

    switch (method) {
    case AVT_DATA_COMPRESSION_NONE:
        target = in;
        break;
    case AVT_DATA_COMPRESSION_ZSTD:
#ifdef CONFIG_HAVE_LIBZSTD
        src = avt_buffer_get_data(in, &src_len);

        dst_size = ZSTD_compressBound(src_len);

        /* TODO: use a buffer pool */
        dst = malloc(dst_size);
        if (!dst)
            return AVT_ERROR(ENOMEM);

        dst_len = ZSTD_compressCCtx(out->zstd_ctx, dst, dst_size, src, src_len, lvl);
        if (!dst_len) {
            avt_log(out, AVT_LOG_ERROR, "Error while compressing with ZSTD!\n");
            err = AVT_ERROR(EINVAL);
            free(dst);
            break;
        }

        target = avt_buffer_create(dst, dst_len, NULL, avt_buffer_default_free);
        if (!target) {
            free(dst);
            err = AVT_ERROR(ENOMEM);
            break;
        }
#else
        avt_log(out, AVT_LOG_ERROR, "ZSTD compression not enabled during build!\n");
        err = AVT_ERROR(EINVAL);
#endif
        break;
    case AVT_DATA_COMPRESSION_BROTLI:
#ifdef CONFIG_HAVE_LIBBROTLIENC
        src = avt_buffer_get_data(in, &src_len);

        dst_size = BrotliEncoderMaxCompressedSize(src_len);

        /* TODO: use a buffer pool */
        dst = malloc(dst_size);
        if (!dst)
            return AVT_ERROR(ENOMEM);

        /* Brotli has a braindead advanced API that
         * makes it really hard to use pooling. Since Brotli is mostly
         * used by text, meh, good enough for now. */
        if (!BrotliEncoderCompress(lvl, BROTLI_DEFAULT_WINDOW, BROTLI_DEFAULT_MODE,
                                   src_len, src, &dst_size, dst)) {
            avt_log(out, AVT_LOG_ERROR, "Error while compressing with Brotli!\n");
            err = AVT_ERROR(EINVAL);
            free(dst);
            break;
        }

        target = avt_buffer_create(dst, dst_size, NULL, avt_buffer_default_free);
        if (!target) {
            free(dst);
            err = AVT_ERROR(ENOMEM);
            break;
        }
#else
        avt_log(out, AVT_LOG_ERROR, "Brotli compression not enabled during build!\n");
        err = AVT_ERROR(EINVAL);
#endif
        break;
    default:
        avt_log(out, AVT_LOG_ERROR, "Unknown compression method: %i\n", method);
        return AVT_ERROR(EINVAL);
    };

    if (err >= 0)
        *data_compression = method;

    /* Generate has for the payload if enabled */
    if (out->opts.hash) {
        src = avt_buffer_get_data(target, &src_len);
        XXH3_128bits_update(out->xxh_state, src, src_len);
        XXH128_hash_t hash = XXH3_128bits_digest(out->xxh_state);
        XXH128_canonicalFromHash((XXH128_canonical_t *)&p->pl_hash, hash);
        p->pl_has_hash = true;
    }

    // TODO: get target refd into pktd

    return err;
}

static inline int send_pkt(AVTOutput *out,
                           union AVTPacketData pkt, AVTBuffer *pl)
{
    int ret = 0;

    for (int i = 0; i < out->nb_conn; i++) {
        int err = avt_connection_send(out->conn[i], pkt, pl);
        if (err < 0)
            ret = err;
    }

    return ret;
}

int avt_send_time_sync(AVTOutput *out)
{
    union AVTPacketData pkt = AVT_TIME_SYNC_HDR(
        .ts_clock_id = 0,
        .ts_clock_hz2 = 0,
        .epoch = out->epoch,
        .ts_clock_seq = 0,
        .ts_clock_hz = 0,
    );

    return send_pkt(out, pkt, nullptr);
}

int avt_send_stream_register(AVTOutput *out, AVTStream *st)
{
    union AVTPacketData pkt = AVT_STREAM_REGISTRATION_HDR(
        .stream_id = st->id,
        .related_stream_id = st->related_to ? st->related_to->id : UINT16_MAX,
        .derived_stream_id = st->derived_from ? st->derived_from->id : UINT16_MAX,
        .bandwidth = st->bitrate,
        .stream_flags = st->flags,

        .codec_id = st->codec_id,
        .timebase = st->timebase,
        .ts_clock_id = 0,
        .skip_preroll = 0,
        .init_packets = 0,
    );

    return send_pkt(out, pkt, nullptr);
}

int avt_send_stream_data(AVTOutput *out, AVTStream *st, AVTPacket *pkt)
{
    /* Compress payload if necessary */
    AVTBuffer *pl = pkt->data;
    AVTPktd tmp;

    enum AVTDataCompression data_compression;
    int err = payload_process(out, &tmp, st, pl, AVT_PKT_STREAM_DATA,
                              &data_compression);
    if (err < 0)
        return err;

    union AVTPacketData hdr = AVT_STREAM_DATA_HDR(
        .frame_type = pkt->type,
        .pkt_in_fec_group = 0,
        .field_id = 0,
        .pkt_compression = data_compression,
        .stream_id = st->id,
        .pts = pkt->pts,
        .duration = pkt->duration,
    );

    return send_pkt(out, hdr, pl);
}

#if 0
int avt_send_generic_data(AVTOutput *out,
                          AVTStream *st, AVTBuffer *data, int64_t pts,
                          uint32_t init_desc, uint32_t seg_desc)
{
    INIT_SEGMENTED(data, hdr_desc)

    avt_encode_generic_data(&bs, &(AVTGenericData){
        .generic_data_descriptor = hdr_desc,
        .stream_id = st->id,
        .total_payload_length = payload_size,
        .generic_data_compression = data_compression,
        .pts = pts,
        .payload_length = seg_len,
    });

    SEGMENT(data, seg_desc)
}

int avt_send_lut_data(AVTOutput *out,
                      AVTStream *st, int64_t pts)
{
    INIT_SEGMENTED(st->lut_data, AVT_PKT_LUT_ICC)

    AVTLutIcc pkt = st->lut_info;
    pkt.stream_id = st->id;
    pkt.pts = pts;
    pkt.lut_compression = data_compression;

    avt_encode_lut_icc(&bs, &pkt);

    SEGMENT(st->lut_data, AVT_PKT_STREAM_DATA_SEGMENT)
}

int avt_send_icc_data(AVTOutput *out,
                      AVTStream *st, int64_t pts)
{
    INIT_SEGMENTED(st->icc_data, AVT_PKT_LUT_ICC)

    AVTLutIcc pkt = st->icc_info;
    pkt.global_seq = atomic_fetch_add(&out->seq, 1ULL) & UINT32_MAX;
    pkt.stream_id = st->id;
    pkt.pts = pts;
    pkt.lut_compression = data_compression;

    avt_encode_lut_icc(&bs, &pkt);

    SEGMENT(st->icc_data, AVT_PKT_STREAM_DATA_SEGMENT)
}

int avt_send_video_info(AVTOutput *out,
                        AVTStream *st, int64_t pts)
{
    uint8_t hdr[AVT_MAX_HEADER_LEN];
    AVTBytestream bs = avt_bs_init(hdr, sizeof(hdr));

    AVTVideoInfo pkt = st->video_info;
    pkt.global_seq = atomic_fetch_add(&out->seq, 1ULL) & UINT32_MAX;
    pkt.stream_id = st->id;
    pkt.pts = pts;

    avt_encode_video_info(&bs, &pkt);

    return avt_packet_send(out, hdr, avt_bs_offs(&bs), NULL);
}

int avt_send_video_orientation(AVTOutput *out,
                               AVTStream *st, int64_t pts)
{
    uint8_t hdr[AVT_MAX_HEADER_LEN];
    AVTBytestream bs = avt_bs_init(hdr, sizeof(hdr));

    AVTVideoOrientation pkt = st->video_orientation;
    pkt.global_seq = atomic_fetch_add(&out->seq, 1ULL) & UINT32_MAX;
    pkt.stream_id = st->id;
    pkt.pts = pts;

    avt_encode_video_orientation(&bs, &pkt);

    return avt_packet_send(out, hdr, avt_bs_offs(&bs), NULL);
}
#endif
