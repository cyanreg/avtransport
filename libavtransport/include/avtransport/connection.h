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

#ifndef AVTRANSPORT_CONNECTION_H
#define AVTRANSPORT_CONNECTION_H

#include <avtransport/packet_enums.h>
#include <avtransport/packet_data.h>
#include <avtransport/utils.h>

typedef struct AVTConnection AVTConnection;

/* Connection type */
enum AVTConnectionType {
    /* Null connection. Transmits nothing. Receives only session start packets. */
    AVT_CONNECTION_NULL = 0,

    /* URL in the form of:
     *
     * <scheme>://[<transport>[:<mode>]@]<address>[:<port>][/[<uuid>][#<param1>=<val1>[&<param2>=<val2>]]]
     *
     * - <scheme> may be "avt", "udp", "udplite", "quic", "socket", "file".
     *   <transport> and <mode> may only be present for "avt".
     *   <uuid>, <param1> are only for "avt", "udp", "udplite", "quic", "socket".
     *   Using "avt" is strongly recommended.
     *
     * - <transport> may be either missing (default: UDP),
     *   or: "udp", "udplite", "quic", or "file".
     *
     * - <mode> is an optional setting which has different meanings for
     *   senders and receivers, and transports.
     *
     *   For senders, the "default" value, "active" means to start continuously sending
     *   packets to the given address. "passive" means to wait for receivers to
     *   send packets on the given address to begin transmitting back to them.
     *
     *   For receivers, the "default" value, "passive" means to listen on the given
     *   address for any packets received. "active" for receivers means that receivers
     *   will actively connect and try to request a stream from the target address.
     *
     * - <address> of the remote host, or local host, or multicast group
     *   May be suffixed with %<interface> (for IPv6, this must be in the brackets)
     *   to indicate an interface to attempt to bind to.
     *   May be a hostname.
     *
     * - <port> on which to listen on/transmit to
     *
     * - <uuid> a unique identifier of the stream
     *
     * - <param1>=<val1> are a key=value pair of settings for the connection,
     *   separated by `&` signs. Currently accepted values are:
     *     - t=<seconds> to immediately seek upon playback to the given timestamp
     *     - default=<stream_id>[,<stream_id>] of the stream(s) to present as default
     *       (overriding those signalled by the sender)
     *     - rx_buf: receive buffer size
     *     - tx_buf: send buffer size
     *     - cert: certificate file path for QUIC
     *     - key: key file path for QUIC
     *
     * Examples:
     *     - "avt://192.168.1.1"
     *     - "avt://quic@[2001:db8::2]:9999"
     *     - "avt://udp:active@[2001:db8::2]:9999"
     *     - "quic://[2001:db8::1]:9999"
     *     - "udp://192.168.1.2:9999"
     *     - "udp://192.168.1.5/#rx_buf=65536"
     */
    AVT_CONNECTION_URL,

    /* File path */
    AVT_CONNECTION_FILE,

    /* Bound socket */
    AVT_CONNECTION_SOCKET,

    /* File descriptor */
    AVT_CONNECTION_FD,

    /* UNIX domain socket */
    AVT_CONNECTION_UNIX,

    /* Raw byte-level reader/writer using a callback. */
    AVT_CONNECTION_DATA,

    /* Deserialized packet-level input/output via a callback */
    AVT_CONNECTION_PACKET,
};

enum AVTProtocolType {
    AVT_PROTOCOL_UDP = 1,
    AVT_PROTOCOL_UDP_LITE,
    AVT_PROTOCOL_QUIC,
};

enum AVTProtocolMode {
    AVT_MODE_DEFAULT,
    AVT_MODE_PASSIVE,
    AVT_MODE_ACTIVE,
};

