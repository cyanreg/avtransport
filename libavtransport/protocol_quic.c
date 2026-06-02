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
#include <openssl/rand.h>

#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>

#include <avtransport/avtransport.h>
#include "protocol_common.h"
#include "io_common.h"
#include "bytestream.h"
#include "ldpc_decode.h"

/* NOTE: when updating the spec version, update the last digit of the ALPN */
static const uint8_t avt_alpn[] = { 4, 'a', 'v', 't', '0' };

/* Single UDP packet scratch buffer. 1452 is the conservative IPv6+UDP MSS that
 * ngtcp2 defaults to; we round up a touch for path MTU growth. Datagram frames
 * are not fragmentable, so any AVT data packet must fit within this. */
#define QUIC_UDP_BUF_SIZE 1500

/* Base receive flow-control window, matching the initial_max_stream_data_*
 * transport params. We grant the peer this much headroom past whatever the
 * application has consumed (see fc_grant), so a slow consumer throttles the
 * peer instead of letting the reassembly buffer grow without bound. */
#define QUIC_STREAM_FC_WINDOW (1u << 20)

/* A single immovable chunk of outgoing stream data. ngtcp2 holds pointers
 * into caller-supplied bytes (it does not copy them) and may retransmit
 * from those pointers later — so we must never realloc after handing data
 * over. Each stream_tx_append() allocates its own chunk, and chunks are
 * freed only after the peer has acknowledged every byte they hold. */
typedef struct AVTQuicTxChunk {
    uint8_t *data;
    size_t len;
    size_t consumed;       /* bytes already passed to writev_stream */
    uint64_t base_off;     /* stream offset of byte 0 of this chunk */
} AVTQuicTxChunk;

typedef struct AVTQuicStream {
    int64_t qid;            /* QUIC stream ID, or -1 until opened */
    uint16_t avt_stream_id; /* AVT stream ID this is mapped to */
    bool is_control;        /* true for the global control stream */

    /* Outgoing chunks, FIFO. Bytes are appended to the tail and consumed
     * from the head once ngtcp2 acknowledges them. */
    AVTQuicTxChunk *chunks;
    int nb_chunks;
    int alloc_chunks;
    uint64_t tx_total;      /* total bytes ever appended (= next chunk's base_off) */

    /* Incoming reassembly: QUIC streams are byte streams, AVT packets are
     * self-describing, so we reframe them here. */
    uint8_t *rx_buf;
    size_t rx_len;
    size_t rx_alloc;
    bool rx_eof;

    /* Receive flow control. We extend the QUIC window as the application
     * consumes whole packets (backpressure), but always keep it at least one
     * full in-flight packet ahead so a packet larger than the base window can
     * never deadlock. fc_max is the absolute stream-offset limit we have
     * granted the peer; fc_consumed is the offset up to which we have handed
     * complete packets to the fifo. */
    uint64_t fc_max;
    uint64_t fc_consumed;
} AVTQuicStream;

typedef struct AVTQuicDgram {
    uint8_t *data;
    size_t len;
} AVTQuicDgram;

struct AVTProtocolCtx {
    const AVTIO *io;
    AVTIOCtx *io_ctx;
    AVTProtocolOpts opts;

    bool listen;
    bool established;
    bool closing;
    char hostname[256];
    int fd;

    /* TLS + ngtcp2 wiring. The ngtcp2_crypto_conn_ref ties an SSL object back
     * to its ngtcp2_conn via SSL_get_app_data() from inside crypto callbacks. */
    SSL_CTX *ssl_ctx;
    SSL *ssl;
    ngtcp2_crypto_ossl_ctx *ossl_ctx;
    ngtcp2_crypto_conn_ref conn_ref;

    ngtcp2_conn *conn;
    /* The current network path. local is fixed from the bound socket; remote
     * is replaced from recvmsg on the server side (the client socket is
     * connect()ed, so its remote is constant). */
    struct sockaddr_in6 local_addr;
    socklen_t local_addr_len;
    struct sockaddr_in6 remote_addr;
    socklen_t remote_addr_len;

    /* AVT→QUIC stream mapping. The control stream carries every reliable
     * packet that lacks an AVT stream_id (session start, FEC group, etc).
     * tx_streams[] is keyed by avt_stream_id; rx_streams[] is keyed by the
     * peer-assigned QUIC stream id, so they are separate tables. */
    AVTQuicStream **tx_streams;
    int nb_tx_streams;
    int tx_rr;                  /* round-robin cursor for fair stream service */
    AVTQuicStream *control_stream;

    AVTQuicStream **rx_streams;
    int nb_rx_streams;

    /* Outgoing datagrams: AVT data packets routed to RFC 9221 DATAGRAM
     * frames. Each entry is the full serialized packet (header + payload). */
    AVTQuicDgram *tx_dgrams;
    int nb_tx_dgrams;
    int nb_alloc_tx_dgrams;

    /* Incoming datagrams collected from the recv_datagram callback during
     * read_pkt; drained into the receive fifo after read_pkt returns. */
    AVTQuicDgram *rx_dgrams;
    int nb_rx_dgrams;
    int nb_alloc_rx_dgrams;
};

int avt_quic_handshake(AVTProtocolCtx *p, int64_t timeout);
static void open_pending_tx_streams(AVTProtocolCtx *p);

/* Monotonic timestamp for ngtcp2. The library compares timestamps among
 * themselves and against its own monotonic deltas, so a wall clock would
 * break retransmission logic. */
static ngtcp2_tstamp quic_now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (ngtcp2_tstamp)ts.tv_sec * NGTCP2_SECONDS + (ngtcp2_tstamp)ts.tv_nsec;
}

static inline ngtcp2_conn *cb_get_conn(ngtcp2_crypto_conn_ref *ref)
{
    AVTProtocolCtx *p = ref->user_data;
    return p->conn;
}

/* Log a NGTCP2 error and translate it to a negative AVT error code. */
static int log_ngtcp2_err(void *log_ctx, const char *msg, int e)
{
    avt_log(log_ctx, AVT_LOG_ERROR, "%s: %s (%d)\n", msg, ngtcp2_strerror(e), e);
    if (e == NGTCP2_ERR_NOMEM)
        return AVT_ERROR(ENOMEM);
    return AVT_ERROR(EIO);
}

/* Spec classification. A descriptor goes on a QUIC datagram if its high byte
 * is 0x01 (the STREAM_DATA family) or 0x02 (the EXTENDED_STREAM_DATA family) -
 * both carry media payload, with the low byte holding flags - or if it is one
 * of the bare data descriptors 0xFC/0xFD/0xFE/0xFF (STREAM_DATA parity/segment
 * and reserved). All other packets travel on a reliable QUIC stream. See
 * draft-avtransport-spec §QUIC. */
static inline bool desc_is_unreliable(uint16_t desc)
{
    if ((desc & 0xFF00) == 0x0100 || (desc & 0xFF00) == 0x0200)
        return true;
    if (desc == 0xFC || desc == 0xFD || desc == 0xFE || desc == 0xFF)
        return true;
    return false;
}

/* Look up the per-packet AVT stream_id if the descriptor carries one. The
 * generated AVTGeneric* structs share the {desc, global_seq, stream_id}
 * head with all the per-stream packet types, so reading via
 * pkt.stream_data.stream_id works for every descriptor that has a stream_id
 * field. Returns false for the few descriptors without one (session start,
 * time sync, FEC grouping, user data). */
