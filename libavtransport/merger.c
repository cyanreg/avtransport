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
#include <string.h>
#include <inttypes.h>
#include <avtransport/avtransport.h>

#include "merger.h"
#include "fec.h"

#include "packet_decode.h"
#include "bytestream.h"
#include "buffer.h"
#include "utils_packet.h"
#include "mem.h"

/* Insert a received byte range [seg_off, seg_off + seg_size) into a merger's
 * sorted, disjoint, non-adjacent range list, coalescing it with any ranges it
 * now touches. validate_packet() guarantees segments never partially overlap.
 *
 * The same routine maintains the payload ranges and, with is_parity set, the
 * stream-data-parity ranges. */
static inline int fill_ranges(AVTMerger *m, bool is_parity,
                              uint32_t seg_off, uint32_t seg_size)
{
    AVTMergerRange **ranges_dst;
    uint32_t *nb_ranges_dst;
    uint32_t *ranges_allocated_dst;

    if (!is_parity) {
        ranges_dst = &m->ranges;
        nb_ranges_dst = &m->nb_ranges;
        ranges_allocated_dst = &m->ranges_allocated;
    } else {
        ranges_dst = &m->parity_ranges;
        nb_ranges_dst = &m->nb_parity_ranges;
        ranges_allocated_dst = &m->parity_ranges_allocated;
    }

    AVTMergerRange *ranges = *ranges_dst;
    uint32_t nb_ranges = *nb_ranges_dst;
    const uint32_t seg_end = seg_off + seg_size;

    uint32_t new_start = seg_off;
    uint32_t new_end = seg_end;

    /* Find the contiguous span of existing ranges the new one touches or
     * adjoins, growing the merged extent to cover them all. */
    uint32_t first = nb_ranges, last = 0;
    for (uint32_t i = 0; i < nb_ranges; i++) {
        const uint32_t r_start = ranges[i].offset;
        const uint32_t r_end = r_start + ranges[i].size;
        if (r_end < seg_off || r_start > seg_end)
            continue;
        if (i < first)
            first = i;
        last = i + 1;
        if (r_start < new_start)
            new_start = r_start;
        if (r_end > new_end)
            new_end = r_end;
    }

    if (first >= last) {
        /* Disjoint from every existing range: a plain sorted insertion. */
        uint32_t pos = 0;
        while (pos < nb_ranges && ranges[pos].offset < new_start)
            pos++;

        if ((nb_ranges + 1) > *ranges_allocated_dst) {
            uint32_t na = (nb_ranges + 1) << 1;
            AVTMergerRange *tmp = avt_reallocarray(ranges, na, sizeof(*tmp));
            if (!tmp)
                return AVT_ERROR(ENOMEM);
            ranges = tmp;
            *ranges_dst = ranges;
            *ranges_allocated_dst = na;
        }

        memmove(&ranges[pos + 1], &ranges[pos],
                (nb_ranges - pos) * sizeof(*ranges));
        ranges[pos] = (AVTMergerRange){ new_start, new_end - new_start };
        nb_ranges++;
    } else {
        /* Replace the touched span [first, last) with one merged range. */
        ranges[first] = (AVTMergerRange){ new_start, new_end - new_start };
        memmove(&ranges[first + 1], &ranges[last],
                (nb_ranges - last) * sizeof(*ranges));
        nb_ranges -= (last - first) - 1;
    }

    *nb_ranges_dst = nb_ranges;
    return 0;
}

/* Attempt RaptorQ recovery of a merger's missing payload bytes from the
 * stream-data-parity collected so far. On success the recovered payload is
 * left in m->p.pl and 0 is returned; otherwise a negative AVT_ERROR(). */
