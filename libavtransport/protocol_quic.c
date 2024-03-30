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
#include <openssl/err.h>

#include "protocol_common.h"
#include "io_common.h"

/* NOTE: when updating the spec version, update the last digit of the ALPN */
static const uint8_t avt_alpn[] = { 4, 'a', 'v', 't', '0' };

typedef struct AVTSSL {
    SSL *ssl;
    BIO *b_in;
    BIO *b_out;
} AVTSSL;

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;

    OSSL_LIB_CTX *ossl;
    SSL_CTX *ssl;

    BIO_ADDR *addr_local;
    BIO_ADDR *addr_remote;
    char hostname[256];

    /* Client */
    AVTSSL client;

    /* Server */
    AVTSSL listeners[64];
    int nb_listeners;
};

static inline int reason_to_err(unsigned long sslerr)
{
    switch (ERR_GET_REASON(sslerr)) {
    case ERR_R_MALLOC_FAILURE: return AVT_ERROR(ENOMEM);
    default: return AVT_ERROR(EINVAL);
    }
}

static void free_ssl(AVTSSL *s)
{
    /* BIOs are not ref'd by SSL, so they get freed with it */
    SSL_free(s->ssl);
}

static int create_ssl(AVTProtocolCtx *p, AVTSSL *s, bool listener)
{
    unsigned long sslerr;

    s->ssl = listener ? SSL_new_listener(p->ssl, 0) :
                        SSL_new(p->ssl);
    if (!s->ssl)
        return AVT_ERROR(ENOMEM);

    const BIO_METHOD *b_dgram = BIO_s_dgram_mem();
    if (!(s->b_out = BIO_new(b_dgram))) {
        SSL_free(s->ssl);
        return AVT_ERROR(ENOMEM);
    }

    if (!(s->b_in  = BIO_new(b_dgram))) {
        BIO_free_all(s->b_out);
        SSL_free(s->ssl);
        return AVT_ERROR(ENOMEM);
    }

    /* We'll handle dst addr on output */
    if (!BIO_dgram_set_caps(s->b_out,
                            BIO_DGRAM_CAP_HANDLES_DST_ADDR)) {
        sslerr = ERR_get_error();
        avt_log(p, AVT_LOG_ERROR, "Unable to set output BIO capability: %s\n",
                ERR_error_string(sslerr, NULL));
        free_ssl(s);
        return reason_to_err(sslerr);
    }

    /* We'll provide src addr in input */
    if (!BIO_dgram_set_caps(s->b_in,
                            BIO_DGRAM_CAP_PROVIDES_SRC_ADDR)) {
        sslerr = ERR_get_error();
        avt_log(p, AVT_LOG_ERROR, "Unable to set input BIO capability: %s\n",
                ERR_error_string(sslerr, NULL));
        free_ssl(s);
        return reason_to_err(sslerr);
    }

    SSL_set_bio(s->ssl, s->b_in, s->b_out);

    if (!listener) {
        /* Set hostname, important for clients */
        if (!SSL_set1_host(s->ssl, p->hostname)) {
            sslerr = ERR_get_error();
            avt_log(p, AVT_LOG_ERROR, "Unable to set connection hostname: %s\n",
                    ERR_error_string(sslerr, NULL));
            free_ssl(s);
            return reason_to_err(sslerr);
        }

        /* Configure ALPN, which is required for QUIC */
        if (SSL_set_alpn_protos(s->ssl, avt_alpn, sizeof(avt_alpn))) {
            sslerr = ERR_get_error();
            avt_log(p, AVT_LOG_ERROR, "Unable to set SSL ALPN: %s\n",
                    ERR_error_string(sslerr, NULL));
            free_ssl(s);
            return reason_to_err(sslerr);
        }
    }

    return 0;
}

static COLD int quic_proto_close(AVTProtocolCtx **_p)
{
    AVTProtocolCtx *p = *_p;

    free_ssl(&p->client);
    for (int i = 0; i < p->nb_listeners; i++)
        free_ssl(&p->listeners[i]);

    SSL_CTX_free(p->ssl);
    OSSL_LIB_CTX_free(p->ossl);

    free(p);
    *_p = NULL;
    return 0;
}