typedef struct AVTConnectionInfo {
    /* Connection type */
    enum AVTConnectionType type;

    /* Connection interface */
    union {
        /* AVT_CONNECTION_FILE: file path */
        const char *path;

        /* AVT_CONNECTION_URL: url using the syntax described above */
        struct {
            const char *url;
            /* Whether to listen on, or transmit to the address. */
            bool listen;
        } url;

        /* AVT_CONNECTION_FD: regular seekable file descriptor
         * AVT_CONNECTION_UNIX: UNIX domain SOCK_STREAM socket
         * NOTE: dup()-licated on success, users can close() this freely */
        int fd;

        /* AVT_CONNECTION_SOCKET: opened and bound/connected network socket */
        struct {
            /* Bound socket
             * NOTE: dup()'d on success, users can close() this freely */
            int socket;
            /* Sending: destination IP (IPv4 must be always mapped in IPv6) */
            uint8_t dst[16];
            /* Connection port */
            uint16_t port;
            /* Sending: use the last received address instead of dst */
            bool use_receiver_addr;
            /* Which protocol to use */
            enum AVTProtocolType protocol;
            /* Behaviour of the protocol */
            enum AVTProtocolMode mode;
            /* Certificate/key file path for QUIC */
            const char *cert;
            const char *key;
        } socket;

        /* AVT_CONNECTION_DATA: the following structure */
        struct {
            /* Called by libavtransport in strictly sequential order,
             * with no holes, to write data. Returns the offset after
             * writing, or a negative error. */
            int64_t (*write)(void *opaque,
                             uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                             AVTBuffer *payload);

            /* Called by libavtransport to retrieve a piece of
             * data with a particular offset. Returns the offset after
             * reading, or a negative error.
             * The buffer `data` is set to a buffer with the data requested.
             * Ownership of the buffer is not transferred. */
            int64_t (*read)(void *opaque, AVTBuffer **data,
                            size_t len, int64_t offset);

            /* Opaque pointer which will be used for the data() callback. */
            void *opaque;
        } cb;

        /* AVT_CONNECTION_PACKET: the following structure */
        struct {
            /* Called by libavtransport in output order
             * (pkt.global_seq may not increase monotonically) */
            int (*out)(void *opaque,
                       union AVTPacketData pkt, AVTBuffer *buf);

            /* Called by libavtransport to retrieve a piece of
             * data with a particular sequence number. */
            int (*in)(void *opaque,
                      union AVTPacketData *pkt, AVTBuffer **buf,
                      uint64_t seq);

            /* Opaque pointer which will be used for the data() callback. */
            void *opaque;
        } pkt;
    };

    /* Input options */
    struct {
        /* Buffer size limit. Zero means automatic. Approximate/best effort. */
        size_t buffer;

        /* Controls the number of iterations for LDPC decoding. Zero means
         * automatic (very rarely), higher values increase overhead
         * and decrease potential errors, negative values disables decoding. */
        int ldpc_iterations;
    } input_opts;

    struct {
        /* Buffer size limit. Adjusts retransmission capabilites for output.
         * Zero means automatic. Approximate/best effort. */
        size_t buffer;

        /* Available sender or receiver bandwidth, in bits per second.
         *
         * This setting controls the packet scheduler, which ensures that
         * during playback, no stream starves another stream of data
         * due to limited bandwidth.
         *
         * This also serves as a bandwidth limit. Setting this lower than
         * the total bitrate of all streams will result in the buffer
         * filling up and new packets being rejected during calls
         * to avt_output*().
         *
         * If left at zero, this takes a default value to:
         *  - For streams: total bandwidth during the last 10 seconds
         *    of packet duration, plus 10%.
         *  - For files: maximum timestamp difference between packets
         *    from all streams is limited to 500ms.
         *
         * If set to INT64_MAX, all interleaving is disabled, which
         * may improve latency, at the risk of stream bitrate starvation.
         */
        int64_t bandwidth;

        /* Frequency for session start packets, required to identify a stream
         * as AVTransport. Minimum value. libavtransport may add additional.
         *  - 0: Default. For the stream with the longest keyframe period,
         *       send one on each keyframe.
         *  - N: In addition, send one every N packets (segmentation is ignored).
         */
        unsigned int session_start_freq;
    } output_opts;

    /* When greater than 0, enables asynchronous mode.
     * Values greater than 1 are reserved. */
    int async;
} AVTConnectionInfo;

/**
 * Initialize a connection. If this function returns 0 or greater,
 * the connection set in *conn will be initialized to a connection context,
 * which may be passed onto other functions.
 */
AVT_API int avt_connection_create(AVTContext *ctx, AVTConnection **conn,
                                  AVTConnectionInfo *info);

/**
 * Processes received packets and transmits scheduled packets.
 *
 * timeout is a value in nanoseconds to wait for the operation to
 * complete.
 *
 * Recommended operation is to always use a timeout of 0 and check
 * the return value. Errors may be delayed, but the function will
 * not block for I/O.
 */
