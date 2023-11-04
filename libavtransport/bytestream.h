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

#ifndef LIBAVTRANSPORT_BYTESTREAM
#define LIBAVTRANSPORT_BYTESTREAM

#include <stdint.h>
#include <string.h>

#include "utils.h"

#ifndef PQ_RB8
#define PQ_RB8(x)               \
    (((const uint8_t *)(x))[0])
#endif

#ifndef PQ_RL8
#define PQ_RL8(x)               \
    (((const uint8_t *)(x))[0])
#endif

#ifndef PQ_WB8
#define PQ_WB8(p, val)             \
    do {                           \
        uint8_t d = (val);         \
        ((uint8_t *)(p))[0] = (d); \
    } while(0)
#endif

#ifndef PQ_WL8
#define PQ_WL8(p, val)             \
    do {                           \
        uint8_t d = (val);         \
        ((uint8_t *)(p))[0] = (d); \
    } while(0)
#endif

#ifndef PQ_RB16
#define PQ_RB16(x)                                                 \
    ((((const uint8_t *)(x))[0] << 8) | ((const uint8_t *)(x))[1])
#endif

#ifndef PQ_RL16
#define PQ_RL16(x)                                                 \
    ((((const uint8_t *)(x))[1] << 8) | ((const uint8_t *)(x))[0])
#endif

#ifndef PQ_WB16
#define PQ_WB16(p, val)                 \
    do {                                \
        uint16_t d = (val);             \
        ((uint8_t *)(p))[1] = (d) >> 0; \
        ((uint8_t *)(p))[0] = (d) >> 8; \
    } while(0)
#endif

#ifndef PQ_WL16
#define PQ_WL16(p, val)                 \
    do {                                \
        uint16_t d = (val);             \
        ((uint8_t *)(p))[0] = (d) >> 0; \
        ((uint8_t *)(p))[1] = (d) >> 8; \
    } while(0)
#endif

#ifndef PQ_RB32
#define PQ_RB32(x)                                                \
    ((uint32_t)((((uint32_t)(((const uint8_t *)(x))[0])) << 24) | \
                (((uint32_t)(((const uint8_t *)(x))[1])) << 16) | \
                (((uint32_t)(((const uint8_t *)(x))[2])) <<  8) | \
                (((uint32_t)(((const uint8_t *)(x))[3])) <<  0)))
#endif

#ifndef PQ_RL32
#define PQ_RL32(x)                                                \
    ((uint32_t)((((uint32_t)(((const uint8_t *)(x))[3])) << 24) | \
                (((uint32_t)(((const uint8_t *)(x))[2])) << 16) | \
                (((uint32_t)(((const uint8_t *)(x))[1])) <<  8) | \
                (((uint32_t)(((const uint8_t *)(x))[0])) <<  0)))
#endif

#ifndef PQ_WB32
#define PQ_WB32(p, val)                  \
    do {                                 \
        uint32_t d = (val);              \
        ((uint8_t *)(p))[3] = (d) >>  0; \
        ((uint8_t *)(p))[2] = (d) >>  8; \
        ((uint8_t *)(p))[1] = (d) >> 16; \
        ((uint8_t *)(p))[0] = (d) >> 24; \
    } while(0)
#endif

#ifndef PQ_WL32
#define PQ_WL32(p, val)                  \
    do {                                 \
        uint32_t d = (val);              \
        ((uint8_t *)(p))[0] = (d) >>  0; \
        ((uint8_t *)(p))[1] = (d) >>  8; \
        ((uint8_t *)(p))[2] = (d) >> 16; \
        ((uint8_t *)(p))[3] = (d) >> 24; \
    } while(0)
#endif

