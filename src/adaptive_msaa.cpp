#include "adaptive_msaa.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace
{
    using namespace uvsr::adaptive_msaa;

    bool IsFiniteHitData(const SampleDescriptor& sample)
    {
        return std::isfinite(sample.linearDepth) &&
            std::isfinite(sample.worldPosition.x) &&
            std::isfinite(sample.worldPosition.y) &&
            std::isfinite(sample.worldPosition.z) &&
            std::isfinite(sample.pixelFootprint) &&
            sample.pixelFootprint >= 0.f &&
            std::isfinite(sample.barycentrics[0]) &&
            std::isfinite(sample.barycentrics[1]);
    }

    uint32_t CountSetBits(uint32_t value)
    {
        uint32_t count = 0u;
        while (value != 0u)
        {
            count += value & 1u;
            value >>= 1u;
        }
        return count;
    }

    void InitializeSampleAssignments(SampleGroupingResult& result)
    {
        result.sampleToGroup.fill(BackgroundSampleAssignment);
    }

    void FinalizeWeights(SampleGroupingResult& result)
    {
        if (result.sampleCount == 0u)
            return;

        const float inverseSampleCount =
            1.f / float(result.sampleCount);
        result.backgroundWeight =
            float(result.backgroundSampleCount) * inverseSampleCount;
        result.totalWeight = result.backgroundWeight;
        for (uint32_t groupIndex = 0u;
            groupIndex < result.groupCount;
            ++groupIndex)
        {
            SampleGroup& group = result.groups[groupIndex];
            group.sampleCount = CountSetBits(group.sampleMask);
            group.weight =
                float(group.sampleCount) * inverseSampleCount;
            result.totalWeight += group.weight;
        }
        result.weightConserved = std::isfinite(result.totalWeight) &&
            std::abs(result.totalWeight - 1.f) <= GroupWeightTolerance;
    }

    void AddFallbackSingleton(
        SampleGroupingResult& result,
        const SampleDescriptor& sample,
        uint32_t sampleIndex,
        FallbackReason reason)
    {
        SampleGroup& group = result.groups[result.groupCount];
        group.packedKey = sample.packedKey;
        group.representativeSample = sampleIndex;
        group.sampleMask = 1u << sampleIndex;
        group.sampleCount = 1u;
        group.referenceFallback = true;
        group.fallbackReason = reason;
        result.sampleToGroup[sampleIndex] =
            static_cast<uint8_t>(result.groupCount);
        ++result.groupCount;
        result.hasReferenceFallback = true;
    }

    FallbackReason ValidateIndividualSample(
        const SampleDescriptor& sample);

    void BuildFullReferenceFallback(
        SampleGroupingResult& result,
        const std::array<SampleDescriptor, MaximumAdaptiveSampleCount>& samples,
        uint32_t sampleCount,
        FallbackReason reason,
        uint32_t loadedSampleCount)
    {
        result = {};
        InitializeSampleAssignments(result);
        result.requestedSampleCount = sampleCount;
        result.sampleCount = sampleCount;
        result.loadedSampleCount = loadedSampleCount;
        result.reason = reason;
        result.requiresFullReference = true;

        for (uint32_t sampleIndex = 0u;
            sampleIndex < sampleCount;
            ++sampleIndex)
        {
            if (!samples[sampleIndex].covered)
            {
                ++result.backgroundSampleCount;
                continue;
            }
            const FallbackReason individualReason =
                ValidateIndividualSample(samples[sampleIndex]);
            AddFallbackSingleton(
                result,
                samples[sampleIndex],
                sampleIndex,
                individualReason == FallbackReason::None
                    ? reason
                    : individualReason);
        }
        FinalizeWeights(result);
    }

    FallbackReason ValidateIndividualSample(
        const SampleDescriptor& sample)
    {
        if (sample.validationReason != FallbackReason::None)
            return sample.validationReason;

        const FallbackReason keyReason =
            ValidateGpuVisibilityKey(sample.packedKey);
        if (keyReason != FallbackReason::None)
            return keyReason;
        if (!sample.reconstructionValid || !IsFiniteHitData(sample))
            return FallbackReason::ReconstructionFailure;
        if (sample.sampleDependentBlocker)
            return FallbackReason::UnsafeSharing;
        return FallbackReason::None;
    }

    SampleDescriptor LoadDescriptorArray(
        void* context,
        uint32_t sampleIndex)
    {
        return static_cast<const SampleDescriptor*>(context)[sampleIndex];
    }

    SurfaceMappingResult ResolveMappedSurfaceHit(
        const SurfaceRegistry& registry,
        uint32_t geometryInstanceHandle,
        uint64_t primitiveIndex,
        const std::array<float, 2>& barycentrics,
        bool frontFace,
        const RegistryValidation& validation)
    {
        SurfaceMappingResult result;
        const GpuVisibilityKeyResult packed = MakeGpuVisibilityKey(
            geometryInstanceHandle,
            primitiveIndex);
        result.packedKey = packed.key;
        if (!packed.Succeeded())
        {
            result.reason = packed.reason;
            return result;
        }

        const SurfaceResolutionResult resolved =
            registry.ResolveSurfaceKey(packed.key, validation);
        if (!resolved.Succeeded())
        {
            result.reason = resolved.reason;
            return result;
        }

        result.hit.key = resolved.key;
        result.hit.barycentrics = barycentrics;
        result.hit.frontFace = frontFace;
        return result;
    }
}

