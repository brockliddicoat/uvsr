#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>

namespace uvsr
{
    constexpr uint32_t DiagnosticCsmMaximumCascades = 4u;
    constexpr uint32_t DiagnosticCsmDefaultResolution = 2048u;
    constexpr uint32_t DiagnosticCsmCachedShadowDrawListSlotCount = 8u;
    constexpr float DiagnosticCsmMaximumFilterRadiusTexels = 8.f;
    // The reference slope saturates at one when NoL squared is at or below
    // 0.5. Stay one representable float below that exact boundary so rounding
    // cannot admit a value whose reference result is just under saturation.
    constexpr float
        DiagnosticCsmConservativeSaturatedSlopeNoLSquaredThreshold =
            0.49999997f;
    constexpr uint32_t
        DiagnosticCsmResolveLightDirectionPermutationCount = 2u;
    constexpr uint32_t
        DiagnosticCsmResolveReceiverTransformPermutationCount = 2u;
    constexpr uint32_t DiagnosticCsmResolvePermutationCount =
        DiagnosticCsmResolveLightDirectionPermutationCount *
        DiagnosticCsmResolveReceiverTransformPermutationCount;

    struct DiagnosticCsmLightFrame
    {
        std::array<double, 3u> x{};
        std::array<double, 3u> y{};
        std::array<double, 3u> z{};
    };

    [[nodiscard]] inline bool TryBuildDiagnosticCsmLightFrame(
        const std::array<double, 3u>& direction,
        const std::array<double, 3u>& preferredUp,
        const std::array<double, 3u>& preferredRight,
        DiagnosticCsmLightFrame& output)
    {
        output = {};
        const auto IsFinite = [](const std::array<double, 3u>& value) {
            return std::isfinite(value[0]) &&
                std::isfinite(value[1]) &&
                std::isfinite(value[2]);
        };
        const auto Dot = [](
            const std::array<double, 3u>& left,
            const std::array<double, 3u>& right) {
                return left[0] * right[0] +
                    left[1] * right[1] +
                    left[2] * right[2];
            };
        const auto Cross = [](
            const std::array<double, 3u>& left,
            const std::array<double, 3u>& right) {
                return std::array<double, 3u>{
                    left[1] * right[2] - left[2] * right[1],
                    left[2] * right[0] - left[0] * right[2],
                    left[0] * right[1] - left[1] * right[0]
                };
            };
        const auto Normalize = [&IsFinite, &Dot](
            std::array<double, 3u>& value) {
                if (!IsFinite(value))
                    return false;
                const double lengthSquared = Dot(value, value);
                if (!std::isfinite(lengthSquared) ||
                    !(lengthSquared > 1e-20))
                {
                    return false;
                }
                const double inverseLength =
                    1.0 / std::sqrt(lengthSquared);
                value = {
                    value[0] * inverseLength,
                    value[1] * inverseLength,
                    value[2] * inverseLength
                };
                return IsFinite(value);
            };
        const auto ProjectPerpendicular = [&Dot](
            const std::array<double, 3u>& value,
            const std::array<double, 3u>& normal) {
                const double projection = Dot(value, normal);
                return std::array<double, 3u>{
                    value[0] - normal[0] * projection,
                    value[1] - normal[1] * projection,
                    value[2] - normal[2] * projection
                };
            };

        output.z = direction;
        if (!Normalize(output.z))
            return false;

        output.y = ProjectPerpendicular(preferredUp, output.z);
        if (!Normalize(output.y))
        {
            output.x = ProjectPerpendicular(
                preferredRight, output.z);
            if (Normalize(output.x))
            {
                output.y = Cross(output.x, output.z);
                if (!Normalize(output.y))
                    return false;
            }
            else
            {
                const std::array<double, 3u> stableUp =
                    std::abs(output.z[1]) < 0.9
                    ? std::array<double, 3u>{ 0.0, 1.0, 0.0 }
                    : std::array<double, 3u>{ 1.0, 0.0, 0.0 };
                output.y = ProjectPerpendicular(
                    stableUp, output.z);
                if (!Normalize(output.y))
                    return false;
            }
        }

        output.x = Cross(output.z, output.y);
        if (!Normalize(output.x))
            return false;
        output.y = Cross(output.x, output.z);
        return Normalize(output.y);
    }

    [[nodiscard]] constexpr bool
        ShouldRetainDiagnosticCsmCoarseBounds(
            bool reliableBounds,
            bool intersectsCascade)
    {
        return !reliableBounds || intersectsCascade;
    }

    [[nodiscard]] constexpr uint32_t
        GetDiagnosticCsmResolveLightDirectionPermutation(
            bool preNormalizedReceiverLightDirectionEnabled)
    {
        // Permutation zero deliberately remains the exact legacy shader.
        return preNormalizedReceiverLightDirectionEnabled ? 1u : 0u;
    }

    [[nodiscard]] constexpr uint32_t
        GetDiagnosticCsmResolvePermutation(
            bool preNormalizedReceiverLightDirectionEnabled,
            bool precomposedClipToShadowEnabled)
    {
        // Keep the two independent optimizations in separate bits. The
        // all-disabled combination deliberately remains permutation zero.
        return GetDiagnosticCsmResolveLightDirectionPermutation(
                preNormalizedReceiverLightDirectionEnabled) +
            (precomposedClipToShadowEnabled
                ? DiagnosticCsmResolveLightDirectionPermutationCount
                : 0u);
    }

    template <typename Matrix>
    [[nodiscard]] inline Matrix BuildDiagnosticCsmReceiverTransform(
        bool precomposedClipToShadowEnabled,
        const Matrix& cameraClipToWorld,
        const Matrix& worldToUvzw)
    {
        return precomposedClipToShadowEnabled
            ? cameraClipToWorld * worldToUvzw
            : worldToUvzw;
    }

    [[nodiscard]] inline float NormalizeDiagnosticCsmFilterRadiusTexels(
        float radius)
    {
        return std::isfinite(radius)
            ? std::clamp(
                radius,
                0.f,
                DiagnosticCsmMaximumFilterRadiusTexels)
            : radius;
    }

    [[nodiscard]] inline bool
        ShouldUseDiagnosticCsmConservativeSaturatedSlope(
            float depthAxisDot,
            float inverseDepthAxisLength,
            float normalLengthSquared)
    {
        if (!std::isfinite(inverseDepthAxisLength) ||
            !(inverseDepthAxisLength > 0.f) ||
            !std::isfinite(normalLengthSquared) ||
            !(normalLengthSquared > 1e-12f))
        {
            return false;
        }

        const float projectedNumerator =
            depthAxisDot * inverseDepthAxisLength;
        const float projectedNumeratorSquared =
            projectedNumerator * projectedNumerator;
        const float saturationLimit =
            DiagnosticCsmConservativeSaturatedSlopeNoLSquaredThreshold *
            normalLengthSquared;
        return std::isfinite(projectedNumeratorSquared) &&
            std::isfinite(saturationLimit) &&
            projectedNumeratorSquared <= saturationLimit;
    }

    [[nodiscard]] inline bool TryComputeDiagnosticCsmAlgebraicSlowSlope(
        float depthAxisDot,
        float inverseDepthAxisLength,
        float normalLengthSquared,
        float& slope)
    {
        slope = 1.f;
        if (!std::isfinite(inverseDepthAxisLength) ||
            !(inverseDepthAxisLength > 0.f) ||
            !std::isfinite(normalLengthSquared) ||
            !(normalLengthSquared > 1e-12f))
        {
            return false;
        }

        const float projectedNumerator =
            depthAxisDot * inverseDepthAxisLength;
        const float projectedNumeratorSquared =
            projectedNumerator * projectedNumerator;
        if (!std::isfinite(projectedNumeratorSquared))
            return false;

        const float projectedMagnitude = std::abs(projectedNumerator);
        if (projectedMagnitude == 0.f)
            return true;

        const float remainingNormalSquared = std::max(
            normalLengthSquared - projectedNumeratorSquared,
            0.f);
        const float result =
            std::sqrt(remainingNormalSquared) / projectedMagnitude;
        if (!std::isfinite(result))
            return false;

        slope = std::clamp(result, 0.f, 1.f);
        return true;
    }

