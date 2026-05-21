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
#include <string.h>

#include <avtransport/avtransport.h>
#include <avtransport/receive.h>

#include "common.h"
#include "connection_internal.h"
#include "utils_internal.h"

struct AVTReceiver {
    AVTReceiveCallbacks cb;
    void *cb_opaque;
    AVTReceiveOptions opts;

    /* Connection this receiver is attached to. */
    AVTConnection *conn;

    /* Sparse table of allocated AVTStream pointers, indexed by stream_id. */
    AVTStream *streams[UINT16_MAX + 1];

    /* Set true once we've seen a stream's STREAM_REGISTRATION packet. */
    bool registered[UINT16_MAX + 1];
};

static AVTStream *get_or_alloc_stream(AVTReceiver *r, uint16_t id)
{
    if (r->streams[id])
        return r->streams[id];

    AVTStream *st = calloc(1, sizeof(*st));
    if (!st)
        return NULL;
    st->id = id;
    r->streams[id] = st;
    return st;
}

static void free_stream(AVTReceiver *r, uint16_t id)
{
    AVTStream *st = r->streams[id];
    if (!st)
        return;
    avt_buffer_unref(&st->init_data);
    avt_buffer_unref(&st->icc_data);
    avt_buffer_unref(&st->lut_data);
    free(st->priv);
    free(st);
    r->streams[id] = NULL;
    r->registered[id] = false;
}

/* Populate an AVTPacket from a decoded stream-data union and assembled
 * payload buffer (transferring the ref into pkt->data). */
static void populate_packet(AVTPacket *pkt, const union AVTPacketData *src,
                            AVTBuffer *buf)
{
    *pkt = (AVTPacket){ };
    pkt->data = buf;
    pkt->total_size = buf ? avt_buffer_get_data_len(buf) : 0;

    uint32_t desc = src->desc & ~AVT_PKT_FLAG_LSB_BITMASK;
    if (desc == (AVT_PKT_STREAM_DATA & ~AVT_PKT_FLAG_LSB_BITMASK)) {
        pkt->type = src->stream_data.frame_type;
        pkt->pts = src->stream_data.pts;
        pkt->dts = src->stream_data.pts;
        pkt->duration = src->stream_data.duration;
    } else if (desc == (AVT_PKT_EXTENDED_STREAM_DATA & ~AVT_PKT_FLAG_LSB_BITMASK)) {
        pkt->type = src->extended_stream_data.frame_type;
        pkt->pts = src->extended_stream_data.pts;
        pkt->dts = src->extended_stream_data.dts;
        pkt->duration = src->extended_stream_data.duration;
    }
}

/* Dispatch one packet through the user's callbacks.
 * Consumes the buffer reference (or unrefs it). */
