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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <signal.h>
#include <string.h>

#include <avtransport/avtransport.h>
#include <avtransport/receive.h>

#ifdef HAVE_FFMPEG
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#endif

#include "avcat/io.h"

#define GEN_OPT_MAX_ARR 16
#define GEN_OPT_LOG avt_log
#define GEN_OPT_LOG_INFO AVT_LOG_INFO
#define GEN_OPT_LOG_ERROR AVT_LOG_ERROR
#define GEN_OPT_RATIONAL AVTRational
#define GEN_OPT_HELPSTRING "avcat " PROJECT_VERSION_STRING
#include "genopt.h"

#define MAX_INPUTS 4
#define MAX_STREAMS 16

#define READ_CHUNK (1 * 1024 * 1024)

static volatile sig_atomic_t signal_terminate;
static void on_quit_signal(const int signo)
{
    signal_terminate = 1;
}

/* Send a buffer as a stream data packet through the AVT output. */
static int send_packet(IOContext *out, AVTBuffer *buf, int64_t pts,
                       enum AVTFrameType type)
{
    AVTPacket pkt = {
        .data = buf,
        .type = type,
        .pts = pts,
    };
    return avt_send_stream_data(out->st, &pkt);
}

/* Write a payload buffer to a raw file output. */
static int write_raw(IOContext *out, AVTBuffer *buf)
{
    size_t len;
    uint8_t *data = avt_buffer_get_data(buf, &len);
    if (!data || !len)
        return 0;

    size_t w = fwrite(data, 1, len, out->raw);
    if (w != len) {
        avt_log(NULL, AVT_LOG_ERROR, "Short write: %zu of %zu bytes\n", w, len);
        return AVT_ERROR(EIO);
    }
    return 0;
}

/* Dispatch one assembled payload to whatever output type is active. */
static int dispatch_payload(IOContext *out, AVTBuffer *buf, int64_t pts,
                            enum AVTFrameType type)
{
    switch (out->mode) {
    case IO_AVT:
        return send_packet(out, buf, pts, type);
    case IO_RAW:
        return write_raw(out, buf);
    case IO_LAVF:
#ifdef HAVE_FFMPEG
        /* TODO: full LAVF passthrough */
        avt_log(NULL, AVT_LOG_ERROR, "LAVF output not yet supported\n");
        return AVT_ERROR(ENOSYS);
#else
        return AVT_ERROR(EINVAL);
#endif
    }
    return AVT_ERROR(EINVAL);
}

/* Create and register an output stream on an AVT sender. If ref is non-NULL,
 * the stream parameters are mirrored from it (for AVT-to-AVT remuxing) and
 * codec_id/tb are ignored; otherwise codec_id and tb are applied directly.
 * Returns NULL on failure. */
static AVTStream *setup_out_stream(AVTSender *out, uint16_t id,
                                   enum AVTCodecID codec_id, AVTRational tb,
                                   const AVTStream *ref)
{
    AVTStream *st = avt_send_stream_add(out, id);
    if (!st)
        return NULL;

    if (ref) {
        st->codec_id          = ref->codec_id;
        st->timebase          = ref->timebase;
        st->duration          = ref->duration;
        st->flags             = ref->flags;
        st->bitrate           = ref->bitrate;
        st->video_info        = ref->video_info;
        st->video_orientation = ref->video_orientation;
    } else {
        st->codec_id = codec_id;
        st->timebase = tb;
    }

    if (avt_send_stream_update(st) < 0)
        return NULL;
    return st;
}

/* State passed to the receive callbacks. */
typedef struct AvtRecvState {
    IOContext *out;

    /* Map from input stream id to the mirrored output stream, used when
     * the output is itself an AVT connection (remuxing). */
    struct {
        uint32_t in_id;
        AVTStream *out_st;
    } smap[MAX_STREAMS];
    int nb_smap;

    int64_t streams_seen;
    int64_t packets_seen;
    int err;
} AvtRecvState;

/* Look up the output stream mirrored from a given input stream id. */
static AVTStream *smap_get(AvtRecvState *s, uint32_t in_id)
{
    for (int i = 0; i < s->nb_smap; i++)
        if (s->smap[i].in_id == in_id)
            return s->smap[i].out_st;
    return NULL;
}

