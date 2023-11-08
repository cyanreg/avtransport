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

#ifndef LIBAVTRANSPORT_REORDER
#define LIBAVTRANSPORT_REORDER

#include "common.h"
#include "buffer.h"

enum AVTReorderChainType {
    AVT_REORDER_CHAIN_SEGMENT,
    AVT_REORDER_CHAIN_PARITY,
    AVT_REORDER_CHAIN_FEC_GROUP,
    AVT_REORDER_CHAIN_GLOBAL,
};

typedef struct AVTReorderPkt {
    uint8_t header[384];
    AVTBuffer *payload;
    enum AVTPktDescriptors desc;
    uint64_t recv_order;
    uint64_t seq;

    /* Segment */
    struct AVTReorderPkt *prev;
    struct AVTReorderPkt *next;

    /* Parity */
    struct AVTReorderPkt *parity_prev;
    struct AVTReorderPkt *parity_next;

    /* FEC group */
    struct AVTReorderPkt *fecg_prev;
    struct AVTReorderPkt *fecg_next;

    /* Global */
    struct AVTReorderPkt *global_prev;
    struct AVTReorderPkt *global_next;
} AVTReorderPkt;

typedef struct AVTReorderChain {
    enum AVTReorderChainType type;
    AVTReorderPkt *start;

    uint32_t nb_packets; /* Received */
    uint32_t tot_nb_packets; /* Signalled */

    size_t payload_size; /* Received */
    size_t tot_payload_size; /* Signalled */

    uint16_t stream_id;
    uint16_t fec_group_id;

    uint16_t fecg_streams[16];
    int nb_fecg_streams;

    struct AVTReorderChain *parity;
    struct AVTReorderChain *fecg;
} AVTReorderChain;

typedef struct AVTReorderBuffer {
    /* Buffer for all packet structs */
    AVTReorderPkt *pkt;
    uint32_t nb_pkt;
    uint32_t nb_pkt_allocated;

    /* Buffer for all chains */
    AVTReorderChain *chain;
    uint32_t nb_chains;
    uint32_t nb_chains_allocated;

    AVTReorderChain *global_chain;

    AVTReorderChain **segment_chains;
    uint32_t nb_segment_chains;
    uint32_t nb_segment_chains_allocated;

    AVTReorderChain **fecg_chains;
    uint32_t nb_fecg_chains;
    uint32_t nb_fecg_chains_allocated;

    size_t max_global_size;
} AVTReorderBuffer;

int avt_reorder_init(AVTContext *ctx, AVTReorderBuffer *rb,
                     size_t max_size, uint32_t patience);

int avt_reorder_push(AVTContext *ctx, AVTReorderBuffer *rb, AVTBuffer *data,
                     uint32_t seq, enum AVTPktDescriptors desc);

int avt_reorder_pop(AVTContext *ctx, AVTReorderChain **chain);

void avt_reorder_free(AVTContext *ctx, AVTReorderBuffer *rb);

#endif
