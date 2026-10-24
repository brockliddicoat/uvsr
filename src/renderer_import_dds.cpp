/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
 * Copyright (c) 2018 Microsoft Corp.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "import/renderer_import_image_private.h"

namespace uvsr
{
    namespace
    {
        using Format = ImportImageFormat;
        struct FormatInfo { Format format; uint8_t fileCode, bits, blockBytes; };
        // supported DDS encodings. file codes belong to DDS, not the backend API.
        constexpr FormatInfo formats[]{
            {Format::Unknown, 0, 0, 0},
            {Format::R8_UINT, 62, 8, 0}, {Format::R8_SINT, 64, 8, 0}, {Format::R8_UNORM, 61, 8, 0}, {Format::R8_SNORM, 63, 8, 0},
            {Format::RG8_UINT, 50, 16, 0}, {Format::RG8_SINT, 52, 16, 0}, {Format::RG8_UNORM, 49, 16, 0}, {Format::RG8_SNORM, 51, 16, 0},
            {Format::R16_UINT, 57, 16, 0}, {Format::R16_SINT, 59, 16, 0}, {Format::R16_UNORM, 56, 16, 0}, {Format::R16_SNORM, 58, 16, 0}, {Format::R16_FLOAT, 54, 16, 0},
            {Format::BGRA4_UNORM, 115, 16, 0}, {Format::B5G6R5_UNORM, 85, 16, 0}, {Format::B5G5R5A1_UNORM, 86, 16, 0},
            {Format::RGBA8_UINT, 30, 32, 0}, {Format::RGBA8_SINT, 32, 32, 0}, {Format::RGBA8_UNORM, 28, 32, 0}, {Format::RGBA8_SNORM, 31, 32, 0},
            {Format::BGRA8_UNORM, 87, 32, 0}, {Format::BGRX8_UNORM, 88, 32, 0}, {Format::SRGBA8_UNORM, 29, 32, 0}, {Format::SBGRA8_UNORM, 91, 32, 0}, {Format::SBGRX8_UNORM, 93, 32, 0},
            {Format::R10G10B10A2_UNORM, 24, 32, 0}, {Format::R11G11B10_FLOAT, 26, 32, 0},
            {Format::RG16_UINT, 36, 32, 0}, {Format::RG16_SINT, 38, 32, 0}, {Format::RG16_UNORM, 35, 32, 0}, {Format::RG16_SNORM, 37, 32, 0}, {Format::RG16_FLOAT, 34, 32, 0},
            {Format::R32_UINT, 42, 32, 0}, {Format::R32_SINT, 43, 32, 0}, {Format::R32_FLOAT, 41, 32, 0},
            {Format::RGBA16_UINT, 12, 64, 0}, {Format::RGBA16_SINT, 14, 64, 0}, {Format::RGBA16_FLOAT, 10, 64, 0}, {Format::RGBA16_UNORM, 11, 64, 0}, {Format::RGBA16_SNORM, 13, 64, 0},
            {Format::RG32_UINT, 17, 64, 0}, {Format::RG32_SINT, 18, 64, 0}, {Format::RG32_FLOAT, 16, 64, 0},
            {Format::RGB32_UINT, 7, 96, 0}, {Format::RGB32_SINT, 8, 96, 0}, {Format::RGB32_FLOAT, 6, 96, 0},
            {Format::RGBA32_UINT, 3, 128, 0}, {Format::RGBA32_SINT, 4, 128, 0}, {Format::RGBA32_FLOAT, 2, 128, 0},
            {Format::D24S8, 46, 32, 0}, {Format::X24G8_UINT, 47, 32, 0}, {Format::D32S8, 21, 64, 0}, {Format::X32G8_UINT, 22, 64, 0},
            {Format::BC1_UNORM, 71, 4, 8}, {Format::BC1_UNORM_SRGB, 72, 4, 8}, {Format::BC2_UNORM, 74, 8, 16}, {Format::BC2_UNORM_SRGB, 75, 8, 16},
            {Format::BC3_UNORM, 77, 8, 16}, {Format::BC3_UNORM_SRGB, 78, 8, 16}, {Format::BC4_UNORM, 80, 4, 8}, {Format::BC4_SNORM, 81, 4, 8},
            {Format::BC5_UNORM, 83, 8, 16}, {Format::BC5_SNORM, 84, 8, 16}, {Format::BC6H_UFLOAT, 95, 8, 16}, {Format::BC6H_SFLOAT, 96, 8, 16},
            {Format::BC7_UNORM, 98, 8, 16}, {Format::BC7_UNORM_SRGB, 99, 8, 16}
        };
        constexpr bool FormatOrder() noexcept
        {
            if constexpr (sizeof(formats) / sizeof(formats[0]) != uint32_t(Format::Count)) return false;
            for (uint32_t i = 0; i < uint32_t(Format::Count); ++i) if (uint32_t(formats[i].format) != i) return false;
            return true;
        }
        static_assert(FormatOrder());

