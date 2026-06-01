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

#include <avtransport/avtransport.h>
#include "packet_decode.h"
#include "protocol_common.h"
#include "mem.h"

extern const AVTProtocol avt_protocol_datagram;
extern const AVTProtocol avt_protocol_stream;
extern const AVTProtocol avt_protocol_pkt;

#ifdef CONFIG_HAVE_OPENSSL
extern const AVTProtocol avt_protocol_quic;
#endif

static const AVTProtocol *avt_protocol_list[AVT_PROTOCOL_MAX] = {
    [AVT_PROTOCOL_DATAGRAM]     = &avt_protocol_datagram,
    [AVT_PROTOCOL_UDP]          = &avt_protocol_datagram,
    [AVT_PROTOCOL_STREAM]       = &avt_protocol_stream,
    [AVT_PROTOCOL_FILE]         = &avt_protocol_stream,
#ifdef CONFIG_HAVE_OPENSSL
    [AVT_PROTOCOL_QUIC]         = &avt_protocol_quic,
#endif
//    [AVT_PROTOCOL_CALLBACK_PKT] = &avt_protocol_pkt,
};

/* For connections to call */
int avt_protocol_init(AVTContext *ctx, const AVTProtocol **_p,
                      AVTProtocolCtx **p_ctx, AVTAddress *addr,
                      const AVTIO *io, AVTIOCtx *io_ctx, AVTProtocolOpts *opts)
{
    const AVTProtocol *p = avt_protocol_list[addr->proto];
    if (!p) {
        avt_log(ctx, AVT_LOG_ERROR, "No support for protocol #%i\n",
                addr->proto);
        return AVT_ERROR(ENOTSUP);
    }

    int err = p->init(ctx, p_ctx, addr, io, io_ctx, opts);
    if (err >= 0)
        *_p = p;

    return err;
}

int avt_index_list_parse(AVTIndexContext *ic, AVTBytestream *bs,
                         AVTStreamIndex *pkt)
{
    for (auto i = 0; i < pkt->nb_indices; i++) {
        auto dst = ic->nb_index;
        if (ic->nb_index >= ic->nb_index_max) {
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
