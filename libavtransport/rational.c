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

#if __has_include("stdbit.h")
#include <stdbit.h>
#endif
#include <limits.h>
#include <avtransport/rational.h>

#include "utils_internal.h"

#if defined(__GNUC__) && !defined(__clang__)
typedef __int128 rational_int;
#else
typedef _BitInt(128) rational_int;
#endif

static inline rational_int rescale_rnd(rational_int a,
                                       rational_int b,
                                       rational_int c,
                                       enum AVTRoundingMode rnd)
{
    avt_assert1(c >  0);
    avt_assert1(b >= 0);

    rational_int r = 0;

    if (c <= 0 || b < 0 || !((unsigned)(rnd) <= 5 && (rnd) != 4))
        return INT64_MIN;

    if (a < 0)
        return -(uint64_t)rescale_rnd(-AVT_MAX(a, -INT64_MAX), b, c, rnd ^ ((rnd >> 1) & 1));

    if (rnd == AVT_ROUND_NEAR_INF)
        r = c / 2;
    else if (rnd & 1)
        r = c - 1;

    return (((a * b) + r) / c);
}

int64_t avt_rescale_rnd(int64_t s_a, int64_t s_b, int64_t s_c, enum AVTRoundingMode rnd)
{
    rational_int a = s_a;
    rational_int b = s_b;
    rational_int c = s_c;
    return rescale_rnd(a, b, c, rnd);
}

int64_t avt_rescale(int64_t a, int64_t b, int64_t c)
{
    return rescale_rnd(a, b, c, AVT_ROUND_NEAR_INF);
}

int64_t avt_rescale_rational_rnd(int64_t a, AVTRational b, AVTRational c,
                                 enum AVTRoundingMode rnd)
{
    rational_int s_b = b.num * (rational_int)c.den;
    rational_int s_c = c.num * (rational_int)b.den;
    return avt_rescale_rnd(a, s_b, s_c, rnd);
}

int64_t avt_rescale_rational(int64_t a, AVTRational b, AVTRational c)
{
    return avt_rescale_rational_rnd(a, b, c, AVT_ROUND_NEAR_INF);
}

int avt_compare_ts(int64_t ts_a, AVTRational tb_a,
                   int64_t ts_b, AVTRational tb_b)
{
    rational_int a = tb_a.num * (rational_int)tb_b.den;
    rational_int b = tb_b.num * (rational_int)tb_a.den;

    /* Fast path - if no overflows are possible */
    if ((llabs(ts_a) | a | llabs(ts_b) | b) <= INT_MAX)
        return (ts_a * a > ts_b * b) - (ts_a * a < ts_b * b);

    if (rescale_rnd(ts_a, a, b, AVT_ROUND_DOWN) < ts_b)
        return -1;
    if (rescale_rnd(ts_b, b, a, AVT_ROUND_DOWN) < ts_a)
        return 1;
    return 0;
}

static inline int64_t gcd_ab(int64_t a, int64_t b)
{
    int za, zb, k;
    int64_t u, v;
    if (a == 0)
        return b;
    else if (b == 0)
        return a;
#if __has_include("stdbit.h")
    za = stdc_trailing_zeros(a);
    zb = stdc_trailing_zeros(b);
#else
    za = __builtin_ctz(a);
    zb = __builtin_ctz(b);
#endif
    k  = AVT_MIN(za, zb);
    u = llabs(a >> za);
    v = llabs(b >> zb);
    while (u != v) {
        if (u > v) {
            int64_t tmp = v;
            v = u;
            u = tmp;
        }
        v -= u;
#if __has_include("stdbit.h")
        v >>= stdc_trailing_zeros(v);
#else
        v >>= __builtin_ctz(v);
#endif
    }
    return (uint64_t)u << k;
}

/* Reduce a rational */
static AVTRational reduce_rational(AVTRational in, int64_t max)
{
    AVTRational a0 = { 0, 1 }, a1 = { 1, 0 };
    int sign = (in.num < 0) ^ (in.den < 0);

    int64_t num = in.num;
    int64_t den = in.den;

    /* Fast path - common divisor */
    int64_t gcd = gcd_ab(llabs(num), llabs(den));
    if (gcd) {
        num = llabs(num) / gcd;
        den = llabs(den) / gcd;
    }
    if (num <= max && den <= max) {
        a1 = (AVTRational){ num, den };
        den = 0;
    }

    while (den) {
        uint64_t x        = num / den;
        int64_t next_den  = num - den * x;
        int64_t a2n       = x * a1.num + a0.num;
        int64_t a2d       = x * a1.den + a0.den;

        if (a2n > max || a2d > max) {
            if (a1.num) x =          (max - a0.num) / a1.num;
            if (a1.den) x = AVT_MIN(x, (max - a0.den) / a1.den);

            if (den * (2 * x * a1.den + a0.den) > num * a1.den)
                a1 = (AVTRational) { x * a1.num + a0.num, x * a1.den + a0.den };
            break;
        }

        a0  = a1;
        a1  = (AVTRational) { a2n, a2d };
        num = den;
        den = next_den;
    }
    avt_assert2(gcd_ab(a1.num, a1.den) <= 1U);
    avt_assert2(a1.num <= max && a1.den <= max);

    return (AVTRational) {
        .num = sign ? -a1.num : a1.num,
        .den = a1.den,
    };
}

AVTRational avt_make_rational(int num, int den)
{
    return reduce_rational((AVTRational) { num, den }, INT_MAX);
}

static AVTRational mul_rational(AVTRational b, AVTRational c)
{
    return reduce_rational((AVTRational) {
        b.num * (int64_t)c.num,
        b.den * (int64_t)c.den,
    }, INT_MAX);
}

int64_t avt_add_stable(AVTRational ts_tb, int64_t ts,
                       AVTRational inc_tb, int64_t inc)
{
    int64_t m, d;

    if (inc != 1)
        inc_tb = mul_rational(inc_tb, (AVTRational) {inc, 1});

    m = inc_tb.num * (int64_t)ts_tb.den;
    d = inc_tb.den * (int64_t)ts_tb.num;

    if (m % d == 0 && ts <= INT64_MAX - m / d)
        return ts + m / d;
    if (m < d)
        return ts;

    int64_t old = avt_rescale_rational(ts, ts_tb, inc_tb);
    int64_t old_ts = avt_rescale_rational(old, inc_tb, ts_tb);

    if (old == INT64_MAX)
        return ts;

    rational_int a = avt_rescale_rational(old + 1, inc_tb, ts_tb);
    rational_int b = ts - old_ts;
    a += b;
    if (a < INT64_MIN)
        a = INT64_MIN;
    if (a > INT64_MAX)
        a = INT64_MAX;

    return a;
}
