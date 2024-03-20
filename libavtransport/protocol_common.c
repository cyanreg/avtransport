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

#include "protocol_common.h"

extern const AVTProtocol avt_protocol_stream;
extern const AVTProtocol avt_protocol_file;
extern const AVTProtocol avt_protocol_quic;
extern const AVTProtocol avt_protocol_pkt;

static const AVTProtocol *avt_protocol_list[AVT_PROTOCOL_MAX] = {
    [AVT_PROTOCOL_STREAM]       = &avt_protocol_stream,
    [AVT_PROTOCOL_UDP]          = &avt_protocol_stream,
    [AVT_PROTOCOL_UDP_LITE]     = &avt_protocol_stream,
    [AVT_PROTOCOL_FILE]         = &avt_protocol_file,
//    [AVT_PROTOCOL_QUIC]         = &avt_protocol_quic,
//    [AVT_PROTOCOL_CALLBACK_PKT] = &avt_protocol_pkt,
};

/* For connections to call */
int avt_protocol_init(AVTContext *ctx, const AVTProtocol **_p,
                      AVTProtocolCtx **p_ctx, AVTAddress *addr)
{
    const AVTProtocol *p = avt_protocol_list[addr->proto];
    if (!p) {
        avt_log(ctx, AVT_LOG_ERROR, "No support for protocol #%i\n",
                addr->proto);
        return AVT_ERROR(ENOTSUP);
    }

    int err = p->init(ctx, p_ctx, addr);
    if (err >= 0)
        *_p = p;

    return err;
}
