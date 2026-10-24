#include "adaptive_msaa.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    using namespace uvsr::adaptive_msaa;

    bool g_Passed = true;

    void Check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "Adaptive MSAA validation failed: "
                << message << '\n';
            g_Passed = false;
        }
    }

    bool Near(float actual, float expected, float tolerance = 1e-6f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    RegistryEntryDesc MakeEntry(
        uint64_t stableInstance,
        uint32_t geometry,
        uint64_t epoch,
        uint32_t lod = 1u,
        uint32_t deformation = 2u,
        uint32_t material = 3u,
        bool deformationSupported = true,
        bool materialSupported = true)
    {
        RegistryEntryDesc entry;
        entry.stableInstanceHandle = stableInstance;
        entry.geometryIndex = geometry;
        entry.topologyEpoch = epoch;
        entry.lodRevision = lod;
        entry.deformationRevision = deformation;
        entry.materialRevision = material;
        entry.deformationSupported = deformationSupported;
        entry.materialSupported = materialSupported;
        return entry;
    }

    RegistryValidation MakeValidation(
        uint64_t epoch,
        uint32_t lod = 1u,
        uint32_t deformation = 2u,
        uint32_t material = 3u,
        bool requireDeformation = false,
        bool requireMaterial = false)
    {
        RegistryValidation validation;
        validation.topologyEpoch = epoch;
        validation.lodRevision = lod;
        validation.deformationRevision = deformation;
        validation.materialRevision = material;
        validation.requireSupportedDeformation = requireDeformation;
        validation.requireSupportedMaterial = requireMaterial;
        return validation;
    }

    GpuVisibilityKey MakePackedKey(uint32_t slot, uint32_t primitive)
    {
        const HandlePackResult handle =
            PackGeometryInstanceHandle(slot, 1u);
        Check(handle.Succeeded(), "test packed handle must be valid");
        const GpuVisibilityKeyResult key =
            MakeGpuVisibilityKey(handle.handle, primitive);
        Check(key.Succeeded(), "test packed key must be valid");
        return key.key;
    }

    SampleDescriptor MakeCoveredSample(
        const GpuVisibilityKey& key,
        float depth = 10.f,
        Float3 position = {},
        float footprint = 1.f,
        bool frontFace = true)
    {
        SampleDescriptor sample;
        sample.covered = true;
        sample.packedKey = key;
        sample.frontFace = frontFace;
        sample.barycentrics = { 0.25f, 0.25f };
        sample.linearDepth = depth;
        sample.worldPosition = position;
        sample.pixelFootprint = footprint;
        return sample;
    }

    struct CountingLoader
    {
        std::array<SampleDescriptor, MaximumAdaptiveSampleCount> samples{};
        std::array<uint32_t, MaximumAdaptiveSampleCount> loads{};
        uint32_t nextExpectedSample = 0u;
        bool ascendingOrder = true;
    };

    SampleDescriptor LoadCountingSample(void* context, uint32_t sampleIndex)
    {
        auto& loader = *static_cast<CountingLoader*>(context);
        loader.ascendingOrder = loader.ascendingOrder &&
            sampleIndex == loader.nextExpectedSample;
        ++loader.nextExpectedSample;
        ++loader.loads[sampleIndex];
        return loader.samples[sampleIndex];
    }

    uint32_t CountBits(uint32_t value)
    {
        uint32_t count = 0u;
        while (value != 0u)
        {
            count += value & 1u;
            value >>= 1u;
        }
        return count;
    }

    uint32_t LowestSetBit(uint32_t value)
    {
        for (uint32_t bit = 0u; bit < 32u; ++bit)
        {
            if ((value & (1u << bit)) != 0u)
                return bit;
        }
        return 32u;
    }

    void TestFallbackReasonAbi()
    {
        constexpr std::array<FallbackReason, 25> reasons = {{
            FallbackReason::None,
            FallbackReason::InvalidSentinel,
            FallbackReason::InvalidHandleEncoding,
            FallbackReason::SlotOutOfRange,
            FallbackReason::GenerationMismatch,
            FallbackReason::TopologyEpochMismatch,
            FallbackReason::LodRevisionMismatch,
            FallbackReason::DeformationRevisionMismatch,
            FallbackReason::MaterialRevisionMismatch,
            FallbackReason::CapacityOverflow,
            FallbackReason::PrimitiveOverflow,
            FallbackReason::GenerationExhausted,
            FallbackReason::RegistryResetRequired,
            FallbackReason::TransientMappingCollision,
            FallbackReason::TransientMappingFailure,
            FallbackReason::UnsupportedDeformation,
            FallbackReason::UnsupportedMaterial,
            FallbackReason::UnsafeSharing,
            FallbackReason::ReconstructionFailure,
            FallbackReason::FormatUnsupported,
            FallbackReason::SampleCountUnsupported,
            FallbackReason::WorkListOverflow,
            FallbackReason::TextureContentRevisionMismatch,
            FallbackReason::TextureQualificationMismatch,
            FallbackReason::Count
        }};
        for (uint32_t index = 0u; index < reasons.size(); ++index)
        {
            Check(static_cast<uint32_t>(reasons[index]) == index,
                "fallback ABI values must remain contiguous and explicit");
            Check(std::string(GetFallbackReasonName(reasons[index])) !=
                    "Unknown",
                "every fallback ABI value has a usable diagnostics label");
        }
        Check(std::string(GetFallbackReasonName(
                static_cast<FallbackReason>(0xffffffffu))) == "Unknown",
            "out-of-ABI fallback values remain visibly distinguishable");
    }

    void TestInvalidationIdentityExhaustion()
    {
        uint32_t revision = 0u;
        Check(TryAdvanceInvalidationIdentity(revision) && revision == 1u,
            "a fresh 32-bit invalidation revision advances to the first nonzero identity");
        revision = std::numeric_limits<uint32_t>::max() - 1u;
        Check(TryAdvanceInvalidationIdentity(revision) &&
                revision == std::numeric_limits<uint32_t>::max(),
            "the final unique 32-bit invalidation revision remains usable");
        Check(!TryAdvanceInvalidationIdentity(revision) &&
                revision == std::numeric_limits<uint32_t>::max(),
            "32-bit invalidation exhaustion must not wrap or alias an earlier revision");

        InvalidationIdentityAuthority topologyAuthority(
            std::numeric_limits<uint64_t>::max() - 1u);
        const TopologyEpochAdvanceResult finalEpoch =
            topologyAuthority.AcquireTopologyEpoch();
        Check(finalEpoch.Succeeded() &&
                finalEpoch.topologyEpoch ==
                    std::numeric_limits<uint64_t>::max() &&
                topologyAuthority.LastTopologyEpoch() ==
                    finalEpoch.topologyEpoch,
            "the process authority may issue the final globally unique topology epoch once");
        const TopologyEpochAdvanceResult exhaustedEpoch =
            topologyAuthority.AcquireTopologyEpoch();
        Check(!exhaustedEpoch.Succeeded() &&
                exhaustedEpoch.reason ==
                    FallbackReason::RegistryResetRequired &&
                topologyAuthority.IsExhausted() &&
                topologyAuthority.LastTopologyEpoch() ==
                    std::numeric_limits<uint64_t>::max(),
            "the process authority permanently latches instead of reusing an exhausted topology epoch");

        InvalidationIdentityAuthority explicitLatch(17u);
        explicitLatch.LatchExhaustion();
        const TopologyEpochAdvanceResult latchedEpoch =
            explicitLatch.AcquireTopologyEpoch();
        Check(!latchedEpoch.Succeeded() &&
                latchedEpoch.reason ==
                    FallbackReason::RegistryResetRequired &&
                explicitLatch.LastTopologyEpoch() == 17u,
            "a material or revision overflow can permanently latch the same authority without consuming another epoch");

        InvalidationIdentityAuthority pairedAuthority(31u);
        uint32_t pairedRevision = 7u;
        const TopologyIdentityAdvanceResult paired =
            pairedAuthority.AcquireTopologyIdentity(pairedRevision);
        Check(paired.Succeeded() && paired.topologyEpoch == 32u &&
                paired.topologyRevision == 8u &&
                pairedAuthority.LastTopologyEpoch() == 32u &&
                pairedRevision == 8u,
            "topology epoch and revision advance as one authority transaction");

        InvalidationIdentityAuthority revisionOverflowAuthority(41u);
        uint32_t exhaustedRevision =
            std::numeric_limits<uint32_t>::max();
        const TopologyIdentityAdvanceResult revisionOverflow =
            revisionOverflowAuthority.AcquireTopologyIdentity(
                exhaustedRevision);
        Check(!revisionOverflow.Succeeded() &&
                revisionOverflow.reason ==
                    FallbackReason::RegistryResetRequired &&
                revisionOverflowAuthority.LastTopologyEpoch() == 41u &&
                exhaustedRevision ==
                    std::numeric_limits<uint32_t>::max() &&
                revisionOverflowAuthority.IsExhausted(),
            "revision exhaustion leaves both topology identity components unchanged and latches the authority");

        InvalidationIdentityAuthority epochOverflowAuthority(
            std::numeric_limits<uint64_t>::max());
        uint32_t unchangedRevision = 9u;
        const TopologyIdentityAdvanceResult epochOverflow =
            epochOverflowAuthority.AcquireTopologyIdentity(
                unchangedRevision);
        Check(!epochOverflow.Succeeded() &&
                epochOverflowAuthority.LastTopologyEpoch() ==
                    std::numeric_limits<uint64_t>::max() &&
                unchangedRevision == 9u &&
                epochOverflowAuthority.IsExhausted(),
            "epoch exhaustion leaves both topology identity components unchanged and latches the authority");
    }

    void TestPackingAndSurfaceIdentity()
    {
        const HandlePackResult first =
            PackGeometryInstanceHandle(0u, 1u);
        Check(first.Succeeded() && first.handle == 0x00100000u,
            "generation occupies the high twelve handle bits");
        const HandlePackResult maximum = PackGeometryInstanceHandle(
            GeometryInstanceSlotMask,
            MaximumGeometryInstanceGeneration);
        Check(maximum.Succeeded() && maximum.handle == 0xffefffffu,
            "maximum non-reserved handle remains lossless");

        const DecodedGeometryInstanceHandle decoded =
            DecodeGeometryInstanceHandle(maximum.handle);
        Check(decoded.Succeeded() &&
                decoded.slot == GeometryInstanceSlotMask &&
                decoded.generation == MaximumGeometryInstanceGeneration,
            "handle decoding reverses exact packing");
        Check(PackGeometryInstanceHandle(
                GeometryInstanceSlotCount, 1u).reason ==
                    FallbackReason::SlotOutOfRange,
            "slot overflow fails closed");
        Check(PackGeometryInstanceHandle(0u, 0u).reason ==
                FallbackReason::InvalidHandleEncoding &&
            PackGeometryInstanceHandle(
                0u, GeometryInstanceGenerationMask).reason ==
                FallbackReason::InvalidHandleEncoding,
            "reserved generations fail closed");
        Check(DecodeGeometryInstanceHandle(InvalidVisibilityWord).reason ==
                FallbackReason::InvalidSentinel &&
            DecodeGeometryInstanceHandle(0u).reason ==
                FallbackReason::InvalidHandleEncoding,
            "invalid and reserved packed handles remain distinguishable");
        const GpuVisibilityKey cleared{
            ClearedVisibilityWord, ClearedVisibilityWord };
        Check(IsClearedVisibilityKey(cleared) &&
                ValidateGpuVisibilityKey(cleared) ==
                    FallbackReason::InvalidHandleEncoding,
            "the exact RTV clear is recognizable but never a valid surface");

        const GpuVisibilityKeyResult largestPrimitive =
            MakeGpuVisibilityKey(maximum.handle, 0xfffffffeull);
        Check(largestPrimitive.Succeeded() &&
                largestPrimitive.key.geometryLocalPrimitiveIndex ==
                    0xfffffffeu,
            "largest non-sentinel primitive is lossless");
        Check(MakeGpuVisibilityKey(maximum.handle, 0xffffffffull).reason ==
                FallbackReason::PrimitiveOverflow &&
            MakeGpuVisibilityKey(maximum.handle, 0x100000000ull).reason ==
                FallbackReason::PrimitiveOverflow,
            "primitive sentinel collision and integer overflow fail closed");

        SurfaceKey logical{ 0x123456789abcdef0ull, 17u, 23u };
        SurfaceHit firstHit{ logical, { 0.1f, 0.2f }, true };
        SurfaceHit secondHit{ logical, { 0.7f, 0.1f }, false };
        Check(SameSurface(firstHit, secondHit),
            "barycentrics and front face do not alter logical ownership");
        ++secondHit.key.geometryLocalPrimitiveIndex;
        Check(!SameSurface(firstHit, secondHit),
            "geometry-local primitive participates in logical ownership");

        const GpuVisibilityKey valid = MakePackedKey(3u, 9u);
        Check(SameSurface(valid, valid),
            "equal valid GPU keys prove common ownership");
        GpuVisibilityKey invalid;
        Check(!SameSurface(invalid, invalid),
            "equal invalid sentinels never prove common ownership");
        Check(!SameSurface(cleared, cleared),
            "equal cleared-background encodings never prove ownership");
    }

    void TestRegistryValidationAndReuse()
    {
        constexpr uint64_t Epoch = 41u;
        SurfaceRegistry registry(2u, Epoch);
        Check(registry.Capacity() == 2u &&
                registry.ConfigurationReason() == FallbackReason::None,
            "valid registry capacity is retained");
        SurfaceRegistry oversized(GeometryInstanceSlotCount + 1u, Epoch);
        Check(oversized.ConfigurationReason() ==
                FallbackReason::CapacityOverflow &&
            !oversized.Register(MakeEntry(1u, 1u, Epoch)).Succeeded(),
            "registry capacity beyond twenty slot bits fails closed");

        const RegistryEntryDesc firstEntry =
            MakeEntry(0xfedcba9876543210ull, 7u, Epoch);
        const RegistryRegistrationResult first =
            registry.Register(firstEntry);
        const RegistryRegistrationResult duplicate =
            registry.Register(firstEntry);
        Check(first.Succeeded() && duplicate.Succeeded() &&
                duplicate.geometryInstanceHandle ==
                    first.geometryInstanceHandle &&
                registry.ActiveEntryCount() == 1u,
            "exact logical registration is idempotent");

        RegistryEntryDesc changedLod = firstEntry;
        ++changedLod.lodRevision;
        Check(registry.Register(changedLod).reason ==
                FallbackReason::LodRevisionMismatch,
            "LOD changes require identity invalidation");
        RegistryEntryDesc changedTopology = firstEntry;
        ++changedTopology.topologyEpoch;
        Check(registry.Register(changedTopology).reason ==
                FallbackReason::TopologyEpochMismatch,
            "topology changes require synchronized invalidation");

        const RegistryRegistrationResult second =
            registry.Register(MakeEntry(2u, 8u, Epoch));
        Check(second.Succeeded() &&
                registry.Register(MakeEntry(3u, 9u, Epoch)).reason ==
                    FallbackReason::CapacityOverflow,
            "full registry reports capacity overflow");

        const GpuVisibilityKeyResult packed = MakeGpuVisibilityKey(
            first.geometryInstanceHandle, 19u);
        const RegistryValidation validation = MakeValidation(Epoch);
        const SurfaceResolutionResult resolved =
            registry.ResolveSurfaceKey(packed.key, validation);
        Check(resolved.Succeeded() &&
                resolved.key.stableInstanceHandle ==
                    firstEntry.stableInstanceHandle &&
                resolved.key.geometryIndex == firstEntry.geometryIndex &&
                resolved.key.geometryLocalPrimitiveIndex == 19u,
            "registry resolves full logical identity without truncation");

        RegistryValidation mismatch = validation;
        ++mismatch.topologyEpoch;
        Check(registry.ResolveSurfaceKey(packed.key, mismatch).reason ==
                FallbackReason::TopologyEpochMismatch,
            "stale topology epoch is rejected");
        mismatch = validation;
        ++mismatch.lodRevision;
        Check(registry.ResolveSurfaceKey(packed.key, mismatch).reason ==
                FallbackReason::LodRevisionMismatch,
            "stale LOD revision is rejected");
        mismatch = validation;
        ++mismatch.deformationRevision;
        Check(registry.ResolveSurfaceKey(packed.key, mismatch).reason ==
                FallbackReason::DeformationRevisionMismatch,
            "stale deformation revision is explicit metadata failure");
        mismatch = validation;
        ++mismatch.materialRevision;
        Check(registry.ResolveSurfaceKey(packed.key, mismatch).reason ==
                FallbackReason::MaterialRevisionMismatch,
            "stale material revision is explicit metadata failure");

        Check(registry.UpdateNonIdentityMetadata(
                first.geometryInstanceHandle,
                12u, 13u, false, false) == FallbackReason::None,
            "non-identity metadata updates retain the handle");
        Check(registry.ResolveSurfaceKey(packed.key, validation).reason ==
                FallbackReason::DeformationRevisionMismatch,
            "old non-identity revision cannot silently resolve");
        RegistryValidation updated =
            MakeValidation(Epoch, 1u, 12u, 13u, true, false);
        Check(registry.ResolveSurfaceKey(packed.key, updated).reason ==
                FallbackReason::UnsupportedDeformation,
            "unsupported deformation selects an explicit fallback");
        updated.requireSupportedDeformation = false;
        updated.requireSupportedMaterial = true;
        Check(registry.ResolveSurfaceKey(packed.key, updated).reason ==
                FallbackReason::UnsupportedMaterial,
            "unsupported material selects an explicit fallback");
        updated.requireSupportedMaterial = false;
        Check(registry.ResolveSurfaceKey(packed.key, updated).Succeeded(),
            "unsupported flags do not alter identity when support is not required");

        const uint32_t staleHandle = first.geometryInstanceHandle;
        Check(registry.InvalidateIdentity(staleHandle) ==
                FallbackReason::None &&
            registry.Inspect(staleHandle).reason ==
                FallbackReason::GenerationMismatch,
            "retirement invalidates the previous generation immediately");
        const RegistryRegistrationResult reused =
            registry.Register(MakeEntry(4u, 10u, Epoch));
        const auto staleDecoded = DecodeGeometryInstanceHandle(staleHandle);
        const auto reusedDecoded = DecodeGeometryInstanceHandle(
            reused.geometryInstanceHandle);
        Check(reused.Succeeded() &&
                reusedDecoded.slot == staleDecoded.slot &&
                reusedDecoded.generation == staleDecoded.generation + 1u,
            "slot reuse advances generation without aliasing stale handles");

        const HandlePackResult outOfRange =
            PackGeometryInstanceHandle(9u, 1u);
        Check(registry.Inspect(outOfRange.handle).reason ==
                FallbackReason::SlotOutOfRange,
            "validly encoded slots outside configured capacity are rejected");
    }

    void TestGenerationExhaustionAndEpochReset()
    {
        constexpr uint64_t InitialEpoch = 9u;
        SurfaceRegistry registry(1u, InitialEpoch);
        uint32_t oldestHandle = InvalidVisibilityWord;
        uint32_t finalHandle = InvalidVisibilityWord;

        for (uint32_t generation = MinimumGeometryInstanceGeneration;
            generation <= MaximumGeometryInstanceGeneration;
            ++generation)
        {
            const RegistryRegistrationResult registration =
                registry.Register(MakeEntry(
                    uint64_t(generation), 2u, InitialEpoch));
            const auto decoded = DecodeGeometryInstanceHandle(
                registration.geometryInstanceHandle);
            Check(registration.Succeeded() &&
                    decoded.generation == generation,
                "every valid generation is issued exactly once");
            if (generation == MinimumGeometryInstanceGeneration)
                oldestHandle = registration.geometryInstanceHandle;
            finalHandle = registration.geometryInstanceHandle;

            const FallbackReason retired = registry.InvalidateIdentity(
                registration.geometryInstanceHandle);
            const FallbackReason expected =
                generation == MaximumGeometryInstanceGeneration
                    ? FallbackReason::GenerationExhausted
                    : FallbackReason::None;
            Check(retired == expected,
                "generation exhaustion is reported at reserved boundary");
        }

        Check(registry.RequiresEpochReset() &&
                registry.Register(MakeEntry(8u, 2u, InitialEpoch)).reason ==
                    FallbackReason::RegistryResetRequired &&
                registry.Inspect(finalHandle).reason ==
                    FallbackReason::GenerationMismatch,
            "exhaustion blocks reuse and leaves retired handles invalid");
        Check(registry.ResetForTopologyEpoch(InitialEpoch) ==
                FallbackReason::TopologyEpochMismatch,
            "full reset requires a strictly wider epoch bump");
        Check(registry.ResetForTopologyEpoch(InitialEpoch + 1u) ==
                FallbackReason::None &&
            !registry.RequiresEpochReset() &&
            registry.ActiveEntryCount() == 0u,
            "wider epoch reset clears every slot and exhaustion marker");

        const RegistryRegistrationResult replacement = registry.Register(
            MakeEntry(999u, 2u, InitialEpoch + 1u));
        Check(replacement.Succeeded() &&
                replacement.geometryInstanceHandle == oldestHandle,
            "full reset may repeat packed bits only under a new wider epoch");
        const auto key = MakeGpuVisibilityKey(
            replacement.geometryInstanceHandle, 0u).key;
        Check(registry.ResolveSurfaceKey(
                key, MakeValidation(InitialEpoch)).reason ==
                    FallbackReason::TopologyEpochMismatch &&
            registry.ResolveSurfaceKey(
                key, MakeValidation(InitialEpoch + 1u)).Succeeded(),
            "wider epoch distinguishes identical packed bits after full reset");
    }

    void TestRasterAndDxrMapping()
    {
        constexpr uint64_t Epoch = 77u;
        SurfaceRegistry registry(3u, Epoch);
        const RegistryRegistrationResult first = registry.Register(
            MakeEntry(0x1111222233334444ull, 5u, Epoch));
        const RegistryRegistrationResult second = registry.Register(
            MakeEntry(0x9999aaaabbbbccccull, 5u, Epoch));
        const RegistryRegistrationResult otherGeometry = registry.Register(
            MakeEntry(0x5555666677778888ull, 6u, Epoch));
        Check(first.Succeeded() && second.Succeeded() &&
                otherGeometry.Succeeded(),
            "mapping test registry setup succeeds");

        DxrGeometryHandleMap mappings;
        Check(mappings.Lookup(registry, 99u, 5u).reason ==
                FallbackReason::TransientMappingFailure,
            "an empty transient mapping table fails closed as missing");
        Check(mappings.Insert(registry, 21u, 5u,
                first.geometryInstanceHandle) == FallbackReason::None &&
            mappings.Insert(registry, 21u, 5u,
                first.geometryInstanceHandle) == FallbackReason::None &&
            mappings.Size() == 1u,
            "identical transient mapping insertion is idempotent");
        Check(mappings.Insert(registry, 21u, 5u,
                second.geometryInstanceHandle) ==
                    FallbackReason::TransientMappingCollision,
            "one DXR tuple cannot alias distinct logical instances");
        Check(mappings.Insert(registry, 22u, 5u,
                otherGeometry.geometryInstanceHandle) ==
                    FallbackReason::TransientMappingFailure,
            "DXR geometry index must match the resolved registry geometry");
        Check(mappings.Lookup(registry, 99u, 5u).reason ==
                FallbackReason::TransientMappingFailure,
            "missing transient mapping fails closed");

        const RegistryValidation validation = MakeValidation(Epoch);
        const std::array<float, 3> rasterBarycentricWeights = {
            0.2f, 0.3f, 0.5f
        };
        const std::array<float, 2> dxrBarycentrics = { 0.3f, 0.5f };
        const SurfaceMappingResult raster = MapRasterSurfaceHit(
            registry,
            first.geometryInstanceHandle,
            37u,
            rasterBarycentricWeights,
            false,
            validation);
        const SurfaceMappingResult dxr = MapDxrSurfaceHit(
            registry,
            mappings,
            21u,
            5u,
            37u,
            dxrBarycentrics,
            false,
            validation);
        Check(raster.Succeeded() && dxr.Succeeded() &&
                SameSurface(raster.packedKey, dxr.packedKey) &&
                SameSurface(raster.hit, dxr.hit) &&
                raster.hit.barycentrics == dxrBarycentrics &&
                raster.hit.barycentrics == dxr.hit.barycentrics &&
                raster.hit.frontFace == dxr.hit.frontFace,
            "raster and future DXR tuples converge on one SurfaceHit identity");
        Check(MapDxrSurfaceHit(
                registry, mappings, 21u, 5u, 0xffffffffull,
                dxrBarycentrics, true, validation).reason ==
                    FallbackReason::PrimitiveOverflow,
            "DXR primitive sentinel collision fails closed");

        Check(registry.InvalidateIdentity(first.geometryInstanceHandle) ==
                FallbackReason::None &&
            MapDxrSurfaceHit(
                registry, mappings, 21u, 5u, 1u,
                dxrBarycentrics, true, validation).reason ==
                    FallbackReason::GenerationMismatch,
            "stale transient maps cannot survive handle retirement");
        mappings.Clear();
        Check(mappings.Size() == 0u,
            "transient mapping table clears at acceleration-structure rebuild");

        SurfaceRegistry resetRegistry(1u, Epoch);
        const RegistryRegistrationResult beforeReset = resetRegistry.Register(
            MakeEntry(101u, 0u, Epoch));
        DxrGeometryHandleMap staleMappings;
        Check(staleMappings.Insert(
                resetRegistry, 0u, 0u,
                beforeReset.geometryInstanceHandle) ==
                    FallbackReason::None,
            "pre-reset transient mapping succeeds");
        Check(resetRegistry.ResetForTopologyEpoch(Epoch + 1u) ==
                FallbackReason::None,
            "DXR stale-map test advances topology epoch");
        const RegistryRegistrationResult afterReset = resetRegistry.Register(
            MakeEntry(202u, 0u, Epoch + 1u));
        Check(afterReset.Succeeded() &&
                afterReset.geometryInstanceHandle ==
                    beforeReset.geometryInstanceHandle,
            "epoch reset deliberately permits packed handle-bit reuse");
        Check(staleMappings.Lookup(resetRegistry, 0u, 0u).reason ==
                FallbackReason::TopologyEpochMismatch &&
            MapDxrSurfaceHit(
                resetRegistry,
                staleMappings,
                0u,
                0u,
                1u,
                dxrBarycentrics,
                true,
                MakeValidation(Epoch + 1u)).reason ==
                    FallbackReason::TopologyEpochMismatch,
            "stale DXR tuples cannot alias reused handle bits after epoch reset");

        SurfaceRegistry instanceRegistry(3u, Epoch);
        const auto instanceGeometry0 = instanceRegistry.Register(
            MakeEntry(301u, 0u, Epoch));
        const auto instanceGeometry1 = instanceRegistry.Register(
            MakeEntry(301u, 1u, Epoch));
        const auto otherInstanceGeometry = instanceRegistry.Register(
            MakeEntry(302u, 2u, Epoch));
        DxrGeometryHandleMap instanceMappings;
        Check(instanceMappings.Insert(
                instanceRegistry, 4u, 0u,
                instanceGeometry0.geometryInstanceHandle) ==
                    FallbackReason::None &&
            instanceMappings.Insert(
                instanceRegistry, 4u, 1u,
                instanceGeometry1.geometryInstanceHandle) ==
                    FallbackReason::None,
            "one DXR instance may map multiple geometries of one stable instance");
        Check(instanceMappings.Insert(
                instanceRegistry, 4u, 2u,
                otherInstanceGeometry.geometryInstanceHandle) ==
                    FallbackReason::TransientMappingCollision,
            "one DXR InstanceIndex cannot mix logical stable instances");
    }

    void TestCanShareShadingBoundaries()
    {
        const GpuVisibilityKey key = MakePackedKey(1u, 3u);
        SampleDescriptor first = MakeCoveredSample(key);
        SampleDescriptor second = first;
        Check(CanShareShading(first, second),
            "identical finite samples may share invariant work");

        second.linearDepth = first.linearDepth *
            (1.f + RelativeLinearDepthTolerance * 0.9f);
        Check(CanShareShading(first, second),
            "depth difference below relative 2^-12 bound is accepted");
        second.linearDepth = first.linearDepth *
            (1.f + RelativeLinearDepthTolerance * 1.2f);
        Check(!CanShareShading(first, second),
            "depth difference above relative 2^-12 bound is rejected");

        second = first;
        second.worldPosition.x = PositionFootprintFraction;
        Check(CanShareShading(first, second),
            "position span at one eighth of smaller footprint is accepted");
        second.worldPosition.x = PositionFootprintFraction + 1e-4f;
        Check(!CanShareShading(first, second),
            "position span beyond one eighth footprint is rejected");
        second = first;
        second.pixelFootprint = 0.5f;
        second.worldPosition.x = 0.07f;
        Check(!CanShareShading(first, second),
            "smaller of the two footprints controls the position bound");

        second = first;
        second.frontFace = !first.frontFace;
        Check(!CanShareShading(first, second),
            "opposite front-face state cannot share shading");
        second = first;
        second.sampleDependentBlocker = true;
        Check(!CanShareShading(first, second),
            "caller-supplied sample-dependent blocker is authoritative");
        second = first;
        second.reconstructionValid = false;
        Check(!CanShareShading(first, second),
            "failed reconstruction cannot share shading");
        second = first;
        second.linearDepth = std::numeric_limits<float>::quiet_NaN();
        Check(!CanShareShading(first, second),
            "non-finite depth cannot share shading");
        second = first;
        second.worldPosition.x =
            std::numeric_limits<float>::infinity();
        Check(!CanShareShading(first, second),
            "non-finite world position cannot share shading");
        second = first;
        second.barycentrics[0] =
            std::numeric_limits<float>::quiet_NaN();
        Check(!CanShareShading(first, second),
            "non-finite barycentrics cannot share shading");
        second = first;
        second.packedKey = MakePackedKey(2u, 3u);
        Check(!CanShareShading(first, second),
            "different ownership never shares shading");
    }

    void TestGroupingPatternsAndWeights()
    {
        const GpuVisibilityKey firstKey = MakePackedKey(1u, 7u);
        const GpuVisibilityKey secondKey = MakePackedKey(2u, 7u);

        for (uint32_t sampleCount : { 2u, 4u, 8u, 16u })
        {
            CountingLoader loader;
            for (uint32_t index = 0u; index < sampleCount; ++index)
                loader.samples[index] = MakeCoveredSample(firstKey);
            const SampleGroupingResult allSame = GroupSamples(
                sampleCount,
                LoadCountingSample,
                &loader);
            Check(allSame.groupCount == 1u &&
                    allSame.groups[0].representativeSample == 0u &&
                    allSame.groups[0].sampleCount == sampleCount &&
                    Near(allSame.groups[0].weight, 1.f) &&
                    allSame.weightConserved &&
                    loader.ascendingOrder &&
                    loader.nextExpectedSample == sampleCount,
                "all-safe samples group under lowest representative");
            for (uint32_t index = 0u; index < sampleCount; ++index)
            {
                Check(loader.loads[index] == 1u &&
                        allSame.sampleToGroup[index] == 0u,
                    "every supported descriptor is loaded exactly once");
            }

            std::array<SampleDescriptor, MaximumAdaptiveSampleCount> unique{};
            for (uint32_t index = 0u; index < sampleCount; ++index)
                unique[index] = MakeCoveredSample(MakePackedKey(index, index));
            const SampleGroupingResult allUnique =
                GroupSamples(unique.data(), sampleCount);
            Check(allUnique.groupCount == sampleCount &&
                    allUnique.backgroundSampleCount == 0u &&
                    allUnique.weightConserved,
                "unique ownership produces one deterministic group per sample");
            for (uint32_t index = 0u; index < sampleCount; ++index)
            {
                Check(allUnique.groups[index].representativeSample == index &&
                        allUnique.groups[index].sampleMask == (1u << index) &&
                        allUnique.sampleToGroup[index] == index &&
                        Near(allUnique.groups[index].weight,
                            1.f / float(sampleCount)),
                    "unique groups retain ascending representatives and weights");
            }

            std::array<SampleDescriptor, MaximumAdaptiveSampleCount> alternating{};
            uint32_t evenMask = 0u;
            uint32_t oddMask = 0u;
            for (uint32_t index = 0u; index < sampleCount; ++index)
            {
                alternating[index] = MakeCoveredSample(
                    (index & 1u) == 0u ? firstKey : secondKey);
                ((index & 1u) == 0u ? evenMask : oddMask) |= 1u << index;
            }
            const SampleGroupingResult twoGroups =
                GroupSamples(alternating.data(), sampleCount);
            Check(twoGroups.groupCount == 2u &&
                    twoGroups.groups[0].representativeSample == 0u &&
                    twoGroups.groups[0].sampleMask == evenMask &&
                    twoGroups.groups[1].representativeSample == 1u &&
                    twoGroups.groups[1].sampleMask == oddMask &&
                    Near(twoGroups.groups[0].weight, 0.5f) &&
                    Near(twoGroups.groups[1].weight, 0.5f),
                "interleaved equal keys group deterministically, not contiguously");
            for (uint32_t index = 0u; index < sampleCount; ++index)
            {
                Check(twoGroups.sampleToGroup[index] == (index & 1u),
                    "sample routing follows the deterministic interleaved groups");
            }

            // Exhaust every coverage mask for every supported sample count.
            // All covered samples use one safe key, so this simultaneously
            // locks background accounting, representative choice, masks, and
            // exact binary groupCount/totalSampleCount weights at 16x.
            const uint32_t maskLimit = 1u << sampleCount;
            for (uint32_t coverageMask = 0u;
                coverageMask < maskLimit;
                ++coverageMask)
            {
                std::array<SampleDescriptor, MaximumAdaptiveSampleCount>
                    samples{};
                for (uint32_t index = 0u; index < sampleCount; ++index)
                {
                    samples[index] = MakeCoveredSample(firstKey);
                    samples[index].covered =
                        (coverageMask & (1u << index)) != 0u;
                }
                const SampleGroupingResult grouped =
                    GroupSamples(samples.data(), sampleCount);
                const uint32_t coveredCount = CountBits(coverageMask);
                Check(grouped.backgroundSampleCount ==
                        sampleCount - coveredCount &&
                        Near(grouped.backgroundWeight,
                            float(sampleCount - coveredCount) /
                                float(sampleCount)) &&
                        grouped.groupCount == (coveredCount == 0u ? 0u : 1u) &&
                        grouped.weightConserved,
                    "every coverage mask conserves explicit background weight");
                if (coveredCount != 0u)
                {
                    Check(grouped.groups[0].sampleMask == coverageMask &&
                            grouped.groups[0].representativeSample ==
                                LowestSetBit(coverageMask) &&
                            Near(grouped.groups[0].weight,
                                float(coveredCount) / float(sampleCount)),
                        "every coverage mask retains exact group membership and weight");
                    for (uint32_t index = 0u; index < sampleCount; ++index)
                    {
                        const uint8_t expected =
                            (coverageMask & (1u << index)) != 0u
                                ? 0u
                                : BackgroundSampleAssignment;
                        Check(grouped.sampleToGroup[index] == expected,
                            "coverage routing distinguishes the group from background");
                    }
                }
            }
        }

        std::array<SampleDescriptor, 4> mixed = {{
            MakeCoveredSample(firstKey),
            MakeCoveredSample(firstKey),
            MakeCoveredSample(secondKey),
            SampleDescriptor{}
        }};
        const SampleGroupingResult mixedResult =
            GroupSamples(mixed.data(), 4u);
        Check(mixedResult.groupCount == 2u &&
                mixedResult.backgroundSampleCount == 1u &&
                Near(mixedResult.groups[0].weight, 0.5f) &&
                Near(mixedResult.groups[1].weight, 0.25f) &&
                Near(mixedResult.backgroundWeight, 0.25f) &&
                Near(mixedResult.totalWeight, 1.f),
            "safe groups plus uncovered samples resolve to exactly one pixel");
    }

    void TestPairwiseSplittingAndFallbacks()
    {
        const GpuVisibilityKey key = MakePackedKey(3u, 4u);
        std::array<SampleDescriptor, 4> samples = {{
            MakeCoveredSample(key, 10.f, { 0.f, 0.f, 0.f }),
            MakeCoveredSample(key, 10.f, { 0.1f, 0.f, 0.f }),
            MakeCoveredSample(key, 10.f, { -0.1f, 0.f, 0.f }),
            SampleDescriptor{}
        }};
        const SampleGroupingResult pairwise =
            GroupSamples(samples.data(), 4u);
        Check(pairwise.groupCount == 2u &&
                pairwise.groups[0].sampleMask == 0x3u &&
                !pairwise.groups[0].referenceFallback &&
                pairwise.groups[1].sampleMask == 0x4u &&
                pairwise.groups[1].referenceFallback &&
                pairwise.groups[1].fallbackReason ==
                    FallbackReason::UnsafeSharing &&
                pairwise.groups[1].representativeSample == 2u &&
                Near(pairwise.backgroundWeight, 0.25f) &&
                pairwise.weightConserved,
            "coherence is pairwise across all members, not representative-only");

        samples[0].sampleDependentBlocker = true;
        samples[1].frontFace = false;
        samples[2].reconstructionValid = false;
        const SampleGroupingResult blocked =
            GroupSamples(samples.data(), 4u);
        Check(blocked.groupCount == 3u &&
                blocked.groups[0].referenceFallback &&
                blocked.groups[0].fallbackReason ==
                    FallbackReason::UnsafeSharing &&
                !blocked.groups[1].referenceFallback &&
                blocked.groups[2].referenceFallback &&
                blocked.groups[2].fallbackReason ==
                    FallbackReason::ReconstructionFailure &&
                blocked.hasReferenceFallback &&
                blocked.weightConserved,
            "blockers and failed reconstruction produce deterministic singletons");

        samples = {{
            MakeCoveredSample(key),
            MakeCoveredSample(key),
            MakeCoveredSample(key),
            SampleDescriptor{}
        }};
        samples[0].packedKey = {};
        samples[1].validationReason = FallbackReason::GenerationMismatch;
        samples[2].validationReason = FallbackReason::UnsupportedMaterial;
        const SampleGroupingResult invalid =
            GroupSamples(samples.data(), 4u);
        Check(invalid.groupCount == 3u &&
                invalid.groups[0].fallbackReason ==
                    FallbackReason::InvalidSentinel &&
                invalid.groups[1].fallbackReason ==
                    FallbackReason::GenerationMismatch &&
                invalid.groups[2].fallbackReason ==
                    FallbackReason::UnsupportedMaterial &&
                invalid.backgroundSampleCount == 1u &&
                invalid.weightConserved,
            "invalid and unsupported covered samples retain stable reasons and weight");
    }

    void TestGlobalFailClosedPaths()
    {
        CountingLoader loader;
        const GpuVisibilityKey key = MakePackedKey(0u, 0u);
        for (SampleDescriptor& sample : loader.samples)
            sample = MakeCoveredSample(key);

        for (uint32_t unsupported : { 0u, 1u, 3u, 5u, 32u })
        {
            loader.loads = {};
            loader.nextExpectedSample = 0u;
            loader.ascendingOrder = true;
            const SampleGroupingResult result = GroupSamples(
                unsupported,
                LoadCountingSample,
                &loader);
            uint32_t totalLoads = 0u;
            for (uint32_t count : loader.loads)
                totalLoads += count;
            Check(result.reason == FallbackReason::SampleCountUnsupported &&
                    result.requiresFullReference &&
                    result.requestedSampleCount == unsupported &&
                    result.sampleCount == 0u &&
                    result.loadedSampleCount == 0u &&
                    totalLoads == 0u,
                "unsupported sample count routes whole pixel to reference without loads");
        }

        loader.loads = {};
        loader.nextExpectedSample = 0u;
        loader.ascendingOrder = true;
        SampleGroupingOptions formatOptions;
        formatOptions.formatSupported = false;
        const SampleGroupingResult format = GroupSamples(
            4u, LoadCountingSample, &loader, formatOptions);
        Check(format.reason == FallbackReason::FormatUnsupported &&
                format.requiresFullReference &&
                format.loadedSampleCount == 0u,
            "unsupported format routes whole pixel to reference before loading");

        const SampleGroupingResult missingLoader = GroupSamples(
            4u, nullptr, nullptr);
        Check(missingLoader.reason == FallbackReason::ReconstructionFailure &&
                missingLoader.requiresFullReference,
            "missing descriptor loader fails closed");

        std::array<SampleDescriptor, 4> unique = {{
            MakeCoveredSample(MakePackedKey(0u, 0u)),
            MakeCoveredSample(MakePackedKey(1u, 0u)),
            MakeCoveredSample(MakePackedKey(2u, 0u)),
            SampleDescriptor{}
        }};
        SampleGroupingOptions overflowOptions;
        overflowOptions.workListCapacity = 2u;
        const SampleGroupingResult overflow = GroupSamples(
            unique.data(), 4u, overflowOptions);
        Check(overflow.reason == FallbackReason::WorkListOverflow &&
                overflow.requiresFullReference &&
                overflow.groupCount == 3u &&
                overflow.backgroundSampleCount == 1u &&
                overflow.weightConserved,
            "work-list overflow converts every covered sample to reference shading");
        for (uint32_t groupIndex = 0u;
            groupIndex < overflow.groupCount;
            ++groupIndex)
        {
            Check(overflow.groups[groupIndex].referenceFallback &&
                    overflow.groups[groupIndex].fallbackReason ==
                        FallbackReason::WorkListOverflow &&
                    overflow.groups[groupIndex].sampleCount == 1u,
                "overflow fallback is explicit and deterministic per covered sample");
        }

        unique[0].validationReason =
            FallbackReason::GenerationMismatch;
        const SampleGroupingResult mixedOverflow = GroupSamples(
            unique.data(), 4u, overflowOptions);
        Check(mixedOverflow.reason == FallbackReason::WorkListOverflow &&
                mixedOverflow.groups[0].fallbackReason ==
                    FallbackReason::GenerationMismatch &&
                mixedOverflow.groups[1].fallbackReason ==
                    FallbackReason::WorkListOverflow &&
                mixedOverflow.groups[2].fallbackReason ==
                    FallbackReason::WorkListOverflow,
            "whole-pixel overflow preserves pre-existing per-sample fallback reasons");
    }

    void TestFiniteExtremeWeighting()
    {
        volatile float finiteSample =
            std::numeric_limits<float>::max() * 0.5f;
        float unweightedThenAveraged = 0.f;
        float referenceWeighted = 0.f;
        for (uint32_t sampleIndex = 0u; sampleIndex < 4u; ++sampleIndex)
        {
            unweightedThenAveraged += finiteSample;
            referenceWeighted += finiteSample * 0.25f;
        }

        Check(!std::isfinite(unweightedThenAveraged) &&
                std::isfinite(referenceWeighted),
            "per-sample weighting prevents finite reference HDR from overflowing an intermediate group sum");
    }

    void TestComposedCapabilityResolution()
    {
        ComposedCapabilityRequest request;
        request.requestedSampleCount = 16u;
        request.resolvedSampleCount = 8u;
        request.requestedImplementation =
            uvsr::AdaptiveMsaaImplementation::CompactVisibility;
        request.surfaceKeySupported = true;
        request.compactDepthSupported = true;
        request.visibilityClassSupported = true;
        const ComposedCapabilityResult ordinaryDowngrade =
            ResolveComposedAdaptiveMsaaCapability(request);
        Check(ordinaryDowngrade.requestedSampleCount == 16u &&
                ordinaryDowngrade.resolvedSampleCount == 8u &&
                ordinaryDowngrade.renderTargetSampleDowngraded &&
                ordinaryDowngrade.implementation ==
                    uvsr::AdaptiveMsaaImplementation::CompactVisibility &&
                ordinaryDowngrade.reason ==
                    ComposedCapabilityReason::RenderTargetSampleDowngrade,
            "requested 16x resolving to 8x remains an ordinary render-target downgrade");

        request.resolvedSampleCount = 16u;
        request.surfaceKeySupported = false;
        const ComposedCapabilityResult missingKey =
            ResolveComposedAdaptiveMsaaCapability(request);
        Check(missingKey.implementation ==
                    uvsr::AdaptiveMsaaImplementation::Reference &&
                missingKey.reason ==
                    ComposedCapabilityReason::SurfaceKeyUnsupported &&
                !missingKey.renderTargetSampleDowngraded,
            "resolved 16x without RG32_UINT selects Reference before compact allocation");

        request.surfaceKeySupported = true;
        request.compactDepthSupported = false;
        const ComposedCapabilityResult missingDepth =
            ResolveComposedAdaptiveMsaaCapability(request);
        Check(missingDepth.implementation ==
                    uvsr::AdaptiveMsaaImplementation::Reference &&
                missingDepth.reason ==
                    ComposedCapabilityReason::CompactDepthUnsupported,
            "resolved 16x without compact D32 selects Reference explicitly");

        request.compactDepthSupported = true;
        request.visibilityClassSupported = false;
        const ComposedCapabilityResult missingClass =
            ResolveComposedAdaptiveMsaaCapability(request);
        Check(missingClass.implementation ==
                    uvsr::AdaptiveMsaaImplementation::Reference &&
                missingClass.reason ==
                    ComposedCapabilityReason::VisibilityClassUnsupported,
            "resolved 16x without R8/R32 ownership class selects Reference explicitly");

        request.visibilityClassSupported = true;
        const ComposedCapabilityResult futureNative16 =
            ResolveComposedAdaptiveMsaaCapability(request);
        Check(futureNative16.implementation ==
                    uvsr::AdaptiveMsaaImplementation::CompactVisibility &&
                futureNative16.reason ==
                    ComposedCapabilityReason::None &&
                !futureNative16.renderTargetSampleDowngraded,
            "injected future 16x capabilities preserve compact visibility");

        request.requestedImplementation =
            uvsr::AdaptiveMsaaImplementation::AdaptiveGBuffer;
        request.compactDepthSupported = false;
        request.visibilityClassSupported = false;
        const ComposedCapabilityResult adaptiveGBuffer =
            ResolveComposedAdaptiveMsaaCapability(request);
        Check(adaptiveGBuffer.implementation ==
                    uvsr::AdaptiveMsaaImplementation::AdaptiveGBuffer &&
                adaptiveGBuffer.reason == ComposedCapabilityReason::None,
            "adaptive G-buffer capability does not depend on compact depth or class formats");
    }
}

int main()
{
    TestFallbackReasonAbi();
    TestInvalidationIdentityExhaustion();
    TestPackingAndSurfaceIdentity();
    TestRegistryValidationAndReuse();
    TestGenerationExhaustionAndEpochReset();
    TestRasterAndDxrMapping();
    TestCanShareShadingBoundaries();
    TestGroupingPatternsAndWeights();
    TestPairwiseSplittingAndFallbacks();
    TestGlobalFailClosedPaths();
    TestFiniteExtremeWeighting();
    TestComposedCapabilityResolution();

    if (g_Passed)
        std::cout << "UVSR adaptive MSAA core validation passed\n";
    return g_Passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