static bool packet_has_stream_id(uint16_t desc, AVTPktd *pkt, uint16_t *out)
{
    /* TIME_SYNC's enum carries the LSB-bitmask flag in bit 31, so its
     * descriptor exceeds uint16_t and can't appear in the switch below;
     * match it via the truncated 0x03** wire prefix instead. */
    if ((desc & 0xFF00) == 0x0300)
        return false;

    switch (desc) {
    case AVT_PKT_SESSION_START:
    case AVT_PKT_FEC_GROUPING:
    case AVT_PKT_FEC_GROUP_DATA:
    case AVT_PKT_USER_DATA:
    case AVT_PKT_USER_DATA_SEGMENT:
    case AVT_PKT_USER_DATA_PARITY:
        return false;
    default:
        *out = pkt->pkt.stream_id;
        return true;
    }
}

static AVTQuicStream *stream_new(uint16_t avt_id, bool is_control)
{
    AVTQuicStream *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->qid = -1;
    s->avt_stream_id = avt_id;
    s->is_control = is_control;
    s->fc_max = QUIC_STREAM_FC_WINDOW; /* matches the advertised initial window */
    return s;
}

static void stream_free(AVTQuicStream *s)
{
    if (!s)
        return;
    for (int i = 0; i < s->nb_chunks; i++)
        free(s->chunks[i].data);
    free(s->chunks);
    free(s->rx_buf);
    free(s);
}

static AVTQuicStream *find_tx_stream(AVTProtocolCtx *p, uint16_t avt_id)
{
    for (int i = 0; i < p->nb_tx_streams; i++)
        if (p->tx_streams[i]->avt_stream_id == avt_id &&
            !p->tx_streams[i]->is_control)
            return p->tx_streams[i];
    return NULL;
}

static AVTQuicStream *find_rx_stream(AVTProtocolCtx *p, int64_t qid)
{
    for (int i = 0; i < p->nb_rx_streams; i++)
        if (p->rx_streams[i]->qid == qid)
            return p->rx_streams[i];
    return NULL;
}

/* Append a stream to a table, growing it geometrically. Steals ownership. */
static int table_push(AVTQuicStream ***tab, int *nb, AVTQuicStream *s)
{
    AVTQuicStream **n = realloc(*tab, sizeof(*n) * (*nb + 1));
    if (!n)
        return AVT_ERROR(ENOMEM);
    *tab = n;
    n[(*nb)++] = s;
    return 0;
}

static int dgram_queue_push(AVTQuicDgram **q, int *nb, int *alloc,
                            const uint8_t *src1, size_t l1,
                            const uint8_t *src2, size_t l2)
{
    if (*nb == *alloc) {
        int na = *alloc ? *alloc * 2 : 16;
        AVTQuicDgram *r = realloc(*q, sizeof(*r) * na);
        if (!r)
            return AVT_ERROR(ENOMEM);
        *q = r;
        *alloc = na;
    }
    size_t total = l1 + l2;
    uint8_t *buf = malloc(total ? total : 1);
    if (!buf)
        return AVT_ERROR(ENOMEM);
    if (l1)
        memcpy(buf, src1, l1);
    if (l2)
        memcpy(buf + l1, src2, l2);
    (*q)[*nb].data = buf;
    (*q)[*nb].len  = total;
    (*nb)++;
    return 0;
}

static void dgram_queue_pop_head(AVTQuicDgram *q, int *nb)
{
    if (*nb <= 0)
        return;
    free(q[0].data);
    memmove(&q[0], &q[1], sizeof(*q) * (*nb - 1));
    (*nb)--;
}

/* Append bytes to a stream's outgoing queue as a new stable chunk. The
 * chunk is malloc'd separately and never moved; ngtcp2 may retain a pointer
 * to it for retransmits until the peer acks. */
static int stream_tx_append(AVTQuicStream *s, const uint8_t *data, size_t len)
{
    if (!len)
        return 0;

    if (s->nb_chunks == s->alloc_chunks) {
        int na = s->alloc_chunks ? s->alloc_chunks * 2 : 8;
        AVTQuicTxChunk *nc = realloc(s->chunks, sizeof(*nc) * na);
        if (!nc)
            return AVT_ERROR(ENOMEM);
        s->chunks = nc;
        s->alloc_chunks = na;
    }
    uint8_t *buf = malloc(len);
    if (!buf)
        return AVT_ERROR(ENOMEM);
    memcpy(buf, data, len);
    s->chunks[s->nb_chunks++] = (AVTQuicTxChunk){
        .data = buf,
        .len = len,
        .consumed = 0,
        .base_off = s->tx_total,
    };
    s->tx_total += len;
    return 0;
}

/* Drop fully-acked chunks from the head. acked is the stream offset up to
 * which ngtcp2 reports the peer has received contiguous bytes. */
static void stream_tx_drop_acked(AVTQuicStream *s, uint64_t acked)
{
    int drop = 0;
    while (drop < s->nb_chunks &&
           s->chunks[drop].base_off + s->chunks[drop].len <= acked)
        drop++;
    if (!drop)
        return;
    for (int i = 0; i < drop; i++)
        free(s->chunks[i].data);
    memmove(&s->chunks[0], &s->chunks[drop],
            sizeof(*s->chunks) * (s->nb_chunks - drop));
    s->nb_chunks -= drop;
}

/* Return the first unsent byte pointer and remaining length within the
 * earliest chunk that has bytes ngtcp2 hasn't been given yet. Returns false
 * if everything appended has already been handed off. */
static bool stream_tx_peek(AVTQuicStream *s, const uint8_t **out, size_t *outlen)
{
    for (int i = 0; i < s->nb_chunks; i++) {
        AVTQuicTxChunk *c = &s->chunks[i];
        if (c->consumed < c->len) {
            *out    = c->data + c->consumed;
            *outlen = c->len  - c->consumed;
            return true;
        }
    }
    return false;
}

/* Advance the per-chunk consumed counters by `n` bytes; called after
 * writev_stream reports it accepted that many bytes. */
static void stream_tx_consume(AVTQuicStream *s, size_t n)
{
    for (int i = 0; i < s->nb_chunks && n; i++) {
        AVTQuicTxChunk *c = &s->chunks[i];
        size_t avail = c->len - c->consumed;
        size_t take = n < avail ? n : avail;
        c->consumed += take;
        n -= take;
    }
}

/* ngtcp2 callbacks --------------------------------------------------------- */

