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

#include <stdlib.h>
#include <avtransport/version.h>

#include "connection_internal.h"
#include "protocol_common.h"
#include "io_common.h"
#include "utils_internal.h"
#include "scheduler.h"

struct AVTConnection {
    AVTAddress addr;
    uint32_t session_seq;

    /* I/O */
    const AVTIO *io;
    AVTIOCtx *io_ctx;

    /* Protocol */
    const AVTProtocol *p;
    AVTProtocolCtx *p_ctx;

    /* File mirror */
    const AVTProtocol *mirror;
    AVTProtocolCtx *mirror_ctx;

    /* Input buffer */
    AVTPacketFifo in_fifo;

    /* Output FIFO, pre-scheduler */
    AVTPacketFifo out_fifo_pre;
    AVTPacketFifo out_fifo_post;
    AVTScheduler  out_scheduler;
};

int avt_connection_destroy(AVTConnection **_conn)
{
    AVTConnection *conn = *_conn;
    if (conn)
        return 0;

    int err = conn->p->close(&conn->p_ctx);

    avt_pkt_fifo_free(&conn->out_fifo_post);
    avt_scheduler_free(&conn->out_scheduler);
    avt_pkt_fifo_free(&conn->out_fifo_pre);
    avt_addr_free(&conn->addr);

    if (conn->p_ctx)
        conn->p->close(&conn->p_ctx);

    if (conn->io_ctx)
        conn->io->close(&conn->io_ctx);

    free(conn);
    *_conn = NULL;
    return err;
}

static int send_session_start_pkt(AVTConnection *conn)
{
    AVTPktd p = {
        .pkt = AVT_SESSION_START_HDR(
            .session_seq = conn->session_seq,
            .session_flags = 0x0,
            .producer_major = AVTRANSPORT_VERSION_MAJOR,
            .producer_minor = AVTRANSPORT_VERSION_MINOR,
            .producer_micro = AVTRANSPORT_VERSION_MICRO,
        ),
    };

    memcpy(p.pkt.session_start.session_uuid, conn->addr.uuid, 16);
    memccpy(p.pkt.session_start.producer_name, "avtransport",
            '\0', sizeof(p.pkt.session_start.producer_name));

    int err = avt_scheduler_push(&conn->out_scheduler, &p);
    if (err < 0)
        return err;

    return 0;
}

int avt_connection_init(AVTContext *ctx, AVTConnection **_conn,
                        AVTConnectionInfo *info)
{
    int ret;
    AVTConnection *conn = calloc(1, sizeof(*conn));
    if (!conn)
        return AVT_ERROR(ENOMEM);

    /* Address parsing */
    ret = avt_addr_from_info(ctx, &conn->addr, info);
    if (ret < 0)
        goto fail;

    /* I/O init */
    ret = avt_io_init(ctx, &conn->io, &conn->io_ctx, &conn->addr);
    if (ret < 0)
        goto fail;

    /* Protocol init */
    AVTProtocolOpts opts = {
        .ldpc_iterations = info->input_opts.ldpc_iterations,
    };
    ret = avt_protocol_init(ctx, &conn->p, &conn->p_ctx, &conn->addr,
                            conn->io, conn->io_ctx, &opts);
    if (ret < 0)
        goto fail;

    /* Get max packet size */
    size_t max_pkt_size;
    ret = conn->p->get_max_pkt_len(conn->p_ctx, &max_pkt_size);
    if (ret < 0)
        goto fail;

    /* Output scheduler */
    ret = avt_scheduler_init(&conn->out_scheduler, max_pkt_size,
                             info->output_opts.bandwidth);
    if (ret < 0)
        goto fail;

    /* Write a session start packet */
    conn->session_seq = avt_get_time_ns() & 0xFFFFFFFF;
    ret = send_session_start_pkt(conn);
    if (ret < 0)
        goto fail;

    *_conn = conn;

    return 0;
fail:
    avt_connection_destroy(&conn);
    return ret;
}

int avt_connection_send(AVTConnection *conn, AVTPktd *p)
{
    int err;

    err = avt_scheduler_push(&conn->out_scheduler, p);
    if (err < 0)
        return err;

    return 0;
}

int avt_connection_process(AVTConnection *conn, int64_t timeout)
{
    int err;

    AVTPacketFifo *seq;
    err = avt_scheduler_pop(&conn->out_scheduler, &seq);
    if (err < 0)
        return err;

    /* Ref segmented output packets for retransmission purposes */
    err = avt_pkt_fifo_copy(&conn->out_fifo_pre, seq);
    if (err < 0)
        return err;

    err = conn->p->send_seq(conn->p_ctx, seq, timeout);
    if (err < 0)
        avt_scheduler_done(&conn->out_scheduler, seq);

    return err;
}

int avt_connection_flush(AVTConnection *conn, int64_t timeout)
{
    int err;

    AVTPacketFifo *seq;
    err = avt_scheduler_flush(&conn->out_scheduler, &seq);
    if (err < 0)
        return err;

    if (seq) {
        err = conn->p->send_seq(conn->p_ctx, seq, timeout);
        if (err < 0)
            avt_scheduler_done(&conn->out_scheduler, seq);
    }

    return conn->p->flush(conn->p_ctx, timeout);
}

int avt_connection_mirror_open(AVTContext *ctx, AVTConnection *conn,
                               AVTConnectionInfo *info)
{
    return  0;
}

int avt_connection_mirror_close(AVTContext *ctx, AVTConnection *conn)
{
    return  0;
}