namespace uvsr::adaptive_msaa
{
    namespace
    {
        InvalidationIdentityAuthority g_ProcessInvalidationIdentityAuthority;
    }

    const char* GetFallbackReasonName(FallbackReason reason)
    {
        switch (reason)
        {
        case FallbackReason::None: return "None";
        case FallbackReason::InvalidSentinel: return "Invalid Sentinel";
        case FallbackReason::InvalidHandleEncoding:
            return "Invalid Handle Encoding";
        case FallbackReason::SlotOutOfRange: return "Slot Out of Range";
        case FallbackReason::GenerationMismatch:
            return "Generation Mismatch";
        case FallbackReason::TopologyEpochMismatch:
            return "Topology Epoch Mismatch";
        case FallbackReason::LodRevisionMismatch:
            return "LOD Revision Mismatch";
        case FallbackReason::DeformationRevisionMismatch:
            return "Deformation Revision Mismatch";
        case FallbackReason::MaterialRevisionMismatch:
            return "Material Revision Mismatch";
        case FallbackReason::CapacityOverflow: return "Capacity Overflow";
        case FallbackReason::PrimitiveOverflow: return "Primitive Overflow";
        case FallbackReason::GenerationExhausted:
            return "Generation Exhausted";
        case FallbackReason::RegistryResetRequired:
            return "Registry Reset Required";
        case FallbackReason::TransientMappingCollision:
            return "Transient Mapping Collision";
        case FallbackReason::TransientMappingFailure:
            return "Transient Mapping Failure";
        case FallbackReason::UnsupportedDeformation:
            return "Unsupported Deformation";
        case FallbackReason::UnsupportedMaterial:
            return "Unsupported Material";
        case FallbackReason::UnsafeSharing: return "Unsafe Sharing";
        case FallbackReason::ReconstructionFailure:
            return "Reconstruction Failure";
        case FallbackReason::FormatUnsupported:
            return "Format Unsupported";
        case FallbackReason::SampleCountUnsupported:
            return "Sample Count Unsupported";
        case FallbackReason::WorkListOverflow:
            return "Work-List Overflow";
        case FallbackReason::TextureContentRevisionMismatch:
            return "Texture Content Revision Mismatch";
        case FallbackReason::TextureQualificationMismatch:
            return "Texture Qualification Mismatch";
        case FallbackReason::Count: return "Count";
        }
        return "Unknown";
    }

    bool TryAdvanceInvalidationIdentity(uint32_t& value)
    {
        if (value == std::numeric_limits<uint32_t>::max())
            return false;
        ++value;
        return value != 0u;
    }

    InvalidationIdentityAuthority::InvalidationIdentityAuthority(
        uint64_t lastTopologyEpoch)
        : m_LastTopologyEpoch(lastTopologyEpoch)
    {
    }

