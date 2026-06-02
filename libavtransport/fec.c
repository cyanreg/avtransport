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

/*
 * RaptorQ (RFC 6330) forward error correction, backed by the vendored,
 * MIT-licensed nanorq library (subprojects/nanorq).
 *
 * The AVTransport wire format carries no RaptorQ Object Transmission
 * Information: the symbol size is fixed at 128 bytes and every other
 * parameter follows from the protected payload's length. The sender and
 * the receiver therefore derive identical codec parameters from that
 * length alone, with no OTI or per-symbol signalling.
 *
 * A payload is split into the minimum number of RaptorQ source blocks
 * that keeps every block within nanorq's per-block symbol limit. Each
 * block's repair symbols carry ESIs running sequentially from that block's
 * K, and the parity blob holds them block 0 first; the receiver re-derives
 * the same block layout from the payload length, so block identity needs
 * no signalling either.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <avtransport/avtransport.h>
#include <nanorq.h>

#include "fec.h"
#include "utils_internal.h"

/* Largest source block nanorq will accept (its K_max). A 128-byte symbol
 * caps a single-block payload at 56403 x 128, i.e. ~6.9 MiB. */
#define AVT_FEC_MAX_SYMBOLS 56403

/* RaptorQ symbol alignment (RFC 6330's Al), distinct from the symbol size;
 * the symbol size must be a multiple of it. */
#define AVT_FEC_SYMBOL_ALIGN 4

/* Maximum RaptorQ source blocks — nanorq's Z_max, bounded by the 8-bit SBN. */
#define AVT_FEC_MAX_BLOCKS 256

/* Derive the RaptorQ source-block partition for an `flen`-byte payload: the
 * total symbol count `*kt` and the nominal per-block symbol count `*kn`
 * (chosen so the block count is the minimum that keeps each block within
 * AVT_FEC_MAX_SYMBOLS). The sender and receiver run this identically, so
 * they agree on the layout with nothing signalled. The block count is then
 * (*kt + *kn - 1) / *kn.
 *
 * Returns 0, or AVT_ERROR(ENOTSUP) if the payload is too large to protect. */
static int avt_fec_partition(size_t flen, uint32_t *kt, uint32_t *kn)
{
    const uint64_t nsym = (flen + AVT_FEC_SYMBOL_SIZE - 1) /
                          AVT_FEC_SYMBOL_SIZE;
    if (nsym < 1 ||
        nsym > (uint64_t)AVT_FEC_MAX_BLOCKS * AVT_FEC_MAX_SYMBOLS)
        return AVT_ERROR(ENOTSUP);

    uint32_t nb = (uint32_t)((nsym + AVT_FEC_MAX_SYMBOLS - 1) /
                             AVT_FEC_MAX_SYMBOLS);
    if (nb < 1)
        nb = 1;

    *kt = (uint32_t)nsym;
    *kn = (uint32_t)((nsym + nb - 1) / nb);
    return 0;
}

