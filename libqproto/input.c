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

#include "input.h"

extern const PQInput pq_input_file;

static const PQInput *pq_input_list[] = {
    &pq_input_file,

    NULL,
};

int qp_input_open(QprotoContext *qp, QprotoInputSource *src,
                  QprotoInputCallbacks *cb,
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

    return in->init(qp, &qp->src.ctx, src, opts);
}

int qp_input_process(QprotoContext *qp, int64_t timeout)
{
    if (!qp->src.ctx)
        return QP_ERROR(EINVAL);

    return qp->src.cb->process(qp, qp->src.ctx);
}

static int pq_input_demux(QprotoContext *qp, QprotoBuffer *buf)
{
    size_t len;
    uint8_t *data = qp_buffer_get_data(buf, &len);



    return 0;
}

int qp_input_close(QprotoContext *qp)
{
    if (!qp->src.ctx)
        return QP_ERROR(EINVAL);

    return qp->src.cb->close(qp, &qp->src.ctx);
}