static int cb_recv_stream_data(ngtcp2_conn *conn, uint32_t flags,
                               int64_t stream_id, uint64_t offset,
                               const uint8_t *data, size_t datalen,
                               void *user_data, void *stream_user_data)
{
    (void)conn; (void)offset;
    AVTProtocolCtx *p = user_data;
    AVTQuicStream *s = stream_user_data;

    if (!s) {
        s = find_rx_stream(p, stream_id);
        if (!s) {
            s = stream_new(0, false);
            if (!s)
                return NGTCP2_ERR_NOMEM;
            s->qid = stream_id;
            if (table_push(&p->rx_streams, &p->nb_rx_streams, s) < 0) {
                stream_free(s);
                return NGTCP2_ERR_NOMEM;
            }
            ngtcp2_conn_set_stream_user_data(conn, stream_id, s);
        }
    }

    if (datalen) {
        if (s->rx_len + datalen > s->rx_alloc) {
            size_t na = s->rx_alloc ? s->rx_alloc * 2 : 65536;
            if (na < s->rx_len + datalen)
                na = s->rx_len + datalen;
            uint8_t *nb = realloc(s->rx_buf, na);
            if (!nb)
                return NGTCP2_ERR_NOMEM;
            s->rx_buf = nb;
            s->rx_alloc = na;
        }
        memcpy(s->rx_buf + s->rx_len, data, datalen);
        s->rx_len += datalen;
        /* Deliberately do NOT extend the flow-control window here. The window
         * is advanced from extract_from_stream() as the application consumes
         * whole packets, which is what gives us backpressure; see fc_grant. */
    }
    if (flags & NGTCP2_STREAM_DATA_FLAG_FIN)
        s->rx_eof = true;

    return 0;
}

static int cb_stream_open(ngtcp2_conn *conn, int64_t stream_id, void *user_data)
{
    AVTProtocolCtx *p = user_data;
    AVTQuicStream *s = find_rx_stream(p, stream_id);
    if (!s) {
        s = stream_new(0, false);
        if (!s)
            return NGTCP2_ERR_NOMEM;
        s->qid = stream_id;
        if (table_push(&p->rx_streams, &p->nb_rx_streams, s) < 0) {
            stream_free(s);
            return NGTCP2_ERR_NOMEM;
        }
    }
    ngtcp2_conn_set_stream_user_data(conn, stream_id, s);
    return 0;
}

static int cb_stream_close(ngtcp2_conn *conn, uint32_t flags, int64_t stream_id,
                           uint64_t app_error_code, void *user_data,
                           void *stream_user_data)
{
    (void)conn; (void)flags; (void)app_error_code;
    AVTProtocolCtx *p = user_data;
    AVTQuicStream *s = stream_user_data;
    if (s)
        s->rx_eof = true;
    (void)p;
    return 0;
}

static int cb_acked_stream_data_offset(ngtcp2_conn *conn, int64_t stream_id,
                                       uint64_t offset, uint64_t datalen,
                                       void *user_data, void *stream_user_data)
{
    (void)conn; (void)user_data;
    AVTQuicStream *s = stream_user_data;
    if (s)
        stream_tx_drop_acked(s, offset + datalen);
    (void)stream_id;
    return 0;
}

static int cb_recv_datagram(ngtcp2_conn *conn, uint32_t flags,
                            const uint8_t *data, size_t datalen,
                            void *user_data)
{
    (void)conn; (void)flags;
    AVTProtocolCtx *p = user_data;
    int r = dgram_queue_push(&p->rx_dgrams, &p->nb_rx_dgrams,
                             &p->nb_alloc_rx_dgrams,
                             data, datalen, NULL, 0);
    if (r < 0)
        return NGTCP2_ERR_NOMEM;
    return 0;
}

static void cb_rand(uint8_t *dest, size_t destlen,
                    const ngtcp2_rand_ctx *rand_ctx)
{
    (void)rand_ctx;
    if (RAND_bytes(dest, destlen) != 1) {
        /* Fall back to a deterministic but non-empty pattern; QUIC connection
         * IDs only need uniqueness for the lifetime of the connection. */
        for (size_t i = 0; i < destlen; i++)
            dest[i] = (uint8_t)i;
    }
}

static int cb_get_new_connection_id(ngtcp2_conn *conn, ngtcp2_cid *cid,
                                    uint8_t *token, size_t cidlen,
                                    void *user_data)
{
    (void)conn; (void)user_data;
    if (RAND_bytes(cid->data, cidlen) != 1)
        return NGTCP2_ERR_CALLBACK_FAILURE;
    cid->datalen = cidlen;
    if (RAND_bytes(token, NGTCP2_STATELESS_RESET_TOKENLEN) != 1)
        return NGTCP2_ERR_CALLBACK_FAILURE;
    return 0;
}

/* Build the callbacks table. The crypto helpers cover everything except
 * stream/datagram delivery and randomness. */
static void fill_callbacks(ngtcp2_callbacks *cb, bool client)
{
    memset(cb, 0, sizeof(*cb));
    if (client) {
        cb->client_initial = ngtcp2_crypto_client_initial_cb;
        cb->recv_retry     = ngtcp2_crypto_recv_retry_cb;
    } else {
        cb->recv_client_initial = ngtcp2_crypto_recv_client_initial_cb;
    }
    cb->recv_crypto_data        = ngtcp2_crypto_recv_crypto_data_cb;
    cb->encrypt                 = ngtcp2_crypto_encrypt_cb;
    cb->decrypt                 = ngtcp2_crypto_decrypt_cb;
    cb->hp_mask                 = ngtcp2_crypto_hp_mask_cb;
    cb->update_key              = ngtcp2_crypto_update_key_cb;
    cb->delete_crypto_aead_ctx  = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
    cb->delete_crypto_cipher_ctx= ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
    cb->get_path_challenge_data = ngtcp2_crypto_get_path_challenge_data_cb;
    cb->version_negotiation     = ngtcp2_crypto_version_negotiation_cb;

    cb->rand                    = cb_rand;
    cb->get_new_connection_id   = cb_get_new_connection_id;

    cb->recv_stream_data        = cb_recv_stream_data;
    cb->stream_open             = cb_stream_open;
    cb->stream_close            = cb_stream_close;
    cb->acked_stream_data_offset= cb_acked_stream_data_offset;
    cb->recv_datagram           = cb_recv_datagram;
}

/* TLS setup ---------------------------------------------------------------- */

/* Server-side ALPN selection. Same logic as before: insist on "avt0". */
static int alpn_select_cb(SSL *ssl, const unsigned char **out,
                          unsigned char *outlen, const unsigned char *in,
                          unsigned int inlen, void *arg)
{
    (void)ssl; (void)arg;
    int ret = SSL_select_next_proto((uint8_t **)out, outlen,
                                    avt_alpn, sizeof(avt_alpn), in, inlen);
    return ret == OPENSSL_NPN_NEGOTIATED ? SSL_TLSEXT_ERR_OK
                                         : SSL_TLSEXT_ERR_ALERT_FATAL;
}

