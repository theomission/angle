//
// Copyright (c) 2013-2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// loadimage_etc.cpp: Decodes ETC and EAC encoded textures.

#include "libANGLE/renderer/d3d/loadimage_etc.h"

#include "libANGLE/renderer/d3d/loadimage.h"
#include "libANGLE/renderer/d3d/imageformats.h"

namespace rx
{
namespace
{

struct ETC2Block
{
    // Decodes unsigned single or dual channel block to bytes
    void decodeAsSingleChannel(uint8_t *dest,
                               size_t x,
                               size_t y,
                               size_t w,
                               size_t h,
                               size_t destPixelStride,
                               size_t destRowPitch,
                               bool isSigned) const
    {
        for (size_t j = 0; j < 4 && (y + j) < h; j++)
        {
            uint8_t *row = dest + (j * destRowPitch);
            for (size_t i = 0; i < 4 && (x + i) < w; i++)
            {
                uint8_t *pixel = row + (i * destPixelStride);
                if (isSigned)
                {
                    *pixel = clampSByte(getSingleChannel(i, j, isSigned));
                }
                else
                {
                    *pixel = clampByte(getSingleChannel(i, j, isSigned));
                }
            }
        }
    }

    // Decodes RGB block to rgba8
    void decodeAsRGB(uint8_t *dest,
                     size_t x,
                     size_t y,
                     size_t w,
                     size_t h,
                     size_t destRowPitch,
                     const uint8_t alphaValues[4][4],
                     bool punchThroughAlpha) const
    {
        bool opaqueBit                  = u.idht.mode.idm.diffbit;
        bool nonOpaquePunchThroughAlpha = punchThroughAlpha && !opaqueBit;
        // Select mode
        if (u.idht.mode.idm.diffbit || punchThroughAlpha)
        {
            const auto &block = u.idht.mode.idm.colors.diff;
            int r             = (block.R + block.dR);
            int g             = (block.G + block.dG);
            int b = (block.B + block.dB);
            if (r < 0 || r > 31)
            {
                decodeTBlock(dest, x, y, w, h, destRowPitch, alphaValues,
                             nonOpaquePunchThroughAlpha);
            }
            else if (g < 0 || g > 31)
            {
                decodeHBlock(dest, x, y, w, h, destRowPitch, alphaValues,
                             nonOpaquePunchThroughAlpha);
            }
            else if (b < 0 || b > 31)
            {
                decodePlanarBlock(dest, x, y, w, h, destRowPitch, alphaValues);
            }
            else
            {
                decodeDifferentialBlock(dest, x, y, w, h, destRowPitch, alphaValues,
                                        nonOpaquePunchThroughAlpha);
            }
        }
        else
        {
            decodeIndividualBlock(dest, x, y, w, h, destRowPitch, alphaValues,
                                  nonOpaquePunchThroughAlpha);
        }
    }