AVTBuffer *avt_fec_gen_parity(void *log_ctx, AVTBuffer *payload, int pct)
{
    size_t flen;
    uint8_t *fdata = avt_buffer_get_data(payload, &flen);
    if (!fdata || pct <= 0 || flen < AVT_FEC_SYMBOL_SIZE)
        return NULL;

    uint32_t kt, kn;
    if (avt_fec_partition(flen, &kt, &kn) < 0) {
        avt_log(log_ctx, AVT_LOG_DEBUG,
                "FEC: %zu-byte payload exceeds the FEC size limit; "
                "skipping parity\n", flen);
        return NULL;
    }

    /* Passing the nominal per-block symbol count makes nanorq split the
     * payload into the matching number of source blocks. */
    nanorq *rq = nanorq_encoder_new_ex(flen, AVT_FEC_SYMBOL_SIZE, kn, 0,
                                       AVT_FEC_SYMBOL_ALIGN);
    if (!rq) {
        avt_log(log_ctx, AVT_LOG_ERROR, "FEC: RaptorQ encoder init failed\n");
        return NULL;
    }

    const uint32_t nb_blocks = nanorq_blocks(rq);

    /* Repair symbols per block are capped at the smallest block's symbol
     * count, so every block's repair ESIs stay within the decoder's range. */
    uint32_t k_min = UINT32_MAX;
    for (uint32_t b = 0; b < nb_blocks; b++) {
        uint32_t kb = nanorq_block_symbols(rq, b);
        if (kb < k_min)
            k_min = kb;
    }

    uint32_t r_per_block = (uint32_t)(((uint64_t)k_min * (uint64_t)pct + 99) /
                                      100);
    if (!r_per_block)
        r_per_block = 1;
    if (r_per_block > k_min)
        r_per_block = k_min;

    const size_t blob_len = (size_t)r_per_block * nb_blocks *
                            AVT_FEC_SYMBOL_SIZE;
    uint8_t *blob = malloc(blob_len);
    struct ioctx *io = blob ? ioctx_from_mem(fdata, flen) : NULL;
    if (!io) {
        free(blob);
        nanorq_free(rq);
        return NULL;
    }

    /* For each block: precompute its intermediate symbols, then emit
     * `r_per_block` repair symbols with ESIs running from that block's K.
     * The blob holds block 0's repair symbols first, then block 1's, ... */
    int err = 0;
    for (uint32_t b = 0; b < nb_blocks && !err; b++) {
        if (!nanorq_generate_symbols(rq, b, io)) {
            avt_log(log_ctx, AVT_LOG_ERROR,
                    "FEC: RaptorQ symbol generation failed (block %"
                    PRIu32 ")\n", b);
            err = 1;
            break;
        }
        const uint32_t kb = nanorq_block_symbols(rq, b);
        for (uint32_t j = 0; j < r_per_block; j++) {
            const size_t off = ((size_t)b * r_per_block + j) *
                               AVT_FEC_SYMBOL_SIZE;
            if (nanorq_encode(rq, blob + off, kb + j, b, io) !=
                AVT_FEC_SYMBOL_SIZE) {
                avt_log(log_ctx, AVT_LOG_ERROR,
                        "FEC: failed to encode a repair symbol\n");
                err = 1;
                break;
            }
        }
    }

    io->destroy(io);
    nanorq_free(rq);

    if (err) {
        free(blob);
        return NULL;
    }

    AVTBuffer *buf = avt_buffer_create(blob, blob_len, NULL,
                                       avt_buffer_default_free);
    if (!buf) {
        free(blob);
        return NULL;
    }

    avt_log(log_ctx, AVT_LOG_DEBUG,
            "FEC: %" PRIu32 " repair symbols x %" PRIu32 " block(s) (%zu B) "
            "for a %zu B payload, Kt=%" PRIu32 "\n",
            r_per_block, nb_blocks, blob_len, flen, kt);
    return buf;
}