static int merger_fec_recover(void *log_ctx, AVTMerger *m)
{
    const size_t ndata = (m->target_tot_len + AVT_FEC_SYMBOL_SIZE - 1) /
                         AVT_FEC_SYMBOL_SIZE;
    const size_t nrep = m->parity_tot_len / AVT_FEC_SYMBOL_SIZE;

    uint8_t *data_present = calloc(1, ndata ? ndata : 1);
    uint8_t *parity_present = calloc(1, nrep ? nrep : 1);
    if (!data_present || !parity_present) {
        free(data_present);
        free(parity_present);
        return AVT_ERROR(ENOMEM);
    }

    /* A symbol counts as present when a received byte range fully covers it. */
    for (uint32_t i = 0; i < m->nb_ranges; i++) {
        const uint32_t s = m->ranges[i].offset;
        const uint32_t e = s + m->ranges[i].size;
        for (uint32_t y = (s + AVT_FEC_SYMBOL_SIZE - 1) / AVT_FEC_SYMBOL_SIZE;
             y < ndata && (y + 1) * AVT_FEC_SYMBOL_SIZE <= e; y++)
            data_present[y] = 1;
    }
    for (uint32_t i = 0; i < m->nb_parity_ranges; i++) {
        const uint32_t s = m->parity_ranges[i].offset;
        const uint32_t e = s + m->parity_ranges[i].size;
        for (uint32_t y = (s + AVT_FEC_SYMBOL_SIZE - 1) / AVT_FEC_SYMBOL_SIZE;
             y < nrep && (y + 1) * AVT_FEC_SYMBOL_SIZE <= e; y++)
            parity_present[y] = 1;
    }

    uint8_t *data = avt_buffer_get_data(&m->p.pl, NULL);
    uint8_t *parity = avt_buffer_get_data(&m->parity, NULL);

    int ret = avt_fec_recover(log_ctx, data, m->target_tot_len, data_present,
                              parity, m->parity_tot_len, parity_present);

    free(data_present);
    free(parity_present);
    return ret;
}

static inline int fill_phantom_header(void *log_ctx, AVTMerger *m,
                                      AVTPktd *p, bool is_parity)
{
    int hdr_part = p->pkt.seq % 7;

    if (!is_parity)
        memcpy(&m->p.hdr[4*hdr_part], p->pkt.generic_segment.header_7, 4);
    else
        memcpy(&m->p.hdr[4*hdr_part], p->pkt.generic_parity.header_7, 4);

    m->hdr_mask |= 1 << (6 - hdr_part);

    /* We're not ready to reconstruct the header yet */
    if (!(m->hdr_mask == 0x7F))
        return 0;

    uint16_t tgt_desc = AVT_RB16(&m->p.hdr[0]);

    if ((tgt_desc == (AVT_PKT_TIME_SYNC & 0xFF00)) ||
        (tgt_desc == (AVT_PKT_STREAM_DATA & 0xFF00)))
        tgt_desc &= 0xFF00;

    AVTBytestream bs = avt_bs_init(m->p.hdr, sizeof(m->p.hdr));

    // TODO more sanity checking

    switch (tgt_desc) {
    case AVT_PKT_STREAM_DATA & ~(AVT_PKT_FLAG_LSB_BITMASK):
        avt_decode_stream_data(&bs, &m->p.pkt.stream_data);
        break;
    case AVT_PKT_LUT_ICC:
        avt_decode_lut_icc(&bs, &m->p.pkt.lut_icc);
        /* name will be missing */
        break;
    case AVT_PKT_FONT_DATA:
        avt_decode_font_data(&bs, &m->p.pkt.font_data);
        /* name will be missing */
        break;
    case AVT_PKT_USER_DATA:
        avt_decode_user_data(&bs, &m->p.pkt.user_data);
        break;
    case AVT_PKT_STREAM_CONFIG: [[fallthrough]];
    case AVT_PKT_METADATA:
        avt_decode_generic_data(&bs, &m->p.pkt.generic_data);
        break;
    default:
        /* Any other packets may not be segmented. Perhaps the top part of the
         * reconstructed segment is faulty. Invalidate it. */
        m->hdr_mask &= 0x3F;

        /* If the current packet contributed this part, skip copying the
         * payload, as it's likely wrong. Otherwise continue. */
        return hdr_part == 0 ? AVT_ERROR(EAGAIN) : 0;
    }

    m->p_avail = true;

    return 0;
}