  private:
    union
    {
        // Individual, differential, H and T modes
        struct
        {
            union
            {
                // Individual and differential modes
                struct
                {
                    union
                    {
                        struct  // Individual colors
                        {
                            unsigned char R2 : 4;
                            unsigned char R1 : 4;
                            unsigned char G2 : 4;
                            unsigned char G1 : 4;
                            unsigned char B2 : 4;
                            unsigned char B1 : 4;
                        } indiv;
                        struct  // Differential colors
                        {
                            signed char dR : 3;
                            unsigned char R : 5;
                            signed char dG : 3;
                            unsigned char G : 5;
                            signed char dB : 3;
                            unsigned char B : 5;
                        } diff;
                    } colors;
                    bool flipbit : 1;
                    bool diffbit : 1;
                    unsigned char cw2 : 3;
                    unsigned char cw1 : 3;
                } idm;
                // T mode
                struct
                {
                    // Byte 1
                    unsigned char TR1b : 2;
                    unsigned char TdummyB : 1;
                    unsigned char TR1a : 2;
                    unsigned char TdummyA : 3;
                    // Byte 2
                    unsigned char TB1 : 4;
                    unsigned char TG1 : 4;
                    // Byte 3
                    unsigned char TG2 : 4;
                    unsigned char TR2 : 4;
                    // Byte 4
                    unsigned char Tdb : 1;
                    bool Tflipbit : 1;
                    unsigned char Tda : 2;
                    unsigned char TB2 : 4;
                } tm;
                // H mode
                struct
                {
                    // Byte 1
                    unsigned char HG1a : 3;
                    unsigned char HR1 : 4;
                    unsigned char HdummyA : 1;
                    // Byte 2
                    unsigned char HB1b : 2;
                    unsigned char HdummyC : 1;
                    unsigned char HB1a : 1;
                    unsigned char HG1b : 1;
                    unsigned char HdummyB : 3;
                    // Byte 3
                    unsigned char HG2a : 3;
                    unsigned char HR2 : 4;
                    unsigned char HB1c : 1;
                    // Byte 4
                    unsigned char Hdb : 1;
                    bool Hflipbit : 1;
                    unsigned char Hda : 1;
                    unsigned char HB2 : 4;
                    unsigned char HG2b : 1;
                } hm;
            } mode;
            unsigned char pixelIndexMSB[2];
            unsigned char pixelIndexLSB[2];
        } idht;
        // planar mode
        struct
        {
            // Byte 1
            unsigned char GO1 : 1;
            unsigned char RO : 6;
            unsigned char PdummyA : 1;
            // Byte 2
            unsigned char BO1 : 1;
            unsigned char GO2 : 6;
            unsigned char PdummyB : 1;
            // Byte 3
            unsigned char BO3a : 2;
            unsigned char PdummyD : 1;
            unsigned char BO2 : 2;
            unsigned char PdummyC : 3;
            // Byte 4
            unsigned char RH2 : 1;
            bool Pflipbit : 1;
            unsigned char RH1 : 5;
            unsigned char BO3b : 1;
            // Byte 5
            unsigned char BHa : 1;
            unsigned char GH : 7;
            // Byte 6
            unsigned char RVa : 3;
            unsigned char BHb : 5;
            // Byte 7
            unsigned char GVa : 5;
            unsigned char RVb : 3;
            // Byte 8
            unsigned char BV : 6;
            unsigned char GVb : 2;
        } pblk;
        // Single channel block
        struct
        {
            union
            {
                unsigned char us;
                signed char s;
            } base_codeword;
            unsigned char table_index : 4;
            unsigned char multiplier : 4;
            unsigned char mc1 : 2;
            unsigned char mb : 3;
            unsigned char ma : 3;
            unsigned char mf1 : 1;
            unsigned char me : 3;
            unsigned char md : 3;
            unsigned char mc2 : 1;
            unsigned char mh : 3;
            unsigned char mg : 3;
            unsigned char mf2 : 2;
            unsigned char mk1 : 2;
            unsigned char mj : 3;
            unsigned char mi : 3;
            unsigned char mn1 : 1;
            unsigned char mm : 3;
            unsigned char ml : 3;
            unsigned char mk2 : 1;
            unsigned char mp : 3;
            unsigned char mo : 3;
            unsigned char mn2 : 2;
        } scblk;
    } u;

    static unsigned char clampByte(int value)
    {
        return static_cast<unsigned char>(gl::clamp(value, 0, 255));
    }

    static signed char clampSByte(int value)
    {
        return static_cast<signed char>(gl::clamp(value, -128, 127));
    }

    static R8G8B8A8 createRGBA(int red, int green, int blue, int alpha)
    {
        R8G8B8A8 rgba;
        rgba.R = clampByte(red);
        rgba.G = clampByte(green);
        rgba.B = clampByte(blue);
        rgba.A = clampByte(alpha);
        return rgba;
    }

    static R8G8B8A8 createRGBA(int red, int green, int blue)
    {
        return createRGBA(red, green, blue, 255);
    }

    static int extend_4to8bits(int x) { return (x << 4) | x; }
    static int extend_5to8bits(int x) { return (x << 3) | (x >> 2); }
    static int extend_6to8bits(int x) { return (x << 2) | (x >> 4); }
    static int extend_7to8bits(int x) { return (x << 1) | (x >> 6); }