    enum class DiagnosticCsmProfile : uint32_t
    {
        SingleMapReference,
        LowCostCsm,
        Ue5CsmReference,
        CachedSingleShadow,
        OptimizedCachedSingleShadow,
        OptimizedCachedCsm,
        Custom,
        Count
    };

    enum class DiagnosticCsmFilter : uint32_t
    {
        Ue5Pcf5x5,
        Poisson,
        Count
    };

    enum class DiagnosticCsmDebugView : uint32_t
    {
        None,
        Visibility,
        Cascade,
        CacheAction,
        Count
    };

    enum class DiagnosticCsmUpdateAction : uint32_t
    {
        Reused,
        FullRedraw,
        DirtyRectangles,
        Scrolled,
        Count
    };

    [[nodiscard]] inline bool
        TryGetDiagnosticCsmTranslationOnlyTransform(
            const std::array<float, 12u>& shaderTransform,
            std::array<float, 3u>& translation)
    {
        translation = {};

        // Donut's affineToColumnMajor layout is three HLSL float4 rows:
        // identity-linear elements occupy 0/5/10 and translation 3/7/11.
        constexpr std::array<size_t, 3u> DiagonalIndices = {
            0u, 5u, 10u
        };
        constexpr std::array<size_t, 6u> OffDiagonalIndices = {
            1u, 2u, 4u, 6u, 8u, 9u
        };
        constexpr std::array<size_t, 3u> TranslationIndices = {
            3u, 7u, 11u
        };

        for (size_t index : DiagonalIndices)
        {
            if (shaderTransform[index] != 1.f)
                return false;
        }
        for (size_t index : OffDiagonalIndices)
        {
            const float value = shaderTransform[index];
            if (value != 0.f || std::signbit(value))
                return false;
        }
        for (size_t component = 0u;
            component < TranslationIndices.size();
            ++component)
        {
            const float value =
                shaderTransform[TranslationIndices[component]];
            if (!std::isfinite(value))
                return false;
        }
        for (size_t component = 0u;
            component < TranslationIndices.size();
            ++component)
        {
            translation[component] =
                shaderTransform[TranslationIndices[component]];
        }
        return true;
    }

    [[nodiscard]] constexpr bool
        ShouldUseDiagnosticCsmTranslationOnlyDraw(
            bool enabled,
            bool lookupCurrent,
            uint32_t instanceCount)
    {
        return enabled && lookupCurrent && instanceCount == 1u;
    }

    [[nodiscard]] constexpr bool
        TryGetDiagnosticCsmTranslationRegistryIndex(
            int signedInstanceIndex,
            size_t registrySize,
            uint32_t& registryIndex)
    {
        registryIndex = 0u;
        if (signedInstanceIndex < 0 ||
            size_t(signedInstanceIndex) >= registrySize)
        {
            return false;
        }

        registryIndex = uint32_t(signedInstanceIndex);
        return true;
    }

    [[nodiscard]] constexpr bool
        ShouldUseDiagnosticCsmInputAssemblerCasterFetch(
            bool enabled,
            bool translationOnly,
            bool deforming,
            bool hasPosition,
            bool hasTexCoord,
            bool hasNormal,
            bool hasInstanceBuffer)
    {
        return enabled &&
            !translationOnly &&
            !deforming &&
            hasPosition &&
            hasTexCoord &&
            hasNormal &&
            hasInstanceBuffer;
    }

    struct DiagnosticCascadedShadowMapSettings
    {
        bool enabled = false;
        DiagnosticCsmProfile profile =
            DiagnosticCsmProfile::Ue5CsmReference;

        uint32_t cascadeCount = 4u;
        uint32_t shadowMapResolution = DiagnosticCsmDefaultResolution;
        float maximumShadowDistance = 1000.f;
        float maximumLightDepth = 2000.f;
        float cascadeDistributionExponent = 4.f;
        float cascadeTransitionFraction = 0.1f;
        float shadowDistanceFadeoutFraction = 0.1f;
        uint32_t projectionSnapTexelMultiple = 4u;
        bool enforceUeMinimumLightDepth = true;

        float depthBias = 10.f;
        float slopeScaledDepthBias = 3.f;
        float directionalLightShadowBias = 0.5f;
        float directionalLightShadowSlopeBias = 0.5f;
        float receiverDepthBias = 0.9f;

        DiagnosticCsmFilter filter = DiagnosticCsmFilter::Ue5Pcf5x5;
        uint32_t poissonTapCount = 16u;
        float filterRadiusTexels = 3.f;

        // These conventional CSM depth, receiver, and caster-path
        // optimizations remain independent so each legacy path is always one
        // toggle away and incurs no extra GPU work when selected.
        bool use16BitDepthEnabled = true;
        bool opaqueDepthStateMergingEnabled = true;
        bool positionOnlyOpaqueEnabled = true;
        bool translationOnlyCasterTransformEnabled = true;
        // Experimental CSM-local alternative for the non-translation caster
        // minority. Unsupported or ambiguous casters retain the manual-fetch
        // depth path, and named reference profiles deliberately leave this
        // disabled until a matched A/B proves it beneficial.
        bool inputAssemblerCasterFetchEnabled = false;
        bool precomputedDepthAxisInverseLengthEnabled = true;
        bool conservativeSaturatedSlopeEnabled = true;
        bool algebraicSlowSlopeEnabled = true;
        bool preNormalizedReceiverLightDirectionEnabled = true;
        bool precomposedClipToShadowEnabled = true;
        bool accurateCasterCullingEnabled = true;
        bool ueCasterRadiusThresholdEnabled = true;
        float casterRadiusThreshold = 0.01f;
        bool singleTraversalCasterClassificationEnabled = true;
        bool precomputedReceiverHullAxesEnabled = true;
        bool sharedCasterLightProjectionEnabled = true;
        bool directCasterSubmissionEnabled = true;
        bool cachedShadowDrawListsEnabled = true;
        bool batchedFullRedrawClearEnabled = true;
        // UE exposes the analogous r.Shadow.CSMScissorOptim as an optional,
        // default-off optimization. UVSR keeps it independently reversible
        // and gates it off whenever cached texels could outlive the current
        // camera receiver footprint.
        bool receiverRasterScissorEnabled = true;

        bool wholeMapReuseEnabled = false;
        bool wholeCascadeReuseEnabled = false;
        bool dirtyRectanglesEnabled = false;
        bool scrollingEnabled = false;
        float minimumScrollOverlap = 0.75f;

        bool detailedGpuTimingEnabled = true;
        DiagnosticCsmDebugView debugView = DiagnosticCsmDebugView::None;
    };

    [[nodiscard]] constexpr const char* GetDiagnosticCsmProfileLabel(
        DiagnosticCsmProfile profile)
    {
        switch (profile)
        {
        case DiagnosticCsmProfile::SingleMapReference:
            return "Single-Map Reference";
        case DiagnosticCsmProfile::LowCostCsm:
            return "Low-Cost CSM";
        case DiagnosticCsmProfile::Ue5CsmReference:
            return "UE5 CSM Reference";
        case DiagnosticCsmProfile::CachedSingleShadow:
            return "Cached Single Shadow";
        case DiagnosticCsmProfile::OptimizedCachedSingleShadow:
            return "Optimized Cached Single Shadow";
        case DiagnosticCsmProfile::OptimizedCachedCsm:
            return "Optimized Cached CSM";
        case DiagnosticCsmProfile::Custom:
            return "(Custom)";
        default:
            return "Unknown";
        }
    }

