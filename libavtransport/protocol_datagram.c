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
#include <stdlib.h>
#include <errno.h>

#include "protocol_common.h"
#include "io_common.h"

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;
};

static COLD int datagram_proto_init(AVTContext *ctx, AVTProtocolCtx **p, AVTAddress *addr)
{
    AVTProtocolCtx *priv = malloc(sizeof(*priv));
    if (!priv)
        return AVT_ERROR(ENOMEM);

    int err = avt_io_init(ctx, &priv->io, &priv->io_ctx, addr);
    if (err < 0)
        free(priv);
    else
        *p = priv;

    return err;
}

static int datagram_proto_add_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->add_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->add_dst(p->io_ctx, addr);
}

static int datagram_proto_rm_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->del_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->del_dst(p->io_ctx, addr);
}

static int64_t datagram_proto_send_packet(AVTProtocolCtx *p, AVTPktd *pkt,
                                          int64_t timeout)
{
    return p->io->write_pkt(p->io_ctx, pkt, timeout);
}

static int64_t datagram_proto_send_seq(AVTProtocolCtx *p, AVTPacketFifo *seq,
                                       int64_t timeout)
{
    return p->io->write_vec(p->io_ctx, seq->data, seq->nb, timeout);
}

static int64_t datagram_proto_receive_packet(AVTProtocolCtx *p,
                                             union AVTPacketData *pkt, AVTBuffer **pl,
                                             int64_t timeout)
{
    AVTBuffer *buf;
    int64_t err = p->io->read_input(p->io_ctx, &buf, 0, timeout);
    if (err < 0)
        return err;

    // TODO - deserialize packet here

    return err;
}

static int64_t datagram_proto_max_pkt_len(AVTProtocolCtx *p)
{
    return p->io->get_max_pkt_len(p->io_ctx);
}

static int64_t datagram_proto_seek(AVTProtocolCtx *p,
                                   int64_t off, uint32_t seq,
                                   int64_t ts, bool ts_is_dts)
{
    if (p->io->seek)
        return p->io->seek(p->io_ctx, off);
    return AVT_ERROR(ENOTSUP);
}

static int datagram_proto_flush(AVTProtocolCtx *p, int64_t timeout)
{
    if (p->io->flush)
        return p->io->flush(p->io_ctx, timeout);
    return AVT_ERROR(ENOTSUP);
}

static COLD int datagram_proto_close(AVTProtocolCtx **p)
{
    AVTProtocolCtx *priv = *p;
    int err = priv->io->close(&priv->io_ctx);
    free(priv);
    *p = NULL;
    return err;
}

const AVTProtocol avt_protocol_datagram = {
    .name = "datagram",
    .type = AVT_PROTOCOL_DATAGRAM,
    .init = datagram_proto_init,
    .add_dst = datagram_proto_add_dst,
    .rm_dst = datagram_proto_rm_dst,
    .get_max_pkt_len = datagram_proto_max_pkt_len,
    .send_packet = datagram_proto_send_packet,
    .send_seq = datagram_proto_send_seq,
    .update_packet = NULL,
    .receive_packet = datagram_proto_receive_packet,
    .seek = datagram_proto_seek,
    .flush = datagram_proto_flush,
    .close = datagram_proto_close,
};