    void decodeIndividualBlock(uint8_t *dest,
                               size_t x,
                               size_t y,
                               size_t w,
                               size_t h,
                               size_t destRowPitch,
                               const uint8_t alphaValues[4][4],
                               bool nonOpaquePunchThroughAlpha) const
    {
        const auto &block = u.idht.mode.idm.colors.indiv;
        int r1            = extend_4to8bits(block.R1);
        int g1            = extend_4to8bits(block.G1);
        int b1            = extend_4to8bits(block.B1);
        int r2            = extend_4to8bits(block.R2);
        int g2            = extend_4to8bits(block.G2);
        int b2 = extend_4to8bits(block.B2);
        decodeIndividualOrDifferentialBlock(dest, x, y, w, h, destRowPitch, r1, g1, b1, r2, g2, b2,
                                            alphaValues, nonOpaquePunchThroughAlpha);
    }

    void decodeDifferentialBlock(uint8_t *dest,
                                 size_t x,
                                 size_t y,
                                 size_t w,
                                 size_t h,
                                 size_t destRowPitch,
                                 const uint8_t alphaValues[4][4],
                                 bool nonOpaquePunchThroughAlpha) const
    {
        const auto &block = u.idht.mode.idm.colors.diff;
        int b1            = extend_5to8bits(block.B);
        int g1            = extend_5to8bits(block.G);
        int r1            = extend_5to8bits(block.R);
        int r2            = extend_5to8bits(block.R + block.dR);
        int g2            = extend_5to8bits(block.G + block.dG);
        int b2 = extend_5to8bits(block.B + block.dB);
        decodeIndividualOrDifferentialBlock(dest, x, y, w, h, destRowPitch, r1, g1, b1, r2, g2, b2,
                                            alphaValues, nonOpaquePunchThroughAlpha);
    }

    void decodeIndividualOrDifferentialBlock(uint8_t *dest,
                                             size_t x,
                                             size_t y,
                                             size_t w,
                                             size_t h,
                                             size_t destRowPitch,
                                             int r1,
                                             int g1,
                                             int b1,
                                             int r2,
                                             int g2,
                                             int b2,
                                             const uint8_t alphaValues[4][4],
                                             bool nonOpaquePunchThroughAlpha) const
    {
        // Table 3.17.2 sorted according to table 3.17.3
        // clang-format off
        static const int intensityModifierDefault[8][4] =
        {
            {  2,   8,  -2,   -8 },
            {  5,  17,  -5,  -17 },
            {  9,  29,  -9,  -29 },
            { 13,  42, -13,  -42 },
            { 18,  60, -18,  -60 },
            { 24,  80, -24,  -80 },
            { 33, 106, -33, -106 },
            { 47, 183, -47, -183 },
        };
        // clang-format on

        // Table C.12, intensity modifier for non opaque punchthrough alpha
        // clang-format off
        static const int intensityModifierNonOpaque[8][4] =
        {
            { 0,   8, 0,   -8 },
            { 0,  17, 0,  -17 },
            { 0,  29, 0,  -29 },
            { 0,  42, 0,  -42 },
            { 0,  60, 0,  -60 },
            { 0,  80, 0,  -80 },
            { 0, 106, 0, -106 },
            { 0, 183, 0, -183 },
        };
        // clang-format on

        const int(&intensityModifier)[8][4] =
            nonOpaquePunchThroughAlpha ? intensityModifierNonOpaque : intensityModifierDefault;

        R8G8B8A8 subblockColors0[4];
        R8G8B8A8 subblockColors1[4];
        for (size_t blockIdx = 0; blockIdx < 4; blockIdx++)
        {
            const int i1              = intensityModifier[u.idht.mode.idm.cw1][blockIdx];
            subblockColors0[blockIdx] = createRGBA(r1 + i1, g1 + i1, b1 + i1);

            const int i2              = intensityModifier[u.idht.mode.idm.cw2][blockIdx];
            subblockColors1[blockIdx] = createRGBA(r2 + i2, g2 + i2, b2 + i2);
        }

        if (u.idht.mode.idm.flipbit)
        {
            uint8_t *curPixel = dest;
            for (size_t j = 0; j < 2 && (y + j) < h; j++)
            {
                R8G8B8A8 *row = reinterpret_cast<R8G8B8A8 *>(curPixel);
                for (size_t i = 0; i < 4 && (x + i) < w; i++)
                {
                    row[i]   = subblockColors0[getIndex(i, j)];
                    row[i].A = alphaValues[j][i];
                }
                curPixel += destRowPitch;
            }
            for (size_t j = 2; j < 4 && (y + j) < h; j++)
            {
                R8G8B8A8 *row = reinterpret_cast<R8G8B8A8 *>(curPixel);
                for (size_t i = 0; i < 4 && (x + i) < w; i++)
                {
                    row[i]   = subblockColors1[getIndex(i, j)];
                    row[i].A = alphaValues[j][i];
                }
                curPixel += destRowPitch;
            }
        }
        else
        {
            uint8_t *curPixel = dest;
            for (size_t j = 0; j < 4 && (y + j) < h; j++)
            {
                R8G8B8A8 *row = reinterpret_cast<R8G8B8A8 *>(curPixel);
                for (size_t i = 0; i < 2 && (x + i) < w; i++)
                {
                    row[i]   = subblockColors0[getIndex(i, j)];
                    row[i].A = alphaValues[j][i];
                }
                for (size_t i = 2; i < 4 && (x + i) < w; i++)
                {
                    row[i]   = subblockColors1[getIndex(i, j)];
                    row[i].A = alphaValues[j][i];
                }
                curPixel += destRowPitch;
            }
        }
        if (nonOpaquePunchThroughAlpha)
        {
            decodePunchThroughAlphaBlock(dest, x, y, w, h, destRowPitch);
        }
    }

