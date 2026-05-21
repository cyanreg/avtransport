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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/quic.h>

#include <avtransport/avtransport.h>
#include "protocol_common.h"
#include "io_common.h"
#include "bytestream.h"
#include "ldpc_decode.h"

/* NOTE: when updating the spec version, update the last digit of the ALPN */
static const uint8_t avt_alpn[] = { 4, 'a', 'v', 't', '0' };

typedef struct AVTSSL {
    SSL *ssl;
    /* Datagram BIO wrapping the io socket. Owned by the SSL object once set
     * via SSL_set_bio(). NULL for connections obtained from a listener. */
    BIO *bio;
} AVTSSL;

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;
    AVTProtocolOpts opts;

    OSSL_LIB_CTX *ossl;
    SSL_CTX *ssl_ctx;

    bool listen;
    bool established;
    char hostname[256];

    /* Server only: the listening SSL object. */
    AVTSSL listener;

    /* The QUIC connection. For a client this is created at init; for a
     * server it is produced by SSL_accept_connection() during the handshake. */
    AVTSSL conn;

    /* Reliable QUIC streams. tx is created on the first send, rx is accepted
     * on the first receive. */
    SSL *tx_stream;
    SSL *rx_stream;

    /* Transmit buffer: packets are appended here and pushed onto the stream
     * opportunistically, so a flow-controlled write never forces a caller to
     * resend (which would corrupt the byte stream). */
    uint8_t *tx_buf;
    size_t tx_len;
    size_t tx_alloc;

    /* Receive reassembly buffer. A QUIC stream is a byte stream, so packets
     * are reframed from their self-describing AVT headers. */
    uint8_t *rx_buf;
    size_t rx_len;
    size_t rx_alloc;

    /* Set once the peer concludes the receive stream (clean EOF). */
    bool rx_eof;
};

/* Drive the QUIC handshake (defined below; also called by the protocol
 * test). */
int avt_quic_handshake(AVTProtocolCtx *p, int64_t timeout);

/* Push the buffered transmit data onto the QUIC stream (defined below). */
static int quic_tx_drain(AVTProtocolCtx *p, int64_t timeout);

static inline int reason_to_err(unsigned long sslerr)
{
    switch (ERR_GET_REASON(sslerr)) {
    case ERR_R_MALLOC_FAILURE: return AVT_ERROR(ENOMEM);
    default:                   return AVT_ERROR(EINVAL);
    }
}

