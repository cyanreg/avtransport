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
#include <stdlib.h>
#include <inttypes.h>
#include <threads.h>

#include "address.h"
#include "io_common.h"

extern const AVTIO avt_io_udp;
static const AVTIO *io = &avt_io_udp;

typedef struct ThreadCtx {
    AVTContext *avt;

    AVTAddress addr;

    AVTIOCtx *ioctx;
    AVTPktd test_pkts[16];
    int nb_pkts;

    AVTBuffer *output;

    int err;
} ThreadCtx;

static int free_context(thrd_t tctx, ThreadCtx *ctx)
{
    int ret;
    thrd_join(tctx, &ret);
    avt_addr_free(&ctx->addr);
    avt_buffer_unref(&ctx->output);
    if (ctx->ioctx) {
        if (ret < 0)
            io->close(ctx->avt, &ctx->ioctx);
        else
            ret = io->close(ctx->avt, &ctx->ioctx);
    }
    free(ctx);

    return ret;
}

static int client_fn(void *_ctx)
{
    int64_t ret;
    ThreadCtx *ctx = _ctx;

    /* Write single packet test */
    ret = io->write_pkt(ctx->avt, ctx->ioctx, &ctx->test_pkts[0], INT64_MAX);
    if (ret <= 0)
        printf("Error writing %" PRIi64 "\n", ret);
    else
        printf("Wrote %" PRIi64 " bytes\n", ret);

    thrd_exit(ret);
}

static int server_fn(void *_ctx)
{
    int64_t ret;
    ThreadCtx *ctx = _ctx;

    /* Read */
    ret = io->read_input(ctx->avt, ctx->ioctx, &ctx->output,
                         ctx->test_pkts[0].hdr_len,
                         INT64_MAX);
    if (ret <= 0) {
        printf("No bytes read\n");
    } else {
        printf("Received %" PRIi64 " bytes\n", ret);

        size_t buf_len;
        uint8_t *data = avt_buffer_get_data(ctx->output, &buf_len);
        if (memcmp(data, ctx->test_pkts[0].hdr, ctx->test_pkts[0].hdr_len)) {
            printf("Mismatch between data sent and received!\n");
            ret = AVT_ERROR(EINVAL);
        }
    }

    thrd_exit(ret);
}

int main(void)
{
    int64_t ret;
    AVTContext *avt;

    thrd_t server_thread = 0;
    ThreadCtx *server_ctx = calloc(1, sizeof(ThreadCtx));
    if (!server_ctx)
        return AVT_ERROR(ENOMEM);

    thrd_t client_thread = 0;
    ThreadCtx *client_ctx = calloc(1, sizeof(ThreadCtx));
    if (!client_ctx) {
        free(server_ctx);
        return AVT_ERROR(ENOMEM);
    }

    ret = avt_init(&avt, NULL);
    if (ret < 0)
        goto end;
    server_ctx->avt = avt;
    client_ctx->avt = avt;

    /** Server */
    ret = avt_addr_from_url(avt, &server_ctx->addr, true, "udp://[::1]");
    if (ret < 0)
        goto end;

    ret = io->init(avt, &server_ctx->ioctx, &server_ctx->addr);
    if (ret < 0)
        goto end;

    /** Client */
    ret = avt_addr_from_url(avt, &client_ctx->addr, false, "udp://[::1]");
    if (ret < 0)
        goto end;

    ret = io->init(avt, &client_ctx->ioctx, &client_ctx->addr);
    if (ret < 0)
        goto end;

    uint32_t mtu = io->get_max_pkt_len(avt, client_ctx->ioctx);
    if (!mtu) {
        ret = AVT_ERROR(EINVAL);
        goto end;
    }

    printf("MTU received = %u\n", mtu);

    /* Packet data */
    AVTPktd test_pkt[16] = { };
    for (int i = 0; i < AVT_ARRAY_ELEMS(test_pkt); i++) {
        test_pkt[i].hdr_len = sizeof(test_pkt[i].hdr);
        for (int j = 0; j < test_pkt[i].hdr_len; j++)
            test_pkt[i].hdr[j] = (i + j) & 0xFF;
    }

    memcpy(server_ctx->test_pkts, test_pkt, sizeof(test_pkt));
    thrd_create(&server_thread, server_fn, server_ctx);

    memcpy(client_ctx->test_pkts, test_pkt, sizeof(test_pkt));
    thrd_create(&client_thread, client_fn, client_ctx);

end:
    int err;

    err = free_context(client_thread, client_ctx);
    if (ret >= 0)
        ret = err;

    err = free_context(server_thread, server_ctx);
    if (ret >= 0)
        ret = err;

    avt_close(&avt);
    return AVT_ERROR(ret);
}