static int on_stream_register(void *opaque, AVTStream *st)
{
    AvtRecvState *s = opaque;
    s->streams_seen++;
    avt_log(NULL, AVT_LOG_INFO,
            "Registered stream id=%u codec_id=0x%x timebase=%d/%d\n",
            st->id, st->codec_id, st->timebase.num, st->timebase.den);

    /* For an AVT output, mirror the stream so the remux preserves its
     * codec parameters. Other outputs need no per-stream state. */
    if (s->out->mode != IO_AVT)
        return 0;

    if (s->nb_smap >= MAX_STREAMS) {
        avt_log(NULL, AVT_LOG_ERROR, "Too many streams (max %d)\n", MAX_STREAMS);
        return AVT_ERROR(ENOSYS);
    }

    AVTStream *o = setup_out_stream(s->out->out, (uint16_t)st->id,
                                    0, (AVTRational){ 0, 0 }, st);
    if (!o) {
        avt_log(NULL, AVT_LOG_ERROR, "Could not mirror stream %u\n", st->id);
        return AVT_ERROR(ENOMEM);
    }

    s->smap[s->nb_smap].in_id  = st->id;
    s->smap[s->nb_smap].out_st = o;
    s->nb_smap++;
    return 0;
}

static void on_stream_update(void *opaque, AVTStream *st)
{
    AvtRecvState *s = opaque;
    AVTStream *o = smap_get(s, st->id);
    if (!o)
        return;

    o->codec_id          = st->codec_id;
    o->timebase          = st->timebase;
    o->duration          = st->duration;
    o->flags             = st->flags;
    o->bitrate           = st->bitrate;
    o->video_info        = st->video_info;
    o->video_orientation = st->video_orientation;
    avt_send_stream_update(o);
}

static void on_stream_close(void *opaque, AVTStream *st)
{
    AvtRecvState *s = opaque;
    for (int i = 0; i < s->nb_smap; i++) {
        if (s->smap[i].in_id == st->id) {
            avt_send_stream_close(&s->smap[i].out_st);
            s->smap[i] = s->smap[--s->nb_smap];
            return;
        }
    }
}

static int on_stream_pkt(void *opaque, AVTStream *st, AVTPacket pkt)
{
    AvtRecvState *s = opaque;
    s->packets_seen++;
    if (!pkt.data)
        return 0;

    int ret;
    if (s->out->mode == IO_AVT) {
        /* Forward the whole packet so pts/dts/duration survive the remux. */
        AVTStream *o = smap_get(s, st->id);
        if (!o) {
            avt_log(NULL, AVT_LOG_ERROR,
                    "Dropping packet for unmapped stream %u\n", st->id);
            return 0;
        }
        ret = avt_send_stream_data(o, &pkt);
    } else {
        ret = dispatch_payload(s->out, pkt.data, pkt.pts, pkt.type);
    }

    if (ret < 0)
        s->err = ret;
    return ret;
}

/* Pump one AVT input to the output until EOF or termination using the
 * public receive callback API. */
static int process_avt_input(IOContext *in, IOContext *out, AVTContext *avt)
{
    AvtRecvState state = { .out = out };
    AVTReceiveCallbacks cbs = {
        .stream_register_cb = on_stream_register,
        .stream_update_cb   = on_stream_update,
        .stream_close_cb    = on_stream_close,
        .stream_pkt_cb      = on_stream_pkt,
    };
    AVTReceiveOptions opts = { };

    int ret = avt_receive_open(avt, in->conn, &cbs, &state, &opts);
    if (ret < 0) {
        avt_log(NULL, AVT_LOG_ERROR, "avt_receive_open failed: %d\n", ret);
        return ret;
    }

    while (!signal_terminate) {
        ret = avt_connection_process(in->conn, 0);
        if (ret == AVT_ERROR(ENODATA)) {
            ret = 0; /* Clean EOF. */
            break;
        }
        if (ret < 0) {
            avt_log(NULL, AVT_LOG_ERROR, "Connection process error: %d\n", ret);
            break;
        }
        if (state.err < 0) {
            ret = state.err;
            break;
        }
    }

    avt_log(NULL, AVT_LOG_INFO,
            "Received %ld stream(s), %ld stream-data packet(s)\n",
            (long)state.streams_seen, (long)state.packets_seen);

    avt_receive_close(avt);
    return ret;
}

