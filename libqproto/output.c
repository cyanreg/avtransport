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

#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "libqproto/video.h"
#include "output.h"
#include "utils.h"
#include "buffer.h"
#include "raptor.h"

extern const PQOutput pq_output_file;
extern const PQOutput pq_output_socket;

static const PQOutput *pq_output_list[] = {
    &pq_output_file,
    &pq_output_socket,

    NULL,
};

int qp_output_open(QprotoContext *qp, QprotoOutputDestination *dst,
                   QprotoOutputOptions *opts)
{
    if (qp->dst.ctx)
        return QP_ERROR(EINVAL);

    const PQOutput *out;
    for (out = pq_output_list[0]; out; out++) {
        if (out->type == dst->type)
            break;
    }

    if (!out)
        return QP_ERROR(ENOTSUP);

    qp->dst.cb = out;
    qp->dst.dst = *dst;

    int ret = out->init(qp, &qp->dst.ctx, dst, opts);
    if (ret < 0)
        return ret;

    uint8_t hdr[36];
    PQ_INIT(bs, hdr, sizeof(hdr))

    PQ_WBL(bs, 16, QP_PKT_SESSION_START);
    PQ_WBL(bs, 16, 0x0);
    PQ_WBL(bs, 32, atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed));
    PQ_WBL(bs, 8, QPMIN(strlen(PROJECT_NAME), 13));
    PQ_WST(bs, PROJECT_NAME, 13);
    PQ_WBL(bs, 16, PROJECT_VERSION_MAJOR);
    PQ_WBL(bs, 16, PROJECT_VERSION_MINOR);
    PQ_WBL(bs, 16, PROJECT_VERSION_MICRO);
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));

    ret = qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), NULL);
    if (ret < 0)
        qp->dst.cb->close(qp, &qp->dst.ctx);

    return ret;
}

int qp_output_set_epoch(QprotoContext *qp, uint64_t epoch)
{
    if (!qp->dst.ctx)
        return QP_ERROR(EINVAL);

    uint8_t hdr[36];
    PQ_INIT(bs, hdr, sizeof(hdr))

    PQ_WBL(bs, 16, QP_PKT_TIME_SYNC);
    PQ_WBL(bs, 16, 0);
    PQ_WBL(bs, 32, atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed));
    PQ_WBL(bs, 64, epoch);
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 32, 0);
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));

    return qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), NULL);
}

QprotoStream *qp_output_add_stream(QprotoContext *qp, uint16_t id)
{
    if (!qp->dst.ctx)
        return NULL;

    QprotoStream *ret = NULL;
    QprotoStream **new = realloc(qp->stream, sizeof(*new)*(qp->nb_stream + 1));
    if (!new)
        return NULL;
    qp->stream = new;

    ret = calloc(1, sizeof(**new));
    if (!ret)
        return NULL;

    ret->private = calloc(1, sizeof(*ret->private));
    if (!ret->private) {
        free(ret);
        return NULL;
    }

    new[qp->nb_stream] = ret;
    qp->nb_stream++;

    return ret;
}

static int pq_output_generic_segment(QprotoContext *qp, uint8_t *header,
                                     uint32_t header_pkt_seq, QprotoStream *st,
                                     QprotoBuffer *data, size_t offset, size_t max,
                                     uint16_t seg_desc, uint16_t term_desc)
{
    size_t len_tot = qp_buffer_get_data_len(data), len = len_tot, len_tgt;
    size_t off = offset;
    uint8_t h7 = 0;
    uint8_t hdr[36];
    PQ_INIT(bs, hdr, sizeof(hdr))
    QprotoBuffer tmp;
    int ret;

    do {
        PQ_RESET(bs);
        len_tgt = QPMIN(len, max);

        pq_buffer_quick_ref(&tmp, data, off, len_tgt);

        PQ_WBL(bs, 16, len < max ? term_desc : seg_desc);
        PQ_WBL(bs, 16, st->id);
        PQ_WBL(bs, 32, atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed));
        PQ_WBL(bs, 32, header_pkt_seq);
        PQ_WBL(bs, 32, len_tot);
        PQ_WBL(bs, 32, off);
        PQ_WBL(bs, 32, QPMIN(len, max));
        PQ_WBL(bs, 32, PQ_RB32(header + h7));
        PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));

        ret = qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), &tmp);
        pq_buffer_quick_unref(&tmp);
        if (ret < 0) {
            return ret;
        }

        h7 = (h7 + 1) % 28;
        len -= len_tgt;
        off += len_tgt;
    } while (len > 0);

    return ret;
}

