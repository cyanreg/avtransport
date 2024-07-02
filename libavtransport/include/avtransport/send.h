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

#ifndef AVTRANSPORT_SEND_H
#define AVTRANSPORT_SEND_H

#include "connection.h"
#include "stream.h"
#include "utils.h"

/* Sender context */
typedef struct AVTSender AVTSender;

/* Compression mode flags */
enum AVTSenderCompressionFlags {
    /* Compress everything not already compressed, except uncompressed video and audio */
    AVT_SENDER_COMPRESS_AUTO  =  0,

    AVT_SENDER_COMPRESS_META  =  1 << 0,       /* Compress metadata */
    AVT_SENDER_COMPRESS_AUX   =  1 << 1,       /* Compress auxillary payloads (ICC profiles/fonts) */
    AVT_SENDER_COMPRESS_VIDEO =  1 << 2,       /* Compress video */
    AVT_SENDER_COMPRESS_AUDIO =  1 << 3,       /* Compress audio */
    AVT_SENDER_COMPRESS_SUBS  =  1 << 4,       /* Compress subtitles */

    AVT_SENDER_COMPRESS_FORCE =  1 << 30,      /* Force compression even if data is already compressed */
    AVT_SENDER_COMPRESS_NONE  =  INT32_MAX,    /* Do not compress anything */
};

typedef struct AVTSenderOptions {
    /* Compression mode */
    enum AVTSenderCompressionFlags compress;

    /* Compression algorithm.
     * If left as 0 (AVT_DATA_COMPRESSION_AUTO), will automatically
     * decide the best algorithm (Brotli for subtitles, Zstd for everything
     * else not already compressed, except uncompressed video)
     *
     * Otherwise, will force the use of this compression algorithm. */
    enum AVTDataCompression compress_algo;

    /* Compression level. Zero means automatic (the default for each compressor). */
    int compress_level;

    /* Set to true to enable sending hash packets for all packets with a payload. */
    bool hash;

    /* Padding to allow for future options. Must always be set to 0. */
    uint8_t padding[4096 - 1*8 - 0*16 - 3*32 - 0*64];
} AVTSenderOptions;

/* Open a send context and immediately send/write a stream session packet.
 *
 * NOTE: Multiple connections may be bound for a single sender to enable
 * one-to-many streaming or writing, but not vice-versa. */
AVT_API int avt_send_open(AVTContext *ctx, AVTSender **s,
                          AVTConnection *conn, AVTSenderOptions *opts);

/* Set the epoch to use, as nanoseconds after 00:00:00 UTC on 1 January 1970.
 * Should be called once, at the start of streaming.
 * If zero, or not called, the current time will be used. */
AVT_API int avt_send_set_epoch(AVTSender *s, uint64_t epoch);

/* Update a given clock.
 * clock_id must be assigned to at least a single stream, and must be non-zero.
 * clock_hz is the clock's frequency hertz.
 * clock_hz16 is the remainder of the clock's frequency, in 1/65536 of a hertz.
 * clock_seq is the clock's current value
 * */
AVT_API int avt_send_clock_sync(AVTSender *s, uint8_t clock_id,
                                uint32_t clock_hz, uint16_t clock_hz16,
                                uint64_t clock_seq);

/* Register a stream and allocate internal state for it.
 * To automatically assign a stream ID, set id to UINT16_MAX.
 * If there's an existing stream with the same ID, will return NULL. */
AVT_API AVTStream *avt_send_stream_add(AVTSender *s, uint16_t id);

/* Update a stream, (re-)emmitting a stream registration packet.
 * The id MUST match the one from avt_send_add_stream().
 * All present fields in the AVTStream input must be set. */
AVT_API int avt_send_stream_update(AVTStream *st);

/* Attach a font to a stream. The font data must be fully in the AVTBuffer.
 * The filename is sent alongside as metadata. */
AVT_API int avt_send_font_attachment(AVTStream *st, AVTBuffer *file,
                                     const char filename[252],
                                     enum AVTFontType type);

/* Write a complete stream data packet to the output. */
AVT_API int avt_send_stream_data(AVTStream *st, AVTPacket *pkt);

/**
 * This function allows for writing of a stream data packet
 * in chunks.
 * Useful for extremely low-latency scenarios.
 *
 * To use:
 *  - pkt->data must be NULL
 *  - pkt->total_size must be equal to the sum of the size of all future segments
 *  - pkt must be identical between each call
 *  - for the first call, use offset 0
 *  - for each call after the offset must be equal to the previous offset, plus
 *    the size of the previous buffer
 */
AVT_API int avt_send_stream_data_streaming(AVTStream *st,
                                           uint8_t state[AVT_MAX_HEADER_LEN],
                                           AVTPacket *pkt,
                                           AVTBuffer *buf, size_t offset);

/* Write user data packets.
 * The immediate flag can be used to skip any potential queueing. */
AVT_API int avt_send_user_data(AVTSender *s, AVTBuffer *data,
                               uint16_t user, uint64_t opaque,
                               bool immediate);

/* Immediately refresh all stream configuration data */
AVT_API int avt_send_refresh(AVTSender *s);

/* Close a single stream */
AVT_API int avt_send_stream_close(AVTStream **st);

/* Close all streams and free resources */
AVT_API int avt_send_close(AVTSender **s);

#endif /* AVTRANSPORT_SEND_H */
