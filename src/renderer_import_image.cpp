#include "import/renderer_import_image_private.h"
#include "import/renderer_import_allocation.h"
#include "renderer_import_path.h"

#include <stb_image.h>
#include <tinyexr.h>
#include <limits.h>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace uvsr
{
    struct ImportDecodedImageState
    {
        ImportImagePlan plan;
        uint8_t* bytes = nullptr;
        ImportImageSubresource* layout = nullptr;
        size_t storageBytes = 0;
        bool stbAllocation = false;

        ~ImportDecodedImageState() noexcept
        {
            if (stbAllocation) stbi_image_free(bytes);
            else free(bytes);
            free(layout);
        }
    };

    namespace
    {
        ImportResult Failure(ImportError error = ImportError::InvalidData, int codec = 0) noexcept
        { return {error, ImportObject::Image, SIZE_MAX, uint32_t(codec)}; }
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
        template<class T> bool Range(ArrayView<const T> view) noexcept
        {
            size_t size = 0;
            return view.IsValid() && Product(view.count, sizeof(T), size) &&
                (!size || uintptr_t(view.data) <= UINTPTR_MAX - size);
        }
        bool Text(ArrayView<const char> text) noexcept
        { return Range(text) && (!text.count || !memchr(text.data, 0, text.count)); }
        bool Equal(ArrayView<const char> text, const char* literal) noexcept
        { const auto size = strlen(literal); return text.count == size && (!size || !memcmp(text.data, literal, size)); }
        uint32_t Word(const uint8_t* p) noexcept
        { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
        uint64_t LongWord(const uint8_t* p) noexcept
        { return uint64_t(Word(p)) | (uint64_t(Word(p + 4)) << 32); }
        int64_t SignedWord(const uint8_t* p) noexcept
        { const uint32_t value = Word(p); return value <= INT_MAX ? int64_t(value) : int64_t(value) - 0x100000000ll; }

        void Destroy(ImportDecodedImageState* state) noexcept
        { if (state) { state->~ImportDecodedImageState(); free(state); } }
        struct Candidate
        {
            ImportDecodedImageState* state = nullptr;
            ~Candidate() noexcept { Destroy(state); }
        };
        struct Scratch
        {
            uint8_t* bytes = nullptr;
            ~Scratch() noexcept { free(bytes); }
        };

        ImportResult Storage(const ImportImagePlan& plan, size_t limit, size_t& output) noexcept
        {
            size_t layout = 0, total = 0;
            if (!Product(plan.subresources, sizeof(ImportImageSubresource), layout) ||
                !Sum(sizeof(ImportDecodedImageState), layout, total) || !Sum(total, plan.dataBytes, total))
                return Failure(ImportError::Overflow);
            if (total > limit) return Failure(ImportError::Capacity);
            output = total; return {};
        }
        ImportResult Allocate(const ImportImagePlan& plan, size_t limit, Candidate& candidate) noexcept
        {
            size_t storage = 0;
            auto result = Storage(plan, limit, storage);
            if (!result) return result;
            auto* memory = ImportAllocate(sizeof(ImportDecodedImageState));
            if (!memory) return Failure(ImportError::OutOfMemory);
            // placement construction supplies the typed lifetime in checked raw storage.
            candidate.state = new(memory) ImportDecodedImageState{};
            auto& state = *candidate.state;
            state.plan = plan; state.storageBytes = storage;
            state.layout = static_cast<ImportImageSubresource*>(ImportAllocate(plan.subresources * sizeof(ImportImageSubresource)));
            if (!state.layout) return Failure(ImportError::OutOfMemory);
            for (size_t i = 0; i < plan.subresources; ++i) new(state.layout + i) ImportImageSubresource{};
            return WriteImportImageLayout(plan, {state.layout, plan.subresources});
        }
        ImportResult PixelPlan(int64_t width, int64_t height, uint32_t channels, bool hdr,
            uint32_t originalChannels, bool forceSRGB, ImportImagePlan& plan) noexcept
        {
            if (width <= 0 || height <= 0 || width > INT_MAX || height > INT_MAX ||
                (channels != 1 && channels != 2 && channels != 4)) return Failure();
            plan.info.width = uint32_t(width); plan.info.height = uint32_t(height);
            plan.info.allowGeneratedMips = true;
            plan.info.originalBitsPerPixel = originalChannels * (hdr ? 32 : 8);
            switch (channels)
            {
            case 1: plan.info.format = hdr ? ImportImageFormat::R32_FLOAT : ImportImageFormat::R8_UNORM; break;
            case 2: plan.info.format = hdr ? ImportImageFormat::RG32_FLOAT : ImportImageFormat::RG8_UNORM; break;
            default: plan.info.format = hdr ? ImportImageFormat::RGBA32_FLOAT :
                (forceSRGB ? ImportImageFormat::SRGBA8_UNORM : ImportImageFormat::RGBA8_UNORM); break;
            }
            size_t pixels = 0;
            if (!Product(size_t(width), size_t(height), pixels) || !Product(pixels, channels * (hdr ? 4 : 1), plan.dataBytes))
                return Failure(ImportError::Overflow);
            plan.subresources = 1; return {};
        }

        ImportResult DecodeSTB(ArrayView<const uint8_t> bytes, const ImportImageDecodeOptions& options, Candidate& candidate) noexcept
        {
            if (bytes.count > INT_MAX) return Failure(ImportError::Overflow);
            int width = 0, height = 0, original = 0;
            if (!stbi_info_from_memory(bytes.data, int(bytes.count), &width, &height, &original)) return Failure();
            if (original < 1 || original > 4) return Failure(ImportError::UnsupportedData);
            const uint32_t channels = original == 3 ? 4 : uint32_t(original);
            const bool hdr = stbi_is_hdr_from_memory(bytes.data, int(bytes.count)) != 0;
            ImportImagePlan plan;
            auto result = PixelPlan(width, height, channels, hdr, uint32_t(original), options.forceSRGB, plan);
            if (!result) return result;
            result = Allocate(plan, options.maxStorageBytes, candidate);
            if (!result) return result;
            auto& state = *candidate.state;
            state.stbAllocation = true;
            int loadedWidth = 0, loadedHeight = 0, loadedChannels = 0;
            state.bytes = hdr ? reinterpret_cast<uint8_t*>(stbi_loadf_from_memory(bytes.data, int(bytes.count),
                &loadedWidth, &loadedHeight, &loadedChannels, int(channels))) :
                stbi_load_from_memory(bytes.data, int(bytes.count), &loadedWidth, &loadedHeight, &loadedChannels, int(channels));
            if (!state.bytes) return Failure();
            if (loadedWidth != width || loadedHeight != height || loadedChannels != original) return Failure();
            return {};
        }

        struct EXRStorage
        {
            EXRHeader header;
            EXRImage image;
            const char* error = nullptr;
            EXRStorage() noexcept { InitEXRHeader(&header); InitEXRImage(&image); }
            ~EXRStorage() noexcept { FreeEXRImage(&image); FreeEXRHeader(&header); FreeEXRErrorMessage(error); }
            void ClearError() noexcept { FreeEXRErrorMessage(error); error = nullptr; }
        };

        // this pinned codec assumes valid chunk coordinates and cleanup counts.
        // validate complete coverage before allocating its pixel planes. offsets
        // of zero retain the codec's sequential scanline reconstruction behavior.
        ImportResult EXRChunks(ArrayView<const uint8_t> bytes, const EXRHeader& header,
            const ImportImagePlan& plan, size_t& tileCount) noexcept
        {
            const size_t width = plan.info.width, height = plan.info.height;
            const size_t rows = header.compression_type == TINYEXR_COMPRESSIONTYPE_PIZ ? 32 :
                (header.compression_type == TINYEXR_COMPRESSIONTYPE_ZIP ? 16 : 1);
            const size_t tilesX = header.tiled ? (width - 1) / size_t(header.tile_size_x) + 1 : 1;
            const size_t tilesY = header.tiled ? (height - 1) / size_t(header.tile_size_y) + 1 : (height - 1) / rows + 1;
            size_t sourcePixelBytes = 0;
            for (int c = 0; c < header.num_channels; ++c)
                if (!Sum(sourcePixelBytes, header.pixel_types[c] == TINYEXR_PIXELTYPE_HALF ? 2 : 4, sourcePixelBytes) ||
                    sourcePixelBytes > INT_MAX) return Failure(ImportError::Overflow);
            size_t count = 0, table = 0, start = 0, end = 0;
            if (!Product(tilesX, tilesY, count) || count > INT_MAX || !Product(count, 8, table) ||
                !Sum(header.header_len, 8, start) || !Sum(start, table, end)) return Failure(ImportError::Overflow);
            if (!header.header_len || end >= bytes.count || header.chunk_count < 0 ||
                (header.chunk_count && size_t(header.chunk_count) != count)) return Failure(ImportError::InvalidRange);
            bool reconstruct = false;
            for (size_t i = 0; i < count; ++i)
            {
                const auto offset = LongWord(bytes.data + start + i * 8);
                if (offset >= bytes.count) return Failure(ImportError::InvalidRange);
                reconstruct |= offset == 0;
            }
            if (reconstruct && header.tiled) return Failure(ImportError::UnsupportedData);
            Scratch visited;
            const size_t maskBytes = (count - 1) / 8 + 1;
            visited.bytes = static_cast<uint8_t*>(ImportAllocate(maskBytes));
            if (!visited.bytes) return Failure(ImportError::OutOfMemory);
            memset(visited.bytes, 0, maskBytes);
            size_t sequential = end;
            for (size_t i = 0; i < count; ++i)
            {
                const size_t offset = reconstruct ? sequential : size_t(LongWord(bytes.data + start + i * 8));
                const size_t prefix = header.tiled ? 20 : 8;
                if (offset < end || offset > bytes.count || prefix > bytes.count - offset) return Failure(ImportError::InvalidRange);
                const auto* p = bytes.data + offset;
                const size_t length = Word(p + prefix - 4);
                if (!length || length > INT_MAX || length > bytes.count - offset - prefix) return Failure(ImportError::InvalidRange);
                size_t slot = 0, chunkWidth = width, chunkHeight = 0;
                if (header.tiled)
                {
                    const uint32_t x = Word(p), y = Word(p + 4);
                    if (x >= tilesX || y >= tilesY || Word(p + 8) || Word(p + 12) || length < 4) return Failure();
                    slot = size_t(y) * tilesX + x;
                    chunkWidth = width - size_t(x) * size_t(header.tile_size_x);
                    if (chunkWidth > size_t(header.tile_size_x)) chunkWidth = size_t(header.tile_size_x);
                    chunkHeight = height - size_t(y) * size_t(header.tile_size_y);
                    if (chunkHeight > size_t(header.tile_size_y)) chunkHeight = size_t(header.tile_size_y);
                }
                else
                {
                    const int64_t line = SignedWord(p), relative = line - header.data_window[1];
                    if (relative < 0 || uint64_t(relative) >= height || size_t(relative) % rows ||
                        line < -(2 << 20) || line > (2 << 20) || line + int64_t(rows) > INT_MAX) return Failure();
                    slot = size_t(relative) / rows;
                    chunkHeight = height - size_t(relative);
                    if (chunkHeight > rows) chunkHeight = rows;
                }
                size_t decodedBytes = 0;
                if (!Product(chunkWidth, chunkHeight, decodedBytes) || !Product(decodedBytes, sourcePixelBytes, decodedBytes) ||
                    decodedBytes > INT_MAX) return Failure(ImportError::Overflow);
                // the retained uncompressed decoder ignores data_len entirely.
                if (header.compression_type == TINYEXR_COMPRESSIONTYPE_NONE && length < decodedBytes)
                    return Failure(ImportError::InvalidRange);
                const uint8_t bit = uint8_t(1u << (slot % 8));
                if (visited.bytes[slot / 8] & bit) return Failure();
                visited.bytes[slot / 8] |= bit;
                sequential = offset + prefix + length;
            }
            tileCount = header.tiled ? count : 0; return {};
        }

        float EXRValue(const uint8_t* source, size_t pixel) noexcept
        {
            // retained UINT channels are interpreted as float bits by the old
            // convenience API. memcpy preserves those bits without aliasing UB.
            float value = 0; memcpy(&value, source + pixel * sizeof(float), sizeof(value)); return value;
        }
        void EXRPixel(uint8_t* output, unsigned char** planes, const int* channels, size_t source) noexcept
        {
            float values[4];
            for (uint32_t c = 0; c < 4; ++c)
                values[c] = channels[c] < 0 ? 1.f : EXRValue(planes[channels[c]], source);
            memcpy(output, values, sizeof(values));
        }
        ImportResult DecodeEXR(ArrayView<const uint8_t> bytes, const ImportImageDecodeOptions& options, Candidate& candidate) noexcept
        {
            EXRVersion version{};
            int code = ParseEXRVersionFromMemory(&version, bytes.data, bytes.count);
            if (code != TINYEXR_SUCCESS) return Failure(ImportError::InvalidContainer, code);
            if (version.multipart || version.non_image) return Failure(ImportError::UnsupportedData);
            EXRStorage storage;
            auto& header = storage.header;
            code = ParseEXRHeaderFromMemory(&header, &version, bytes.data, bytes.count, &storage.error);
            if (code != TINYEXR_SUCCESS) return Failure(ImportError::InvalidContainer, code);
            const int64_t width = int64_t(header.data_window[2]) - header.data_window[0] + 1;
            const int64_t height = int64_t(header.data_window[3]) - header.data_window[1] + 1;
            ImportImagePlan plan;
            auto result = PixelPlan(width, height, 4, true, 4, false, plan);
            if (!result) return result;
            if (width * height > INT_MAX || width > 1024 * 8192 || height > 1024 * 8192 ||
                header.data_window[3] == INT_MAX) return Failure(ImportError::Overflow);
            if (header.num_channels < 1 || !header.channels || !header.pixel_types || !header.requested_pixel_types)
                return Failure();
            if (header.compression_type < TINYEXR_COMPRESSIONTYPE_NONE || header.compression_type > TINYEXR_COMPRESSIONTYPE_PIZ)
                return Failure(ImportError::UnsupportedData);
            int channels[4]{-1, -1, -1, -1};
            for (int c = 0; c < header.num_channels; ++c)
            {
                const auto& channel = header.channels[c];
                if (!memchr(channel.name, 0, sizeof(channel.name))) return Failure();
                // this codec's pixel decoder never consumes channel sampling.
                if (channel.x_sampling != 1 || channel.y_sampling != 1) return Failure(ImportError::UnsupportedData);
                if (header.pixel_types[c] != TINYEXR_PIXELTYPE_HALF && header.pixel_types[c] != TINYEXR_PIXELTYPE_FLOAT &&
                    header.pixel_types[c] != TINYEXR_PIXELTYPE_UINT) return Failure(ImportError::UnsupportedData);
                if (header.pixel_types[c] == TINYEXR_PIXELTYPE_HALF) header.requested_pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
                if (!strcmp(channel.name, "R")) channels[0] = c;
                else if (!strcmp(channel.name, "G")) channels[1] = c;
                else if (!strcmp(channel.name, "B")) channels[2] = c;
                else if (!strcmp(channel.name, "A")) channels[3] = c;
            }
            if (header.num_channels == 1) for (auto& channel : channels) channel = 0;
            else if (channels[0] < 0 || channels[1] < 0 || channels[2] < 0) return Failure(ImportError::UnsupportedData);
            if (header.tiled && (header.tile_size_x <= 0 || header.tile_size_y <= 0 ||
                header.tile_level_mode != TINYEXR_TILE_ONE_LEVEL)) return Failure(ImportError::UnsupportedData);
            size_t temporary = 0, pixels = size_t(width) * size_t(height), tileCount = 0, planeArrays = 1;
            if (header.tiled)
            {
                size_t tiles = 0, tilePixels = 0;
                if (!Product((size_t(width) - 1) / size_t(header.tile_size_x) + 1,
                        (size_t(height) - 1) / size_t(header.tile_size_y) + 1, tiles) ||
                    !Product(size_t(header.tile_size_x), size_t(header.tile_size_y), tilePixels) ||
                    tilePixels > INT_MAX || !Product(tiles, tilePixels, pixels)) return Failure(ImportError::Overflow);
                if (!Product(tiles, sizeof(EXRTile), temporary)) return Failure(ImportError::Overflow);
                planeArrays = tiles;
            }
            size_t planes = 0, planePointers = 0;
            if (!Product(pixels, 4, planes) || !Product(planes, size_t(header.num_channels), planes) ||
                !Product(size_t(header.num_channels), sizeof(uint8_t*), planePointers) ||
                !Product(planePointers, planeArrays, planePointers) ||
                !Sum(temporary, planes, temporary) || !Sum(temporary, planePointers, temporary)) return Failure(ImportError::Overflow);
            if (temporary > options.maxTemporaryPixelBytes) return Failure(ImportError::Capacity);
            size_t owned = 0;
            result = Storage(plan, options.maxStorageBytes, owned);
            if (!result) return result;
            result = EXRChunks(bytes, header, plan, tileCount);
            if (!result) return result;
            result = Allocate(plan, options.maxStorageBytes, candidate);
            if (!result) return result;
            auto& state = *candidate.state;
            state.bytes = static_cast<uint8_t*>(ImportAllocate(plan.dataBytes));
            if (!state.bytes) return Failure(ImportError::OutOfMemory);
            auto& image = storage.image;
            // failed DecodeChunk frees using these counts before assigning its
            // successful output fields. its zeroed tile table permits partial cleanup.
            image.num_channels = header.num_channels; image.num_tiles = int(tileCount);
            storage.ClearError();
            code = LoadEXRImageFromMemory(&image, &header, bytes.data, bytes.count, &storage.error);
            if (code != TINYEXR_SUCCESS)
            {
                // all allocating failures already call FreeEXRImage, which leaves
                // dangling fields in this pin. early failures allocated no pixels.
                InitEXRImage(&image);
                return Failure(ImportError::InvalidData, code);
            }
            if (image.width != width || image.height != height || image.num_channels != header.num_channels ||
                image.num_tiles != int(tileCount)) return Failure();
            auto* output = state.bytes;
            if (!header.tiled)
            {
                if (!image.images) return Failure();
                for (int c = 0; c < image.num_channels; ++c) if (!image.images[c]) return Failure();
                for (size_t i = 0; i < pixels; ++i) EXRPixel(output + i * 4 * sizeof(float), image.images, channels, i);
            }
            else
            {
                if (!image.tiles) return Failure();
                for (int t = 0; t < image.num_tiles; ++t)
                {
                    const auto& tile = image.tiles[t];
                    if (!tile.images || tile.offset_x < 0 || tile.offset_y < 0 || tile.level_x || tile.level_y) return Failure();
                    for (int c = 0; c < image.num_channels; ++c) if (!tile.images[c]) return Failure();
                    const size_t x = size_t(tile.offset_x) * size_t(header.tile_size_x);
                    const size_t y = size_t(tile.offset_y) * size_t(header.tile_size_y);
                    if (x >= size_t(width) || y >= size_t(height) || tile.width <= 0 || tile.height <= 0 ||
                        tile.width > header.tile_size_x || tile.height > header.tile_size_y ||
                        size_t(tile.width) > size_t(width) - x || size_t(tile.height) > size_t(height) - y) return Failure();
                    const size_t expectedWidth = size_t(width) - x < size_t(header.tile_size_x) ? size_t(width) - x : size_t(header.tile_size_x);
                    const size_t expectedHeight = size_t(height) - y < size_t(header.tile_size_y) ? size_t(height) - y : size_t(header.tile_size_y);
                    if (size_t(tile.width) != expectedWidth || size_t(tile.height) != expectedHeight) return Failure();
                    for (size_t j = 0; j < size_t(tile.height); ++j)
                        for (size_t i = 0; i < size_t(tile.width); ++i)
                            EXRPixel(output + ((y + j) * size_t(width) + x + i) * 4 * sizeof(float),
                                tile.images, channels, j * size_t(header.tile_size_x) + i);
                }
            }
            return {};
        }
    }

    ImportDecodedImage::~ImportDecodedImage() noexcept { Reset(); }
    ImportDecodedImage::ImportDecodedImage(ImportDecodedImage&& other) noexcept : m_State(other.m_State) { other.m_State = nullptr; }
    ImportDecodedImage& ImportDecodedImage::operator=(ImportDecodedImage&& other) noexcept
    {
        if (this != &other) { Reset(); m_State = other.m_State; other.m_State = nullptr; }
        return *this;
    }
    ImportResult ImportDecodedImage::Decode(const ImportImageView& image, const ImportImageDecodeOptions& options) noexcept
    {
        if (!Text(image.path) || !Text(image.mimeType) || !Range(image.bytes) || !image.bytes.count) return Failure(ImportError::InvalidInput);
        if (image.bytes.count > options.maxEncodedBytes) return Failure(ImportError::Capacity);
        ArrayView<const char> extension;
        const auto pathResult = ReadImportPathExtension(image.path, extension);
        if (!pathResult) return pathResult;
        Candidate candidate;
        ImportResult result;
        if (Equal(extension, ".dds") || Equal(extension, ".DDS") || Equal(image.mimeType, "image/vnd-ms.dds"))
        {
            ImportImagePlan plan;
            result = PlanImportDDS(image.bytes, options.forceSRGB, plan);
            if (!result) return result;
            result = Allocate(plan, options.maxStorageBytes, candidate);
            if (!result) return result;
            candidate.state->bytes = static_cast<uint8_t*>(ImportAllocate(plan.dataBytes));
            if (!candidate.state->bytes) return Failure(ImportError::OutOfMemory);
            memcpy(candidate.state->bytes, image.bytes.data, plan.dataBytes);
        }
        else if (Equal(extension, ".exr") || Equal(extension, ".EXR") || Equal(image.mimeType, "image/aces"))
            result = DecodeEXR(image.bytes, options, candidate);
        else result = DecodeSTB(image.bytes, options, candidate);
        if (!result) return result;
        Reset(); m_State = candidate.state; candidate.state = nullptr; return {};
    }
    ImportDecodedImageInfo ImportDecodedImage::Info() const noexcept { return m_State ? m_State->plan.info : ImportDecodedImageInfo{}; }
    ArrayView<const uint8_t> ImportDecodedImage::Bytes() const noexcept { return m_State ? ArrayView<const uint8_t>{m_State->bytes, m_State->plan.dataBytes} : ArrayView<const uint8_t>{}; }
    ArrayView<const ImportImageSubresource> ImportDecodedImage::Subresources() const noexcept
    { return m_State ? ArrayView<const ImportImageSubresource>{m_State->layout, m_State->plan.subresources} : ArrayView<const ImportImageSubresource>{}; }
    size_t ImportDecodedImage::StorageBytes() const noexcept { return m_State ? m_State->storageBytes : 0; }
    void ImportDecodedImage::Reset() noexcept { Destroy(m_State); m_State = nullptr; }
}