static int pq_output_generic_data(QprotoContext *qp, QprotoStream *st,
                                  QprotoBuffer *buf,
                                  uint16_t desc_full, uint16_t desc_part,
                                  uint16_t desc_seg, uint16_t desc_end)
{
    if (!buf)
        return 0;

    int ret;
    uint8_t hdr[36];
    PQ_INIT(bs, hdr, sizeof(hdr))
    uint32_t seq;
    size_t data_len = qp_buffer_get_data_len(buf);
    if (data_len > UINT32_MAX)
        return QP_ERROR(EINVAL);

    uint32_t mtu = qp->dst.cb->max_pkt_len(qp, qp->dst.ctx);

    seq = atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed);

    PQ_WBL(bs, 16, data_len > mtu ? desc_part : desc_full);
    PQ_WBL(bs, 16, st->id);
    PQ_WBL(bs, 32, seq);
    PQ_WBL(bs, 32, QPMIN(data_len, mtu));
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));

    ret = qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), buf);
    if (ret < 0)
        return ret;

    if (data_len > mtu)
        ret = pq_output_generic_segment(qp, hdr, seq, st, buf,
                                        QPMIN(data_len, mtu), mtu,
                                        desc_seg, desc_end);

    return ret;
}

static int pq_output_video_info(QprotoContext *qp, QprotoStream *st)
{
    QprotoStreamVideoInfo *vi = &st->video_info;
    uint8_t hdr[356];
    uint8_t raptor2[80];
    PQ_INIT(bs, hdr, sizeof(hdr))

    PQ_WBL(bs, 16, QP_PKT_STREAM_DURATION);
    PQ_WBL(bs, 16, st->id);
    PQ_WBL(bs, 32, atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed));
    PQ_WBL(bs, 32, vi->width);
    PQ_WBL(bs, 32, vi->height);
    PQ_WBL(bs, 32, vi->signal_aspect.num);
    PQ_WBL(bs, 32, vi->signal_aspect.den);
    PQ_WBL(bs, 8,  vi->subsampling);
    PQ_WBL(bs, 8,  vi->colorspace);
    PQ_WBL(bs, 8,  vi->bit_depth);
    PQ_WBL(bs, 8,  vi->interlaced);
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));
    PQ_WBL(bs, 32, vi->gamma.num);
    PQ_WBL(bs, 32, vi->gamma.den);
    PQ_WBL(bs, 32, vi->framerate.num);
    PQ_WBL(bs, 32, vi->framerate.den);
    PQ_WBL(bs, 16, vi->limited_range);
    PQ_WBL(bs, 8,  vi->chroma_pos);
    PQ_WBL(bs, 8,  vi->primaries);
    PQ_WBL(bs, 8,  vi->transfer);
    PQ_WBL(bs, 8,  vi->matrix);
    PQ_WBL(bs, 8,  vi->has_mastering_primaries);
    PQ_WBL(bs, 8,  vi->has_luminance);
    for (int i = 0; i < 16; i++) {
        PQ_WBL(bs, 32, vi->custom_matrix[i].num);
        PQ_WBL(bs, 32, vi->custom_matrix[i].den);
    }
    for (int i = 0; i < 6; i++) {
        PQ_WBL(bs, 32, vi->custom_primaries[i].num);
        PQ_WBL(bs, 32, vi->custom_primaries[i].den);
    }
    PQ_WBL(bs, 32, vi->white_point[0].num);
    PQ_WBL(bs, 32, vi->white_point[0].den);
    PQ_WBL(bs, 32, vi->white_point[1].num);
    PQ_WBL(bs, 32, vi->white_point[1].den);
    PQ_WBL(bs, 32, vi->min_luminance.num);
    PQ_WBL(bs, 32, vi->min_luminance.den);
    PQ_WBL(bs, 32, vi->max_luminance.num);
    PQ_WBL(bs, 32, vi->max_luminance.den);
    PQ_WDT(bs, pq_calc_raptor_short(PQ_GETLAST(bs, 240), raptor2, 240, 80), 80);

    return qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), NULL);
}