    void decodeTBlock(uint8_t *dest,
                      size_t x,
                      size_t y,
                      size_t w,
                      size_t h,
                      size_t destRowPitch,
                      const uint8_t alphaValues[4][4],
                      bool nonOpaquePunchThroughAlpha) const
    {
        // Table C.8, distance index for T and H modes
        const auto &block = u.idht.mode.tm;

        int r1 = extend_4to8bits(block.TR1a << 2 | block.TR1b);
        int g1 = extend_4to8bits(block.TG1);
        int b1 = extend_4to8bits(block.TB1);
        int r2 = extend_4to8bits(block.TR2);
        int g2 = extend_4to8bits(block.TG2);
        int b2 = extend_4to8bits(block.TB2);

        static int distance[8] = {3, 6, 11, 16, 23, 32, 41, 64};
        const int d            = distance[block.Tda << 1 | block.Tdb];

        const R8G8B8A8 paintColors[4] = {
            createRGBA(r1, g1, b1), createRGBA(r2 + d, g2 + d, b2 + d), createRGBA(r2, g2, b2),
            createRGBA(r2 - d, g2 - d, b2 - d),
        };

        uint8_t *curPixel = dest;
        for (size_t j = 0; j < 4 && (y + j) < h; j++)
        {
            R8G8B8A8 *row = reinterpret_cast<R8G8B8A8 *>(curPixel);
            for (size_t i = 0; i < 4 && (x + i) < w; i++)
            {
                row[i]   = paintColors[getIndex(i, j)];
                row[i].A = alphaValues[j][i];
            }
            curPixel += destRowPitch;
        }

        if (nonOpaquePunchThroughAlpha)
        {
            decodePunchThroughAlphaBlock(dest, x, y, w, h, destRowPitch);
        }
    }

