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

#ifndef LIBAVTRANSPORT_CONNECTION_INTERNAL
#define LIBAVTRANSPORT_CONNECTION_INTERNAL

#include "common.h"
#include "reorder.h"

enum AVTConnectionSubtype {
    /* Start values: AVTConnectionType */
    AVT_CONNECTION_UDP = 131072,
    AVT_CONNECTION_UDP_LITE,
    AVT_CONNECTION_QUIC,
};

typedef struct AVTConnectionPrototype {
    const char *name;
    enum AVTConnectionSubtype type;

    /* Initialize a context */
    int (*init)(AVTContext *ctx, AVTConnection *conn, AVTConnectionInfo *info);

    /* Return the maximum packet length */
    uint32_t (*get_max_pkt_len)(AVTContext *ctx, AVTConnection *conn);

    /* Fetch input, and call the callbacks, or return an error */
    int (*process_input)(AVTContext *ctx, AVTConnection *conn);

    /* Send output */
    int (*send_output)(AVTContext *ctx, AVTConnection *conn,
                       uint8_t *hdr, size_t hdr_len, AVTBuffer *buf);

    /* Seek to a place in the stream */
    int (*seek)(AVTContext *ctx, AVTConnection *conn,
                int64_t min_offset, uint32_t target_seq);

    /* Close */
    int (*close)(AVTContext *ctx, AVTConnection *conn);
} AVTConnectionPrototype;

struct AVTConnection {
    AVTConnectionPrototype *p;
    AVTReorderBuffer *in_buffer;

    /* Copy of the info provided to the API */
    AVTConnectionInfo info;

    /* Private context */
    struct AVTConnectionContext *ctx;
};

#endif
