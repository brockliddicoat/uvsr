#pragma once

#include "diagnostic_cascaded_shadow_map.h"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>

namespace uvsr
{
    [[nodiscard]] inline bool IsValidDiagnosticCsmBenchmarkTiming(
        const DiagnosticCsmTimings& timings)
    {
        const auto finiteNonnegative = [](float value) {
            return std::isfinite(value) && value >= 0.f;
        };
        if (!timings.supported ||
            !timings.active ||
            timings.gpuTimingSource !=
                DiagnosticCsmGpuTimingSource::TimerQuery ||
            !std::isfinite(timings.totalMilliseconds) ||
            !(timings.totalMilliseconds > 0.f) ||
            !finiteNonnegative(timings.setupCpuMilliseconds) ||
            !finiteNonnegative(timings.cullingCpuMilliseconds) ||
            !finiteNonnegative(timings.recordingCpuMilliseconds) ||
            !finiteNonnegative(timings.totalCpuMilliseconds))
        {
            return false;
        }

        return !timings.detailedGpuTimingEnabled ||
            (finiteNonnegative(timings.cullingGpuMilliseconds) &&
                finiteNonnegative(timings.clearUpdateMilliseconds) &&
                finiteNonnegative(timings.rasterMilliseconds) &&
                finiteNonnegative(timings.samplingMilliseconds));
    }

    [[nodiscard]] inline std::string
        BuildDiagnosticCsmTimingConfigurationIdentity(
            const DiagnosticCascadedShadowMapSettings& settings)
    {
        std::ostringstream identity;
        identity.imbue(std::locale::classic());
        identity << std::setprecision(
            std::numeric_limits<float>::max_digits10)
            << "diagnostic-csm-timing-v2"
            << ";cascadeCount=" << settings.cascadeCount
            << ";shadowMapResolution=" << settings.shadowMapResolution
            << ";maximumShadowDistance="
            << settings.maximumShadowDistance
            << ";maximumLightDepth=" << settings.maximumLightDepth
            << ";cascadeDistributionExponent="
            << settings.cascadeDistributionExponent
            << ";cascadeTransitionFraction="
            << settings.cascadeTransitionFraction
            << ";shadowDistanceFadeoutFraction="
            << settings.shadowDistanceFadeoutFraction
            << ";projectionSnapTexelMultiple="
            << settings.projectionSnapTexelMultiple
            << ";enforceUeMinimumLightDepth="
            << uint32_t(settings.enforceUeMinimumLightDepth)
            << ";depthBias=" << settings.depthBias
            << ";slopeScaledDepthBias=" << settings.slopeScaledDepthBias
            << ";directionalLightShadowBias="
            << settings.directionalLightShadowBias
            << ";directionalLightShadowSlopeBias="
            << settings.directionalLightShadowSlopeBias
            << ";receiverDepthBias=" << settings.receiverDepthBias
            << ";filter=" << uint32_t(settings.filter)
            << ";poissonTapCount=" << settings.poissonTapCount
            << ";filterRadiusTexels=" << settings.filterRadiusTexels
            << ";use16BitDepthEnabled="
            << uint32_t(settings.use16BitDepthEnabled)
            << ";opaqueDepthStateMergingEnabled="
            << uint32_t(settings.opaqueDepthStateMergingEnabled)
            << ";positionOnlyOpaqueEnabled="
            << uint32_t(settings.positionOnlyOpaqueEnabled)
            << ";translationOnlyCasterTransformEnabled="
            << uint32_t(
                settings.translationOnlyCasterTransformEnabled)
            << ";inputAssemblerCasterFetchEnabled="
            << uint32_t(settings.inputAssemblerCasterFetchEnabled)
            << ";precomputedDepthAxisInverseLengthEnabled="
            << uint32_t(
                settings.precomputedDepthAxisInverseLengthEnabled)
            << ";conservativeSaturatedSlopeEnabled="
            << uint32_t(settings.conservativeSaturatedSlopeEnabled)
            << ";algebraicSlowSlopeEnabled="
            << uint32_t(settings.algebraicSlowSlopeEnabled)
            << ";preNormalizedReceiverLightDirectionEnabled="
            << uint32_t(
                settings.preNormalizedReceiverLightDirectionEnabled)
            << ";precomposedClipToShadowEnabled="
            << uint32_t(settings.precomposedClipToShadowEnabled)
            << ";accurateCasterCullingEnabled="
            << uint32_t(settings.accurateCasterCullingEnabled)
            << ";ueCasterRadiusThresholdEnabled="
            << uint32_t(settings.ueCasterRadiusThresholdEnabled)
            << ";casterRadiusThreshold="
            << settings.casterRadiusThreshold
            << ";singleTraversalCasterClassificationEnabled="
            << uint32_t(
                settings.singleTraversalCasterClassificationEnabled)
            << ";precomputedReceiverHullAxesEnabled="
            << uint32_t(settings.precomputedReceiverHullAxesEnabled)
            << ";sharedCasterLightProjectionEnabled="
            << uint32_t(settings.sharedCasterLightProjectionEnabled)
            << ";directCasterSubmissionEnabled="
            << uint32_t(settings.directCasterSubmissionEnabled)
            << ";cachedShadowDrawListsEnabled="
            << uint32_t(settings.cachedShadowDrawListsEnabled)
            << ";batchedFullRedrawClearEnabled="
            << uint32_t(settings.batchedFullRedrawClearEnabled)
            << ";receiverRasterScissorEnabled="
            << uint32_t(settings.receiverRasterScissorEnabled)
            << ";wholeMapReuseEnabled="
            << uint32_t(settings.wholeMapReuseEnabled)
            << ";wholeCascadeReuseEnabled="
            << uint32_t(settings.wholeCascadeReuseEnabled)
            << ";dirtyRectanglesEnabled="
            << uint32_t(settings.dirtyRectanglesEnabled)
            << ";scrollingEnabled="
            << uint32_t(settings.scrollingEnabled)
            << ";minimumScrollOverlap=" << settings.minimumScrollOverlap
            << ";detailedGpuTimingEnabled="
            << uint32_t(settings.detailedGpuTimingEnabled)
            << ";debugView=" << uint32_t(settings.debugView);
        return identity.str();
    }

    [[nodiscard]] inline uint64_t
        HashDiagnosticCsmTimingConfigurationIdentity(
            std::string_view identity)
    {
        uint64_t hash = 14695981039346656037ull;
        for (const unsigned char value : identity)
        {
            hash ^= uint64_t(value);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    [[nodiscard]] inline std::string
        BuildDiagnosticCsmTimingConfigurationId(
            const DiagnosticCascadedShadowMapSettings& settings)
    {
        const std::string identity =
            BuildDiagnosticCsmTimingConfigurationIdentity(settings);
        std::ostringstream result;
        result.imbue(std::locale::classic());
        result << std::hex << std::uppercase << std::setfill('0')
            << std::setw(16)
            << HashDiagnosticCsmTimingConfigurationIdentity(identity);
        return result.str();
    }
}