    [[nodiscard]] constexpr const char* GetDiagnosticCsmFilterLabel(
        DiagnosticCsmFilter filter)
    {
        switch (filter)
        {
        case DiagnosticCsmFilter::Ue5Pcf5x5:
            return "UE5 Manual 5x5 PCF";
        case DiagnosticCsmFilter::Poisson:
            return "SVSM-Matched Point Poisson";
        default:
            return "Unknown";
        }
    }

    [[nodiscard]] constexpr const char* GetDiagnosticCsmDebugViewLabel(
        DiagnosticCsmDebugView view)
    {
        switch (view)
        {
        case DiagnosticCsmDebugView::None:
            return "None";
        case DiagnosticCsmDebugView::Visibility:
            return "Visibility";
        case DiagnosticCsmDebugView::Cascade:
            return "Cascade Selection";
        case DiagnosticCsmDebugView::CacheAction:
            return "Cache Action";
        default:
            return "Unknown";
        }
    }

    [[nodiscard]] inline DiagnosticCascadedShadowMapSettings
        ApplyDiagnosticCsmProfile(
            const DiagnosticCascadedShadowMapSettings& current,
            DiagnosticCsmProfile profile)
    {
        if (profile == DiagnosticCsmProfile::Custom ||
            profile >= DiagnosticCsmProfile::Count)
        {
            return current;
        }

        DiagnosticCascadedShadowMapSettings result = current;
        result.profile = profile;
        result.shadowMapResolution = DiagnosticCsmDefaultResolution;
        result.cascadeDistributionExponent = 4.f;
        result.cascadeTransitionFraction = 0.1f;
        result.shadowDistanceFadeoutFraction = 0.1f;
        result.projectionSnapTexelMultiple = 4u;
        result.enforceUeMinimumLightDepth = true;
        result.depthBias = 10.f;
        result.slopeScaledDepthBias = 3.f;
        result.directionalLightShadowBias = 0.5f;
        result.directionalLightShadowSlopeBias = 0.5f;
        result.receiverDepthBias = 0.9f;
        result.filter = DiagnosticCsmFilter::Ue5Pcf5x5;
        result.poissonTapCount = 16u;
        result.filterRadiusTexels = 3.f;
        result.use16BitDepthEnabled = true;
        result.opaqueDepthStateMergingEnabled = true;
        result.positionOnlyOpaqueEnabled = true;
        result.translationOnlyCasterTransformEnabled = true;
        result.inputAssemblerCasterFetchEnabled = false;
        result.precomputedDepthAxisInverseLengthEnabled = true;
        result.conservativeSaturatedSlopeEnabled = true;
        result.algebraicSlowSlopeEnabled = true;
        result.preNormalizedReceiverLightDirectionEnabled = true;
        result.precomposedClipToShadowEnabled = true;
        result.accurateCasterCullingEnabled = true;
        result.ueCasterRadiusThresholdEnabled = true;
        result.casterRadiusThreshold = 0.01f;
        result.singleTraversalCasterClassificationEnabled = true;
        result.precomputedReceiverHullAxesEnabled = true;
        result.sharedCasterLightProjectionEnabled = true;
        result.directCasterSubmissionEnabled = true;
        result.cachedShadowDrawListsEnabled = true;
        result.batchedFullRedrawClearEnabled = true;
        result.receiverRasterScissorEnabled = true;
        result.wholeMapReuseEnabled = false;
        result.wholeCascadeReuseEnabled = false;
        result.dirtyRectanglesEnabled = false;
        result.scrollingEnabled = false;
        result.minimumScrollOverlap = 0.75f;

        switch (profile)
        {
        case DiagnosticCsmProfile::SingleMapReference:
            result.cascadeCount = 1u;
            result.cachedShadowDrawListsEnabled = false;
            break;
        case DiagnosticCsmProfile::LowCostCsm:
            result.cascadeCount = 2u;
            break;
        case DiagnosticCsmProfile::Ue5CsmReference:
            result.cascadeCount = 4u;
            break;
        case DiagnosticCsmProfile::CachedSingleShadow:
            result.cascadeCount = 1u;
            result.wholeMapReuseEnabled = true;
            break;
        case DiagnosticCsmProfile::OptimizedCachedSingleShadow:
            result.cascadeCount = 1u;
            result.wholeCascadeReuseEnabled = true;
            result.dirtyRectanglesEnabled = true;
            break;
        case DiagnosticCsmProfile::OptimizedCachedCsm:
            result.cascadeCount = 4u;
            result.wholeCascadeReuseEnabled = true;
            result.dirtyRectanglesEnabled = true;
            result.scrollingEnabled = true;
            break;
        default:
            break;
        }

        return result;
    }

    [[nodiscard]] constexpr bool IsDiagnosticCsmCascadeCountValid(
        uint32_t cascadeCount)
    {
        return cascadeCount >= 1u &&
            cascadeCount <= DiagnosticCsmMaximumCascades;
    }

    struct DiagnosticCsmCasterGatherPlan
    {
        uint32_t cascadeMask = 0u;
        uint32_t cascadeCount = 0u;
        bool singleTraversal = false;
    };

    [[nodiscard]] inline DiagnosticCsmCasterGatherPlan
        BuildDiagnosticCsmCasterGatherPlan(
            bool singleTraversalRequested,
            uint32_t cascadeCount,
            const std::array<DiagnosticCsmUpdateAction,
                DiagnosticCsmMaximumCascades>& actions)
    {
        DiagnosticCsmCasterGatherPlan plan;
        if (!IsDiagnosticCsmCascadeCountValid(cascadeCount))
            return plan;

        for (uint32_t cascade = 0u;
            cascade < cascadeCount;
            ++cascade)
        {
            if (actions[cascade] !=
                DiagnosticCsmUpdateAction::Reused)
            {
                plan.cascadeMask |= 1u << cascade;
                ++plan.cascadeCount;
            }
        }
        plan.singleTraversal =
            singleTraversalRequested &&
            plan.cascadeCount > 1u;
        return plan;
    }