/* Pump a raw input file to the output as a sequence of packets. */
static int process_raw_input(IOContext *in, IOContext *out)
{
    int ret = 0;
    int64_t pts = 0;
    size_t len_remaining;

    if (fseek(in->raw, 0L, SEEK_END) != 0)
        return AVT_ERROR(errno);
    long file_len = ftell(in->raw);
    if (file_len < 0)
        return AVT_ERROR(errno);
    if (fseek(in->raw, 0L, SEEK_SET) != 0)
        return AVT_ERROR(errno);

    len_remaining = (size_t)file_len;
    while (len_remaining > 0 && !signal_terminate) {
        size_t chunk = len_remaining > READ_CHUNK ? READ_CHUNK : len_remaining;
        uint8_t *data = malloc(chunk);
        if (!data)
            return AVT_ERROR(ENOMEM);

        size_t r = fread(data, 1, chunk, in->raw);
        if (r != chunk) {
            free(data);
            avt_log(NULL, AVT_LOG_ERROR, "Short read: %zu of %zu\n", r, chunk);
            return AVT_ERROR(EIO);
        }

        AVTBuffer *buf = avt_buffer_create(data, chunk, NULL,
                                           avt_buffer_default_free);
        if (!buf) {
            free(data);
            return AVT_ERROR(ENOMEM);
        }

        ret = dispatch_payload(out, buf, pts++, AVT_FRAME_TYPE_KEY);
        avt_buffer_unref(&buf);
        if (ret < 0)
            return ret;

        len_remaining -= chunk;
    }

    return 0;
}

#ifdef HAVE_FFMPEG
/* Map an FFmpeg codec ID to an AVTransport codec ID, or 0 if there is no
 * equivalent. */
static enum AVTCodecID codec_id_from_lavf(enum AVCodecID id)
{
    switch (id) {
    case AV_CODEC_ID_H264:      return AVT_CODEC_ID_H264;
    case AV_CODEC_ID_HEVC:      return AVT_CODEC_ID_H265;
    case AV_CODEC_ID_AV1:       return AVT_CODEC_ID_AV1;
    case AV_CODEC_ID_VP9:       return AVT_CODEC_ID_VP9;
    case AV_CODEC_ID_THEORA:    return AVT_CODEC_ID_THEORA;
    case AV_CODEC_ID_DIRAC:     return AVT_CODEC_ID_DIRAC;
    case AV_CODEC_ID_FFV1:      return AVT_CODEC_ID_FFV1;
    case AV_CODEC_ID_VC1:       return AVT_CODEC_ID_VC1;
    case AV_CODEC_ID_APV:       return AVT_CODEC_ID_APV;
    case AV_CODEC_ID_MJPEG:     return AVT_CODEC_ID_JPEG;
    case AV_CODEC_ID_JPEG2000:  return AVT_CODEC_ID_JPEG2000;
    case AV_CODEC_ID_PNG:       return AVT_CODEC_ID_PNG;
    case AV_CODEC_ID_TIFF:      return AVT_CODEC_ID_TIFF;
    case AV_CODEC_ID_RAWVIDEO:  return AVT_CODEC_ID_RAW_VIDEO;
    case AV_CODEC_ID_AAC:       return AVT_CODEC_ID_AAC;
    case AV_CODEC_ID_AC3:       return AVT_CODEC_ID_AC3;
    case AV_CODEC_ID_OPUS:      return AVT_CODEC_ID_OPUS;
    case AV_CODEC_ID_FLAC:      return AVT_CODEC_ID_FLAC;
    case AV_CODEC_ID_TAK:       return AVT_CODEC_ID_TAK;
    case AV_CODEC_ID_ASS:       return AVT_CODEC_ID_ASS;
    case AV_CODEC_ID_SUBRIP:    return AVT_CODEC_ID_SRT;
    case AV_CODEC_ID_WEBVTT:    return AVT_CODEC_ID_WEBVTT;
    default:                    return 0;
    }
}

