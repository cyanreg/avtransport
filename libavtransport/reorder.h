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

#ifndef LIBAVTRANSPORT_REORDER
#define LIBAVTRANSPORT_REORDER

#include "common.h"
#include "buffer.h"

typedef struct PQPktChain PQPktChain;

enum PQPktChainStatus {
    PQ_PKT_CHAIN_COMPLETE,
    PQ_PKT_CHAIN_INCOMPLETE,
};

int pq_reorder_init(AVTContext *ctx, size_t max_buffer);

int pq_reorder_push_pkt(AVTContext *ctx, AVTBuffer *data,
                        ptrdiff_t offset, uint32_t seq,
                        enum PQPacketType type);

enum PQPktChainStatus pq_chain_get_status(AVTContext *ctx, PQPktChain *chain);

AVTBuffer *pq_chain_get_buffer(AVTContext *ctx, PQPktChain *chain);

void pq_reorder_pop_chain(AVTContext *ctx, PQPktChain *chain);

void pq_reorder_uninit(AVTContext *ctx);

#endif
