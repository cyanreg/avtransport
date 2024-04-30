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
#include <stdlib.h>
#include <string.h>

#include <avtransport/avtransport.h>

#include "common.h"

int avt_init(AVTContext **ctx, AVTContextOptions *opts)
{
    AVTContext *tmp = malloc(sizeof(*tmp));
    if (!tmp)
        return AVT_ERROR(ENOMEM);

    if (opts)
        tmp->opts = *opts;

    *ctx = tmp;
    return 0;
}

void avt_close(AVTContext **ctx)
{
    if (ctx) {
        free(*ctx);
        *ctx = NULL;
    }
}

void avt_log(void *ctx, enum AVTLogLevel level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    FILE *std = level == AVT_LOG_ERROR ? stderr : stdout;

    bool with_color = true;
    bool colored_message = 0;
    if (with_color) {
        switch (level) {
        case AVT_LOG_FATAL:
            fprintf(std, "\033[1;031m");   colored_message = true; break;
        case AVT_LOG_ERROR:
            fprintf(std, "\033[1;031m");   colored_message = true; break;
        case AVT_LOG_WARN:
            fprintf(std, "\033[1;033m");   colored_message = true; break;
        case AVT_LOG_VERBOSE:
            fprintf(std, "\033[38;5;46m"); colored_message = true; break;
        case AVT_LOG_DEBUG:
            fprintf(std, "\033[38;5;34m"); colored_message = true; break;
        case AVT_LOG_TRACE:
            fprintf(std, "\033[38;5;28m"); colored_message = true; break;
        default:
            break;
        }
    }

    vfprintf(std, fmt, args);

    if (colored_message)
        fprintf(std, "\033[0m");

    va_end(args);
}