int avt_fec_recover(void *log_ctx,
                    uint8_t *data, size_t data_len,
                    const uint8_t *data_present,
                    const uint8_t *parity, size_t parity_len,
                    const uint8_t *parity_present)
{
    if (!data || !data_present || data_len < AVT_FEC_SYMBOL_SIZE)
        return AVT_ERROR(EINVAL);

    uint32_t kt, kn;
    if (avt_fec_partition(data_len, &kt, &kn) < 0)
        return AVT_ERROR(ENOTSUP);
    const uint32_t z = (kt + kn - 1) / kn;

    const size_t nrep = (parity && parity_present) ?
                        parity_len / AVT_FEC_SYMBOL_SIZE : 0;
    const uint32_t r_per_block = (uint32_t)(nrep / z);

    /* Reconstruct the OTI nanorq's encoder produced: `z` source blocks, one
     * sub-block, AVT_FEC_SYMBOL_ALIGN alignment, of `data_len` bytes. */
    const uint64_t oti_common = ((uint64_t)data_len << 24) |
                                (AVT_FEC_SYMBOL_SIZE - 1);
    const uint32_t oti_scheme = ((z - 1) << 24) | AVT_FEC_SYMBOL_ALIGN;

    nanorq *dec = nanorq_decoder_new(oti_common, oti_scheme);
    if (!dec)
        return AVT_ERROR(ENOMEM);

    const uint32_t nb_blocks = nanorq_blocks(dec);

    /* Recovery is per block, so a global symbol count is not enough: bail
     * before allocating scratch if any block lacks the K symbols it needs. */
    for (uint32_t b = 0, base = 0; b < nb_blocks; b++) {
        const uint32_t kb = nanorq_block_symbols(dec, b);
        uint32_t have = 0;
        for (uint32_t e = 0; e < kb; e++)
            have += !!data_present[base + e];
        for (uint32_t j = 0; j < r_per_block; j++)
            have += !!parity_present[(size_t)b * r_per_block + j];
        base += kb;
        if (have < kb) {
            nanorq_free(dec);
            return AVT_ERROR(EAGAIN);
        }
    }

    int err = 0;
    struct ioctx *io = NULL;
    uint8_t *obj = calloc(1, data_len);
    if (!obj) {
        err = AVT_ERROR(ENOMEM);
        goto done;
    }
    io = ioctx_from_mem(obj, data_len);
    if (!io) {
        err = AVT_ERROR(ENOMEM);
        goto done;
    }

    /* Feed each block its surviving source and repair symbols, then solve
     * it. `base` is the running global source-symbol index, which is also
     * the symbol's byte offset divided by the symbol size. */
    uint8_t sym[AVT_FEC_SYMBOL_SIZE];
    uint32_t base = 0;
    for (uint32_t b = 0; b < nb_blocks && !err; b++) {
        const uint32_t kb = nanorq_block_symbols(dec, b);

        for (uint32_t e = 0; e < kb && !err; e++) {
            if (!data_present[base + e])
                continue;
            const size_t off = (size_t)(base + e) * AVT_FEC_SYMBOL_SIZE;
            size_t n = data_len - off;
            if (n > AVT_FEC_SYMBOL_SIZE)
                n = AVT_FEC_SYMBOL_SIZE;
            memset(sym, 0, sizeof(sym));
            memcpy(sym, data + off, n);
            if (nanorq_decoder_add_symbol(dec, sym, nanorq_tag(b, e),
                                          io) == NANORQ_SYM_ERR)
                err = AVT_ERROR(EINVAL);
        }

        for (uint32_t j = 0; j < r_per_block && !err; j++) {
            const size_t ridx = (size_t)b * r_per_block + j;
            if (!parity_present[ridx])
                continue;
            memcpy(sym, parity + ridx * AVT_FEC_SYMBOL_SIZE,
                   AVT_FEC_SYMBOL_SIZE);
            if (nanorq_decoder_add_symbol(dec, sym, nanorq_tag(b, kb + j),
                                          io) == NANORQ_SYM_ERR)
                err = AVT_ERROR(EINVAL);
        }

        /* Solve the block; nanorq writes its source symbols into `obj`. */
        if (!err && !nanorq_repair_block(dec, io, b))
            err = AVT_ERROR(EAGAIN);

        base += kb;
    }

    if (!err) {
        memcpy(data, obj, data_len);
        avt_log(log_ctx, AVT_LOG_DEBUG,
                "FEC: recovered a %zu B payload across %" PRIu32 " block(s)\n",
                data_len, nb_blocks);
    }

done:
    if (io)
        io->destroy(io);
    free(obj);
    nanorq_free(dec);
    return err;
}

/* OTI alignment used for AVTransport FEC groups. */
#define AVT_FEC_GROUP_OTI_AL 4

void avt_fec_oti_pack(uint64_t F, uint16_t T, uint8_t Z, uint16_t N,
                      uint8_t Al,
                      uint64_t *common, uint32_t *scheme)
{
    *common = ((F & ((1ULL << 40) - 1)) << 24) |
              (uint64_t)((T ? (uint16_t)(T - 1) : 0u));
    *scheme = ((uint32_t)(Z ? (Z - 1) : 0u) << 24) |
              ((uint32_t)(N ? (N - 1) : 0u) << 8) |
              (uint32_t)Al;
}

