/*
 * Copyright © 2022 Lynne
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the “Software”), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef LIBQPROTO_UTILS
#define LIBQPROTO_UTILS

#include "../config.h"

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

#define PQ_WBL(p, l, v) \
    do {                \
        PQ_WB##l(p, v); \
        p += l >> 3;    \
    } while (0)

#define PQ_WLL(p, l, v) \
    do {                \
        PQ_WL##l(p, v); \
        p += l >> 3;    \
    } while (0)

#endif