    void decodeHBlock(uint8_t *dest,
                      size_t x,
                      size_t y,
                      size_t w,
                      size_t h,
                      size_t destRowPitch,
                      const uint8_t alphaValues[4][4],
                      bool nonOpaquePunchThroughAlpha) const
    {
        // Table C.8, distance index for T and H modes
        const auto &block = u.idht.mode.hm;

        int r1 = extend_4to8bits(block.HR1);
        int g1 = extend_4to8bits(block.HG1a << 1 | block.HG1b);
        int b1 = extend_4to8bits(block.HB1a << 3 | block.HB1b << 1 | block.HB1c);
        int r2 = extend_4to8bits(block.HR2);
        int g2 = extend_4to8bits(block.HG2a << 1 | block.HG2b);
        int b2 = extend_4to8bits(block.HB2);

        static const int distance[8] = {3, 6, 11, 16, 23, 32, 41, 64};
        const int d = distance[(block.Hda << 2) | (block.Hdb << 1) |
                               ((r1 << 16 | g1 << 8 | b1) >= (r2 << 16 | g2 << 8 | b2) ? 1 : 0)];

        const R8G8B8A8 paintColors[4] = {
            createRGBA(r1 + d, g1 + d, b1 + d), createRGBA(r1 - d, g1 - d, b1 - d),
            createRGBA(r2 + d, g2 + d, b2 + d), createRGBA(r2 - d, g2 - d, b2 - d),
        };

        uint8_t *curPixel = dest;
        for (size_t j = 0; j < 4 && (y + j) < h; j++)
        {
            R8G8B8A8 *row = reinterpret_cast<R8G8B8A8 *>(curPixel);
            for (size_t i = 0; i < 4 && (x + i) < w; i++)
            {
                row[i]   = paintColors[getIndex(i, j)];
                row[i].A = alphaValues[j][i];
            }
            curPixel += destRowPitch;
        }

        if (nonOpaquePunchThroughAlpha)
        {
            decodePunchThroughAlphaBlock(dest, x, y, w, h, destRowPitch);
        }
    }

    void decodePlanarBlock(uint8_t *dest,
                           size_t x,
                           size_t y,
                           size_t w,
                           size_t h,
                           size_t pitch,
                           const uint8_t alphaValues[4][4]) const
    {
        int ro = extend_6to8bits(u.pblk.RO);
        int go = extend_7to8bits(u.pblk.GO1 << 6 | u.pblk.GO2);
        int bo =
            extend_6to8bits(u.pblk.BO1 << 5 | u.pblk.BO2 << 3 | u.pblk.BO3a << 1 | u.pblk.BO3b);
        int rh = extend_6to8bits(u.pblk.RH1 << 1 | u.pblk.RH2);
        int gh = extend_7to8bits(u.pblk.GH);
        int bh = extend_6to8bits(u.pblk.BHa << 5 | u.pblk.BHb);
        int rv = extend_6to8bits(u.pblk.RVa << 3 | u.pblk.RVb);
        int gv = extend_7to8bits(u.pblk.GVa << 2 | u.pblk.GVb);
        int bv = extend_6to8bits(u.pblk.BV);

        uint8_t *curPixel = dest;
        for (size_t j = 0; j < 4 && (y + j) < h; j++)
        {
            R8G8B8A8 *row = reinterpret_cast<R8G8B8A8 *>(curPixel);

            int ry = static_cast<int>(j) * (rv - ro) + 2;
            int gy = static_cast<int>(j) * (gv - go) + 2;
            int by = static_cast<int>(j) * (bv - bo) + 2;
            for (size_t i = 0; i < 4 && (x + i) < w; i++)
            {
                row[i] = createRGBA(((static_cast<int>(i) * (rh - ro) + ry) >> 2) + ro,
                                    ((static_cast<int>(i) * (gh - go) + gy) >> 2) + go,
                                    ((static_cast<int>(i) * (bh - bo) + by) >> 2) + bo,
                                    alphaValues[j][i]);
            }
            curPixel += pitch;
        }
    }

    // Index for individual, differential, H and T modes
    size_t getIndex(size_t x, size_t y) const
    {
        size_t bitIndex  = x * 4 + y;
        size_t bitOffset = bitIndex & 7;
        size_t lsb       = (u.idht.pixelIndexLSB[1 - (bitIndex >> 3)] >> bitOffset) & 1;
        size_t msb = (u.idht.pixelIndexMSB[1 - (bitIndex >> 3)] >> bitOffset) & 1;
        return (msb << 1) | lsb;
    }

    void decodePunchThroughAlphaBlock(uint8_t *dest,
                                      size_t x,
                                      size_t y,
                                      size_t w,
                                      size_t h,
                                      size_t destRowPitch) const
    {
        uint8_t *curPixel = dest;
        for (size_t j = 0; j < 4 && (y + j) < h; j++)
        {
            R8G8B8A8 *row = reinterpret_cast<R8G8B8A8 *>(curPixel);
            for (size_t i = 0; i < 4 && (x + i) < w; i++)
            {
                if (getIndex(i, j) == 2)  //  msb == 1 && lsb == 0
                {
                    row[i] = createRGBA(0, 0, 0, 0);
                }
            }
            curPixel += destRowPitch;
        }
    }