    [[nodiscard]] inline bool NextDiagnosticCsmCasterSubmissionIndex(
        size_t casterCount,
        const size_t* indices,
        size_t indexCount,
        bool indexed,
        size_t& readIndex,
        size_t& casterIndex)
    {
        if (!indexed)
        {
            if (readIndex >= casterCount)
                return false;
            casterIndex = readIndex++;
            return true;
        }
        if (!indices)
            return false;

        while (readIndex < indexCount)
        {
            const size_t candidate = indices[readIndex++];
            if (candidate < casterCount)
            {
                casterIndex = candidate;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] inline uint32_t NormalizeDiagnosticCsmTapCount(
        uint32_t tapCount)
    {
        if (tapCount <= 1u)
            return 1u;
        if (tapCount <= 4u)
            return 4u;
        if (tapCount <= 8u)
            return 8u;
        return 16u;
    }

    [[nodiscard]] inline bool HasAnyDiagnosticCsmCachePolicy(
        const DiagnosticCascadedShadowMapSettings& settings)
    {
        return settings.wholeMapReuseEnabled ||
            settings.wholeCascadeReuseEnabled;
    }

    // A cached map already avoids caster gathering. Keep the independent
    // draw-list cache focused on full-redraw profiles, where it can remove the
    // scene walk and sort without interacting with localized cache metadata.
    [[nodiscard]] inline bool
        IsDiagnosticCsmCachedShadowDrawListEligible(
            const DiagnosticCascadedShadowMapSettings& settings,
            bool sceneStateRevisionReliable)
    {
        return settings.cachedShadowDrawListsEnabled &&
            sceneStateRevisionReliable &&
            !HasAnyDiagnosticCsmCachePolicy(settings);
    }

    [[nodiscard]] constexpr bool IsDiagnosticCsmSceneStateChanged(
        bool requiresFullSceneInvalidation,
        bool sceneStateCompatible,
        uint64_t sceneStateRevision,
        uint64_t previousSceneStateRevision)
    {
        return requiresFullSceneInvalidation ||
            !sceneStateCompatible ||
            sceneStateRevision != previousSceneStateRevision;
    }

    [[nodiscard]] constexpr bool
        ShouldInvalidateDiagnosticCsmCachedShadowDrawLists(
            bool cachedShadowDrawListsEnabled,
            bool requiresFullSceneInvalidation,
            bool sceneStateRevisionReliable)
    {
        return cachedShadowDrawListsEnabled &&
            (requiresFullSceneInvalidation ||
                !sceneStateRevisionReliable);
    }

    [[nodiscard]] constexpr bool
        ShouldReleaseDiagnosticCsmCachedShadowDrawLists(
            bool cacheWasEligible,
            bool cacheIsEligible)
    {
        return cacheWasEligible && !cacheIsEligible;
    }

    [[nodiscard]] inline bool
        TryRestoreDiagnosticCsmCachedTranslationOnlyTransform(
            bool translationOnlyTransform,
            const std::array<float, 3u>& cachedTranslation,
            std::array<float, 3u>& restoredTranslation)
    {
        restoredTranslation = {};
        if (!translationOnlyTransform ||
            !std::isfinite(cachedTranslation[0]) ||
            !std::isfinite(cachedTranslation[1]) ||
            !std::isfinite(cachedTranslation[2]))
        {
            return false;
        }
        restoredTranslation = cachedTranslation;
        return true;
    }

    [[nodiscard]] constexpr uint32_t
        SelectDiagnosticCsmCachedShadowDrawListSlot(
            const std::array<bool,
                DiagnosticCsmCachedShadowDrawListSlotCount>& valid,
            const std::array<uint64_t,
                DiagnosticCsmCachedShadowDrawListSlotCount>& lastUse)
    {
        uint32_t selected = 0u;
        for (uint32_t slot = 0u;
            slot < DiagnosticCsmCachedShadowDrawListSlotCount;
            ++slot)
        {
            if (!valid[slot])
                return slot;
            if (lastUse[slot] < lastUse[selected])
                selected = slot;
        }
        return selected;
    }

    [[nodiscard]] inline bool IsSameDiagnosticCsmTimingConfiguration(
        const DiagnosticCascadedShadowMapSettings& left,
        const DiagnosticCascadedShadowMapSettings& right);

    [[nodiscard]] inline bool
        IsSameDiagnosticCsmDrawListConfiguration(
            const DiagnosticCascadedShadowMapSettings& left,
            const DiagnosticCascadedShadowMapSettings& right)
    {
        // The timing identity already exhaustively includes every projection,
        // culling, filter-footprint, sorting, depth-path, and cache-policy
        // input. Reuse that strict key rather than maintaining a fragile
        // partial membership list.
        return IsSameDiagnosticCsmTimingConfiguration(left, right);
    }

    [[nodiscard]] inline bool IsSameDiagnosticCsmTimingConfiguration(
        const DiagnosticCascadedShadowMapSettings& left,
        const DiagnosticCascadedShadowMapSettings& right)
    {
        return left.cascadeCount == right.cascadeCount &&
            left.shadowMapResolution == right.shadowMapResolution &&
            left.maximumShadowDistance == right.maximumShadowDistance &&
            left.maximumLightDepth == right.maximumLightDepth &&
            left.cascadeDistributionExponent ==
                right.cascadeDistributionExponent &&
            left.cascadeTransitionFraction ==
                right.cascadeTransitionFraction &&
            left.shadowDistanceFadeoutFraction ==
                right.shadowDistanceFadeoutFraction &&
            left.projectionSnapTexelMultiple ==
                right.projectionSnapTexelMultiple &&
            left.enforceUeMinimumLightDepth ==
                right.enforceUeMinimumLightDepth &&
            left.depthBias == right.depthBias &&
            left.slopeScaledDepthBias == right.slopeScaledDepthBias &&
            left.directionalLightShadowBias ==
                right.directionalLightShadowBias &&
            left.directionalLightShadowSlopeBias ==
                right.directionalLightShadowSlopeBias &&
            left.receiverDepthBias == right.receiverDepthBias &&
            left.filter == right.filter &&
            left.poissonTapCount == right.poissonTapCount &&
            left.filterRadiusTexels == right.filterRadiusTexels &&
            left.use16BitDepthEnabled == right.use16BitDepthEnabled &&
            left.opaqueDepthStateMergingEnabled ==
                right.opaqueDepthStateMergingEnabled &&
            left.positionOnlyOpaqueEnabled ==
                right.positionOnlyOpaqueEnabled &&
            left.translationOnlyCasterTransformEnabled ==
                right.translationOnlyCasterTransformEnabled &&
            left.inputAssemblerCasterFetchEnabled ==
                right.inputAssemblerCasterFetchEnabled &&
            left.precomputedDepthAxisInverseLengthEnabled ==
                right.precomputedDepthAxisInverseLengthEnabled &&
            left.conservativeSaturatedSlopeEnabled ==
                right.conservativeSaturatedSlopeEnabled &&
            left.algebraicSlowSlopeEnabled ==
                right.algebraicSlowSlopeEnabled &&
            left.preNormalizedReceiverLightDirectionEnabled ==
                right.preNormalizedReceiverLightDirectionEnabled &&
            left.precomposedClipToShadowEnabled ==
                right.precomposedClipToShadowEnabled &&
            left.accurateCasterCullingEnabled ==
                right.accurateCasterCullingEnabled &&
            left.ueCasterRadiusThresholdEnabled ==
                right.ueCasterRadiusThresholdEnabled &&
            left.casterRadiusThreshold == right.casterRadiusThreshold &&
            left.singleTraversalCasterClassificationEnabled ==
                right.singleTraversalCasterClassificationEnabled &&
            left.precomputedReceiverHullAxesEnabled ==
                right.precomputedReceiverHullAxesEnabled &&
            left.sharedCasterLightProjectionEnabled ==
                right.sharedCasterLightProjectionEnabled &&
            left.directCasterSubmissionEnabled ==
                right.directCasterSubmissionEnabled &&
            left.cachedShadowDrawListsEnabled ==
                right.cachedShadowDrawListsEnabled &&
            left.batchedFullRedrawClearEnabled ==
                right.batchedFullRedrawClearEnabled &&
            left.receiverRasterScissorEnabled ==
                right.receiverRasterScissorEnabled &&
            left.wholeMapReuseEnabled == right.wholeMapReuseEnabled &&
            left.wholeCascadeReuseEnabled ==
                right.wholeCascadeReuseEnabled &&
            left.dirtyRectanglesEnabled ==
                right.dirtyRectanglesEnabled &&
            left.scrollingEnabled == right.scrollingEnabled &&
            left.minimumScrollOverlap == right.minimumScrollOverlap &&
            left.detailedGpuTimingEnabled ==
                right.detailedGpuTimingEnabled &&
            left.debugView == right.debugView;
    }

    [[nodiscard]] inline float ComputeDiagnosticCsmLightDepthSpan(
        float projectionRadius,
        float requestedDepthSpan)
    {
        if (!std::isfinite(projectionRadius) ||
            !std::isfinite(requestedDepthSpan) ||
            !(projectionRadius > 0.f) ||
            !(requestedDepthSpan > 0.f))
        {
            return 0.f;
        }
        return 2.f * std::max(
            projectionRadius,
            requestedDepthSpan * 0.5f);
    }

    // Normalize the exact row-vector world-to-clip Z column used by the
    // directional depth shader. Its length includes both the light-view basis
    // scale and the orthographic projection scale, so the nominal depth span
    // alone is not exact for every accepted finite basis.
    [[nodiscard]] inline float
        ComputeDiagnosticCsmDepthAxisInverseLength(
            const std::array<float, 3u>& scaledDepthAxis)
    {
        if (!std::isfinite(scaledDepthAxis[0]) ||
            !std::isfinite(scaledDepthAxis[1]) ||
            !std::isfinite(scaledDepthAxis[2]))
        {
            return 0.f;
        }

        const float lengthSquared =
            scaledDepthAxis[0] * scaledDepthAxis[0] +
            scaledDepthAxis[1] * scaledDepthAxis[1] +
            scaledDepthAxis[2] * scaledDepthAxis[2];
        if (!std::isfinite(lengthSquared) || !(lengthSquared > 1e-12f))
            return 0.f;

        const float inverseLength = 1.f / std::sqrt(lengthSquared);
        return std::isfinite(inverseLength) && inverseLength > 0.f
            ? inverseLength
            : 0.f;
    }

    [[nodiscard]] inline float ComputeDiagnosticCsmProjectionGuardTexels(
        float filterRadiusTexels,
        uint32_t projectionSnapTexelMultiple)
    {
        filterRadiusTexels = std::isfinite(filterRadiusTexels)
            ? std::max(filterRadiusTexels, 0.f)
            : 0.f;
        return std::ceil(
            filterRadiusTexels + 0.5f +
            float(std::max(projectionSnapTexelMultiple, 1u)));
    }

    [[nodiscard]] inline float SnapUeCsmProjectionCoordinate(
        float coordinate,
        float snapWorldSize)
    {
        if (!std::isfinite(coordinate) ||
            !std::isfinite(snapWorldSize) ||
            !(snapWorldSize > 0.f))
        {
            return coordinate;
        }
        return coordinate - std::fmod(coordinate, snapWorldSize);
    }

    // UE's directional CSM receiver transition is derived from the constant
    // depth bias, the normalized light-depth span and the cascade's world-space
    // texel scale. The shader consumes the reciprocal. Clamp the transition
    // exactly as UE does so a zero bias is finite and approaches a hard test.
    [[nodiscard]] inline float ComputeUeCsmSoftTransitionScale(
        float depthBias,
        float lightDepthSpan,
        float projectionRadius,
        uint32_t resolution)
    {
        constexpr float MinimumTransitionSize = 1e-5f;
        if (!std::isfinite(depthBias) ||
            !std::isfinite(lightDepthSpan) ||
            !std::isfinite(projectionRadius) ||
            !(lightDepthSpan > 0.f) ||
            !(projectionRadius > 0.f) ||
            resolution == 0u)
        {
            return 0.f;
        }

        const float worldSpaceTexelScale =
            projectionRadius / float(resolution);
        const float transitionSize = std::max(
            std::max(depthBias, 0.f) / lightDepthSpan *
                worldSpaceTexelScale,
            MinimumTransitionSize);
        return 1.f / transitionSize;
    }

    // UE applies the directional-light constant bias in normalized shadow
    // depth. This keeps the effective bias independent of D16 versus D32 and
    // scales it with each cascade's world-space texel footprint.
    [[nodiscard]] inline float ComputeUeCsmShaderDepthBias(
        float depthBias,
        float lightDepthSpan,
        float projectionRadius,
        uint32_t resolution)
    {
        if (!std::isfinite(depthBias) ||
            !std::isfinite(lightDepthSpan) ||
            !std::isfinite(projectionRadius) ||
            !(lightDepthSpan > 0.f) ||
            !(projectionRadius > 0.f) ||
            resolution == 0u)
        {
            return 0.f;
        }
        return std::max(depthBias, 0.f) / lightDepthSpan *
            (projectionRadius / float(resolution));
    }

    // r.Shadow.CSMReceiverBias changes UE's soft comparison transition as a
    // function of the receiver normal; it is not a direct shadow-depth offset.
    [[nodiscard]] inline float ComputeUeCsmReceiverTransitionScale(
        float baseTransitionScale,
        float receiverBias,
        float normalDotLight)
    {
        if (!std::isfinite(baseTransitionScale) ||
            !std::isfinite(receiverBias) ||
            !std::isfinite(normalDotLight) ||
            !(baseTransitionScale > 0.f))
        {
            return 0.f;
        }
        receiverBias = std::clamp(receiverBias, 0.f, 1.f);
        normalDotLight = std::clamp(normalDotLight, 0.f, 1.f);
        return baseTransitionScale *
            ((1.f - receiverBias) * (1.f - normalDotLight) +
                normalDotLight);
    }

    [[nodiscard]] inline bool ShouldCullDiagnosticCsmCasterByRadiusSquared(
        float casterRadiusSquared,
        float distanceToCameraSquared,
        float radiusThreshold)
    {
        if (!std::isfinite(casterRadiusSquared) ||
            !std::isfinite(distanceToCameraSquared) ||
            !std::isfinite(radiusThreshold) ||
            !(casterRadiusSquared >= 0.f) ||
            !(distanceToCameraSquared >= 0.f) ||
            !(radiusThreshold > 0.f))
        {
            return false;
        }
        const float thresholdRadiusSquared =
            radiusThreshold * radiusThreshold *
                distanceToCameraSquared;
        return std::isfinite(thresholdRadiusSquared) &&
            casterRadiusSquared < thresholdRadiusSquared;
    }

    [[nodiscard]] constexpr bool
        ShouldBuildDiagnosticCsmSharedCasterLightShape(
            bool radiusRejected,
            bool sharedCasterProjectionActive,
            bool reliableBounds)
    {
        return !radiusRejected &&
            sharedCasterProjectionActive &&
            reliableBounds;
    }

    // Receiver-hull and camera-size rejection are view-dependent. Cached maps
    // must retain the caster population that produced their reused texels.
    [[nodiscard]] inline bool CanUseDiagnosticCsmViewDependentCasterCulling(
        const DiagnosticCascadedShadowMapSettings& settings)
    {
        return !HasAnyDiagnosticCsmCachePolicy(settings);
    }

    [[nodiscard]] inline bool IsDiagnosticCsmCasterRadiusThresholdActive(
        const DiagnosticCascadedShadowMapSettings& settings)
    {
        return settings.ueCasterRadiusThresholdEnabled &&
            std::isfinite(settings.casterRadiusThreshold) &&
            settings.casterRadiusThreshold > 0.f &&
            CanUseDiagnosticCsmViewDependentCasterCulling(settings);
    }

    [[nodiscard]] inline bool
        CanUseDiagnosticCsmReceiverRasterScissor(
            const DiagnosticCascadedShadowMapSettings& settings)
    {
        return settings.receiverRasterScissorEnabled &&
            CanUseDiagnosticCsmViewDependentCasterCulling(settings);
    }

    struct DiagnosticCsmFullRedrawClearPlan
    {
        bool batched = false;
        uint32_t baseArraySlice = 0u;
        uint32_t arraySliceCount = 0u;
    };

    // A single DSV clear can cover a contiguous texture-array range. Restrict
    // the optimization to two or more active cascades that are all receiving
    // identical full-map clears; every mixed or localized update keeps the
    // legacy per-cascade command sequence.
    [[nodiscard]] inline DiagnosticCsmFullRedrawClearPlan
        BuildDiagnosticCsmFullRedrawClearPlan(
            const DiagnosticCascadedShadowMapSettings& settings,
            const std::array<DiagnosticCsmUpdateAction,
                DiagnosticCsmMaximumCascades>& actions)
    {
        DiagnosticCsmFullRedrawClearPlan plan;
        if (!settings.batchedFullRedrawClearEnabled ||
            !IsDiagnosticCsmCascadeCountValid(settings.cascadeCount) ||
            settings.cascadeCount < 2u)
        {
            return plan;
        }

        for (uint32_t cascade = 0u;
            cascade < settings.cascadeCount;
            ++cascade)
        {
            if (actions[cascade] != DiagnosticCsmUpdateAction::FullRedraw)
                return plan;
        }

        plan.batched = true;
        plan.arraySliceCount = settings.cascadeCount;
        return plan;
    }

    [[nodiscard]] constexpr DiagnosticCsmUpdateAction
        FinalizeDiagnosticCsmLocalizedSceneAction(
            DiagnosticCsmUpdateAction action,
            bool sceneChanged,
            bool changedCasterOverlapsCascade)
    {
        if (sceneChanged && !changedCasterOverlapsCascade &&
            action == DiagnosticCsmUpdateAction::DirtyRectangles)
        {
            return DiagnosticCsmUpdateAction::Reused;
        }
        return action;
    }

    [[nodiscard]] constexpr bool
        ShouldResetDiagnosticCsmDepthBindings(
            bool sceneChanged,
            bool requiresFullSceneInvalidation)
    {
        return sceneChanged && requiresFullSceneInvalidation;
    }

    // UE5's ComputeAccumulatedScale iteratively sums geometric interval
    // weights. Keeping the same accumulation order also handles exponent 1
    // without a special closed-form singularity.
    [[nodiscard]] inline float ComputeUeCsmAccumulatedScale(
        float exponent,
        uint32_t splitIndex,
        uint32_t cascadeCount)
    {
        if (!IsDiagnosticCsmCascadeCountValid(cascadeCount))
            return 0.f;

        exponent = std::isfinite(exponent)
            ? std::max(exponent, 1.f)
            : 1.f;
        splitIndex = std::min(splitIndex, cascadeCount);

        float currentScale = 1.f;
        float totalScale = 0.f;
        float accumulatedScale = 0.f;
        for (uint32_t cascade = 0u;
            cascade < cascadeCount;
            ++cascade)
        {
            if (cascade < splitIndex)
                accumulatedScale += currentScale;
            totalScale += currentScale;
            currentScale *= exponent;
        }

        return totalScale > 0.f
            ? accumulatedScale / totalScale
            : 0.f;
    }

    struct DiagnosticCsmSplitSet
    {
        std::array<float, DiagnosticCsmMaximumCascades + 1u> distances{};
        uint32_t cascadeCount = 0u;
        bool valid = false;
    };

    [[nodiscard]] inline DiagnosticCsmSplitSet ComputeUeCsmSplits(
        float nearDistance,
        float maximumShadowDistance,
        float exponent,
        uint32_t cascadeCount)
    {
        DiagnosticCsmSplitSet result;
        result.cascadeCount = cascadeCount;
        if (!IsDiagnosticCsmCascadeCountValid(cascadeCount) ||
            !std::isfinite(nearDistance) ||
            !std::isfinite(maximumShadowDistance) ||
            !(nearDistance >= 0.f) ||
            !(maximumShadowDistance > nearDistance))
        {
            return result;
        }

        result.distances[0] = nearDistance;
        for (uint32_t split = 1u;
            split <= cascadeCount;
            ++split)
        {
            const float scale = ComputeUeCsmAccumulatedScale(
                exponent, split, cascadeCount);
            result.distances[split] = nearDistance +
                scale * (maximumShadowDistance - nearDistance);
            if (!std::isfinite(result.distances[split]) ||
                !(result.distances[split] >
                    result.distances[split - 1u]))
            {
                return result;
            }
        }

        result.distances[cascadeCount] = maximumShadowDistance;
        for (uint32_t split = cascadeCount + 1u;
            split < result.distances.size();
            ++split)
        {
            result.distances[split] = maximumShadowDistance;
        }
        result.valid = true;
        return result;
    }

    struct DiagnosticCsmCascadeRange
    {
        float nominalNear = 0.f;
        float nominalFar = 0.f;
        float projectedFar = 0.f;
        float cascadeFadeOffset = 0.f;
        float cascadeFadeLength = 0.f;
    };

    [[nodiscard]] inline DiagnosticCsmCascadeRange
        ComputeUeCsmCascadeRange(
            float nominalNear,
            float nominalFar,
            float transitionFraction,
            bool lastCascade)
    {
        DiagnosticCsmCascadeRange result;
        result.nominalNear = nominalNear;
        result.nominalFar = nominalFar;
        if (!std::isfinite(nominalNear) ||
            !std::isfinite(nominalFar) ||
            !(nominalFar > nominalNear))
        {
            return result;
        }

        transitionFraction = std::isfinite(transitionFraction)
            ? std::clamp(transitionFraction, 0.f, 1.f)
            : 0.f;
        const float extension =
            (nominalFar - nominalNear) * transitionFraction;
        if (lastCascade)
        {
            result.projectedFar = nominalFar;
            result.cascadeFadeOffset = nominalFar - extension;
            result.cascadeFadeLength = extension;
        }
        else
        {
            result.projectedFar = nominalFar + extension;
            result.cascadeFadeOffset = nominalFar;
            result.cascadeFadeLength = extension;
        }
        return result;
    }

    [[nodiscard]] inline float EvaluateDiagnosticCsmFadeAlpha(
        float viewDepth,
        float fadeOffset,
        float fadeLength)
    {
        if (!std::isfinite(viewDepth) ||
            !std::isfinite(fadeOffset) ||
            !std::isfinite(fadeLength))
        {
            return 0.f;
        }
        if (!(fadeLength > 0.f))
            return viewDepth <= fadeOffset ? 1.f : 0.f;
        return 1.f - std::clamp(
            (viewDepth - fadeOffset) / fadeLength,
            0.f,
            1.f);
    }

    [[nodiscard]] inline float EvaluateDiagnosticCsmDistanceFadeAlpha(
        float viewDepth,
        float maximumShadowDistance,
        float fadeoutFraction)
    {
        if (!std::isfinite(viewDepth) ||
            !std::isfinite(maximumShadowDistance) ||
            !(maximumShadowDistance > 0.f))
        {
            return 0.f;
        }
        fadeoutFraction = std::isfinite(fadeoutFraction)
            ? std::clamp(fadeoutFraction, 0.f, 1.f)
            : 0.f;
        const float fadeLength = maximumShadowDistance * fadeoutFraction;
        if (!(fadeLength > 0.f))
            return viewDepth <= maximumShadowDistance ? 1.f : 0.f;
        const float fadeProgress = std::clamp(
            (viewDepth - (maximumShadowDistance - fadeLength)) /
                fadeLength,
            0.f,
            1.f);
        return 1.f - fadeProgress * fadeProgress;
    }

    struct DiagnosticCsmRect
    {
        int32_t minX = 0;
        int32_t maxX = 0;
        int32_t minY = 0;
        int32_t maxY = 0;

        [[nodiscard]] constexpr bool IsValid() const
        {
            return maxX > minX && maxY > minY;
        }

        [[nodiscard]] constexpr uint64_t Area() const
        {
            return IsValid()
                ? uint64_t(maxX - minX) * uint64_t(maxY - minY)
                : 0u;
        }

        [[nodiscard]] constexpr int32_t Width() const
        {
            return IsValid() ? maxX - minX : 0;
        }

        [[nodiscard]] constexpr int32_t Height() const
        {
            return IsValid() ? maxY - minY : 0;
        }
    };

    [[nodiscard]] constexpr bool DiagnosticCsmRectsOverlap(
        const DiagnosticCsmRect& left,
        const DiagnosticCsmRect& right)
    {
        return left.IsValid() && right.IsValid() &&
            left.minX < right.maxX && left.maxX > right.minX &&
            left.minY < right.maxY && left.maxY > right.minY;
    }

    [[nodiscard]] constexpr bool
        ShouldRenderDiagnosticCsmCasterForUpdateRect(
            bool casterBoundsReliable,
            const DiagnosticCsmRect& casterRectangle,
            const DiagnosticCsmRect& updateRectangle)
    {
        return updateRectangle.IsValid() &&
            (!casterBoundsReliable || DiagnosticCsmRectsOverlap(
                casterRectangle, updateRectangle));
    }

    [[nodiscard]] constexpr DiagnosticCsmRect UnionDiagnosticCsmRects(
        const DiagnosticCsmRect& left,
        const DiagnosticCsmRect& right)
    {
        if (!left.IsValid())
            return right;
        if (!right.IsValid())
            return left;
        return {
            std::min(left.minX, right.minX),
            std::max(left.maxX, right.maxX),
            std::min(left.minY, right.minY),
            std::max(left.maxY, right.maxY)
        };
    }

    [[nodiscard]] constexpr DiagnosticCsmRect ClipDiagnosticCsmRect(
        const DiagnosticCsmRect& rect,
        uint32_t resolution)
    {
        const int32_t limit = resolution > uint32_t(
            std::numeric_limits<int32_t>::max())
            ? std::numeric_limits<int32_t>::max()
            : int32_t(resolution);
        return {
            std::clamp(rect.minX, 0, limit),
            std::clamp(rect.maxX, 0, limit),
            std::clamp(rect.minY, 0, limit),
            std::clamp(rect.maxY, 0, limit)
        };
    }

    [[nodiscard]] inline DiagnosticCsmRect
        MakeClippedDiagnosticCsmRectFromUvBounds(
            float minimumU,
            float maximumU,
            float minimumV,
            float maximumV,
            uint32_t resolution,
            uint32_t halo)
    {
        if (!std::isfinite(minimumU) || !std::isfinite(maximumU) ||
            !std::isfinite(minimumV) || !std::isfinite(maximumV) ||
            maximumU < minimumU || maximumV < minimumV)
        {
            return {};
        }

        const int32_t limit = resolution > uint32_t(
            std::numeric_limits<int32_t>::max())
            ? std::numeric_limits<int32_t>::max()
            : int32_t(resolution);
        const double scale = double(resolution);
        const double haloAsDouble = double(halo);
        const auto clipBeforeCast = [limit](double coordinate)
        {
            return int32_t(std::clamp(
                coordinate,
                0.0,
                double(limit)));
        };
        return {
            clipBeforeCast(std::floor(double(minimumU) * scale) -
                haloAsDouble),
            clipBeforeCast(std::ceil(double(maximumU) * scale) +
                haloAsDouble),
            clipBeforeCast(std::floor(double(minimumV) * scale) -
                haloAsDouble),
            clipBeforeCast(std::ceil(double(maximumV) * scale) +
                haloAsDouble)
        };
    }

    constexpr uint32_t DiagnosticCsmReceiverCornerCount = 8u;

    [[nodiscard]] constexpr DiagnosticCsmRect
        MakeFullDiagnosticCsmRect(uint32_t resolution)
    {
        if (resolution == 0u ||
            resolution >
                uint32_t(std::numeric_limits<int32_t>::max()))
        {
            return {};
        }
        return {
            0,
            int32_t(resolution),
            0,
            int32_t(resolution)
        };
    }

    // Converts all eight receiver-frustum corners after the snapped shadow
    // mapping into an exclusive raster scissor. Failure always restores the
    // full map so malformed camera data can only reduce efficiency.
    [[nodiscard]] inline bool
        TryBuildDiagnosticCsmReceiverRasterScissor(
            const std::array<std::array<float, 2u>,
                DiagnosticCsmReceiverCornerCount>& receiverUv,
            uint32_t resolution,
            uint32_t projectionGuardTexels,
            DiagnosticCsmRect& scissor)
    {
        scissor = MakeFullDiagnosticCsmRect(resolution);
        if (!scissor.IsValid())
            return false;

        float minimumU = std::numeric_limits<float>::infinity();
        float maximumU = -std::numeric_limits<float>::infinity();
        float minimumV = std::numeric_limits<float>::infinity();
        float maximumV = -std::numeric_limits<float>::infinity();
        for (const auto& corner : receiverUv)
        {
            if (!std::isfinite(corner[0]) ||
                !std::isfinite(corner[1]))
            {
                return false;
            }
            minimumU = std::min(minimumU, corner[0]);
            maximumU = std::max(maximumU, corner[0]);
            minimumV = std::min(minimumV, corner[1]);
            maximumV = std::max(maximumV, corner[1]);
        }
        if (!(maximumU > minimumU) || !(maximumV > minimumV))
            return false;

        const DiagnosticCsmRect candidate =
            MakeClippedDiagnosticCsmRectFromUvBounds(
                minimumU,
                maximumU,
                minimumV,
                maximumV,
                resolution,
                projectionGuardTexels);
        if (!candidate.IsValid())
            return false;

        scissor = candidate;
        return true;
    }

    struct DiagnosticCsmScrollRegions
    {
        DiagnosticCsmRect source;
        DiagnosticCsmRect destination;
        std::array<DiagnosticCsmRect, 2u> exposed{};
        uint32_t exposedCount = 0u;
        uint64_t copiedTexels = 0u;
        uint64_t exposedTexels = 0u;
        bool valid = false;
    };

    [[nodiscard]] constexpr std::array<int32_t, 2u>
        ComputeDiagnosticCsmScrollSourceOffset(
            const DiagnosticCsmScrollRegions& regions)
    {
        return regions.valid
            ? std::array<int32_t, 2u>{
                regions.source.minX - regions.destination.minX,
                regions.source.minY - regions.destination.minY }
            : std::array<int32_t, 2u>{ 0, 0 };
    }

    [[nodiscard]] constexpr bool
        CanUseDiagnosticCsmPartialSceneUpdate(
            bool sceneChanged,
            bool sceneCompatible,
            bool dirtyRectanglesEnabled,
            bool requiresFullSceneInvalidation)
    {
        return !sceneChanged ||
            (sceneCompatible &&
                dirtyRectanglesEnabled &&
                !requiresFullSceneInvalidation);
    }

    // destinationShift describes where an old texel lands in the new map:
    // destination = source + destinationShift.
    [[nodiscard]] inline DiagnosticCsmScrollRegions
        ComputeDiagnosticCsmScrollRegions(
            uint32_t resolution,
            int32_t destinationShiftX,
            int32_t destinationShiftY)
    {
        DiagnosticCsmScrollRegions result;
        if (resolution == 0u ||
            resolution > uint32_t(std::numeric_limits<int32_t>::max()))
        {
            return result;
        }

        const int64_t size = int64_t(resolution);
        const int64_t shiftX = int64_t(destinationShiftX);
        const int64_t shiftY = int64_t(destinationShiftY);
        if (std::abs(shiftX) >= size || std::abs(shiftY) >= size)
        {
            return result;
        }

        const auto coordinate = [](int64_t value)
        {
            return int32_t(value);
        };

        result.source = {
            coordinate(std::max(int64_t(0), -shiftX)),
            coordinate(std::min(size, size - shiftX)),
            coordinate(std::max(int64_t(0), -shiftY)),
            coordinate(std::min(size, size - shiftY))
        };
        result.destination = {
            coordinate(int64_t(result.source.minX) + shiftX),
            coordinate(int64_t(result.source.maxX) + shiftX),
            coordinate(int64_t(result.source.minY) + shiftY),
            coordinate(int64_t(result.source.maxY) + shiftY)
        };
        if (!result.source.IsValid() || !result.destination.IsValid())
            return result;

        // A two-rectangle representation is sufficient when the vertical strip
        // spans the full map and the horizontal strip covers only the overlap
        // height. Rebuild it directly to avoid accidental L-shape overlap.
        result.exposedCount = 0u;
        if (destinationShiftY != 0)
        {
            result.exposed[result.exposedCount++] = destinationShiftY > 0
                ? DiagnosticCsmRect{
                    0, coordinate(size), 0, destinationShiftY }
                : DiagnosticCsmRect{
                    0,
                    coordinate(size),
                    coordinate(size + shiftY),
                    coordinate(size) };
        }
        if (destinationShiftX != 0)
        {
            const int32_t y0 = destinationShiftY > 0
                ? destinationShiftY
                : 0;
            const int32_t y1 = destinationShiftY < 0
                ? coordinate(size + shiftY)
                : coordinate(size);
            result.exposed[result.exposedCount++] = destinationShiftX > 0
                ? DiagnosticCsmRect{ 0, destinationShiftX, y0, y1 }
                : DiagnosticCsmRect{
                    coordinate(size + shiftX),
                    coordinate(size),
                    y0,
                    y1 };
        }

        result.copiedTexels = result.source.Area();
        const uint64_t totalTexels = uint64_t(resolution) * resolution;
        result.exposedTexels = totalTexels - result.copiedTexels;
        result.valid = true;
        return result;
    }

    struct DiagnosticCsmProjectionCompatibility
    {
        const void* lightIdentity = nullptr;
        std::array<float, 9u> lightBasis{};
        float radius = 0.f;
        float texelWorldSize = 0.f;
        float snappedCenterX = 0.f;
        float snappedCenterY = 0.f;
        float snappedCenterZ = 0.f;
        float depthNear = 0.f;
        float depthFar = 0.f;
        float splitNear = 0.f;
        float splitFar = 0.f;
        float depthBias = 0.f;
        float slopeScaledDepthBias = 0.f;
        uint32_t resolution = 0u;
        uint32_t formatKey = 0u;
        bool normalDepth = true;
    };

    struct DiagnosticCsmScrollClassification
    {
        bool exactReuse = false;
        bool scrollCompatible = false;
        int32_t destinationShiftX = 0;
        int32_t destinationShiftY = 0;
        float overlap = 0.f;
    };

    [[nodiscard]] inline bool
        IsFiniteDiagnosticCsmProjectionCompatibility(
            const DiagnosticCsmProjectionCompatibility& projection)
    {
        if (!projection.lightIdentity || projection.resolution == 0u ||
            !std::isfinite(projection.radius) ||
            !std::isfinite(projection.texelWorldSize) ||
            !std::isfinite(projection.snappedCenterX) ||
            !std::isfinite(projection.snappedCenterY) ||
            !std::isfinite(projection.snappedCenterZ) ||
            !std::isfinite(projection.depthNear) ||
            !std::isfinite(projection.depthFar) ||
            !std::isfinite(projection.splitNear) ||
            !std::isfinite(projection.splitFar) ||
            !std::isfinite(projection.depthBias) ||
            !std::isfinite(projection.slopeScaledDepthBias) ||
            !(projection.radius > 0.f) ||
            !(projection.texelWorldSize > 0.f) ||
            !(projection.depthFar > projection.depthNear) ||
            !(projection.splitFar >= projection.splitNear) ||
            projection.depthBias < 0.f ||
            projection.slopeScaledDepthBias < 0.f)
        {
            return false;
        }
        return std::all_of(
            projection.lightBasis.begin(),
            projection.lightBasis.end(),
            [](float value) { return std::isfinite(value); });
    }

    [[nodiscard]] inline DiagnosticCsmScrollClassification
        ClassifyDiagnosticCsmProjectionChange(
            const DiagnosticCsmProjectionCompatibility& previous,
            const DiagnosticCsmProjectionCompatibility& current,
            float minimumOverlap)
    {
        DiagnosticCsmScrollClassification result;
        const bool fixedFieldsMatch =
            IsFiniteDiagnosticCsmProjectionCompatibility(previous) &&
            IsFiniteDiagnosticCsmProjectionCompatibility(current) &&
            previous.lightIdentity == current.lightIdentity &&
            previous.lightBasis == current.lightBasis &&
            previous.radius == current.radius &&
            previous.texelWorldSize == current.texelWorldSize &&
            previous.snappedCenterZ == current.snappedCenterZ &&
            previous.depthNear == current.depthNear &&
            previous.depthFar == current.depthFar &&
            previous.splitNear == current.splitNear &&
            previous.splitFar == current.splitFar &&
            previous.depthBias == current.depthBias &&
            previous.slopeScaledDepthBias ==
                current.slopeScaledDepthBias &&
            previous.resolution == current.resolution &&
            previous.formatKey == current.formatKey &&
            previous.normalDepth == current.normalDepth;
        if (!fixedFieldsMatch)
            return result;

        const double deltaX =
            (double(current.snappedCenterX) -
                double(previous.snappedCenterX)) /
            double(current.texelWorldSize);
        const double deltaY =
            (double(current.snappedCenterY) -
                double(previous.snappedCenterY)) /
            double(current.texelWorldSize);
        if (!std::isfinite(deltaX) || !std::isfinite(deltaY))
            return result;

        const double roundedX = std::round(deltaX);
        const double roundedY = std::round(deltaY);
        constexpr double IntegerTolerance = 1e-4;
        if (std::abs(deltaX - roundedX) > IntegerTolerance ||
            std::abs(deltaY - roundedY) > IntegerTolerance ||
            std::abs(roundedX) >= double(current.resolution) ||
            std::abs(roundedY) >= double(current.resolution) ||
            std::abs(roundedX) >
                double(std::numeric_limits<int32_t>::max()) ||
            std::abs(roundedY) >
                double(std::numeric_limits<int32_t>::max()))
        {
            return result;
        }

        // Moving the snapped projection center right moves unchanged world
        // texels left in the destination map. MakeWorldToUvzw flips clip Y,
        // so moving the light-space center up moves unchanged UV texels down.
        result.destinationShiftX = -int32_t(roundedX);
        result.destinationShiftY = int32_t(roundedY);
        if (result.destinationShiftX == 0 &&
            result.destinationShiftY == 0)
        {
            result.exactReuse = true;
            result.overlap = 1.f;
            return result;
        }

        const DiagnosticCsmScrollRegions regions =
            ComputeDiagnosticCsmScrollRegions(
                current.resolution,
                result.destinationShiftX,
                result.destinationShiftY);
        if (!regions.valid)
            return result;

        result.overlap = float(regions.copiedTexels) /
            float(uint64_t(current.resolution) * current.resolution);
        minimumOverlap = std::isfinite(minimumOverlap)
            ? std::clamp(minimumOverlap, 0.f, 1.f)
            : 1.f;
        result.scrollCompatible = result.overlap >= minimumOverlap;
        return result;
    }
}
