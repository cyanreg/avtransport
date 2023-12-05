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

#ifndef AVTRANSPORT_BYTESTREAM
#define AVTRANSPORT_BYTESTREAM

#include <stdint.h>
#include <string.h>

#include <avtransport/utils.h>
#include <avtransport/rational.h>
#include "utils_internal.h"

#ifndef AVT_WB8
#define AVT_WB8(p, val)             \
    do {                            \
        uint8_t d = (val);          \
        ((uint8_t *)(p))[0] = (d);  \
    } while(0)
#endif

#ifndef AVT_WL8
#define AVT_WL8(p, val)             \
    do {                            \
        uint8_t d = (val);          \
        ((uint8_t *)(p))[0] = (d);  \
    } while(0)
#endif

#ifndef AVT_RB8
#define AVT_RB8(x)               \
    (((const uint8_t *)(x))[0])
#endif

#ifndef AVT_RL8
#define AVT_RL8(x)               \
    (((const uint8_t *)(x))[0])
#endif

#ifndef AVT_WB16
#define AVT_WB16(p, val)                          \
    do {                                          \
        uint16_t d = (val);                       \
        ((uint8_t *)(p))[0] = ((d) >> 8) & 0xFF;  \
        ((uint8_t *)(p))[1] = ((d) >> 0) & 0xFF;  \
    } while(0)
#endif

#ifndef AVT_WL16
#define AVT_WL16(p, val)                          \
    do {                                          \
        uint16_t d = (val);                       \
        ((uint8_t *)(p))[0] = ((d) >> 0) & 0xFF;  \
        ((uint8_t *)(p))[1] = ((d) >> 8) & 0xFF;  \
    } while(0)
#endif

#ifndef AVT_RB16
#define AVT_RB16(x)                                               \
    ((uint16_t)((((uint16_t)(((const uint8_t *)(x))[0])) << 8) |  \
                (((uint16_t)(((const uint8_t *)(x))[1])) << 0)))
#endif

#ifndef AVT_RL16
#define AVT_RL16(x)                                               \
    ((uint16_t)((((uint16_t)(((const uint8_t *)(x))[0])) << 8) |  \
                (((uint16_t)(((const uint8_t *)(x))[1])) << 0)))
#endif

#ifndef AVT_WB24
#define AVT_WB24(p, val)                           \
    do {                                           \
        uint32_t d = (val);                        \
        ((uint8_t *)(p))[0] = ((d) >> 24) & 0xFF;  \
        ((uint8_t *)(p))[1] = ((d) >> 16) & 0xFF;  \
        ((uint8_t *)(p))[2] = ((d) >>  8) & 0xFF;  \
    } while(0)
#endif

#ifndef AVT_WL24
#define AVT_WL24(p, val)                           \
    do {                                           \
        uint32_t d = (val);                        \
        ((uint8_t *)(p))[0] = ((d) >>  8) & 0xFF;  \
        ((uint8_t *)(p))[1] = ((d) >> 16) & 0xFF;  \
        ((uint8_t *)(p))[2] = ((d) >> 24) & 0xFF;  \
    } while(0)
#endif

#ifndef AVT_RB24
#define AVT_RB24(x)                                                \
    ((uint32_t)((((uint32_t)(((const uint8_t *)(x))[0])) << 24) |  \
                (((uint32_t)(((const uint8_t *)(x))[1])) << 16) |  \
                (((uint32_t)(((const uint8_t *)(x))[2])) <<  8)))
#endif

#ifndef AVT_RL24
#define AVT_RL24(x)                                                \
    ((uint32_t)((((uint32_t)(((const uint8_t *)(x))[0])) <<  8) |  \
                (((uint32_t)(((const uint8_t *)(x))[1])) << 16) |  \
                (((uint32_t)(((const uint8_t *)(x))[3])) << 24)))
#endif

#ifndef AVT_WB32
#define AVT_WB32(p, val)                           \
    do {                                           \
        uint32_t d = (val);                        \
        ((uint8_t *)(p))[0] = ((d) >> 24) & 0xFF;  \
        ((uint8_t *)(p))[1] = ((d) >> 16) & 0xFF;  \
        ((uint8_t *)(p))[2] = ((d) >>  8) & 0xFF;  \
        ((uint8_t *)(p))[3] = ((d) >>  0) & 0xFF;  \
    } while(0)
