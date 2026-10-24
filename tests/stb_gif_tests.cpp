#include "renderer_import_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;

    void Require(bool value, const char* reason) noexcept
    {
        if (value) return;
        fprintf(stderr, "GIF decode failed: %s\n", reason);
        exit(1);
    }

    const uint8_t Palette[4][3]{{19,73,211}, {229,47,31}, {53,197,83}, {157,101,233}};

    struct Gif
    {
        // the largest fixture emits 4093 codes of at most 12 bits plus headers.
        uint8_t data[8192]{};
        uint8_t block[255]{};
        size_t count = 0, blockCount = 0;
        uint32_t bits = 0, bitCount = 0;

        void Byte(uint32_t value) noexcept
        {
            Require(count < sizeof(data) && value <= 255, "encoded fixture capacity");
            data[count++] = uint8_t(value);
        }
        void Word(uint32_t value) noexcept
        {
            Require(value <= 65535, "fixture dimension");
            Byte(value & 255); Byte(value >> 8);
        }
        void FlushBlock() noexcept
        {
            if (!blockCount) return;
            Byte(uint32_t(blockCount));
            for (size_t i = 0; i < blockCount; ++i) Byte(block[i]);
            blockCount = 0;
        }
        void RasterByte(uint32_t value) noexcept
        {
            if (blockCount == sizeof(block)) FlushBlock();
            block[blockCount++] = uint8_t(value);
        }
        void Code(uint32_t code, uint32_t width) noexcept
        {
            Require(width <= 12 && code < (1u << width), "fixture code width");
            bits |= code << bitCount;
            bitCount += width;
            while (bitCount >= 8)
            {
                RasterByte(bits & 255); bits >>= 8; bitCount -= 8;
            }
        }
        void Finish(uint32_t width) noexcept
        {
            Code(5, width);
            if (bitCount) RasterByte(bits & 255);
            FlushBlock(); Byte(0); Byte(0x3b);
        }
        void Begin(uint32_t width, uint32_t height, bool interlaced, bool transparent) noexcept
        {
            const char signature[] = "GIF89a";
            for (size_t i = 0; i < 6; ++i) Byte(uint8_t(signature[i]));
            Word(width); Word(height); Byte(0x81); Byte(0); Byte(0);
            for (const auto& color : Palette)
                for (uint8_t component : color) Byte(component);
            if (transparent)
            {
                Byte(0x21); Byte(0xf9); Byte(4); Byte(1);
                Word(0); Byte(1); Byte(0);
            }
            Byte(0x2c); Word(0); Word(0); Word(width); Word(height);
            Byte(interlaced ? 0x40 : 0); Byte(2);
        }
        ImportImageView View() const noexcept
        {
            // content sniffing preserves GIF support even without a GIF suffix.
            static const char path[] = "fixture.image";
            return {{path, sizeof(path) - 1}, {}, {data, count}, true};
        }
    };

    uint32_t Color(uint32_t x, uint32_t y) noexcept { return (x + 2 * y + y / 3) % 4; }

    void CheckColor(const uint8_t* pixel, uint32_t color, bool transparent) noexcept
    {
        if (transparent && color == 1)
            Require(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 0, "transparent pixel");
        else
            Require(!memcmp(pixel, Palette[color], 3) && pixel[3] == 255, "known palette pixel");
    }

    void Grid(bool interlaced, bool transparent) noexcept
    {
        Gif input; input.Begin(4, 8, interlaced, transparent);
        const uint32_t rows[]{0,4,2,6,1,3,5,7};
        for (uint32_t row = 0; row < 8; ++row)
            for (uint32_t x = 0; x < 4; ++x)
            {
                input.Code(4, 3);
                input.Code(Color(x, interlaced ? rows[row] : row), 3);
            }
        input.Finish(3);
        ImportDecodedImage image;
        Require(bool(image.Decode(input.View())), "decode palette grid");
        Require(image.Info().width == 4 && image.Info().height == 8 && image.Bytes().count == 128, "grid layout");
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 4; ++x)
                CheckColor(image.Bytes().data + 4 * (4 * y + x), Color(x, y), transparent);

        const auto retained = image.Bytes();
        Gif invalid; invalid.Begin(4, 8, false, false);
        invalid.Code(4, 3); invalid.Code(0, 3); invalid.Code(7, 3); invalid.Finish(3);
        Require(!image.Decode(invalid.View()), "reject undefined dictionary code");
        Require(image.Bytes().data == retained.data && image.Bytes().count == retained.count, "preserve owner on malformed GIF");
        for (uint32_t y = 0; y < 8; ++y)
            for (uint32_t x = 0; x < 4; ++x)
                CheckColor(image.Bytes().data + 4 * (4 * y + x), Color(x, y), transparent);
    }

    void LongPrefixChain() noexcept
    {
        // each new dictionary code repeats the previous all-zero string plus
        // one symbol. the final code expands to 4091 pixels; all codes together
        // fill exactly 4091 * 2046 pixels, independently known to be palette 0.
        Gif input; input.Begin(4091, 2046, false, false);
        input.Code(4, 3); input.Code(0, 3);
        uint32_t width = 3;
        for (uint32_t code = 6; code <= 4095; ++code)
        {
            input.Code(code, width);
            if (code + 1 == (1u << width) && width < 12) ++width;
        }
        input.Finish(width);
        ImportDecodedImage image;
        Require(bool(image.Decode(input.View())), "decode longest 12-bit prefix chain");
        constexpr size_t pixels = 4091u * 2046u;
        Require(image.Info().width == 4091 && image.Info().height == 2046 && image.Bytes().count == pixels * 4, "deep GIF layout");
        for (size_t i = 0; i < pixels; ++i) CheckColor(image.Bytes().data + 4 * i, 0, false);
    }
}

void VerifyGifImages() noexcept
{
    Grid(false, false); Grid(true, false); Grid(false, true); Grid(true, true);
    LongPrefixChain();
    printf("GIF decode passed: four known grids, four preserved-output rejections, 4091-deep dictionary and 8370186 known pixels\n");
}