#ifndef PQ_RB64
#define PQ_RB64(x)                                                \
    ((uint64_t)((((uint64_t)(((const uint8_t *)(x))[0])) << 56) | \
                (((uint64_t)(((const uint8_t *)(x))[1])) << 48) | \
                (((uint64_t)(((const uint8_t *)(x))[2])) << 40) | \
                (((uint64_t)(((const uint8_t *)(x))[3])) << 32) | \
                (((uint64_t)(((const uint8_t *)(x))[4])) << 24) | \
                (((uint64_t)(((const uint8_t *)(x))[5])) << 16) | \
                (((uint64_t)(((const uint8_t *)(x))[6])) <<  8) | \
                (((uint64_t)(((const uint8_t *)(x))[7])) <<  0)))
#endif

#ifndef PQ_RL64
#define PQ_RL64(x)                                                \
    ((uint64_t)((((uint64_t)(((const uint8_t *)(x))[7])) << 56) | \
                (((uint64_t)(((const uint8_t *)(x))[6])) << 48) | \
                (((uint64_t)(((const uint8_t *)(x))[5])) << 40) | \
                (((uint64_t)(((const uint8_t *)(x))[4])) << 32) | \
                (((uint64_t)(((const uint8_t *)(x))[3])) << 24) | \
                (((uint64_t)(((const uint8_t *)(x))[2])) << 16) | \
                (((uint64_t)(((const uint8_t *)(x))[1])) <<  8) | \
                (((uint64_t)(((const uint8_t *)(x))[0])) <<  0)))
#endif

#ifndef PQ_WB64
#define PQ_WB64(p, val)                  \
    do {                                 \
        uint64_t d = (val);              \
        ((uint8_t *)(p))[7] = (d) >>  0; \
        ((uint8_t *)(p))[6] = (d) >>  8; \
        ((uint8_t *)(p))[5] = (d) >> 16; \
        ((uint8_t *)(p))[4] = (d) >> 24; \
        ((uint8_t *)(p))[3] = (d) >> 32; \
        ((uint8_t *)(p))[2] = (d) >> 40; \
        ((uint8_t *)(p))[1] = (d) >> 48; \
        ((uint8_t *)(p))[0] = (d) >> 56; \
    } while(0)
#endif

#ifndef PQ_WL64
#define PQ_WL64(p, val)                  \
    do {                                 \
        uint64_t d = (val);              \
        ((uint8_t *)(p))[0] = (d) >>  0; \
        ((uint8_t *)(p))[1] = (d) >>  8; \
        ((uint8_t *)(p))[2] = (d) >> 16; \
        ((uint8_t *)(p))[3] = (d) >> 24; \
        ((uint8_t *)(p))[4] = (d) >> 32; \
        ((uint8_t *)(p))[5] = (d) >> 40; \
        ((uint8_t *)(p))[6] = (d) >> 48; \
        ((uint8_t *)(p))[7] = (d) >> 56; \
    } while(0)
#endif

#if CONFIG_BIG_ENDIAN
#define PQ_RN(p, l)    PQ_RB##l(p)
#define PQ_WN(p, l, v) PQ_WB##l(p, v)
#else
#define PQ_RN(p, l)    PQ_RL##l(p)
#define PQ_WN(p, l, v) PQ_WL##l(p, v)
#endif

typedef struct PQByteStream {
    uint8_t *start;
    uint8_t *ptr;
    uint8_t *end;
} PQByteStream;

/* Initialize bytestream for reading or writing */
PQByteStream *pq_bs_init(uint8_t *buf, size_t len)
{
    return (PQByteStream) {
        .start = buf,
        .ptr = buf,
        .end = buf + len,
    };
}

/* Reset bytestream reader/writer */
void inline pq_bs_reset(PQByteStream *bs)
{
    bs->ptr = bs->start;
}

#define PQ_WRITE(en, n, len)                                          \
void inline pq_bsw_u##len##n(PQByteStream *bs, uint##len_t val)       \
{                                                                     \
    pq_assert1(((bs->ptr) + (l >> 3)) <= bs->end);                    \
    en##l(bs->ptr, t.val);                                            \
    bs->ptr += l >> 3;                                                \
}                                                                     \
                                                                      \
