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
#include "libqproto/utils.h"
#include "utils.h"
#include "input.h"

extern const PQInput pq_input_file;

static const PQInput *pq_input_list[] = {
    &pq_input_file,

    NULL,
};

int qp_input_open(QprotoContext *qp, QprotoInputSource *src,
                  QprotoInputCallbacks *cb, void *cb_opaque,
                  QprotoInputOptions *opts)
{
    const PQInput *in;
    for (in = pq_input_list[0]; in; in++) {
        if (in->type == src->type)
            break;
    }

    if (!in)
        return QP_ERROR(ENOTSUP);

    qp->src.cb = in;
    qp->src.src = *src;
    qp->src.proc = *cb;
    qp->src.cb_opaque = cb_opaque;

    return in->init(qp, &qp->src.ctx, src, opts);
}

int qp_input_process(QprotoContext *qp, int64_t timeout)
{
    if (!qp->src.ctx)
        return QP_ERROR(EINVAL);

    return qp->src.cb->process(qp, qp->src.ctx);
}

static int pq_input_stream_data(QprotoContext *qp, PQByteStream *bs,
                                QprotoBuffer *buf)
{
    uint16_t desc = pq_bs_read_b16(bs);
    uint16_t st = pq_bs_read_b16(bs);



    return 0;
}

static int pq_input_user_data(QprotoContext *qp, PQByteStream *bs,
                              QprotoBuffer *buf)
{
    uint16_t desc = pq_bs_read_b16(bs);
    uint16_t user_field = pq_bs_read_b16(bs);
    uint32_t seq = pq_bs_read_b32(bs);
    uint32_t len = pq_bs_read_b32(bs);

    if (len != qp_buffer_get_data_len(buf) - 36)
        pq_log(qp, QP_LOG_ERROR, "Error: mismatching user packet length field!\n");

    int ret = qp->src.proc.user_pkt_cb(qp->src.cb_opaque, buf, desc,
                                       user_field, seq);
    qp_buffer_unref(&buf);

    return ret;
}

static int pq_input_demux(QprotoContext *qp, QprotoBuffer *buf)
{
    size_t len;
    uint8_t *data = qp_buffer_get_data(buf, &len);

    /* Packets are never smaller than 36 bytes */
    if (len < 36)
        return QP_ERROR(EINVAL);

    PQ_INIT(bs, data, len)

    uint16_t desc = pq_bs_read_b16(&bs);

    PQ_RESET(bs);

    /* Filter out the non-flag, non-arbitrary bits. */
    uint8_t desc_h = desc >> 8;
    if (!(desc_h & (QP_PKT_STREAM_DATA & 0xFF00)))
        pq_input_stream_data(qp, &bs, buf);
    if (!(desc_h & (QP_PKT_USER_DATA & 0xFF00)))
        pq_input_user_data(qp, &bs, buf);



    return 0;
}

int qp_input_close(QprotoContext *qp)
{
    if (!qp->src.ctx)
        return QP_ERROR(EINVAL);

    return qp->src.cb->close(qp, &qp->src.ctx);
}
