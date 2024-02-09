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

#ifndef AVTRANSPORT_OUTPUT_H
#define AVTRANSPORT_OUTPUT_H

#include "connection.h"
#include "stream.h"
#include "utils.h"

typedef struct AVTOutput AVTOutput;

/* Compression mode flags. By default, AVT_OUTPUT_COMPRESS_ALL is used,
 * which compresses everything not already compressed, except video. */
enum AVTOutputCompressionFlags {
    AVT_SENDER_COMPRESS_NONE   = 0 << 0,        /* Don't compress anything */
    AVT_SENDER_COMPRESS_AUX    = 1 << 0,        /* Compress auxillary payloads (ICC profiles/fonts) */
    AVT_SENDER_COMPRESS_META   = 1 << 1,        /* Compress metadata */
    AVT_SENDER_COMPRESS_SUBS   = 1 << 2,        /* Compress subtitles */
    AVT_SENDER_COMPRESS_AUDIO  = 1 << 3,        /* Compress audio */
    AVT_SENDER_COMPRESS_VIDEO  = 1 << 4,        /* Compress video */
    AVT_SENDER_COMPRESS_USER   = 1 << 5,        /* Compress user data */
    AVT_SENDER_COMPRESS_ALL    = (1 << 16) - 1, /* Compress everything */
    AVT_SENDER_COMPRESS_FORCE  = 1 << 31,       /* Compress even if data is already compressed */
};

typedef struct AVTOutputOptions {
    /* Compression mode */
    enum AVTOutputCompressionFlags compress;

    /* Compression algorithm.
     * If left as 0 (AVT_DATA_COMPRESSION_NONE), will automatically
     * decide the best algorithm (Brotli for subtitles, Zstd for everything else)
     *
     * Otherwise, will force the use of this compression algorithm. */
    enum AVTDataCompression compress_algo;

    /* Compression level. Zero means automatic (the default for each compressor). */
    int compress_level;
} AVTOutputOptions;

/* All functions listed here are thread-safe. */

/* Open an output and immediately send/write a stream session packet.
 *
 * NOTE: Multiple connections may be bound for output to enable
 * one-to-many streaming or writing */
AVT_API int avt_output_open(AVTContext *ctx, AVTOutput **out,
                            AVTConnection *conn, AVTOutputOptions *opts);

/* Set the epoch to use, as nanoseconds after 00:00:00 UTC on 1 January 1970.
 * Should be called once, at the start of streaming.
 * If zero, or not called, the current time will be used. */
AVT_API int avt_output_set_epoch(AVTOutput *out, uint64_t epoch);

/* Register a stream and allocate internal state for it.
 * To automatically assign a stream ID, set id to UINT16_MAX.
 * If there's an existing stream with the same ID, will return NULL. */
AVT_API AVTStream *avt_output_stream_add(AVTOutput *out, uint16_t id);

/* Update a stream, (re-)emmitting a stream registration packet.
 * The id MUST match the one from avt_output_add_stream(). */
AVT_API int avt_output_stream_update(AVTOutput *out, AVTStream *st);

/* Attach a font to a stream */
AVT_API int avt_output_font_attachment(AVTStream *st, AVTBuffer *file,
                                       const char *filename, enum AVTFontType type);

/* Write a complete stream data packet to the output. */
AVT_API int avt_output_stream_data(AVTStream *st, AVTPacket *pkt);

/* This function allows for writing of a stream data packet
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
AVT_API int avt_output_stream_data_streaming(AVTStream *st,
                                             uint8_t state[1024],
                                             AVTPacket *pkt,
                                             AVTBuffer *buf, size_t offset);

/* Write user data packets. The immediate flag can be used to skip
 * any potential queueing. */
AVT_API int avt_output_user_data(AVTOutput *out, AVTBuffer *data,
                                 uint64_t opaque, int immediate);

/* Immediately refresh all stream configuration data */
AVT_API int avt_output_refresh(AVTOutput *out);

/* Close a single stream */
AVT_API int avt_output_stream_close(AVTStream **st);

/* Close all streams and free resources */
AVT_API int avt_output_close(AVTOutput **out);

#endif /* AVTRANSPORT_OUTPUT_H */
