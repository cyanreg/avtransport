/*
 * Copyright © 2023, Lynne
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

#ifndef LIBAVTRANSPORT_CONNECTION_HEADER
#define LIBAVTRANSPORT_CONNECTION_HEADER

#include <libavtransport/utils.h>

typedef struct AVTConnection AVTConnection;

/* Connection type */
enum AVTConnectionType {
    /* URL address in the form of:
     *
     * avt://[<transport>[:<mode>]@]<address>[:<port>][?<setting1>=<value1>]
     *
     * - <transport> may be either missing (fallback to UDP),
     *   "udp", "udplite", "quic".
     *
     * - <mode> is an optional setting which has different meanings for
     *   senders and receivers, and transports.
     *
     *   For senders, the default value, active means to start continuously sending
     *   packets to the given address. Passive means to wait for receivers to
     *   send packets on the given address to begin transmitting back to them.
     *
     *   For receivers, the default value, passive means to listen on the given
     *   address for any packets received. Active for receivers means that receivers
     *   will actively connect and try to request a stream from the target address.
     *
     * - <address> of the remote host, or local host, or multicast group
     *
     * - <port> on which to listen on/transmit to
     *
     * - <setting1>=<value1> are a key=value pair of settings for the connection
     *   For a list of options, consult AVTInputOptions for receivers in the
     *   libavtransport/input.h header, and AVTOutputOptions for senders in the
     *   libavtransport/output.h header.
     */
    AVT_CONNECTION_URL = 0,

    /* Reverse connectivity for receivers.
     * The context MUST already have an output initialized. */
    AVT_CONNECTION_REVERSE,

    /* File path */
    AVT_CONNECTION_FILE,

    /* Socket */
    AVT_CONNECTION_SOCKET,

    /* Raw reader/writer using callbacks. */
    AVT_CONNECTION_CALLBACKS,
};

typedef struct AVTConnectionInfo {
    /* Connection type */
    enum AVTConnectionType type;

    union {
        /* For AVT_CONNECTION_URL: URL using the syntax described above
         * For files: file path */
        const char *path;

        /* Socket or file descriptor */
        int socket;

        /* Reading: callback to be called to give libavtransport a buffer of data.
         * Writing: callback to be called to receive a buffer from libavtransport. */
        int (*data)(void *opaque, AVTBuffer *buf);
    };

    /* Opaque pointer which will be used for the data() callback. */
    void *opaque;

    /* Input: seek buffer */
    size_t buffer;
} AVTConnectionInfo;

/**
 * Initialize a connection. If this function returns 0 or greater,
 * the connection set in *conn will be initialized to a connection context,
 * which may be passed onto other functions.
 */
AVT_API int avt_connection_create(AVTContext *ctx, AVTConnection **conn,
                                  AVTConnectionInfo *info);

/**
 * Immediately flush all buffered data for a connection.
 * Should be called before avt_connection_destroy().
 */
AVT_API void avt_connection_flush(AVTContext *ctx, AVTConnection **conn);

/**
 * Immediately destroy a connection, and free all resources associated
 * with it.
 * Will not flush.
 */
AVT_API void avt_connection_destroy(AVTContext *ctx, AVTConnection **conn);

#endif
