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
#include <stdlib.h>
#include <inttypes.h>
#include <threads.h>

#include <avtransport/avtransport.h>
#include "net_io_common.h"

#include <stdio.h>

typedef struct NetListenerContext {
    AVTContext *avt;
    const AVTIO *io;
    AVTIOCtx *ioctx;
    AVTBuffer *output;
    size_t pkt_len;
    int nb_pkts;
    int64_t offset;
} NetListenerContext;

static int listener_fn(void *_ctx)
{
    NetListenerContext *ctx = _ctx;
    int64_t off = 0;

    /* Read */
    for (int i = 0; i < ctx->nb_pkts; i++) {
        AVTBuffer *slice = avt_buffer_ref(ctx->output, off, ctx->pkt_len);
        if (!slice)
            thrd_exit(AVT_ERROR(ENOMEM));

        int ret = ctx->io->read_input(ctx->ioctx, slice,
                                      ctx->pkt_len, INT64_MAX,
                                      AVT_IO_READ_MUTABLE);

        size_t read_bytes = avt_buffer_get_data_len(slice);
        avt_buffer_unref(&slice);
        if (ret < 0) {
            avt_log(ctx->avt, AVT_LOG_ERROR, "No bytes read\n");
            thrd_exit(AVT_ERROR(EINVAL));
        } else if (read_bytes != ctx->pkt_len) {
            avt_log(ctx->avt, AVT_LOG_ERROR, "Read length mismatch: received "
                    "%" PRIi64 " bytes, expected %zu bytes\n", read_bytes, ctx->pkt_len);
            thrd_exit(AVT_ERROR(EINVAL));
        } else {
            avt_log(ctx->avt, AVT_LOG_INFO, "Received %" PRIi64 " bytes\n", read_bytes);
            off += read_bytes;
        }
    }

    thrd_exit(off);
}

