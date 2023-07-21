/*
 * Copyright © 2022 Lynne
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the “Software”), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef LIBAVTRANSPORT_VIDEO_HEADER
#define LIBAVTRANSPORT_VIDEO_HEADER

#include <stdint.h>
#include <stdbool.h>

#include <libavtransport/utils.h>

enum AVTSubsampling {
    AVT_SUB_444 = 0,
    AVT_SUB_420 = 1,
    AVT_SUB_422 = 2,
};

enum AVTColorspace {
    AVT_COL_MONOCHROME = 0,
    AVT_COL_RGB = 1,
    AVT_COL_YUV = 2,
    AVT_COL_YCOCGR = 3,
    AVT_COL_YCGCOR = 4,
    AVT_COL_XYZ = 5,
    AVT_COL_XYB = 6,
    AVT_COL_ICTCP = 7,
};

enum AVTChromaPos {
    AVT_CHROMA_UNSPEC = 0,
    AVT_CHROMA_LEFT = 1,
    AVT_CHROMA_CENTER = 2,
    AVT_CHROMA_TOPLEFT = 3,
    AVT_CHROMA_TOP = 4,
    AVT_CHROMA_BOTTOMLEFT = 5,
    AVT_CHROMA_BOTTOM = 6,
};

enum AVTPrimaries {
    AVT_PRIM_BT709 = 1,
    AVT_PRIM_UNSPEC = 2,
    AVT_PRIM_BT470 = 4,
    AVT_PRIM_BT601_625 = 5,
    AVT_PRIM_BT601_525 = 6,
    AVT_PRIM_ST240 = 7,
    AVT_PRIM_FILM = 8,
    AVT_PRIM_BT2020 = 9,
    AVT_PRIM_ST428 = 10,
};

enum AVTTransfer {
    AVT_TRANSFER_BT709 = 1,
    AVT_TRANSFER_UNSPEC = 2,
    AVT_TRANSFER_FCC = 4,
    AVT_TRANSFER_BT470 = 5,
    AVT_TRANSFER_BT601 = 6,
    AVT_TRANSFER_ST240 = 7,
    AVT_TRANSFER_LINEAR = 8,
    AVT_TRANSFER_LOG = 9,
    AVT_TRANSFER_LOG_SQ = 10,
    AVT_TRANSFER_BT1361 = 12,
    AVT_TRANSFER_BT2020 = 14,
    AVT_TRANSFER_ST2084 = 16,
    AVT_TRANSFER_HLG = 18,
};

enum AVTMatrix {
    AVT_MATRIX_IDENT = 0,
    AVT_MATRIX_BT709 = 1,
    AVT_MATRIX_UNSPEC = 2,
    AVT_MATRIX_FCC = 4,
    AVT_MATRIX_BT470 = 5,
    AVT_MATRIX_BT601 = 6,
    AVT_MATRIX_ST240 = 7,
    AVT_MATRIX_YCGCO = 8,
    AVT_MATRIX_BT2020_CL = 9,
    AVT_MATRIX_BT2020_NCL = 10,
    AVT_MATRIX_ST2085 = 11,
    AVT_MATRIX_BT2100 = 14,
};

typedef struct AVTStreamVideoInfo {
    uint32_t width;
    uint32_t height;
    AVTRational signal_aspect;
    enum AVTSubsampling subsampling;
    enum AVTColorspace colorspace;
    uint8_t bit_depth;
    uint8_t interlaced;
    AVTRational gamma;
    AVTRational framerate;
    uint16_t limited_range;
    enum AVTChromaPos chroma_pos;
    enum AVTPrimaries primaries;
    enum AVTTransfer transfer;
    enum AVTMatrix matrix;
    bool has_mastering_primaries;

    AVTRational custom_matrix[16];
    AVTRational custom_primaries[6];
    AVTRational white_point[2];

    bool has_luminance;
    AVTRational min_luminance;
    AVTRational max_luminance;
} AVTStreamVideoInfo;

#endif
