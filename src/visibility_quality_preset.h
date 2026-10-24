#pragma once

#include <cstdint>

namespace uvsr
{
    enum class ScreenSpaceVisibilityQuality : uint32_t
    {
        Low,
        Medium,
        High,
        Ultra,
        Custom
    };

    enum class VisibilitySpatialFilter : uint32_t
    {
        JointBilateral,
        GaussianJointBilateral
    };

    struct VisibilityQualityReconstructionPreset
    {
        bool spatialEnabled = false;
        VisibilitySpatialFilter spatialFilter =
            VisibilitySpatialFilter::GaussianJointBilateral;
        float spatialRadius = 4.0f;
    };

    [[nodiscard]] constexpr VisibilityQualityReconstructionPreset
    GetVisibilityQualityReconstructionPreset(
        ScreenSpaceVisibilityQuality quality)
    {
        switch (quality)
        {
        case ScreenSpaceVisibilityQuality::Low:
        case ScreenSpaceVisibilityQuality::Medium:
            return {
                true,
                VisibilitySpatialFilter::JointBilateral,
                4.0f
            };
        case ScreenSpaceVisibilityQuality::High:
        case ScreenSpaceVisibilityQuality::Ultra:
            return {
                true,
                VisibilitySpatialFilter::GaussianJointBilateral,
                4.0f
            };
        default:
            return {};
        }
    }
}
