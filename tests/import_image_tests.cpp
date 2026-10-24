#include "renderer_import_image.h"
#include "renderer_import_path.h"

#include "import_image_fixture.h"

#include <filesystem>
#include <string>
#include <stb_image_write.h>
#include <tinyexr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void VerifyGifImages() noexcept;

namespace
{
    using namespace uvsr;
    size_t comparisons = 0, rejections = 0, retries = 0;
    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "image import failed: %s\n", reason); exit(1);
    }
    void Good(ImportResult result, const char* reason)
    {
        if (result) return;
        fprintf(stderr, "image import failed: %s: %s, object %u, index %zu, parser %u\n",
            reason, ImportErrorText(result.error), unsigned(result.object), result.index, result.parserCode); exit(1);
    }
    ArrayView<const char> Text(const char* text) { return {text, strlen(text)}; }
    struct Encoded
    {
        uint8_t* data = static_cast<uint8_t*>(malloc(1 << 20));
        size_t count = 0;
        Encoded() { Require(data != nullptr, "encoded fixture storage"); }
        ~Encoded() { free(data); }
        Encoded(const Encoded&) = delete;
        void Put(size_t offset, uint32_t value)
        { Require(offset <= (1 << 20) - 4, "fixture word range"); memcpy(data + offset, &value, 4); }
        uint32_t Word(size_t offset) const
        { Require(offset <= count && 4 <= count - offset, "read fixture word"); uint32_t result = 0; memcpy(&result, data + offset, 4); return result; }
        size_t Attribute(const char* wanted) const
        {
            size_t offset = 8;
            while (offset < count && data[offset])
            {
                const char* name = reinterpret_cast<const char*>(data + offset);
                offset += strlen(name) + 1;
                offset += strlen(reinterpret_cast<const char*>(data + offset)) + 1;
                const size_t size = Word(offset); offset += 4;
                if (!strcmp(name, wanted)) return offset;
                Require(size <= count - offset, "EXR fixture attribute range"); offset += size;
            }
            return SIZE_MAX;
        }
        size_t HeaderEnd() const
        {
            size_t offset = 8;
            while (offset < count && data[offset])
            {
                offset += strlen(reinterpret_cast<const char*>(data + offset)) + 1;
                offset += strlen(reinterpret_cast<const char*>(data + offset)) + 1;
                const size_t size = Word(offset); offset += 4;
                Require(size <= count - offset, "EXR fixture attribute range"); offset += size;
            }
            Require(offset < count, "EXR fixture header terminator"); return offset + 1;
        }
        static void Write(void* context, void* bytes, int size)
        {
            auto& self = *static_cast<Encoded*>(context);
            Require(size >= 0 && size_t(size) <= (1 << 20) - self.count, "encoded fixture capacity");
            memcpy(self.data + self.count, bytes, size_t(size)); self.count += size_t(size);
        }
        ImportImageView View(const char* path, const char* mime = "") const
        { return {Text(path), Text(mime), {data, count}, true}; }
        void Png(uint32_t channels, int kind = 0)
        {
            uint8_t pixels[7 * 5 * 4];
            for (size_t i = 0; i < sizeof(pixels); ++i) pixels[i] = uint8_t(i * 37 + 11);
            count = 0;
            int result = 0;
            if (kind == 0) result = stbi_write_png_to_func(Write, this, 7, 5, int(channels), pixels, 7 * int(channels));
            else if (kind == 1) result = stbi_write_bmp_to_func(Write, this, 7, 5, int(channels), pixels);
            else if (kind == 2) result = stbi_write_tga_to_func(Write, this, 7, 5, int(channels), pixels);
            else result = stbi_write_jpg_to_func(Write, this, 7, 5, int(channels), pixels, 95);
            Require(result != 0, "encode generic fixture");
        }
        void Hdr()
        {
            float pixels[7 * 5 * 3];
            for (size_t i = 0; i < sizeof(pixels) / sizeof(float); ++i) pixels[i] = float(i + 1) * 0.25f;
            count = 0;
            Require(stbi_write_hdr_to_func(Write, this, 7, 5, 3, pixels) != 0, "encode HDR fixture");
        }
        void Exr(int channelCount, bool half = false, int compression = TINYEXR_COMPRESSIONTYPE_ZIP, bool integer = false)
        {
            EXRHeader header; InitEXRHeader(&header);
            EXRImage image; InitEXRImage(&image);
            EXRChannelInfo channels[5]{};
            int types[5]{}, requested[5]{};
            unsigned char* planes[5]{};
            float pixels[5][35];
            const char* names[]{"B", "G", "R", "A", "unused"};
            for (int c = 0; c < channelCount; ++c)
            {
                strcpy_s(channels[c].name, names[c]);
                channels[c].x_sampling = 1; channels[c].y_sampling = 1;
                types[c] = integer ? TINYEXR_PIXELTYPE_UINT : TINYEXR_PIXELTYPE_FLOAT;
                requested[c] = half ? TINYEXR_PIXELTYPE_HALF : types[c];
                for (size_t i = 0; i < 35; ++i) pixels[c][i] = float(i + 1) * float(c + 1) * 0.125f;
                planes[c] = reinterpret_cast<unsigned char*>(pixels[c]);
            }
            header.num_channels = channelCount; header.channels = channels;
            header.pixel_types = types; header.requested_pixel_types = requested; header.compression_type = compression;
            image.num_channels = channelCount; image.width = 7; image.height = 5; image.images = planes;
            unsigned char* memory = nullptr; const char* error = nullptr;
            count = SaveEXRImageToMemory(&image, &header, &memory, &error);
            if (!count) fprintf(stderr, "EXR encode: %s\n", error ? error : "no error");
            FreeEXRErrorMessage(error);
            Require(count > 0 && count < (1 << 20) && memory, "encode EXR fixture");
            memcpy(data, memory, count); free(memory);
        }
        void TiledExr(int channels)
        {
            Exr(channels, false, TINYEXR_COMPRESSIONTYPE_NONE);
            count = HeaderEnd() - 1; Put(4, 0x202);
            const char attribute[] = "tiles\0tiledesc";
            memcpy(data + count, attribute, sizeof(attribute)); count += sizeof(attribute);
            Put(count, 9); Put(count + 4, 3); Put(count + 8, 2);
            data[count + 12] = 0; data[count + 13] = 0; count += 14;
            const size_t table = count; memset(data + count, 0, 9 * 8); count += 9 * 8;
            // physical chunk order is reversed; the offset table maps each tile.
            for (int t = 8; t >= 0; --t)
            {
                const uint32_t x = uint32_t(t % 3), y = uint32_t(t / 3);
                const uint32_t width = x == 2 ? 1 : 3, height = y == 2 ? 1 : 2;
                Put(table + size_t(t) * 8, uint32_t(count));
                Put(count, x); Put(count + 4, y); Put(count + 8, 0); Put(count + 12, 0);
                Put(count + 16, width * height * uint32_t(channels) * 4); count += 20;
                for (uint32_t j = 0; j < height; ++j)
                    for (int c = 0; c < channels; ++c)
                        for (uint32_t i = 0; i < width; ++i)
                        {
                            const float value = float((y * 2 + j) * 7 + x * 3 + i + 1) * float(c + 1) * 0.125f;
                            memcpy(data + count, &value, 4); count += 4;
                        }
            }
        }
        void Dds(uint32_t code, uint32_t width = 7, uint32_t height = 5,
            uint32_t mips = 1, uint32_t dimension = 3, uint32_t arrays = 1, uint32_t depth = 1, bool cube = false)
        {
            count = 65536; memset(data, 0, count);
            Put(0, 0x20534444); Put(4, 124); Put(8, dimension == 4 ? 0x801007 : 0x1007);
            Put(12, height); Put(16, width); Put(24, depth); Put(28, mips);
            Put(76, 32); Put(80, 4); Put(84, 0x30315844); Put(108, 0x1000);
            Put(128, code); Put(132, dimension); Put(136, cube ? 4 : 0); Put(140, arrays);
            for (size_t i = 148; i < count; ++i) data[i] = uint8_t(i * 31 + 7);
        }
    };

    struct FormatCase { uint32_t code; };
