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

#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <stdlib.h>

#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/x509v3.h>

#include <avtransport/avtransport.h>

#include "address.h"
#include "protocol_common.h"
#include "io_common.h"
#include "buffer.h"
#include "packet_encode.h"

extern const AVTIO avt_io_udp;
extern const AVTProtocol avt_protocol_quic;
extern int avt_quic_handshake(AVTProtocolCtx *p, int64_t timeout);

/* Send a packet from the client and receive it on the server, verifying it
 * round-trips intact. Returns 0 on success. */
static int test_roundtrip(AVTContext *avt, const AVTProtocol *p,
                          AVTProtocolCtx *client, AVTProtocolCtx *server)
{
    static const char payload[] = "AVTransport QUIC step 2 payload";
    const size_t pl_len = sizeof(payload); /* includes the trailing NUL */
    int ret;

    /* Build a stream data packet with a small payload. */
    AVTPktd sent = { };
    AVTBytestream bs = avt_bs_init(sent.hdr, sizeof(sent.hdr));
    avt_encode_stream_data(&bs, (AVTStreamData) {
        .stream_data_descriptor = AVT_PKT_STREAM_DATA,
        .stream_id   = 0x21,
        .global_seq  = 0x40,
        .frame_type  = AVT_FRAME_TYPE_KEY,
        .pts         = 0x1234,
        .duration    = 0x10,
        .data_length = pl_len,
    });
    sent.hdr_len = bs.ptr - sent.hdr;

    uint8_t *plbuf = avt_buffer_quick_alloc(&sent.pl, pl_len);
    if (!plbuf)
        return AVT_ERROR(ENOMEM);
    memcpy(plbuf, payload, pl_len);

    ret = p->send_packet(client, &sent, 0);
    avt_buffer_quick_unref(&sent.pl);
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "QUIC send failed: %d\n", ret);
        return ret;
    }

    /* Pull it back out on the server. */
    AVTPacketFifo fifo = { };
    ret = AVT_ERROR(EAGAIN);
    for (int i = 0; i < 2000 && ret == AVT_ERROR(EAGAIN); i++) {
        ret = p->receive(server, &fifo, 0);
        if (ret == AVT_ERROR(EAGAIN))
            nanosleep(&(struct timespec){ .tv_nsec = 1000 * 1000 }, NULL);
    }
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "QUIC receive failed: %d\n", ret);
        avt_pkt_fifo_free(&fifo);
        return ret;
    }

    /* Verify it matches what was sent. */
    ret = AVT_ERROR(EINVAL);
    if (fifo.nb != 1) {
        avt_log(avt, AVT_LOG_ERROR, "Expected 1 packet, got %u\n", fifo.nb);
    } else {
        AVTPktd *r = &fifo.data[0];
        size_t rpl_len;
        uint8_t *rpl = avt_buffer_get_data(&r->pl, &rpl_len);
        if (r->pkt.stream_data.stream_id   != 0x21   ||
            r->pkt.stream_data.pts         != 0x1234 ||
            r->pkt.stream_data.data_length != pl_len ||
            !rpl || rpl_len != pl_len || memcmp(rpl, payload, pl_len)) {
            avt_log(avt, AVT_LOG_ERROR, "Received packet does not match\n");
        } else {
            avt_log(avt, AVT_LOG_INFO, "QUIC packet round-trip OK\n");
            ret = 0;
        }
    }

    avt_pkt_fifo_free(&fifo);
    return ret;
}

/* Send a payload larger than the QUIC flow-control window with a
 * non-blocking (zero timeout) send: the part that cannot be written
 * immediately must be buffered rather than fail or be resent, and the whole
 * payload must arrive intact. */
