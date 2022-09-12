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

#ifndef LIBQPROTO_VIDEO_HEADER
#define LIBQPROTO_VIDEO_HEADER

#include <stdint.h>
#include <stdbool.h>

#include <libqproto/utils.h>

enum QprotoSubsampling {
    QPROTO_SUB_444 = 0,
    QPROTO_SUB_420 = 1,
    QPROTO_SUB_422 = 2,
};

enum QprotoColorspace {
    QPROTO_COL_MONOCHROME = 0,
    QPROTO_COL_RGB = 1,
    QPROTO_COL_YUV = 2,
    QPROTO_COL_YCOCGR = 3,
    QPROTO_COL_YCGCOR = 4,
    QPROTO_COL_XYZ = 5,
    QPROTO_COL_XYB = 6,
    QPROTO_COL_ICTCP = 7,
};

enum QprotoChromaPos {
    QPROTO_CHROMA_UNSPEC = 0,
    QPROTO_CHROMA_LEFT = 1,
    QPROTO_CHROMA_CENTER = 2,
    QPROTO_CHROMA_TOPLEFT = 3,
    QPROTO_CHROMA_TOP = 4,
    QPROTO_CHROMA_BOTTOMLEFT = 5,
    QPROTO_CHROMA_BOTTOM = 6,
};

enum QprotoPrimaries {
    QPROTO_PRIM_BT709 = 1,
    QPROTO_PRIM_UNSPEC = 2,
    QPROTO_PRIM_BT470 = 4,
    QPROTO_PRIM_BT601_625 = 5,
    QPROTO_PRIM_BT601_525 = 6,
    QPROTO_PRIM_ST240 = 7,
    QPROTO_PRIM_FILM = 8,
    QPROTO_PRIM_BT2020 = 9,
    QPROTO_PRIM_ST428 = 10,
};

enum QprotoTransfer {
    QPROTO_TRANSFER_BT709 = 1,
    QPROTO_TRANSFER_UNSPEC = 2,
    QPROTO_TRANSFER_FCC = 4,
    QPROTO_TRANSFER_BT470 = 5,
    QPROTO_TRANSFER_BT601 = 6,
    QPROTO_TRANSFER_ST240 = 7,
    QPROTO_TRANSFER_LINEAR = 8,
    QPROTO_TRANSFER_LOG = 9,
    QPROTO_TRANSFER_LOG_SQ = 10,
    QPROTO_TRANSFER_BT1361 = 12,
    QPROTO_TRANSFER_BT2020 = 14,
    QPROTO_TRANSFER_ST2084 = 16,
    QPROTO_TRANSFER_HLG = 18,
};

enum QprotoMatrix {
    QPROTO_MATRIX_IDENT = 0,
    QPROTO_MATRIX_BT709 = 1,
    QPROTO_MATRIX_UNSPEC = 2,
    QPROTO_MATRIX_FCC = 4,
    QPROTO_MATRIX_BT470 = 5,
    QPROTO_MATRIX_BT601 = 6,
    QPROTO_MATRIX_ST240 = 7,
    QPROTO_MATRIX_YCGCO = 8,
    QPROTO_MATRIX_BT2020_CL = 9,
    QPROTO_MATRIX_BT2020_NCL = 10,
    QPROTO_MATRIX_ST2085 = 11,
    QPROTO_MATRIX_BT2100 = 14,
};

typedef struct QprotoStreamVideoInfo {
    uint32_t width;
    uint32_t height;
    QprotoRational signal_aspect;
    enum QprotoSubsampling subsampling;
    enum QprotoColorspace colorspace;
    uint8_t bit_depth;
    uint8_t interlaced;
    QprotoRational gamma;
    QprotoRational framerate;
    uint16_t limited_range;
    enum QprotoChromaPos chroma_pos;
    enum QprotoPrimaries primaries;
    enum QprotoTransfer transfer;
    enum QprotoMatrix matrix;
    bool has_mastering_primaries;

    QprotoRational custom_matrix[16];
    QprotoRational custom_primaries[6];
    QprotoRational white_point[2];

    bool has_luminance;
    QprotoRational min_luminance;
    QprotoRational max_luminance;
} QprotoStreamVideoInfo;

#endif
