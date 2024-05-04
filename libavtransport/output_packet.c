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
#include "utils_packet.h"

#ifdef CONFIG_HAVE_LIBBROTLIENC
#include <brotli/encode.h>
#endif

static inline enum AVTDataCompression compress_method(AVTPktd *p,
                                                      AVTStream *st,
                                                      AVTSenderOptions *opts)
{
    switch (p->pkt.desc) {
    case AVT_PKT_STREAM_DATA:
        switch (st->codec_id) {
        case AVT_CODEC_ID_SRT: [[fallthrough]];
        case AVT_CODEC_ID_WEBVTT: [[fallthrough]];
        case AVT_CODEC_ID_ASS:
            if (opts->compress & AVT_SENDER_COMPRESS_SUBS)
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
            if (opts->compress & (AVT_SENDER_COMPRESS_FORCE |
                                  AVT_SENDER_COMPRESS_AUDIO))
                return AVT_DATA_COMPRESSION_ZSTD;
            break;
        case AVT_CODEC_ID_SVG:
            /* Special exception for SVG, which is usually just text */
            if (opts->compress & AVT_SENDER_COMPRESS_VIDEO)
                return AVT_DATA_COMPRESSION_BROTLI;
            break;
        case AVT_CODEC_ID_TIFF:
            /* TODO: perhaps parse the TIFF header for JPEG */
            if (opts->compress & AVT_SENDER_COMPRESS_VIDEO)
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
            if (opts->compress & (AVT_SENDER_COMPRESS_FORCE |
                                  AVT_SENDER_COMPRESS_VIDEO))
                return AVT_DATA_COMPRESSION_ZSTD;
            break;
        default:
            break;
        }
        break;
    case AVT_PKT_FONT_DATA:
        /* If WOFF2, compress only if forced to */
        if ((p->pkt.font_data.font_type == AVT_FONT_TYPE_WOFF2) &&
            (opts->compress & (AVT_SENDER_COMPRESS_FORCE |
                               AVT_SENDER_COMPRESS_AUX)))
            return AVT_DATA_COMPRESSION_ZSTD;
        else if (opts->compress & AVT_SENDER_COMPRESS_AUX)
            return AVT_DATA_COMPRESSION_ZSTD;
        break;
    /* Permit user data to be compressed as well */
    case AVT_PKT_STREAM_CONFIG: [[fallthrough]];
    case AVT_PKT_USER_DATA: [[fallthrough]];
    case AVT_PKT_LUT_ICC:
        if (opts->compress & AVT_SENDER_COMPRESS_AUX)
            return AVT_DATA_COMPRESSION_ZSTD;
        break;
    case AVT_PKT_METADATA:
        if (opts->compress & AVT_SENDER_COMPRESS_META)
            return AVT_DATA_COMPRESSION_BROTLI;
        break;
    default:
        break;
    };
    return AVT_DATA_COMPRESSION_NONE;
}

static int payload_process(AVTSender *s, AVTStream *st,
                           AVTPktd *p, AVTBuffer *pl)
{
    int err = 0;
    AVTBuffer *zbuf = NULL;

    size_t src_len;
    uint8_t *src = avt_buffer_get_data(pl, &src_len);
    if (!src_len)
        return 0;

    /* TODO: clip to supported levels */
    [[maybe_unused]] int lvl = s->opts.compress_level;
    enum AVTDataCompression method = compress_method(p, st, &s->opts);

    [[maybe_unused]] uint8_t *dst;
    [[maybe_unused]] size_t dst_len;
    [[maybe_unused]] size_t dst_size;

    /* Reset here just in case it OOMs */
    if (s->opts.hash) {
        XXH_errorcode ret = XXH3_128bits_reset(s->xxh_state);
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
        avt_buffer_quick_ref(&p->pl, pl, 0, AVT_BUFFER_REF_ALL);
        break;
#ifdef CONFIG_HAVE_LIBZSTD
    case AVT_DATA_COMPRESSION_ZSTD:
        dst_size = ZSTD_compressBound(src_len);

        /* TODO: use a buffer pool */
        dst = malloc(dst_size);
        if (!dst)
            return AVT_ERROR(ENOMEM);

        dst_len = ZSTD_compressCCtx(s->zstd_ctx, dst, dst_size, src, src_len, lvl);
        if (!dst_len) {
            avt_log(s, AVT_LOG_ERROR, "Error while compressing with ZSTD!\n");
            err = AVT_ERROR(EINVAL);
            free(dst);
            break;
        }

        zbuf = avt_buffer_create(dst, dst_len, NULL, avt_buffer_default_free);
        if (!zbuf) {
            free(dst);
            err = AVT_ERROR(ENOMEM);
            break;
        }

        avt_buffer_quick_ref(&p->pl, zbuf, 0, AVT_BUFFER_REF_ALL);

        // TODO: removeme when there's pooling
        avt_buffer_unref(&zbuf);
        break;
#endif
#ifdef CONFIG_HAVE_LIBBROTLIENC
    case AVT_DATA_COMPRESSION_BROTLI:
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
            avt_log(s, AVT_LOG_ERROR, "Error while compressing with Brotli!\n");
            err = AVT_ERROR(EINVAL);
            free(dst);
            break;
        }

        zbuf = avt_buffer_create(dst, dst_size, NULL, avt_buffer_default_free);
        if (!zbuf) {
            free(dst);
            err = AVT_ERROR(ENOMEM);
            break;
        }

        avt_buffer_quick_ref(&p->pl, zbuf, 0, AVT_BUFFER_REF_ALL);

        // TODO: removeme when there's pooling
        avt_buffer_unref(&zbuf);
        break;
#endif
    default:
        avt_log(s, AVT_LOG_ERROR, "Unknown compression method: %i\n", method);
        return AVT_ERROR(EINVAL);
    };

    /* Set compression method */
    avt_packet_set_compression(p, method);

    /* Generate has for the payload if enabled */
    if (s->opts.hash) {
        src = avt_buffer_get_data(&p->pl, &src_len);
        XXH3_128bits_update(s->xxh_state, src, src_len);
        XXH128_hash_t hash = XXH3_128bits_digest(s->xxh_state);
        XXH128_canonicalFromHash((XXH128_canonical_t *)&p->pl_hash, hash);
        p->pl_has_hash = true;
    }

    return err;
}

