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
#include <stdio.h>
#include <string.h>
#include <avtransport/version.h>

#include "common.h"
#include "connection_internal.h"
#include "protocol_common.h"
#include "io_common.h"
#include "utils_internal.h"
#include "scheduler.h"
#include "reorder.h"
#include "config.h"

#ifdef CONFIG_HAVE_LIBZSTD
#include <zstd.h>
#endif
#ifdef CONFIG_HAVE_LIBBROTLIDEC
#include <brotli/decode.h>
#endif

struct AVTConnection {
    AVTContext *ctx;
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
    AVTReorder    in_reorder;

    /* Single-packet receive state */
    AVTPacketFifo *recv_pending;
    unsigned int   recv_pending_idx;

    /* Output FIFO, pre-scheduler */
    AVTPacketFifo out_fifo_pre;
    AVTPacketFifo out_fifo_post;
    AVTScheduler  out_scheduler;
};

int avt_connection_destroy(AVTConnection **_conn)
{
    AVTConnection *conn = *_conn;
    if (!conn)
        return 0;

    int err = 0;
    if (conn->p_ctx)
        err = conn->p->close(&conn->p_ctx);

    if (conn->recv_pending) {
        avt_reorder_done(&conn->in_reorder, conn->recv_pending);
        conn->recv_pending = NULL;
    }
    avt_pkt_fifo_free(&conn->in_fifo);
    avt_reorder_free(&conn->in_reorder);
    avt_pkt_fifo_free(&conn->out_fifo_post);
    avt_scheduler_free(&conn->out_scheduler);
    avt_pkt_fifo_free(&conn->out_fifo_pre);
    avt_addr_free(&conn->addr);

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

    conn->ctx = ctx;

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

    /* Output scheduler. Use unlimited bandwidth if none specified
     * so output isn't blocked on a 0 bps budget. */
    int64_t bw = info->output_opts.bandwidth;
    if (bw <= 0)
        bw = INT64_MAX;
    ret = avt_scheduler_init(&conn->out_scheduler, max_pkt_size, bw,
                             info->output_opts.fec_parity_pct);
    if (ret < 0)
        goto fail;

    /* Input reorder buffer */
    ret = avt_reorder_init(ctx, &conn->in_reorder,
                           info->input_opts.buffer ?
                                info->input_opts.buffer : (16 * 1024 * 1024));
    if (ret < 0)
        goto fail;

    /* For read-only connections, don't emit a session start packet. */
    if (!info->listen) {
        conn->session_seq = avt_get_time_ns() & 0xFFFFFFFF;
        ret = send_session_start_pkt(conn);
        if (ret < 0)
            goto fail;
    }

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
    int send_err = 0;
    int recv_err = AVT_ERROR(EAGAIN);

    /* Try to drain any scheduled output */
    AVTPacketFifo *seq = NULL;
    int err = avt_scheduler_pop(&conn->out_scheduler, &seq);
    if (err == 0 && seq) {
        /* Ref segmented output packets for retransmission purposes */
        err = avt_pkt_fifo_copy(&conn->out_fifo_pre, seq);
        if (err < 0) {
            avt_scheduler_done(&conn->out_scheduler, seq);
            return err;
        }

        send_err = conn->p->send_seq(conn->p_ctx, seq, timeout);
        if (send_err < 0)
            avt_scheduler_done(&conn->out_scheduler, seq);
    } else if (err != AVT_ERROR(EAGAIN)) {
        send_err = err;
    }

    /* Drain available input through the receiver, if attached. */
    if (conn->ctx && conn->ctx->recv)
        recv_err = avt_receiver_drive(conn->ctx, conn, timeout);

    /* EOF wins over send errors so callers can exit cleanly. */
    if (recv_err == AVT_ERROR(ENODATA))
        return AVT_ERROR(ENODATA);
    if (send_err < 0 && send_err != AVT_ERROR(EAGAIN))
        return send_err;
    if (recv_err < 0 && recv_err != AVT_ERROR(EAGAIN))
        return recv_err;
    return 0;
}

/* Pull one packet from the protocol, push it through the reorder, and
 * return a fifo of assembled packets via *out. Caller must call
 * avt_connection_receive_done() to release the fifo. */
int avt_connection_receive(AVTConnection *conn, AVTPacketFifo **out,
                           int64_t timeout)
{
    int err;

    *out = NULL;

    /* Read one packet from the protocol into the input fifo */
    int io_err = conn->p->receive(conn->p_ctx, &conn->in_fifo, timeout);
    if (io_err < 0 && io_err != AVT_ERROR(EAGAIN) && io_err != AVT_ERROR(ENODATA))
        return io_err;

    /* If protocol indicated bypass-reordering or EOF/no-data, drop the
     * partially-pushed packet from in_fifo. */
    if (io_err == AVT_ERROR(EAGAIN) || io_err == AVT_ERROR(ENODATA))
        avt_pkt_fifo_clear(&conn->in_fifo);

    /* Push received packets through the reorder buffer */
    if (conn->in_fifo.nb > 0) {
        err = avt_reorder_push(&conn->in_reorder, &conn->in_fifo);
        if (err < 0)
            return err;
    }

    /* Try to pop a sequence of assembled packets. If nothing is ready and
     * the underlying input hit EOF, propagate that. */
    err = avt_reorder_pop(&conn->in_reorder, out);
    if (err == AVT_ERROR(EAGAIN) && io_err == AVT_ERROR(ENODATA))
        return AVT_ERROR(ENODATA);
    return err;
}

