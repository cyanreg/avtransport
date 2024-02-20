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
#include <string.h>
#include <inttypes.h>

#include <avtransport/avtransport.h>
#include <avtransport/utils.h>
#include "packet_common.h"
#include "io_common.h"
#include "address.h"

/* Always available, on all platforms */
extern const AVTIO avt_io_file;

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
    ret = io->read_input(avt, io_ctx, test_buf, bytes);
    if (ret <= 0) {
        printf("No bytes read\n");
        return AVT_ERROR(EINVAL);
    }

    if ((ret - pre) != bytes) {
        printf("Too few bytes read: got %" PRIu64 "; wanted %i\n", ret, bytes);
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

int main(void)
{
    int ret;

    /* Open context */
    AVTContext *avt;
    ret = avt_init(&avt, NULL);
    if (ret < 0)
        return AVT_ERROR(ret);

    /* Open io context */
    const AVTIO *io = &avt_io_file;
    AVTIOCtx *io_ctx;
    AVTAddress addr = { .path = "io_file_test.avt" };

    ret = io->init(avt, &io_ctx, &addr);
    if (ret < 0) {
        printf("Unable to create test file: %s\n", addr.path);
        avt_close(&avt);
        return AVT_ERROR(ret);
    }

    AVTPktd test_pkt;
    test_pkt.hdr_len = sizeof(test_pkt.hdr);
    for (int i = 0; i < test_pkt.hdr_len; i++)
        test_pkt.hdr[i] = i & 0xFF;

    /* Write packet */
    AVTBuffer *buf = NULL;
    ret = io->write(avt, io_ctx, &test_pkt);
    if (ret < 0)
        goto fail;

    /* Flush test */
    ret = io->flush(avt, io_ctx);
    if (ret < 0)
        goto fail;

    /* Read 32 bytes */
    ret = read_fn(avt, io, io_ctx, &test_pkt, &buf, 32);
    if (ret < 0)
        goto fail;

    /* Read again */
    ret = read_fn(avt, io, io_ctx, &test_pkt, &buf, 32);
    if (ret < 0)
        goto fail;

    /* Seek test */
    ret = io->seek(avt, io_ctx, 0);
    if (ret < 0)
        goto fail;

    /* Free buffer to start over */
    avt_buffer_unref(&buf);

    /* Rewrite test */
    for (int i = 0; i < test_pkt.hdr_len; i++)
        test_pkt.hdr[i] = ~test_pkt.hdr[i];
    ret = io->rewrite(avt, io_ctx, &test_pkt, 0);
    if (ret < 0)
        goto fail;

    /* Read again in 1-byte chunks */
    for (int i = 0; i < 64; i++) {
        ret = read_fn(avt, io, io_ctx, &test_pkt, &buf, 1);
        if (ret < 0)
            goto fail;
    }

    ret = 0;

fail:
    avt_buffer_unref(&buf);

    if (ret)
        io->close(avt, &io_ctx);
    else
        ret = io->close(avt, &io_ctx);
    avt_close(&avt);
    return AVT_ERROR(ret);
}
