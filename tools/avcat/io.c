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

#include "io.h"

/* Best-effort codec ID for raw elementary-stream inputs, by file extension.
 * Defaults to raw video for generic or unknown payloads. */
static enum AVTCodecID codec_from_path(const char *path)
{
    static const struct {
        const char *ext;
        enum AVTCodecID id;
    } map[] = {
        { ".h264", AVT_CODEC_ID_H264 },
        { ".h265", AVT_CODEC_ID_H265 },
        { ".hevc", AVT_CODEC_ID_H265 },
        { ".obu",  AVT_CODEC_ID_AV1  },
        { ".aac",  AVT_CODEC_ID_AAC  },
        { ".ac3",  AVT_CODEC_ID_AC3  },
        { ".tiff", AVT_CODEC_ID_TIFF },
        { ".dng",  AVT_CODEC_ID_TIFF },
        { ".jpeg", AVT_CODEC_ID_JPEG },
        { ".jpg",  AVT_CODEC_ID_JPEG },
        { ".png",  AVT_CODEC_ID_PNG  },
        { ".svg",  AVT_CODEC_ID_SVG  },
    };

    size_t len = strlen(path);
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        size_t elen = strlen(map[i].ext);
        if (len >= elen && !strcmp(&path[len - elen], map[i].ext))
            return map[i].id;
    }
    return AVT_CODEC_ID_RAW_VIDEO;
}

static enum IOMode path_id(const char *path,
                           enum AVTConnectionType *conn_type,
                           int is_out)
{
    int len = strlen(path);

    if (!strncmp(path, "avt://", strlen("avt://")) ||
        !strncmp(path, "quic://", strlen("quic://")) ||
        !strncmp(path, "udp://", strlen("udp://")) ||
        !strncmp(path, "socket://", strlen("socket://")) ||
        !strncmp(path, "file://", strlen("file://"))) {
        if (!strncmp(path, "udp://", strlen("udp://")))
            printf("Note: udp:// is AVTransport over UDP. "
                   "Use mpegts:// or rtp:// for anything else\n");
        *conn_type = AVT_CONNECTION_URL;
        return IO_AVT;
    } else if (!strcmp(&path[len - 4], ".avt") ||
               !strcmp(&path[len - 3], ".ata") || /* Audio */
               !strcmp(&path[len - 3], ".ati") || /* Images */
               !strcmp(&path[len - 3], ".ats")) { /* Subtitles */
        *conn_type = AVT_CONNECTION_FILE;
        return IO_AVT;
    } else if ((len >= 5 && (!strcmp(&path[len - 5], ".h264") ||
                             !strcmp(&path[len - 5], ".h265") ||
                             !strcmp(&path[len - 5], ".hevc") ||
                             !strcmp(&path[len - 5], ".tiff") ||
                             !strcmp(&path[len - 5], ".jpeg"))) ||
               (len >= 4 && (!strcmp(&path[len - 4], ".aac")  ||
                             !strcmp(&path[len - 4], ".ac3")  ||
                             !strcmp(&path[len - 4], ".nal")  || /* Raw MPEG NALUs */
                             !strcmp(&path[len - 4], ".obu")  || /* Raw AV1 OBUs */
                             !strcmp(&path[len - 4], ".raw") || /* Generic raw data */
                             !strcmp(&path[len - 4], ".dng")  ||
                             !strcmp(&path[len - 4], ".png")  ||
                             !strcmp(&path[len - 4], ".jpg")  ||
                             !strcmp(&path[len - 4], ".cbor") ||
                             !strcmp(&path[len - 4], ".svg")))) {
        return IO_RAW;
    } else {
        return IO_LAVF;
    }

    return 0;
}

int io_close(IOContext *io, int is_out)
{
    switch (io->mode) {
    case IO_AVT: {
        if (io->out)
            avt_send_close(&io->out);
        if (io->conn) {
            if (is_out)
                avt_connection_flush(io->conn, INT64_MAX);
            avt_connection_destroy(&io->conn);
        }
        break;
    }
    case IO_RAW: {
        if (io->raw)
            fclose(io->raw);
        break;
    }
    case IO_LAVF: {
#ifdef HAVE_FFMPEG
        if (!io->avf)
            break;
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

int io_open(IOContext *io, AVTContext *avt, const char *path, int is_out)
{
    int err;

    io->mode = path_id(path, &io->type, is_out);
    io->codec_id = AVT_CODEC_ID_RAW_VIDEO;

    switch (io->mode) {
    case IO_AVT: {
        AVTConnectionInfo conn_info = {
            .type = io->type,
            .listen = !is_out,
            .output_opts.bandwidth = is_out ? 80 * 1000 * 1000 : 0,
        };
        /* URL connections carry the listen flag inside the url sub-struct;
         * file-like ones use the top-level field. */
        if (io->type == AVT_CONNECTION_URL) {
            conn_info.url.url = path;
            conn_info.url.listen = !is_out;
        } else {
            conn_info.path = path;
        }
        err = avt_connection_init(avt, &io->conn, &conn_info);
        if (err < 0) {
            avt_log(NULL, AVT_LOG_ERROR, "Could not open %s: %i!\n", path,
                    err);
            return err;
        }

        if (is_out) {
            AVTSenderOptions opts = {
            };

            err = avt_send_open(avt, &io->out, io->conn, &opts);
            if (err < 0) {
                avt_log(NULL, AVT_LOG_ERROR, "Could not open %s as an output: %i\n", path,
                        err);
                return err;
            }

            /* Streams are added later, once the input parameters are
             * known (see avcat.c). */
        }
        break;
    }
    case IO_RAW: {
        io->raw = fopen(path, is_out ? "w+" : "r");
        if (!io->raw) {
            avt_log(NULL, AVT_LOG_ERROR, "Could not open %s: %i!\n", path, errno);
            return AVT_ERROR(errno);
        }
        io->codec_id = codec_from_path(path);
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