#define F(name, code) {code}
    const FormatCase formats[]{
        F(R8_UINT,62), F(R8_SINT,64), F(R8_UNORM,61), F(R8_SNORM,63),
        F(RG8_UINT,50), F(RG8_SINT,52), F(RG8_UNORM,49), F(RG8_SNORM,51),
        F(R16_UINT,57), F(R16_SINT,59), F(R16_UNORM,56), F(R16_SNORM,58), F(R16_FLOAT,54),
        F(BGRA4_UNORM,115), F(B5G6R5_UNORM,85), F(B5G5R5A1_UNORM,86),
        F(RGBA8_UINT,30), F(RGBA8_SINT,32), F(RGBA8_UNORM,28), F(RGBA8_SNORM,31),
        F(BGRA8_UNORM,87), F(BGRX8_UNORM,88), F(SRGBA8_UNORM,29), F(SBGRA8_UNORM,91), F(SBGRX8_UNORM,93),
        F(R10G10B10A2_UNORM,24), F(R11G11B10_FLOAT,26),
        F(RG16_UINT,36), F(RG16_SINT,38), F(RG16_UNORM,35), F(RG16_SNORM,37), F(RG16_FLOAT,34),
        F(R32_UINT,42), F(R32_SINT,43), F(R32_FLOAT,41),
        F(RGBA16_UINT,12), F(RGBA16_SINT,14), F(RGBA16_FLOAT,10), F(RGBA16_UNORM,11), F(RGBA16_SNORM,13),
        F(RG32_UINT,17), F(RG32_SINT,18), F(RG32_FLOAT,16), F(RGB32_UINT,7), F(RGB32_SINT,8), F(RGB32_FLOAT,6),
        F(RGBA32_UINT,3), F(RGBA32_SINT,4), F(RGBA32_FLOAT,2), F(D24S8,46), F(X24G8_UINT,47), F(D32S8,21), F(X32G8_UINT,22),
        F(BC1_UNORM,71), F(BC1_UNORM_SRGB,72), F(BC2_UNORM,74), F(BC2_UNORM_SRGB,75), F(BC3_UNORM,77), F(BC3_UNORM_SRGB,78),
        F(BC4_UNORM,80), F(BC4_SNORM,81), F(BC5_UNORM,83), F(BC5_SNORM,84), F(BC6H_UFLOAT,95), F(BC6H_SFLOAT,96),
        F(BC7_UNORM,98), F(BC7_UNORM_SRGB,99)
    };
