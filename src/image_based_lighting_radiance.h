#pragma once

#include "image_based_lighting_environment.h"
#include "image_based_lighting_sources.h"
#include "renderer_environment_math.h"
#include <stddef.h>

namespace uvsr
{
    inline constexpr uint32_t ImageBasedLightingRadianceDimension = 512u;
    inline constexpr uint32_t ImageBasedLightingDiffuseDimension = 16u;

    // complete face-major tables. synchronous upload calls may borrow the bytes;
    // moving, clearing or destroying this owner ends that borrow.
    class ImageBasedLightingFaces
    {
    public:
        ImageBasedLightingFaces() noexcept = default;
        ~ImageBasedLightingFaces() noexcept;
        ImageBasedLightingFaces(const ImageBasedLightingFaces&) = delete;
        ImageBasedLightingFaces& operator=(const ImageBasedLightingFaces&) = delete;
        ImageBasedLightingFaces(ImageBasedLightingFaces&& other) noexcept;
        ImageBasedLightingFaces& operator=(ImageBasedLightingFaces&& other) noexcept;
        [[nodiscard]] const ImageBasedLightingHalf4* Data() const noexcept { return m_Data; }
        [[nodiscard]] size_t Size() const noexcept { return m_Size; }
        void Clear() noexcept;
    private:
        ImageBasedLightingHalf4* m_Data = nullptr;
        size_t m_Size = 0;
        friend struct ImageBasedLightingRadianceWriter;
    };

    class PreparedImageBasedLightingRadiance
    {
    public:
        PreparedImageBasedLightingRadiance() noexcept = default;
        PreparedImageBasedLightingRadiance(const PreparedImageBasedLightingRadiance&) = delete;
        PreparedImageBasedLightingRadiance& operator=(const PreparedImageBasedLightingRadiance&) = delete;
        PreparedImageBasedLightingRadiance(PreparedImageBasedLightingRadiance&&) noexcept = default;
        PreparedImageBasedLightingRadiance& operator=(PreparedImageBasedLightingRadiance&&) noexcept = default;
        [[nodiscard]] explicit operator bool() const noexcept { return m_Faces.Data() != nullptr; }
        // metadata is valid only while the owner is nonempty.
        [[nodiscard]] ImageBasedLightingSource Source() const noexcept { return m_Source; }
        [[nodiscard]] bool Neutralize() const noexcept { return m_Neutralize; }
        [[nodiscard]] const RendererDiffuseEnvironmentSh& DiffuseSh() const noexcept { return m_DiffuseSh; }
        [[nodiscard]] const ImageBasedLightingFaces& Faces() const noexcept { return m_Faces; }
        void Clear() noexcept { m_Faces.Clear(); }
    private:
        ImageBasedLightingSource m_Source = ImageBasedLightingSource::Count;
        bool m_Neutralize = false;
        ImageBasedLightingFaces m_Faces;
        RendererDiffuseEnvironmentSh m_DiffuseSh;
        friend struct ImageBasedLightingRadianceWriter;
    };

    enum class ImageBasedLightingRadianceError : uint8_t
    {
        None, Decode, Dimensions, Projection, Allocation
    };
    struct ImageBasedLightingRadianceResult
    {
        ImageBasedLightingRadianceError error = ImageBasedLightingRadianceError::None;
        int width = 0, height = 0, sourceChannels = 0;
        float averageLuminance = 0.f;
        // stb returns static diagnostic text. consume on this thread before
        // another decode; successful prepared state retains no decoder storage.
        const char* decoderReason = nullptr;
        [[nodiscard]] bool Unavailable() const noexcept
        {
            return error == ImageBasedLightingRadianceError::Decode ||
                error == ImageBasedLightingRadianceError::Dimensions ||
                error == ImageBasedLightingRadianceError::Projection;
        }
    };

    // every failure preserves the prior output. stb null, including its own
    // allocation failure, retains the existing unavailable-image classification.
    [[nodiscard]] ImageBasedLightingRadianceResult PrepareImageBasedLightingRadiance(
        const char* fileName, ImageBasedLightingSource source, bool neutralize,
        PreparedImageBasedLightingRadiance& output) noexcept;
    [[nodiscard]] bool PrepareImageBasedLightingDiffuseFaces(
        const RendererDiffuseEnvironmentSh& sh, ImageBasedLightingFaces& output) noexcept;

#if defined(UVSR_IBL_RADIANCE_TEST_HOOKS)
    void FailImageBasedLightingRadianceAllocationAfter(size_t count) noexcept;
    void ClearImageBasedLightingRadianceAllocationFailure() noexcept;
    using ImageBasedLightingPixelObserver = void (*)(const float*, size_t, bool sanitized) noexcept;
    void SetImageBasedLightingPixelObserver(ImageBasedLightingPixelObserver observer) noexcept;
#endif
}
