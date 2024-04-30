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

#include "output_internal.h"

/* Session start */
int avt_send_pkt_session_start(AVTSender *s);

/* Time sync */
int avt_send_pkt_time_sync(AVTSender *s);

/* Stream registration and data */
int avt_send_pkt_stream_register(AVTSender *s, AVTStream *st);
int avt_send_pkt_stream_data(AVTSender *s, AVTStream *st, AVTPacket *pkt);

/* Generic data */
int avt_send_pkt_generic_data(AVTSender *s,
                              AVTStream *st, AVTBuffer *data, int64_t pts,
                              uint32_t init_desc, uint32_t seg_desc);

/* LUT/ICC */
int avt_send_pkt_lut_data(AVTSender *s, AVTStream *st);
int avt_send_pkt_icc_data(AVTSender *s, AVTStream *st);

/* Video info/orientation */
int avt_send_pkt_video_info(AVTSender *s, AVTStream *st);
int avt_send_pkt_video_orientation(AVTSender *s, AVTStream *st);