static int validate_packet(void *log_ctx, AVTMerger *m, AVTPktd *p, int series,
                           uint32_t seg_off, uint32_t seg_size,
                           uint32_t tot_size, bool is_parity)
{
    /* Check for length mismatch, comparing against the payload total for
     * data packets and the parity total for parity packets. */
    const uint32_t expect_tot = is_parity ? m->parity_tot_len
                                          : m->target_tot_len;
    if (expect_tot && tot_size && (tot_size != expect_tot))
        return AVT_ERROR(EINVAL);

    /* Check for segmentation issues */
    if (series == 0 && m->target == p->pkt.seq) {
        avt_log(log_ctx, AVT_LOG_DEBUG, "Header packet for %" PRIu64 " indicates "
                                        "no segmentation, but segments received",
                p->pkt.seq);
        return AVT_ERROR(EINVAL);
    }

    /* Check for phantom header mismatch. Only segment and parity packets
     * (series < 0) carry a header_7 slice; a series == 1 packet is the
     * real header itself and has no such field to validate. */
    const uint32_t hdr_part = p->pkt.seq % 7;
    if (series < 0 && (m->hdr_mask & (1 << (6 - hdr_part)))) {
        const uint8_t *hdr_part_data = &m->p.hdr[4*hdr_part];
        const uint8_t *pkt_hdr_7 = is_parity ? p->pkt.generic_parity.header_7
                                             : p->pkt.generic_segment.header_7;
        for (auto i = 0; i < 4; i++)
            if (hdr_part_data[i] != pkt_hdr_7[i])
                return AVT_ERROR(EINVAL);
    }

    /* Check for overlaps */
    AVTMergerRange *ranges;
    uint32_t nb_ranges;
    if (!is_parity) {
        ranges = m->ranges;
        nb_ranges = m->nb_ranges;
    } else {
        ranges = m->parity_ranges;
        nb_ranges = m->nb_parity_ranges;
    }

    for (auto i = 0; i < nb_ranges; i++) {
        AVTMergerRange *r = &ranges[i];
        /* Check for overlaps first */
        if (r->offset == seg_off && r->size == seg_size) {
            return AVT_ERROR(EAGAIN);
        } else if ((r->offset + r->size) < seg_off &&
            r->offset >= seg_off) {
            /* Segment's start overlaps with another segment */
            return AVT_ERROR(EINVAL);
        } else if ((seg_off + seg_size) > r->offset &&
            (seg_off + seg_size) <= (r->offset + r->size)) {
            /* Segment's end overlaps with another segment */
            return AVT_ERROR(EINVAL);
        }
    }

    return 0;
}