#endif

#ifndef AVT_WL32
#define AVT_WL32(p, val)                           \
    do {                                           \
        uint32_t d = (val);                        \
        ((uint8_t *)(p))[0] = ((d) >>  0) & 0xFF;  \
        ((uint8_t *)(p))[1] = ((d) >>  8) & 0xFF;  \
        ((uint8_t *)(p))[2] = ((d) >> 16) & 0xFF;  \
        ((uint8_t *)(p))[3] = ((d) >> 24) & 0xFF;  \
    } while(0)
#endif

#ifndef AVT_RB32
#define AVT_RB32(x)                                                \
    ((uint32_t)((((uint32_t)(((const uint8_t *)(x))[0])) << 24) |  \
                (((uint32_t)(((const uint8_t *)(x))[1])) << 16) |  \
                (((uint32_t)(((const uint8_t *)(x))[2])) <<  8) |  \
                (((uint32_t)(((const uint8_t *)(x))[3])) <<  0)))
#endif

#ifndef AVT_RL32
#define AVT_RL32(x)                                                \
    ((uint32_t)((((uint32_t)(((const uint8_t *)(x))[0])) <<  0) |  \
                (((uint32_t)(((const uint8_t *)(x))[1])) <<  8) |  \
                (((uint32_t)(((const uint8_t *)(x))[2])) << 16) |  \
                (((uint32_t)(((const uint8_t *)(x))[3])) << 24)))
#endif

#ifndef AVT_WB64
#define AVT_WB64(p, val)                          \
    do {                                          \
        uint64_t d = (val);                       \
        ((uint8_t *)(p))[0] = ((d) >> 56) & 0xFF; \
        ((uint8_t *)(p))[1] = ((d) >> 48) & 0xFF; \
        ((uint8_t *)(p))[2] = ((d) >> 40) & 0xFF; \
        ((uint8_t *)(p))[3] = ((d) >> 32) & 0xFF; \
        ((uint8_t *)(p))[4] = ((d) >> 24) & 0xFF; \
        ((uint8_t *)(p))[5] = ((d) >> 16) & 0xFF; \
        ((uint8_t *)(p))[6] = ((d) >>  8) & 0xFF; \
        ((uint8_t *)(p))[7] = ((d) >>  0) & 0xFF; \
    } while(0)
#endif

#ifndef AVT_WL64
#define AVT_WL64(p, val)                          \
    do {                                          \
        uint64_t d = (val);                       \
        ((uint8_t *)(p))[0] = ((d) >>  0) & 0xFF; \
        ((uint8_t *)(p))[1] = ((d) >>  8) & 0xFF; \
        ((uint8_t *)(p))[2] = ((d) >> 16) & 0xFF; \
        ((uint8_t *)(p))[3] = ((d) >> 24) & 0xFF; \
        ((uint8_t *)(p))[4] = ((d) >> 32) & 0xFF; \
        ((uint8_t *)(p))[5] = ((d) >> 40) & 0xFF; \
        ((uint8_t *)(p))[6] = ((d) >> 48) & 0xFF; \
        ((uint8_t *)(p))[7] = ((d) >> 56) & 0xFF; \
    } while(0)
#endif

#ifndef AVT_RB64
#define AVT_RB64(x)                                                \
    ((uint64_t)((((uint64_t)(((const uint8_t *)(x))[0])) << 56) |  \
                (((uint64_t)(((const uint8_t *)(x))[1])) << 48) |  \
                (((uint64_t)(((const uint8_t *)(x))[2])) << 40) |  \
                (((uint64_t)(((const uint8_t *)(x))[3])) << 32) |  \
                (((uint64_t)(((const uint8_t *)(x))[4])) << 24) |  \
                (((uint64_t)(((const uint8_t *)(x))[5])) << 16) |  \
                (((uint64_t)(((const uint8_t *)(x))[6])) <<  8) |  \
                (((uint64_t)(((const uint8_t *)(x))[7])) <<  0)))
#endif

