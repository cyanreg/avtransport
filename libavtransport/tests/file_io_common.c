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
#include <inttypes.h>

#include "file_io_common.h"

#include <avtransport/avtransport.h>
#include "packet_common.h"

static int read_fn(AVTContext *avt, const AVTIO *io, AVTIOCtx *io_ctx,
                   AVTPktd *test_pkt, int bytes)
{
    int64_t ret;
    size_t test_buf_size;
    uint8_t *test_buf_data;

    AVTBuffer *buf = avt_buffer_alloc(32768);
    if (!buf)
        return AVT_ERROR(ENOMEM);

    /* Read */
    ret = io->read_input(io_ctx, buf, bytes, INT64_MAX, 0);

    test_buf_data = avt_buffer_get_data(buf, &test_buf_size);
    if (test_buf_size != bytes) {
        avt_log(avt, AVT_LOG_ERROR, "Too few bytes read at offset %" PRIi64 ": got %" PRIi64 "; wanted %i\n",
                ret, test_buf_size, bytes);
        avt_buffer_unref(&buf);
        return AVT_ERROR(EINVAL);
    }

    /* Check data read */
    if (memcmp(test_buf_data, test_pkt->hdr, test_buf_size)) {
        avt_log(avt, AVT_LOG_ERROR, "Mismatch between read and written data!\n");
        avt_buffer_unref(&buf);
        return AVT_ERROR(EINVAL);
    }
    avt_buffer_unref(&buf);

    return 0;
}

int file_io_test(AVTContext *avt, const AVTIO *io, AVTIOCtx *io_ctx)
{
    int64_t ret;

    /* Packet data */
    int64_t sum = 0;
    AVTPktd test_pkt[16] = { };
    for (int i = 0; i < AVT_ARRAY_ELEMS(test_pkt); i++) {
        test_pkt[i].hdr_len = sizeof(test_pkt[i].hdr);
        sum += test_pkt[i].hdr_len;
        for (int j = 0; j < test_pkt[i].hdr_len; j++)
            test_pkt[i].hdr[j] = (i + j) & 0xFF;
    }

    ret = io->write_vec(io_ctx, test_pkt, AVT_ARRAY_ELEMS(test_pkt),
                        INT64_MAX);
    if (ret < 0)
        goto fail;

    /* Flush test */
    ret = io->flush(io_ctx, INT64_MAX);
    if (ret < 0)
        goto fail;

    /* Seek test */
    ret = io->seek(io_ctx, 0);
    if (ret < 0)
        goto fail;

    /* Read test */
    for (int i = 0; i < AVT_ARRAY_ELEMS(test_pkt); i++) {
        AVTBuffer *buf = avt_buffer_alloc(32768);
        if (!buf)
            return AVT_ERROR(ENOMEM);

        int64_t off = io->read_input(io_ctx, buf, test_pkt[i].hdr_len,
                                     INT64_MAX, 0x0);
        if (off < 0) {
            avt_log(avt, AVT_LOG_ERROR,"Error reading\n");
            avt_buffer_unref(&buf);
            ret = off;
            goto fail;
        }

        size_t len = avt_buffer_get_data_len(buf);

        if (len != test_pkt[i].hdr_len) {
            avt_log(avt, AVT_LOG_ERROR,"Too few bytes read: got %" PRIi64 "; wanted %i\n",
                    len, test_pkt[i].hdr_len);
            avt_buffer_unref(&buf);
            ret = AVT_ERROR(EINVAL);
            goto fail;
        }

        size_t test_buf_size;
        uint8_t *test_buf_data = avt_buffer_get_data(buf, &test_buf_size);
        if (memcmp(test_buf_data, test_pkt[i].hdr, test_buf_size)) {
            avt_log(avt, AVT_LOG_ERROR,"Mismatch between read and written data!\n");
            avt_buffer_unref(&buf);
            goto fail;
        }

        avt_buffer_unref(&buf);
    }

    /* Seek test */
    ret = io->seek(io_ctx, 0);
    if (ret < 0)
        goto fail;

    /* Read 32 bytes */
    ret = read_fn(avt, io, io_ctx, &test_pkt[0], 32);
    if (ret < 0)
        goto fail;

    /* Seek test */
    ret = io->seek(io_ctx, 0);
    if (ret < 0)
        goto fail;

    /* Rewrite test */
    for (int i = 0; i < test_pkt[0].hdr_len; i++)
        test_pkt[0].hdr[i] = ~test_pkt[0].hdr[i];

    ret = io->rewrite(io_ctx, &test_pkt[0], 0,
                      INT64_MAX);
    if (ret < 0)
        goto fail;

    /* Read again */
    ret = read_fn(avt, io, io_ctx, &test_pkt[0], test_pkt[0].hdr_len);
    if (ret < 0)
        goto fail;

    ret = 0;

fail:
    return ret;
}