        ImportResult Failure(ImportError error = ImportError::InvalidContainer) noexcept
        { return {error, ImportObject::Image}; }
        bool Product(size_t a, size_t b, size_t& output) noexcept
        {
            if (a && b > size_t(PTRDIFF_MAX) / a) return false;
            output = a * b; return true;
        }
        bool Sum(size_t a, size_t b, size_t& output) noexcept
        {
            if (a > size_t(PTRDIFF_MAX) || b > size_t(PTRDIFF_MAX) - a) return false;
            output = a + b; return true;
        }
        uint32_t Word(const uint8_t* p) noexcept
        { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
        constexpr uint32_t FourCC(char a, char b, char c, char d) noexcept
        { return uint32_t(a) | (uint32_t(b) << 8) | (uint32_t(c) << 16) | (uint32_t(d) << 24); }
        Format SRGB(Format format) noexcept
        {
            switch (format)
            {
            case Format::RGBA8_UNORM: return Format::SRGBA8_UNORM;
            case Format::BGRA8_UNORM: return Format::SBGRA8_UNORM;
            case Format::BGRX8_UNORM: return Format::SBGRX8_UNORM;
            case Format::BC1_UNORM: return Format::BC1_UNORM_SRGB;
            case Format::BC2_UNORM: return Format::BC2_UNORM_SRGB;
            case Format::BC3_UNORM: return Format::BC3_UNORM_SRGB;
            case Format::BC7_UNORM: return Format::BC7_UNORM_SRGB;
            default: return format;
            }
        }

        struct Mask { uint32_t bits, r, g, b, a; Format format; };
        Format Masked(const uint8_t* pixelFormat, ArrayView<const Mask> masks) noexcept
        {
            for (size_t i = 0; i < masks.count; ++i)
            {
                const auto& mask = masks.data[i];
                if (Word(pixelFormat + 12) == mask.bits && Word(pixelFormat + 16) == mask.r &&
                    Word(pixelFormat + 20) == mask.g && Word(pixelFormat + 24) == mask.b && Word(pixelFormat + 28) == mask.a)
                    return mask.format;
            }
            return Format::Unknown;
        }
        Format Legacy(const uint8_t* pf, bool forceSRGB) noexcept
        {
            const uint32_t flags = Word(pf + 4), code = Word(pf + 8);
            if (flags & 0x40)
            {
                const Mask rgb[]{
                    {32,0xff,0xff00,0xff0000,0xff000000,Format::RGBA8_UNORM},
                    {32,0xff0000,0xff00,0xff,0xff000000,Format::BGRA8_UNORM},
                    {32,0xff0000,0xff00,0xff,0,Format::BGRA8_UNORM},
                    {32,0x3ff00000,0xffc00,0x3ff,0xc0000000,Format::R10G10B10A2_UNORM},
                    {32,0xffff,0xffff0000,0,0,Format::RG16_UNORM},
                    {32,0xffffffff,0,0,0,Format::R32_FLOAT},
                    {16,0x7c00,0x3e0,0x1f,0x8000,Format::B5G5R5A1_UNORM},
                    {16,0xf800,0x7e0,0x1f,0,Format::B5G6R5_UNORM},
                    {16,0xf00,0xf0,0xf,0xf000,Format::BGRA4_UNORM}
                };
                const auto format = Masked(pf, rgb);
                // retain the old uncompressed legacy inversion; DX10 and BC use
                // ordinary promotion. changing this would change existing pixels.
                return forceSRGB ? format : SRGB(format);
            }
            if (flags & 0x20000)
            {
                const Mask luminance[]{
                    {8,0xff,0,0,0,Format::R8_UNORM}, {8,0xff,0,0,0xff00,Format::RG8_UNORM},
                    {16,0xffff,0,0,0,Format::R16_UNORM}, {16,0xff,0,0,0xff00,Format::RG8_UNORM}
                };
                return Masked(pf, luminance);
            }
            if (flags & 0x2) return Word(pf + 12) == 8 ? Format::R8_UNORM : Format::Unknown;
            if (flags & 0x80000)
            {
                const Mask signedValues[]{
                    {16,0xff,0xff00,0,0,Format::RG8_SNORM},
                    {32,0xff,0xff00,0xff0000,0xff000000,Format::RGBA8_SNORM},
                    {32,0xffff,0xffff0000,0,0,Format::RG16_SNORM}
                };
                return Masked(pf, signedValues);
            }
            if (!(flags & 0x4)) return Format::Unknown;
            struct Code { uint32_t code; Format format; bool promote; };
            const Code codes[]{
                {FourCC('D','X','T','1'),Format::BC1_UNORM,true}, {FourCC('D','X','T','3'),Format::BC2_UNORM,true},
                {FourCC('D','X','T','5'),Format::BC3_UNORM,true}, {FourCC('D','X','T','2'),Format::BC2_UNORM,false},
                {FourCC('D','X','T','4'),Format::BC3_UNORM,false}, {FourCC('A','T','I','1'),Format::BC4_UNORM,false},
                {FourCC('B','C','4','U'),Format::BC4_UNORM,false}, {FourCC('B','C','4','S'),Format::BC4_SNORM,false},
                {FourCC('A','T','I','2'),Format::BC5_UNORM,false}, {FourCC('B','C','5','U'),Format::BC5_UNORM,false},
                {FourCC('B','C','5','S'),Format::BC5_SNORM,false}, {36,Format::RGBA16_UNORM,false},
                {110,Format::RGBA16_SNORM,false}, {111,Format::R16_FLOAT,false}, {112,Format::RG16_FLOAT,false},
                {113,Format::RGBA16_FLOAT,false}, {114,Format::R32_FLOAT,false}, {115,Format::RG32_FLOAT,false},
                {116,Format::RGBA32_FLOAT,false}
            };
            for (const auto& value : codes)
                if (value.code == code) return value.promote && forceSRGB ? SRGB(value.format) : value.format;
            return Format::Unknown;
        }

        ImportResult Level(const ImportDecodedImageInfo& info, size_t width, size_t height, size_t depth,
            size_t offset, ImportImageSubresource& output) noexcept
        {
            const auto& format = formats[uint32_t(info.format)];
            size_t row = 0, pitch = 0, size = 0;
            if (format.blockBytes)
            {
                size_t roundedWidth = 0, roundedHeight = 0;
                if (!Sum(width, 3, roundedWidth) || !Sum(height, 3, roundedHeight) ||
                    !Product(roundedWidth / 4, format.blockBytes, row) || !Product(row, roundedHeight / 4, pitch)) return Failure(ImportError::Overflow);
            }
            else if (!Product(width, format.bits / 8, row) || !Product(row, height, pitch)) return Failure(ImportError::Overflow);
            if (!Product(pitch, depth, size)) return Failure(ImportError::Overflow);
            output = {offset, row, pitch, size}; return {};
        }
    }