int net_io_test(NetTestContext *ntc)
{
    int64_t ret = 0;
    int t_err = thrd_success, t_res = 0;
    thrd_t listener_thread = 0;
    NetListenerContext listener_ctx = {
        .avt = ntc->avt,
        .io = ntc->io,
        .output = avt_buffer_alloc(1024*1024),
        .ioctx = ntc->ioctx_listener,
    };
    if (!listener_ctx.output)
        return AVT_ERROR(ENOMEM);

    int64_t mtu;
    ret = ntc->io->get_max_pkt_len(ntc->ioctx_sender, &mtu);
    if (ret < 0) {
        avt_log(ntc->avt, AVT_LOG_ERROR, "Error getting MTU!\n");
        goto fail;
    }

    avt_log(ntc->avt, AVT_LOG_INFO, "MTU received = %" PRIi64 "\n", mtu);

    /* Random packet data */
    int64_t sum = 0;
    AVTPktd test_pkt[16] = { };
    for (int i = 0; i < AVT_ARRAY_ELEMS(test_pkt); i++) {
        test_pkt[i].hdr_len = AVT_MIN(mtu, sizeof(test_pkt[i].hdr));
        sum += test_pkt[i].hdr_len;
        for (int j = 0; j < test_pkt[i].hdr_len; j++)
            test_pkt[i].hdr[j] = (rand() & 0xFF);
    }

    listener_ctx.pkt_len = test_pkt[0].hdr_len;
    listener_ctx.nb_pkts = AVT_ARRAY_ELEMS(test_pkt);
    t_err = thrd_create(&listener_thread, listener_fn, &listener_ctx);
    if (t_err != thrd_success)
        goto fail;

    /* Write vector test */
    ret = ntc->io->write_vec(ntc->ioctx_sender, test_pkt,
                             AVT_ARRAY_ELEMS(test_pkt), INT64_MAX);
    if (ret < 0)
        goto fail;
    avt_log(ntc->avt, AVT_LOG_INFO,"Sent %" PRIu64 " packets\n", AVT_ARRAY_ELEMS(test_pkt));

    if (thrd_join(listener_thread, &t_res) != thrd_success)
        goto fail;

    size_t buf_size;
    uint8_t *buf_data = avt_buffer_get_data(listener_ctx.output, &buf_size);

    /* Two loops as packets may be received out of order */
    for (int i = 0; i < listener_ctx.nb_pkts; i++) {
        bool found = false;
        for (int j = (listener_ctx.nb_pkts - 1); j >= 0; j--) {
            if (!memcmp(buf_data, test_pkt[j].hdr, test_pkt[j].hdr_len)) {
                found = true;
                break;
            }
        }
        if (!found) {
            avt_log(ntc->avt, AVT_LOG_ERROR,"Data written and read does not match (pkt %i)\n", i);
            ret = AVT_ERROR(EINVAL);
            goto fail;
        }
        buf_data += test_pkt[i].hdr_len;
    }

    listener_ctx.pkt_len = test_pkt[0].hdr_len;
    listener_ctx.nb_pkts = 1;

    t_err = thrd_create(&listener_thread, listener_fn, &listener_ctx);
    if (t_err != thrd_success)
        goto fail;

    /* Write vector test */
    ret = ntc->io->write_pkt(ntc->ioctx_sender, &test_pkt[0],
                             INT64_MAX);
    if (ret < 0)
        goto fail;
    avt_log(ntc->avt, AVT_LOG_INFO,"Sent a single packet\n");

    if (thrd_join(listener_thread, &t_res) != thrd_success)
        goto fail;

    buf_data = avt_buffer_get_data(listener_ctx.output, &buf_size);
    if (memcmp(buf_data, test_pkt[0].hdr, test_pkt[0].hdr_len)) {
       avt_log(ntc->avt, AVT_LOG_ERROR,"Data written and read does not match\n");
       ret = AVT_ERROR(EINVAL);
       goto fail;
    }

fail:
    if (ret >= 0 && t_res)
        ret = t_res;

    if (ret >= 0 && (t_err != thrd_success)) {
        avt_log(ntc->avt, AVT_LOG_ERROR, "Threading error\n");
        ret = AVT_ERROR(EINVAL);
        listener_thread = 0;
    }

    if (listener_thread)
        thrd_join(listener_thread, NULL);

    avt_buffer_unref(&listener_ctx.output);

    return ret;
}

int net_io_init(NetTestContext *ntc, const AVTIO *io, const char *url)
{
    int ret;
    AVTAddress listener_addr = { };
    AVTAddress sender_addr = { };

    memset(ntc, 0, sizeof(*ntc));
    ntc->io = io;

    ret = avt_init(&ntc->avt, NULL);
    if (ret < 0)
        goto fail;

    /* Listener */
    ret = avt_addr_from_url(ntc->avt, &listener_addr, true, url);
    if (ret < 0)
        goto fail;

    ret = ntc->io->init(ntc->avt, &ntc->ioctx_listener, &listener_addr);
    if (ret < 0)
        goto fail;

    /* Sender */
    ret = avt_addr_from_url(ntc->avt, &sender_addr, false, url);
    if (ret < 0)
        goto fail;

    ret = ntc->io->init(ntc->avt, &ntc->ioctx_sender, &sender_addr);
    if (ret < 0)
        goto fail;

    avt_addr_free(&listener_addr);
    avt_addr_free(&sender_addr);
    return ret;

fail:
    avt_addr_free(&listener_addr);
    avt_addr_free(&sender_addr);
    net_io_free(ntc);
    return ret;
}

int net_io_free(NetTestContext *ntc)
{
    int err = ntc->io->close(&ntc->ioctx_sender);
    if (err)
        ntc->io->close(&ntc->ioctx_listener);
    else
        err = ntc->io->close(&ntc->ioctx_listener);
    avt_close(&ntc->avt);
    return err;
}