static int test_buffered_send(AVTContext *avt, const AVTProtocol *p,
                              AVTProtocolCtx *client, AVTProtocolCtx *server)
{
    const size_t pl_len = 2 * 1024 * 1024;
    int ret;

    uint8_t *payload = malloc(pl_len);
    if (!payload)
        return AVT_ERROR(ENOMEM);
    for (size_t i = 0; i < pl_len; i++)
        payload[i] = (uint8_t)(i * 131 + 7);

    AVTPktd sent = { };
    AVTBytestream bs = avt_bs_init(sent.hdr, sizeof(sent.hdr));
    avt_encode_stream_data(&bs, (AVTStreamData) {
        .stream_data_descriptor = AVT_PKT_STREAM_DATA,
        .stream_id   = 0x22,
        .global_seq  = 0x41,
        .frame_type  = AVT_FRAME_TYPE_KEY,
        .pts         = 0x5678,
        .data_length = pl_len,
    });
    sent.hdr_len = bs.ptr - sent.hdr;

    uint8_t *plbuf = avt_buffer_quick_alloc(&sent.pl, pl_len);
    if (!plbuf) {
        free(payload);
        return AVT_ERROR(ENOMEM);
    }
    memcpy(plbuf, payload, pl_len);

    /* Non-blocking send: most of the payload cannot be written immediately,
     * so it must be buffered; the call still has to succeed. */
    ret = p->send_packet(client, &sent, 0);
    avt_buffer_quick_unref(&sent.pl);
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "Buffered send failed: %d\n", ret);
        free(payload);
        return ret;
    }

    /* Drive both ends: the client drains its buffer non-blocking, the server
     * reads and acknowledges, which reopens the client's flow-control
     * window. */
    AVTPacketFifo fifo = { };
    ret = AVT_ERROR(EAGAIN);
    for (int i = 0; i < 20000 && ret == AVT_ERROR(EAGAIN); i++) {
        p->flush(client, 0);
        ret = p->receive(server, &fifo, 0);
        if (ret == AVT_ERROR(EAGAIN))
            nanosleep(&(struct timespec){ .tv_nsec = 100 * 1000 }, NULL);
    }
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "Buffered receive failed: %d\n", ret);
        free(payload);
        avt_pkt_fifo_free(&fifo);
        return ret;
    }

    ret = AVT_ERROR(EINVAL);
    if (fifo.nb != 1) {
        avt_log(avt, AVT_LOG_ERROR, "Expected 1 packet, got %u\n", fifo.nb);
    } else {
        AVTPktd *r = &fifo.data[0];
        size_t rpl_len;
        uint8_t *rpl = avt_buffer_get_data(&r->pl, &rpl_len);
        if (r->pkt.stream_data.data_length != pl_len ||
            !rpl || rpl_len != pl_len || memcmp(rpl, payload, pl_len)) {
            avt_log(avt, AVT_LOG_ERROR, "Buffered payload does not match\n");
        } else {
            avt_log(avt, AVT_LOG_INFO,
                    "QUIC buffered send round-trip OK (%zu bytes)\n", pl_len);
            ret = 0;
        }
    }

    free(payload);
    avt_pkt_fifo_free(&fifo);
    return ret;
}

/* Loopback address and port shared by the test's client and server. */
#define TEST_URL_BASE "quic://localhost:48212/"

static void add_x509_ext(X509 *cert, X509V3_CTX *ctx, int ext_nid, const char *value)
{
    X509_EXTENSION *ex = X509V3_EXT_conf_nid(NULL, ctx, ext_nid, value);
    X509_add_ext(cert, ex, -1);
    X509_EXTENSION_free(ex);
}

static int create_cert(AVTContext *avt, const char *certfile, const char *keyfile)
{
    EVP_PKEY *p_key = NULL;
    EVP_PKEY_CTX *p_key_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, NULL);
    EVP_PKEY_keygen_init(p_key_ctx);

    if (EVP_PKEY_keygen(p_key_ctx, &p_key) <= 0) {
        avt_log(avt, AVT_LOG_ERROR, "Error generating key!\n");
        return AVT_ERROR(EINVAL);
    }

    /* Write the private key file */
    FILE *f_key = fopen(keyfile, "wb");
    int ret = PEM_write_PrivateKey(f_key, p_key, NULL, NULL, 0, NULL, NULL);
    fclose(f_key);
    if (ret <= 0) {
        avt_log(avt, AVT_LOG_ERROR, "Error writing private key!\n");
        return AVT_ERROR(EINVAL);
    }

    /* Create the certificate object */
    X509 *cert = X509_new();
    X509_set_version(cert, 3);

    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 600L); /* 10 minutes */
    X509_set_pubkey(cert, p_key);

    X509_NAME *cert_name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(cert_name, "CN", MBSTRING_ASC, (unsigned char *)"localhost", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name,  "C", MBSTRING_ASC, (unsigned char *)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name,  "O", MBSTRING_ASC, (unsigned char *)"HANMA", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name, "OU", MBSTRING_ASC, (unsigned char *)"KANMA", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name, "ST", MBSTRING_ASC, (unsigned char *)"GOOOS", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name, "SN", MBSTRING_ASC, (unsigned char *)"DESANE", -1, -1, 0);
    X509_NAME_add_entry_by_txt(cert_name, "GN", MBSTRING_ASC, (unsigned char *)"HAAH", -1, -1, 0);
    X509_set_issuer_name(cert, cert_name);

    X509V3_CTX v3ctx;
    X509V3_set_ctx_nodb(&v3ctx);
    X509V3_set_ctx(&v3ctx, cert, cert, NULL, NULL, 0);

    add_x509_ext(cert, &v3ctx, NID_authority_key_identifier, "keyid");
    add_x509_ext(cert, &v3ctx, NID_basic_constraints, "critical,CA:TRUE");
    /* Modern hostname verification matches the SAN, not the CN. */
    add_x509_ext(cert, &v3ctx, NID_subject_alt_name, "DNS:localhost");

    if (X509_sign(cert, p_key, NULL /* No digest for Ed25519 keys */) <= 0) {
        avt_log(avt, AVT_LOG_ERROR, "Error signing X509 certificate with key!\n");
        return AVT_ERROR(EINVAL);
    }

    /* Write the certificate file */
    FILE *f_cert = fopen(certfile, "wb");
    PEM_write_X509(f_cert, cert);
    fclose(f_cert);

    EVP_PKEY_free(p_key);
    EVP_PKEY_CTX_free(p_key_ctx);
    X509_free(cert);

    return 0;
}