AVT_API int avt_connection_process(AVTConnection *conn, int64_t timeout);

/**
 * Creates an AVTransport stream mirror.
 *
 * The resulting stream contains every single packet going in or out,
 * allowing for exact backing up and preservation of ephemeral streams,
 * as well as debugging, and more expensive offline FEC.
 *
 * When the URL specified is a file, it will be used for caching and retransmit
 * cache, reducing memory usage and allowing infinite retransmission.
 *
 * NOTE: avt_connection_flush() should be called before calling
 *       avt_connection_mirror_close().
 */
AVT_API int avt_connection_mirror_open(AVTContext *ctx, AVTConnection *conn,
                                       AVTConnectionInfo *info);
AVT_API int avt_connection_mirror_close(AVTContext *ctx, AVTConnection *conn);

enum AVTConnectionStatusFlags {
    AVT_CONN_STATE_MTU,
    AVT_CONN_STATE_ERR,
    AVT_CONN_STATE_RX_FEC,
    AVT_CONN_STATE_RX_CORRUPT,
    AVT_CONN_STATE_RX_DROPPED,
    AVT_CONN_STATE_RX_LOST,
    AVT_CONN_STATE_RX_TOTAL,
    /* Bitrate is intentionally absent -- it very likely always changes. */
    AVT_CONN_STATE_TX_SENT,
    /* Same with sender bitrate */
    /* Same with buffer */
    /* Same with buffered duration */
};

/**
 * Queries connection status.
 * Depending on connection configuration, may query the receiver.
 */
typedef struct AVTConnectionStatus {
    /* Only valid for avt_connection_status_cb. Indicates what changed. */
    enum AVTConnectionStatusFlags flags;

    /* Raw MTU of the connection */
    uint32_t mtu;

    /* If a non-fatal error occurs */
    int err;

    /* Receive statistics */
    struct {
        /* Indicates the total number of missing packets that FEC was able to
         * reconstruct. */
        uint64_t fec_corrections;

        /* Indicates the total number of corrupt packets. This also counts
         * corrupt packets FEC was not able to correct. */
        uint64_t corrupt_packets;

        /* Indicates the total number of dropped packets due to slow processing
         * (socket side). */
        uint64_t dropped_packets;

        /* Indicates the total number of packets missed. */
        uint64_t lost_packets;

        /* The total number of packets correctly received */
        uint64_t packets;

        /* Receive bitrate in bits per second */
        int64_t bitrate;
    } rx;

    /* Sent statistics */
    struct {
        /* The total number of packets send */
        uint64_t packets;

        /* Bitrate in bits per second */
        int64_t bitrate;

        /* Total number of bytes buffered */
        int64_t buffer;

        /* Total duration of all packets buffered (timebase: 1 nanosecond) */
        int64_t buffer_duration;
    } tx;
} AVTConnectionStatus;

/**
 * Subscribe to receive status notifications.
 * Special error codes like AVTERROR_EOS will be returned from status_cb on
 * error and where appropriate, otherwise 0.
 */
AVT_API int avt_connection_status_cb(AVTConnection *conn, void *opaque,
                                     void (*status_cb)(void *opaque,
                                                       AVTConnectionStatus *s));

/**
 * Seek into the stream. Affects only reading. Output is always continuous.
 * If pts is not INT64_MIN, pts will be used to find the seek point. tb must
 * be the timebase of pts.
 * If pts is INT64_MIN, then offset, a byte value, will be used.
 * If offset_is_absolute, the offset will be treated as an absolute position,
 * otherwise, a relative seek will be performed.
 * May return an error if no starting PTS was found, or a seek was impossible. */
AVT_API int avt_connection_seek(AVTContext *ctx, AVTConnection *st,
                                int64_t pts, AVTRational tb,
                                int64_t offset, bool offset_is_absolute);

/**
 * Immediately flush all buffered data for a connection.
 * Should be called before avt_connection_destroy().
 */
AVT_API int avt_connection_flush(AVTConnection *conn, int64_t timeout);

/**
 * Immediately destroy a connection, and free all resources associated with it.
 *
 * NOTE: Will not flush
 */
AVT_API int avt_connection_destroy(AVTConnection **conn);

#endif /* AVTRANSPORT_CONNECTION_H */
