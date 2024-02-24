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

#include <inttypes.h>
#include <string.h>
#include <avtransport/utils.h>

#include "packet_common.h"

static int read_fn(AVTContext *avt, const AVTIO *io, AVTIOCtx *io_ctx,
                   AVTPktd *test_pkt, AVTBuffer **test_buf, int bytes)
{
    int64_t ret;
    size_t pre = 0;
    size_t test_buf_size;
    uint8_t *test_buf_data;

    if ((*test_buf))
        avt_buffer_get_data(*test_buf, &pre);

    /* Read */
    ret = io->read_input(avt, io_ctx, test_buf, bytes, INT64_MAX);
    if (ret <= 0) {
        printf("No bytes read\n");
        return AVT_ERROR(EINVAL);
    }

    if ((ret - pre) != bytes) {
        printf("Too few bytes read: got %" PRIi64 "; wanted %i\n", ret, bytes);
        return AVT_ERROR(EINVAL);
    }

    /* Check data read */
    test_buf_data = avt_buffer_get_data(*test_buf, &test_buf_size);
    if (memcmp(test_buf_data, test_pkt->hdr, test_buf_size)) {
        printf("Mismatch between read and written data!\n");
        return AVT_ERROR(EINVAL);
    }

    return 0;
}

static int test_io(AVTContext *avt, const AVTIO *io, AVTIOCtx *io_ctx)
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

    /* Write vector test */
    AVTBuffer *buf = NULL;
    ret = io->write_vec(avt, io_ctx, test_pkt, AVT_ARRAY_ELEMS(test_pkt),
                        INT64_MAX);
    if (ret < 0) {
        goto fail;
    } else if (ret == 0) {
        printf("No bytes written\n");
        ret = AVT_ERROR(EINVAL);
        goto fail;
    } else if (ret != sum) {
        printf("Bytes written do not match: got %" PRIi64 "; wanted %" PRIi64 "\n",
               ret, sum);
        ret = AVT_ERROR(EINVAL);
        goto fail;
    }

    /* Flush test */
    ret = io->flush(avt, io_ctx, INT64_MAX);
    if (ret < 0)
        goto fail;

    /* Seek test */
    ret = io->seek(avt, io_ctx, 0);
    if (ret < 0)
        goto fail;

    /* Read test */
    int64_t last = 0;
    for (int i = 0; i < AVT_ARRAY_ELEMS(test_pkt); i++) {
        int64_t off = io->read_input(avt, io_ctx, &buf, test_pkt[i].hdr_len,
                                     INT64_MAX);
        if (off == 0) {
            printf("No bytes read\n");
            ret = AVT_ERROR(EINVAL);
            goto fail;
        } else if (off < 0) {
            printf("Error reading\n");
            ret = off;
            goto fail;
        }

        int64_t diff = off - last;
        last = off;

        if ((diff != test_pkt[i].hdr_len) ||
            ((i == (AVT_ARRAY_ELEMS(test_pkt) - 1)) && (sum != off))) {
            printf("Too few bytes read: got %" PRIi64 "; wanted %i\n",
                   diff, test_pkt[i].hdr_len);
            ret = AVT_ERROR(EINVAL);
            goto fail;
        }

        size_t test_buf_size;
        uint8_t *test_buf_data = avt_buffer_get_data(buf, &test_buf_size);
        if (memcmp(test_buf_data, test_pkt[i].hdr, test_buf_size)) {
            printf("Mismatch between read and written data!\n");
            goto fail;
        }

        avt_buffer_unref(&buf);
    }

    /* Seek test */
    ret = io->seek(avt, io_ctx, 0);
    if (ret < 0)
        goto fail;

    /* Read 32 bytes */
    ret = read_fn(avt, io, io_ctx, &test_pkt[0], &buf, 32);
    if (ret < 0)
        goto fail;

    /* Read again */
    ret = read_fn(avt, io, io_ctx, &test_pkt[0], &buf, 32);
    if (ret < 0)
        goto fail;

    /* Seek test */
    ret = io->seek(avt, io_ctx, 0);
    if (ret < 0)
        goto fail;

    /* Free buffer to start over */
    avt_buffer_unref(&buf);

    /* Rewrite test */
    for (int i = 0; i < test_pkt[0].hdr_len; i++)
        test_pkt[0].hdr[i] = ~test_pkt[0].hdr[i];
    ret = io->rewrite(avt, io_ctx, &test_pkt[0], 0,
                      INT64_MAX);
    if (ret < 0)
        goto fail;

    /* Read again in 1-byte chunks */
    for (int i = 0; i < 64; i++) {
        ret = read_fn(avt, io, io_ctx, &test_pkt[0], &buf, 1);
        if (ret < 0)
            goto fail;
    }

    ret = 0;

fail:
    avt_buffer_unref(&buf);
    return ret;
}