void inline pq_bsw_i##len##n(PQByteStream *bs, int##len_t _val)       \
{                                                                     \
    pq_assert1(((bs->ptr) + (l >> 3)) <= bs->end);                    \
    union t { int##len_t _val, uint##len_t val };                     \
    en##l(bs->ptr, t.val);                                            \
    bs->ptr += l >> 3;                                                \
}

PQ_WRITE_FN(PQ_WB, b, 8)
PQ_WRITE_FN(PQ_WB, b, 16)
PQ_WRITE_FN(PQ_WB, b, 32)
PQ_WRITE_FN(PQ_WB, b, 64)

PQ_WRITE_FN(PQ_LB, l, 8)
PQ_WRITE_FN(PQ_LB, l, 16)
PQ_WRITE_FN(PQ_LB, l, 32)
PQ_WRITE_FN(PQ_LB, l, 64)

#define PQ_WRITE_RATIONAL(en, n)                                      \
void inline pq_bsw_rt##n(PQByteStream *bs, AVTRational *r)            \
{                                                                     \
    pq_assert1(((bs->ptr) + 8) <= bs->end);                           \
    union { int##len_t _val, uint##len_t val } t[2];                  \
    t[0]._val = r.num;                                                \
    t[1]._val = r.den;                                                \
    en##32(bs->ptr, t[0].val);                                        \
    en##32(bs->ptr, t[1].val);                                        \
    bs->ptr += 8;                                                     \
}

PQ_WRITE_RATIONAL(PQ_WB, be)
PQ_WRITE_RATIONAL(PQ_WB, le)

/* Write padding */
void inline pq_bsw_zpad(PQByteStream *bs, size_t len)
{
    pq_assert1(((bs->ptr) + len) <= bs->end);
    memset(bs->ptr, 0, len);
    bs->ptr += len;
}

/* Write buffer to bytestream */
void inline pq_bsw_sbuf(PQByteStream *bs, void *buf, size_t len)
{
    pq_assert1(((bs->ptr) + len) <= bs->end);
    memcpy(bs->ptr, buf, len);
    bs->ptr += len;
}

/* Write fixed-length zero-terminated string to bytestream */
void inline pq_bsw_fstr(PQByteStream *bs, const char *str, size_t fixed_len)
{
    size_t lens = AVTMIN(strlen(str), fixed_len - 1);
    pq_assert1(((bs->ptr) + fixed_len) <= bs->end);
    memcpy(bs->ptr, str, lens);
    memset(bs->ptr, 0, fixed_len - lens);
    bs->ptr += fixed_len;
}

uint8_t *pq_bs_pos(PQByteStream *bs)
{
    return bs->ptr;
}

size_t pq_bs_offs(PQByteStream *bs)
{
    return bs->ptr - bs->start;
}

#define PQ_RDR(base_read, e, len, s)                                \
static inline uint ##len## _t pq_bs_read_##e##len(PQByteStream *bs) \
{                                                                   \
    uint ##len## _t v;                                              \
    if ((bs)->ptr + (len >> 3) <= (bs)->end) {                      \
        v = base_read##len((bs)->ptr);                              \
        (bs)->ptr += len >> 3;                                      \
    }                                                               \
    return v;                                                       \
}

static inline void pq_bs_skip(PQByteStream *bs, size_t len)
{
    if (bs->ptr + (len >> 3) <= bs->end)
        bs->ptr += len >> 3;
}

PQ_RDR(PQ_RB, b, 8, i)
PQ_RDR(PQ_RL, l, 8, i)
PQ_RDR(PQ_RB, b, 16, i)
PQ_RDR(PQ_RL, l, 16, i)
PQ_RDR(PQ_RB, b, 32, i)
PQ_RDR(PQ_RL, l, 32, i)
PQ_RDR(PQ_RB, b, 64, i)
PQ_RDR(PQ_RL, l, 64, i)

#endif

