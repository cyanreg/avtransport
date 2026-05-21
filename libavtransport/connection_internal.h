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

#ifndef AVTRANSPORT_CONNECTION_INTERNAL_H
#define AVTRANSPORT_CONNECTION_INTERNAL_H

#include <avtransport/connection.h>
#include <avtransport/send.h>
#include "packet_common.h"
#include "utils_internal.h"

int avt_connection_register_sender(AVTConnection *conn, AVTSender *s);

int avt_connection_send(AVTConnection *conn, AVTPktd *p);

/* Receive one or more assembled packets. Returns 0 with *out populated
 * when packets are ready, AVT_ERROR(EAGAIN) when not yet, or another
 * negative error code. */
int avt_connection_receive(AVTConnection *conn, AVTPacketFifo **out,
                           int64_t timeout);

/* Release a fifo obtained from avt_connection_receive(). */
int avt_connection_receive_done(AVTConnection *conn, AVTPacketFifo *out);

/* Receive one packet at a time (used by the receive dispatcher).
 * Returns 0 on success with pkt/buf populated, AVT_ERROR(EAGAIN) if
 * nothing is ready, AVT_ERROR(ENODATA) on EOF. The caller owns *buf
 * and must call avt_buffer_unref() on it. */
int avt_connection_receive_packet(AVTConnection *conn,
                                  union AVTPacketData *pkt,
                                  AVTBuffer **buf,
                                  int64_t timeout);

/* Drain available input for any receiver attached to this connection.
 * Returns 0 if work was done, AVT_ERROR(EAGAIN) if nothing to do,
 * AVT_ERROR(ENODATA) on EOF. */
int avt_receiver_drive(AVTContext *ctx, AVTConnection *conn, int64_t timeout);

#endif /* AVTRANSPORT_CONNECTION_INTERNAL_H */