int avt_pkt_merge_seg(void *log_ctx, AVTMerger *m, AVTPktd *p)
{
    int ret;

    bool is_parity;
    uint32_t seg_off, seg_size, tot_size;
    int series = avt_packet_series(p, &is_parity, &seg_off, &seg_size, &tot_size);

    /* Packet needs nothing else */
    if (!series) {

#if 0
        /* error resilience wip:
         * If we have received two or more segments, and yet we
         * get the main data packet claiming it isn't segmented, consider
         * a corrupt bit happened and accept it. If it overlaps, it'll get
         * rejected during validate_packet(). */
        if (m->nb_tgt_packets >= 2 && m->target == p->pkt.seq) {
            int hdr_match = 0;
            /* Intentionally iterating from 1 to 7,
             * as 7 is guaranteed to be a mismatch */
            for (int i = 1; i < 7; i++) {
                if (((m->hdr_mask >> i) & 1) &&
                    memcmp(&m->p.hdr[4*i], p->hdr, 4))
                    hdr_match++;
            }

            if (!hdr_match) {
                avt_pkt_merge_done(m);
                return 0;
            }

            series = 1;
        } else

#endif

        {
            /* Direct output - just copy the input */
            memcpy(&m->p, p, sizeof(*p));
            m->hdr_mask = 0x7F;
            m->active = 1;
            m->p_avail = 1;
            return 0;
        }
    }

    size_t src_size;
    uint8_t *src = avt_buffer_get_data(&p->pl, &src_size);
    if (src_size != seg_size) {
        avt_log(log_ctx, AVT_LOG_ERROR, "Mismatch between signalled payload size "
                                        "%u and actual payload %zu in pkt %" PRIu64 "\n",
                seg_size, src_size, p->pkt.seq);
        return AVT_ERROR(EINVAL);
    }

    if (m->active) {
        uint32_t target;
        if (series == 1)
            target = p->pkt.seq;
        else if (is_parity)
            target = p->pkt.generic_parity.target_seq;
        else
            target = p->pkt.generic_segment.target_seq;

        if (target != m->target)
            return AVT_ERROR(EBUSY);
    } else {
        m->hdr_mask = 0x0;
        m->nb_ranges = 0;
        m->nb_parity_ranges = 0;
        m->pkt_len_track = 0;
        m->pkt_parity_len_track = 0;
        /* A parity packet's signalled total describes the parity data, not
         * the payload; only a data packet establishes target_tot_len. */
        m->target_tot_len = is_parity ? 0 : tot_size;
        m->parity_tot_len = is_parity ? tot_size : 0;
        m->nb_tgt_packets = !!tot_size;
        m->p_avail = false;
        m->last = p->pkt.seq;
        avt_buffer_quick_unref(&m->parity);

        if (series > 0) {
            /* Starting with a segment, not the actual start */
            m->p = *p;
            m->p_avail = true;
            m->hdr_mask = 0x7F;
            m->target = p->pkt.seq;
        } else if (!is_parity) {
            /* Starting with a segment */
            const uint32_t hdr_part = p->pkt.seq % 7;
            memcpy(&m->p.hdr[4*hdr_part], p->pkt.generic_segment.header_7, 4);
            m->hdr_mask = 1 << (6 - hdr_part);
            m->target = p->pkt.generic_segment.target_seq;
        } else {
            /* Starting with a parity segment */
            const uint32_t hdr_part = p->pkt.seq % 7;
            memcpy(&m->p.hdr[4*hdr_part], p->pkt.generic_parity.header_7, 4);
            m->hdr_mask |= 1 << (6 - hdr_part);
            m->target = p->pkt.generic_parity.target_seq;
        }

        /* Ensure there's enough memory for some amount of ranges */
        AVTMergerRange *tmp;
        if (m->ranges_allocated < 128) {
            tmp = avt_reallocarray(m->ranges,
                                   (m->nb_ranges + 1) << 1,
                                   sizeof(*tmp));
            if (!tmp)
                return AVT_ERROR(ENOMEM);

            m->ranges = tmp;
            m->ranges_allocated = (m->nb_ranges + 1) << 1;
        }

        if (m->parity_ranges_allocated < 128) {
            tmp = avt_reallocarray(m->parity_ranges,
                                   (m->nb_parity_ranges + 1) << 1,
                                   sizeof(*tmp));
            if (!tmp)
                return AVT_ERROR(ENOMEM);

            m->parity_ranges = tmp;
            m->parity_ranges_allocated = (m->nb_parity_ranges + 1) << 1;
        }

        /* Setup buffer */
        AVTBuffer *target;
        if (!is_parity)
            target = &m->p.pl;
        else
            target = &m->parity;

        /* Have enough memory for either data or parity */
        if (seg_off || avt_buffer_read_only(&p->pl)) {
            /* In case we have a read-only buffer, copy the data */
            AVTBuffer tmp_buf;
            uint8_t *dst = avt_buffer_quick_alloc(&tmp_buf, tot_size);
            if (!dst)
                return AVT_ERROR(ENOMEM);

            memcpy(dst + seg_off, src, seg_size);
            avt_buffer_quick_unref(&p->pl);
            *target = tmp_buf;
        } else {
            /* Resize the buffer if possible */
            ret = avt_buffer_resize(&p->pl, tot_size);
            if (ret < 0)
                return ret;

            *target = p->pl;
            p->pl = (AVTBuffer){ };
        }

        /* Mark what we have available */
        if (!is_parity) {
            m->ranges[m->nb_ranges++] = (AVTMergerRange) { seg_off, seg_size };
            m->pkt_len_track = seg_size;
        } else {
            m->parity_ranges[m->nb_parity_ranges++] = (AVTMergerRange) { seg_off, seg_size };
            m->pkt_parity_len_track = seg_size;
        }

        m->active = true;

        return AVT_ERROR(EAGAIN);
    }

    /* More sanity checking */
    ret = validate_packet(log_ctx, m, p, series,
                          seg_off, seg_size, tot_size, is_parity);
    if (ret < 0)
        return ret;

    /* Packet header state */
    if (!m->p_avail && (series < 0)) {
        /* Recover header */
        ret = fill_phantom_header(log_ctx, m, p, is_parity);
        if (ret < 0)
            return ret;
    } else if (series == 1) {
        /* We got a real header packet which we didn't have */
        m->p.pkt = p->pkt;
        memcpy(m->p.hdr, p->hdr, sizeof(p->hdr));
        m->hdr_mask = 0x7F;
        m->p_avail = true;
    }

    /* If total packet size was unknown, populate it and reallocate. */
    if (!is_parity && !m->target_tot_len) {
        avt_assert1(tot_size);

        ret = avt_buffer_resize(&m->p.pl, tot_size);
        if (ret < 0)
            return ret;

        m->target_tot_len = tot_size;
    }

    /* The parity buffer is allocated lazily: a merger may start with data
     * segments and only later receive its first parity segment. */
    if (is_parity && !m->parity_tot_len) {
        avt_assert1(tot_size);
        if (!avt_buffer_quick_alloc(&m->parity, tot_size))
            return AVT_ERROR(ENOMEM);
        m->parity_tot_len = tot_size;
    }

    /* Track ranges */
    ret = fill_ranges(m, is_parity, seg_off, seg_size);
    if (ret < 0)
        return ret;

    /* Copy new data */
    if (!is_parity) {
        uint8_t *dst = avt_buffer_get_data(&m->p.pl, NULL);
        memcpy(dst + seg_off, src, seg_size);
        m->pkt_len_track += seg_size;
    } else {
        uint8_t *dst = avt_buffer_get_data(&m->parity, NULL);
        memcpy(dst + seg_off, src, seg_size);
        m->pkt_parity_len_track += seg_size;
    }

    /* Update last packet */
    if (m->last < p->pkt.seq)
        m->last = p->pkt.seq;

    /* Unref source packet */
    avt_buffer_quick_unref(&p->pl);

    if (m->target_tot_len && m->pkt_len_track >= m->target_tot_len)
        return 0;

    /* The payload is incomplete. If stream-data-parity is available and
     * there are plausibly enough symbols across the data and the parity,
     * attempt RaptorQ recovery of the missing payload bytes. */
    if (m->parity_tot_len && m->target_tot_len &&
        ((size_t)m->pkt_len_track + m->pkt_parity_len_track) >=
            m->target_tot_len) {
        ret = merger_fec_recover(log_ctx, m);
        if (ret == 0) {
            m->pkt_len_track = m->target_tot_len;
            return 0;
        }
    }

    return AVT_ERROR(EAGAIN);
}