    // Single channel utility functions
    int getSingleChannel(size_t x, size_t y, bool isSigned) const
    {
        int codeword = isSigned ? u.scblk.base_codeword.s : u.scblk.base_codeword.us;
        return codeword + getSingleChannelModifier(x, y) * u.scblk.multiplier;
    }

    int getSingleChannelIndex(size_t x, size_t y) const
    {
        ASSERT(x < 4 && y < 4);

        // clang-format off
        switch (x * 4 + y)
        {
            case 0: return u.scblk.ma;
            case 1: return u.scblk.mb;
            case 2: return u.scblk.mc1 << 1 | u.scblk.mc2;
            case 3: return u.scblk.md;
            case 4: return u.scblk.me;
            case 5: return u.scblk.mf1 << 2 | u.scblk.mf2;
            case 6: return u.scblk.mg;
            case 7: return u.scblk.mh;
            case 8: return u.scblk.mi;
            case 9: return u.scblk.mj;
            case 10: return u.scblk.mk1 << 1 | u.scblk.mk2;
            case 11: return u.scblk.ml;
            case 12: return u.scblk.mm;
            case 13: return u.scblk.mn1 << 2 | u.scblk.mn2;
            case 14: return u.scblk.mo;
            case 15: return u.scblk.mp;
            default: UNREACHABLE(); return 0;
        }
        // clang-format on
    }