static int build_ssl_ctx(AVTProtocolCtx *p, AVTAddress *addr)
{
    p->ssl_ctx = SSL_CTX_new(p->listen ? TLS_server_method() : TLS_client_method());
    if (!p->ssl_ctx) {
        avt_log(p, AVT_LOG_ERROR, "Unable to create SSL_CTX\n");
        return AVT_ERROR(ENOMEM);
    }

    /* TLS 1.3 is mandatory for QUIC. */
    SSL_CTX_set_min_proto_version(p->ssl_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(p->ssl_ctx, TLS1_3_VERSION);

    if (p->listen) {
        if (!addr->opts.certfile || !addr->opts.keyfile) {
            avt_log(p, AVT_LOG_ERROR,
                    "A certificate and key are required for a QUIC server\n");
            return AVT_ERROR(EINVAL);
        }
        if (SSL_CTX_use_certificate_file(p->ssl_ctx, addr->opts.certfile,
                                         SSL_FILETYPE_PEM) <= 0) {
            avt_log(p, AVT_LOG_ERROR, "Unable to load the certificate file\n");
            return AVT_ERROR(EINVAL);
        }
        if (SSL_CTX_use_PrivateKey_file(p->ssl_ctx, addr->opts.keyfile,
                                        SSL_FILETYPE_PEM) <= 0) {
            avt_log(p, AVT_LOG_ERROR, "Unable to load the private key file\n");
            return AVT_ERROR(EINVAL);
        }
        if (!SSL_CTX_check_private_key(p->ssl_ctx)) {
            avt_log(p, AVT_LOG_ERROR,
                    "Private key does not match the certificate\n");
            return AVT_ERROR(EINVAL);
        }
        SSL_CTX_set_alpn_select_cb(p->ssl_ctx, alpn_select_cb, NULL);
    } else {
        if (addr->opts.certfile) {
            if (!SSL_CTX_load_verify_file(p->ssl_ctx, addr->opts.certfile)) {
                avt_log(p, AVT_LOG_ERROR,
                        "Unable to load the trusted certificate\n");
                return AVT_ERROR(EINVAL);
            }
        } else if (!SSL_CTX_set_default_verify_paths(p->ssl_ctx)) {
            avt_log(p, AVT_LOG_ERROR, "Unable to load the default trust store\n");
            return AVT_ERROR(EINVAL);
        }
        SSL_CTX_set_verify(p->ssl_ctx, SSL_VERIFY_PEER, NULL);
    }
    return 0;
}

static int build_ssl(AVTProtocolCtx *p)
{
    /* The OSSL crypto helper expects to be handed an ngtcp2_crypto_ossl_ctx
     * which owns the SSL object; allocate it first, then attach an SSL. */
    if (ngtcp2_crypto_ossl_ctx_new(&p->ossl_ctx, NULL) != 0)
        return AVT_ERROR(ENOMEM);

    p->ssl = SSL_new(p->ssl_ctx);
    if (!p->ssl)
        return AVT_ERROR(ENOMEM);

    ngtcp2_crypto_ossl_ctx_set_ssl(p->ossl_ctx, p->ssl);

    p->conn_ref.get_conn  = cb_get_conn;
    p->conn_ref.user_data = p;

    if (p->listen) {
        if (ngtcp2_crypto_ossl_configure_server_session(p->ssl) != 0)
            return AVT_ERROR(EINVAL);
        SSL_set_app_data(p->ssl, &p->conn_ref);
        SSL_set_accept_state(p->ssl);
    } else {
        if (ngtcp2_crypto_ossl_configure_client_session(p->ssl) != 0)
            return AVT_ERROR(EINVAL);
        SSL_set_app_data(p->ssl, &p->conn_ref);
        SSL_set_connect_state(p->ssl);
        /* Hostname verification + ALPN announcement. SSL_set1_host pins the
         * verification name used during the TLS handshake; the SNI is set
         * separately via SSL_set_tlsext_host_name. */
        if (!SSL_set1_host(p->ssl, p->hostname))
            return AVT_ERROR(EINVAL);
        if (SSL_set_alpn_protos(p->ssl, avt_alpn, sizeof(avt_alpn)) != 0)
            return AVT_ERROR(EINVAL);
        SSL_set_tlsext_host_name(p->ssl, p->hostname);
    }

    return 0;
}

/* Helpers to plug a sockaddr into an ngtcp2_path. */
static void path_set(ngtcp2_path *path,
                     struct sockaddr_in6 *local, socklen_t llen,
                     struct sockaddr_in6 *remote, socklen_t rlen)
{
    path->local.addr  = (ngtcp2_sockaddr *)local;
    path->local.addrlen = llen;
    path->remote.addr = (ngtcp2_sockaddr *)remote;
    path->remote.addrlen = rlen;
    path->user_data = NULL;
}

/* Initial ngtcp2 connection object. For the client this is called from
 * quic_proto_init; for the server it is deferred until the first Initial
 * packet arrives and supplies the DCID. */
static int create_client_conn(AVTProtocolCtx *p)
{
    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    settings.initial_ts = quic_now();

    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);
    params.initial_max_streams_bidi = 64;
    params.initial_max_streams_uni  = 64;
    params.initial_max_stream_data_bidi_local  = 1 << 20;
    params.initial_max_stream_data_bidi_remote = 1 << 20;
    params.initial_max_stream_data_uni         = 1 << 20;
    params.initial_max_data                    = 16 << 20;
    params.max_idle_timeout = 30 * NGTCP2_SECONDS;
    params.max_datagram_frame_size = 65535;
    params.active_connection_id_limit = 4;

    ngtcp2_callbacks cbs;
    fill_callbacks(&cbs, true);

    ngtcp2_cid dcid = { 0 }, scid = { 0 };
    dcid.datalen = NGTCP2_MIN_INITIAL_DCIDLEN;
    scid.datalen = 8;
    cb_rand(dcid.data, dcid.datalen, NULL);
    cb_rand(scid.data, scid.datalen, NULL);

    ngtcp2_path path;
    path_set(&path, &p->local_addr, p->local_addr_len,
                    &p->remote_addr, p->remote_addr_len);

    int r = ngtcp2_conn_client_new(&p->conn, &dcid, &scid, &path,
                                   NGTCP2_PROTO_VER_V1, &cbs,
                                   &settings, &params, NULL, p);
    if (r < 0)
        return log_ngtcp2_err(p, "ngtcp2_conn_client_new failed", r);

    ngtcp2_conn_set_tls_native_handle(p->conn, p->ossl_ctx);
    return 0;
}

/* Server: create the ngtcp2_conn the first time an Initial packet shows up. */
static int create_server_conn_from_initial(AVTProtocolCtx *p,
                                           const uint8_t *pkt, size_t pktlen,
                                           const ngtcp2_path *path)
{
    ngtcp2_version_cid vc;
    int r = ngtcp2_pkt_decode_version_cid(&vc, pkt, pktlen, NGTCP2_MAX_CIDLEN);
    if (r < 0)
        return log_ngtcp2_err(p, "ngtcp2_pkt_decode_version_cid failed", r);

    ngtcp2_pkt_hd hd;
    if (ngtcp2_accept(&hd, pkt, pktlen) < 0)
        return AVT_ERROR(EAGAIN);

    ngtcp2_settings settings;
    ngtcp2_settings_default(&settings);
    settings.initial_ts = quic_now();

    ngtcp2_transport_params params;
    ngtcp2_transport_params_default(&params);
    params.initial_max_streams_bidi = 64;
    params.initial_max_streams_uni  = 64;
    params.initial_max_stream_data_bidi_local  = 1 << 20;
    params.initial_max_stream_data_bidi_remote = 1 << 20;
    params.initial_max_stream_data_uni         = 1 << 20;
    params.initial_max_data                    = 16 << 20;
    params.max_idle_timeout = 30 * NGTCP2_SECONDS;
    params.max_datagram_frame_size = 65535;
    params.active_connection_id_limit = 4;
    params.original_dcid_present = 1;
    params.original_dcid = hd.dcid;

    ngtcp2_callbacks cbs;
    fill_callbacks(&cbs, false);

    /* Local SCID: fresh random. The peer's SCID becomes our DCID. */
    ngtcp2_cid scid = { 0 };
    scid.datalen = 8;
    cb_rand(scid.data, scid.datalen, NULL);

    r = ngtcp2_conn_server_new(&p->conn, &hd.scid, &scid, path,
                               hd.version, &cbs,
                               &settings, &params, NULL, p);
    if (r < 0)
        return log_ngtcp2_err(p, "ngtcp2_conn_server_new failed", r);

    ngtcp2_conn_set_tls_native_handle(p->conn, p->ossl_ctx);
    return 0;
}

