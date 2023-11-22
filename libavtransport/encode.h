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

#ifndef LIBAVTRANSPORT_ENCODE
#define LIBAVTRANSPORT_ENCODE

#include "output_internal.h"

/* Session start */
int avt_send_session_start(AVTOutput *out);

/* Time sync */
int avt_send_time_sync(AVTOutput *out);

/* Stream registration and data */
int avt_send_stream_register(AVTOutput *out, AVTStream *st);
int avt_send_stream_data(AVTOutput *out,
                         AVTStream *st, AVTPacket *pkt);

/* Generic data */
int avt_send_generic_data(AVTOutput *out,
                          AVTStream *st, AVTBuffer *data, int64_t pts,
                          uint32_t first_desd, uint32_t seg_desc);

/* LUT/ICC */
int avt_send_lut_data(AVTOutput *out,
                      AVTStream *st, int64_t pts);
int avt_send_icc_data(AVTOutput *out,
                      AVTStream *st, int64_t pts);

/* Video info/orientation */
int avt_send_video_info(AVTOutput *out,
                        AVTStream *st, int64_t pts);
int avt_send_video_orientation(AVTOutput *out,
                               AVTStream *st, int64_t pts);

#endif