int avt_connection_receive_done(AVTConnection *conn, AVTPacketFifo *out)
{
    return avt_reorder_done(&conn->in_reorder, out);
}

/* Read the compression field for a packet, or AVT_DATA_COMPRESSION_NONE. */
static enum AVTDataCompression packet_compression(const union AVTPacketData *p)
{
    uint32_t desc = p->desc & ~AVT_PKT_FLAG_LSB_BITMASK;
    switch (desc) {
    case AVT_PKT_STREAM_DATA & ~AVT_PKT_FLAG_LSB_BITMASK:
        return p->stream_data.pkt_compression;
    case AVT_PKT_EXTENDED_STREAM_DATA & ~AVT_PKT_FLAG_LSB_BITMASK:
        return p->extended_stream_data.pkt_compression;
    case AVT_PKT_STREAM_CONFIG:
    case AVT_PKT_METADATA:
        return p->generic_data.generic_data_compression;
    case AVT_PKT_USER_DATA:
        return p->user_data.userdata_compression;
    case AVT_PKT_LUT_ICC:
        return p->lut_icc.lut_compression;
    case AVT_PKT_FONT_DATA:
        return p->font_data.font_compression;
    default:
        return AVT_DATA_COMPRESSION_NONE;
    }
}

/* Decompress a payload buffer in-place if needed. Returns 0 if no
 * change, 1 if decompressed (caller's buf replaced), or negative error. */
static int maybe_decompress(union AVTPacketData *pkt, AVTBuffer **buf)
{
    enum AVTDataCompression method = packet_compression(pkt);
    if (method == AVT_DATA_COMPRESSION_NONE || !*buf)
        return 0;

    size_t src_len;
    uint8_t *src = avt_buffer_get_data(*buf, &src_len);
    if (!src || !src_len)
        return 0;

    uint8_t *dst = NULL;
    size_t dst_len = 0;

    switch (method) {
#ifdef CONFIG_HAVE_LIBZSTD
    case AVT_DATA_COMPRESSION_ZSTD: {
        unsigned long long expected = ZSTD_getFrameContentSize(src, src_len);
        if (expected == ZSTD_CONTENTSIZE_ERROR || expected == ZSTD_CONTENTSIZE_UNKNOWN)
            expected = src_len * 16; /* fallback */
        dst = malloc(expected);
        if (!dst)
            return AVT_ERROR(ENOMEM);
        size_t r = ZSTD_decompress(dst, expected, src, src_len);
        if (ZSTD_isError(r)) {
            free(dst);
            return AVT_ERROR(EINVAL);
        }
        dst_len = r;
        break;
    }
#endif
#ifdef CONFIG_HAVE_LIBBROTLIDEC
    case AVT_DATA_COMPRESSION_BROTLI: {
        size_t cap = src_len * 16;
        dst = malloc(cap);
        if (!dst)
            return AVT_ERROR(ENOMEM);
        dst_len = cap;
        if (BrotliDecoderDecompress(src_len, src, &dst_len, dst) !=
            BROTLI_DECODER_RESULT_SUCCESS) {
            free(dst);
            return AVT_ERROR(EINVAL);
        }
        break;
    }
#endif
    default:
        /* Unsupported method - hand back the raw buffer. */
        return 0;
    }

    AVTBuffer *new_buf = avt_buffer_create(dst, dst_len, NULL,
                                           avt_buffer_default_free);
    if (!new_buf) {
        free(dst);
        return AVT_ERROR(ENOMEM);
    }

    avt_buffer_unref(buf);
    *buf = new_buf;
    return 1;
}

/* Public single-packet receive. Owns a small stash of an unread fifo
 * across calls so we can return one packet per call. */
int avt_connection_receive_packet(AVTConnection *conn,
                                  union AVTPacketData *pkt,
                                  AVTBuffer **buf,
                                  int64_t timeout)
{
    int err;

    /* If we have a previously-popped fifo with unread packets, take next */
    if (conn->recv_pending && conn->recv_pending->nb > conn->recv_pending_idx)
        goto deliver;

    /* If we held onto an empty fifo, release it back to the pool */
    if (conn->recv_pending) {
        avt_reorder_done(&conn->in_reorder, conn->recv_pending);
        conn->recv_pending = NULL;
        conn->recv_pending_idx = 0;
    }

    /* Try to get a new sequence of assembled packets. Loop until we
     * get something or the underlying IO returns EAGAIN/EOF. */
    int spins = 0;
    do {
        err = avt_connection_receive(conn, &conn->recv_pending, timeout);
        if (err == AVT_ERROR(EAGAIN)) {
            if (spins++ >= 64)
                return AVT_ERROR(EAGAIN);
            continue;
        }
        if (err == AVT_ERROR(ENODATA))
            return AVT_ERROR(ENODATA);
        if (err < 0)
            return err;
    } while (!conn->recv_pending);

    conn->recv_pending_idx = 0;

deliver: {
    AVTPktd *p = &conn->recv_pending->data[conn->recv_pending_idx++];
    *pkt = p->pkt;
    if (buf) {
        if (p->pl.refcnt && avt_buffer_get_data_len(&p->pl)) {
            *buf = avt_buffer_ref(&p->pl, 0, AVT_BUFFER_REF_ALL);
            if (!*buf)
                return AVT_ERROR(ENOMEM);
            int dret = maybe_decompress(pkt, buf);
            if (dret < 0) {
                avt_buffer_unref(buf);
                return dret;
            }
        } else {
            *buf = NULL;
        }
    }
    return 0;
}
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