/* I/O pump ----------------------------------------------------------------- */

/* Send a single UDP packet built by ngtcp2 out through our socket. Uses
 * sendto() so it works on both connected (client) and unconnected (server)
 * sockets. */
static int udp_send(AVTProtocolCtx *p, const uint8_t *buf, size_t len)
{
    struct sockaddr *dst = (struct sockaddr *)&p->remote_addr;
    socklen_t dstlen = p->remote_addr_len;
    for (;;) {
        ssize_t r = sendto(p->fd, buf, len, MSG_DONTWAIT, dst, dstlen);
        if (r >= 0)
            return 0;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return AVT_ERROR(EAGAIN);
        return AVT_ERROR(EIO);
    }
}

/* Drain every UDP datagram the kernel has queued. Returns 0 if at least one
 * packet was processed, EAGAIN if the socket is empty, or a negative error. */
static int recv_pkts(AVTProtocolCtx *p)
{
    int got = 0;
    uint8_t buf[QUIC_UDP_BUF_SIZE];
    struct sockaddr_in6 src;
    for (;;) {
        socklen_t srclen = sizeof(src);
        ssize_t r = recvfrom(p->fd, buf, sizeof(buf), MSG_DONTWAIT,
                             (struct sockaddr *)&src, &srclen);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return got ? 0 : AVT_ERROR(EAGAIN);
            return AVT_ERROR(EIO);
        }

        /* For the server: remember the peer address so writes go back to it,
         * and use it as the path's remote in the read_pkt call. The client
         * has a connect()ed socket, so its remote is already correct. */
        if (p->listen) {
            memcpy(&p->remote_addr, &src, srclen);
            p->remote_addr_len = srclen;
        }

        ngtcp2_path path;
        path_set(&path, &p->local_addr, p->local_addr_len,
                        &p->remote_addr, p->remote_addr_len);

        if (!p->conn) {
            int rr = create_server_conn_from_initial(p, buf, r, &path);
            if (rr < 0)
                return rr;
        }

        ngtcp2_pkt_info pi = { 0 };
        int rr = ngtcp2_conn_read_pkt(p->conn, &path, &pi, buf, r, quic_now());
        if (rr == NGTCP2_ERR_DRAINING || rr == NGTCP2_ERR_CLOSING) {
            p->closing = true;
            return 0;
        }
        if (rr == NGTCP2_ERR_DROP_CONN) {
            p->closing = true;
            return AVT_ERROR(EIO);
        }
        if (rr < 0)
            return log_ngtcp2_err(p, "ngtcp2_conn_read_pkt", rr);
        got++;
    }
}

/* Drain outgoing data into UDP packets. Generates as many UDP packets as
 * ngtcp2 will let us, sending each via sendto. Returns 0 once the connection
 * has nothing more to write right now. */
static int send_pkts(AVTProtocolCtx *p)
{
    if (!p->conn || p->closing)
        return 0;

    /* Give any tx stream that buffered data before it had a QUIC stream a
     * chance to open one now. */
    open_pending_tx_streams(p);

    uint8_t buf[QUIC_UDP_BUF_SIZE];
    ngtcp2_path_storage ps;
    ngtcp2_path_storage_zero(&ps);

    for (;;) {
        ngtcp2_ssize n = -1;
        ngtcp2_pkt_info pi = { 0 };
        ngtcp2_tstamp ts = quic_now();

        /* Prefer flushing a queued datagram. ngtcp2 may refuse if the peer
         * hasn't advertised max_datagram_frame_size yet (handshake in
         * progress), so we silently keep the datagram queued in that case. */
        if (p->nb_tx_dgrams > 0 && ngtcp2_conn_get_handshake_completed(p->conn)) {
            int accepted = 0;
            ngtcp2_vec dvec = {
                .base = p->tx_dgrams[0].data,
                .len  = p->tx_dgrams[0].len,
            };
            ngtcp2_ssize wn = ngtcp2_conn_writev_datagram(
                p->conn, &ps.path, &pi, buf, sizeof(buf), &accepted,
                NGTCP2_WRITE_DATAGRAM_FLAG_NONE, 0, &dvec, 1, ts);
            if (wn == NGTCP2_ERR_INVALID_ARGUMENT ||
                wn == NGTCP2_ERR_INVALID_STATE) {
                /* Peer can't accept this datagram (too big or DATAGRAM
                 * support not negotiated). Drop it - unreliable means we
                 * never resend, and the alternative is stalling forever. */
                avt_log(p, AVT_LOG_WARN,
                        "Dropping datagram (%zu bytes): %s\n",
                        p->tx_dgrams[0].len, ngtcp2_strerror((int)wn));
                dgram_queue_pop_head(p->tx_dgrams, &p->nb_tx_dgrams);
                continue;
            }
            if (wn < 0)
                return log_ngtcp2_err(p, "ngtcp2_conn_writev_datagram", (int)wn);
            if (accepted)
                dgram_queue_pop_head(p->tx_dgrams, &p->nb_tx_dgrams);
            n = wn;
        }

        if (n < 0) {
            /* Try each stream with pending bytes, starting from the round-robin
             * cursor and skipping any that are currently flow-control blocked.
             * Skipping rather than falling straight back to ACK-only means one
             * blocked stream cannot starve the others (no cross-stream
             * head-of-line blocking), and rotating the cursor keeps service
             * fair when several streams always have data. */
            bool wrote = false;
            const int ns = p->nb_tx_streams;
            for (int k = 0; k < ns; k++) {
                int i = (p->tx_rr + k) % ns;
                AVTQuicStream *s = p->tx_streams[i];
                const uint8_t *chunk_data = NULL;
                size_t chunk_len = 0;
                if (s->qid < 0 || !stream_tx_peek(s, &chunk_data, &chunk_len))
                    continue;

                ngtcp2_vec vec = {
                    .base = (uint8_t *)chunk_data,
                    .len  = chunk_len,
                };
                ngtcp2_ssize datalen = 0;
                ngtcp2_ssize wn = ngtcp2_conn_writev_stream(
                    p->conn, &ps.path, &pi, buf, sizeof(buf), &datalen,
                    NGTCP2_WRITE_STREAM_FLAG_NONE, s->qid, &vec, 1, ts);
                if (wn == NGTCP2_ERR_STREAM_DATA_BLOCKED ||
                    wn == NGTCP2_ERR_STREAM_SHUT_WR ||
                    wn == NGTCP2_ERR_STREAM_NOT_FOUND)
                    continue; /* this stream can't send now; try the next */
                if (wn < 0)
                    return log_ngtcp2_err(p, "ngtcp2_conn_writev_stream", (int)wn);
                if (datalen > 0)
                    stream_tx_consume(s, (size_t)datalen);
                p->tx_rr = (i + 1) % ns;
                n = wn;
                wrote = true;
                break;
            }
            if (!wrote) {
                /* Nothing sendable on any stream (none pending, or all blocked):
                 * flush pending ACKs / control frames. */
                n = ngtcp2_conn_writev_stream(
                    p->conn, &ps.path, &pi, buf, sizeof(buf), NULL,
                    NGTCP2_WRITE_STREAM_FLAG_NONE, -1, NULL, 0, ts);
                if (n < 0)
                    return log_ngtcp2_err(p, "ngtcp2_conn_writev_stream(flush)",
                                          (int)n);
            }
        }

        if (n == 0)
            return 0;

        int sret = udp_send(p, buf, n);
        if (sret == AVT_ERROR(EAGAIN))
            return 0; /* socket buffer full, try again later */
        if (sret < 0)
            return sret;
    }
}

