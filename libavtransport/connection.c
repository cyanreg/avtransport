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

#include "connection_internal.h"
#include "protocol_common.h"
#include "utils_internal.h"

#include "../config.h"

struct AVTConnection {
    AVTAddress addr;
    AVTContext *ctx;

    /* Protocol */
    const AVTProtocol *p;
    AVTProtocolCtx *p_ctx;

    /* File mirror */
    const AVTProtocol *mirror;
    AVTProtocolCtx *mirror_ctx;

    /* Output FIFO, pre-scheduler */
    AVTPacketFifo out_fifo_pre;
    AVTScheduler out_scheduler;
    AVTPacketFifo out_fifo_post;

    /* Input reorder buffer */
    AVTReorderBuffer in_buffer;
};

int avt_connection_create(AVTContext *ctx, AVTConnection **_conn,
                          AVTConnectionInfo *info)
{
    AVTAddress addr;
    int err = avt_addr_from_info(ctx, &addr, info);
    if (err < 0)
        return err;

    AVTConnection *conn = calloc(1, sizeof(*conn));

    conn->addr = addr;
    conn->ctx = ctx;

    /* Output scheduler */
    err = avt_scheduler_init(&conn->out_scheduler);
    if (err < 0)
        return err;

    /* Protocol init */
    err = avt_protocol_init(ctx, &conn->p, &conn->p_ctx, &addr);
    if (err < 0) {
        free(conn);
        return err;
    }

    *_conn = conn;

    return 0;
}

int avt_connection_send(AVTConnection *conn,
                        union AVTPacketData pkt, AVTBuffer *pl)
{
    int err;

    err = avt_pkt_fifo_push(&conn->out_fifo_pre, pkt, pl);
    if (err < 0)
        return err;

    err = avt_scheduler_push(&conn->out_scheduler, pkt, pl);
    if (err < 0)
        return err;



    return 0;
}

int avt_connection_flush(AVTConnection *conn)
{
    return 0;
}

int avt_connection_destroy(AVTConnection **_conn)
{
    AVTConnection *conn = *_conn;
    if (conn)
        return 0;

    int err = conn->p->close(conn->ctx, &conn->p_ctx);

    avt_pkt_fifo_free(&conn->out_fifo_post);
    avt_scheduler_free(&conn->out_scheduler);
    avt_pkt_fifo_free(&conn->out_fifo_pre);
    avt_addr_free(&conn->addr);

    free(conn);
    *_conn = NULL;
    return err;
}

int avt_connection_mirror(AVTConnection *conn, const char *path)
{
    return  0;
}