#undef F
    void Compare(const ImportImageView& input, bool srgb = false)
    {
        ImportDecodedImage image;
        ImportImageDecodeOptions options; options.forceSRGB = srgb;
        Good(image.Decode(input, options), "candidate image decode");
        CompareImportImageReference(input, srgb, image);
        ++comparisons;
    }
    uint64_t Digest(const ImportDecodedImage& image)
    {
        uint64_t value = 14695981039346656037ull;
        const auto bytes = image.Bytes();
        for (size_t i = 0; i < bytes.count; ++i) { value ^= bytes.data[i]; value *= 1099511628211ull; }
        const auto layout = image.Subresources();
        for (size_t i = 0; i < layout.count; ++i)
        {
            value ^= layout.data[i].offset; value *= 1099511628211ull;
            value ^= layout.data[i].size; value *= 1099511628211ull;
        }
        value ^= uint32_t(image.Info().format); value ^= image.StorageBytes(); return value;
    }
    void Reject(ImportDecodedImage& image, const ImportImageView& input, ImportImageDecodeOptions options = {},
        ImportError expected = ImportError::None)
    {
        const uint64_t digest = Digest(image); const auto pointer = image.Bytes().data;
        const auto result = image.Decode(input, options);
        Require(!result && (expected == ImportError::None || result.error == expected), "explicit image rejection");
        Require(digest == Digest(image) && pointer == image.Bytes().data, "failure preserves prior image"); ++rejections;
    }
    void LifetimeAndFailures()
    {
        Encoded input; input.Png(4);
        ImportDecodedImage image; Good(image.Decode(input.View("image.png")), "initial image");
        const size_t storage = image.StorageBytes(); const uint64_t digest = Digest(image);
        ImportImageDecodeOptions options; options.maxStorageBytes = storage - 1;
        Reject(image, input.View("image.png"), options, ImportError::Capacity);
        options.maxStorageBytes = storage; Good(image.Decode(input.View("image.png"), options), "exact storage limit");
        options.maxEncodedBytes = input.count - 1; Reject(image, input.View("image.png"), options, ImportError::Capacity);
        options = {}; Good(image.Decode(input.View("image.png"), options), "retry after capacity");
        memset(input.data, 0xcc, input.count); Require(digest == Digest(image), "encoded storage released after decode");
        ImportDecodedImage moved(static_cast<ImportDecodedImage&&>(image));
        Require(!image.Bytes().count && Digest(moved) == digest, "move construction");
        image = static_cast<ImportDecodedImage&&>(moved);
        Require(!moved.Bytes().count && Digest(image) == digest, "move assignment");
        auto* self = &image; image = static_cast<ImportDecodedImage&&>(*self);
        Require(Digest(image) == digest, "self move");
        Reject(image, {});
        auto bad = input.View("image.png"); bad.bytes = {nullptr, 3}; Reject(image, bad);
        bad = input.View("image.png"); bad.bytes.count = SIZE_MAX; Reject(image, bad);
        bad = input.View("image.png"); bad.path = {"bad\0path", 8}; Reject(image, bad);
        bad = input.View("image.png"); bad.mimeType = {nullptr, 1}; Reject(image, bad);
        for (int kind = 0; kind < 3; ++kind)
        {
            if (kind == 0) input.Png(4); else if (kind == 1) input.Dds(28); else input.Exr(4);
            const char* path = kind == 0 ? "image.png" : kind == 1 ? "image.dds" : "image.exr";
            size_t failures = 0;
            for (int64_t allocation = 0; allocation < 12; ++allocation)
            {
                const uint64_t before = Digest(image); const auto pointer = image.Bytes().data;
                SetImportAllocationFailureCountdown(allocation);
                const auto result = image.Decode(input.View(path));
                SetImportAllocationFailureCountdown(-1);
                if (result) break;
                Require(result.error == ImportError::OutOfMemory && before == Digest(image) && pointer == image.Bytes().data,
                    "allocation failure preserves output");
                Good(image.Decode(input.View(path)), "allocation retry"); ++failures; ++retries;
            }
            Require(failures >= 2 && failures < 12, "all first-party allocation boundaries reached");
        }
        image.Reset(); image.Reset(); Require(!image.Bytes().count && !image.Subresources().count && !image.StorageBytes(), "idempotent reset");
        printf("owned image limit: %zu bytes; allocation retries: %zu\n", storage, retries);
    }
    void DDSCases()
    {
        Encoded input;
        for (const auto& format : formats)
        {
            input.Dds(format.code); Compare(input.View("image.dds")); Compare(input.View("image.DDS"), true);
        }
        for (uint32_t alpha = 0; alpha < 8; ++alpha)
        { input.Dds(28); input.Put(144, alpha); Compare(input.View("alpha.dds")); }
        input.Dds(28, 8, 4, 3, 3, 3); Compare(input.View("array.dds"));
        input.Dds(71, 7, 7, 3, 3, 2, 1, true); Compare(input.View("cube.dds"));
        input.Dds(28, 8, 4, 4, 4, 1, 4); Compare(input.View("volume.dds"));
        input.Dds(28, 8, 1, 4, 2); Compare(input.View("line.dds"));
        input.Dds(71, 1, 1); Compare(input.View("block.dds"));
        input.Dds(28); Compare(input.View("untyped", "image/vnd-ms.dds"));
        ImportDecodedImage image; Good(image.Decode(input.View("image.dds")), "DDS rejection seed");
        const size_t shortSizes[]{0, 3, 4, 75, 127, 147, 148};
        for (const auto size : shortSizes) { auto bad = input.View("image.dds"); bad.bytes.count = size; Reject(image, bad); }
        struct Bad { size_t offset; uint32_t value; };
        const Bad badWords[]{{0,0}, {4,0}, {76,0}, {128,0}, {132,0}, {140,0}, {12,0}, {16,0}, {28,33}, {140,UINT32_MAX}};
        for (const auto& bad : badWords)
        { input.Dds(28); input.Put(bad.offset, bad.value); Reject(image, input.View("image.dds")); }
        input.Dds(28, 8, 4, 1, 4, 2, 3); Reject(image, input.View("volume.dds"), {}, ImportError::InvalidData);
        input.Dds(28, 8, 4, 1, 4, 1, 0); Reject(image, input.View("volume.dds"), {}, ImportError::InvalidData);
        input.Dds(28, 8, 4, 1, 3, 1, 1, true); Reject(image, input.View("cube.dds"), {}, ImportError::InvalidData);
        input.Dds(28, 8, 8, 1, 3, UINT32_MAX, 1, true); Reject(image, input.View("cube.dds"), {}, ImportError::Overflow);
        input.Dds(28, 8, 1, 1, 2, 2);
        const uint32_t nativeArraySize = ReadImportImageArrayReference(input.View("line.dds"));
        Good(image.Decode(input.View("line.dds")), "corrected complete 1D array");
        Require(nativeArraySize == 1 && image.Info().arraySize == 2 && image.Subresources().count == 2,
            "native 1D array omission classified separately");
        for (size_t slice = 0; slice < 2; ++slice)
        {
            const auto subresource = image.Subresources().data[slice];
            const auto bytes = image.Bytes();
            Require(subresource.offset <= bytes.count && subresource.size == 32 && subresource.size <= bytes.count - subresource.offset &&
                !memcmp(bytes.data + subresource.offset, input.data + 148 + slice * 32, 32), "complete 1D array slice pixels");
        }
        printf("DDS format controls: %zu; corrected 1D array copies both slices\n", sizeof(formats) / sizeof(formats[0]));
    }

    void LegacyDDS()
    {
        Encoded input;
        struct Mask { uint32_t flags, bits, r, g, b, a; };
        const Mask masks[]{
            {0x40,32,0xff,0xff00,0xff0000,0xff000000}, {0x40,32,0xff0000,0xff00,0xff,0xff000000},
            {0x40,32,0xff0000,0xff00,0xff,0}, {0x40,32,0x3ff00000,0xffc00,0x3ff,0xc0000000},
            {0x40,32,0xffff,0xffff0000,0,0}, {0x40,32,0xffffffff,0,0,0},
            {0x40,16,0x7c00,0x3e0,0x1f,0x8000}, {0x40,16,0xf800,0x7e0,0x1f,0}, {0x40,16,0xf00,0xf0,0xf,0xf000},
            {0x20000,8,0xff,0,0,0}, {0x20000,8,0xff,0,0,0xff00}, {0x20000,16,0xffff,0,0,0}, {0x20000,16,0xff,0,0,0xff00},
            {0x2,8,0,0,0,0}, {0x80000,16,0xff,0xff00,0,0},
            {0x80000,32,0xff,0xff00,0xff0000,0xff000000}, {0x80000,32,0xffff,0xffff0000,0,0}
        };
        for (const auto& mask : masks)
        {
            input.Dds(28); input.Put(80, mask.flags); input.Put(84, 0); input.Put(88, mask.bits);
            input.Put(92, mask.r); input.Put(96, mask.g); input.Put(100, mask.b); input.Put(104, mask.a);
            Compare(input.View("legacy.dds")); Compare(input.View("legacy.dds"), true);
        }
        const uint32_t codes[]{0x31545844,0x33545844,0x35545844,0x32545844,0x34545844,
            0x31495441,0x55344342,0x53344342,0x32495441,0x55354342,0x53354342,36,110,111,112,113,114,115,116};
        for (const uint32_t code : codes)
        {
            input.Dds(28); input.Put(84, code);
            Compare(input.View("legacy.dds")); Compare(input.View("legacy.dds"), true);
        }
        input.Dds(28, 8, 8, 4); input.Put(84, 0x31545844); input.Put(112, 0xfe00); Compare(input.View("legacy-cube.dds"));
        input.Dds(28, 8, 4, 4, 4, 1, 4); input.Put(84, 0x34545844); Compare(input.View("legacy-volume.dds"), true);
        ImportDecodedImage image; Good(image.Decode(input.View("legacy-volume.dds")), "legacy rejection seed");
        input.Put(112, 0x200); input.Put(8, 0x1007); Reject(image, input.View("partial-cube.dds"));
        input.Dds(28); input.Put(80, 0x40); input.Put(88, 24); Reject(image, input.View("unsupported.dds"));
        input.Dds(28); Compare(input.View("C:image.dds:stream"));
    }

    void EXRCases()
    {
        Encoded input;
        const int counts[]{1, 3, 4, 5};
        for (const int channels : counts)
        {
            input.TiledExr(channels); Compare(input.View("tiled.exr"));
            ImportDecodedImage image; Good(image.Decode(input.View("tiled.exr")), "tiled literal image");
            for (size_t i = 0; i < 35; ++i)
                for (size_t c = 0; c < 4; ++c)
                {
                    const size_t source = channels == 1 ? 0 : c == 0 ? 2 : c == 1 ? 1 : c == 2 ? 0 : 3;
                    const float expected = c == 3 && channels == 3 ? 1.f : float(i + 1) * float(source + 1) * 0.125f;
                    float value = 0; memcpy(&value, image.Bytes().data + (i * 4 + c) * 4, 4);
                    Require(value == expected, "tiled edge and channel literal pixels");
                }
        }
        input.Exr(4);
        ImportDecodedImage image; Good(image.Decode(input.View("image.exr")), "EXR rejection seed");
        const size_t originalCount = input.count;
        for (size_t size = 0; size < originalCount; ++size)
        { auto view = input.View("image.exr"); view.bytes.count = size; Reject(image, view); }
        const uint32_t versions[]{0, 0x802, 0x1002};
        for (const auto version : versions)
        { input.Exr(4); input.Put(4, version); Reject(image, input.View("image.exr")); }
        input.Exr(2); Reject(image, input.View("image.exr"), {}, ImportError::UnsupportedData);
        input.Exr(4); input.data[input.Attribute("channels")] = 'Z';
        Reject(image, input.View("image.exr"), {}, ImportError::UnsupportedData);
        input.Exr(4); input.Put(input.Attribute("channels") + 10, 2);
        Reject(image, input.View("image.exr"), {}, ImportError::UnsupportedData);
        struct BadWindow { uint32_t minX, minY, maxX, maxY; };
        const BadWindow windows[]{{0,0,UINT32_MAX,4}, {0,0,6,UINT32_MAX}, {0,0,INT32_MAX,4},
            {0,0,6,INT32_MAX}, {0x80000000,0,INT32_MAX,4}, {0,0,65535,65535}};
        for (const auto& window : windows)
        {
            input.Exr(4); const size_t offset = input.Attribute("dataWindow");
            input.Put(offset, window.minX); input.Put(offset + 4, window.minY);
            input.Put(offset + 8, window.maxX); input.Put(offset + 12, window.maxY);
            Reject(image, input.View("image.exr"));
        }
        input.Exr(4); input.data[input.Attribute("compression")] = 255; Reject(image, input.View("image.exr"));
        input.Exr(4); const size_t first = input.Word(input.HeaderEnd());
        memset(input.data + first + 8, 0xff, input.Word(first + 4));
        for (size_t retry = 0; retry < 16; ++retry) Reject(image, input.View("corrupt.exr"), {}, ImportError::InvalidData);
        input.Exr(4); Good(image.Decode(input.View("image.exr")), "retry after compressed decoder failure");
        ImportImageDecodeOptions options; options.maxTemporaryPixelBytes = 35 * 4 * 4 + 4 * sizeof(void*) - 1;
        Reject(image, input.View("image.exr"), options, ImportError::Capacity);
        ++options.maxTemporaryPixelBytes; Good(image.Decode(input.View("image.exr"), options), "exact EXR plane limit");
        input.Exr(4, false, TINYEXR_COMPRESSIONTYPE_NONE);
        const size_t line = input.Word(input.HeaderEnd()); input.Put(line + 4, 4);
        Reject(image, input.View("short-scanline.exr"), {}, ImportError::InvalidRange);
        for (uint32_t field = 0; field < 6; ++field)
        {
            input.TiledExr(4); const size_t tile = input.Word(input.HeaderEnd());
            if (field < 4) input.Put(tile + field * 4, UINT32_MAX);
            else if (field == 4) input.Put(tile + 16, 4);
            else input.Put(input.HeaderEnd() + 8, uint32_t(tile));
            Reject(image, input.View("bad-tile.exr"));
        }
        for (uint32_t field = 0; field < 3; ++field)
        {
            input.TiledExr(4); const auto tile = input.Attribute("tiles");
            if (field < 2) input.Put(tile + field * 4, 0); else input.data[tile + 8] = 1;
            Reject(image, input.View("bad-tile.exr"), {}, ImportError::UnsupportedData);
        }
        input.TiledExr(4);
        // compressed failure after several allocated tiles exercises cleanup counts.
        input.data[input.Attribute("compression")] = TINYEXR_COMPRESSIONTYPE_ZIP;
        const size_t tile = input.Word(input.HeaderEnd() + 8 * 8);
        input.Put(tile + 16, 4); memset(input.data + tile + 20, 0xff, 4);
        for (size_t retry = 0; retry < 16; ++retry) Reject(image, input.View("corrupt-tile.exr"), {}, ImportError::InvalidData);
        input.TiledExr(4); Good(image.Decode(input.View("image.exr")), "retry after tiled decoder failure");
        printf("EXR truncation points: %zu; scanline/tiled cleanup failures recovered\n", originalCount);
    }

    void Extensions()
    {
        const char* roots[]{"", "C:", "C:/", "//server", "//server/", "//server/share/", "\\\\?\\C:\\", "\\??\\C:\\", "folder.dds/"};
        const char* names[]{"", ".", "..", "...", ".dds", ".EXR", "image.dds", "image.exr", "image.",
            "image.png:stream", ".dds:stream", "image.dds:stream.png", "subfolder/.dds", "subfolder//image.exr"};
        size_t checked = 0;
        for (const auto root : roots)
            for (const auto name : names)
            {
                const std::string path = std::string(root) + name;
                const std::string expected = std::filesystem::path(path).extension().string();
                ArrayView<const char> extension;
                Good(ReadImportPathExtension({path.data(), path.size()}, extension), "path extension");
                Require(std::string(extension.data ? extension.data : "", extension.count) == expected, "native extension grammar");
                ++checked;
            }
        printf("native path extensions: %zu\n", checked);
    }
}

