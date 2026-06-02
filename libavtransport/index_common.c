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

/* Stream-index helper. Lives separately from protocol_common.c so callers
 * (notably protocol_stream and the seek unit test) can link it without
 * pulling in the protocol_list table, which references every transport
 * singleton (datagram / quic / ...). */

#include <avtransport/avtransport.h>
#include "packet_decode.h"
#include "protocol_common.h"
#include "mem.h"

int avt_index_list_config(AVTIndexContext *ic, uint64_t nb_index_max)
{
    ic->nb_index_max = nb_index_max;
    return 0;
}

int avt_index_list_parse(AVTIndexContext *ic, AVTBytestream *bs,
                         AVTStreamIndex *pkt)
{
    for (auto i = 0; i < pkt->nb_indices; i++) {
        auto dst = ic->nb_index;
        /* A configured cap turns the store into a ring buffer; without one
         * (nb_index_max == 0) grow indefinitely rather than dividing by zero. */
        if (ic->nb_index_max && ic->nb_index >= ic->nb_index_max) {
            dst = ic->nb_index_total % ic->nb_index_max;
        } else if ((ic->nb_index + 1) > ic->nb_alloc_index) {
            AVTIndexEntry *tmp = avt_reallocarray(ic->index,
                                                  ic->nb_alloc_index + 1,
                                                  sizeof(*tmp));
            if (!tmp)
                return AVT_ERROR(ENOMEM);

            ic->index = tmp;
            ic->nb_index++;
            ic->nb_alloc_index++;
        }

        avt_decode_index_entry(bs, &ic->index[dst]);
        ic->nb_index_total++;
    }

    return 0;
}
