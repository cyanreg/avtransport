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
#include <stdnoreturn.h>
#include <signal.h>

#include <avtransport/avtransport.h>

#ifdef HAVE_FFMPEG
#include <libavformat/avformat.h>
#endif

#include "avcat/io.h"

#define GEN_OPT_MAX_ARR 16
#define GEN_OPT_LOG avt_log
#define GEN_OPT_LOG_CTX NULL
#define GEN_OPT_LOG_INFO AVT_LOG_INFO
#define GEN_OPT_LOG_ERROR AVT_LOG_ERROR
#define GEN_OPT_RATIONAL AVTRational
#define GEN_OPT_HELPSTRING "avcat " PROJECT_VERSION_STRING
#include "genopt.h"

#define MAX_INPUTS 4

typedef struct CatContext {
    AVTContext *avt;

    IOContext in[MAX_INPUTS];
    int nb_in;

    IOContext out;
} CatContext;

static volatile sig_atomic_t signal_terminate;
static void on_quit_signal(const int signo)
{
    signal_terminate = 1;
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
    IOContext in[MAX_INPUTS] = { 0 };
    for (int i = 0; input[i]; i++) {
        err = io_open(&in[i], input[i], 0);
        if (err < 0)
            goto end;
    }

    IOContext out = { 0 };
    if (output) {
        err = io_open(&out, output, 1);
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
            .pts = 0,
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
        io_close(&in[i], 0);
    io_close(&out, 1);

end:
    return 0;
}