int main()
{
    VerifyGifImages();
    Encoded input;
    for (uint32_t channels = 1; channels <= 4; ++channels)
    {
        input.Png(channels); Compare(input.View("image.png")); Compare(input.View("image.PNG"), true);
        Compare(input.View(".DDS")); Compare(input.View(".EXR"));
        Compare(input.View("image.Dds")); Compare(input.View("folder.dds/image"));
        Compare(input.View("C:.DDS")); Compare(input.View("//host.dds"));
    }
    for (int kind = 1; kind <= 3; ++kind) { input.Png(3, kind); Compare(input.View("image.generic")); }
    input.Hdr(); Compare(input.View("image.hdr")); Compare(input.View("image.hdr"), true);
    const int compressions[]{TINYEXR_COMPRESSIONTYPE_NONE, TINYEXR_COMPRESSIONTYPE_RLE,
        TINYEXR_COMPRESSIONTYPE_ZIPS, TINYEXR_COMPRESSIONTYPE_ZIP, TINYEXR_COMPRESSIONTYPE_PIZ};
    const int channelCounts[]{1, 3, 4, 5};
    for (const auto compression : compressions)
        for (const auto channels : channelCounts)
            for (int half = 0; half < 2; ++half)
            {
                input.Exr(channels, half != 0, compression); Compare(input.View("image.exr"));
                Compare(input.View("image.unknown", "image/aces"), true);
            }
    for (const auto compression : compressions)
    {
        input.Exr(4, false, compression, true); Compare(input.View("integer.exr"));
    }
    DDSCases(); LegacyDDS(); EXRCases(); Extensions(); LifetimeAndFailures();
    printf("image decode passed: %zu captured native pixel/layout comparisons, %zu preserved-output rejections, %zu allocation retries\n",
        comparisons, rejections, retries);
    FinishImportImageReference();
    return 0;
}