#ifndef AVT_RL64
#define AVT_RL64(x)                                                \
    ((uint64_t)((((uint64_t)(((const uint8_t *)(x))[0])) <<  0) |  \
                (((uint64_t)(((const uint8_t *)(x))[1])) <<  8) |  \
                (((uint64_t)(((const uint8_t *)(x))[2])) << 16) |  \
                (((uint64_t)(((const uint8_t *)(x))[3])) << 24) |  \
                (((uint64_t)(((const uint8_t *)(x))[4])) << 32) |  \
                (((uint64_t)(((const uint8_t *)(x))[5])) << 40) |  \
                (((uint64_t)(((const uint8_t *)(x))[6])) << 48) |  \
                (((uint64_t)(((const uint8_t *)(x))[7])) << 56)))
#endif

#ifdef CONFIG_BIG_ENDIAN
#define AVT_RN(p, l)    AVT_RB##l(p)
#define AVT_WN(p, l, v) AVT_WB##l(p, v)
#else
#define AVT_RN(p, l)    AVT_RL##l(p)
#define AVT_WN(p, l, v) AVT_WL##l(p, v)
#endif

typedef struct AVTBytestream {
    uint8_t *start;
    uint8_t *ptr;
    uint8_t *end;
} AVTBytestream;

/* Initialize bytestream for reading or writing */
static inline AVTBytestream avt_bs_init(uint8_t *buf, size_t len)
{
    return (AVTBytestream) {
        .start = buf,
        .ptr = buf,
        .end = buf + len,
    };
}

/* Reset bytestream reader/writer */
static inline void avt_bs_reset(AVTBytestream *bs)
{
    bs->ptr = bs->start;
}

#define AVT_WRITE_FN(en, n, len, tlen)                                               \
static inline void avt_bsw_u ##len ##n (AVTBytestream *bs, const uint##tlen##_t val) \
{                                                                                    \
    avt_assert1(((bs->ptr) + (len >> 3)) <= bs->end);                                \
    en##len(bs->ptr, val);                                                           \
    bs->ptr += len >> 3;                                                             \
}                                                                                    \
                                                                                     \
