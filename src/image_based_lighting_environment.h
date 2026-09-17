#pragma once

#include "renderer_gpu_contract.h"
#include <cmath>
#include <cstdint>

namespace uvsr
{
    struct ImageBasedLightingHalf4
    {
        std::uint16_t x = 0u;
        std::uint16_t y = 0u;
        std::uint16_t z = 0u;
        std::uint16_t w = 0u;
    };

    static_assert(sizeof(ImageBasedLightingHalf4) == 8u);

    [[nodiscard]] inline bool IsImageBasedLightingProbeActive(
        bool hasDiffuseMap,
        bool hasSpecularMap,
        bool hasEnvironmentBrdf,
        float diffuseScale,
        float specularScale) noexcept
    {
        return (hasDiffuseMap && std::isfinite(diffuseScale) &&
                diffuseScale > 0.f) ||
            (hasSpecularMap && hasEnvironmentBrdf &&
                std::isfinite(specularScale) && specularScale > 0.f);
    }

    [[nodiscard]] inline LightProbeConstants
        MakeImageBasedLightingProbeConstants(
            std::uint32_t diffuseArrayIndex,
            std::uint32_t specularArrayIndex,
            float diffuseScale,
            float specularScale,
            float specularMipLevels) noexcept
    {
        LightProbeConstants constants{};
        constants.diffuseScale = diffuseScale;
        constants.specularScale = specularScale;
        constants.mipLevels = specularMipLevels;
        constants.diffuseArrayIndex = diffuseArrayIndex;
        constants.specularArrayIndex = specularArrayIndex;
        for (auto& plane : constants.frustumPlanes)
            plane = { 0.f, 0.f, 0.f, 1.f };
        return constants;
    }

    enum class ImageBasedLightingPreparationStatus : std::uint8_t
    {
        Idle,
        Preparing,
        Ready,
        Failed
    };

    // One explicit state prevents "no active stage" from disguising a failed
    // upload as ready. Production owns this state and tests drive the same
    // transitions without requiring a graphics device.
    class ImageBasedLightingPreparationState
    {
    public:
        void Begin() noexcept
        {
            m_Status = ImageBasedLightingPreparationStatus::Preparing;
        }

        void Complete() noexcept
        {
            m_Status = m_Status ==
                    ImageBasedLightingPreparationStatus::Preparing
                ? ImageBasedLightingPreparationStatus::Ready
                : ImageBasedLightingPreparationStatus::Failed;
        }

        void Fail() noexcept
        {
            m_Status = ImageBasedLightingPreparationStatus::Failed;
        }

        [[nodiscard]] ImageBasedLightingPreparationStatus Get() const noexcept
        {
            return m_Status;
        }

        [[nodiscard]] bool IsReady() const noexcept
        {
            return m_Status == ImageBasedLightingPreparationStatus::Ready;
        }

        [[nodiscard]] bool HasFailed() const noexcept
        {
            return m_Status == ImageBasedLightingPreparationStatus::Failed;
        }

    private:
        ImageBasedLightingPreparationStatus m_Status =
            ImageBasedLightingPreparationStatus::Idle;
    };

}
