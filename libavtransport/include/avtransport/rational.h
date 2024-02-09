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

#ifndef AVTRANSPORT_RATIONAL_H
#define AVTRANSPORT_RATIONAL_H

#include <stdint.h>

/* Rational data type structure. */
typedef struct AVTRational {
    int num;
    int den;
} AVTRational;

/* Rounding methods */
enum AVTRoundingMode {
    AVT_ROUND_ZERO = 0, /* Round toward zero */
    AVT_ROUND_INF = 1, /* Round away from zero */
    AVT_ROUND_DOWN = 2, /* Round toward -infinity */
    AVT_ROUND_UP = 3, /* Round toward +infinity */
    AVT_ROUND_NEAR_INF = 5, /* Round to nearest and halfway cases away from zero */
};

/* Reduces num/den by their common factors and returns a rational structure */
AVTRational avt_make_rational(int num, int den);

/* Perform "a * b / c" in an overflow-free way, with a specific rounding */
int64_t avt_rescale_rnd(int64_t a, int64_t b, int64_t c, enum AVTRoundingMode rnd);
int64_t avt_rescale_rational_rnd(int64_t a, AVTRational b, AVTRational c,
                                 enum AVTRoundingMode rnd);

/* Same as above, but with rnd equal to AVT_ROUND_NEAR_INF */
int64_t avt_rescale(int64_t a, int64_t b, int64_t c);
int64_t avt_rescale_rational(int64_t a, AVTRational b, AVTRational c);

/**
 * Compare two timestamps "ts_a" and "ts_b",
 * which have timebases of "tb_a" and "tb_b", respectively.
 * Returns: -1 if ts_a is before ts_b
 *           0 if ts_a is equal to ts_b
 *          +1 if ts_a is after ts_b
 */
int avt_compare_ts(int64_t ts_a, AVTRational tb_a, int64_t ts_b, AVTRational tb_b);

/* Adds two rational numbers. Guarantees reversibility. */
int64_t avt_add_stable(AVTRational ts_tb, int64_t ts,
                       AVTRational inc_tb, int64_t inc);

/* Convert a rational to a float */
static inline double avt_r2d(AVTRational a)
{
    return a.num / (double) a.den;
}

#endif /* AVTRANSPORT_RATIONAL_H */
