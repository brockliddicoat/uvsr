/*
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "renderer_texture_bmp.h"

#include <limits.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

namespace uvsr
{
namespace
{
    constexpr uint32_t BmpHeaderSize = 54u;

    void StoreU16(uint8_t* header, size_t offset, uint16_t value) noexcept
    {
        header[offset] = uint8_t(value);
        header[offset + 1u] = uint8_t(value >> 8u);
    }

    void StoreU32(uint8_t* header, size_t offset, uint32_t value) noexcept
    {
        for (size_t byte = 0u; byte < 4u; ++byte)
            header[offset + byte] = uint8_t(value >> (byte * 8u));
    }
}

bool WriteRendererBmp(const wchar_t* path, uint32_t width, uint32_t height,
    size_t sourceRowPitch, const void* rgbaPixels) noexcept
{
    if (!path || !path[0] || !rgbaPixels || !width || !height ||
        width > uint32_t(INT32_MAX) || height > uint32_t(INT32_MAX)) return false;
    constexpr uint64_t BytesPerPixel = 4u;
    const uint64_t packedRowBytes = uint64_t(width) * BytesPerPixel;
    const uint64_t pixelBytes = packedRowBytes * height;
    if (sourceRowPitch < packedRowBytes || pixelBytes > UINT32_MAX - BmpHeaderSize) return false;
    const size_t rowBytes = size_t(packedRowBytes);
    const size_t precedingRows = height - 1u;
    if (precedingRows && sourceRowPitch > (SIZE_MAX - rowBytes) / precedingRows) return false;
    const size_t sourceBytes = precedingRows * sourceRowPitch + rowBytes;
    if (sourceBytes > size_t(PTRDIFF_MAX) ||
        sourceBytes > UINTPTR_MAX - reinterpret_cast<uintptr_t>(rgbaPixels)) return false;

    FILE* output = _wfsopen(path, L"wb", _SH_DENYNO);
    if (!output) return false;
    uint8_t header[BmpHeaderSize]{};
    header[0] = 'B'; header[1] = 'M';
    StoreU32(header, 2u, BmpHeaderSize + uint32_t(pixelBytes));
    StoreU32(header, 10u, BmpHeaderSize);
    StoreU32(header, 14u, 40u);
    StoreU32(header, 18u, width);
    StoreU32(header, 22u, height);
    StoreU16(header, 26u, 1u);
    StoreU16(header, 28u, 32u);
    StoreU32(header, 34u, uint32_t(pixelBytes));
    bool written = fwrite(header, 1, sizeof(header), output) == sizeof(header);
    auto* bgraRow = written ? static_cast<uint8_t*>(malloc(rowBytes)) : nullptr;
    if (written && !bgraRow) written = false;
    const auto* source = static_cast<const uint8_t*>(rgbaPixels);
    for (uint32_t outputRow = 0u; written && outputRow < height; ++outputRow)
    {
        const uint32_t sourceRow = height - outputRow - 1u;
        const uint8_t* sourcePixel = source + size_t(sourceRow) * sourceRowPitch;
        for (uint32_t column = 0u; column < width; ++column)
        {
            const size_t offset = size_t(column) * 4u;
            bgraRow[offset] = sourcePixel[offset + 2u];
            bgraRow[offset + 1u] = sourcePixel[offset + 1u];
            bgraRow[offset + 2u] = sourcePixel[offset];
            bgraRow[offset + 3u] = sourcePixel[offset + 3u];
        }
        written = fwrite(bgraRow, 1, rowBytes, output) == rowBytes;
    }
    free(bgraRow);
    const bool closed = fclose(output) == 0;
    return written && closed;
}
}
