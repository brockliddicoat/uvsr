#pragma once

#include "renderer_import_scene.h"

namespace uvsr
{
    enum class ImportImageFormat : uint8_t
    {
        Unknown,
        R8_UINT, R8_SINT, R8_UNORM, R8_SNORM,
        RG8_UINT, RG8_SINT, RG8_UNORM, RG8_SNORM,
        R16_UINT, R16_SINT, R16_UNORM, R16_SNORM, R16_FLOAT,
        BGRA4_UNORM, B5G6R5_UNORM, B5G5R5A1_UNORM,
        RGBA8_UINT, RGBA8_SINT, RGBA8_UNORM, RGBA8_SNORM,
        BGRA8_UNORM, BGRX8_UNORM, SRGBA8_UNORM, SBGRA8_UNORM, SBGRX8_UNORM,
        R10G10B10A2_UNORM, R11G11B10_FLOAT,
        RG16_UINT, RG16_SINT, RG16_UNORM, RG16_SNORM, RG16_FLOAT,
        R32_UINT, R32_SINT, R32_FLOAT,
        RGBA16_UINT, RGBA16_SINT, RGBA16_FLOAT, RGBA16_UNORM, RGBA16_SNORM,
        RG32_UINT, RG32_SINT, RG32_FLOAT,
        RGB32_UINT, RGB32_SINT, RGB32_FLOAT,
        RGBA32_UINT, RGBA32_SINT, RGBA32_FLOAT,
        D24S8, X24G8_UINT, D32S8, X32G8_UINT,
        BC1_UNORM, BC1_UNORM_SRGB, BC2_UNORM, BC2_UNORM_SRGB,
        BC3_UNORM, BC3_UNORM_SRGB, BC4_UNORM, BC4_SNORM,
        BC5_UNORM, BC5_SNORM, BC6H_UFLOAT, BC6H_SFLOAT, BC7_UNORM, BC7_UNORM_SRGB,
        Count
    };

    enum class ImportImageDimension : uint8_t { Texture1D, Texture2D, Texture3D, Cube };

    struct ImportDecodedImageInfo
    {
        ImportImageFormat format = ImportImageFormat::Unknown;
        ImportImageDimension dimension = ImportImageDimension::Texture2D;
        RendererSceneTextureAlpha alpha = RendererSceneTextureAlpha::Unknown;
        uint32_t width = 0, height = 0, depth = 1;
        uint32_t arraySize = 1; // face count for cubes; always one for volumes.
        uint32_t mipLevels = 1;
        uint32_t originalBitsPerPixel = 0;
        bool allowGeneratedMips = false;
    };

    struct ImportImageSubresource
    {
        size_t offset = 0;
        size_t rowPitch = 0;
        size_t depthPitch = 0;
        size_t size = 0; // every depth slice in this mip.
    };

    struct ImportImageDecodeOptions
    {
        bool forceSRGB = false;
        size_t maxStorageBytes = SIZE_MAX;
        size_t maxEncodedBytes = SIZE_MAX;
        // EXR channel planes and tile storage, excluding private codec workspace.
        size_t maxTemporaryPixelBytes = SIZE_MAX;
    };

    struct ImportDecodedImageState;

    // decoded storage has no parser/file borrow. calls are exclusive; views remain
    // valid until reset, successful replacement, move assignment or destruction.
    class ImportDecodedImage final
    {
    public:
        ImportDecodedImage() noexcept = default;
        ~ImportDecodedImage() noexcept;
        ImportDecodedImage(const ImportDecodedImage&) = delete;
        ImportDecodedImage& operator=(const ImportDecodedImage&) = delete;
        ImportDecodedImage(ImportDecodedImage&& other) noexcept;
        ImportDecodedImage& operator=(ImportDecodedImage&& other) noexcept;
        // path/MIME select the retained codec. bytes must contain the encoded
        // image, including external files. failure preserves this owner.
        [[nodiscard]] ImportResult Decode(const ImportImageView& image,
            const ImportImageDecodeOptions& options = {}) noexcept;
        [[nodiscard]] ImportDecodedImageInfo Info() const noexcept;
        [[nodiscard]] ArrayView<const uint8_t> Bytes() const noexcept;
        // array-major, then mip-major. all ranges are contained in Bytes().
        [[nodiscard]] ArrayView<const ImportImageSubresource> Subresources() const noexcept;
        [[nodiscard]] size_t StorageBytes() const noexcept;
        void Reset() noexcept;

    private:
        ImportDecodedImageState* m_State = nullptr;
    };

    [[nodiscard]] uint32_t ImportImageBitsPerPixel(ImportImageFormat format) noexcept;
    [[nodiscard]] uint32_t ImportImageBlockBytes(ImportImageFormat format) noexcept;
}