static int alpn_select_cb(SSL *ssl, const unsigned char **out,
                          unsigned char *outlen, const unsigned char *in,
                          unsigned int inlen, void *arg)
{
    int ret = SSL_select_next_proto((uint8_t **)out, outlen,
                                    avt_alpn, sizeof(avt_alpn),
                                    in, inlen);
    if (ret != OPENSSL_NPN_NEGOTIATED)
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    return SSL_TLSEXT_ERR_OK;
}

static COLD int quic_proto_init(AVTContext *ctx, AVTProtocolCtx **_p, AVTAddress *addr,
                                const AVTIO *io, AVTIOCtx *io_ctx)
{
    int ret;
    unsigned long sslerr;
    AVTProtocolCtx *p = calloc(1, sizeof(*p));
    if (!p)
        return AVT_ERROR(ENOMEM);

    p->io = io;
    p->io_ctx = io_ctx;
    memcpy(p->hostname, addr->host, sizeof(p->hostname));

    p->ossl = OSSL_LIB_CTX_new();
    if (!p->ossl) {
        ret = AVT_ERROR(ENOMEM);
        goto fail;
    }

    p->ssl = SSL_CTX_new_ex(p->ossl, NULL,
                            addr->listen ? OSSL_QUIC_server_method() :
                                           OSSL_QUIC_client_method());
    if (!p->ssl) {
        ret = AVT_ERROR(ENOMEM);
        goto fail;
    }

    /* Enable trust chain verification */
    SSL_CTX_set_verify(p->ssl, SSL_VERIFY_PEER, NULL);

    /* Load certificate, if any */
    if (addr->opts.certfile) {
        if (SSL_CTX_use_certificate_file(p->ssl, addr->opts.certfile, SSL_FILETYPE_PEM) <= 0) {
            sslerr = ERR_get_error();
            avt_log(p, AVT_LOG_ERROR, "Unable to load certificate file \"%s\": %s\n",
                    addr->opts.certfile, ERR_error_string(sslerr, NULL));
            ret = reason_to_err(sslerr);
            goto fail;
        }
    } else {
        /* Load default root CA store */
        if (!SSL_CTX_set_default_verify_paths(p->ssl)) {
            sslerr = ERR_get_error();
            avt_log(p, AVT_LOG_ERROR, "Unable to load default root CA store: %s\n",
                    ERR_error_string(sslerr, NULL));
            ret = reason_to_err(sslerr);
            goto fail;
        }
    }

    if (addr->listen) {
        /* Load keyfile, if any */
        if (addr->opts.keyfile) {
            if (SSL_CTX_use_PrivateKey_file(p->ssl, addr->opts.keyfile, SSL_FILETYPE_PEM) <= 0) {
                sslerr = ERR_get_error();
                avt_log(p, AVT_LOG_ERROR, "Unable to load key file \"%s\": %s\n", addr->opts.keyfile,
                        ERR_error_string(sslerr, NULL));
                ret = reason_to_err(sslerr);
                goto fail;
            }

            if (!SSL_CTX_check_private_key(p->ssl)) {
                sslerr = ERR_get_error();
                avt_log(p, AVT_LOG_ERROR, "Private key does not match the public certificate: %s\n",
                        ERR_error_string(sslerr, NULL));
                ret = reason_to_err(sslerr);
                goto fail;
            }
        }

        SSL_CTX_set_alpn_select_cb(p->ssl, alpn_select_cb, NULL);
    }

    ret = create_ssl(p, &p->client, addr->listen);
    if (ret < 0)
        goto fail;

    *_p = p;
    return 0;

fail:
    quic_proto_close(&p);
    return ret;
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

    size_t buf_len;
    uint8_t *data = avt_buffer_get_data(buf, &buf_len);
//    BIO_write(p->bio, data, buf_len);

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
