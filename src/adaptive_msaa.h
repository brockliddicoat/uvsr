#pragma once

#include "temporal_aa_options.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace uvsr::adaptive_msaa
{
    inline constexpr uint32_t GeometryInstanceSlotBits = 20u;
    inline constexpr uint32_t GeometryInstanceGenerationBits = 12u;
    inline constexpr uint32_t GeometryInstanceSlotCount =
        1u << GeometryInstanceSlotBits;
    inline constexpr uint32_t GeometryInstanceSlotMask =
        GeometryInstanceSlotCount - 1u;
    inline constexpr uint32_t GeometryInstanceGenerationMask =
        (1u << GeometryInstanceGenerationBits) - 1u;
    inline constexpr uint32_t MinimumGeometryInstanceGeneration = 1u;
    inline constexpr uint32_t MaximumGeometryInstanceGeneration =
        GeometryInstanceGenerationMask - 1u;
    inline constexpr uint32_t InvalidVisibilityWord = 0xffffffffu;
    // NVRHI's D3D12 RTV-only integer clear is expressed through
    // ClearRenderTargetView float values. Zero is exactly representable there,
    // while InvalidVisibilityWord is not. Generation zero is reserved, so an
    // exact { 0, 0 } render-target clear cannot collide with a valid key.
    inline constexpr uint32_t ClearedVisibilityWord = 0u;
    inline constexpr uint32_t MaximumAdaptiveSampleCount = 16u;
    inline constexpr uint8_t BackgroundSampleAssignment = 0xffu;
    inline constexpr float RelativeLinearDepthTolerance =
        1.f / 4096.f;
    inline constexpr float PositionFootprintFraction = 1.f / 8.f;
    inline constexpr float GroupWeightTolerance = 1e-6f;

    // These explicit values are the shared CPU/HLSL diagnostics ABI. Never
    // reorder or implicitly renumber them: diagnostics and serialized test
    // evidence must keep the same reason meaning across implementations.
    enum class FallbackReason : uint32_t
    {
        None = 0u,
        InvalidSentinel = 1u,
        InvalidHandleEncoding = 2u,
        SlotOutOfRange = 3u,
        GenerationMismatch = 4u,
        TopologyEpochMismatch = 5u,
        LodRevisionMismatch = 6u,
        DeformationRevisionMismatch = 7u,
        MaterialRevisionMismatch = 8u,
        CapacityOverflow = 9u,
        PrimitiveOverflow = 10u,
        GenerationExhausted = 11u,
        RegistryResetRequired = 12u,
        TransientMappingCollision = 13u,
        TransientMappingFailure = 14u,
        UnsupportedDeformation = 15u,
        UnsupportedMaterial = 16u,
        UnsafeSharing = 17u,
        ReconstructionFailure = 18u,
        FormatUnsupported = 19u,
        SampleCountUnsupported = 20u,
        WorkListOverflow = 21u,
        TextureContentRevisionMismatch = 22u,
        TextureQualificationMismatch = 23u,
        Count = 24u
    };

    [[nodiscard]] const char* GetFallbackReasonName(
        FallbackReason reason);

    // A per-epoch revision never wraps. Exhaustion leaves the old value
    // unchanged so the caller can latch the process authority below and force
    // permanent Reference fallback instead of aliasing an earlier revision.
    [[nodiscard]] bool TryAdvanceInvalidationIdentity(uint32_t& value);

    struct TopologyEpochAdvanceResult
    {
        uint64_t topologyEpoch = 0u;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None && topologyEpoch != 0u;
        }
    };

    struct TopologyIdentityAdvanceResult
    {
        uint64_t topologyEpoch = 0u;
        uint32_t topologyRevision = 0u;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None &&
                topologyEpoch != 0u && topologyRevision != 0u;
        }
    };

    // Slow-path authority used only when a scene identity changes. The
    // process-global instance survives viewer and scene recreation, while
    // constructible local instances make exhaustion behavior testable.
    class InvalidationIdentityAuthority final
    {
    public:
        explicit InvalidationIdentityAuthority(
            uint64_t lastTopologyEpoch = 0u);

        [[nodiscard]] TopologyEpochAdvanceResult AcquireTopologyEpoch();
        [[nodiscard]] TopologyIdentityAdvanceResult
            AcquireTopologyIdentity(uint32_t& topologyRevision);
        void LatchExhaustion();
        [[nodiscard]] bool IsExhausted() const;
        [[nodiscard]] uint64_t LastTopologyEpoch() const;

    private:
        mutable std::mutex m_Mutex;
        uint64_t m_LastTopologyEpoch = 0u;
        bool m_Exhausted = false;
    };

    [[nodiscard]] TopologyEpochAdvanceResult AcquireProcessTopologyEpoch();
    [[nodiscard]] TopologyIdentityAdvanceResult
        AcquireProcessTopologyIdentity(uint32_t& topologyRevision);
    void LatchProcessInvalidationIdentityExhaustion();
    [[nodiscard]] bool ProcessInvalidationIdentityExhausted();

    enum class ComposedCapabilityReason : uint32_t
    {
        None = 0u,
        RenderTargetSampleDowngrade,
        ResolvedSingleSample,
        SurfaceKeyUnsupported,
        CompactDepthUnsupported,
        VisibilityClassUnsupported
    };

    struct ComposedCapabilityRequest
    {
        uint32_t requestedSampleCount = 1u;
        uint32_t resolvedSampleCount = 1u;
        AdaptiveMsaaImplementation requestedImplementation =
            AdaptiveMsaaImplementation::Reference;
        bool surfaceKeySupported = false;
        bool compactDepthSupported = false;
        bool visibilityClassSupported = false;
    };

    struct ComposedCapabilityResult
    {
        uint32_t requestedSampleCount = 1u;
        uint32_t resolvedSampleCount = 1u;
        AdaptiveMsaaImplementation implementation =
            AdaptiveMsaaImplementation::Reference;
        ComposedCapabilityReason reason = ComposedCapabilityReason::None;
        bool renderTargetSampleDowngraded = false;
    };

    [[nodiscard]] ComposedCapabilityResult
        ResolveComposedAdaptiveMsaaCapability(
            const ComposedCapabilityRequest& request);
    [[nodiscard]] const char* GetComposedCapabilityReasonName(
        ComposedCapabilityReason reason);

    struct SurfaceKey
    {
        uint64_t stableInstanceHandle = 0u;
        uint32_t geometryIndex = 0u;
        uint32_t geometryLocalPrimitiveIndex = 0u;
    };

    struct SurfaceHit
    {
        SurfaceKey key;
        // Canonical DXR convention: x is the perspective-correct weight of
        // triangle vertex 1 and y is the weight of vertex 2; vertex 0 is
        // 1 - x - y. These values and frontFace describe a hit; they never
        // alter ownership.
        std::array<float, 2> barycentrics{};
        bool frontFace = true;
    };

    // This structure is deliberately layout-equivalent to the requested GPU
    // uint2. The first word is opaque outside SurfaceRegistry; the second is
    // the geometry-local triangle index.
    struct GpuVisibilityKey
    {
        uint32_t geometryInstanceHandle = InvalidVisibilityWord;
        uint32_t geometryLocalPrimitiveIndex = InvalidVisibilityWord;
    };

    [[nodiscard]] constexpr bool IsClearedVisibilityKey(
        const GpuVisibilityKey& key)
    {
        return key.geometryInstanceHandle == ClearedVisibilityWord &&
            key.geometryLocalPrimitiveIndex == ClearedVisibilityWord;
    }

    static_assert(sizeof(SurfaceKey) == 16u);
    static_assert(sizeof(GpuVisibilityKey) == 8u);
    static_assert(alignof(GpuVisibilityKey) == alignof(uint32_t));
    static_assert(std::is_standard_layout_v<GpuVisibilityKey>);
    static_assert(sizeof(FallbackReason) == sizeof(uint32_t));
    static_assert(MinimumGeometryInstanceGeneration > 0u,
        "generation zero must remain reserved for the exact RTV clear");

    struct HandlePackResult
    {
        uint32_t handle = InvalidVisibilityWord;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    struct DecodedGeometryInstanceHandle
    {
        uint32_t slot = 0u;
        uint32_t generation = 0u;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    struct GpuVisibilityKeyResult
    {
        GpuVisibilityKey key;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    [[nodiscard]] HandlePackResult PackGeometryInstanceHandle(
        uint32_t slot,
        uint32_t generation);

    [[nodiscard]] DecodedGeometryInstanceHandle
        DecodeGeometryInstanceHandle(uint32_t handle);

    [[nodiscard]] GpuVisibilityKeyResult MakeGpuVisibilityKey(
        uint32_t geometryInstanceHandle,
        uint64_t geometryLocalPrimitiveIndex);

    [[nodiscard]] FallbackReason ValidateGpuVisibilityKey(
        const GpuVisibilityKey& key);

    // Logical SurfaceKey/SurfaceHit overloads compare ownership only. The GPU
    // overload additionally rejects invalid encodings before exact equality.
    [[nodiscard]] bool SameSurface(
        const SurfaceKey& first,
        const SurfaceKey& second);
    [[nodiscard]] bool SameSurface(
        const SurfaceHit& first,
        const SurfaceHit& second);
    [[nodiscard]] bool SameSurface(
        const GpuVisibilityKey& first,
        const GpuVisibilityKey& second);

    struct RegistryEntryDesc
    {
        uint64_t stableInstanceHandle = 0u;
        uint32_t geometryIndex = 0u;
        uint64_t topologyEpoch = 0u;
        uint32_t lodRevision = 0u;
        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;
        bool deformationSupported = true;
        bool materialSupported = true;
    };

    struct RegistryValidation
    {
        uint64_t topologyEpoch = 0u;
        uint32_t lodRevision = 0u;
        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;
        bool requireSupportedDeformation = false;
        bool requireSupportedMaterial = false;
    };

    struct RegistryRegistrationResult
    {
        uint32_t geometryInstanceHandle = InvalidVisibilityWord;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    struct RegistryLookupResult
    {
        RegistryEntryDesc entry;
        uint32_t slot = 0u;
        uint32_t generation = 0u;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    struct SurfaceResolutionResult
    {
        SurfaceKey key;
        RegistryEntryDesc metadata;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    class SurfaceRegistry
    {
    public:
        explicit SurfaceRegistry(
            uint32_t capacity,
            uint64_t topologyEpoch);

        [[nodiscard]] RegistryRegistrationResult Register(
            const RegistryEntryDesc& entry);

        // Topology or LOD changes must retire the old handle and register a
        // replacement. Ordinary retirement advances generation before reuse.
        // Retiring generation 0xffe blocks every reuse until ResetForTopologyEpoch.
        [[nodiscard]] FallbackReason InvalidateIdentity(
            uint32_t geometryInstanceHandle);

        // Deformation and material revisions are validation metadata, not key
        // fields, so they can change without manufacturing a new identity.
        [[nodiscard]] FallbackReason UpdateNonIdentityMetadata(
            uint32_t geometryInstanceHandle,
            uint32_t deformationRevision,
            uint32_t materialRevision,
            bool deformationSupported,
            bool materialSupported);

        [[nodiscard]] RegistryLookupResult Inspect(
            uint32_t geometryInstanceHandle) const;

        [[nodiscard]] SurfaceResolutionResult ResolveSurfaceKey(
            const GpuVisibilityKey& key,
            const RegistryValidation& validation) const;

        // A wider epoch bump is a synchronized operation: every old GPU key
        // and transient map must already be unreachable. Handles may repeat
        // numerically after this full reset; the wider epoch detects stale use.
        [[nodiscard]] FallbackReason ResetForTopologyEpoch(
            uint64_t newTopologyEpoch);

        [[nodiscard]] uint32_t Capacity() const;
        [[nodiscard]] uint32_t ActiveEntryCount() const;
        [[nodiscard]] uint64_t TopologyEpoch() const;
        [[nodiscard]] bool RequiresEpochReset() const;
        [[nodiscard]] FallbackReason ConfigurationReason() const;

    private:
        struct RegistryIdentity
        {
            uint64_t stableInstanceHandle = 0u;
            uint32_t geometryIndex = 0u;

            [[nodiscard]] bool operator==(
                const RegistryIdentity& other) const
            {
                return stableInstanceHandle == other.stableInstanceHandle &&
                    geometryIndex == other.geometryIndex;
            }
        };

        struct RegistryIdentityHash
        {
            [[nodiscard]] size_t operator()(
                const RegistryIdentity& identity) const;
        };

        struct Slot
        {
            RegistryEntryDesc entry;
            uint32_t generation = MinimumGeometryInstanceGeneration;
            bool occupied = false;
            bool exhausted = false;
        };

        std::vector<Slot> m_Slots;
        std::unordered_map<RegistryIdentity, uint32_t, RegistryIdentityHash>
            m_IdentityToSlot;
        std::priority_queue<
            uint32_t,
            std::vector<uint32_t>,
            std::greater<uint32_t>> m_RecycledSlots;
        uint64_t m_TopologyEpoch = 0u;
        uint32_t m_NextUnusedSlot = 0u;
        uint32_t m_ActiveEntryCount = 0u;
        FallbackReason m_ConfigurationReason = FallbackReason::None;
        bool m_RequiresEpochReset = false;
    };

    struct DxrHandleLookupResult
    {
        uint32_t geometryInstanceHandle = InvalidVisibilityWord;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    // A table belongs to one transient acceleration-structure build. Rebuilds
    // clear it; generation and topology validation catch any stale handle that
    // nevertheless survives beyond that boundary.
    class DxrGeometryHandleMap
    {
    public:
        [[nodiscard]] FallbackReason Insert(
            const SurfaceRegistry& registry,
            uint32_t instanceIndex,
            uint32_t geometryIndex,
            uint32_t geometryInstanceHandle);

        [[nodiscard]] DxrHandleLookupResult Lookup(
            const SurfaceRegistry& registry,
            uint32_t instanceIndex,
            uint32_t geometryIndex) const;

        void Clear();
        [[nodiscard]] size_t Size() const;

    private:
        struct Mapping
        {
            uint32_t instanceIndex = 0u;
            uint32_t geometryIndex = 0u;
            uint32_t geometryInstanceHandle = InvalidVisibilityWord;
            uint64_t stableInstanceHandle = 0u;
        };

        [[nodiscard]] static uint64_t MakeTupleKey(
            uint32_t instanceIndex,
            uint32_t geometryIndex);

        std::unordered_map<uint64_t, Mapping> m_Mappings;
        std::unordered_map<uint32_t, uint64_t> m_InstanceStableHandles;
        uint64_t m_TopologyEpoch = 0u;
        bool m_HasTopologyEpoch = false;
    };

    struct SurfaceMappingResult
    {
        GpuVisibilityKey packedKey;
        SurfaceHit hit;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    [[nodiscard]] SurfaceMappingResult MapRasterSurfaceHit(
        const SurfaceRegistry& registry,
        uint32_t geometryInstanceHandle,
        uint64_t geometryLocalPrimitiveIndex,
        // Raster SV_Barycentrics supplies weights in vertex 0, 1, 2 order.
        // This API accepts all three so callers cannot silently pass the
        // incompatible native DXR pair.
        const std::array<float, 3>& rasterBarycentricWeights,
        bool frontFace,
        const RegistryValidation& validation);

    [[nodiscard]] SurfaceMappingResult MapDxrSurfaceHit(
        const SurfaceRegistry& registry,
        const DxrGeometryHandleMap& mappings,
        uint32_t instanceIndex,
        uint32_t geometryIndex,
        uint64_t primitiveIndex,
        // Native DXR BuiltInTriangleIntersectionAttributes convention:
        // weights for triangle vertices 1 and 2.
        const std::array<float, 2>& barycentrics,
        bool frontFace,
        const RegistryValidation& validation);

    struct Float3
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    struct SampleDescriptor
    {
        bool covered = false;
        GpuVisibilityKey packedKey;
        std::array<float, 2> barycentrics{};
        bool frontFace = true;
        float linearDepth = 0.f;
        Float3 worldPosition;
        // World-space width of the reconstructed pixel footprint at this hit.
        float pixelFootprint = 0.f;
        bool sampleDependentBlocker = false;
        bool reconstructionValid = true;
        // Mapping/reconstruction code records its fail-closed reason here.
        FallbackReason validationReason = FallbackReason::None;
    };

    [[nodiscard]] bool CanShareShading(
        const SampleDescriptor& first,
        const SampleDescriptor& second);

    struct SampleGroup
    {
        GpuVisibilityKey packedKey;
        uint32_t representativeSample = 0u;
        uint32_t sampleMask = 0u;
        uint32_t sampleCount = 0u;
        float weight = 0.f;
        bool referenceFallback = false;
        FallbackReason fallbackReason = FallbackReason::None;
    };

    struct SampleGroupingOptions
    {
        bool formatSupported = true;
        // Models the capacity of a later separated GPU work list. The inline
        // core always retains enough local records to route the whole pixel to
        // reference shading if that external capacity is exceeded.
        uint32_t workListCapacity = MaximumAdaptiveSampleCount;
    };

    struct SampleGroupingResult
    {
        std::array<SampleGroup, MaximumAdaptiveSampleCount> groups{};
        std::array<uint8_t, MaximumAdaptiveSampleCount>
            sampleToGroup{};
        // requestedSampleCount records invalid external input for diagnostics.
        // sampleCount is always either zero or a safe bound for every fixed
        // per-sample array in this result.
        uint32_t requestedSampleCount = 0u;
        uint32_t sampleCount = 0u;
        uint32_t loadedSampleCount = 0u;
        uint32_t groupCount = 0u;
        uint32_t backgroundSampleCount = 0u;
        float backgroundWeight = 0.f;
        float totalWeight = 0.f;
        FallbackReason reason = FallbackReason::None;
        bool hasReferenceFallback = false;
        bool requiresFullReference = false;
        bool weightConserved = false;
    };

    using SampleDescriptorLoader = SampleDescriptor (*)(
        void* context,
        uint32_t sampleIndex);

    [[nodiscard]] bool IsSupportedSampleCount(uint32_t sampleCount);

    // The loader is called exactly once for each supported sample, in ascending
    // order. All later comparisons use the local copy, matching the eventual
    // GPU contract that each multisample descriptor is fetched once.
    [[nodiscard]] SampleGroupingResult GroupSamples(
        uint32_t sampleCount,
        SampleDescriptorLoader loader,
        void* loaderContext,
        const SampleGroupingOptions& options = {});

    [[nodiscard]] SampleGroupingResult GroupSamples(
        const SampleDescriptor* samples,
        uint32_t sampleCount,
        const SampleGroupingOptions& options = {});
}