int avt_pkt_merge_out(void *log_ctx, AVTMerger *m, AVTPktd *p, int force)
{
    /* If inactive, we don't have anything */
    if (!m->active)
        return AVT_ERROR(ENOENT);

    /* If we couldn't collect enough header parts, we can't really output
     * something worth propagating either. */
    if (!(m->hdr_mask == 0x7F))
        return AVT_ERROR(EAGAIN);

    /* We can output something */
    if (!force && m->pkt_len_track < m->target_tot_len)
        return AVT_ERROR(EAGAIN);

    /* As each packet with a payload is required to have a non-zero amount
     * of payload, this should be impossible, as reconstructed headers don't
     * contain any payload. */
    avt_assert1(m->p_avail);
    *p = m->p;

    /* Deactivate context */
    m->p = (AVTPktd){ };
    m->active = false;

    return force ? m->target_tot_len : m->pkt_len_track;
}

void avt_pkt_merge_done(AVTMerger *m)
{
    avt_buffer_quick_unref(&m->p.pl);
    m->active = false;
}

void avt_pkt_merge_free(AVTMerger *m)
{
    avt_pkt_merge_done(m);
    free(m->ranges);
    free(m->parity_ranges);
    avt_buffer_quick_unref(&m->parity);
    memset(m, 0, sizeof(*m));
}
