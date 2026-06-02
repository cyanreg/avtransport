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

#include <stdio.h>
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

/* Drive both ends until the server's receive fifo contains nb_expected
 * packets, or we time out. Each iteration drives flush on the client and
 * receive on the server, so flow-control credit flows back and forth. */
static int pump_until(const AVTProtocol *p, AVTProtocolCtx *client,
                      AVTProtocolCtx *server, AVTPacketFifo *fifo,
                      uint32_t nb_expected, int max_iter)
{
    int ret = 0;
    for (int i = 0; i < max_iter && fifo->nb < nb_expected; i++) {
        p->flush(client, 0);
        int rr = p->receive(server, fifo, 0);
        if (rr < 0 && rr != AVT_ERROR(EAGAIN))
            return rr;
        if (fifo->nb >= nb_expected)
            break;
        nanosleep(&(struct timespec){ .tv_nsec = 100 * 1000 }, NULL);
    }
    if (fifo->nb < nb_expected)
        return AVT_ERROR(EAGAIN);
    return ret;
}

/* Send a large FONT_DATA payload over a reliable QUIC stream. Per the spec,
 * non-data descriptors travel on reliable streams; the test exercises the
 * stream's flow-control + buffered-send path (a non-blocking call with a
 * payload much larger than the initial flow-control window must buffer the
 * remainder and let later calls drain it). */
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
    avt_encode_font_data(&bs, (AVTFontData) {
        .font_data_descriptor = AVT_PKT_FONT_DATA,
        .stream_id            = 0x30,
        .global_seq           = 0x41,
        .font_type            = AVT_FONT_TYPE_TTF,
        .font_compression     = AVT_DATA_COMPRESSION_NONE,
        .font_data_length     = pl_len,
        .font_pl_length       = pl_len,
        .font_name            = "test.ttf",
    });
    sent.hdr_len = bs.ptr - sent.hdr;

    uint8_t *plbuf = avt_buffer_quick_alloc(&sent.pl, pl_len);
    if (!plbuf) {
        free(payload);
        return AVT_ERROR(ENOMEM);
    }
    memcpy(plbuf, payload, pl_len);

    /* Non-blocking send: the QUIC stream's initial flow-control window is
     * smaller than 2 MB, so most of the payload must be buffered; the call
     * still has to succeed. */
    ret = p->send_packet(client, &sent, 0);
    avt_buffer_quick_unref(&sent.pl);
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "Buffered send failed: %d\n", ret);
        free(payload);
        return ret;
    }

    AVTPacketFifo fifo = { };
    ret = pump_until(p, client, server, &fifo, 1, 20000);
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
        if (r->pkt.font_data.font_pl_length != pl_len ||
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

/* The spec requires each AVT stream to map to a *different* QUIC stream.
 * Send a reliable per-stream packet on two distinct AVT stream_ids and check
 * both arrive intact on the server. The protocol layer must not collapse
 * them onto the same QUIC stream (which would byte-stream-interleave their
 * headers and corrupt reassembly). */
static int test_multi_stream(AVTContext *avt, const AVTProtocol *p,
                             AVTProtocolCtx *client, AVTProtocolCtx *server)
{
    static const struct {
        uint16_t sid;
        uint64_t bw;
    } items[] = {
        { 0x50, 0xAAAA0001 },
        { 0x51, 0xBBBB0002 },
        { 0x52, 0xCCCC0003 },
    };
    const int N = sizeof(items) / sizeof(items[0]);
    int ret;

    for (int i = 0; i < N; i++) {
        AVTPktd sent = { };
        AVTBytestream bs = avt_bs_init(sent.hdr, sizeof(sent.hdr));
        avt_encode_stream_registration(&bs, (AVTStreamRegistration) {
            .stream_registration_descriptor = AVT_PKT_STREAM_REGISTRATION,
            .stream_id   = items[i].sid,
            .global_seq  = 0x100 + i,
            .bandwidth   = items[i].bw,
            .codec_id    = AVT_CODEC_ID_RAW_VIDEO,
        });
        sent.hdr_len = bs.ptr - sent.hdr;
        ret = p->send_packet(client, &sent, 0);
        if (ret < 0) {
            avt_log(avt, AVT_LOG_ERROR, "multi-stream send %d failed: %d\n", i, ret);
            return ret;
        }
    }

    AVTPacketFifo fifo = { };
    ret = pump_until(p, client, server, &fifo, N, 20000);
    if (ret < 0) {
        avt_log(avt, AVT_LOG_ERROR, "multi-stream receive failed: %d\n", ret);
        avt_pkt_fifo_free(&fifo);
        return ret;
    }

    if (fifo.nb != (uint32_t)N) {
        avt_log(avt, AVT_LOG_ERROR, "multi-stream: expected %d, got %u\n",
                N, fifo.nb);
        avt_pkt_fifo_free(&fifo);
        return AVT_ERROR(EINVAL);
    }

    /* Each AVT stream_id should appear exactly once with its bandwidth. */
    bool seen[3] = { 0 };
    for (uint32_t i = 0; i < fifo.nb; i++) {
        for (int k = 0; k < N; k++) {
            if (fifo.data[i].pkt.stream_registration.stream_id == items[k].sid &&
                fifo.data[i].pkt.stream_registration.bandwidth == items[k].bw) {
                seen[k] = true;
                break;
            }
        }
    }
    for (int k = 0; k < N; k++) {
        if (!seen[k]) {
            avt_log(avt, AVT_LOG_ERROR,
                    "multi-stream: registration for sid 0x%x missing\n",
                    items[k].sid);
            avt_pkt_fifo_free(&fifo);
            return AVT_ERROR(EINVAL);
        }
    }
    avt_log(avt, AVT_LOG_INFO,
            "QUIC multi-stream isolation OK (%d streams)\n", N);
    avt_pkt_fifo_free(&fifo);
    return 0;
}

/* Send several small STREAM_DATA packets and verify they all arrive. These
 * descriptors (0x01**) are routed onto RFC 9221 unreliable datagrams; small
 * payloads sent on a quiet loopback should not be lost. */
static int test_datagrams(AVTContext *avt, const AVTProtocol *p,
                          AVTProtocolCtx *client, AVTProtocolCtx *server)
{
    const int N = 16;
    int ret;

    for (int i = 0; i < N; i++) {
        AVTPktd sent = { };
        AVTBytestream bs = avt_bs_init(sent.hdr, sizeof(sent.hdr));
        avt_encode_stream_data(&bs, (AVTStreamData) {
            .stream_data_descriptor = AVT_PKT_STREAM_DATA,
            .stream_id   = 0x60,
            .global_seq  = 0x200 + i,
            .frame_type  = AVT_FRAME_TYPE_KEY,
            .pts         = i,
            .duration    = 1,
            .data_length = 16,
        });
        sent.hdr_len = bs.ptr - sent.hdr;
        uint8_t *plbuf = avt_buffer_quick_alloc(&sent.pl, 16);
        if (!plbuf)
            return AVT_ERROR(ENOMEM);
        for (int j = 0; j < 16; j++)
            plbuf[j] = (uint8_t)(i * 16 + j);

        ret = p->send_packet(client, &sent, 0);
        avt_buffer_quick_unref(&sent.pl);
        if (ret < 0) {
            avt_log(avt, AVT_LOG_ERROR, "datagram send %d failed: %d\n", i, ret);
            return ret;
        }
    }

    AVTPacketFifo fifo = { };
    /* Datagrams are unreliable - accept best-effort delivery here, then
     * verify whatever did arrive is intact. The quiet loopback case
     * normally delivers all of them. */
    ret = pump_until(p, client, server, &fifo, N, 5000);
    if (ret < 0 && fifo.nb == 0) {
        avt_log(avt, AVT_LOG_ERROR, "datagram: no packets arrived\n");
        avt_pkt_fifo_free(&fifo);
        return AVT_ERROR(EINVAL);
    }

    /* Verify every arrived packet has the correct content for its pts. */
    int bad = 0;
    for (uint32_t i = 0; i < fifo.nb; i++) {
        AVTPktd *r = &fifo.data[i];
        int idx = (int)r->pkt.stream_data.pts;
        size_t rpl_len;
        uint8_t *rpl = avt_buffer_get_data(&r->pl, &rpl_len);
        if (idx < 0 || idx >= N || rpl_len != 16) { bad++; continue; }
        for (int j = 0; j < 16; j++) {
            if (rpl[j] != (uint8_t)(idx * 16 + j)) { bad++; break; }
        }
    }
    if (bad) {
        avt_log(avt, AVT_LOG_ERROR,
                "datagram: %d malformed packets out of %u\n", bad, fifo.nb);
        avt_pkt_fifo_free(&fifo);
        return AVT_ERROR(EINVAL);
    }
    avt_log(avt, AVT_LOG_INFO,
            "QUIC datagram round-trip OK (%u/%d delivered)\n", fifo.nb, N);
    avt_pkt_fifo_free(&fifo);
    return 0;
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

    ret = test_datagrams(avt, p, p_client_ctx, p_server_ctx);
    if (ret < 0)
        goto end;

    ret = test_multi_stream(avt, p, p_client_ctx, p_server_ctx);
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

    /* The generated test certificate/key are throwaway; don't leave them
     * behind in the working directory. */
    remove("test_cert.pem");
    remove("test_key.pem");

    return ret < 0 ? 1 : 0;
}