static inline int send_pkt(AVTSender *s, AVTPktd *p)
{
    int ret = 0;

    for (int i = 0; i < s->nb_conn; i++) {
        int err = avt_connection_send(s->conn[i], p);
        if (err < 0)
            ret = err;
    }

    avt_buffer_quick_unref(&p->pl);

    return ret;
}

int avt_send_pkt_time_sync(AVTSender *s)
{
    AVTPktd p = {
        .pkt = AVT_TIME_SYNC_HDR(
            .ts_clock_id = 0,
            .ts_clock_hz2 = 0,
            .epoch = s->epoch,
            .ts_clock_seq = 0,
            .ts_clock_hz = 0,
        ),
    };

    return send_pkt(s, &p);
}

int avt_send_pkt_stream_register(AVTSender *s, AVTStream *st)
{
    AVTPktd p = {
        .pkt = AVT_STREAM_REGISTRATION_HDR(
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
        ),
    };

    return send_pkt(s, &p);
}

int avt_send_pkt_stream_data(AVTSender *s, AVTStream *st, AVTPacket *pkt)
{
    AVTPktd p = {
        .pkt = AVT_STREAM_DATA_HDR(
            .frame_type = pkt->type,
            .pkt_in_fec_group = 0,
            .field_id = 0,
            .pkt_compression = AVT_DATA_COMPRESSION_NONE,
            .stream_id = st->id,
            .pts = pkt->pts,
            .duration = pkt->duration,
        ),
    };

    int err = payload_process(s, st, &p, pkt->data);
    if (err < 0)
        return err;

    return send_pkt(s, &p);
}

int avt_send_pkt_video_info(AVTSender *s, AVTStream *st)
{
    AVTPktd p = {
        .pkt.video_info = st->video_info,
    };

    p.pkt.video_info.video_info_descriptor = AVT_PKT_VIDEO_INFO;
    p.pkt.video_info.stream_id = st->id;

    return send_pkt(s, &p);
}

int avt_send_pkt_video_orientation(AVTSender *s, AVTStream *st)
{
    AVTPktd p = {
        .pkt.video_orientation = st->video_orientation,
    };

    p.pkt.video_orientation.video_orientation_descriptor = AVT_PKT_VIDEO_ORIENTATION;
    p.pkt.video_orientation.stream_id = st->id;

    return send_pkt(s, &p);
}

#if 0
int avt_send_pkt_generic_data(AVTSender *s,
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

int avt_send_pkt_lut_data(AVTSender *s,
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

int avt_send_pkt_icc_data(AVTSender *s,
                      AVTStream *st, int64_t pts)
{
    INIT_SEGMENTED(st->icc_data, AVT_PKT_LUT_ICC)

    AVTLutIcc pkt = st->icc_info;
    pkt.global_seq = atomic_fetch_add(&s->seq, 1ULL) & UINT32_MAX;
    pkt.stream_id = st->id;
    pkt.pts = pts;
    pkt.lut_compression = data_compression;

    avt_encode_lut_icc(&bs, &pkt);

    SEGMENT(st->icc_data, AVT_PKT_STREAM_DATA_SEGMENT)
}
#endif
