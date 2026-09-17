#include "image_based_lighting_radiance.h"
#include <DirectXPackedVector.h>
#include <stb_image.h>
#include <new>
#include <stdlib.h>

namespace
{
    const DirectX::XMFLOAT3 LuminanceWeights(
        0.2126f, 0.7152f, 0.0722f);

    DirectX::XMFLOAT3 SanitizeRadiance(
        DirectX::XMFLOAT3 value,
        bool neutralize)
    {
        value = uvsr::RendererClampFloat3(
            value, 0.f, uvsr::RendererEnvironmentHalfMaximum);
        if (neutralize)
        {
            const float luminance = DirectX::XMVectorGetX(
                DirectX::XMVector3Dot(
                    DirectX::XMLoadFloat3(&value),
                    DirectX::XMLoadFloat3(&LuminanceWeights)));
            value = { luminance, luminance, luminance };
        }
        return value;
    }

    uvsr::ImageBasedLightingHalf4 ToHalf4(DirectX::XMFLOAT3 value)
    {
        using DirectX::PackedVector::XMConvertFloatToHalf;
        return {
            XMConvertFloatToHalf(value.x),
            XMConvertFloatToHalf(value.y),
            XMConvertFloatToHalf(value.z),
            XMConvertFloatToHalf(0.f)
        };
    }

    DirectX::XMFLOAT3 SampleLatLongBilinear(
        const float* pixels,
        uint32_t width,
        uint32_t height,
        DirectX::XMFLOAT3 direction)
    {
        direction = uvsr::RendererNormalizeEnvironmentDirection(
            direction, { 0.f, 1.f, 0.f });
        float u = std::atan2(direction.z, direction.x) /
            (2.f * DirectX::XM_PI) + 0.5f;
        u -= std::floor(u);
        const float v = std::acos(std::clamp(
            direction.y, -1.f, 1.f)) / DirectX::XM_PI;
        const float sourceX = u * float(width) - 0.5f;
        const float sourceY = v * float(height) - 0.5f;
        const int32_t x0 = int32_t(std::floor(sourceX));
        const int32_t y0 = int32_t(std::floor(sourceY));
        const float tx = sourceX - std::floor(sourceX);
        const float ty = sourceY - std::floor(sourceY);
        const auto load = [=](int32_t x, int32_t y)
        {
            x %= int32_t(width);
            if (x < 0)
                x += int32_t(width);
            y = std::clamp(y, 0, int32_t(height) - 1);
            const size_t offset = (size_t(y) * width + uint32_t(x)) * 3u;
            return DirectX::XMVectorSet(
                pixels[offset], pixels[offset + 1u], pixels[offset + 2u], 0.f);
        };
        return uvsr::RendererStoreFloat3(DirectX::XMVectorLerp(
            DirectX::XMVectorLerp(load(x0, y0), load(x0 + 1, y0), tx),
            DirectX::XMVectorLerp(load(x0, y0 + 1), load(x0 + 1, y0 + 1), tx),
            ty));
    }


#if defined(UVSR_IBL_RADIANCE_TEST_HOOKS)
    thread_local size_t AllocationsBeforeFailure = SIZE_MAX;
    thread_local uvsr::ImageBasedLightingPixelObserver PixelObserver = nullptr;
#endif
    uvsr::ImageBasedLightingHalf4* AllocateFaces(size_t count) noexcept
    {
#if defined(UVSR_IBL_RADIANCE_TEST_HOOKS)
        if (AllocationsBeforeFailure != SIZE_MAX)
        {
            if (!AllocationsBeforeFailure) return nullptr;
            --AllocationsBeforeFailure;
        }
#endif
        return new (std::nothrow) uvsr::ImageBasedLightingHalf4[count];
    }
    struct DecodedRadiancePixels
    {
        explicit DecodedRadiancePixels(float* pixels) noexcept : data(pixels) {}
        DecodedRadiancePixels(const DecodedRadiancePixels&) = delete;
        DecodedRadiancePixels& operator=(const DecodedRadiancePixels&) = delete;
        float* data;
        ~DecodedRadiancePixels() noexcept { stbi_image_free(data); }
    };
}

namespace uvsr
{
    struct ImageBasedLightingRadianceWriter
    {
        template <typename Sample>
        static bool MakeFaces(uint32_t size, const Sample& sample,
            ImageBasedLightingFaces& output) noexcept
        {
            if (!size || size_t(size) > size_t(PTRDIFF_MAX) / sizeof(ImageBasedLightingHalf4) / 6u / size)
                return false;
            const size_t count = size_t(6u) * size * size;
            auto* faces = AllocateFaces(count);
            if (!faces) return false;
            for (uint32_t face = 0u; face < 6u; ++face)
                for (uint32_t y = 0u; y < size; ++y)
                    for (uint32_t x = 0u; x < size; ++x)
                        faces[(size_t(face) * size + y) * size + x] = ToHalf4(
                            SanitizeRadiance(sample(RendererEnvironmentCubeDirection(
                                face, x, y, size)), false));
            output.Clear();
            output.m_Data = faces;
            output.m_Size = count;
            return true;
        }
        static bool Prepare(const float* pixels, uint32_t width, uint32_t height,
            ImageBasedLightingSource source, bool neutralize, const RendererDiffuseEnvironmentSh& sh,
            PreparedImageBasedLightingRadiance& output) noexcept
        {
            PreparedImageBasedLightingRadiance candidate;
            if (!MakeFaces(ImageBasedLightingRadianceDimension,
                [&](DirectX::XMFLOAT3 direction)
                {
                    return SampleLatLongBilinear(pixels, width, height, direction);
                }, candidate.m_Faces)) return false;
            candidate.m_Source = source;
            candidate.m_Neutralize = neutralize;
            candidate.m_DiffuseSh = sh;
            output = static_cast<PreparedImageBasedLightingRadiance&&>(candidate);
            return true;
        }
    };

