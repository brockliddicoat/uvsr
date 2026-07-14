#ifndef UVSR_LIGHTING_CONTRIBUTION_HLSLI
#define UVSR_LIGHTING_CONTRIBUTION_HLSLI

#include "lighting_contribution_shared.h"

// Shared contribution gate for UVSR lighting shaders. Source-activity signals
// are positive and composable: a scope may be skipped only when every relevant
// source is known inactive. Unknown data therefore remains conservatively
// active, while future scene, material, visibility, clustering, or residency
// systems can contribute tighter known-inactive masks without coupling passes.
static const uint LightingSource_Direct = UVSR_LIGHTING_SOURCE_DIRECT;
static const uint LightingSource_Emissive = UVSR_LIGHTING_SOURCE_EMISSIVE;
static const uint LightingSource_Environment = UVSR_LIGHTING_SOURCE_ENVIRONMENT;
static const uint LightingSource_IndirectDiffuse =
    UVSR_LIGHTING_SOURCE_INDIRECT_DIFFUSE;
static const uint LightingSource_IndirectSpecular =
    UVSR_LIGHTING_SOURCE_INDIRECT_SPECULAR;
static const uint LightingSource_All = UVSR_LIGHTING_SOURCE_ALL;

// Local hard-rejection reasons are diagnostic labels. Any one is sufficient
// to reject the operation in which it was produced; they must not be reused as
// global source-availability flags because another source may still contribute.
static const uint LightingRejection_None = 0u;
static const uint LightingRejection_ZeroSignal = 1u << 0u;
static const uint LightingRejection_BelowThreshold = 1u << 1u;
static const uint LightingRejection_NonFinite = 1u << 2u;
static const uint LightingRejection_BackFacing = 1u << 3u;
static const uint LightingRejection_ZeroVisibility = 1u << 4u;
static const uint LightingRejection_OutsideInfluence = 1u << 5u;
static const uint LightingRejection_Material = 1u << 6u;

struct LightingContributionGate
{
    // Default zero means no source has been proven inactive.
    uint knownInactiveSources;
    // Cutoff is measured after exposure but before tone mapping. Zero enables
    // exact-zero rejection only.
    float minimumContribution;
    float exposureScale;
};

LightingContributionGate MakeLightingContributionGate(
    uint knownInactiveSources,
    float minimumContribution,
    float exposureScale)
{
    LightingContributionGate gate;
    gate.knownInactiveSources = knownInactiveSources & LightingSource_All;
    gate.minimumContribution = max(minimumContribution, 0.0f);
    gate.exposureScale = max(exposureScale, 0.0f);
    return gate;
}

float LightingPeakSignal(float3 signal)
{
    signal = max(signal, 0.0f);
    return max(signal.r, max(signal.g, signal.b));
}

bool LightingHasPotentialSource(
    LightingContributionGate gate,
    uint relevantSources)
{
    relevantSources &= LightingSource_All;
    return relevantSources != 0u &&
        (gate.knownInactiveSources & relevantSources) != relevantSources;
}

uint LightingClassifyContribution(
    LightingContributionGate gate,
    uint relevantSources,
    float3 signal,
    float throughputUpperBound)
{
    if (!LightingHasPotentialSource(gate, relevantSources))
        return LightingRejection_ZeroSignal;
    if (!all(isfinite(signal)) || !isfinite(throughputUpperBound))
        return LightingRejection_NonFinite;

    float peakContribution = LightingPeakSignal(signal) *
        max(throughputUpperBound, 0.0f) * gate.exposureScale;
    if (!(peakContribution > 0.0f))
        return LightingRejection_ZeroSignal;
    if (peakContribution <= gate.minimumContribution)
        return LightingRejection_BelowThreshold;
    return LightingRejection_None;
}

uint LightingRejectIf(bool reject, uint reason)
{
    return reject ? reason : LightingRejection_None;
}

bool LightingShouldEvaluate(uint localRejectionReasons)
{
    return localRejectionReasons == LightingRejection_None;
}

#endif // UVSR_LIGHTING_CONTRIBUTION_HLSLI