static int dispatch_packet(AVTReceiver *r, const union AVTPacketData *src,
                           AVTBuffer *buf)
{
    int ret = 0;
    uint32_t desc = src->desc & ~AVT_PKT_FLAG_LSB_BITMASK;

    switch (desc) {
    case AVT_PKT_SESSION_START:
        /* Internal: nothing to dispatch. */
        break;

    case AVT_PKT_TIME_SYNC & ~AVT_PKT_FLAG_LSB_BITMASK:
        if (r->cb.time_sync_cb)
            r->cb.time_sync_cb(r->cb_opaque, src->time_sync.epoch);
        break;

    case AVT_PKT_STREAM_REGISTRATION: {
        uint16_t id = src->stream_registration.stream_id;
        AVTStream *st = get_or_alloc_stream(r, id);
        if (!st) { ret = AVT_ERROR(ENOMEM); break; }

        st->codec_id = src->stream_registration.codec_id;
        st->timebase = src->stream_registration.timebase;
        st->flags = src->stream_registration.stream_flags;
        st->bitrate = src->stream_registration.bandwidth;

        bool first = !r->registered[id];
        r->registered[id] = true;
        if (first) {
            if (r->cb.stream_register_cb)
                ret = r->cb.stream_register_cb(r->cb_opaque, st);
        } else {
            if (r->cb.stream_update_cb)
                r->cb.stream_update_cb(r->cb_opaque, st);
        }
        break;
    }

    case AVT_PKT_VIDEO_INFO: {
        uint16_t id = src->video_info.stream_id;
        AVTStream *st = get_or_alloc_stream(r, id);
        if (!st) { ret = AVT_ERROR(ENOMEM); break; }
        st->video_info = src->video_info;
        if (r->cb.stream_update_cb)
            r->cb.stream_update_cb(r->cb_opaque, st);
        break;
    }

    case AVT_PKT_VIDEO_ORIENTATION: {
        uint16_t id = src->video_orientation.stream_id;
        AVTStream *st = get_or_alloc_stream(r, id);
        if (!st) { ret = AVT_ERROR(ENOMEM); break; }
        st->video_orientation = src->video_orientation;
        if (r->cb.stream_update_cb)
            r->cb.stream_update_cb(r->cb_opaque, st);
        break;
    }

    case AVT_PKT_FONT_DATA: {
        uint16_t id = src->font_data.stream_id;
        AVTStream *st = r->streams[id];   /* may be NULL for global fonts */
        if (r->cb.font_register_cb) {
            char name[252] = { };
            memcpy(name, src->font_data.font_name, sizeof(src->font_data.font_name));
            r->cb.font_register_cb(r->cb_opaque, st, buf, name);
        }
        break;
    }

    case AVT_PKT_USER_DATA:
        if (r->cb.user_pkt_cb)
            ret = r->cb.user_pkt_cb(r->cb_opaque, buf,
                                    src->user_data.user_data_descriptor,
                                    src->user_data.user_field,
                                    src->user_data.global_seq);
        break;

    case AVT_PKT_STREAM_END: {
        uint16_t id = src->stream_end.stream_id;
        AVTStream *st = r->streams[id];
        if (st && r->cb.stream_close_cb)
            r->cb.stream_close_cb(r->cb_opaque, st);
        free_stream(r, id);
        break;
    }

    case AVT_PKT_STREAM_DATA & ~AVT_PKT_FLAG_LSB_BITMASK:
    case AVT_PKT_EXTENDED_STREAM_DATA & ~AVT_PKT_FLAG_LSB_BITMASK: {
        uint16_t id = src->stream_id;
        AVTStream *st = r->streams[id];
        if (!st) {
            avt_log(NULL, AVT_LOG_WARN,
                    "Stream data for unregistered stream 0x%X dropped\n", id);
            break;
        }
        if (r->cb.stream_pkt_cb) {
            AVTPacket pkt;
            populate_packet(&pkt, src, buf);
            ret = r->cb.stream_pkt_cb(r->cb_opaque, st, pkt);
        }
        break;
    }

    default:
        /* Metadata, FEC, hash, etc. - not yet dispatched. */
        break;
    }

    avt_buffer_unref(&buf);
    return ret;
}

int avt_receive_open(AVTContext *ctx, AVTConnection *conn,
                     AVTReceiveCallbacks *cb, void *cb_opaque,
                     AVTReceiveOptions *opts)
{
    if (!ctx || !conn || !cb)
        return AVT_ERROR(EINVAL);
    if (ctx->recv)
        return AVT_ERROR(EBUSY);

    AVTReceiver *r = calloc(1, sizeof(*r));
    if (!r)
        return AVT_ERROR(ENOMEM);

    r->cb = *cb;
    r->cb_opaque = cb_opaque;
    if (opts)
        r->opts = *opts;
    r->conn = conn;

    ctx->recv = r;
    return 0;
}

int avt_receive_set_options(AVTContext *ctx, AVTReceiveOptions *opts)
{
    if (!ctx || !ctx->recv || !opts)
        return AVT_ERROR(EINVAL);
    ctx->recv->opts = *opts;
    return 0;
}

int avt_receive_close(AVTContext *ctx)
{
    if (!ctx || !ctx->recv)
        return 0;

    AVTReceiver *r = ctx->recv;
    ctx->recv = NULL;

    for (int i = 0; i <= UINT16_MAX; i++)
        free_stream(r, (uint16_t)i);
    free(r);
    return 0;
}

/* Called by avt_connection_process to drain available input and fire
 * callbacks. Returns AVT_ERROR(EAGAIN) if nothing happened, 0 if work
 * was done, AVT_ERROR(ENODATA) on EOF, or another negative error. */
int avt_receiver_drive(AVTContext *ctx, AVTConnection *conn, int64_t timeout)
{
    AVTReceiver *r = ctx ? ctx->recv : NULL;
    if (!r || r->conn != conn)
        return AVT_ERROR(EAGAIN);

    int drained = 0;
    while (1) {
        union AVTPacketData pkt;
        AVTBuffer *buf = NULL;
        int err = avt_connection_receive_packet(conn, &pkt, &buf, timeout);
        if (err == AVT_ERROR(EAGAIN))
            break;
        if (err == AVT_ERROR(ENODATA))
            return drained ? 0 : AVT_ERROR(ENODATA);
        if (err < 0) {
            avt_buffer_unref(&buf);
            return err;
        }
        drained++;
        err = dispatch_packet(r, &pkt, buf);
        if (err < 0)
            return err;
    }

    return drained ? 0 : AVT_ERROR(EAGAIN);
}
