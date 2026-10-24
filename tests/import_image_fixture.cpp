#include "import_image_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    FILE* reference = nullptr;
    uint32_t comparisons = 0;

    void Require(bool value, const char* reason) noexcept
    {
        if (value) return;
        fprintf(stderr, "captured image comparison %u failed: %s\n", comparisons, reason);
        exit(1);
    }

    uint32_t U32() noexcept
    {
        uint32_t value;
        Require(fread(&value, 1, sizeof(value), reference) == sizeof(value), "complete reference field");
        return value;
    }

    uint64_t U64() noexcept
    {
        uint64_t value;
        Require(fread(&value, 1, sizeof(value), reference) == sizeof(value), "complete reference field");
        return value;
    }

    void MatchBytes(const void* data, size_t count, const char* reason) noexcept
    {
        unsigned char expected[256];
        const auto* actual = static_cast<const unsigned char*>(data);
        while (count)
        {
            const size_t size = count < sizeof(expected) ? count : sizeof(expected);
            Require(fread(expected, 1, size, reference) == size, "complete reference bytes");
            Require(!memcmp(expected, actual, size), reason);
            actual += size;
            count -= size;
        }
    }

    void Input(const uvsr::ImportImageView& input, bool srgb, uint32_t tag) noexcept
    {
        if (!reference)
        {
            // fixed test data, captured from the independent decoder in import_image_fixture.md.
            const uint32_t endian = 1;
            Require(*reinterpret_cast<const unsigned char*>(&endian) == 1, "little-endian fixture");
            reference = fopen("import_image_fixture.bin", "rb");
            Require(reference != nullptr, "open reference");
            MatchBytes("UVII0001", 8, "reference version");
            Require(U32() == 350, "reference case count");
        }
        Require(comparisons < 350 && U32() == tag && U32() == ++comparisons, "reference case order");
        Require(U32() == input.path.count, "reference path length");
        MatchBytes(input.path.data, input.path.count, "reference path");
        Require(U32() == input.mimeType.count, "reference MIME length");
        MatchBytes(input.mimeType.data, input.mimeType.count, "reference MIME");
        Require(U32() == uint32_t(srgb) && U64() == input.bytes.count, "reference decode inputs");
        uint64_t fingerprint = 14695981039346656037ull;
        for (size_t i = 0; i < input.bytes.count; ++i)
        { fingerprint ^= input.bytes.data[i]; fingerprint *= 1099511628211ull; }
        Require(U64() == fingerprint, "encoded input fingerprint");
    }
}

void CompareImportImageReference(const uvsr::ImportImageView& input, bool srgb,
    const uvsr::ImportDecodedImage& image) noexcept
{
    Input(input, srgb, 1);
    uint32_t expected[11];
    Require(fread(expected, 1, sizeof(expected), reference) == sizeof(expected), "complete reference metadata");
    const auto info = image.Info();
    const auto layout = image.Subresources();
    const auto bytes = image.Bytes();
    Require(uint32_t(info.format) == expected[0] && uint32_t(info.dimension) == expected[1], "captured format/dimension");
    Require(uint32_t(info.alpha) == expected[2] && info.width == expected[3] && info.height == expected[4] &&
        info.depth == expected[5] && info.arraySize == expected[6] && info.mipLevels == expected[7] &&
        info.originalBitsPerPixel == expected[8] && uint32_t(info.allowGeneratedMips) == expected[9], "captured image metadata");
    Require(layout.count == expected[10], "subresource count");
    for (size_t index = 0; index < layout.count; ++index)
    {
        const uint64_t offset = U64(), rowPitch = U64(), depthPitch = U64(), size = U64();
        const auto& target = layout.data[index];
        Require(target.offset == offset && target.rowPitch == rowPitch && target.size == size &&
            (!depthPitch || target.depthPitch == depthPitch), "captured subresource layout");
        Require(target.offset <= bytes.count && target.size <= bytes.count - target.offset, "owned subresource range");
        MatchBytes(bytes.data + target.offset, target.size, "exact captured decoded pixels");
    }
}

uint32_t ReadImportImageArrayReference(const uvsr::ImportImageView& input) noexcept
{
    Input(input, false, 2);
    return U32();
}

void FinishImportImageReference() noexcept
{
    Require(reference != nullptr && comparisons == 350, "all reference cases consumed");
    MatchBytes("UVIIEND1", 8, "reference footer");
    Require(fgetc(reference) == EOF && !ferror(reference), "exact reference length");
    Require(fclose(reference) == 0, "close reference");
    reference = nullptr;
}