static int pq_output_stream_duration(QprotoContext *qp, QprotoStream *st)
{
    if (!st->duration)
        return 0;

    uint8_t hdr[36];
    PQ_INIT(bs, hdr, sizeof(hdr))

    PQ_WBL(bs, 16, QP_PKT_STREAM_DURATION);
    PQ_WBL(bs, 16, st->id);
    PQ_WBL(bs, 32, atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed));
    PQ_WBL(bs, 64, st->duration);
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 32, 0);
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));

    return qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), NULL);
}

int qp_output_update_stream(QprotoContext *qp, QprotoStream *st)
{
    if (!qp->dst.ctx)
        return QP_ERROR(EINVAL);

    int i;
    for (i = 0; i < qp->nb_stream; i++)
        if (qp->stream[i]->id == st->id)
            break;
    if (i == qp->nb_stream)
        return QP_ERROR(EINVAL);

    uint8_t hdr[56];
    PQ_INIT(bs, hdr, sizeof(hdr))

    PQ_WBL(bs, 16, QP_PKT_STREAM_REG);
    PQ_WBL(bs, 16, st->id);
    PQ_WBL(bs, 32, atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed));
    PQ_WBL(bs, 16, st->related_to ? st->related_to->id : st->id);
    PQ_WBL(bs, 16, st->derived_from ? st->derived_from->id : st->id);
    PQ_WBL(bs, 64, st->bitrate);
    PQ_WBL(bs, 64, st->flags);
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));
    PQ_WBL(bs, 32, st->codec_id);
    PQ_WBL(bs, 32, st->timebase.num);
    PQ_WBL(bs, 32, st->timebase.den);
    PQ_WBL(bs, 64, pq_calc_raptor_160(PQ_GETLAST(bs, 12)));

    st->private->codec_id = st->codec_id;

    int ret = qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), NULL);
    if (ret < 0)
        return ret;

    ret = pq_output_stream_duration(qp, st);
    if (ret < 0)
        return ret;

    ret = pq_output_video_info(qp, st);
    if (ret < 0)
        return ret;

    ret = pq_output_generic_data(qp, st, st->init_data,
                                 QP_PKT_STREAM_INIT, QP_PKT_STREAM_INIT_PRT,
                                 QP_PKT_STREAM_INIT_SEG, QP_PKT_STREAM_INIT_END);
    if (ret < 0)
        return ret;

    ret = pq_output_generic_data(qp, st, st->icc_profile,
                                 QP_PKT_ICC, QP_PKT_ICC_PRT,
                                 QP_PKT_ICC_SEG, QP_PKT_ICC_END);
    if (ret < 0)
        return ret;

    return ret;
}