/* Drive any expired ngtcp2 timers (loss detection, idle, etc). Safe to call
 * whenever; cheap when nothing has expired. */
static void handle_expiry(AVTProtocolCtx *p)
{
    if (!p->conn)
        return;
    if (ngtcp2_conn_get_expiry(p->conn) <= quic_now())
        ngtcp2_conn_handle_expiry(p->conn, quic_now());
}

/* Handshake -------------------------------------------------------------- */

static int quic_handshake_step(AVTProtocolCtx *p)
{
    if (p->established)
        return 0;
    handle_expiry(p);
    int r = recv_pkts(p);
    if (r < 0 && r != AVT_ERROR(EAGAIN))
        return r;
    r = send_pkts(p);
    if (r < 0)
        return r;

    if (p->conn && ngtcp2_conn_get_handshake_completed(p->conn))
        p->established = true;

    return p->established ? 0 : AVT_ERROR(EAGAIN);
}

int avt_quic_handshake(AVTProtocolCtx *p, int64_t timeout)
{
    int64_t now = avt_get_time_ns();
    int64_t deadline = (timeout > 0 && timeout < (INT64_MAX - now))
                       ? now + timeout : INT64_MAX;
    for (;;) {
        int r = quic_handshake_step(p);
        if (r != AVT_ERROR(EAGAIN))
            return r;
        if (timeout <= 0 || avt_get_time_ns() >= deadline)
            return AVT_ERROR(EAGAIN);
        nanosleep(&(struct timespec){ .tv_nsec = 500 * 1000 }, NULL);
    }
}

/* Stream lookup / open --------------------------------------------------- */

/* Get (or create) a tx stream for a given AVT stream_id. The QUIC stream is
 * opened the first time it is needed (after the handshake completes - earlier
 * calls return NULL and let the caller buffer). is_control selects the
 * shared control stream rather than a per-AVT-stream stream. */
static AVTQuicStream *get_or_open_stream(AVTProtocolCtx *p, uint16_t avt_id,
                                         bool is_control)
{
    AVTQuicStream *s;
    if (is_control)
        s = p->control_stream;
    else
        s = find_tx_stream(p, avt_id);

    if (!s) {
        s = stream_new(is_control ? 0 : avt_id, is_control);
        if (!s)
            return NULL;
        if (table_push(&p->tx_streams, &p->nb_tx_streams, s) < 0) {
            stream_free(s);
            return NULL;
        }
        if (is_control)
            p->control_stream = s;
    }

    if (s->qid < 0 && p->established && p->conn) {
        int64_t qid = -1;
        if (ngtcp2_conn_open_bidi_stream(p->conn, &qid, s) == 0)
            s->qid = qid;
        /* On failure (peer's stream limit reached) leave qid = -1. The caller
         * still buffers the packet into the stream's tx queue, and
         * open_pending_tx_streams() opens the QUIC stream and flushes the
         * backlog once capacity frees up. Never drop the data. */
    }

    return s;
}

/* Open QUIC streams for any tx stream still waiting for one. Run once the
 * handshake is up. A failure means the peer's stream limit is currently
 * reached, so we leave the data buffered and retry on the next pass. */
static void open_pending_tx_streams(AVTProtocolCtx *p)
{
    if (!p->conn || !p->established)
        return;
    for (int i = 0; i < p->nb_tx_streams; i++) {
        AVTQuicStream *s = p->tx_streams[i];
        if (s->qid >= 0)
            continue;
        int64_t qid = -1;
        if (ngtcp2_conn_open_bidi_stream(p->conn, &qid, s) != 0)
            break; /* at the stream limit; try again next time */
        s->qid = qid;
    }
}

/* Serialize one AVT packet to its target carrier (stream tx buffer or
 * datagram queue). The classification is purely descriptor-driven. */
static int route_packet(AVTProtocolCtx *p, AVTPktd *pkt)
{
    size_t pl_len = 0;
    uint8_t *pl = avt_buffer_get_data(&pkt->pl, &pl_len);
    if (!pl)
        pl_len = 0;

    uint16_t desc = (uint16_t)pkt->pkt.desc;

    if (desc_is_unreliable(desc)) {
        /* Spec: data packets ride RFC 9221 datagrams. */
        return dgram_queue_push(&p->tx_dgrams, &p->nb_tx_dgrams,
                                &p->nb_alloc_tx_dgrams,
                                pkt->hdr, pkt->hdr_len, pl, pl_len);
    }

    uint16_t avt_id = 0;
    bool is_ctrl = !packet_has_stream_id(desc, pkt, &avt_id);
    AVTQuicStream *s = get_or_open_stream(p, avt_id, is_ctrl);
    if (!s)
        return AVT_ERROR(ENOMEM); /* only fails on OOM now; never silently drop */

    /* The bytes are buffered into the stream's tx queue even if the QUIC
     * stream has not been opened yet (qid < 0); open_pending_tx_streams()
     * flushes the backlog once the stream is opened. */
    int r = stream_tx_append(s, pkt->hdr, pkt->hdr_len);
    if (r < 0)
        return r;
    if (pl_len)
        r = stream_tx_append(s, pl, pl_len);
    return r;
}

/* Packet reassembly ------------------------------------------------------ */

/* Advance the receive flow-control window for stream s. Grants the peer at
 * least QUIC_STREAM_FC_WINDOW bytes past what the application has consumed,
 * and at least up to `needed_abs` absolute stream bytes so a packet larger
 * than the base window can still be received without deadlocking. The same
 * delta is mirrored onto the connection window, since every stream byte also
 * counts against it. */
static void fc_grant(AVTProtocolCtx *p, AVTQuicStream *s, uint64_t needed_abs)
{
    if (s->qid < 0)
        return;
    uint64_t want = s->fc_consumed + QUIC_STREAM_FC_WINDOW;
    if (needed_abs > want)
        want = needed_abs;
    if (want <= s->fc_max)
        return;
    uint64_t d = want - s->fc_max;
    s->fc_max = want;
    ngtcp2_conn_extend_max_stream_offset(p->conn, s->qid, d);
    ngtcp2_conn_extend_max_offset(p->conn, d);
}

/* Pull as many AVT packets as possible from the head of a stream's reassembly
 * buffer into the fifo. Stops when fewer than one full packet's worth of
 * bytes are left. */