    int getSingleChannelModifier(size_t x, size_t y) const
    {
        // clang-format off
        static const int modifierTable[16][8] =
        {
            { -3, -6,  -9, -15, 2, 5, 8, 14 },
            { -3, -7, -10, -13, 2, 6, 9, 12 },
            { -2, -5,  -8, -13, 1, 4, 7, 12 },
            { -2, -4,  -6, -13, 1, 3, 5, 12 },
            { -3, -6,  -8, -12, 2, 5, 7, 11 },
            { -3, -7,  -9, -11, 2, 6, 8, 10 },
            { -4, -7,  -8, -11, 3, 6, 7, 10 },
            { -3, -5,  -8, -11, 2, 4, 7, 10 },
            { -2, -6,  -8, -10, 1, 5, 7,  9 },
            { -2, -5,  -8, -10, 1, 4, 7,  9 },
            { -2, -4,  -8, -10, 1, 3, 7,  9 },
            { -2, -5,  -7, -10, 1, 4, 6,  9 },
            { -3, -4,  -7, -10, 2, 3, 6,  9 },
            { -1, -2,  -3, -10, 0, 1, 2,  9 },
            { -4, -6,  -8,  -9, 3, 5, 7,  8 },
            { -3, -5,  -7,  -9, 2, 4, 6,  8 }
        };
        // clang-format on

        return modifierTable[u.scblk.table_index][getSingleChannelIndex(x, y)];
    }
};

// clang-format off
static const uint8_t DefaultETCAlphaValues[4][4] =
{
    { 255, 255, 255, 255 },
    { 255, 255, 255, 255 },
    { 255, 255, 255, 255 },
    { 255, 255, 255, 255 },
};
// clang-format on

void LoadR11EACToR8(size_t width,
                    size_t height,
                    size_t depth,
                    const uint8_t *input,
                    size_t inputRowPitch,
                    size_t inputDepthPitch,
                    uint8_t *output,
                    size_t outputRowPitch,
                    size_t outputDepthPitch,
                    bool isSigned)
{
    for (size_t z = 0; z < depth; z++)
    {
        for (size_t y = 0; y < height; y += 4)
        {
            const ETC2Block *sourceRow =
                OffsetDataPointer<ETC2Block>(input, y / 4, z, inputRowPitch, inputDepthPitch);
            uint8_t *destRow =
                OffsetDataPointer<uint8_t>(output, y, z, outputRowPitch, outputDepthPitch);

            for (size_t x = 0; x < width; x += 4)
            {
                const ETC2Block *sourceBlock = sourceRow + (x / 4);
                uint8_t *destPixels          = destRow + x;

                sourceBlock->decodeAsSingleChannel(destPixels, x, y, width, height, 1,
                                                   outputRowPitch, isSigned);
            }
        }
    }
}

void LoadRG11EACToRG8(size_t width,
                      size_t height,
                      size_t depth,
                      const uint8_t *input,
                      size_t inputRowPitch,
                      size_t inputDepthPitch,
                      uint8_t *output,
                      size_t outputRowPitch,
                      size_t outputDepthPitch,
                      bool isSigned)
{
    for (size_t z = 0; z < depth; z++)
    {
        for (size_t y = 0; y < height; y += 4)
        {
            const ETC2Block *sourceRow =
                OffsetDataPointer<ETC2Block>(input, y / 4, z, inputRowPitch, inputDepthPitch);
            uint8_t *destRow =
                OffsetDataPointer<uint8_t>(output, y, z, outputRowPitch, outputDepthPitch);

            for (size_t x = 0; x < width; x += 4)
            {
                uint8_t *destPixelsRed          = destRow + (x * 2);
                const ETC2Block *sourceBlockRed = sourceRow + (x / 2);
                sourceBlockRed->decodeAsSingleChannel(destPixelsRed, x, y, width, height, 2,
                                                      outputRowPitch, isSigned);

                uint8_t *destPixelsGreen          = destPixelsRed + 1;
                const ETC2Block *sourceBlockGreen = sourceBlockRed + 1;
                sourceBlockGreen->decodeAsSingleChannel(destPixelsGreen, x, y, width, height, 2,
                                                        outputRowPitch, isSigned);
            }
        }
    }
}

void LoadETC2RGB8ToRGBA8(size_t width,
                         size_t height,
                         size_t depth,
                         const uint8_t *input,
                         size_t inputRowPitch,
                         size_t inputDepthPitch,
                         uint8_t *output,
                         size_t outputRowPitch,
                         size_t outputDepthPitch,
                         bool punchthroughAlpha)
{
    for (size_t z = 0; z < depth; z++)
    {
        for (size_t y = 0; y < height; y += 4)
        {
            const ETC2Block *sourceRow =
                OffsetDataPointer<ETC2Block>(input, y / 4, z, inputRowPitch, inputDepthPitch);
            uint8_t *destRow =
                OffsetDataPointer<uint8_t>(output, y, z, outputRowPitch, outputDepthPitch);

            for (size_t x = 0; x < width; x += 4)
            {
                const ETC2Block *sourceBlock = sourceRow + (x / 4);
                uint8_t *destPixels          = destRow + (x * 4);

                sourceBlock->decodeAsRGB(destPixels, x, y, width, height, outputRowPitch,
                                         DefaultETCAlphaValues, punchthroughAlpha);
            }
        }
    }
}

void LoadETC2RGBA8ToRGBA8(size_t width,
                          size_t height,
                          size_t depth,
                          const uint8_t *input,
                          size_t inputRowPitch,
                          size_t inputDepthPitch,
                          uint8_t *output,
                          size_t outputRowPitch,
                          size_t outputDepthPitch,
                          bool srgb)
{
    uint8_t decodedAlphaValues[4][4];

    for (size_t z = 0; z < depth; z++)
    {
        for (size_t y = 0; y < height; y += 4)
        {
            const ETC2Block *sourceRow =
                OffsetDataPointer<ETC2Block>(input, y / 4, z, inputRowPitch, inputDepthPitch);
            uint8_t *destRow =
                OffsetDataPointer<uint8_t>(output, y, z, outputRowPitch, outputDepthPitch);

            for (size_t x = 0; x < width; x += 4)
            {
                const ETC2Block *sourceBlockAlpha = sourceRow + (x / 2);
                sourceBlockAlpha->decodeAsSingleChannel(
                    reinterpret_cast<uint8_t *>(decodedAlphaValues), x, y, width, height, 1, 4,
                    false);

                uint8_t *destPixels             = destRow + (x * 4);
                const ETC2Block *sourceBlockRGB = sourceBlockAlpha + 1;
                sourceBlockRGB->decodeAsRGB(destPixels, x, y, width, height, outputRowPitch,
                                            decodedAlphaValues, false);
            }
        }
    }
}

}  // anonymous namespace

void LoadEACR11ToR8(size_t width,
                    size_t height,
                    size_t depth,
                    const uint8_t *input,
                    size_t inputRowPitch,
                    size_t inputDepthPitch,
                    uint8_t *output,
                    size_t outputRowPitch,
                    size_t outputDepthPitch)
{
    LoadR11EACToR8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                   outputRowPitch, outputDepthPitch, false);
}

