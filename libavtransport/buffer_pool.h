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

#ifndef AVTRANSPORT_BUFFER_POOL_H
#define AVTRANSPORT_BUFFER_POOL_H

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* A small LIFO of malloc'd scratch buffers, keyed by capacity. The acquire
 * path picks the smallest cached buffer that fits (best-fit), falling back to
 * a fresh malloc; release returns the buffer to the cache unless the slot cap
 * is reached, in which case the buffer is freed outright.
 *
 * The pool is reference counted and heap allocated so it can safely outlive
 * the context that owns it. A borrowed buffer's AVTBuffer free_cb points at
 * the pool (not at the owning context), and each outstanding borrow holds a
 * pool reference. The owner drops its reference on close; the pool itself is
 * freed only once the owner and every borrowed buffer are gone. This avoids a
 * use-after-free when, e.g., a send scheduler or a downstream consumer still
 * holds a payload buffer after the sender / protocol context is torn down.
 *
 * The freelist (slots/nb) is only mutated under the owning send / receive
 * context's serialization, as before; only the reference count is atomic, so
 * the last release can land on a different thread than the owner's close. */

#define AVT_BUF_POOL_MAX 8

typedef struct AVTBufPoolSlot {
    uint8_t *buf;
    size_t cap;
} AVTBufPoolSlot;

typedef struct AVTBufPool {
    AVTBufPoolSlot slots[AVT_BUF_POOL_MAX];
    int nb;
    /* Owner (1) plus one per outstanding borrowed buffer. */
    atomic_int refcount;
} AVTBufPool;

/* Drop a single pool reference, freeing the pool and any cached buffers once
 * the last reference (owner or borrow) is gone. */
static inline void avt_pool_unref(AVTBufPool *pool)
{
    if (atomic_fetch_sub_explicit(&pool->refcount, 1,
                                  memory_order_acq_rel) <= 1) {
        for (int i = 0; i < pool->nb; i++)
            free(pool->slots[i].buf);
        free(pool);
    }
}

/* Acquire a scratch buffer of at least `want` bytes. *ppool is lazily
 * allocated (and seeded with the owner reference) on first use. The returned
 * buffer carries one pool reference, released by avt_pool_release. Returns
 * NULL on allocation failure, leaving any lazily-created pool owned by *ppool
 * so the owner still frees it on drain. */
static inline uint8_t *avt_pool_acquire(AVTBufPool **ppool, size_t want,
                                        size_t *cap_out)
{
    AVTBufPool *pool = *ppool;
    if (!pool) {
        pool = calloc(1, sizeof(*pool));
        if (!pool)
            return NULL;
        atomic_init(&pool->refcount, 1); /* owner reference */
        *ppool = pool;
    }

    int best = -1;
    for (int i = 0; i < pool->nb; i++) {
        if (pool->slots[i].cap < want)
            continue;
        if (best < 0 || pool->slots[i].cap < pool->slots[best].cap)
            best = i;
    }

    uint8_t *buf;
    if (best >= 0) {
        buf = pool->slots[best].buf;
        *cap_out = pool->slots[best].cap;
        pool->slots[best] = pool->slots[pool->nb - 1];
        pool->nb--;
    } else {
        buf = malloc(want);
        if (!buf)
            return NULL;
        *cap_out = want;
    }

    atomic_fetch_add_explicit(&pool->refcount, 1, memory_order_relaxed);
    return buf;
}

/* AVTBuffer free_cb signature. `opaque` is the AVTBufPool the buffer was
 * acquired from. Re-caches the buffer while the pool is still in use, then
 * drops the borrow's reference. */
static inline void avt_pool_release(void *opaque, void *base, size_t len)
{
    AVTBufPool *pool = opaque;
    if (!pool) {
        free(base);
        return;
    }

    /* If anything other than this borrow still references the pool (the owner,
     * or another outstanding buffer), re-cache for reuse. If we are the last
     * reference the owner is already gone, so caching would be pointless -
     * avt_pool_unref will free everything below. */
    if (atomic_load_explicit(&pool->refcount, memory_order_relaxed) > 1 &&
        pool->nb < AVT_BUF_POOL_MAX) {
        pool->slots[pool->nb].buf = base;
        pool->slots[pool->nb].cap = len;
        pool->nb++;
    } else {
        free(base);
    }

    avt_pool_unref(pool);
}

/* Owner-side teardown: drop the owner reference. Outstanding borrowed buffers
 * keep the pool alive until each is released. *ppool is cleared. */
static inline void avt_pool_drain(AVTBufPool **ppool)
{
    AVTBufPool *pool = *ppool;
    if (!pool)
        return;
    *ppool = NULL;
    avt_pool_unref(pool);
}

#endif /* AVTRANSPORT_BUFFER_POOL_H */
