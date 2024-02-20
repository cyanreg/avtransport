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

#include <stdio.h>
#include <stdnoreturn.h>
#include <signal.h>

#include <avtransport/avtransport.h>

#include "config.h"

#ifdef HAVE_FFMPEG
#include <libavformat/avformat.h>
#endif

#define GEN_OPT_MAX_ARR 16
#define GEN_OPT_LOG avt_log
#define GEN_OPT_LOG_CTX NULL
#define GEN_OPT_LOG_INFO AVT_LOG_INFO
#define GEN_OPT_LOG_ERROR AVT_LOG_ERROR
#define GEN_OPT_RATIONAL AVTRational
#define GEN_OPT_HELPSTRING "avcat " PROJECT_VERSION_STRING
#include "genopt.h"

#define MAX_INPUTS 4

enum IOMode {
    IO_RAW,
    IO_AVT,
    IO_LAVF,
};

typedef struct IOContext {
    enum IOMode mode;
    int64_t val;

    enum AVTConnectionType type;
    AVTContext *avt;
    AVTConnection *conn;
    AVTStream *st;
    AVTOutput *out;

    FILE *raw;

#ifdef HAVE_FFMPEG
    AVFormatContext *avf;
#endif
} IOContext;

static enum IOMode path_is_avt(const char *path, enum AVTConnectionType *conn_type, int is_out)
{
    int len = strlen(path);

    if (!strncmp(path, "avt://", strlen("avt://")) ||
        !strncmp(path, "file://", strlen("file://"))) {
        *conn_type = AVT_CONNECTION_URL;
        return IO_AVT;
    } else if (!strcmp(&path[len - 4], ".avt") || !strcmp(&path[len - 3], ".at")) {
        *conn_type = AVT_CONNECTION_FILE;
        return IO_AVT;
    } else if (is_out && /* For output, anything that can be concat'd payload */
               (!strcmp(&path[len - 5], ".h264") ||
                !strcmp(&path[len - 5], ".h265") ||
                !strcmp(&path[len - 5], ".hevc") ||
                !strcmp(&path[len - 5], ".tiff") ||
                !strcmp(&path[len - 5], ".jpeg") ||
                !strcmp(&path[len - 4], ".dng")  ||
                !strcmp(&path[len - 4], ".png")  ||
                !strcmp(&path[len - 4], ".jpg")  ||
                !strcmp(&path[len - 4], ".aac")  ||
                !strcmp(&path[len - 4], ".ac3")  ||
                !strcmp(&path[len - 4], ".svg"))) {
        return IO_RAW;
    } else if (!is_out && /* Individual files do not need any framing */
               (!strcmp(&path[len - 5], ".tiff") ||
                !strcmp(&path[len - 5], ".jpeg") ||
                !strcmp(&path[len - 4], ".dng")  ||
                !strcmp(&path[len - 4], ".png")  ||
                !strcmp(&path[len - 4], ".jpg")  ||
                !strcmp(&path[len - 4], ".svg"))) {
        return IO_RAW;
    }

    return 0;
}

static int close_io(IOContext *io, int is_out)
{
    switch (io->mode) {
    case IO_AVT: {
        avt_output_close(&io->out);
        avt_connection_flush(io->conn, INT64_MAX);
        avt_connection_destroy(&io->conn);
        avt_close(&io->avt);
        break;
    }
    case IO_RAW: {
        fclose(io->raw);
        break;
    }
    case IO_LAVF: {
#ifdef HAVE_FFMPEG
        if (is_out) {
            av_write_trailer(io->avf);
            avformat_flush(io->avf);
            avio_closep(&io->avf->pb);
            avformat_free_context(io->avf);
            io->avf = NULL;
        } else {
            avformat_close_input(&io->avf);
        }
        break;
#else
        return AVT_ERROR(EINVAL);
#endif
    }
    };

    return 0;
}

