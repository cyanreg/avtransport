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

#include <openssl/ssl.h>

#include "protocol_common.h"
#include "io_common.h"

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;

    SSL_CTX *ssl_ctx;

    BIO *bio;
    SSL *ssl;
};

static COLD int quic_proto_close(AVTProtocolCtx **_p)
{
    AVTProtocolCtx *p = *_p;
    int err = p->io->close(&p->io_ctx);

    BIO_free_all(p->bio);
    SSL_CTX_free(p->ssl_ctx);

    free(p);
    *_p = NULL;
    return err;
}

static COLD int quic_proto_init(AVTContext *ctx, AVTProtocolCtx **_p, AVTAddress *addr)
{
    AVTProtocolCtx *p = malloc(sizeof(*p));
    if (!p)
        return AVT_ERROR(ENOMEM);

    int err = avt_io_init(ctx, &p->io, &p->io_ctx, addr);
    if (err < 0)
        free(p);

    p->ssl_ctx = SSL_CTX_new(OSSL_QUIC_client_method());
    if (!p->ssl_ctx) {
        quic_proto_close(&p);
        return AVT_ERROR(ENOMEM);
    }

    /* Enable trust chain verification */
    SSL_CTX_set_verify(p->ssl_ctx, SSL_VERIFY_PEER, NULL);

    /* Load default root CA store */
    if (!SSL_CTX_set_default_verify_paths(p->ssl_ctx)) {
        avt_log(p, AVT_LOG_ERROR, "Unable to load default root CA store\n");
        quic_proto_close(&p);
    }

    p->bio = BIO_new_ssl_connect(p->ssl_ctx);
    if (!p->bio) {
        quic_proto_close(&p);
        return AVT_ERROR(EINVAL);
    }

    if (!BIO_get_ssl(p->bio, &p->ssl)) {
        avt_log(p, AVT_LOG_ERROR, "Unable to get SSL context\n");
        quic_proto_close(&p);
        return AVT_ERROR(EINVAL);
    }

    if (strlen(addr->host)) {
        /* Tell the SSL object the hostname to check certificates against. */
        if (SSL_set1_host(p->ssl, addr->host) <= 0) {
            avt_log(p, AVT_LOG_ERROR, "Unable to set SSL hostname\n");
            quic_proto_close(&p);
            return AVT_ERROR(EINVAL);
        }
    }

    /* Configure ALPN, which is required for QUIC */
    static const uint8_t alpn[] = { 4, 'a', 'v', 't', '0' };
    if (SSL_set_alpn_protos(p->ssl, alpn, sizeof(alpn))) {
        /* Note: SSL_set_alpn_protos returns 1 for failure */
        avt_log(p, AVT_LOG_ERROR, "Unable to set ALPN\n");
        quic_proto_close(&p);
        return AVT_ERROR(EINVAL);
    }

    *_p = p;
    return err;
}

static int quic_proto_add_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->add_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->add_dst(p->io_ctx, addr);
}

static int quic_proto_rm_dst(AVTProtocolCtx *p, AVTAddress *addr)
{
    if (!p->io->del_dst)
        return AVT_ERROR(ENOTSUP);
    return p->io->del_dst(p->io_ctx, addr);
}

static int64_t quic_proto_send_packet(AVTProtocolCtx *p, AVTPktd *pkt,
                                      int64_t timeout)
{
    return p->io->write_pkt(p->io_ctx, pkt, timeout);
}

static int64_t quic_proto_send_seq(AVTProtocolCtx *p, AVTPacketFifo *seq,
                                   int64_t timeout)
{
    return p->io->write_vec(p->io_ctx, seq->data, seq->nb, timeout);
}

static int64_t quic_proto_receive_packet(AVTProtocolCtx *p,
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

static int64_t quic_proto_max_pkt_len(AVTProtocolCtx *p)
{
    return p->io->get_max_pkt_len(p->io_ctx);
}

static int64_t quic_proto_seek(AVTProtocolCtx *p,
                               int64_t off, uint32_t seq,
                               int64_t ts, bool ts_is_dts)
{
    if (p->io->seek)
        return p->io->seek(p->io_ctx, off);
    return AVT_ERROR(ENOTSUP);
}

static int quic_proto_flush(AVTProtocolCtx *p, int64_t timeout)
{
    if (p->io->flush)
        return p->io->flush(p->io_ctx, timeout);
    return AVT_ERROR(ENOTSUP);
}

const AVTProtocol avt_protocol_quic = {
    .name = "quic",
    .type = AVT_PROTOCOL_QUIC,
    .init = quic_proto_init,
    .add_dst = quic_proto_add_dst,
    .rm_dst = quic_proto_rm_dst,
    .get_max_pkt_len = quic_proto_max_pkt_len,
    .send_packet = quic_proto_send_packet,
    .send_seq = quic_proto_send_seq,
    .update_packet = NULL,
    .receive_packet = quic_proto_receive_packet,
    .seek = quic_proto_seek,
    .flush = quic_proto_flush,
    .close = quic_proto_close,
};