/* Demux an FFmpeg-supported container, creating one output stream per
 * input stream and routing packets by stream index. */
static int process_lavf_input(IOContext *in, IOContext *out)
{
    int ret = 0;
    unsigned nb = in->avf->nb_streams;
    if (nb > MAX_STREAMS) {
        avt_log(NULL, AVT_LOG_ERROR,
                "Input has %u streams; only %d are supported\n",
                nb, MAX_STREAMS);
        return AVT_ERROR(ENOSYS);
    }

    /* Output stream for each input stream index (NULL if dropped). */
    AVTStream *ostr[MAX_STREAMS] = { 0 };

    if (out->mode == IO_AVT) {
        int created = 0;
        for (unsigned i = 0; i < nb; i++) {
            AVStream *ist = in->avf->streams[i];
            enum AVTCodecID cid = codec_id_from_lavf(ist->codecpar->codec_id);
            if (!cid) {
                avt_log(NULL, AVT_LOG_WARN,
                        "Input stream %u: unsupported codec, dropping it\n", i);
                continue;
            }

            AVTRational tb = { ist->time_base.num, ist->time_base.den };
            if (tb.den <= 0)
                tb = (AVTRational){ 1, 1000000 };

            ostr[i] = setup_out_stream(out->out, (uint16_t)i, cid, tb, NULL);
            if (!ostr[i])
                return AVT_ERROR(ENOMEM);
            created++;
        }
        if (!created) {
            avt_log(NULL, AVT_LOG_ERROR, "No input streams could be muxed\n");
            return AVT_ERROR(EINVAL);
        }
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
        return AVT_ERROR(ENOMEM);

    while (!signal_terminate) {
        int err = av_read_frame(in->avf, pkt);
        if (err == AVERROR_EOF)
            break;
        if (err < 0) {
            avt_log(NULL, AVT_LOG_ERROR, "av_read_frame error: %d\n", err);
            ret = AVT_ERROR(EIO);
            goto end;
        }

        unsigned idx = pkt->stream_index;

        /* Skip packets from dropped or out-of-range streams, and empty ones. */
        if ((out->mode == IO_AVT && (idx >= nb || !ostr[idx])) ||
            pkt->size <= 0) {
            av_packet_unref(pkt);
            continue;
        }

        /* Copy the buffer so AVT owns it after we unref the packet. */
        uint8_t *copy = malloc(pkt->size);
        if (!copy) {
            ret = AVT_ERROR(ENOMEM);
            goto end;
        }
        memcpy(copy, pkt->data, pkt->size);

        AVTBuffer *buf = avt_buffer_create(copy, pkt->size, NULL,
                                           avt_buffer_default_free);
        if (!buf) {
            free(copy);
            ret = AVT_ERROR(ENOMEM);
            goto end;
        }

        enum AVTFrameType type = (pkt->flags & AV_PKT_FLAG_KEY) ?
                                 AVT_FRAME_TYPE_KEY : AVT_FRAME_TYPE_P;
        int64_t pts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : 0;
        int64_t dts = pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pts;

        if (out->mode == IO_AVT) {
            AVTPacket op = {
                .data     = buf,
                .type     = type,
                .pts      = pts,
                .dts      = dts,
                .duration = pkt->duration,
            };
            ret = avt_send_stream_data(ostr[idx], &op);
        } else {
            ret = dispatch_payload(out, buf, pts, type);
        }

        avt_buffer_unref(&buf);
        av_packet_unref(pkt);

        if (ret < 0)
            goto end;
    }

end:
    av_packet_free(&pkt);
    return ret;
}
#endif

int main(int argc, char **argv)
{
    int ret;
    AVTContext *avt = NULL;
    AVTContextOptions ctx_opts = {
        .producer_name = "avcat",
        .producer_ver = { AVTRANSPORT_VERSION_MAJOR,
                          AVTRANSPORT_VERSION_MICRO,
                          AVTRANSPORT_VERSION_MINOR },
    };

    ret = avt_init(&avt, &ctx_opts);
    if (ret < 0)
        return -ret;

    GEN_OPT_INIT(opts_list, 16);
    GEN_OPT_SEC(opts_list, "Input/Output");
    GEN_OPT_ARR(opts_list, char *, input,   "i", 1, MAX_INPUTS, 0, 0, "Input (files or URLs)");
    GEN_OPT_ONE(opts_list, char *, output, "o", 1, 1, NULL, 0, 0, "Destination (file or URL)");
    GEN_OPT_SEC(opts_list, "Miscellaneous");
    GEN_OPT_ONE(opts_list, bool  , unround, "u", 0, 0, false, 0, 0, "Unround timestamps (for remuxing from Matroska)");
    GEN_OPT_ONE(opts_list, char *, mirror,  "m", 1, 1, NULL, 0, 0, "Mirror input and output to a file for monitoring and caching");

    IOContext in[MAX_INPUTS] = { 0 };
    IOContext out = { 0 };

    if ((ret = GEN_OPT_PARSE(opts_list, argc, argv)) < 0)
        goto end;

    if (!input[0]) {
        avt_log(NULL, AVT_LOG_ERROR, "At least one input required!\n");
        ret = AVT_ERROR(EINVAL);
        goto end;
    }
    if (!output) {
        avt_log(NULL, AVT_LOG_ERROR, "Output required (-o)!\n");
        ret = AVT_ERROR(EINVAL);
        goto end;
    }

    if (mirror)
        avt_log(NULL, AVT_LOG_WARN,
                "Mirroring (-m) is not yet implemented; ignoring\n");
    if (unround)
        avt_log(NULL, AVT_LOG_WARN,
                "Timestamp unrounding (-u) is not yet implemented; ignoring\n");

#ifdef _WIN32
    if (signal(SIGINT,  on_quit_signal) < 0 ||
        signal(SIGTERM, on_quit_signal) < 0)
#else
    static const struct sigaction sa = {
        .sa_handler = on_quit_signal,
        .sa_flags = SA_RESETHAND,
    };
    if (sigaction(SIGINT,  &sa, NULL) < 0 ||
        sigaction(SIGTERM, &sa, NULL) < 0)
#endif
        avt_log(NULL, AVT_LOG_ERROR, "Can't init signal handler!\n");

    /* Create inputs */
    for (int i = 0; input[i]; i++) {
        ret = io_open(&in[i], avt, input[i], 0);
        if (ret < 0)
            goto end;
    }

    /* Create output */
    ret = io_open(&out, avt, output, 1);
    if (ret < 0)
        goto end;

    /* A raw input maps to a single output stream, set up here. AVT and
     * LAVF inputs derive their output streams from the input parameters
     * (see process_avt_input / process_lavf_input). */
    if (out.mode == IO_AVT && in[0].mode == IO_RAW) {
        out.st = setup_out_stream(out.out, 0, in[0].codec_id,
                                  (AVTRational){ 1, 1000000000 }, NULL);
        if (!out.st) {
            avt_log(NULL, AVT_LOG_ERROR, "Could not create output stream\n");
            ret = AVT_ERROR(ENOMEM);
            goto end;
        }
    }

    /* Process first input for now; multi-input mixing is a TODO. */
    switch (in[0].mode) {
    case IO_AVT:
        ret = process_avt_input(&in[0], &out, avt);
        break;
    case IO_RAW:
        ret = process_raw_input(&in[0], &out);
        break;
    case IO_LAVF:
#ifdef HAVE_FFMPEG
        ret = process_lavf_input(&in[0], &out);
#else
        avt_log(NULL, AVT_LOG_ERROR, "FFmpeg not compiled in\n");
        ret = AVT_ERROR(EINVAL);
#endif
        break;
    }

end:
    /* io_close is a safe no-op on zero-initialized contexts. */
    io_close(&out, 1);
    for (int i = 0; i < MAX_INPUTS; i++)
        io_close(&in[i], 0);
    avt_close(&avt);
    return ret < 0 ? -ret : 0;
}
