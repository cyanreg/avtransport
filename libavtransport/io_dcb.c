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

#include <stdlib.h>

#include "io_common.h"
#include "attributes.h"

struct AVTIOCtx {
    AVTCallbacksData cb;
    int64_t rpos;
};

static int dcb_close(AVTIOCtx **_io)
{
    AVTIOCtx *io = *_io;
    free(io);
    *_io = NULL;
    return 0;
}

static COLD int dcb_init(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    AVTIOCtx *io = malloc(sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    io->cb = addr->dcb;
    io->rpos = 0;

    *_io = io;
    return 0;
}

static int64_t dcb_max_pkt_len(AVTIOCtx *io)
{
    return UINT32_MAX;
}

static int64_t dcb_seek(AVTIOCtx *io, int64_t off)
{
    return (io->rpos = off);
}

static int64_t dcb_input(AVTIOCtx *io, AVTBuffer **buf, size_t len,
                         int64_t timeout)
{
    int64_t ret = io->cb.read(io->cb.opaque, buf, len, io->rpos);
    if (ret < 0)
        return ret;
    return (io->rpos = ret);
}

static int64_t dcb_write_pkt(AVTIOCtx *io, AVTPktd *p, int64_t timeout)
{
    return io->cb.write(io->cb.opaque, p->hdr, p->hdr_len, &p->pl);
}

static int64_t dcb_write_vec(AVTIOCtx *io, AVTPktd *iov, uint32_t nb_iov,
                             int64_t timeout)
{
    int64_t ret;
    for (int i = 0; i < nb_iov; i++) {
        ret = io->cb.write(io->cb.opaque, iov[i].hdr, iov[i].hdr_len, &iov[i].pl);
        if (ret < 0)
            return ret;
    }
    return 0;
}

const AVTIO avt_io_dcb = {
    .name = "dcb",
    .type = AVT_IO_CALLBACK,
    .init = dcb_init,
    .get_max_pkt_len = dcb_max_pkt_len,
    .read_input = dcb_input,
    .write_vec = dcb_write_vec,
    .write_pkt = dcb_write_pkt,
    .rewrite = NULL,
    .seek = dcb_seek,
    .close = dcb_close,
};