static int open_io(IOContext *io, const char *path, int is_out)
{
    int err;

    io->mode = path_is_avt(path, &io->type, is_out);

    switch (io->mode) {
    case IO_AVT: {
        AVTContextOptions ctx_opts = {
            .log_cb = NULL,
            .producer_name = "avcat",
            .producer_ver = { PROJECT_VERSION_MAJOR,
                              PROJECT_VERSION_MICRO,
                              PROJECT_VERSION_MINOR },
        };
        err = avt_init(&io->avt, &ctx_opts);
        if (err < 0)
            return err;

        AVTConnectionInfo conn_info = {
            .path = path,
            .type = io->type,
        };
        err = avt_connection_create(io->avt, &io->conn, &conn_info);
        if (err < 0) {
            avt_log(NULL, AVT_LOG_ERROR, "Couldn't open %s: %i!\n", path,
                    err);
            return err;
        }

        if (is_out) {
            AVTOutputOptions opts = {
            };

            err = avt_output_open(io->avt, &io->out, io->conn, &opts);
            if (err < 0) {
                avt_log(NULL, AVT_LOG_ERROR, "Couldn't open %s for writing: %i!\n", path,
                        err);
                return err;
            }

            io->st = avt_output_stream_add(io->out, 12765);

            avt_output_stream_update(io->out, io->st);
        } else {

        }
        break;
    }
    case IO_RAW: {
        io->raw = fopen(path, is_out ? "w+" : "r");
        if (!io->raw) {
            avt_log(NULL, AVT_LOG_ERROR, "Couldn't open %s: %i!\n", path, errno);
            return AVT_ERROR(errno);
        }
        break;
    }
    case IO_LAVF: {
#ifdef HAVE_FFMPEG
        if (is_out) {
            err = avformat_alloc_output_context2(&io->avf, NULL, NULL, path);
            if (err < 0) {
                avt_log(NULL, AVT_LOG_ERROR, "Couldn't open output %s: %s!\n", path,
                        av_err2str(err));
                return err;
            }

            /* Open for writing */
            err = avio_open(&io->avf->pb, path, AVIO_FLAG_WRITE);
            if (err < 0) {
                avt_log(NULL, AVT_LOG_ERROR, "Couldn't open output %s: %s!\n", path,
                        av_err2str(err));
                return err;
            }
        } else {
            err = avformat_open_input(&io->avf, path, NULL, NULL);
            if (err < 0) {
                avt_log(NULL, AVT_LOG_ERROR, "Couldn't initialize demuxer: %s!\n",
                        av_err2str(err));
                return err;
            }

            err = avformat_find_stream_info(io->avf, NULL);
            if (err < 0) {
                avt_log(NULL, AVT_LOG_ERROR, "Couldn't find stream info: %s!\n",
                        av_err2str(err));
                return err;
            }
        }
#else
        avt_log(NULL, AVT_LOG_ERROR, "FFmpeg support not compiled in, cannot open %s!\n", path);
        return AVT_ERROR(EINVAL);
#endif
        break;
    }
    };

    avt_log(NULL, AVT_LOG_INFO, "Opened file %s for %s\n", path, is_out ? "writing" : "reading");

    return 0;
}

static noreturn void on_quit_signal(int signo)
{
    int err = 0;
    exit(err);
}

int main(int argc, char **argv)
{
    int err;

    GEN_OPT_INIT(opts_list, 16);
    GEN_OPT_SEC(opts_list, "Input/Output");
    GEN_OPT_ARR(opts_list, char *, input,   "i", 1, MAX_INPUTS, 0, 0, "Input (files or URLs)");
    GEN_OPT_ONE(opts_list, char *, output, "o", 1, 1, NULL, 0, 0, "Destination (file or URL)");
    GEN_OPT_SEC(opts_list, "Miscellaneous");
    GEN_OPT_ONE(opts_list, bool  , unround, "u", 0, 0, false, 0, 0, "Unround timestamps (for remuxing from Matroska)");
    GEN_OPT_ONE(opts_list, char *, mirror,  "m", 1, 1, NULL, 0, 0, "Mirror input and output to a file for monitoring and caching");

    if ((err = GEN_OPT_PARSE(opts_list, argc, argv)) < 0)
        return err;

    if (!input[0]) {
        avt_log(NULL, AVT_LOG_ERROR, "At least one input required!\n");
        return EINVAL;
    }

    if (signal(SIGINT, on_quit_signal) == SIG_ERR)
        avt_log(NULL, AVT_LOG_ERROR, "Can't init signal handler!\n");

    /* Create inputs */
    IOContext in[MAX_INPUTS] = { 0 };
    for (int i = 0; input[i]; i++) {
        err = open_io(&in[i], input[i], 0);
        if (err < 0)
            goto end;
    }

    IOContext out = { 0 };
    if (output) {
        err = open_io(&out, output, 1);
        if (err < 0)
            goto end;
    }

    switch (in[0].mode) {
    case IO_AVT: {

        break;
    }
    case IO_RAW: {
        fseek(in[0].raw, 0L, SEEK_END);
        size_t len = ftell(in[0].raw);
        fseek(in[0].raw, 0L, SEEK_SET);
        uint8_t *data = malloc(len);
        if (!data)
            return AVT_ERROR(ENOMEM);

        fread(data, len, 1, in[0].raw);

        AVTBuffer *buf = avt_buffer_create(data, len, NULL, NULL);
        AVTPacket pkt = {
            .type = AVT_FRAME_TYPE_KEY,
            .data = buf,
        };

        avt_output_stream_data(out.st, &pkt);

        break;
    }
    case IO_LAVF: {
#ifdef HAVE_FFMPEG
        AVPacket *out_packet = av_packet_alloc();
        av_read_frame(in[0].avf, out_packet);

        /* Todo: import */
        AVTBuffer *buf = avt_buffer_create(out_packet->data, out_packet->size, NULL, NULL);

        AVTPacket pkt = {
            .type = AVT_FRAME_TYPE_KEY,
            .data = buf,
        };

        avt_output_stream_data(out.st, &pkt);
#endif
        break;
    }
    };

    for (int i = 0; input[i]; i++)
        close_io(&in[i], 0);
    close_io(&out, 1);

end:
    return 0;
}