/* Log the topmost OpenSSL error and translate it to a negative error code. */
static int log_ssl_err(void *log_ctx, const char *msg)
{
    unsigned long e = ERR_get_error();
    avt_log(log_ctx, AVT_LOG_ERROR, "%s: %s\n", msg,
            e ? ERR_error_string(e, NULL) : "unknown error");
    return e ? reason_to_err(e) : AVT_ERROR(EIO);
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

/* Create the listener SSL (server) or the connection SSL (client), wrapping
 * the io's UDP socket in a datagram BIO. */
static int create_ssl(AVTProtocolCtx *p, AVTAddress *addr, int fd)
{
    AVTSSL *s = p->listen ? &p->listener : &p->conn;

    s->bio = BIO_new_dgram(fd, BIO_NOCLOSE);
    if (!s->bio)
        return AVT_ERROR(ENOMEM);

    s->ssl = p->listen ? SSL_new_listener(p->ssl_ctx, 0)
                       : SSL_new(p->ssl_ctx);
    if (!s->ssl) {
        BIO_free(s->bio);
        s->bio = NULL;
        return log_ssl_err(p, "Unable to create QUIC SSL object");
    }

    /* The SSL object takes ownership of the BIO; a single reference covers
     * both the read and write side when rbio == wbio. From here on the BIO
     * is freed together with the SSL object. */
    SSL_set_bio(s->ssl, s->bio, s->bio);

    /* The protocol layer drives I/O and events explicitly. */
    if (!SSL_set_blocking_mode(s->ssl, 0))
        return log_ssl_err(p, "Unable to set non-blocking mode");

    if (p->listen) {
        if (!SSL_listen(s->ssl))
            return log_ssl_err(p, "Unable to start QUIC listener");
        return 0;
    }

    /* Use explicit streams rather than an implicit default stream. */
    if (!SSL_set_default_stream_mode(s->ssl, SSL_DEFAULT_STREAM_MODE_NONE))
        return log_ssl_err(p, "Unable to set the QUIC stream mode");

    /* Client: verify the server's hostname and announce the ALPN. */
    if (!SSL_set1_dnsname(s->ssl, p->hostname))
        return log_ssl_err(p, "Unable to set connection hostname");

    /* SSL_set_alpn_protos() returns 0 on success. */
    if (SSL_set_alpn_protos(s->ssl, avt_alpn, sizeof(avt_alpn)))
        return log_ssl_err(p, "Unable to set ALPN");

    /* Tell QUIC where the initial packet should go. The address is always
     * stored IPv6-mapped, matching the io's dual-stack socket. */
    BIO_ADDR *peer = BIO_ADDR_new();
    if (!peer)
        return AVT_ERROR(ENOMEM);

    if (!BIO_ADDR_rawmake(peer, AF_INET6, addr->ip, sizeof(addr->ip),
                          htons(addr->port)) ||
        !SSL_set1_initial_peer_addr(s->ssl, peer)) {
        BIO_ADDR_free(peer);
        return log_ssl_err(p, "Unable to set the initial peer address");
    }
    BIO_ADDR_free(peer);

    return 0;
}

static COLD int quic_proto_close(AVTProtocolCtx **_p)
{
    AVTProtocolCtx *p = *_p;
    if (!p)
        return 0;

    /* Gracefully shut an established connection down: conclude the transmit
     * stream so the peer sees a clean EOF, then drain via SSL_shutdown() so
     * buffered data is delivered before the connection is torn down. */
    if (p->established && p->conn.ssl) {
        if (p->tx_stream) {
            /* Flush any still-buffered payload, then signal a clean FIN. */
            quic_tx_drain(p, 2LL * 1000 * 1000 * 1000);
            SSL_stream_conclude(p->tx_stream, 0);

            /* The payload was already drained; this only needs to carry the
             * stream FIN and the connection close. */
            int64_t deadline = avt_get_time_ns() + 500LL * 1000 * 1000;
            do {
                SSL_handle_events(p->conn.ssl);
                if (SSL_shutdown(p->conn.ssl) == 1)
                    break;
                nanosleep(&(struct timespec){ .tv_nsec = 1000 * 1000 }, NULL);
            } while (avt_get_time_ns() < deadline);
        } else {
            SSL_shutdown(p->conn.ssl);
        }
    }

    /* Free streams before the connections that own them. SSL_free() also
     * frees the BIO attached via SSL_set_bio(); the server's connection SSL
     * has no BIO of its own (it shares the listener's). */
    SSL_free(p->tx_stream);
    SSL_free(p->rx_stream);
    SSL_free(p->conn.ssl);
    SSL_free(p->listener.ssl);

    SSL_CTX_free(p->ssl_ctx);
    OSSL_LIB_CTX_free(p->ossl);

    free(p->tx_buf);
    free(p->rx_buf);
    free(p);
    *_p = NULL;
    return 0;
}

static COLD int quic_proto_init(AVTContext *ctx, AVTProtocolCtx **_p, AVTAddress *addr,
                                const AVTIO *io, AVTIOCtx *io_ctx, AVTProtocolOpts *opts)
{
    int ret;

    /* QUIC drives the UDP socket directly, so the io backend must expose it. */
    if (!io->get_fd) {
        avt_log(ctx, AVT_LOG_ERROR, "QUIC requires a socket-based I/O\n");
        return AVT_ERROR(EINVAL);
    }
    int fd = io->get_fd(io_ctx);
    if (fd < 0) {
        avt_log(ctx, AVT_LOG_ERROR, "QUIC I/O has no usable socket\n");
        return AVT_ERROR(EINVAL);
    }

    AVTProtocolCtx *p = calloc(1, sizeof(*p));
    if (!p)
        return AVT_ERROR(ENOMEM);

    p->io = io;
    p->io_ctx = io_ctx;
    p->listen = addr->listen;
    if (opts)
        p->opts = *opts;
    memcpy(p->hostname, addr->host, sizeof(p->hostname));
    p->hostname[sizeof(p->hostname) - 1] = '\0';

    p->ossl = OSSL_LIB_CTX_new();
    if (!p->ossl) {
        ret = AVT_ERROR(ENOMEM);
        goto fail;
    }

    p->ssl_ctx = SSL_CTX_new_ex(p->ossl, NULL,
                                addr->listen ? OSSL_QUIC_server_method() :
                                               OSSL_QUIC_client_method());
    if (!p->ssl_ctx) {
        ret = log_ssl_err(p, "Unable to create the SSL context");
        goto fail;
    }

    if (addr->listen) {
        /* Server: load our own certificate and matching private key. */
        if (!addr->opts.certfile || !addr->opts.keyfile) {
            avt_log(p, AVT_LOG_ERROR,
                    "A certificate and key are required for a QUIC server\n");
            ret = AVT_ERROR(EINVAL);
            goto fail;
        }

        if (SSL_CTX_use_certificate_file(p->ssl_ctx, addr->opts.certfile,
                                         SSL_FILETYPE_PEM) <= 0) {
            ret = log_ssl_err(p, "Unable to load the certificate file");
            goto fail;
        }
        if (SSL_CTX_use_PrivateKey_file(p->ssl_ctx, addr->opts.keyfile,
                                        SSL_FILETYPE_PEM) <= 0) {
            ret = log_ssl_err(p, "Unable to load the private key file");
            goto fail;
        }
        if (!SSL_CTX_check_private_key(p->ssl_ctx)) {
            ret = log_ssl_err(p, "Private key does not match the certificate");
            goto fail;
        }

        SSL_CTX_set_alpn_select_cb(p->ssl_ctx, alpn_select_cb, NULL);
    } else {
        /* Client: verify the server. A certfile, when given, is treated as a
         * trusted root (e.g. a self-signed certificate); otherwise the system
         * default trust store is used. */
        if (addr->opts.certfile) {
            if (!SSL_CTX_load_verify_file(p->ssl_ctx, addr->opts.certfile)) {
                ret = log_ssl_err(p, "Unable to load the trusted certificate");
                goto fail;
            }
        } else if (!SSL_CTX_set_default_verify_paths(p->ssl_ctx)) {
            ret = log_ssl_err(p, "Unable to load the default trust store");
            goto fail;
        }

        SSL_CTX_set_verify(p->ssl_ctx, SSL_VERIFY_PEER, NULL);
    }

    ret = create_ssl(p, addr, fd);
    if (ret < 0)
        goto fail;

    *_p = p;
    return 0;

fail:
    quic_proto_close(&p);
    return ret;
}

/* Perform one non-blocking step of the QUIC handshake. Returns 0 once the
 * connection is established, AVT_ERROR(EAGAIN) if more I/O is required, or a
 * negative error code on failure. */
static int quic_handshake_step(AVTProtocolCtx *p)
{
    if (p->established)
        return 0;

    if (p->listen) {
        /* Process incoming datagrams so the listener can spot new clients. */
        SSL_handle_events(p->listener.ssl);

        if (!p->conn.ssl) {
            p->conn.ssl = SSL_accept_connection(p->listener.ssl, 0);
            if (!p->conn.ssl)
                return AVT_ERROR(EAGAIN);
            SSL_set_blocking_mode(p->conn.ssl, 0);
            SSL_set_default_stream_mode(p->conn.ssl,
                                        SSL_DEFAULT_STREAM_MODE_NONE);
        }

        SSL_handle_events(p->conn.ssl);
        if (!SSL_is_init_finished(p->conn.ssl))
            return AVT_ERROR(EAGAIN);
    } else {
        SSL_handle_events(p->conn.ssl);

        int r = SSL_do_handshake(p->conn.ssl);
        if (r != 1) {
            int err = SSL_get_error(p->conn.ssl, r);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                return AVT_ERROR(EAGAIN);
            return log_ssl_err(p, "QUIC handshake failed");
        }
    }

    p->established = true;
    avt_log(p, AVT_LOG_VERBOSE, "QUIC handshake completed\n");
    return 0;
}

/* Drive the QUIC handshake. With a positive timeout (nanoseconds) this polls
 * until the connection is established; with a zero timeout it performs a
 * single non-blocking step. Returns 0 once established, AVT_ERROR(EAGAIN) if
 * not yet, or a negative error code. */
int avt_quic_handshake(AVTProtocolCtx *p, int64_t timeout)
{
    int64_t now = avt_get_time_ns();
    int64_t deadline = (timeout > 0 && timeout < (INT64_MAX - now)) ?
                       now + timeout : INT64_MAX;

    for (;;) {
        int ret = quic_handshake_step(p);
        if (ret != AVT_ERROR(EAGAIN))
            return ret;
        if (timeout <= 0 || avt_get_time_ns() >= deadline)
            return AVT_ERROR(EAGAIN);
        nanosleep(&(struct timespec){ .tv_nsec = 500 * 1000 }, NULL);
    }
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

/* Advance the handshake and, once established, create the transmit stream.
 * Not being established yet is not an error - data is buffered regardless. */
static int quic_prepare_tx(AVTProtocolCtx *p, int64_t timeout)
{
    int ret = avt_quic_handshake(p, timeout);
    if (ret < 0 && ret != AVT_ERROR(EAGAIN))
        return ret;

    if (p->established && !p->tx_stream) {
        p->tx_stream = SSL_new_stream(p->conn.ssl, 0);
        if (!p->tx_stream)
            return log_ssl_err(p, "Unable to create a QUIC stream");
        /* Permit short writes so the transmit buffer can be drained
         * incrementally across calls, without the strict SSL_write() retry
         * contract that forbids a moved or resized buffer. */
        SSL_set_mode(p->tx_stream, SSL_MODE_ENABLE_PARTIAL_WRITE);
    }

    return 0;
}

/* Append bytes to the transmit buffer, growing it geometrically. */
static int quic_tx_append(AVTProtocolCtx *p, const uint8_t *data, size_t len)
{
    if (!len)
        return 0;

    if (p->tx_len + len > p->tx_alloc) {
        size_t na = p->tx_alloc ? p->tx_alloc * 2 : 65536;
        if (na < p->tx_len + len)
            na = p->tx_len + len;
        uint8_t *nb = realloc(p->tx_buf, na);
        if (!nb)
            return AVT_ERROR(ENOMEM);
        p->tx_buf = nb;
        p->tx_alloc = na;
    }

    memcpy(p->tx_buf + p->tx_len, data, len);
    p->tx_len += len;
    return 0;
}

/* Push as much of the transmit buffer onto the QUIC stream as it will take.
 * With a positive timeout this polls while the stream is flow-controlled;
 * with a zero timeout it returns once the stream would block, leaving the
 * remainder buffered for a later call. A flow-control stall is not an error,
 * so callers never have to resend. */
static int quic_tx_drain(AVTProtocolCtx *p, int64_t timeout)
{
    if (!p->tx_stream || !p->tx_len)
        return 0;

    int64_t now = avt_get_time_ns();
    int64_t deadline = (timeout > 0 && timeout < (INT64_MAX - now)) ?
                       now + timeout : INT64_MAX;

    size_t off = 0;
    int ret = 0;
    while (off < p->tx_len) {
        size_t w = 0;
        int r = SSL_write_ex(p->tx_stream, p->tx_buf + off,
                             p->tx_len - off, &w);
        if (r && w) {
            off += w;
            continue;
        }

        if (!r) {
            int err = SSL_get_error(p->tx_stream, r);
            if (err != SSL_ERROR_WANT_WRITE && err != SSL_ERROR_WANT_READ) {
                ret = log_ssl_err(p, "QUIC stream write failed");
                break;
            }
        }

        /* Flow-controlled: leave the rest buffered for a later call. */
        if (timeout == 0 || avt_get_time_ns() >= deadline)
            break;
        SSL_handle_events(p->conn.ssl);
        nanosleep(&(struct timespec){ .tv_nsec = 200 * 1000 }, NULL);
    }

    /* Drop the bytes that made it onto the stream. */
    if (off) {
        memmove(p->tx_buf, p->tx_buf + off, p->tx_len - off);
        p->tx_len -= off;
    }

    SSL_handle_events(p->conn.ssl);
    return ret;
}

/* Buffer one packet - header followed by payload - for transmission. */
static int quic_tx_buffer_packet(AVTProtocolCtx *p, AVTPktd *pkt)
{
    int ret = quic_tx_append(p, pkt->hdr, pkt->hdr_len);
    if (ret < 0)
        return ret;

    size_t pl_len;
    uint8_t *pl = avt_buffer_get_data(&pkt->pl, &pl_len);
    if (pl && pl_len)
        ret = quic_tx_append(p, pl, pl_len);

    return ret;
}

static int quic_proto_send_packet(AVTProtocolCtx *p, AVTPktd *pkt,
                                  int64_t timeout)
{
    /* Packets are always accepted into the transmit buffer; transmission is
     * best-effort and finished by later send/flush calls. This way a
     * flow-controlled stream never makes a caller resend.
     *
     * Per the spec, data packets (descriptors 0x01xx and 0xFCxx-0xFFxx)
     * belong on an unreliable QUIC datagram and control packets on a
     * reliable stream; OpenSSL has no RFC 9221 datagram API yet, so for now
     * every packet uses the reliable stream. */
    int ret = quic_prepare_tx(p, timeout);
    if (ret < 0)
        return ret;

    ret = quic_tx_buffer_packet(p, pkt);
    if (ret < 0)
        return ret;

    return quic_tx_drain(p, timeout);
}

static int quic_proto_send_seq(AVTProtocolCtx *p, AVTPacketFifo *seq,
                               int64_t timeout)
{
    int ret = quic_prepare_tx(p, timeout);
    if (ret < 0)
        return ret;

    for (uint32_t i = 0; i < seq->nb; i++) {
        ret = quic_tx_buffer_packet(p, &seq->data[i]);
        if (ret < 0)
            return ret;
    }

    return quic_tx_drain(p, timeout);
}

/* Reframe one complete AVT packet from the head of the receive buffer.
 * Returns 0 with the packet pushed onto the fifo, AVT_ERROR(EAGAIN) if a
 * full packet is not yet buffered, or another negative error. */
static int quic_extract_packet(AVTProtocolCtx *p, AVTPacketFifo *fifo)
{
    if (p->rx_len < AVT_MIN_HEADER_LEN)
        return AVT_ERROR(EAGAIN);

    /* Probe the header to determine the full packet size. */
    AVTPktd probe = { };
    memcpy(probe.hdr, p->rx_buf, AVT_MIN_HEADER_LEN);
    avt_ldpc_decode_288_224(probe.hdr, p->opts.ldpc_iterations);

    uint16_t desc = AVT_RB16(&probe.hdr[0]);
    if (((desc & 0xFF00) == (AVT_PKT_TIME_SYNC & 0xFF00)) ||
        ((desc & 0xFF00) == (AVT_PKT_STREAM_DATA & 0xFF00)))
        desc &= 0xFF00;

    size_t hdr_size = avt_pkt_hdr_size(desc);
    if (hdr_size < AVT_MIN_HEADER_LEN || hdr_size > sizeof(probe.hdr)) {
        avt_log(p, AVT_LOG_ERROR, "Invalid QUIC packet header size %zu\n",
                hdr_size);
        return AVT_ERROR(EINVAL);
    }
    if (p->rx_len < hdr_size)
        return AVT_ERROR(EAGAIN);

    /* Decode the rest of the header. */
    int left = (int)hdr_size - AVT_MIN_HEADER_LEN;
    if (left > 0) {
        memcpy(probe.hdr + AVT_MIN_HEADER_LEN,
               p->rx_buf + AVT_MIN_HEADER_LEN, left);
        if (left == 36)
            avt_ldpc_decode_288_224(probe.hdr + AVT_MIN_HEADER_LEN,
                                    p->opts.ldpc_iterations);
        else if (left == 384)
            avt_ldpc_decode_2784_2016(probe.hdr + AVT_MIN_HEADER_LEN,
                                      p->opts.ldpc_iterations);
    }

    size_t pl_bytes = 0;
    int dret = avt_packet_decode(&probe, desc, &pl_bytes);
    if (dret < 0) {
        avt_log(p, AVT_LOG_ERROR, "Unknown descriptor 0x%x received\n", desc);
        return dret;
    }

    size_t total = hdr_size + pl_bytes;
    if (p->rx_len < total)
        return AVT_ERROR(EAGAIN);

    /* The whole packet is buffered: commit it to the fifo. */
    AVTPktd *pkt = avt_pkt_fifo_push_new(fifo, NULL, 0, AVT_BUFFER_REF_ALL);
    if (!pkt)
        return AVT_ERROR(ENOMEM);

    memcpy(pkt->hdr, probe.hdr, hdr_size);
    pkt->hdr_len = hdr_size;
    pkt->pkt = probe.pkt;

    if (pl_bytes) {
        if (!avt_buffer_quick_alloc(&pkt->pl, pl_bytes))
            return AVT_ERROR(ENOMEM);
        memcpy(pkt->pl.data, p->rx_buf + hdr_size, pl_bytes);
    }

    /* Drop the consumed bytes from the reassembly buffer. */
    memmove(p->rx_buf, p->rx_buf + total, p->rx_len - total);
    p->rx_len -= total;

    return 0;
}

static int quic_proto_receive(AVTProtocolCtx *p, AVTPacketFifo *fifo,
                              int64_t timeout)
{
    if (!p->established) {
        int ret = avt_quic_handshake(p, timeout);
        if (ret < 0)
            return ret;
        if (!p->established)
            return AVT_ERROR(EAGAIN);
    }

    SSL_handle_events(p->conn.ssl);

    /* Accept the peer's stream on first use. */
    if (!p->rx_stream) {
        p->rx_stream = SSL_accept_stream(p->conn.ssl, 0);
        if (!p->rx_stream)
            return AVT_ERROR(EAGAIN);
    }

    /* Drain whatever stream data is available into the reassembly buffer. */
    for (;;) {
        if (p->rx_len + 4096 > p->rx_alloc) {
            size_t na = p->rx_alloc ? p->rx_alloc * 2 : 32768;
            if (na < p->rx_len + 4096)
                na = p->rx_len + 4096;
            uint8_t *nb = realloc(p->rx_buf, na);
            if (!nb)
                return AVT_ERROR(ENOMEM);
            p->rx_buf = nb;
            p->rx_alloc = na;
        }

        size_t got = 0;
        int r = SSL_read_ex(p->rx_stream, p->rx_buf + p->rx_len,
                            p->rx_alloc - p->rx_len, &got);
        if (r) {
            p->rx_len += got;
            continue;
        }

        int err = SSL_get_error(p->rx_stream, r);
        if (err == SSL_ERROR_ZERO_RETURN) {
            /* The peer concluded the stream: no more data will arrive. */
            p->rx_eof = true;
            break;
        }
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            break;
        return log_ssl_err(p, "QUIC stream read failed");
    }

    int ret = quic_extract_packet(p, fifo);
    if (ret == AVT_ERROR(EAGAIN) && p->rx_eof)
        return AVT_ERROR(ENODATA);
    return ret;
}

static int quic_proto_max_pkt_len(AVTProtocolCtx *p, size_t *mtu)
{
    size_t tmp;
    const size_t udp_hdr_size = 8;
    const size_t quic_hdr_size = 256; // TODO: use BIO_dgram_get_mtu_overhead();

    int ret = p->io->get_max_pkt_len(p->io_ctx, &tmp);
    if (ret < 0 || (tmp < (AVT_MIN_HEADER_LEN - udp_hdr_size -  quic_hdr_size)))
        return ret;

    *mtu = tmp - udp_hdr_size - quic_hdr_size;

    return 0;
}

static int quic_proto_flush(AVTProtocolCtx *p, int64_t timeout)
{
    if (!p->established || !p->tx_stream) {
        if (p->conn.ssl)
            SSL_handle_events(p->conn.ssl);
        return 0;
    }

    /* Push the transmit buffer onto the QUIC stream first. */
    int dret = quic_tx_drain(p, timeout);
    if (dret < 0)
        return dret;

    /* Then pump until the transmit stream's send buffer empties, i.e. every
     * byte has been sent and acknowledged by the peer. Bounded by the
     * timeout. */
    int64_t now = avt_get_time_ns();
    int64_t deadline = (timeout > 0 && timeout < (INT64_MAX - now)) ?
                       now + timeout : INT64_MAX;

    for (;;) {
        SSL_handle_events(p->conn.ssl);

        uint64_t used = 0;
        if (!SSL_get_stream_write_buf_used(p->tx_stream, &used) || used == 0)
            return 0;
        if (timeout == 0 || avt_get_time_ns() >= deadline)
            return 0;
        nanosleep(&(struct timespec){ .tv_nsec = 500 * 1000 }, NULL);
    }
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
    .receive = quic_proto_receive,
    .seek = NULL,
    .flush = quic_proto_flush,
    .close = quic_proto_close,
};