    uint32_t ImportImageBitsPerPixel(ImportImageFormat format) noexcept
    { return uint32_t(format) < uint32_t(Format::Count) ? formats[uint32_t(format)].bits : 0; }
    uint32_t ImportImageBlockBytes(ImportImageFormat format) noexcept
    { return uint32_t(format) < uint32_t(Format::Count) ? formats[uint32_t(format)].blockBytes : 0; }

    ImportResult PlanImportDDS(ArrayView<const uint8_t> bytes, bool forceSRGB, ImportImagePlan& output) noexcept
    {
        if (!bytes.IsValid() || bytes.count < 128 || Word(bytes.data) != FourCC('D','D','S',' ') ||
            Word(bytes.data + 4) != 124 || Word(bytes.data + 76) != 32) return Failure();
        ImportImagePlan plan;
        auto& info = plan.info;
        const auto* p = bytes.data;
        info.width = Word(p + 16); info.height = Word(p + 12);
        info.mipLevels = Word(p + 28); if (!info.mipLevels) info.mipLevels = 1;
        plan.dataOffset = 128;
        const bool dx10 = (Word(p + 80) & 4) && Word(p + 84) == FourCC('D','X','1','0');
        if (dx10)
        {
            if (bytes.count < 148) return Failure();
            plan.dataOffset = 148;
            const uint32_t code = Word(p + 128), dimension = Word(p + 132), count = Word(p + 140), alpha = Word(p + 144) & 7;
            if (!count) return Failure(ImportError::InvalidData);
            if (alpha >= 1 && alpha <= 4) info.alpha = RendererSceneTextureAlpha(alpha);
            for (const auto& format : formats) if (format.fileCode == code) { info.format = format.format; break; }
            if (forceSRGB) info.format = SRGB(info.format);
            switch (dimension)
            {
            case 2:
                if ((Word(p + 8) & 2) && info.height != 1) return Failure(ImportError::InvalidData);
                info.height = 1; info.arraySize = count; info.dimension = ImportImageDimension::Texture1D; break;
            case 3:
                info.arraySize = count;
                if (Word(p + 136) & 4)
                {
                    if (count > UINT32_MAX / 6) return Failure(ImportError::Overflow);
                    info.arraySize *= 6; info.dimension = ImportImageDimension::Cube;
                }
                break;
            case 4:
                if (!(Word(p + 8) & 0x800000) || count != 1) return Failure(ImportError::InvalidData);
                info.depth = Word(p + 24); info.dimension = ImportImageDimension::Texture3D; break;
            default: return Failure(ImportError::UnsupportedData);
            }
        }
        else
        {
            info.format = Legacy(p + 76, forceSRGB);
            if ((Word(p + 80) & 4) && (Word(p + 84) == FourCC('D','X','T','2') || Word(p + 84) == FourCC('D','X','T','4')))
                info.alpha = RendererSceneTextureAlpha::Premultiplied;
            if (Word(p + 8) & 0x800000) { info.depth = Word(p + 24); info.dimension = ImportImageDimension::Texture3D; }
            else if (Word(p + 112) & 0x200)
            {
                if ((Word(p + 112) & 0xfc00) != 0xfc00) return Failure(ImportError::InvalidData);
                info.arraySize = 6; info.dimension = ImportImageDimension::Cube;
            }
        }
        if (info.format == Format::Unknown) return Failure(ImportError::UnsupportedData);
        if (!info.width || !info.height || !info.depth || (info.dimension == ImportImageDimension::Cube && info.width != info.height))
            return Failure(ImportError::InvalidData);
        uint32_t largest = info.width > info.height ? info.width : info.height;
        if (info.depth > largest) largest = info.depth;
        uint32_t maximumMips = 0;
        while (largest) { ++maximumMips; largest >>= 1; }
        if (info.mipLevels > maximumMips) return Failure(ImportError::InvalidData);
        info.originalBitsPerPixel = ImportImageBitsPerPixel(info.format);
        if (!Product(info.arraySize, info.mipLevels, plan.subresources)) return Failure(ImportError::Overflow);
        size_t chain = 0, width = info.width, height = info.height, depth = info.depth;
        for (uint32_t mip = 0; mip < info.mipLevels; ++mip)
        {
            ImportImageSubresource level;
            const auto result = Level(info, width, height, depth, 0, level);
            if (!result) return result;
            if (!Sum(chain, level.size, chain)) return Failure(ImportError::Overflow);
            width = width > 1 ? width / 2 : 1; height = height > 1 ? height / 2 : 1; depth = depth > 1 ? depth / 2 : 1;
        }
        size_t payload = 0, end = 0;
        if (!Product(chain, info.arraySize, payload) || !Sum(plan.dataOffset, payload, end)) return Failure(ImportError::Overflow);
        if (end > bytes.count) return Failure(ImportError::InvalidRange);
        plan.dataBytes = bytes.count; // retain allowed trailing bytes and original file offsets.
        output = plan; return {};
    }