void avt_fec_oti_unpack(uint64_t common, uint32_t scheme,
                        uint64_t *F, uint16_t *T,
                        uint8_t *Z, uint16_t *N, uint8_t *Al)
{
    *F  = common >> 24;
    *T  = (uint16_t)(common & 0xFFFF) + 1;
    *Z  = (uint8_t)((scheme >> 24) & 0xFF) + 1;
    *N  = (uint16_t)((scheme >> 8) & 0xFFFF) + 1;
    *Al = (uint8_t)(scheme & 0xFF);
}

int avt_fec_group_encode(void *log_ctx,
                         const uint8_t *source, size_t source_len,
                         uint16_t symbol_size,
                         uint16_t repair_per_block,
                         uint64_t *out_common, uint32_t *out_scheme,
                         uint8_t **out_parity, size_t *out_parity_len)
{
    *out_parity = NULL;
    *out_parity_len = 0;

    if (!source || !source_len || !symbol_size ||
        symbol_size % AVT_FEC_GROUP_OTI_AL ||
        source_len % symbol_size ||
        !repair_per_block)
        return AVT_ERROR(EINVAL);

    const size_t Kt = source_len / symbol_size;
    if (Kt < 1 || Kt > (size_t)AVT_FEC_MAX_BLOCKS * AVT_FEC_MAX_SYMBOLS)
        return AVT_ERROR(ENOTSUP);

    uint16_t Z = (uint16_t)((Kt + AVT_FEC_MAX_SYMBOLS - 1) /
                            AVT_FEC_MAX_SYMBOLS);
    if (Z < 1)
        Z = 1;
    const uint16_t Kn = (uint16_t)((Kt + Z - 1) / Z);

    /* nanorq picks the partition itself from K (per-block symbols);
     * passing Kn keeps blocks within K_max. */
    nanorq *rq = nanorq_encoder_new_ex(source_len, symbol_size, Kn, 0,
                                       AVT_FEC_GROUP_OTI_AL);
    if (!rq) {
        avt_log(log_ctx, AVT_LOG_ERROR,
                "FEC group: encoder init failed (F=%zu, T=%u, Kn=%u)\n",
                source_len, symbol_size, Kn);
        return AVT_ERROR(EINVAL);
    }

    const uint32_t nb_blocks = nanorq_blocks(rq);

    /* Repair symbols per block cannot exceed the smallest block's K. */
    uint32_t k_min = UINT32_MAX;
    for (uint32_t b = 0; b < nb_blocks; b++) {
        uint32_t kb = nanorq_block_symbols(rq, b);
        if (kb < k_min)
            k_min = kb;
    }
    uint32_t r_per_block = repair_per_block;
    if (r_per_block > k_min)
        r_per_block = k_min;

    const size_t blob_len = (size_t)r_per_block * nb_blocks * symbol_size;
    uint8_t *blob = malloc(blob_len);
    struct ioctx *io = blob ? ioctx_from_mem((uint8_t *)source, source_len)
                            : NULL;
    if (!io) {
        free(blob);
        nanorq_free(rq);
        return AVT_ERROR(ENOMEM);
    }

    int err = 0;
    for (uint32_t b = 0; b < nb_blocks && !err; b++) {
        if (!nanorq_generate_symbols(rq, b, io)) {
            err = AVT_ERROR(EINVAL);
            break;
        }
        const uint32_t kb = nanorq_block_symbols(rq, b);
        for (uint32_t j = 0; j < r_per_block; j++) {
            const size_t off = ((size_t)b * r_per_block + j) * symbol_size;
            if (nanorq_encode(rq, blob + off, kb + j, b, io) != symbol_size) {
                err = AVT_ERROR(EINVAL);
                break;
            }
        }
    }

    io->destroy(io);
    nanorq_free(rq);

    if (err) {
        free(blob);
        return err;
    }

    *out_parity = blob;
    *out_parity_len = blob_len;
    /* OTI words use nanorq's encoding (T-1, Z-1, N-1) so a freshly built
     * decoder reproduces the same parameters. N is whatever the encoder
     * chose (always 1 today). Al is fixed per the alignment constant. */
    avt_fec_oti_pack(source_len, symbol_size,
                     (uint8_t)nb_blocks, /* Z */
                     1,                  /* N */
                     AVT_FEC_GROUP_OTI_AL,
                     out_common, out_scheme);

    avt_log(log_ctx, AVT_LOG_DEBUG,
            "FEC group: %u repair x %u block(s), T=%u, Kt=%zu, blob=%zu B\n",
            r_per_block, nb_blocks, symbol_size, Kt, blob_len);
    return 0;
}

