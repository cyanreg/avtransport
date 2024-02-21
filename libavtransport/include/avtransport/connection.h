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

    /* URL address in the form of:
     *
     * avt://[<transport>[:<mode>]@]<address>[:<port>][?<setting1>=<value1>][/<stream_id>[+<stream_id>][?<param1>=<val1>]]
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
     *
     * - <port> on which to listen on/transmit to
     *
     * - <stream_id> of the stream(s) to present as default (overriding those
     *   signalled by the sender)
     *
     * - <setting1>=<value1> are a key=value pair of settings for the connection
     *
     * The library will also accept shorthand notations, such as "udp://",
     * "udplite://", "quic://" and "file://", but none may be followed by
     * <transport>:<mode>, only <address>[:<port>]. Using "avt://" is recommended.
     */
    AVT_CONNECTION_URL,

    /* File path */
    AVT_CONNECTION_FILE,

    /* Socket or file descriptor */
    AVT_CONNECTION_SOCKET,

    /* Raw byte-level reader/writer using a callback. */
    AVT_CONNECTION_CALLBACK,

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
        /* AVT_CONNECTION_URL: url or file path using the syntax described above */
        const char *path;

        /* AVT_CONNECTION_SOCKET: opened and bound socket
         * AVT_CONNECTION_FILE:   opened file descriptor */
        struct {
            /* File descriptor or bound socket
             * NOTE: dup()-licated on success, users can close() this freely */
            int socket;
            /* AVT_CONNECTION_SOCKET senders: destination IP (IPv4: mapped in IPv6) */
            uint8_t dst[16];
            /* AVT_CONNECTION_SOCKET: use the receiver address instead of dst */
            bool use_receiver_addr;
            /* AVT_CONNECTION_SOCKET: which protocol to use */
            enum AVTProtocolType protocol;
            /* AVT_CONNECTION_SOCKET: behaviour of the protocol */
            enum AVTProtocolMode mode;
        } fd;

        /* AVT_CONNECTION_CALLBACK: the following structure */
        struct {
            /* Called by libavtransport in strictly sequential order,
             * with no holes, to write data. */
            int (*write)(void *opaque,
                         uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                         AVTBuffer *payload);

            /* Called by libavtransport to retrieve a piece of
             * data at a particular offset. */
            int (*read)(void *opaque,
                        uint8_t hdr[AVT_MAX_HEADER_LEN], size_t hdr_len,
                        AVTBuffer **payload, int64_t offset);

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
             * data at a particular sequence number. */
            int (*in)(void *opaque,
                      union AVTPacketData *pkt, AVTBuffer **buf,
                      uint64_t seq);

            /* Opaque pointer which will be used for the data() callback. */
            void *opaque;
        } pkt;
    };

    /* Input options */
    struct {
        /* Probe data instead of only initializing the connection.
         * avt_connection_create() will block until the first packet is received. */
        bool probe;

        /* Buffer size limit. Zero means automatic. Approximate/best effort. */
        size_t buffer;
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
 * Creates an AVTransport file with the given <path>.
 *
 * The resulting stream contains every single packet going in or out,
 * allowing for exact backing up and preservation of ephemeral streams,
 * as well as debugging, and more expensive offline FEC.
 *
 * The file will also be used as a retransmit cache, reducing memory usage
 * and allowing infinite retransmission (actual retransmitted data packets
 * are omitted from the mirror as they're redundant).
 *
 * NOTE: avt_connection_flush() should be called before calling
 *       avt_connection_mirror_close().
 */
AVT_API int avt_connection_mirror_open(AVTConnection *conn, const char *path);
AVT_API int avt_connection_mirror_close(AVTConnection *conn);

/**
 * Queries connection status.
 * Depending on connection configuration, may query the receiver.
 */
typedef struct AVTConnectionStatus {
    /* Receive statistics */
    struct {
        /* A counter that indicates the total amount of repaired packets
         * (packets with errors that FEC was able to correct). */
        uint64_t fec_corrections;

        /* Indicates the total number of corrupt packets. This also counts
         * corrupt packets FEC was not able to correct. */
        uint64_t corrupt_packets;

        /* Indicates the total number of dropped packets. */
        uint64_t dropped_packets;

        /* The total number of packets received */
        uint64_t packets;

        /* Bitrate in bits per second */
        int64_t bitrate;
    } rx;

    /* Send statistics */
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

AVT_API int avt_connection_status(AVTConnection *conn, AVTConnectionStatus *s,
                                  int64_t timeout);

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
