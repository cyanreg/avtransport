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

#ifndef AVTRANSPORT_MERGER_H
#define AVTRANSPORT_MERGER_H

#include "packet_common.h"

typedef struct AVTMergerRange {
    uint32_t offset;
    uint32_t size;
} AVTMergerRange;

/* One merger per seq ID */
typedef struct AVTMerger {
    uint32_t target;
    bool active;
    bool p_avail; /* If header is available */
    uint8_t hdr_mask; /* 1 bit per 32-bits, 7th bit first, 0th bit last */
    uint8_t _padding_;

    /* Data */
    AVTPktd p;
    uint32_t pkt_len_track;
    uint32_t target_tot_len;

    /* Parity */
    AVTBuffer parity;
    uint32_t pkt_parity_len_track;
    uint32_t parity_tot_len;

    /* Data ranges */
    AVTMergerRange *ranges;
    uint32_t nb_ranges;
    uint32_t ranges_allocated;

    /* Parity ranges */
    AVTMergerRange *parity_ranges;
    uint32_t nb_parity_ranges;
    uint32_t parity_ranges_allocated;
} AVTMerger;

int avt_pkt_merge_seg(void *log_ctx, AVTMerger *m, AVTPktd *p);

#endif /* AVTRANSPORT_MERGER_H */
