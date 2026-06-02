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

#ifndef AVTRANSPORT_STUN_H
#define AVTRANSPORT_STUN_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* Discover the server-reflexive (public) address of a UDP socket by performing
 * a STUN Binding transaction (RFC 8489) with an unauthenticated request. This
 * is the reflection step of NAT traversal: the value returned is the NAT
 * mapping for *this* socket as seen from the public side.
 *
 * Because NATs allocate mappings per source socket (and, for symmetric NATs,
 * per destination), `fd` MUST be the same socket subsequently used for the
 * data path (hole punch + QUIC); querying a separate socket yields a mapping
 * that will not match what the peer needs to reach. Issue the query before the
 * socket is connect()ed and before any other traffic flows on it.
 *
 *   log_ctx     - opaque logging context (may be NULL).
 *   fd          - an open, bound, unconnected UDP socket.
 *   server      - resolved address of the STUN server (e.g. coturn :3478).
 *   server_len  - length of `server`.
 *   mapped      - filled on success with the reflexive address, normalised to
 *                 IPv6 (IPv4 results are returned as v4-mapped ::ffff:a.b.c.d),
 *                 with the port in network byte order.
 *   timeout_ms  - total budget across retransmissions; <= 0 selects a default.
 *
 * Returns 0 on success, or a negative AVT_ERROR() value. Foreign or stale
 * datagrams arriving on the socket are ignored (matched out by transaction
 * ID), so the call is safe on a shared, unconnected socket. */
int avt_stun_query(void *log_ctx, int fd,
                   const struct sockaddr *server, socklen_t server_len,
                   struct sockaddr_in6 *mapped, int64_t timeout_ms);

#endif /* AVTRANSPORT_STUN_H */
