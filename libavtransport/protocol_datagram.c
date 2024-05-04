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

#include <avtransport/avtransport.h>
#include "protocol_common.h"
#include "io_common.h"

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;
    AVTProtocolOpts opts;
};

static COLD int datagram_proto_close(AVTProtocolCtx **p)
{
    AVTProtocolCtx *priv = *p;
    free(priv);
    *p = NULL;
    return 0;
}

static COLD int datagram_proto_init(AVTContext *ctx, AVTProtocolCtx **_p, AVTAddress *addr,
                                    const AVTIO *io, AVTIOCtx *io_ctx, AVTProtocolOpts *opts)
{
    AVTProtocolCtx *p = malloc(sizeof(*p));
    if (!p)
        return AVT_ERROR(ENOMEM);

    p->io = io;
    p->io_ctx = io_ctx;
    p->opts = *opts;

    *_p = p;

    return 0;
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

static int datagram_proto_send_packet(AVTProtocolCtx *p, AVTPktd *pkt,
                                      int64_t timeout)
{
    int64_t ret = p->io->write_pkt(p->io_ctx, pkt, timeout);
    if (ret < 0)
        return ret;
    return 0;
}

static int datagram_proto_send_seq(AVTProtocolCtx *p, AVTPacketFifo *seq,
                                   int64_t timeout)
{
    int64_t ret = p->io->write_vec(p->io_ctx, seq->data, seq->nb, timeout);
    if (ret < 0)
        return ret;
    return 0;
}

static int datagram_proto_receive_packet(AVTProtocolCtx *s, AVTPktd *p,
                                         int64_t timeout)
{

    return 0;
}

static int datagram_proto_max_pkt_len(AVTProtocolCtx *p, size_t *mtu)
{
    size_t tmp;
    const size_t udp_hdr_size = 8;

    int ret = p->io->get_max_pkt_len(p->io_ctx, &tmp);
    if (ret < 0 || (tmp < (AVT_MIN_HEADER_LEN - udp_hdr_size)))
        return ret;

    *mtu = tmp - udp_hdr_size;

    return 0;
}

static int datagram_proto_flush(AVTProtocolCtx *p, int64_t timeout)
{
    if (p->io->flush)
        return p->io->flush(p->io_ctx, timeout);
    return AVT_ERROR(ENOTSUP);
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
    .seek = NULL,
    .flush = datagram_proto_flush,
    .close = datagram_proto_close,
};
