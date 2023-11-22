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

#include <stdio.h>
#include <avtransport/avtransport.h>

#include "../config.h"

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

typedef struct IOContext {
    int is_avt;
    enum AVTConnectionType type;
    AVTContext *avt;
    AVTConnection *conn;
    AVTStream *st;
    AVTOutput *out;

#ifdef HAVE_FFMPEG
    AVFormatContext *avf;
#endif
} IOContext;

static int path_is_avt(const char *path, enum AVTConnectionType *conn_type)
{
    int len = strlen(path);
    if (!strncmp(path, "avt://", strlen("avt://")) ||
        !strncmp(path, "file://", strlen("file://"))) {
        *conn_type = AVT_CONNECTION_URL;
        return 1;
    } else if (!strcmp(&path[len - 4], ".avt") || !strcmp(&path[len - 3], ".at")) {
        *conn_type = AVT_CONNECTION_FILE;
        return 1;
    }
    return 0;
}

static int open_io(IOContext *io, const char *path, int is_out)
{
    int err;

    io->is_avt = path_is_avt(path, &io->type);

    if (io->is_avt) {
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
                .threads = 1,
            };

            err = avt_output_open(io->avt, &io->out, io->conn, &opts);
            if (err < 0) {
                avt_log(NULL, AVT_LOG_ERROR, "Couldn't open %s for writing: %i!\n", path,
                        err);
                return err;
            }

            io->st = avt_output_stream_add(io->out, 12765);

            avt_output_stream_update(io->out, io->st);
        }
    } else {
#ifdef HAVE_FFMPEG
        if (is_out) {
            err = avformat_alloc_output_context2(&io->avf, NULL, NULL, path);
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
    }

    avt_log(NULL, AVT_LOG_INFO, "Opened file %s for %s\n", path, is_out ? "writing" : "reading");

    return 0;
}

int main(int argc, char **argv)
{
    int err;

    GEN_OPT_INIT(opts_list, 16);
    GEN_OPT_ARR(opts_list, char *, input, "i", 1, MAX_INPUTS, 0, 0, "Input (files or URLs)");
    GEN_OPT_ONE(opts_list, char *, output, "o", 1, 1, 0, 0, "Destination (file or URL)");

    if ((err = GEN_OPT_PARSE(opts_list, argc, argv)))
        return err;

    if (!input[0]) {
        avt_log(NULL, AVT_LOG_ERROR, "At least one input required!\n");
        return EINVAL;
    }

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

    AVPacket *out_packet = av_packet_alloc();
    av_read_frame(in->avf, out_packet);

    AVTBuffer *buf = avt_buffer_create(out_packet->data, out_packet->size, NULL, NULL);

    AVTPacket pkt = {
        .type = AVT_FRAME_TYPE_KEY,
        .data = buf,
    };

    avt_output_stream_data(out.st, &pkt);

end:
    return 0;
}