int qp_output_write_stream_data(QprotoContext *qp, QprotoStream *st,
                                QprotoPacket *pkt)
{
    if (!qp->dst.ctx)
        return QP_ERROR(EINVAL);

    int ret;
    uint8_t hdr[36];
    PQ_INIT(bs, hdr, sizeof(hdr))
    uint32_t seq;
    size_t data_len = qp_buffer_get_data_len(pkt->data);
    if (data_len > UINT32_MAX)
        return QP_ERROR(EINVAL);

    uint32_t mtu = qp->dst.cb->max_pkt_len(qp, qp->dst.ctx);

    uint16_t desc;
    desc = QP_PKT_STREAM_DATA & 0xFF00;
    desc |= data_len > mtu ? 0x20 : 0x0;
    desc |= (pkt->type & 0xC0);
    seq = atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed);

    PQ_WBL(bs, 16, desc);
    PQ_WBL(bs, 16, st->id);
    PQ_WBL(bs, 32, seq);
    PQ_WBL(bs, 64, pkt->pts);
    PQ_WBL(bs, 64, pkt->duration);
    PQ_WBL(bs, 32, QPMIN(data_len, mtu));
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));

    ret = qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), pkt->data);
    if (ret < 0)
        return ret;

    if (data_len > mtu)
        ret = pq_output_generic_segment(qp, hdr, seq, st, pkt->data,
                                        QPMIN(data_len, mtu), mtu,
                                        QP_PKT_STREAM_SEG_DATA,
                                        QP_PKT_STREAM_SEG_END);

    return 0;
}

int qp_output_write_user_data(QprotoContext *qp, QprotoBuffer *data,
                              uint8_t descriptor_flags, uint16_t user,
                              int prioritize)
{
    if (!qp->dst.ctx)
        return QP_ERROR(EINVAL);

    uint8_t hdr[36];
    PQ_INIT(bs, hdr, sizeof(hdr))
    uint16_t desc = QP_PKT_USER_DATA & 0xFF00;

    desc |= descriptor_flags;

    PQ_WBL(bs, 16, desc);
    PQ_WBL(bs, 16, user);
    PQ_WBL(bs, 32, atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed));
    PQ_WBL(bs, 32, qp_buffer_get_data_len(data));
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));

    return qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), data);
}

int qp_output_close_stream(QprotoContext *qp, QprotoStream *st)
{
    if (!qp->dst.ctx)
        return QP_ERROR(EINVAL);

    int i;
    for (i = 0; i < qp->nb_stream; i++)
        if (qp->stream[i]->id == st->id)
            break;
    if (i == qp->nb_stream)
        return QP_ERROR(EINVAL);

    uint8_t hdr[36];
    PQ_INIT(bs, hdr, sizeof(hdr))

    PQ_WBL(bs, 16, QP_PKT_EOS);
    PQ_WBL(bs, 16, st->id);
    PQ_WBL(bs, 32, atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed));
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 32, 0);
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));

    free(st->private);
    free(st);

    memmove(&qp->stream[i], &qp->stream[i+1], sizeof(*qp->stream)*(qp->nb_stream - i));
    qp->nb_stream--;

    return qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), NULL);
}

int qp_output_close(QprotoContext *qp)
{
    if (!qp->dst.ctx)
        return QP_ERROR(EINVAL);

    uint8_t hdr[36];
    PQ_INIT(bs, hdr, sizeof(hdr))

    PQ_WBL(bs, 16, QP_PKT_EOS);
    PQ_WBL(bs, 16, 0xFFFF);
    PQ_WBL(bs, 32, atomic_fetch_add_explicit(&qp->dst.seq, 1, memory_order_relaxed));
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 64, 0);
    PQ_WBL(bs, 32, 0);
    PQ_WBL(bs, 64, pq_calc_raptor_224(PQ_GETLAST(bs, 28)));

    int ret  = qp->dst.cb->output(qp, qp->dst.ctx, hdr, PQ_WLEN(bs), NULL);
    int ret2 = qp->dst.cb->close(qp, &qp->dst.ctx);

    return ret < 0 ? ret : ret2;
}

uint32_t pq_unlim_pkt_len(QprotoContext *ctx, PQOutputContext *pc)
{
    return UINT32_MAX;
}