static inline void avt_bsw_i ##len ##n (AVTBytestream *bs, const int##tlen##_t val)  \
{                                                                                    \
    avt_assert1(((bs->ptr) + (len >> 3)) <= bs->end);                                \
    union { int##tlen##_t ival; uint##tlen##_t uval; } t;                            \
    t.ival = val;                                                                    \
    en##len(bs->ptr, t.uval);                                                        \
    bs->ptr += len >> 3;                                                             \
}

AVT_WRITE_FN(AVT_WB, b,  8,  8)
AVT_WRITE_FN(AVT_WB, b, 16, 16)
AVT_WRITE_FN(AVT_WB, b, 24, 32)
AVT_WRITE_FN(AVT_WB, b, 32, 32)
AVT_WRITE_FN(AVT_WB, b, 64, 64)

AVT_WRITE_FN(AVT_WL, l,  8,  8)
AVT_WRITE_FN(AVT_WL, l, 16, 16)
AVT_WRITE_FN(AVT_WL, l, 24, 32)
AVT_WRITE_FN(AVT_WL, l, 32, 32)
AVT_WRITE_FN(AVT_WL, l, 64, 64)

#define AVT_WRITE_RATIONAL(en, n)                                        \
static inline void avt_bsw_rt##n(AVTBytestream *bs, const AVTRational r) \
{                                                                        \
    avt_assert1(((bs->ptr) + 8) <= bs->end);                             \
    union { int32_t _val; uint32_t val; } t[2];                          \
    t[0]._val = r.num;                                                   \
    t[1]._val = r.den;                                                   \
    en##32(bs->ptr, t[0].val);                                           \
    en##32(bs->ptr, t[1].val);                                           \
    bs->ptr += 8;                                                        \
}

AVT_WRITE_RATIONAL(AVT_WB, be)
AVT_WRITE_RATIONAL(AVT_WL, le)

/* Write padding */
static inline void avt_bsw_zpad(AVTBytestream *bs, const size_t len)
{
    avt_assert1(((bs->ptr) + len) <= bs->end);
    memset(bs->ptr, 0, len);
    bs->ptr += len;
}

static inline void avt_bsw_sbuf(AVTBytestream *bs, const char8_t *buf, const size_t len)
{
    avt_assert1(((bs->ptr) + len) <= bs->end);
    memcpy(bs->ptr, buf, len);
    bs->ptr += len;
}

static inline void avt_bsr_sbuf(AVTBytestream *bs, char8_t *buf, size_t len)
{
    len = AVT_MIN(len, bs->end - bs->ptr);
    memcpy(buf, bs->ptr, len);
    bs->ptr += len;
}

/* Write fixed-length zero-terminated string to bytestream */
static inline void avt_bsw_fstr(AVTBytestream *bs, const char *str, const size_t fixed_len)
{
    size_t lens = AVT_MIN(strlen(str), fixed_len);
    avt_assert1(((bs->ptr) + fixed_len) <= bs->end);
    memcpy(bs->ptr, str, lens);
    memset(bs->ptr, 0, fixed_len - lens);
    bs->ptr += fixed_len;
}

static inline void avt_bsr_fstr(AVTBytestream *bs, char *str, const size_t fixed_len)
{
    if ((bs->ptr + fixed_len) > bs->end)
        return;
    memcpy(str, bs->ptr, fixed_len);
    str[fixed_len] = '\0';
    bs->ptr += fixed_len;
}

static inline uint8_t *avt_bs_pos(AVTBytestream *bs)
{
    return bs->ptr;
}

static inline size_t avt_bs_offs(AVTBytestream *bs)
{
    return bs->ptr - bs->start;
}

static inline size_t avt_bs_left(AVTBytestream *bs)
{
    return bs->end - bs->ptr;
}

static inline void avt_bs_skip(AVTBytestream *bs, const size_t len)
{
    bs->ptr = AVT_MIN(bs->ptr + len, bs->end);
}

#define AVT_READ_FN(en, n, len, tlen)                                 \
static inline uint##tlen##_t avt_bsr_u ##len ##n (AVTBytestream *bs)  \
{                                                                     \
    if (((bs->ptr) + (len >> 3)) > bs->end)                           \
        return 0;                                                     \
    bs->ptr += len >> 3;                                              \
    return en##len(bs->ptr - (len >> 3));                             \
}                                                                     \
                                                                      \
static inline int##tlen##_t avt_bsr_i ##len ##n (AVTBytestream *bs)   \
{                                                                     \
    if (((bs->ptr) + (len >> 3)) > bs->end)                           \
        return 0;                                                     \
    union { int##tlen##_t ival; uint##tlen##_t uval; } t;             \
    t.uval = en##len(bs->ptr);                                        \
    bs->ptr += len >> 3;                                              \
    return t.ival;                                                    \
}

AVT_READ_FN(AVT_RB, b,  8,  8)
AVT_READ_FN(AVT_RB, b, 16, 16)
AVT_READ_FN(AVT_RB, b, 24, 32)
AVT_READ_FN(AVT_RB, b, 32, 32)
AVT_READ_FN(AVT_RB, b, 64, 64)

AVT_READ_FN(AVT_RL, l, 8, 8)
AVT_READ_FN(AVT_RL, l, 16, 16)
AVT_READ_FN(AVT_RL, l, 24, 32)
AVT_READ_FN(AVT_RL, l, 32, 32)
AVT_READ_FN(AVT_RL, l, 64, 64)

#define AVT_READ_RATIONAL(en, n)                                         \
static inline AVTRational avt_bsr_rt##n(AVTBytestream *bs)               \
{                                                                        \
    if (((bs->ptr) + 8) > bs->end)                                       \
        return (AVTRational){ 0, 0 };                                    \
    union { int32_t ival; uint32_t uval; } t[2];                         \
    t[0].uval = en##32(bs->ptr + 0);                                     \
    t[1].uval = en##32(bs->ptr + 4);                                     \
    bs->ptr += 8;                                                        \
    return (AVTRational){ t[0].ival, t[1].ival };                        \
}

AVT_READ_RATIONAL(AVT_RB, be)
AVT_READ_RATIONAL(AVT_RL, le)

#endif