    ImportResult WriteImportImageLayout(const ImportImagePlan& plan, ArrayView<ImportImageSubresource> output) noexcept
    {
        if (!output.IsValid() || output.count != plan.subresources || plan.info.format == Format::Unknown ||
            uint32_t(plan.info.format) >= uint32_t(Format::Count)) return Failure(ImportError::InvalidOutput);
        size_t index = 0, offset = plan.dataOffset;
        for (uint32_t slice = 0; slice < plan.info.arraySize; ++slice)
        {
            size_t width = plan.info.width, height = plan.info.height, depth = plan.info.depth;
            for (uint32_t mip = 0; mip < plan.info.mipLevels; ++mip)
            {
                if (index == output.count) return Failure(ImportError::InvalidOutput);
                ImportImageSubresource level;
                const auto result = Level(plan.info, width, height, depth, offset, level);
                if (!result) return result;
                if (!Sum(offset, level.size, offset) || offset > plan.dataBytes) return Failure(ImportError::InvalidRange);
                output.data[index++] = level;
                width = width > 1 ? width / 2 : 1; height = height > 1 ? height / 2 : 1; depth = depth > 1 ? depth / 2 : 1;
            }
        }
        return index == output.count ? ImportResult{} : Failure(ImportError::InvalidOutput);
    }
}