int avt_fec_group_decode(void *log_ctx,
                         uint64_t common, uint32_t scheme,
                         uint8_t *source, size_t source_len,
                         const uint8_t *source_present,
                         const uint8_t *parity, size_t parity_len,
                         const uint8_t *parity_present)
{
    if (!source || !source_present || !source_len)
        return AVT_ERROR(EINVAL);

    uint64_t F;
    uint16_t T;
    uint8_t Z;
    uint16_t N;
    uint8_t Al;
    avt_fec_oti_unpack(common, scheme, &F, &T, &Z, &N, &Al);
    if (!T || source_len != F || source_len % T)
        return AVT_ERROR(EINVAL);

    nanorq *dec = nanorq_decoder_new(common, scheme);
    if (!dec)
        return AVT_ERROR(ENOMEM);

    const uint32_t nb_blocks = nanorq_blocks(dec);
    const size_t nrep = (parity && parity_present) ? parity_len / T : 0;
    const uint32_t r_per_block = nb_blocks ? (uint32_t)(nrep / nb_blocks) : 0;

    /* Bail before allocating scratch if a block lacks enough symbols. */
    for (uint32_t b = 0, base = 0; b < nb_blocks; b++) {
        const uint32_t kb = nanorq_block_symbols(dec, b);
        uint32_t have = 0;
        for (uint32_t e = 0; e < kb; e++)
            have += !!source_present[base + e];
        for (uint32_t j = 0; j < r_per_block; j++)
            have += !!parity_present[(size_t)b * r_per_block + j];
        base += kb;
        if (have < kb) {
            nanorq_free(dec);
            return AVT_ERROR(EAGAIN);
        }
    }

    int err = 0;
    struct ioctx *io = NULL;
    uint8_t *obj = calloc(1, source_len);
    if (!obj) {
        err = AVT_ERROR(ENOMEM);
        goto done;
    }
    io = ioctx_from_mem(obj, source_len);
    if (!io) {
        err = AVT_ERROR(ENOMEM);
        goto done;
    }

    uint8_t *sym = malloc(T);
    if (!sym) {
        err = AVT_ERROR(ENOMEM);
        goto done;
    }

    uint32_t base = 0;
    for (uint32_t b = 0; b < nb_blocks && !err; b++) {
        const uint32_t kb = nanorq_block_symbols(dec, b);

        for (uint32_t e = 0; e < kb && !err; e++) {
            if (!source_present[base + e])
                continue;
            const size_t off = (size_t)(base + e) * T;
            memcpy(sym, source + off, T);
            if (nanorq_decoder_add_symbol(dec, sym, nanorq_tag(b, e),
                                          io) == NANORQ_SYM_ERR)
                err = AVT_ERROR(EINVAL);
        }

        for (uint32_t j = 0; j < r_per_block && !err; j++) {
            const size_t ridx = (size_t)b * r_per_block + j;
            if (!parity_present[ridx])
                continue;
            memcpy(sym, parity + ridx * T, T);
            if (nanorq_decoder_add_symbol(dec, sym, nanorq_tag(b, kb + j),
                                          io) == NANORQ_SYM_ERR)
                err = AVT_ERROR(EINVAL);
        }

        if (!err && !nanorq_repair_block(dec, io, b))
            err = AVT_ERROR(EAGAIN);

        base += kb;
    }

    free(sym);

    if (!err) {
        memcpy(source, obj, source_len);
        avt_log(log_ctx, AVT_LOG_DEBUG,
                "FEC group: recovered %zu B across %u block(s)\n",
                source_len, nb_blocks);
    }

done:
    if (io)
        io->destroy(io);
    free(obj);
    nanorq_free(dec);
    return err;
}