    ImageBasedLightingFaces::~ImageBasedLightingFaces() noexcept { Clear(); }
    ImageBasedLightingFaces::ImageBasedLightingFaces(ImageBasedLightingFaces&& other) noexcept
        : m_Data(other.m_Data), m_Size(other.m_Size)
    {
        other.m_Data = nullptr; other.m_Size = 0;
    }
    ImageBasedLightingFaces& ImageBasedLightingFaces::operator=(ImageBasedLightingFaces&& other) noexcept
    {
        if (this != &other)
        {
            Clear(); m_Data = other.m_Data; m_Size = other.m_Size;
            other.m_Data = nullptr; other.m_Size = 0;
        }
        return *this;
    }
    void ImageBasedLightingFaces::Clear() noexcept
    {
        delete[] m_Data; m_Data = nullptr; m_Size = 0;
    }

    ImageBasedLightingRadianceResult PrepareImageBasedLightingRadiance(
        const char* fileName, ImageBasedLightingSource source, bool neutralize,
        PreparedImageBasedLightingRadiance& output) noexcept
    {
        ImageBasedLightingRadianceResult result;
        int& width = result.width;
        int& height = result.height;
        int& sourceChannels = result.sourceChannels;
        DecodedRadiancePixels decoded{fileName ? stbi_loadf(fileName, &width, &height, &sourceChannels, 3) : nullptr};
        if (!decoded.data)
        {
            result.error = ImageBasedLightingRadianceError::Decode;
            result.decoderReason = fileName ? stbi_failure_reason() : nullptr;
            return result;
        }
        if (width < 4 || height < 2 ||
            std::abs(int64_t(width) - int64_t(height) * 2) > 2)
        {
            result.error = ImageBasedLightingRadianceError::Dimensions;
            return result;
        }
#if defined(UVSR_IBL_RADIANCE_TEST_HOOKS)
        if (PixelObserver) PixelObserver(decoded.data, size_t(width) * size_t(height) * 3u, false);
#endif
        float* pixels = decoded.data;
        for (size_t pixel = 0u; pixel < size_t(width) * size_t(height); ++pixel)
        {
            const DirectX::XMFLOAT3 radiance = SanitizeRadiance(
                { pixels[pixel * 3u], pixels[pixel * 3u + 1u], pixels[pixel * 3u + 2u] },
                neutralize);
            pixels[pixel * 3u] = radiance.x;
            pixels[pixel * 3u + 1u] = radiance.y;
            pixels[pixel * 3u + 2u] = radiance.z;
        }
#if defined(UVSR_IBL_RADIANCE_TEST_HOOKS)
        if (PixelObserver) PixelObserver(pixels, size_t(width) * size_t(height) * 3u, true);
#endif
        const auto projection = ProjectRendererDiffuseEnvironmentLatLongRgb(
            pixels, uint32_t(width), uint32_t(height));
        if (!projection)
        {
            result.error = ImageBasedLightingRadianceError::Projection;
            return result;
        }
        result.averageLuminance = projection->averageLuminance;
        if (!ImageBasedLightingRadianceWriter::Prepare(pixels, uint32_t(width), uint32_t(height),
            source, neutralize, projection->sh, output))
            result.error = ImageBasedLightingRadianceError::Allocation;
        return result;
    }

    bool PrepareImageBasedLightingDiffuseFaces(
        const RendererDiffuseEnvironmentSh& sh, ImageBasedLightingFaces& output) noexcept
    {
        return ImageBasedLightingRadianceWriter::MakeFaces(ImageBasedLightingDiffuseDimension,
            [&](DirectX::XMFLOAT3 direction)
            {
                return EvaluateRendererEnvironmentSh(sh, direction);
            }, output);
    }
#if defined(UVSR_IBL_RADIANCE_TEST_HOOKS)
    void FailImageBasedLightingRadianceAllocationAfter(size_t count) noexcept { AllocationsBeforeFailure = count; }
    void ClearImageBasedLightingRadianceAllocationFailure() noexcept { AllocationsBeforeFailure = SIZE_MAX; }
    void SetImageBasedLightingPixelObserver(ImageBasedLightingPixelObserver observer) noexcept { PixelObserver = observer; }
#endif
}
