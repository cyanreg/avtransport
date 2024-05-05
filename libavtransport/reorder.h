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

#ifndef AVTRANSPORT_REORDER_H
#define AVTRANSPORT_REORDER_H

#include "common.h"
#include "buffer.h"

typedef struct AVTReorderPkt {
    union AVTPacketData pkt;
    AVTBuffer *pl;

    uint64_t recv_order;

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

enum AVTReorderChainType {
    /* Stream chain contains segments of a single stream data packet */
    AVT_REORDER_CHAIN_STREAM_DATA,

    /* Stream chain contains segments of parity data for a single stream data packet */
    AVT_REORDER_CHAIN_PARITY,

    /* Stream chain contains stream data segments of multiple streams, in order */
    AVT_REORDER_CHAIN_FEC_GROUP,

    /* Stream chain contains segments of parity data for a chain of stream data segments of multiple streams */
    AVT_REORDER_CHAIN_FEC_GROUP_PARITY,

    /* Stream chain contains the list of all currently buffered packets */
    AVT_REORDER_CHAIN_GLOBAL,
};

typedef struct AVTReorderChain {
    enum AVTReorderChainType type;
    AVTReorderPkt *start;
    AVTReorderPkt *top;

    uint32_t nb_packets; /* Received */
    uint32_t tot_nb_packets; /* Signalled */

    size_t payload_size; /* Received */
    size_t tot_payload_size; /* Signalled */

    uint16_t stream_id;
    uint16_t fec_group_id;

    uint16_t fecg_streams[16];
    int nb_fecg_streams;

    /*
     * type == AVT_REORDER_CHAIN_STREAM_DATA:
     * Pointer to a AVT_REORDER_CHAIN_PARITY chain that has
     * not been popped off yet, containing parity data not yet
     * fully finished.
     *
     * type == AVT_REORDER_CHAIN_PARITY:
     * Pointer to a poppped-off AVTReorderChain for the packet
     * being given redundancy by this parity data chain
     */
    struct AVTReorderChain *parity;

    /*
     * type == AVT_REORDER_CHAIN_FEC_GROUP:
     * Pointer to a AVT_REORDER_CHAIN_FEC_GROUP_PARITY chain that has
     * not been popped off yet, containing FEC data not yet
     * fully finished.
     *
     * type == AVT_REORDER_CHAIN_FEC_GROUP:
     * Pointer to a poppped-off AVTReorderChain for the packet
     * being given redundancy by this FEC data chain
     */
    struct AVTReorderChain *fec_group;

    /*
     * type == any
     * The global chain itself.
     * the start field may be NULL
     */
    struct AVTReorderChain *global;
} AVTReorderChain;

/* Main context */
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

/* Initialize a reorder buffer with a given max_size which
 * is the approximate bound of all packets and their payloads
 * contained within. */
int avt_reorder_init(AVTContext *ctx, AVTReorderBuffer *rb,
                     size_t max_size);

/* Push data to reorder buffer and let it figure everything out */
int avt_reorder_push(AVTContext *ctx, AVTReorderBuffer *rb,
                     union AVTPacketData pkt, AVTBuffer *pl);

/* Peek at the topmost, most recent stream data chain.
 * Call after push to understand if the packet ended up
 * somewhere useful yet */
int avt_reorder_peek_stream_data(AVTContext *ctx, AVTReorderBuffer *rb,
                                 AVTReorderChain **chain);

/* Pop a finished chain off the reorder buffer */
int avt_reorder_pop(AVTContext *ctx, AVTReorderChain **chain);
/* Mark chain as being done, letting its memory be reused */
int avt_reorder_done(AVTContext *ctx, AVTReorderChain *chain);

/* Free everything in all chains */
void avt_reorder_free(AVTContext *ctx, AVTReorderBuffer *rb);

#endif /* AVTRANSPORT_REORDER_H */