void LoadEACR11SToR8(size_t width,
                     size_t height,
                     size_t depth,
                     const uint8_t *input,
                     size_t inputRowPitch,
                     size_t inputDepthPitch,
                     uint8_t *output,
                     size_t outputRowPitch,
                     size_t outputDepthPitch)
{
    LoadR11EACToR8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                   outputRowPitch, outputDepthPitch, true);
}

void LoadEACRG11ToRG8(size_t width,
                      size_t height,
                      size_t depth,
                      const uint8_t *input,
                      size_t inputRowPitch,
                      size_t inputDepthPitch,
                      uint8_t *output,
                      size_t outputRowPitch,
                      size_t outputDepthPitch)
{
    LoadRG11EACToRG8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                     outputRowPitch, outputDepthPitch, false);
}

void LoadEACRG11SToRG8(size_t width,
                       size_t height,
                       size_t depth,
                       const uint8_t *input,
                       size_t inputRowPitch,
                       size_t inputDepthPitch,
                       uint8_t *output,
                       size_t outputRowPitch,
                       size_t outputDepthPitch)
{
    LoadRG11EACToRG8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                     outputRowPitch, outputDepthPitch, true);
}

void LoadETC2RGB8ToRGBA8(size_t width,
                         size_t height,
                         size_t depth,
                         const uint8_t *input,
                         size_t inputRowPitch,
                         size_t inputDepthPitch,
                         uint8_t *output,
                         size_t outputRowPitch,
                         size_t outputDepthPitch)
{
    LoadETC2RGB8ToRGBA8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                        outputRowPitch, outputDepthPitch, false);
}

void LoadETC2SRGB8ToRGBA8(size_t width,
                          size_t height,
                          size_t depth,
                          const uint8_t *input,
                          size_t inputRowPitch,
                          size_t inputDepthPitch,
                          uint8_t *output,
                          size_t outputRowPitch,
                          size_t outputDepthPitch)
{
    LoadETC2RGB8ToRGBA8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                        outputRowPitch, outputDepthPitch, false);
}

void LoadETC2RGB8A1ToRGBA8(size_t width,
                           size_t height,
                           size_t depth,
                           const uint8_t *input,
                           size_t inputRowPitch,
                           size_t inputDepthPitch,
                           uint8_t *output,
                           size_t outputRowPitch,
                           size_t outputDepthPitch)
{
    LoadETC2RGB8ToRGBA8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                        outputRowPitch, outputDepthPitch, true);
}

void LoadETC2SRGB8A1ToRGBA8(size_t width,
                            size_t height,
                            size_t depth,
                            const uint8_t *input,
                            size_t inputRowPitch,
                            size_t inputDepthPitch,
                            uint8_t *output,
                            size_t outputRowPitch,
                            size_t outputDepthPitch)
{
    LoadETC2RGB8ToRGBA8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                        outputRowPitch, outputDepthPitch, true);
}

void LoadETC2RGBA8ToRGBA8(size_t width,
                          size_t height,
                          size_t depth,
                          const uint8_t *input,
                          size_t inputRowPitch,
                          size_t inputDepthPitch,
                          uint8_t *output,
                          size_t outputRowPitch,
                          size_t outputDepthPitch)
{
    LoadETC2RGBA8ToRGBA8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                         outputRowPitch, outputDepthPitch, false);
}

void LoadETC2SRGBA8ToSRGBA8(size_t width,
                            size_t height,
                            size_t depth,
                            const uint8_t *input,
                            size_t inputRowPitch,
                            size_t inputDepthPitch,
                            uint8_t *output,
                            size_t outputRowPitch,
                            size_t outputDepthPitch)
{
    LoadETC2RGBA8ToRGBA8(width, height, depth, input, inputRowPitch, inputDepthPitch, output,
                         outputRowPitch, outputDepthPitch, true);
}
}  // namespace rx