static int extract_from_stream(AVTProtocolCtx *p, AVTQuicStream *s,
                               AVTPacketFifo *fifo)
{
    while (s->rx_len >= AVT_MIN_HEADER_LEN) {
        AVTPktd probe = { 0 };
        memcpy(probe.hdr, s->rx_buf, AVT_MIN_HEADER_LEN);
        avt_ldpc_decode_288_224(probe.hdr, p->opts.ldpc_iterations);

        uint16_t desc = AVT_RB16(&probe.hdr[0]);
        if (((desc & 0xFF00) == (AVT_PKT_TIME_SYNC & 0xFF00)) ||
            ((desc & 0xFF00) == (AVT_PKT_STREAM_DATA & 0xFF00)))
            desc &= 0xFF00;

        size_t hdr_size = avt_pkt_hdr_size(desc);
        if (hdr_size < AVT_MIN_HEADER_LEN || hdr_size > sizeof(probe.hdr)) {
            avt_log(p, AVT_LOG_ERROR,
                    "Invalid stream packet header size %zu\n", hdr_size);
            return AVT_ERROR(EINVAL);
        }
        if (s->rx_len < hdr_size)
            return 0;

        int left = (int)hdr_size - AVT_MIN_HEADER_LEN;
        if (left > 0) {
            memcpy(probe.hdr + AVT_MIN_HEADER_LEN,
                   s->rx_buf + AVT_MIN_HEADER_LEN, left);
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
            avt_log(p, AVT_LOG_ERROR, "Unknown descriptor 0x%x on stream\n", desc);
            return dret;
        }

        size_t total = hdr_size + pl_bytes;
        if (s->rx_len < total) {
            /* Incomplete: make sure the peer is allowed to send the rest of
             * this packet even if it is larger than the base window. */
            fc_grant(p, s, s->fc_consumed + total);
            return 0;
        }

        AVTPktd *out = avt_pkt_fifo_push_new(fifo, NULL, 0, AVT_BUFFER_REF_ALL);
        if (!out)
            return AVT_ERROR(ENOMEM);
        memcpy(out->hdr, probe.hdr, hdr_size);
        out->hdr_len = hdr_size;
        out->pkt = probe.pkt;
        if (pl_bytes) {
            if (!avt_buffer_quick_alloc(&out->pl, pl_bytes))
                return AVT_ERROR(ENOMEM);
            memcpy(out->pl.data, s->rx_buf + hdr_size, pl_bytes);
        }

        memmove(s->rx_buf, s->rx_buf + total, s->rx_len - total);
        s->rx_len -= total;

        /* Consumed a whole packet: advance the window past it (backpressure). */
        s->fc_consumed += total;
        fc_grant(p, s, 0);
    }
    return 0;
}

/* Decode a single received QUIC datagram into one AVTPktd. Each datagram
 * carries exactly one AVT packet (per the spec; no fragmentation), so this
 * is essentially extract_from_stream() with the boundaries known a priori. */
static int extract_from_datagram(AVTProtocolCtx *p, const uint8_t *buf, size_t len,
                                 AVTPacketFifo *fifo)
{
    if (len < AVT_MIN_HEADER_LEN) {
        avt_log(p, AVT_LOG_ERROR,
                "QUIC datagram too short for an AVT packet: %zu\n", len);
        return AVT_ERROR(EINVAL);
    }

    AVTPktd probe = { 0 };
    memcpy(probe.hdr, buf, AVT_MIN_HEADER_LEN);
    avt_ldpc_decode_288_224(probe.hdr, p->opts.ldpc_iterations);

    uint16_t desc = AVT_RB16(&probe.hdr[0]);
    if (((desc & 0xFF00) == (AVT_PKT_TIME_SYNC & 0xFF00)) ||
        ((desc & 0xFF00) == (AVT_PKT_STREAM_DATA & 0xFF00)))
        desc &= 0xFF00;

    size_t hdr_size = avt_pkt_hdr_size(desc);
    if (hdr_size < AVT_MIN_HEADER_LEN || hdr_size > sizeof(probe.hdr) ||
        len < hdr_size) {
        avt_log(p, AVT_LOG_ERROR,
                "Bad datagram header (desc 0x%x size %zu / len %zu)\n",
                desc, hdr_size, len);
        return AVT_ERROR(EINVAL);
    }

    int left = (int)hdr_size - AVT_MIN_HEADER_LEN;
    if (left > 0) {
        memcpy(probe.hdr + AVT_MIN_HEADER_LEN, buf + AVT_MIN_HEADER_LEN, left);
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
        avt_log(p, AVT_LOG_ERROR, "Unknown descriptor 0x%x in datagram\n", desc);
        return dret;
    }
    if (hdr_size + pl_bytes != len) {
        avt_log(p, AVT_LOG_WARN,
                "Datagram size mismatch: header says %zu+%zu, got %zu\n",
                hdr_size, pl_bytes, len);
        /* Trust the header; truncated payload would have failed decode. */
    }

    AVTPktd *out = avt_pkt_fifo_push_new(fifo, NULL, 0, AVT_BUFFER_REF_ALL);
    if (!out)
        return AVT_ERROR(ENOMEM);
    memcpy(out->hdr, probe.hdr, hdr_size);
    out->hdr_len = hdr_size;
    out->pkt = probe.pkt;
    if (pl_bytes) {
        if (!avt_buffer_quick_alloc(&out->pl, pl_bytes))
            return AVT_ERROR(ENOMEM);
        memcpy(out->pl.data, buf + hdr_size, pl_bytes);
    }
    return 0;
}

static int drain_into_fifo(AVTProtocolCtx *p, AVTPacketFifo *fifo)
{
    int produced = 0;
    for (int i = 0; i < p->nb_rx_streams; i++) {
        uint32_t before = fifo->nb;
        int r = extract_from_stream(p, p->rx_streams[i], fifo);
        if (r < 0)
            return r;
        produced += (int)(fifo->nb - before);
    }
    while (p->nb_rx_dgrams > 0) {
        AVTQuicDgram d = p->rx_dgrams[0];
        memmove(&p->rx_dgrams[0], &p->rx_dgrams[1],
                sizeof(*p->rx_dgrams) * (p->nb_rx_dgrams - 1));
        p->nb_rx_dgrams--;
        int r = extract_from_datagram(p, d.data, d.len, fifo);
        free(d.data);
        if (r < 0)
            return r;
        produced++;
    }
    return produced;
}

/* Protocol API ------------------------------------------------------------ */

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

static int quic_proto_max_pkt_len(AVTProtocolCtx *p, size_t *mtu)
{
    size_t tmp = 0;
    int r = p->io->get_max_pkt_len(p->io_ctx, &tmp);
    if (r < 0)
        return r;
    /* Pessimistic QUIC framing overhead estimate. */
    const size_t udp_hdr = 8;
    const size_t quic_hdr = 64;
    if (tmp <= udp_hdr + quic_hdr)
        *mtu = 0;
    else
        *mtu = tmp - udp_hdr - quic_hdr;
    return 0;
}

static int quic_proto_send_packet(AVTProtocolCtx *p, AVTPktd *pkt,
                                  int64_t timeout)
{
    int r = quic_handshake_step(p);
    if (r < 0 && r != AVT_ERROR(EAGAIN))
        return r;

    r = route_packet(p, pkt);
    if (r < 0 && r != AVT_ERROR(EAGAIN))
        return r;

    int64_t now = avt_get_time_ns();
    int64_t deadline = (timeout > 0 && timeout < (INT64_MAX - now))
                       ? now + timeout : now;
    for (;;) {
        handle_expiry(p);
        int sr = send_pkts(p);
        if (sr < 0)
            return sr;
        int rr = recv_pkts(p);
        if (rr < 0 && rr != AVT_ERROR(EAGAIN))
            return rr;
        if (p->conn && ngtcp2_conn_get_handshake_completed(p->conn))
            p->established = true;
        if (avt_get_time_ns() >= deadline)
            break;
        nanosleep(&(struct timespec){ .tv_nsec = 200 * 1000 }, NULL);
    }
    return 0;
}

