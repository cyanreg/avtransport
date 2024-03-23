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

#ifndef AVTRANSPORT_ATTRIBUTES_H
#define AVTRANSPORT_ATTRIBUTES_H

#include "config.h"

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#ifdef __GNUC__
#define ATTR_ALIAS __attribute__((may_alias))
#define COLD __attribute__((cold))
#else
#define ATTR_ALIAS
#define COLD
#endif

#ifdef _MSC_VER
#define ALWAYS_INLINE __forceinline
#else
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#endif

/* Define tables with EXTERN to enable the compiler to optimize them better */
#if (defined(__ELF__) || defined(__MACH__) || (defined(_WIN32) && defined(__clang__))) && __has_attribute(visibility)
#define EXTERN extern __attribute__((visibility("hidden")))
#else
#define EXTERN extern
#endif

#if defined(NDEBUG) && (defined(__GNUC__) || defined(__clang__))
#undef assert
#define assert(x) do { if (!(x)) __builtin_unreachable(); } while (0)
#elif defined(NDEBUG) && defined(_MSC_VER)
#undef assert
#define assert __assume
#endif

#define avt_assert0(cond) assert(cond)

#if CONFIG_ASSERT_LEVEL > 0
#define avt_assert1(cond) assert(cond)
#else
#define avt_assert1(cond)
#endif

#if CONFIG_ASSERT_LEVEL > 1
#define avt_assert2(cond) assert(cond)
#else
#define avt_assert2(cond)
#endif

#ifdef _MSC_VER
#define PACKED(...) __pragma(pack(push, 1)) __VA_ARGS__ __pragma(pack(pop))
#else
#define PACKED(...) __VA_ARGS__ __attribute__((__packed__))
#endif

#endif /* AVTRANSPORT_ATTRIBUTES_H */