int main(void)
{
    int ret;
    AVTContext *avt;
    AVTAddress addr_server = { 0 };
    AVTAddress addr_client = { 0 };

    const AVTIO *io = &avt_io_udp;
    AVTIOCtx *io_server_ctx = NULL;
    AVTIOCtx *io_client_ctx = NULL;

    const AVTProtocol *p = &avt_protocol_quic;
    AVTProtocolCtx *p_server_ctx = NULL;
    AVTProtocolCtx *p_client_ctx = NULL;

    ret = avt_init(&avt, NULL);
    if (ret < 0)
        return 1;

    ret = create_cert(avt, "test_cert.pem", "test_key.pem");
    if (ret < 0)
        goto end;

    { /* Server */
        ret = avt_addr_from_url(avt, &addr_server, 1,
                                TEST_URL_BASE "#certfile=test_cert.pem&keyfile=test_key.pem");
        if (ret < 0)
            goto end;
        ret = io->init(avt, &io_server_ctx, &addr_server);
        if (ret < 0)
            goto end;
        ret = p->init(avt, &p_server_ctx, &addr_server, io, io_server_ctx, NULL);
        if (ret < 0)
            goto end;
    }

    { /* Client */
        ret = avt_addr_from_url(avt, &addr_client, 0,
                                TEST_URL_BASE "#certfile=test_cert.pem");
        if (ret < 0)
            goto end;
        ret = io->init(avt, &io_client_ctx, &addr_client);
        if (ret < 0)
            goto end;
        ret = p->init(avt, &p_client_ctx, &addr_client, io, io_client_ctx, NULL);
        if (ret < 0)
            goto end;
    }

    /* Drive both ends until the handshake completes. The sockets are real
     * loopback UDP, so the datagrams flow through the kernel between them. */
    bool server_done = false;
    bool client_done = false;
    for (int i = 0; i < 2000 && !(server_done && client_done); i++) {
        if (!server_done) {
            ret = avt_quic_handshake(p_server_ctx, 0);
            if (ret == 0)
                server_done = true;
            else if (ret != AVT_ERROR(EAGAIN))
                goto end;
        }
        if (!client_done) {
            ret = avt_quic_handshake(p_client_ctx, 0);
            if (ret == 0)
                client_done = true;
            else if (ret != AVT_ERROR(EAGAIN))
                goto end;
        }

        nanosleep(&(struct timespec){ .tv_nsec = 1000 * 1000 }, NULL);
    }

    if (!(server_done && client_done)) {
        avt_log(avt, AVT_LOG_ERROR, "QUIC handshake did not complete in time\n");
        ret = AVT_ERROR(ETIMEDOUT);
        goto end;
    }

    avt_log(avt, AVT_LOG_INFO, "QUIC handshake succeeded\n");

    ret = test_roundtrip(avt, p, p_client_ctx, p_server_ctx);
    if (ret < 0)
        goto end;

    ret = test_buffered_send(avt, p, p_client_ctx, p_server_ctx);
    if (ret < 0)
        goto end;

    ret = 0;

end:
    if (p_client_ctx)
        p->close(&p_client_ctx);
    if (io_client_ctx)
        io->close(&io_client_ctx);

    if (p_server_ctx)
        p->close(&p_server_ctx);
    if (io_server_ctx)
        io->close(&io_server_ctx);

    avt_addr_free(&addr_client);
    avt_addr_free(&addr_server);
    avt_close(&avt);

    return ret < 0 ? 1 : 0;
}
