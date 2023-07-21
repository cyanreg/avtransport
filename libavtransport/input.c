/*
 * Copyright © 2022 Lynne
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the “Software”), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "common.h"
#include "buffer.h"
#include "utils.h"
#include "input.h"

extern const PQInput pq_input_file;

static const PQInput *pq_input_list[] = {
    &pq_input_file,

    NULL,
};

int avt_input_open(AVTContext *ctx, AVTInputSource *src,
                   AVTInputCallbacks *cb, void *cb_opaque,
                   AVTInputOptions *opts)
{
    const PQInput *in;
    for (in = pq_input_list[0]; in; in++) {
        if (in->type == src->type)
            break;
    }

    if (!in)
        return AVT_ERROR(ENOTSUP);

    ctx->src.cb = in;
    ctx->src.src = *src;
    ctx->src.proc = *cb;
    ctx->src.cb_opaque = cb_opaque;

    return in->init(ctx, &ctx->src.ctx, src, opts);
}

int avt_input_process(AVTContext *ctx, int64_t timeout)
{
    if (!ctx->src.ctx)
        return AVT_ERROR(EINVAL);

    return ctx->src.cb->process(ctx, ctx->src.ctx);
}

static int pq_input_stream_reg(AVTContext *ctx, PQByteStream *bs,
                               AVTBuffer *buf)
{
    pq_bs_skip(bs, 16);
    uint16_t st_id = pq_bs_read_b16(bs);
    pq_bs_skip(bs, 32);
    pq_bs_skip(bs, 32);
    uint16_t st_id_rel = pq_bs_read_b16(bs);
    uint16_t st_id_der = pq_bs_read_b16(bs);
    uint64_t bandwidth = pq_bs_read_b64(bs);
    uint64_t flags = pq_bs_read_b64(bs);
    pq_bs_skip(bs, 64);
    uint32_t codec_id = pq_bs_read_b32(bs);
    AVTRational tb = { pq_bs_read_b32(bs), pq_bs_read_b32(bs) };

    AVTStream *st = avt_find_stream(ctx, st_id);
    if (!st) {
        st = avt_alloc_stream(ctx, st_id);
        if (!st)
            return AVT_ERROR(ENOMEM);
    }

    st->codec_id = codec_id;
    st->bitrate = bandwidth;
    st->flags = flags;
    st->timebase = tb;

    if (st_id_rel != st_id) {
        AVTStream *rel_stream = avt_find_stream(ctx, st_id_rel);
        if (!rel_stream)
            pq_log(ctx, AVT_LOG_ERROR, "Invalid related stream ID: %i\n", st_id_rel);
        else
            st->related_to = rel_stream;
    }
    if (st_id_der != st_id) {
        AVTStream *derived_stream = avt_find_stream(ctx, st_id_der);
        if (!derived_stream)
            pq_log(ctx, AVT_LOG_ERROR, "Invalid derived stream ID: %i\n", st_id_der);
        else
            st->derived_from = derived_stream;
     }

    int ret = ctx->src.proc.stream_register_cb(ctx->src.cb_opaque, st);

    return ret;
}

static int pq_input_stream_data(AVTContext *ctx, PQByteStream *bs,
                                AVTBuffer *buf)
{
    uint8_t  desc = pq_bs_read_b16(bs);
    uint16_t st_id = pq_bs_read_b16(bs);
    pq_bs_skip(bs, 32);
    int64_t pts = pq_bs_read_b64(bs); // TODO: fix
    int64_t duration = pq_bs_read_b64(bs); // TODO: fix
    uint32_t len = pq_bs_read_b32(bs);

    if (len != avt_buffer_get_data_len(buf) - 36)
        pq_log(ctx, AVT_LOG_ERROR, "Error: mismatching stream packet length field!\n");

    pq_buffer_offset(buf, 36);

    AVTStream *st = avt_find_stream(ctx, st_id);
    if (!st)
        pq_log(ctx, AVT_LOG_ERROR, "Error: invalid stream ID!\n");

    AVTPacket pkt = { 0 };





    pkt.pts = pts;
    pkt.dts = pts;
    pkt.duration = duration;
    pkt.type = desc & 0xFF;
    pkt.data = buf;

    int ret = ctx->src.proc.stream_pkt_cb(ctx->src.cb_opaque, st, pkt, 1);

    return ret;
}

static int pq_input_user_data(AVTContext *ctx, PQByteStream *bs,
                              AVTBuffer *buf)
{
    uint16_t desc = pq_bs_read_b16(bs);
    uint16_t user_field = pq_bs_read_b16(bs);
    uint32_t seq = pq_bs_read_b32(bs);
    uint32_t len = pq_bs_read_b32(bs);

    if (len != avt_buffer_get_data_len(buf) - 36)
        pq_log(ctx, AVT_LOG_ERROR, "Error: mismatching user packet length field!\n");

    pq_buffer_offset(buf, 36);

    int ret = ctx->src.proc.user_pkt_cb(ctx->src.cb_opaque, buf, desc,
                                       user_field, seq);

    return ret;
}

static int pq_input_demux(AVTContext *ctx, AVTBuffer *buf)
{
    size_t len;
    uint8_t *data = avt_buffer_get_data(buf, &len);

    /* Packets are never smaller than 36 bytes */
    if (len < 36)
        return AVT_ERROR(EINVAL);

    PQ_INIT(bs, data, len)

    uint16_t desc = pq_bs_read_b16(&bs);

    PQ_RESET(bs);

    /* Filter out the non-flag, non-arbitrary bits. */
    uint16_t desc_h = desc & 0xFF00;
    if (!(desc_h & (AVT_PKT_STREAM_DATA & 0xFF00)))
        pq_input_stream_data(ctx, &bs, buf);
    if (!(desc_h & (AVT_PKT_USER_DATA & 0xFF00)))
        pq_input_user_data(ctx, &bs, buf);

    switch (desc) {
    case AVT_PKT_STREAM_REG:
        pq_input_stream_reg(ctx, &bs, buf);
        break;
    }

    avt_buffer_unref(&buf);

    return 0;
}

int avt_input_close(AVTContext *ctx)
{
    if (!ctx->src.ctx)
        return AVT_ERROR(EINVAL);

    return ctx->src.cb->close(ctx, &ctx->src.ctx);
}
