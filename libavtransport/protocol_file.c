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
#include "bytestream.h"

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;
};

static int file_init(AVTContext *ctx, AVTProtocolCtx **p, AVTAddress *addr)
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

static int file_add_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->add_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->add_dst(p->io_ctx, addr);
}

static int file_rm_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->del_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->del_dst(p->io_ctx, addr);
}

static int64_t file_proto_send_packet(AVTProtocolCtx *p, AVTPktd *pkt,
                                      int64_t timeout)
{
    return p->io->write_pkt(p->io_ctx, pkt, timeout);
}

static int64_t file_send_seq(AVTProtocolCtx *p,
                             AVTPacketFifo *seq, int64_t timeout)
{
    return p->io->write_vec(p->io_ctx, seq->data, seq->nb, timeout);
}

#include "packet_decode.h"

static int64_t file_receive_packet(AVTProtocolCtx *p,
                                   union AVTPacketData *pkt, AVTBuffer **pl,
                                   int64_t timeout)
{
    /* Request the maximum length. If any data gets caught, it's fine. */
    size_t req_len = AVT_MAX_HEADER_LEN;

    /* Get header */
    AVTBuffer *buf;
    int64_t err = p->io->read_input(p->io_ctx, &buf, req_len, timeout);
    if (err < 0)
        return err;

    size_t len;
    uint8_t *data = avt_buffer_get_data(buf, &len);
    if (!data || len != req_len) { // EOF handling too probably
    }

    // EC happens here

    int64_t off;
    uint16_t desc = AVT_RB24(&data[0]);
    switch (desc) {
    case AVT_PKT_SESSION_START:
        off = avt_decode_session_start(buf, &pkt->session_start);
        break;
    case AVT_PKT_STREAM_REGISTRATION:
        off = avt_decode_stream_registration(buf, &pkt->stream_registration);
        break;
    case AVT_PKT_STREAM_DATA & 0xFF00:
        AVTBuffer tmp;
        off = avt_decode_stream_data(buf, &pkt->stream_data, &tmp);
        break;
    };

    return err;
}

static int64_t file_proto_max_pkt_len(AVTProtocolCtx *p)
{
    return p->io->get_max_pkt_len(p->io_ctx);
}

static int64_t file_proto_seek(AVTProtocolCtx *p,
                               int64_t off, uint32_t seq,
                               int64_t ts, bool ts_is_dts)
{
    if (p->io->seek)
        return p->io->seek(p->io_ctx, off);
    return AVT_ERROR(ENOTSUP);
}

static int file_proto_flush(AVTProtocolCtx *p, int64_t timeout)
{
    if (p->io->flush)
        return p->io->flush(p->io_ctx, timeout);
    return AVT_ERROR(ENOTSUP);
}

static int file_proto_close(AVTProtocolCtx **p)
{
    AVTProtocolCtx *priv = *p;
    int err = priv->io->close(&priv->io_ctx);
    free(priv);
    *p = NULL;
    return err;
}

const AVTProtocol avt_protocol_file = {
    .name = "file",
    .type = AVT_PROTOCOL_FILE,
    .init = file_init,
    .add_dst = file_add_dst,
    .rm_dst = file_rm_dst,
    .get_max_pkt_len = file_proto_max_pkt_len,
    .send_packet = file_proto_send_packet,
    .send_seq = file_send_seq,
    .update_packet = NULL,
    .receive_packet = file_receive_packet,
    .seek = file_proto_seek,
    .flush = file_proto_flush,
    .close = file_proto_close,
};
