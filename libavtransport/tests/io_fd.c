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

#include <stdio.h>

#include <avtransport/avtransport.h>
#include "io_common.h"

#include "file_io_common.h"

extern const AVTIO avt_io_fd_path;

int main(void)
{
    int64_t ret;

    /* Open context */
    AVTContext *avt;
    ret = avt_init(&avt, NULL);
    if (ret < 0)
        return AVT_ERROR(ret);

    /* Open io context */
    const AVTIO *io = &avt_io_fd_path;
    AVTIOCtx *io_ctx;
    AVTAddress addr = { .path = "io_fd_test.avt" };

    ret = io->init(avt, &io_ctx, &addr);
    if (ret < 0) {
        printf("Unable to create test file: %s\n", addr.path);
        avt_close(&avt);
        return AVT_ERROR(ret);
    }

    ret = file_io_test(avt, io, io_ctx);

    if (ret)
        io->close(&io_ctx);
    else
        ret = io->close(&io_ctx);
    avt_close(&avt);
    return AVT_ERROR(ret);
}