static int quic_proto_send_seq(AVTProtocolCtx *p, AVTPacketFifo *seq,
                               int64_t timeout)
{
    int r = quic_handshake_step(p);
    if (r < 0 && r != AVT_ERROR(EAGAIN))
        return r;
    for (uint32_t i = 0; i < seq->nb; i++) {
        int rr = route_packet(p, &seq->data[i]);
        if (rr < 0 && rr != AVT_ERROR(EAGAIN))
            return rr;
    }
    int64_t now = avt_get_time_ns();
    int64_t deadline = (timeout > 0 && timeout < (INT64_MAX - now))
                       ? now + timeout : now;
    for (;;) {
        handle_expiry(p);
        int sr = send_pkts(p);
        if (sr < 0)
            return sr;
        int rr = recv_pkts(p);
        if (rr < 0 && rr != AVT_ERROR(EAGAIN))
            return rr;
        if (avt_get_time_ns() >= deadline)
            break;
        nanosleep(&(struct timespec){ .tv_nsec = 200 * 1000 }, NULL);
    }
    return 0;
}

static int quic_proto_receive(AVTProtocolCtx *p, AVTPacketFifo *fifo,
                              int64_t timeout)
{
    if (!p->established) {
        int r = avt_quic_handshake(p, timeout);
        if (r < 0)
            return r;
    }

    handle_expiry(p);
    int rr = recv_pkts(p);
    if (rr < 0 && rr != AVT_ERROR(EAGAIN))
        return rr;
    int sr = send_pkts(p);
    if (sr < 0)
        return sr;

    int prod = drain_into_fifo(p, fifo);
    if (prod < 0)
        return prod;
    if (prod == 0)
        return AVT_ERROR(EAGAIN);
    return 0;
}

static int quic_proto_flush(AVTProtocolCtx *p, int64_t timeout)
{
    if (!p->established || !p->conn)
        return 0;

    int64_t now = avt_get_time_ns();
    int64_t deadline = (timeout > 0 && timeout < (INT64_MAX - now))
                       ? now + timeout : now;
    for (;;) {
        handle_expiry(p);
        int sr = send_pkts(p);
        if (sr < 0)
            return sr;
        int rr = recv_pkts(p);
        if (rr < 0 && rr != AVT_ERROR(EAGAIN))
            return rr;

        /* Busy while any datagram is queued or any stream still has unsent or
         * unacknowledged bytes. nb_chunks only drops once the peer acks, so
         * this also covers streams whose QUIC stream is still being opened
         * (data buffered, qid < 0): send_pkts opens them via
         * open_pending_tx_streams and the backlog drains. */
        bool busy = p->nb_tx_dgrams > 0;
        for (int i = 0; !busy && i < p->nb_tx_streams; i++) {
            if (p->tx_streams[i]->nb_chunks > 0)
                busy = true;
        }
        if (!busy)
            return 0;
        if (avt_get_time_ns() >= deadline)
            return 0;
        nanosleep(&(struct timespec){ .tv_nsec = 500 * 1000 }, NULL);
    }
}

static int COLD quic_proto_close(AVTProtocolCtx **_p)
{
    AVTProtocolCtx *p = *_p;
    if (!p)
        return 0;

    if (p->conn && !p->closing) {
        uint8_t buf[QUIC_UDP_BUF_SIZE];
        ngtcp2_path_storage ps;
        ngtcp2_path_storage_zero(&ps);
        ngtcp2_ccerr ccerr;
        ngtcp2_ccerr_default(&ccerr);
        ngtcp2_pkt_info pi = { 0 };
        ngtcp2_ssize n = ngtcp2_conn_write_connection_close(
            p->conn, &ps.path, &pi, buf, sizeof(buf), &ccerr, quic_now());
        if (n > 0 && p->fd >= 0)
            udp_send(p, buf, n);
    }

    if (p->ossl_ctx) {
        ngtcp2_crypto_ossl_ctx_del(p->ossl_ctx);
        p->ossl_ctx = NULL;
    }
    if (p->ssl) {
        SSL_set_app_data(p->ssl, NULL);
        SSL_free(p->ssl);
        p->ssl = NULL;
    }
    if (p->conn) {
        ngtcp2_conn_del(p->conn);
        p->conn = NULL;
    }
    if (p->ssl_ctx)
        SSL_CTX_free(p->ssl_ctx);

    for (int i = 0; i < p->nb_tx_streams; i++)
        stream_free(p->tx_streams[i]);
    free(p->tx_streams);
    for (int i = 0; i < p->nb_rx_streams; i++)
        stream_free(p->rx_streams[i]);
    free(p->rx_streams);

    for (int i = 0; i < p->nb_tx_dgrams; i++)
        free(p->tx_dgrams[i].data);
    free(p->tx_dgrams);
    for (int i = 0; i < p->nb_rx_dgrams; i++)
        free(p->rx_dgrams[i].data);
    free(p->rx_dgrams);

    free(p);
    *_p = NULL;
    return 0;
}

static COLD int quic_proto_init(AVTContext *ctx, AVTProtocolCtx **_p, AVTAddress *addr,
                                const AVTIO *io, AVTIOCtx *io_ctx,
                                AVTProtocolOpts *opts)
{
    int ret;

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
    p->fd = fd;
    p->listen = addr->listen;
    if (opts)
        p->opts = *opts;
    memcpy(p->hostname, addr->host, sizeof(p->hostname));
    p->hostname[sizeof(p->hostname) - 1] = '\0';

    /* Snapshot local + remote sockaddrs. The local is read from the bound
     * socket so it includes the ephemeral port the kernel chose. */
    p->local_addr_len = sizeof(p->local_addr);
    if (getsockname(fd, (struct sockaddr *)&p->local_addr,
                    &p->local_addr_len) < 0) {
        avt_log(p, AVT_LOG_ERROR, "getsockname failed: %s\n", strerror(errno));
        ret = AVT_ERROR(EIO);
        goto fail;
    }

    /* Match io_socket_common.c's convention: addr->port goes into sin6_port
     * raw (it does NOT call htons), so the kernel reads a different actual
     * port than the nominal one; for the client this matches the connect()ed
     * peer, and for the server, we only ever overwrite this from recvmsg. */
    p->remote_addr.sin6_family = AF_INET6;
    p->remote_addr.sin6_port   = addr->port;
    memcpy(&p->remote_addr.sin6_addr, addr->ip, 16);
    p->remote_addr_len = sizeof(p->remote_addr);

    if (ngtcp2_crypto_ossl_init() != 0) {
        avt_log(p, AVT_LOG_ERROR, "ngtcp2_crypto_ossl_init failed\n");
        ret = AVT_ERROR(EIO);
        goto fail;
    }

    ret = build_ssl_ctx(p, addr);
    if (ret < 0)
        goto fail;
    ret = build_ssl(p);
    if (ret < 0)
        goto fail;

    if (!p->listen) {
        ret = create_client_conn(p);
        if (ret < 0)
            goto fail;
    }

    *_p = p;
    return 0;

fail:
    quic_proto_close(&p);
    return ret;
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
