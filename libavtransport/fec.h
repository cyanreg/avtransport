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

#ifndef AVTRANSPORT_FEC_H
#define AVTRANSPORT_FEC_H

#include <stddef.h>
#include <stdint.h>

#include "buffer.h"

/* RaptorQ symbol size in bytes. The AVTransport specification mandates a
 * 128-byte FEC symbol size for all parity data. */
#define AVT_FEC_SYMBOL_SIZE 128

/* Generate RaptorQ repair data covering an entire payload.
 *
 * `pct` is the quantity of repair data to produce, as a percentage of the
 * payload's RaptorQ source-symbol count (K). The returned buffer holds
 * ceil(K * pct / 100) repair symbols laid out as contiguous
 * AVT_FEC_SYMBOL_SIZE-byte symbols, with sequential RaptorQ Encoding Symbol
 * IDs starting at K within source block 0.
 *
 * Returns a newly allocated AVTBuffer the caller must unref, or NULL when
 * FEC is disabled (pct <= 0) or not applicable to this payload (too small,
 * or too large for a single RaptorQ source block).
 *
 * `log_ctx` is used only for logging. */
AVTBuffer *avt_fec_gen_parity(void *log_ctx, AVTBuffer *payload, int pct);

/* Recover a payload from partial source data plus RaptorQ repair symbols.
 *
 * `data` is the `data_len`-byte payload buffer with the received bytes
 * already in place; `data_present` is an array of
 * ceil(data_len / AVT_FEC_SYMBOL_SIZE) bytes, one per source symbol,
 * nonzero where that symbol's bytes are intact in `data`.
 *
 * `parity` is the received repair data (`parity_len` bytes), and
 * `parity_present` is an array of parity_len / AVT_FEC_SYMBOL_SIZE bytes,
 * one per repair symbol. `parity` and `parity_present` may be NULL when no
 * repair data is available.
 *
 * On success the gaps in `data` are filled in place and 0 is returned.
 * Returns AVT_ERROR(EAGAIN) if there are not yet enough symbols to decode
 * (the caller may retry once more arrive), or another negative AVT_ERROR()
 * on failure. `log_ctx` is used only for logging. */
int avt_fec_recover(void *log_ctx,
                    uint8_t *data, size_t data_len,
                    const uint8_t *data_present,
                    const uint8_t *parity, size_t parity_len,
                    const uint8_t *parity_present);

/* ---- FEC grouping primitives (spec 0x0030 / 0x0031) -------------------
 *
 * These functions implement the RaptorQ machinery used by cross-stream
 * FEC grouping. Unlike stream-data-parity, the OTI for a group is carried
 * on the wire (inside the 0x0030 packet) so the parameters are explicit.
 *
 * The source object is a contiguous buffer of `K * T` bytes containing
 * the K source packets back-to-back, each zero-padded to the symbol
 * size T. K equals the number of source packets in the group. F (the
 * RFC 6330 transfer length) is `K * T`. The receiver re-derives the
 * partition into source blocks from the OTI it received, exactly as on
 * the sender side. */

/* Pack RFC 6330 OTI parameters into the wire-format words used by
 * AVTransport's FEC grouping packets. */
void avt_fec_oti_pack(uint64_t F, uint16_t T, uint8_t Z, uint16_t N,
                      uint8_t Al,
                      uint64_t *common, uint32_t *scheme);

/* Inverse of avt_fec_oti_pack(). */
void avt_fec_oti_unpack(uint64_t common, uint32_t scheme,
                        uint64_t *F, uint16_t *T,
                        uint8_t *Z, uint16_t *N, uint8_t *Al);

/* Encode RaptorQ repair data for an FEC group.
 *
 * `source` is the K*T-byte source object (packets concatenated, each
 * padded to T). `symbol_size` is T (must be a positive multiple of
 * AVT_FEC_GROUP_OTI_AL). `repair_per_block` is the count of repair
 * symbols generated for each source block.
 *
 * On success: *out_parity points to a malloc()'d blob holding
 * `Z * repair_per_block * T` bytes (the repair symbols, laid out
 * block 0 first), *out_parity_len is set, and the receiver-side OTI
 * words are written to *out_common / *out_scheme. Caller frees
 * *out_parity. */
int avt_fec_group_encode(void *log_ctx,
                         const uint8_t *source, size_t source_len,
                         uint16_t symbol_size,
                         uint16_t repair_per_block,
                         uint64_t *out_common, uint32_t *out_scheme,
                         uint8_t **out_parity, size_t *out_parity_len);

/* Decode an FEC group's source object from partial source + parity.
 *
 * `source` is the K*T-byte source-object buffer, with the received bytes
 * already in place (gaps may hold anything). `source_present` is a
 * K-byte array nonzero at received source symbols. `parity` is the
 * received parity blob (its size signalled by `parity_len`) using the
 * same block-major layout as avt_fec_group_encode(); `parity_present`
 * is one byte per repair symbol slot.
 *
 * On success the gaps in `source` are filled in place. Returns
 * AVT_ERROR(EAGAIN) when a block does not yet have enough symbols to
 * solve, or a negative error otherwise. */
int avt_fec_group_decode(void *log_ctx,
                         uint64_t common, uint32_t scheme,
                         uint8_t *source, size_t source_len,
                         const uint8_t *source_present,
                         const uint8_t *parity, size_t parity_len,
                         const uint8_t *parity_present);

#endif /* AVTRANSPORT_FEC_H */