    TopologyEpochAdvanceResult
        InvalidationIdentityAuthority::AcquireTopologyEpoch()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        TopologyEpochAdvanceResult result;
        if (m_Exhausted ||
            m_LastTopologyEpoch == std::numeric_limits<uint64_t>::max())
        {
            m_Exhausted = true;
            result.reason = FallbackReason::RegistryResetRequired;
            return result;
        }
        ++m_LastTopologyEpoch;
        result.topologyEpoch = m_LastTopologyEpoch;
        return result;
    }

    TopologyIdentityAdvanceResult
        InvalidationIdentityAuthority::AcquireTopologyIdentity(
            uint32_t& topologyRevision)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        TopologyIdentityAdvanceResult result;
        if (m_Exhausted ||
            m_LastTopologyEpoch == std::numeric_limits<uint64_t>::max() ||
            topologyRevision == std::numeric_limits<uint32_t>::max())
        {
            m_Exhausted = true;
            result.reason = FallbackReason::RegistryResetRequired;
            return result;
        }
        ++m_LastTopologyEpoch;
        ++topologyRevision;
        result.topologyEpoch = m_LastTopologyEpoch;
        result.topologyRevision = topologyRevision;
        return result;
    }

    void InvalidationIdentityAuthority::LatchExhaustion()
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Exhausted = true;
    }

    bool InvalidationIdentityAuthority::IsExhausted() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_Exhausted;
    }

    uint64_t InvalidationIdentityAuthority::LastTopologyEpoch() const
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_LastTopologyEpoch;
    }

    TopologyEpochAdvanceResult AcquireProcessTopologyEpoch()
    {
        return g_ProcessInvalidationIdentityAuthority.AcquireTopologyEpoch();
    }

    TopologyIdentityAdvanceResult AcquireProcessTopologyIdentity(
        uint32_t& topologyRevision)
    {
        return g_ProcessInvalidationIdentityAuthority.
            AcquireTopologyIdentity(topologyRevision);
    }

    void LatchProcessInvalidationIdentityExhaustion()
    {
        g_ProcessInvalidationIdentityAuthority.LatchExhaustion();
    }

    bool ProcessInvalidationIdentityExhausted()
    {
        return g_ProcessInvalidationIdentityAuthority.IsExhausted();
    }

    ComposedCapabilityResult ResolveComposedAdaptiveMsaaCapability(
        const ComposedCapabilityRequest& request)
    {
        ComposedCapabilityResult result;
        result.requestedSampleCount = request.requestedSampleCount;
        result.resolvedSampleCount = request.resolvedSampleCount;
        result.renderTargetSampleDowngraded =
            request.resolvedSampleCount < request.requestedSampleCount;

        const bool validRequestedImplementation =
            request.requestedImplementation ==
                AdaptiveMsaaImplementation::Reference ||
            request.requestedImplementation ==
                AdaptiveMsaaImplementation::AdaptiveGBuffer ||
            request.requestedImplementation ==
                AdaptiveMsaaImplementation::CompactVisibility;
        if (!validRequestedImplementation ||
            request.requestedImplementation ==
                AdaptiveMsaaImplementation::Reference)
        {
            return result;
        }
        if (request.resolvedSampleCount <= 1u)
        {
            result.reason = ComposedCapabilityReason::ResolvedSingleSample;
            return result;
        }
        if (!request.surfaceKeySupported)
        {
            result.reason =
                ComposedCapabilityReason::SurfaceKeyUnsupported;
            return result;
        }
        if (request.requestedImplementation ==
                AdaptiveMsaaImplementation::CompactVisibility &&
            !request.compactDepthSupported)
        {
            result.reason =
                ComposedCapabilityReason::CompactDepthUnsupported;
            return result;
        }
        if (request.requestedImplementation ==
                AdaptiveMsaaImplementation::CompactVisibility &&
            !request.visibilityClassSupported)
        {
            result.reason =
                ComposedCapabilityReason::VisibilityClassUnsupported;
            return result;
        }

        result.implementation = request.requestedImplementation;
        result.reason = result.renderTargetSampleDowngraded
            ? ComposedCapabilityReason::RenderTargetSampleDowngrade
            : ComposedCapabilityReason::None;
        return result;
    }

    const char* GetComposedCapabilityReasonName(
        ComposedCapabilityReason reason)
    {
        switch (reason)
        {
        case ComposedCapabilityReason::None: return "none";
        case ComposedCapabilityReason::RenderTargetSampleDowngrade:
            return "render-target-sample-downgrade";
        case ComposedCapabilityReason::ResolvedSingleSample:
            return "resolved-single-sample";
        case ComposedCapabilityReason::SurfaceKeyUnsupported:
            return "surface-key-format-unsupported";
        case ComposedCapabilityReason::CompactDepthUnsupported:
            return "compact-depth-format-unsupported";
        case ComposedCapabilityReason::VisibilityClassUnsupported:
            return "visibility-class-format-unsupported";
        }
        return "unknown";
    }

    /*
     * Grouping Provenance
     * -------------------
     * The deterministic lowest-sample representative, exact per-pixel
     * visibility grouping, groupCount/sampleCount weight, and redistribution
     * shape are adapted conceptually from Christopher A. Burns and Warren A.
     * Hunt, "The Visibility Buffer: A Cache-Friendly Approach to Deferred
     * Shading," JCGT 2(2), 2013, official supplemental code.zip:
     * https://jcgt.org/published/0002/02/04/code.zip
     * SHA-256:
     * 6C9A2410ED37270B6CB5F8FED34CE5A330A229ECD426C16E573646FBBE0C76E2
     * See DeferredPass.hlsl lines 424-508. The source is Copyright (c) 2013
     * Chris Burns & Warren Hunt and distributed under its embedded BSD
     * three-clause-style notice. This is new C++ code, not copied HLSL.
     *
     * The depth-classified sample-frequency flow was also audited from The
     * Forge Visibility_Buffer at commit
     * a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0), especially
     * MsaaEdgeDetect.frag.fsl. Its mean-depth classifier is intentionally not
     * reproduced: it cannot prove final surface ownership. UVSR instead keeps
     * exact ownership and the frozen conservative pairwise sharing predicate.
     */

    HandlePackResult PackGeometryInstanceHandle(
        uint32_t slot,
        uint32_t generation)
    {
        HandlePackResult result;
        if (slot >= GeometryInstanceSlotCount)
        {
            result.reason = FallbackReason::SlotOutOfRange;
            return result;
        }
        if (generation < MinimumGeometryInstanceGeneration ||
            generation > MaximumGeometryInstanceGeneration)
        {
            result.reason = FallbackReason::InvalidHandleEncoding;
            return result;
        }

        result.handle =
            (generation << GeometryInstanceSlotBits) | slot;
        return result;
    }

    DecodedGeometryInstanceHandle DecodeGeometryInstanceHandle(
        uint32_t handle)
    {
        DecodedGeometryInstanceHandle result;
        if (handle == InvalidVisibilityWord)
        {
            result.reason = FallbackReason::InvalidSentinel;
            return result;
        }

        result.slot = handle & GeometryInstanceSlotMask;
        result.generation =
            (handle >> GeometryInstanceSlotBits) &
            GeometryInstanceGenerationMask;
        if (result.generation < MinimumGeometryInstanceGeneration ||
            result.generation > MaximumGeometryInstanceGeneration)
        {
            result.reason = FallbackReason::InvalidHandleEncoding;
        }
        return result;
    }

    GpuVisibilityKeyResult MakeGpuVisibilityKey(
        uint32_t geometryInstanceHandle,
        uint64_t geometryLocalPrimitiveIndex)
    {
        GpuVisibilityKeyResult result;
        const DecodedGeometryInstanceHandle decoded =
            DecodeGeometryInstanceHandle(geometryInstanceHandle);
        if (!decoded.Succeeded())
        {
            result.reason = decoded.reason;
            return result;
        }
        if (geometryLocalPrimitiveIndex >= InvalidVisibilityWord)
        {
            result.reason = FallbackReason::PrimitiveOverflow;
            return result;
        }

        result.key.geometryInstanceHandle = geometryInstanceHandle;
        result.key.geometryLocalPrimitiveIndex =
            static_cast<uint32_t>(geometryLocalPrimitiveIndex);
        return result;
    }

    FallbackReason ValidateGpuVisibilityKey(
        const GpuVisibilityKey& key)
    {
        if (key.geometryInstanceHandle == InvalidVisibilityWord ||
            key.geometryLocalPrimitiveIndex == InvalidVisibilityWord)
        {
            return FallbackReason::InvalidSentinel;
        }
        return DecodeGeometryInstanceHandle(
            key.geometryInstanceHandle).reason;
    }

    bool SameSurface(
        const SurfaceKey& first,
        const SurfaceKey& second)
    {
        return first.stableInstanceHandle == second.stableInstanceHandle &&
            first.geometryIndex == second.geometryIndex &&
            first.geometryLocalPrimitiveIndex ==
                second.geometryLocalPrimitiveIndex;
    }

    size_t SurfaceRegistry::RegistryIdentityHash::operator()(
        const RegistryIdentity& identity) const
    {
        const size_t first = std::hash<uint64_t>{}(
            identity.stableInstanceHandle);
        const size_t second = std::hash<uint32_t>{}(
            identity.geometryIndex);
        return first ^ (second + size_t(0x9e3779b9u) +
            (first << 6u) + (first >> 2u));
    }

    bool SameSurface(
        const SurfaceHit& first,
        const SurfaceHit& second)
    {
        return SameSurface(first.key, second.key);
    }

    bool SameSurface(
        const GpuVisibilityKey& first,
        const GpuVisibilityKey& second)
    {
        return ValidateGpuVisibilityKey(first) == FallbackReason::None &&
            ValidateGpuVisibilityKey(second) == FallbackReason::None &&
            first.geometryInstanceHandle ==
                second.geometryInstanceHandle &&
            first.geometryLocalPrimitiveIndex ==
                second.geometryLocalPrimitiveIndex;
    }

    SurfaceRegistry::SurfaceRegistry(
        uint32_t capacity,
        uint64_t topologyEpoch)
        : m_TopologyEpoch(topologyEpoch)
    {
        if (capacity > GeometryInstanceSlotCount)
        {
            m_ConfigurationReason = FallbackReason::CapacityOverflow;
            return;
        }
        m_Slots.resize(capacity);
    }

    RegistryRegistrationResult SurfaceRegistry::Register(
        const RegistryEntryDesc& entry)
    {
        RegistryRegistrationResult result;
        if (m_ConfigurationReason != FallbackReason::None)
        {
            result.reason = m_ConfigurationReason;
            return result;
        }
        if (entry.topologyEpoch != m_TopologyEpoch)
        {
            result.reason = FallbackReason::TopologyEpochMismatch;
            return result;
        }

        // One active GPU handle must name one logical instance/geometry pair.
        // Exact duplicate registration is idempotent; changed identity metadata
        // must pass through explicit invalidation/update APIs.
        const RegistryIdentity identity{
            entry.stableInstanceHandle,
            entry.geometryIndex };
        const auto active = m_IdentityToSlot.find(identity);
        if (active != m_IdentityToSlot.end())
        {
            const uint32_t slotIndex = active->second;
            const Slot& slot = m_Slots[slotIndex];

            if (slot.entry.topologyEpoch != entry.topologyEpoch)
                result.reason = FallbackReason::TopologyEpochMismatch;
            else if (slot.entry.lodRevision != entry.lodRevision)
                result.reason = FallbackReason::LodRevisionMismatch;
            else if (slot.entry.deformationRevision !=
                entry.deformationRevision)
            {
                result.reason =
                    FallbackReason::DeformationRevisionMismatch;
            }
            else if (slot.entry.materialRevision != entry.materialRevision)
                result.reason = FallbackReason::MaterialRevisionMismatch;
            else if (slot.entry.deformationSupported !=
                entry.deformationSupported)
            {
                result.reason = FallbackReason::UnsupportedDeformation;
            }
            else if (slot.entry.materialSupported != entry.materialSupported)
                result.reason = FallbackReason::UnsupportedMaterial;
            else
            {
                const HandlePackResult packed =
                    PackGeometryInstanceHandle(
                        slotIndex,
                        slot.generation);
                result.geometryInstanceHandle = packed.handle;
                result.reason = packed.reason;
            }
            return result;
        }

        if (m_RequiresEpochReset)
        {
            result.reason = FallbackReason::RegistryResetRequired;
            return result;
        }

        uint32_t slotIndex = uint32_t(m_Slots.size());
        if (!m_RecycledSlots.empty())
        {
            slotIndex = m_RecycledSlots.top();
            m_RecycledSlots.pop();
        }
        else if (m_NextUnusedSlot < m_Slots.size())
            slotIndex = m_NextUnusedSlot++;

        if (slotIndex >= m_Slots.size())
        {
            result.reason = FallbackReason::CapacityOverflow;
            return result;
        }

        Slot& slot = m_Slots[slotIndex];
        if (slot.occupied || slot.exhausted)
        {
            result.reason = FallbackReason::RegistryResetRequired;
            return result;
        }
        const HandlePackResult packed = PackGeometryInstanceHandle(
            slotIndex,
            slot.generation);
        if (!packed.Succeeded())
        {
            result.reason = packed.reason;
            return result;
        }

        slot.entry = entry;
        slot.occupied = true;
        m_IdentityToSlot.emplace(identity, slotIndex);
        ++m_ActiveEntryCount;
        result.geometryInstanceHandle = packed.handle;
        return result;
    }

    FallbackReason SurfaceRegistry::InvalidateIdentity(
        uint32_t geometryInstanceHandle)
    {
        const DecodedGeometryInstanceHandle decoded =
            DecodeGeometryInstanceHandle(geometryInstanceHandle);
        if (!decoded.Succeeded())
            return decoded.reason;
        if (decoded.slot >= m_Slots.size())
            return FallbackReason::SlotOutOfRange;

        Slot& slot = m_Slots[decoded.slot];
        if (slot.generation != decoded.generation || !slot.occupied)
            return FallbackReason::GenerationMismatch;

        m_IdentityToSlot.erase(RegistryIdentity{
            slot.entry.stableInstanceHandle,
            slot.entry.geometryIndex });
        slot.occupied = false;
        slot.entry = {};
        --m_ActiveEntryCount;
        if (slot.generation == MaximumGeometryInstanceGeneration)
        {
            slot.exhausted = true;
            m_RequiresEpochReset = true;
            return FallbackReason::GenerationExhausted;
        }

        ++slot.generation;
        m_RecycledSlots.push(decoded.slot);
        return FallbackReason::None;
    }

    FallbackReason SurfaceRegistry::UpdateNonIdentityMetadata(
        uint32_t geometryInstanceHandle,
        uint32_t deformationRevision,
        uint32_t materialRevision,
        bool deformationSupported,
        bool materialSupported)
    {
        const DecodedGeometryInstanceHandle decoded =
            DecodeGeometryInstanceHandle(geometryInstanceHandle);
        if (!decoded.Succeeded())
            return decoded.reason;
        if (decoded.slot >= m_Slots.size())
            return FallbackReason::SlotOutOfRange;

        Slot& slot = m_Slots[decoded.slot];
        if (slot.generation != decoded.generation || !slot.occupied)
            return FallbackReason::GenerationMismatch;

        slot.entry.deformationRevision = deformationRevision;
        slot.entry.materialRevision = materialRevision;
        slot.entry.deformationSupported = deformationSupported;
        slot.entry.materialSupported = materialSupported;
        return FallbackReason::None;
    }

    RegistryLookupResult SurfaceRegistry::Inspect(
        uint32_t geometryInstanceHandle) const
    {
        RegistryLookupResult result;
        if (m_ConfigurationReason != FallbackReason::None)
        {
            result.reason = m_ConfigurationReason;
            return result;
        }

        const DecodedGeometryInstanceHandle decoded =
            DecodeGeometryInstanceHandle(geometryInstanceHandle);
        result.slot = decoded.slot;
        result.generation = decoded.generation;
        if (!decoded.Succeeded())
        {
            result.reason = decoded.reason;
            return result;
        }
        if (decoded.slot >= m_Slots.size())
        {
            result.reason = FallbackReason::SlotOutOfRange;
            return result;
        }

        const Slot& slot = m_Slots[decoded.slot];
        if (slot.generation != decoded.generation || !slot.occupied)
        {
            result.reason = FallbackReason::GenerationMismatch;
            return result;
        }
        if (slot.entry.topologyEpoch != m_TopologyEpoch)
        {
            result.reason = FallbackReason::TopologyEpochMismatch;
            return result;
        }

        result.entry = slot.entry;
        return result;
    }

    SurfaceResolutionResult SurfaceRegistry::ResolveSurfaceKey(
        const GpuVisibilityKey& key,
        const RegistryValidation& validation) const
    {
        SurfaceResolutionResult result;
        const FallbackReason keyReason = ValidateGpuVisibilityKey(key);
        if (keyReason != FallbackReason::None)
        {
            result.reason = keyReason;
            return result;
        }

        const RegistryLookupResult lookup =
            Inspect(key.geometryInstanceHandle);
        if (!lookup.Succeeded())
        {
            result.reason = lookup.reason;
            return result;
        }
        if (validation.topologyEpoch != m_TopologyEpoch ||
            lookup.entry.topologyEpoch != validation.topologyEpoch)
        {
            result.reason = FallbackReason::TopologyEpochMismatch;
            return result;
        }
        if (lookup.entry.lodRevision != validation.lodRevision)
        {
            result.reason = FallbackReason::LodRevisionMismatch;
            return result;
        }
        if (lookup.entry.deformationRevision !=
            validation.deformationRevision)
        {
            result.reason =
                FallbackReason::DeformationRevisionMismatch;
            return result;
        }
        if (lookup.entry.materialRevision !=
            validation.materialRevision)
        {
            result.reason = FallbackReason::MaterialRevisionMismatch;
            return result;
        }
        if (validation.requireSupportedDeformation &&
            !lookup.entry.deformationSupported)
        {
            result.reason = FallbackReason::UnsupportedDeformation;
            return result;
        }
        if (validation.requireSupportedMaterial &&
            !lookup.entry.materialSupported)
        {
            result.reason = FallbackReason::UnsupportedMaterial;
            return result;
        }

        result.key.stableInstanceHandle =
            lookup.entry.stableInstanceHandle;
        result.key.geometryIndex = lookup.entry.geometryIndex;
        result.key.geometryLocalPrimitiveIndex =
            key.geometryLocalPrimitiveIndex;
        result.metadata = lookup.entry;
        return result;
    }

    FallbackReason SurfaceRegistry::ResetForTopologyEpoch(
        uint64_t newTopologyEpoch)
    {
        if (newTopologyEpoch <= m_TopologyEpoch)
            return FallbackReason::TopologyEpochMismatch;

        m_TopologyEpoch = newTopologyEpoch;
        m_ActiveEntryCount = 0u;
        m_NextUnusedSlot = 0u;
        m_RequiresEpochReset = false;
        m_IdentityToSlot.clear();
        m_RecycledSlots = {};
        for (Slot& slot : m_Slots)
            slot = {};
        return FallbackReason::None;
    }

    uint32_t SurfaceRegistry::Capacity() const
    {
        return static_cast<uint32_t>(m_Slots.size());
    }

    uint32_t SurfaceRegistry::ActiveEntryCount() const
    {
        return m_ActiveEntryCount;
    }

    uint64_t SurfaceRegistry::TopologyEpoch() const
    {
        return m_TopologyEpoch;
    }

    bool SurfaceRegistry::RequiresEpochReset() const
    {
        return m_RequiresEpochReset;
    }

    FallbackReason SurfaceRegistry::ConfigurationReason() const
    {
        return m_ConfigurationReason;
    }

    FallbackReason DxrGeometryHandleMap::Insert(
        const SurfaceRegistry& registry,
        uint32_t instanceIndex,
        uint32_t geometryIndex,
        uint32_t geometryInstanceHandle)
    {
        const RegistryLookupResult lookup =
            registry.Inspect(geometryInstanceHandle);
        if (!lookup.Succeeded() ||
            lookup.entry.geometryIndex != geometryIndex)
        {
            return FallbackReason::TransientMappingFailure;
        }

        if (!m_HasTopologyEpoch)
        {
            m_TopologyEpoch = registry.TopologyEpoch();
            m_HasTopologyEpoch = true;
        }
        else if (m_TopologyEpoch != registry.TopologyEpoch())
        {
            return FallbackReason::TopologyEpochMismatch;
        }

        const auto instanceIdentity =
            m_InstanceStableHandles.find(instanceIndex);
        if (instanceIdentity != m_InstanceStableHandles.end() &&
            instanceIdentity->second != lookup.entry.stableInstanceHandle)
        {
            return FallbackReason::TransientMappingCollision;
        }

        const uint64_t tupleKey = MakeTupleKey(instanceIndex, geometryIndex);
        const auto existing = m_Mappings.find(tupleKey);
        if (existing != m_Mappings.end())
        {
            const Mapping& mapping = existing->second;
            return mapping.geometryInstanceHandle == geometryInstanceHandle &&
                    mapping.stableInstanceHandle ==
                        lookup.entry.stableInstanceHandle
                ? FallbackReason::None
                : FallbackReason::TransientMappingCollision;
        }

        m_Mappings.emplace(tupleKey, Mapping{
            instanceIndex,
            geometryIndex,
            geometryInstanceHandle,
            lookup.entry.stableInstanceHandle });
        m_InstanceStableHandles.emplace(
            instanceIndex,
            lookup.entry.stableInstanceHandle);
        return FallbackReason::None;
    }

    DxrHandleLookupResult DxrGeometryHandleMap::Lookup(
        const SurfaceRegistry& registry,
        uint32_t instanceIndex,
        uint32_t geometryIndex) const
    {
        DxrHandleLookupResult result;
        if (!m_HasTopologyEpoch)
        {
            result.reason = FallbackReason::TransientMappingFailure;
            return result;
        }
        if (m_TopologyEpoch != registry.TopologyEpoch())
        {
            result.reason = FallbackReason::TopologyEpochMismatch;
            return result;
        }

        const auto mapping = m_Mappings.find(
            MakeTupleKey(instanceIndex, geometryIndex));
        if (mapping == m_Mappings.end())
        {
            result.reason = FallbackReason::TransientMappingFailure;
            return result;
        }

        const RegistryLookupResult lookup = registry.Inspect(
            mapping->second.geometryInstanceHandle);
        if (!lookup.Succeeded())
        {
            result.reason = lookup.reason;
            return result;
        }
        if (lookup.entry.stableInstanceHandle !=
                mapping->second.stableInstanceHandle ||
            lookup.entry.geometryIndex != geometryIndex)
        {
            result.reason = FallbackReason::TransientMappingCollision;
            return result;
        }

        result.geometryInstanceHandle =
            mapping->second.geometryInstanceHandle;
        return result;
    }

    void DxrGeometryHandleMap::Clear()
    {
        m_Mappings.clear();
        m_InstanceStableHandles.clear();
        m_TopologyEpoch = 0u;
        m_HasTopologyEpoch = false;
    }

    size_t DxrGeometryHandleMap::Size() const
    {
        return m_Mappings.size();
    }

    uint64_t DxrGeometryHandleMap::MakeTupleKey(
        uint32_t instanceIndex,
        uint32_t geometryIndex)
    {
        return (uint64_t(instanceIndex) << 32u) | uint64_t(geometryIndex);
    }

    SurfaceMappingResult MapRasterSurfaceHit(
        const SurfaceRegistry& registry,
        uint32_t geometryInstanceHandle,
        uint64_t geometryLocalPrimitiveIndex,
        const std::array<float, 3>& rasterBarycentricWeights,
        bool frontFace,
        const RegistryValidation& validation)
    {
        const std::array<float, 2> canonicalDxrBarycentrics = {
            rasterBarycentricWeights[1],
            rasterBarycentricWeights[2]
        };
        return ResolveMappedSurfaceHit(
            registry,
            geometryInstanceHandle,
            geometryLocalPrimitiveIndex,
            canonicalDxrBarycentrics,
            frontFace,
            validation);
    }

    SurfaceMappingResult MapDxrSurfaceHit(
        const SurfaceRegistry& registry,
        const DxrGeometryHandleMap& mappings,
        uint32_t instanceIndex,
        uint32_t geometryIndex,
        uint64_t primitiveIndex,
        const std::array<float, 2>& barycentrics,
        bool frontFace,
        const RegistryValidation& validation)
    {
        const DxrHandleLookupResult lookup =
            mappings.Lookup(registry, instanceIndex, geometryIndex);
        if (!lookup.Succeeded())
        {
            SurfaceMappingResult result;
            result.reason = lookup.reason;
            return result;
        }
        return ResolveMappedSurfaceHit(
            registry,
            lookup.geometryInstanceHandle,
            primitiveIndex,
            barycentrics,
            frontFace,
            validation);
    }

    bool CanShareShading(
        const SampleDescriptor& first,
        const SampleDescriptor& second)
    {
        if (!first.covered || !second.covered ||
            first.validationReason != FallbackReason::None ||
            second.validationReason != FallbackReason::None ||
            !first.reconstructionValid ||
            !second.reconstructionValid ||
            first.sampleDependentBlocker ||
            second.sampleDependentBlocker ||
            first.frontFace != second.frontFace ||
            !SameSurface(first.packedKey, second.packedKey) ||
            !IsFiniteHitData(first) ||
            !IsFiniteHitData(second))
        {
            return false;
        }

        const double firstDepth = double(first.linearDepth);
        const double secondDepth = double(second.linearDepth);
        const double depthDifference =
            std::abs(firstDepth - secondDepth);
        const double depthScale =
            std::max(std::abs(firstDepth), std::abs(secondDepth));
        if ((depthScale == 0.0 && depthDifference != 0.0) ||
            (depthScale != 0.0 &&
                depthDifference / depthScale >
                    double(RelativeLinearDepthTolerance)))
        {
            return false;
        }

        const double dx = double(first.worldPosition.x) -
            double(second.worldPosition.x);
        const double dy = double(first.worldPosition.y) -
            double(second.worldPosition.y);
        const double dz = double(first.worldPosition.z) -
            double(second.worldPosition.z);
        const double positionSpan =
            std::sqrt(dx * dx + dy * dy + dz * dz);
        const double smallerFootprint = std::min(
            double(first.pixelFootprint),
            double(second.pixelFootprint));
        return positionSpan <=
            double(PositionFootprintFraction) * smallerFootprint;
    }

    bool IsSupportedSampleCount(uint32_t sampleCount)
    {
        return sampleCount == 2u ||
            sampleCount == 4u ||
            sampleCount == 8u ||
            sampleCount == 16u;
    }

    SampleGroupingResult GroupSamples(
        uint32_t sampleCount,
        SampleDescriptorLoader loader,
        void* loaderContext,
        const SampleGroupingOptions& options)
    {
        SampleGroupingResult result;
        InitializeSampleAssignments(result);
        result.requestedSampleCount = sampleCount;

        if (!IsSupportedSampleCount(sampleCount))
        {
            result.reason = FallbackReason::SampleCountUnsupported;
            result.requiresFullReference = true;
            return result;
        }
        result.sampleCount = sampleCount;
        if (!options.formatSupported)
        {
            result.reason = FallbackReason::FormatUnsupported;
            result.requiresFullReference = true;
            return result;
        }
        if (!loader)
        {
            result.reason = FallbackReason::ReconstructionFailure;
            result.requiresFullReference = true;
            return result;
        }

        std::array<SampleDescriptor, MaximumAdaptiveSampleCount> samples{};
        for (uint32_t sampleIndex = 0u;
            sampleIndex < sampleCount;
            ++sampleIndex)
        {
            // This is the sole caller-supplied load. Every grouping and
            // pairwise coherence read below uses this stable local snapshot.
            samples[sampleIndex] = loader(loaderContext, sampleIndex);
            ++result.loadedSampleCount;
        }

        for (uint32_t sampleIndex = 0u;
            sampleIndex < sampleCount;
            ++sampleIndex)
        {
            const SampleDescriptor& sample = samples[sampleIndex];
            if (!sample.covered)
            {
                ++result.backgroundSampleCount;
                continue;
            }

            const FallbackReason individualReason =
                ValidateIndividualSample(sample);
            if (individualReason != FallbackReason::None)
            {
                AddFallbackSingleton(
                    result,
                    sample,
                    sampleIndex,
                    individualReason);
                continue;
            }

            uint32_t matchingGroup = MaximumAdaptiveSampleCount;
            for (uint32_t groupIndex = 0u;
                groupIndex < result.groupCount;
                ++groupIndex)
            {
                const SampleGroup& group = result.groups[groupIndex];
                if (!group.referenceFallback &&
                    SameSurface(group.packedKey, sample.packedKey))
                {
                    matchingGroup = groupIndex;
                    break;
                }
            }

            if (matchingGroup == MaximumAdaptiveSampleCount)
            {
                SampleGroup& group = result.groups[result.groupCount];
                group.packedKey = sample.packedKey;
                group.representativeSample = sampleIndex;
                group.sampleMask = 1u << sampleIndex;
                result.sampleToGroup[sampleIndex] =
                    static_cast<uint8_t>(result.groupCount);
                ++result.groupCount;
                continue;
            }

            bool pairwiseSafe = true;
            const uint32_t memberMask =
                result.groups[matchingGroup].sampleMask;
            for (uint32_t memberIndex = 0u;
                memberIndex < sampleIndex;
                ++memberIndex)
            {
                if ((memberMask & (1u << memberIndex)) != 0u &&
                    !CanShareShading(samples[memberIndex], sample))
                {
                    pairwiseSafe = false;
                    break;
                }
            }

            if (!pairwiseSafe)
            {
                AddFallbackSingleton(
                    result,
                    sample,
                    sampleIndex,
                    FallbackReason::UnsafeSharing);
                continue;
            }

            result.groups[matchingGroup].sampleMask |=
                1u << sampleIndex;
            result.sampleToGroup[sampleIndex] =
                static_cast<uint8_t>(matchingGroup);
        }

        if (result.groupCount > options.workListCapacity)
        {
            BuildFullReferenceFallback(
                result,
                samples,
                sampleCount,
                FallbackReason::WorkListOverflow,
                sampleCount);
            return result;
        }

        FinalizeWeights(result);
        if (!result.weightConserved)
        {
            BuildFullReferenceFallback(
                result,
                samples,
                sampleCount,
                FallbackReason::ReconstructionFailure,
                sampleCount);
        }
        return result;
    }

    SampleGroupingResult GroupSamples(
        const SampleDescriptor* samples,
        uint32_t sampleCount,
        const SampleGroupingOptions& options)
    {
        if (!samples)
        {
            return GroupSamples(
                sampleCount,
                nullptr,
                nullptr,
                options);
        }
        return GroupSamples(
            sampleCount,
            LoadDescriptorArray,
            const_cast<SampleDescriptor*>(samples),
            options);
    }
}
