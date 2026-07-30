#pragma once

namespace uvsr
{
    // Unit gains are the radiometric reference for the screen-space transport
    // contract. The traversal outputs irradiance and the receiving composite
    // applies the diffuse BRDF exactly once. UI values above one are deliberate
    // artistic boosts, not hidden normalization.
    inline constexpr float ScreenSpaceIndirectDiffuseReferenceIntensity = 1.f;
    inline constexpr float ScreenSpaceEmissiveReferenceGain = 1.f;

    [[nodiscard]] inline constexpr bool
        HasActiveScreenSpaceLightingConsumer(
            bool visibilityEnabled,
            bool ambientOcclusionActive,
            bool indirectDiffuseActive,
            bool diffuseEnvironmentActive,
            bool specularEnvironmentActive)
    {
        return visibilityEnabled &&
            (indirectDiffuseActive ||
                (ambientOcclusionActive &&
                    (diffuseEnvironmentActive ||
                        specularEnvironmentActive)));
    }
}
