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

#ifdef CONFIG_HAVE_LIBZSTD
#include <zstd.h>
#endif

#ifdef CONFIG_HAVE_LIBBROTLIENC
#include <brotli/encode.h>
#endif

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

static inline int avt_payload_compress(AVTOutput *out,
                                       AVTBuffer **data, enum AVTPktDescriptors desc,
                                       enum AVTDataCompression *data_compression)
{
    int err = 0;

    [[maybe_unused]] int lvl = 0;
    enum AVTDataCompression method = AVT_DATA_COMPRESSION_NONE;

    [[maybe_unused]] uint8_t *src;
    [[maybe_unused]] size_t src_len;
    [[maybe_unused]] uint8_t *dst;
    [[maybe_unused]] size_t dst_len;
    [[maybe_unused]] size_t dst_size;
    [[maybe_unused]] AVTBuffer *tmp;

    switch (method) {
    case AVT_DATA_COMPRESSION_NONE:
        break;
    case AVT_DATA_COMPRESSION_ZSTD:
#ifdef CONFIG_HAVE_LIBZSTD
        src = avt_buffer_get_data(*data, &src_len);

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

        tmp = avt_buffer_create(dst, dst_len, NULL, avt_buffer_default_free);
        if (!tmp) {
            free(dst);
            err = AVT_ERROR(ENOMEM);
            break;
        }

        *data = tmp;
#else
        avt_log(out, AVT_LOG_ERROR, "ZSTD compression not enabled during build!\n");
        err = AVT_ERROR(EINVAL);
#endif
        break;
    case AVT_DATA_COMPRESSION_BROTLI:
#ifdef CONFIG_HAVE_LIBBROTLIENC
        src = avt_buffer_get_data(*data, &src_len);

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

        tmp = avt_buffer_create(dst, dst_size, NULL, avt_buffer_default_free);
        if (!tmp) {
            free(dst);
            err = AVT_ERROR(ENOMEM);
            break;
        }

        *data = tmp;
#else
        avt_log(out, AVT_LOG_ERROR, "Brotli compression not enabled during build!\n");
        err = AVT_ERROR(EINVAL);
#endif
        break;
    default:
        avt_log(out, AVT_LOG_ERROR, "Unknown compression method: %i\n", method);
        err = AVT_ERROR(EINVAL);
    };

    if (err >= 0)
        *data_compression = method;

    return err;
}

int avt_send_session_start(AVTOutput *out)
{
    union AVTPacketData pkt = AVT_SESSION_START_HDR(
        .session_uuid = { 0 },
        .session_seq = 0x0,

        .session_flags = 0x0,
        .producer_major = PROJECT_VERSION_MAJOR,
        .producer_minor = PROJECT_VERSION_MINOR,
        .producer_micro = PROJECT_VERSION_MICRO,
        .producer_name = { 0 },
    );

    memccpy(pkt.session_start.producer_name, PROJECT_NAME,
            '\0', sizeof(pkt.session_start.producer_name));

    return send_pkt(out, pkt, nullptr);
}

int avt_send_time_sync(AVTOutput *out)
{
    union AVTPacketData pkt = AVT_TIME_SYNC_HDR(
        .ts_clock_id = 0,
        .ts_clock_hz2 = 0,
        .epoch = atomic_load(&out->epoch),
        .ts_clock_seq = 0,
        .ts_clock_hz = 0,
    );

    return send_pkt(out, pkt, nullptr);
}

int avt_send_stream_register(AVTOutput *out, AVTStream *st)
{
    union AVTPacketData pkt = AVT_STREAM_REGISTRATION_HDR(
        .stream_id = st->id,
        .global_seq = atomic_fetch_add(&out->seq, 1ULL) & UINT32_MAX,
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

    enum AVTDataCompression data_compression;
    int err = avt_payload_compress(out, &pl, AVT_PKT_STREAM_DATA,
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
        .global_seq = init_seq,
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
    pkt.global_seq = atomic_fetch_add(&out->seq, 1ULL) & UINT32_MAX;
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
