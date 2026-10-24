#include "adaptive_msaa_scene.h"

#include <donut/engine/SceneTypes.h>

using namespace donut::math;
#include <donut/shaders/material_cb.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace uvsr::adaptive_msaa;

    bool g_Passed = true;

    void Check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "Adaptive MSAA scene validation failed: "
                << message << '\n';
            g_Passed = false;
        }
    }

    CompactGeometryValidationDesc MakeValidGeometry()
    {
        CompactGeometryValidationDesc geometry;
        geometry.instanceIndex = 1u;
        geometry.instanceCount = 3u;
        geometry.flattenedHandleIndex = 2u;
        geometry.handleMapCount = 6u;
        geometry.materialIndex = 1u;
        geometry.materialCount = 2u;

        geometry.indexBufferByteSize = 4096u;
        geometry.vertexBufferByteSize = 4096u;
        geometry.firstIndex = 2u;
        geometry.indexCount = 6u;
        geometry.firstVertex = 1u;
        geometry.vertexCount = 3u;

        geometry.positionRange = { 0u, 120u };
        geometry.previousPositionRange = { 128u, 120u };
        geometry.texCoordRange = { 256u, 80u };
        geometry.normalRange = { 384u, 40u };
        geometry.tangentRange = { 512u, 40u };
        return geometry;
    }

    CompactGeometryIdentityDesc MakeIdentity(
        uint64_t stableInstanceHandle,
        uint32_t geometryIndex,
        uint32_t topologyRevision = 3u,
        uint32_t lodRevision = 4u,
        uint32_t deformationRevision = 5u,
        uint32_t materialRevision = 6u)
    {
        CompactGeometryIdentityDesc identity;
        identity.stableInstanceHandle = stableInstanceHandle;
        identity.geometryIndex = geometryIndex;
        identity.topologyRevision = topologyRevision;
        identity.lodRevision = lodRevision;
        identity.deformationRevision = deformationRevision;
        identity.materialRevision = materialRevision;
        return identity;
    }

    RegistryValidation MakeValidation(
        uint64_t topologyEpoch,
        uint32_t lodRevision,
        uint32_t deformationRevision,
        uint32_t materialRevision)
    {
        RegistryValidation validation;
        validation.topologyEpoch = topologyEpoch;
        validation.lodRevision = lodRevision;
        validation.deformationRevision = deformationRevision;
        validation.materialRevision = materialRevision;
        validation.requireSupportedDeformation = true;
        validation.requireSupportedMaterial = true;
        return validation;
    }

    void TestGpuSceneAbi()
    {
        static_assert(sizeof(CompactGeometryInstanceGpuRecord) == 64u);
        static_assert(sizeof(CompactGeometryGpuRecord) == 48u);
        static_assert(sizeof(CompactSceneGpuConstants) == 64u);
        static_assert(CompactTextureRegisterSpace !=
            CompactRawBufferRegisterSpace);
        static_assert(static_cast<uint32_t>(
            CompactGeometryPayloadClass::Invalid) == 0u);
        static_assert(static_cast<uint32_t>(
            CompactGeometryPayloadClass::Reconstruct) == 1u);
        static_assert(static_cast<uint32_t>(
            CompactGeometryPayloadClass::FullGBufferFallback) == 2u);

        const CompactGeometryInstanceGpuRecord instanceRecord;
        Check(instanceRecord.generation == 0u &&
                instanceRecord.geometryIndex == InvalidVisibilityWord &&
                instanceRecord.instanceIndex == InvalidVisibilityWord &&
                instanceRecord.geometryRecordIndex ==
                    InvalidVisibilityWord &&
                instanceRecord.payloadClass == static_cast<uint32_t>(
                    CompactGeometryPayloadClass::Invalid) &&
                instanceRecord.payloadFallbackReason ==
                    static_cast<uint32_t>(
                        FallbackReason::ReconstructionFailure),
            "unused GPU instance records must fail validation by default");

        const CompactGeometryGpuRecord geometryRecord;
        Check(geometryRecord.indexBufferDescriptor ==
                InvalidVisibilityWord &&
                geometryRecord.vertexBufferDescriptor ==
                    InvalidVisibilityWord &&
                geometryRecord.materialIndex == InvalidVisibilityWord,
            "uninitialized GPU geometry indirections must use sentinels");

        CompactSceneGpuConstants descriptors;
        descriptors.textureDescriptorCount = 4u;
        descriptors.rawBufferDescriptorCount = 4u;
        descriptors.descriptorCount = 8u;
        Check(HasExactCompactDescriptorPartition(descriptors) &&
                IsCompactTextureDescriptor(descriptors, 0u) &&
                IsCompactTextureDescriptor(descriptors, 3u) &&
                !IsCompactTextureDescriptor(descriptors, 4u) &&
                IsCompactRawBufferDescriptor(descriptors, 4u) &&
                IsCompactRawBufferDescriptor(descriptors, 7u) &&
                !IsCompactRawBufferDescriptor(descriptors, 3u) &&
                !IsCompactRawBufferDescriptor(descriptors, 8u) &&
                !IsCompactRawBufferDescriptor(
                    descriptors, InvalidVisibilityWord),
            "dense descriptor partition proves each texture and raw slot kind");

        descriptors.rawBufferDescriptorCount = 3u;
        Check(!HasExactCompactDescriptorPartition(descriptors) &&
                !IsCompactTextureDescriptor(descriptors, 0u) &&
                !IsCompactRawBufferDescriptor(descriptors, 4u),
            "descriptor count-sum mismatches invalidate both typed ranges");
        descriptors.textureDescriptorCount = 0u;
        descriptors.rawBufferDescriptorCount = 8u;
        Check(HasExactCompactDescriptorPartition(descriptors) &&
                !IsCompactTextureDescriptor(descriptors, 0u) &&
                IsCompactRawBufferDescriptor(descriptors, 0u) &&
                IsCompactRawBufferDescriptor(descriptors, 7u),
            "a raw-only dense partition has an exact zero-width texture range");
        descriptors.textureDescriptorCount = 8u;
        descriptors.rawBufferDescriptorCount = 0u;
        Check(HasExactCompactDescriptorPartition(descriptors) &&
                IsCompactTextureDescriptor(descriptors, 0u) &&
                IsCompactTextureDescriptor(descriptors, 7u) &&
                !IsCompactRawBufferDescriptor(descriptors, 7u),
            "a texture-only dense partition has an exact zero-width raw range");
    }

    void TestSnapshotVersionOrdering()
    {
        const CompactSceneSnapshotVersion current = { 8u, 20u };
        Check(ValidateCompactSceneSnapshotReplacement(
                { 8u, 21u }, current) == FallbackReason::None,
            "a newer build serial in the live epoch may publish");
        Check(ValidateCompactSceneSnapshotReplacement(
                { 9u, 1u }, current) == FallbackReason::None,
            "a wider topology epoch dominates its reset build serial");
        Check(ValidateCompactSceneSnapshotReplacement(
                { 8u, 20u }, current) ==
                    FallbackReason::TransientMappingFailure,
            "an equal snapshot version cannot replace the live snapshot");
        Check(ValidateCompactSceneSnapshotReplacement(
                { 8u, 19u }, current) ==
                    FallbackReason::TransientMappingFailure,
            "an older same-epoch snapshot cannot be republished");
        Check(ValidateCompactSceneSnapshotReplacement(
                { 7u, 999u }, current) ==
                    FallbackReason::TopologyEpochMismatch,
            "a large serial cannot make an older topology epoch current");
        Check(ValidateCompactSceneSnapshotReplacement(
                { 0u, 1u }, current) ==
                    FallbackReason::TransientMappingFailure &&
                ValidateCompactSceneSnapshotReplacement(
                    { 8u, 21u }, { 0u, 0u }) ==
                        FallbackReason::TransientMappingFailure,
            "zero epoch or serial versions fail closed");
    }

    void TestCompactEligibilityFallbackLatch()
    {
        const CompactEligibilityRevisionIdentity initial = {
            41u, 7u, 8u, 9u, 10u, 11u, 12u
        };
        CompactEligibilityFallbackLatch latch;
        Check(latch.Reason(initial) == FallbackReason::None,
            "an empty eligibility latch permits the first compact attempt");

        latch.Record(initial, FallbackReason::UnsupportedMaterial);
        for (uint32_t unchangedFrame = 0u;
            unchangedFrame < 3u;
            ++unchangedFrame)
        {
            Check(latch.Reason(initial) ==
                    FallbackReason::UnsupportedMaterial,
                "an unchanged failed revision remains on Reference without retrying compact topology");
        }

        std::array<CompactEligibilityRevisionIdentity, 7u> changed = {
            initial, initial, initial, initial, initial, initial, initial
        };
        ++changed[0].topologyEpoch;
        ++changed[1].topologyRevision;
        ++changed[2].lodRevision;
        ++changed[3].deformationRevision;
        ++changed[4].materialRevision;
        ++changed[5].textureContentRevision;
        ++changed[6].textureQualificationToken;
        for (const CompactEligibilityRevisionIdentity& identity : changed)
        {
            Check(latch.Reason(identity) == FallbackReason::None,
                "every relevant revision change permits one fresh compact attempt");
        }

        const CompactEligibilityRevisionIdentity revised = changed.back();
        latch.Record(revised, FallbackReason::UnsupportedDeformation);
        Check(latch.Reason(initial) == FallbackReason::None &&
                latch.Reason(revised) ==
                    FallbackReason::UnsupportedDeformation,
            "a failed retry replaces the latch with its exact revision identity");

        latch.Clear();
        Check(latch.Reason(revised) == FallbackReason::None,
            "clearing the eligibility latch permits the current revision");
        latch.Record(revised, FallbackReason::CapacityOverflow);
        Check(latch.Reason(revised) == FallbackReason::CapacityOverflow,
            "capacity overflow is latched for an unchanged scene revision");
        latch.Record(revised, FallbackReason::None);
        Check(latch.Reason(revised) == FallbackReason::None,
            "recording no failure also clears stale eligibility state");
    }

    void TestCompactMaterialFeatureSlice()
    {
        using donut::engine::MaterialDomain;

        const CompactSceneBuildParameters defaultParameters;
        Check(!defaultParameters.supportAlphaTestedMaterials,
            "compact scene alpha admission is fail-closed by default");

        MaterialConstants constants{};
        Check(ValidateCompactMaterialFeatureSlice(
                constants,
                MaterialDomain::Opaque,
                false,
                false) == FallbackReason::None,
            "the textureless opaque metallic-roughness baseline is admitted");

        constants.flags = MaterialFlags_UseBaseOrDiffuseTexture;
        Check(ValidateCompactMaterialFeatureSlice(
                constants,
                MaterialDomain::Opaque,
                false,
                false) == FallbackReason::UnsupportedMaterial &&
            ValidateCompactMaterialFeatureSlice(
                constants,
                MaterialDomain::Opaque,
                false,
                true) == FallbackReason::None,
            "shader-visible textures require an active qualification lease");
        constants.flags = 0u;

        Check(ValidateCompactMaterialFeatureSlice(
                constants,
                MaterialDomain::AlphaTested,
                false,
                true) == FallbackReason::UnsupportedMaterial &&
            ValidateCompactMaterialFeatureSlice(
                constants,
                MaterialDomain::AlphaTested,
                true,
                true) == FallbackReason::None,
            "alpha testing remains an explicit caller-controlled admission");

        const std::array<uint32_t, 4u> unsupportedFlags = {
            MaterialFlags_Hair,
            MaterialFlags_DoubleSided,
            MaterialFlags_UseSpecularGlossModel,
            MaterialFlags_SubsurfaceScattering
        };
        for (uint32_t flag : unsupportedFlags)
        {
            constants.flags = flag;
            Check(ValidateCompactMaterialFeatureSlice(
                    constants,
                    MaterialDomain::Opaque,
                    false,
                    true) == FallbackReason::UnsupportedMaterial,
                "every non-baseline BRDF/material flag stays on the full G-buffer path");
        }
        constants.flags = 0u;
        constants.transmissionFactor = 0.25f;
        Check(ValidateCompactMaterialFeatureSlice(
                constants,
                MaterialDomain::Opaque,
                false,
                true) == FallbackReason::UnsupportedMaterial,
            "nonzero transmission stays on the full G-buffer path");
        constants.transmissionFactor = 0.f;
        constants.roughness = std::numeric_limits<float>::infinity();
        Check(ValidateCompactMaterialFeatureSlice(
                constants,
                MaterialDomain::Opaque,
                false,
                true) == FallbackReason::UnsupportedMaterial,
            "nonfinite shader-visible material constants fail closed");
    }

    void TestFiniteMaterialTextureFormats()
    {
        Check(IsCompactFiniteMaterialTextureFormat(
                    nvrhi::Format::RGBA8_UNORM) &&
                IsCompactFiniteMaterialTextureFormat(
                    nvrhi::Format::BC7_UNORM_SRGB),
            "normalized material textures have finite decoded float texels");
        Check(!IsCompactFiniteMaterialTextureFormat(
                    nvrhi::Format::R8_UINT) &&
                !IsCompactFiniteMaterialTextureFormat(
                    nvrhi::Format::R16_FLOAT) &&
                !IsCompactFiniteMaterialTextureFormat(
                    nvrhi::Format::RGBA32_FLOAT) &&
                !IsCompactFiniteMaterialTextureFormat(
                    nvrhi::Format::BC6H_UFLOAT) &&
                !IsCompactFiniteMaterialTextureFormat(
                    nvrhi::Format::D32) &&
                !IsCompactFiniteMaterialTextureFormat(
                    nvrhi::Format::UNKNOWN) &&
                !IsCompactFiniteMaterialTextureFormat(
                    nvrhi::Format::COUNT),
            "integer, unproven floating, depth, and invalid texture formats require the full G-buffer payload");
    }

    void TestMaterialTextureDescriptions()
    {
        nvrhi::TextureDesc description;
        description.width = 64u;
        description.height = 32u;
        description.depth = 1u;
        description.arraySize = 1u;
        description.mipLevels = 7u;
        description.sampleCount = 1u;
        description.sampleQuality = 0u;
        description.dimension = nvrhi::TextureDimension::Texture2D;
        description.format = nvrhi::Format::RGBA8_UNORM;
        description.isShaderResource = true;
        const nvrhi::FormatSupport sampled =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderSample;
        Check(ValidateCompactMaterialTexture(description, sampled) ==
                FallbackReason::None,
            "a finite normalized mipmapped Texture2D is compact-sampleable");

        nvrhi::TextureDesc changed = description;
        changed.dimension = nvrhi::TextureDimension::Texture2DArray;
        Check(ValidateCompactMaterialTexture(changed, sampled) ==
                FallbackReason::FormatUnsupported,
            "array material textures remain on the local full G-buffer path");
        changed = description;
        changed.sampleCount = 2u;
        changed.dimension = nvrhi::TextureDimension::Texture2DMS;
        Check(ValidateCompactMaterialTexture(changed, sampled) ==
                FallbackReason::FormatUnsupported,
            "multisampled material textures cannot enter the bindless Texture2D array");
        changed = description;
        changed.format = nvrhi::Format::RGBA16_FLOAT;
        Check(ValidateCompactMaterialTexture(changed, sampled) ==
                FallbackReason::FormatUnsupported,
            "unbounded floating-point texels fail the finite reconstruction contract");
        changed = description;
        changed.isShaderResource = false;
        Check(ValidateCompactMaterialTexture(changed, sampled) ==
                FallbackReason::FormatUnsupported,
            "a texture without an SRV fails locally before descriptor emission");
        changed = description;
        changed.isRenderTarget = true;
        Check(ValidateCompactMaterialTexture(changed, sampled) ==
                FallbackReason::FormatUnsupported,
            "render-target material textures remain mutable and require the local full G-buffer");
#if UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC
        Check(ValidateCompactMaterialTexture(
                changed,
                sampled,
                true) == FallbackReason::None,
            "the 4x proof admits only an explicitly finalized load-time-mip render-target-capable SRV");
        changed.mipLevels = 1u;
        Check(ValidateCompactMaterialTexture(
                changed,
                sampled,
                true) == FallbackReason::FormatUnsupported,
            "a single-mip render-target resource cannot claim load-time mip finalization");
#else
        Check(ValidateCompactMaterialTexture(
                changed,
                sampled,
                true) == FallbackReason::FormatUnsupported,
            "the default build ignores an unqualified render-target proof bit");
#endif
        changed = description;
        changed.isUAV = true;
        Check(ValidateCompactMaterialTexture(changed, sampled) ==
                FallbackReason::FormatUnsupported,
            "unordered-access material textures remain mutable and require the local full G-buffer");
        changed = description;
        changed.isVirtual = true;
        Check(ValidateCompactMaterialTexture(changed, sampled) ==
                FallbackReason::FormatUnsupported,
            "virtual material textures lack an immutable compact content contract");
        changed = description;
        changed.isTiled = true;
        Check(ValidateCompactMaterialTexture(changed, sampled) ==
                FallbackReason::FormatUnsupported,
            "tiled material textures lack an immutable compact content contract");
        changed = description;
        changed.sharedResourceFlags = nvrhi::SharedResourceFlags::Shared;
        Check(ValidateCompactMaterialTexture(changed, sampled) ==
                FallbackReason::FormatUnsupported,
            "shared material textures remain outside the immutable compact snapshot");
        Check(ValidateCompactMaterialTexture(
                description,
                nvrhi::FormatSupport::Texture) ==
                    FallbackReason::FormatUnsupported,
            "a format without filtered sampling support fails locally");
    }

    void TestMaterialTextureSemanticAllowlist()
    {
        using Semantic = CompactMaterialTextureSemantic;
        Check(IsCompactMaterialTextureSemanticFormatSupported(
                Semantic::BaseOrDiffuse,
                nvrhi::Format::SRGBA8_UNORM) &&
                IsCompactMaterialTextureSemanticFormatSupported(
                    Semantic::BaseOrDiffuse,
                    nvrhi::Format::BC1_UNORM_SRGB) &&
                !IsCompactMaterialTextureSemanticFormatSupported(
                    Semantic::BaseOrDiffuse,
                    nvrhi::Format::RGBA8_UNORM),
            "the future qualified base-color slice admits only its explicit sRGB formats");
        Check(IsCompactMaterialTextureSemanticFormatSupported(
                Semantic::MetalRoughOrSpecular,
                nvrhi::Format::RG8_UNORM) &&
                IsCompactMaterialTextureSemanticFormatSupported(
                    Semantic::MetalRoughOrSpecular,
                    nvrhi::Format::BC5_UNORM) &&
                IsCompactMaterialTextureSemanticFormatSupported(
                    Semantic::MetalRoughOrSpecular,
                    nvrhi::Format::RGBA8_UNORM) ==
                        bool(UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC),
            "RGBA metal-roughness is admitted only by the default-off 4x proof");
        Check(IsCompactMaterialTextureSemanticFormatSupported(
                Semantic::Emissive,
                nvrhi::Format::SRGBA8_UNORM) &&
                IsCompactMaterialTextureSemanticFormatSupported(
                    Semantic::Emissive,
                    nvrhi::Format::BC7_UNORM_SRGB),
            "the future qualified emissive slice has an explicit sRGB allowlist");
        Check(IsCompactMaterialTextureSemanticFormatSupported(
                Semantic::Occlusion,
                nvrhi::Format::R8_UNORM) &&
                IsCompactMaterialTextureSemanticFormatSupported(
                    Semantic::Occlusion,
                    nvrhi::Format::BC4_UNORM),
            "the future qualified occlusion slice has an explicit one-channel allowlist");
        Check(!IsCompactMaterialTextureSemanticFormatSupported(
                Semantic::Normal,
                nvrhi::Format::BC5_UNORM) &&
                IsCompactMaterialTextureSemanticFormatSupported(
                    Semantic::Normal,
                    nvrhi::Format::RGBA8_UNORM) ==
                        bool(UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC) &&
                !IsCompactMaterialTextureSemanticFormatSupported(
                    Semantic::Transmission,
                    nvrhi::Format::BC4_UNORM) &&
                !IsCompactMaterialTextureSemanticFormatSupported(
                    Semantic::Opacity,
                    nvrhi::Format::BC3_UNORM_SRGB),
            "only the 4x proof admits RGBA normals; BC5 normal, transmission, and alpha remain closed");

        nvrhi::TextureDesc description;
        description.width = 16u;
        description.height = 16u;
        description.mipLevels = 5u;
        description.format = nvrhi::Format::SRGBA8_UNORM;
        const nvrhi::FormatSupport sampled =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderSample;
        Check(ValidateCompactMaterialTextureForSemantic(
                Semantic::BaseOrDiffuse,
                description,
                sampled) == FallbackReason::None &&
                ValidateCompactMaterialTextureForSemantic(
                    Semantic::Normal,
                    description,
                    sampled) == FallbackReason::FormatUnsupported,
            "resource validity cannot bypass a closed texture semantic");
        description.isUAV = true;
        Check(ValidateCompactMaterialTextureForSemantic(
                Semantic::BaseOrDiffuse,
                description,
                sampled) == FallbackReason::FormatUnsupported,
            "the semantic allowlist cannot bypass mutable-resource rejection");
    }

    void TestDenseDescriptorPlanning()
    {
        CompactDescriptorSlotPlanner planner(6u);
        uint32_t firstTexture = InvalidVisibilityWord;
        uint32_t reusedTexture = InvalidVisibilityWord;
        uint32_t secondTexture = InvalidVisibilityWord;
        uint32_t firstRaw = InvalidVisibilityWord;
        uint32_t reusedRaw = InvalidVisibilityWord;
        Check(planner.AddTexture(0x100u, firstTexture) ==
                    FallbackReason::None &&
                planner.AddTexture(0x100u, reusedTexture) ==
                    FallbackReason::None &&
                planner.AddTexture(0x200u, secondTexture) ==
                    FallbackReason::None &&
                firstTexture == 0u && reusedTexture == 0u &&
                secondTexture == 1u,
            "pointer-equal material resources reuse one dense texture slot");
        Check(planner.AddRawBuffer(0x300u, firstRaw) ==
                    FallbackReason::None &&
                planner.AddRawBuffer(0x300u, reusedRaw) ==
                    FallbackReason::None &&
                firstRaw == 2u && reusedRaw == 2u &&
                planner.TextureCount() == 2u &&
                planner.RawBufferCount() == 1u &&
                planner.DescriptorCount() == 3u &&
                planner.ValidatePartition() == FallbackReason::None,
            "raw slots follow the exact dense texture prefix and also deduplicate");

        uint32_t rejected = InvalidVisibilityWord;
        Check(planner.AddTexture(0x400u, rejected) ==
                FallbackReason::TransientMappingFailure,
            "texture planning cannot resume after the raw descriptor phase");
        Check(CompactDescriptorSlotPlanner(4u).AddTexture(
                0u, rejected) == FallbackReason::UnsupportedMaterial &&
                CompactDescriptorSlotPlanner(4u).AddRawBuffer(
                    0u, rejected) == FallbackReason::ReconstructionFailure,
            "missing texture and raw resource identities fail with distinct local reasons");

        // One accepted texture leaves room for three mandatory raw resources
        // at capacity four. A later material needs two new textures, so its
        // transaction is discarded before the raw phase begins.
        CompactDescriptorSlotPlanner reserved(4u);
        Check(reserved.AddTexture(0x10u, firstTexture) ==
                FallbackReason::None,
            "near-capacity fixture accepts its first material texture");
        CompactDescriptorSlotPlanner materialCandidate = reserved;
        Check(materialCandidate.AddTexture(0x20u, secondTexture) ==
                    FallbackReason::None &&
                materialCandidate.AddTexture(0x30u, rejected) ==
                    FallbackReason::None &&
                materialCandidate.DescriptorCount() + 3u >
                    materialCandidate.Capacity(),
            "an atomic material plan detects that new textures would consume reserved raw capacity");
        uint32_t raw0 = InvalidVisibilityWord;
        uint32_t raw1 = InvalidVisibilityWord;
        uint32_t raw2 = InvalidVisibilityWord;
        Check(reserved.AddRawBuffer(0x40u, raw0) ==
                    FallbackReason::None &&
                reserved.AddRawBuffer(0x50u, raw1) ==
                    FallbackReason::None &&
                reserved.AddRawBuffer(0x60u, raw2) ==
                    FallbackReason::None &&
                raw0 == 1u && raw1 == 2u && raw2 == 3u &&
                reserved.ValidatePartition() == FallbackReason::None,
            "discarding the oversized material leaves every reserved raw slot writable and type-partitioned");
        Check(reserved.AddRawBuffer(0x70u, rejected) ==
                FallbackReason::CapacityOverflow,
            "the planner reports exact descriptor-capacity overflow without mutating the full plan");
    }

    std::array<int32_t, CompactMaterialTextureSemanticCount>
    ReadMaterialTextureIndices(const MaterialConstants& constants)
    {
        return {
            constants.baseOrDiffuseTextureIndex,
            constants.metalRoughOrSpecularTextureIndex,
            constants.emissiveTextureIndex,
            constants.normalTextureIndex,
            constants.occlusionTextureIndex,
            constants.transmissionTextureIndex,
            constants.opacityTextureIndex
        };
    }

    void TestMaterialConstantTextureRemapping()
    {
        using donut::engine::LoadedTexture;
        using donut::engine::Material;

        std::array<std::shared_ptr<LoadedTexture>,
            CompactMaterialTextureSemanticCount> textures;
        for (std::shared_ptr<LoadedTexture>& texture : textures)
            texture = std::make_shared<LoadedTexture>();

        Material material;
        material.baseOrDiffuseTexture = textures[0u];
        material.metalRoughOrSpecularTexture = textures[1u];
        material.emissiveTexture = textures[2u];
        material.normalTexture = textures[3u];
        material.occlusionTexture = textures[4u];
        material.transmissionTexture = textures[5u];
        material.opacityTexture = textures[6u];

        const std::array<uintptr_t,
            CompactMaterialTextureSemanticCount> uniqueIdentities = {
                0x101u, 0x202u, 0x303u, 0x404u,
                0x505u, 0x606u, 0x707u
            };
        MaterialConstants constants{};
        material.FillConstantBuffer(constants, false);
        CompactDescriptorSlotPlanner planner(11u);
        CompactMaterialTextureDescriptorRecord record;
        Check(PlanCompactMaterialTextureDescriptors(
                constants,
                uniqueIdentities,
                planner,
                record) == FallbackReason::None &&
                ReadMaterialTextureIndices(constants) ==
                    std::array<int32_t,
                        CompactMaterialTextureSemanticCount>{
                            0, 1, 2, 3, 4, 5, 6 } &&
                record.descriptors == ReadMaterialTextureIndices(constants) &&
                record.enabledMask == 0x7fu &&
                planner.TextureCount() == 7u,
            "the production remapper replaces every FillConstantBuffer texture index with its dense compact slot");

        uint32_t rawInstance = InvalidVisibilityWord;
        uint32_t rawIndex = InvalidVisibilityWord;
        uint32_t rawVertex = InvalidVisibilityWord;
        uint32_t rawMaterial = InvalidVisibilityWord;
        Check(planner.AddRawBuffer(0x808u, rawInstance) ==
                    FallbackReason::None &&
                planner.AddRawBuffer(0x909u, rawIndex) ==
                    FallbackReason::None &&
                planner.AddRawBuffer(0xa0au, rawVertex) ==
                    FallbackReason::None &&
                planner.AddRawBuffer(0xb0bu, rawMaterial) ==
                    FallbackReason::None &&
                rawInstance == 7u && rawIndex == 8u &&
                rawVertex == 9u && rawMaterial == 10u &&
                planner.ValidatePartition() == FallbackReason::None,
            "all seven material slots leave the four production raw resources in the exact dense suffix");

        material.enableTransmissionTexture = false;
        MaterialConstants disabledConstants{};
        material.FillConstantBuffer(disabledConstants, false);
        CompactDescriptorSlotPlanner disabledPlanner(6u);
        CompactMaterialTextureDescriptorRecord disabledRecord;
        Check(PlanCompactMaterialTextureDescriptors(
                disabledConstants,
                uniqueIdentities,
                disabledPlanner,
                disabledRecord) == FallbackReason::None &&
                ReadMaterialTextureIndices(disabledConstants) ==
                    std::array<int32_t,
                        CompactMaterialTextureSemanticCount>{
                            0, 1, 2, 3, 4, -1, 5 } &&
                disabledRecord.enabledMask == (0x7fu & ~(1u << 5u)),
            "a disabled-but-present texture is reset to -1 and consumes no compact descriptor");
        material.enableTransmissionTexture = true;

        MaterialConstants sharedConstants{};
        material.FillConstantBuffer(sharedConstants, false);
        auto sharedIdentities = uniqueIdentities;
        sharedIdentities[3u] = sharedIdentities[0u];
        CompactDescriptorSlotPlanner sharedPlanner(6u);
        CompactMaterialTextureDescriptorRecord sharedRecord;
        Check(PlanCompactMaterialTextureDescriptors(
                sharedConstants,
                sharedIdentities,
                sharedPlanner,
                sharedRecord) == FallbackReason::None &&
                ReadMaterialTextureIndices(sharedConstants) ==
                    std::array<int32_t,
                        CompactMaterialTextureSemanticCount>{
                            0, 1, 2, 0, 3, 4, 5 } &&
                sharedPlanner.TextureCount() == 6u,
            "pointer-equal FillConstantBuffer texture semantics reuse one exact compact slot");

        MaterialConstants overflowConstants{};
        material.FillConstantBuffer(overflowConstants, false);
        const auto overflowBefore = ReadMaterialTextureIndices(
            overflowConstants);
        CompactDescriptorSlotPlanner overflowPlanner(6u);
        CompactMaterialTextureDescriptorRecord overflowRecord;
        overflowRecord.enabledMask = 0x55u;
        Check(PlanCompactMaterialTextureDescriptors(
                overflowConstants,
                uniqueIdentities,
                overflowPlanner,
                overflowRecord) == FallbackReason::CapacityOverflow &&
                ReadMaterialTextureIndices(overflowConstants) ==
                    overflowBefore &&
                overflowPlanner.DescriptorCount() == 0u &&
                overflowRecord.enabledMask == 0x55u,
            "seven enabled FillConstantBuffer slots demote atomically when compact descriptor capacity is six");

        MaterialConstants missingConstants{};
        material.FillConstantBuffer(missingConstants, false);
        const auto missingBefore = ReadMaterialTextureIndices(
            missingConstants);
        auto missingIdentities = uniqueIdentities;
        missingIdentities[6u] = 0u;
        CompactDescriptorSlotPlanner missingPlanner(7u);
        CompactMaterialTextureDescriptorRecord missingRecord;
        Check(PlanCompactMaterialTextureDescriptors(
                missingConstants,
                missingIdentities,
                missingPlanner,
                missingRecord) == FallbackReason::UnsupportedMaterial &&
                ReadMaterialTextureIndices(missingConstants) ==
                    missingBefore &&
                missingPlanner.DescriptorCount() == 0u,
            "a missing enabled FillConstantBuffer resource identity fails without publishing a partial remap");
    }

    void TestLosslessMaterialFingerprint()
    {
        using donut::engine::LoadedTexture;
        using donut::engine::Material;

        auto material = std::make_shared<Material>();
        material->baseOrDiffuseColor = { 0.2f, 0.3f, 0.4f };
        material->specularColor = { 0.5f, 0.6f, 0.7f };
        material->emissiveColor = { 0.1f, 0.15f, 0.2f };
        material->emissiveIntensity = 2.f;
        material->metalness = 0.25f;
        material->roughness = 0.45f;
        material->normalTextureScale = 0.75f;
        material->occlusionStrength = 0.8f;
        material->transmissionFactor = 0.05f;
        material->normalTextureTransformScale = { 1.5f, 0.5f };
        material->materialID = 17;
        material->enableSubsurfaceScattering = true;
        material->subsurface.transmissionColor = { 0.2f, 0.4f, 0.6f };
        material->subsurface.scatteringColor = { 0.3f, 0.5f, 0.7f };
        material->subsurface.scale = 1.25f;
        material->subsurface.anisotropy = 0.2f;
        material->enableHair = true;
        material->hair.baseColor = { 0.15f, 0.25f, 0.35f };
        material->hair.melanin = 0.4f;
        material->hair.melaninRedness = 0.3f;
        material->hair.longitudinalRoughness = 0.35f;
        material->hair.azimuthalRoughness = 0.55f;
        material->hair.ior = 1.45f;
        material->hair.cuticleAngle = 4.f;
        material->hair.diffuseReflectionTint = { 0.2f, 0.1f, 0.3f };
        material->hair.diffuseReflectionWeight = 0.15f;

        MaterialConstants finalConstants;
        std::memset(&finalConstants, 0, sizeof(finalConstants));
        material->FillConstantBuffer(finalConstants, false);
        CompactMaterialTextureDescriptorRecord descriptors;
        CompactMaterialFingerprint fingerprint;
        const std::shared_ptr<const Material> materialView = material;
        Check(CaptureCompactMaterialFingerprint(
                materialView,
                finalConstants,
                descriptors,
                fingerprint,
                {}) == FallbackReason::None &&
                fingerprint.materialKeepAlive.get() == material.get() &&
                fingerprint.enabledTextureMask == 0u,
            "the fingerprint captures the exact already-written textureless material record and retains its owner");
        Check(ValidateCompactMaterialFingerprint(
                fingerprint,
                materialView,
                descriptors,
                {}) == FallbackReason::None,
            "an unchanged material validates bit-for-bit");

        MaterialConstants transientIndices = finalConstants;
        transientIndices.baseOrDiffuseTextureIndex = 101;
        transientIndices.metalRoughOrSpecularTextureIndex = 102;
        transientIndices.emissiveTextureIndex = 103;
        transientIndices.normalTextureIndex = 104;
        transientIndices.occlusionTextureIndex = 105;
        transientIndices.transmissionTextureIndex = 106;
        transientIndices.opacityTextureIndex = 107;
        transientIndices.padding1 = { 11u, 12u, 13u };
        Check(CanonicalizeCompactMaterialConstants(transientIndices) ==
                CanonicalizeCompactMaterialConstants(finalConstants),
            "snapshot-local bindless indices and padding are the only neutralized material words");
        MaterialConstants signedZero = finalConstants;
        signedZero.baseOrDiffuseColor.x = -0.f;
        Check(CanonicalizeCompactMaterialConstants(signedZero) !=
                CanonicalizeCompactMaterialConstants(finalConstants),
            "canonical material words preserve signed zero instead of numerically normalizing it");

        const Material baseline = *material;
        const std::vector<std::pair<
            const char*,
            std::function<void(Material&)>>> mutations = {
            { "base color", [](Material& value) { value.baseOrDiffuseColor.x += 0.01f; } },
            { "specular color", [](Material& value) { value.specularColor.y += 0.01f; } },
            { "emissive color", [](Material& value) { value.emissiveColor.z += 0.01f; } },
            { "emissive intensity", [](Material& value) { value.emissiveIntensity += 0.01f; } },
            { "metalness", [](Material& value) { value.metalness += 0.01f; } },
            { "roughness", [](Material& value) { value.roughness += 0.01f; } },
            { "normal scale", [](Material& value) { value.normalTextureScale += 0.01f; } },
            { "occlusion strength", [](Material& value) { value.occlusionStrength += 0.01f; } },
            { "transmission factor", [](Material& value) { value.transmissionFactor += 0.01f; } },
            { "normal uv x", [](Material& value) { value.normalTextureTransformScale.x += 0.01f; } },
            { "normal uv y", [](Material& value) { value.normalTextureTransformScale.y += 0.01f; } },
            { "material id", [](Material& value) { ++value.materialID; } },
            { "domain", [](Material& value) { value.domain = donut::engine::MaterialDomain::AlphaTested; } },
            { "specular gloss flag", [](Material& value) { value.useSpecularGlossModel = true; } },
            { "double sided flag", [](Material& value) { value.doubleSided = true; } },
            { "metalness channel flag", [](Material& value) { value.metalnessInRedChannel = true; } },
            { "subsurface flag", [](Material& value) { value.enableSubsurfaceScattering = false; } },
            { "subsurface transmission", [](Material& value) { value.subsurface.transmissionColor.x += 0.01f; } },
            { "subsurface scattering", [](Material& value) { value.subsurface.scatteringColor.y += 0.01f; } },
            { "subsurface scale", [](Material& value) { value.subsurface.scale += 0.01f; } },
            { "subsurface anisotropy", [](Material& value) { value.subsurface.anisotropy += 0.01f; } },
            { "hair flag", [](Material& value) { value.enableHair = false; } },
            { "hair base color", [](Material& value) { value.hair.baseColor.z += 0.01f; } },
            { "hair melanin", [](Material& value) { value.hair.melanin += 0.01f; } },
            { "hair redness", [](Material& value) { value.hair.melaninRedness += 0.01f; } },
            { "hair longitudinal roughness", [](Material& value) { value.hair.longitudinalRoughness += 0.01f; } },
            { "hair azimuthal roughness", [](Material& value) { value.hair.azimuthalRoughness += 0.01f; } },
            { "hair ior", [](Material& value) { value.hair.ior += 0.01f; } },
            { "hair cuticle angle", [](Material& value) { value.hair.cuticleAngle += 0.01f; } },
            { "hair tint", [](Material& value) { value.hair.diffuseReflectionTint.x += 0.01f; } },
            { "hair reflection weight", [](Material& value) { value.hair.diffuseReflectionWeight += 0.01f; } }
        };
        for (const auto& [name, mutate] : mutations)
        {
            *material = baseline;
            mutate(*material);
            Check(ValidateCompactMaterialFingerprint(
                    fingerprint,
                    materialView,
                    descriptors,
                    {}) ==
                        FallbackReason::MaterialRevisionMismatch,
                std::string("a live shader-visible ") + name +
                    " mutation invalidates the immutable material record");
        }
        *material = baseline;

        material->opacity = 0.25f;
        material->alphaCutoff = 0.1f;
        material->dirty = !material->dirty;
        Check(ValidateCompactMaterialFingerprint(
                fingerprint,
                materialView,
                descriptors,
                {}) == FallbackReason::None,
            "opaque-only source opacity, alpha cutoff, and the cooperative dirty bit are ignored when FillConstantBuffer normalizes them away");
        *material = baseline;

        const std::array<
            std::function<void(Material&)>,
            CompactMaterialTextureSemanticCount> enableTextures = {
            [](Material& value) { value.baseOrDiffuseTexture = std::make_shared<LoadedTexture>(); },
            [](Material& value) { value.metalRoughOrSpecularTexture = std::make_shared<LoadedTexture>(); },
            [](Material& value) { value.emissiveTexture = std::make_shared<LoadedTexture>(); },
            [](Material& value) { value.normalTexture = std::make_shared<LoadedTexture>(); },
            [](Material& value) { value.occlusionTexture = std::make_shared<LoadedTexture>(); },
            [](Material& value) { value.transmissionTexture = std::make_shared<LoadedTexture>(); },
            [](Material& value) { value.opacityTexture = std::make_shared<LoadedTexture>(); }
        };
        for (const auto& enableTexture : enableTextures)
        {
            *material = baseline;
            enableTexture(*material);
            Check(ValidateCompactMaterialFingerprint(
                    fingerprint,
                    materialView,
                    descriptors,
                    {}) ==
                        FallbackReason::MaterialRevisionMismatch,
                "every newly enabled texture semantic changes the canonical material flags before resource access");
        }
        *material = baseline;

        auto replacement = std::make_shared<Material>(*material);
        const std::shared_ptr<const Material> replacementView = replacement;
        Check(ValidateCompactMaterialFingerprint(
                fingerprint,
                replacementView,
                descriptors,
                {}) ==
                    FallbackReason::MaterialRevisionMismatch,
            "an exact-value material object replacement cannot alias the retained material identity");

        MaterialConstants wrongFinal = finalConstants;
        wrongFinal.roughness += 0.01f;
        CompactMaterialFingerprint rejectedCapture;
        Check(CaptureCompactMaterialFingerprint(
                materialView,
                wrongFinal,
                descriptors,
                rejectedCapture,
                {}) ==
                    FallbackReason::MaterialRevisionMismatch,
            "capture cannot bless a final GPU record that differs from a fresh live material fill");

        auto disabledTextureMaterial = std::make_shared<Material>();
        disabledTextureMaterial->enableBaseOrDiffuseTexture = false;
        disabledTextureMaterial->baseOrDiffuseTexture =
            std::make_shared<LoadedTexture>();
        MaterialConstants disabledConstants;
        std::memset(&disabledConstants, 0, sizeof(disabledConstants));
        disabledTextureMaterial->FillConstantBuffer(
            disabledConstants,
            false);
        CompactDescriptorSlotPlanner disabledPlanner(1u);
        CompactMaterialTextureDescriptorRecord disabledDescriptors;
        const std::array<
            uintptr_t,
            CompactMaterialTextureSemanticCount> disabledIdentities{};
        Check(PlanCompactMaterialTextureDescriptors(
                disabledConstants,
                disabledIdentities,
                disabledPlanner,
                disabledDescriptors) == FallbackReason::None,
            "descriptor planning neutralizes transient indices for disabled texture objects");
        CompactMaterialFingerprint disabledFingerprint;
        const std::shared_ptr<const Material> disabledMaterialView =
            disabledTextureMaterial;
        Check(CaptureCompactMaterialFingerprint(
                disabledMaterialView,
                disabledConstants,
                disabledDescriptors,
                disabledFingerprint,
                {}) == FallbackReason::None,
            "a disabled-but-present texture has no shader-visible identity");
        disabledTextureMaterial->baseOrDiffuseTexture =
            std::make_shared<LoadedTexture>();
        Check(ValidateCompactMaterialFingerprint(
                disabledFingerprint,
                disabledMaterialView,
                disabledDescriptors,
                {}) == FallbackReason::None,
            "replacing a disabled texture object does not invalidate a material that cannot sample it");
    }

    void TestGeometryValidation()
    {
        const CompactGeometryValidationDesc valid = MakeValidGeometry();
        Check(ValidateCompactGeometry(valid) == FallbackReason::None,
            "a fully bounded indexed triangle geometry is eligible");

        CompactGeometryValidationDesc changed = valid;
        changed.geometrySupported = false;
        changed.deformationSupported = false;
        changed.materialSupported = false;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::ReconstructionFailure,
            "geometry support is the stable first validation failure");

        changed = valid;
        changed.deformationSupported = false;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::UnsupportedDeformation,
            "unsupported deformation rejects compact reconstruction");
        Check(ClassifyCompactGeometryPayload(changed).payloadClass ==
                CompactGeometryPayloadClass::FullGBufferFallback &&
                ClassifyCompactGeometryPayload(changed).reason ==
                    FallbackReason::UnsupportedDeformation,
            "unsupported deformation selects the full G-buffer payload");

        changed = valid;
        changed.materialSupported = false;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::UnsupportedMaterial,
            "unsupported material rejects compact reconstruction");

        changed = valid;
        changed.indexBufferAvailable = false;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::ReconstructionFailure,
            "a missing raw index stream fails closed");

        changed = valid;
        changed.vertexBufferAvailable = false;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::ReconstructionFailure,
            "a missing raw vertex stream fails closed");

        changed = valid;
        changed.instanceIndex = changed.instanceCount;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::TransientMappingFailure,
            "out-of-range scene instance mapping is rejected");
        Check(!ClassifyCompactGeometryPayload(changed).Succeeded(),
            "mapping failures never become expected per-draw fallback");

        changed = valid;
        changed.flattenedHandleIndex = changed.handleMapCount;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::TransientMappingFailure,
            "out-of-range flattened geometry mapping is rejected");

        changed = valid;
        changed.materialIndex = changed.materialCount;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::TransientMappingFailure,
            "out-of-range material mapping is rejected");

        changed = valid;
        changed.indexCount = 4u;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::ReconstructionFailure,
            "non-triangle index counts are rejected");

        changed = valid;
        changed.indexCount = uint64_t(InvalidVisibilityWord) * 3u;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::PrimitiveOverflow,
            "the primitive sentinel cannot be represented as a real hit");

        changed = valid;
        changed.positionRange.byteSize = 12u;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::ReconstructionFailure,
            "attribute reads cannot leave their declared stream range");

        changed = valid;
        changed.positionRange = {
            std::numeric_limits<uint64_t>::max() - 3u,
            16u
        };
        changed.vertexBufferByteSize =
            std::numeric_limits<uint64_t>::max();
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::CapacityOverflow,
            "overflowing byte-range arithmetic is rejected before narrowing");

        changed = valid;
        changed.positionRange = { 0xfffffff8ull, 64u };
        changed.vertexBufferByteSize = 0x100000038ull;
        changed.firstVertex = 0u;
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::CapacityOverflow,
            "GPU byte addresses beyond uint32 range fail losslessly");

        changed = valid;
        changed.previousPositionRequired = true;
        changed.previousPositionRange = {};
        Check(ValidateCompactGeometry(changed) ==
                FallbackReason::UnsupportedDeformation,
            "skinned reconstruction requires a valid previous-position stream");
    }

    void TestWholeFrameEligibility()
    {
        std::array<CompactGeometryValidationDesc, 3> geometries = {{
            MakeValidGeometry(),
            MakeValidGeometry(),
            MakeValidGeometry()
        }};

        CompactScenePartitionSummary summary;
        CompactSceneEligibility result = EvaluateCompactSceneEligibility(
            geometries.data(), geometries.size(), 12u, 12u, 3u,
            &summary);
        Check(result.Succeeded() &&
                result.failingGeometry == InvalidVisibilityWord &&
                summary.identityGeometryCount == 3u &&
                summary.reconstructGeometryCount == 3u &&
                summary.fallbackGeometryCount == 0u &&
                summary.firstFallbackReason == FallbackReason::None,
            "the complete supported scene reconstructs at exact capacities");

        geometries[1].materialSupported = false;
        geometries[2].deformationSupported = false;
        result = EvaluateCompactSceneEligibility(
            geometries.data(), geometries.size(), 12u, 12u, 3u,
            &summary);
        Check(result.Succeeded() && summary.IsMixed() &&
                summary.reconstructGeometryCount == 1u &&
                summary.fallbackGeometryCount == 2u &&
                summary.firstFallbackReason ==
                    FallbackReason::UnsupportedMaterial,
            "expected material and deformation variants form one mixed scene partition");

        geometries[0].materialSupported = false;
        result = EvaluateCompactSceneEligibility(
            geometries.data(), geometries.size(), 2u, 12u, 3u,
            &summary);
        Check(result.Succeeded() && summary.IsAllFallback() &&
                summary.reconstructGeometryCount == 0u &&
                summary.fallbackGeometryCount == 3u,
            "an all-fallback scene remains a valid identity snapshot");

        geometries[0] = MakeValidGeometry();
        geometries[1] = MakeValidGeometry();
        geometries[2] = MakeValidGeometry();
        geometries[1].flattenedHandleIndex =
            geometries[1].handleMapCount;
        result = EvaluateCompactSceneEligibility(
            geometries.data(), geometries.size(), 2u, 12u, 3u,
            &summary);
        Check(!result.Succeeded() &&
                result.reason == FallbackReason::TransientMappingFailure &&
                result.failingGeometry == 1u,
            "mapping corruption remains fatal instead of entering the fallback partition");

        geometries[1] = MakeValidGeometry();

        result = EvaluateCompactSceneEligibility(
            geometries.data(), geometries.size(), 13u, 12u, 3u);
        Check(result.reason == FallbackReason::CapacityOverflow &&
                result.failingGeometry == InvalidVisibilityWord,
            "descriptor overflow fails the whole frame before partial traversal");

        const uint64_t unrepresentableDescriptorCount =
            uint64_t(std::numeric_limits<int>::max()) + 1u;
        result = EvaluateCompactSceneEligibility(
            geometries.data(),
            geometries.size(),
            unrepresentableDescriptorCount,
            unrepresentableDescriptorCount,
            3u);
        Check(result.reason == FallbackReason::CapacityOverflow,
            "descriptor indices cannot exceed the supported signed slot range");

        result = EvaluateCompactSceneEligibility(
            geometries.data(), geometries.size(), 1u, 12u, 2u);
        Check(result.reason == FallbackReason::CapacityOverflow,
            "geometry-instance overflow fails the whole frame");

        result = EvaluateCompactSceneEligibility(
            nullptr, 0u, 0u, 12u, 3u);
        Check(result.reason == FallbackReason::ReconstructionFailure,
            "an empty or missing scene cannot publish a compact snapshot");
    }

    void TestStableInstanceHandles()
    {
        struct Owner
        {
            uint32_t first = 0u;
            uint32_t second = 0u;
        };

        StableInstanceHandleAllocator allocator;
        auto firstOwner = std::make_shared<Owner>();
        auto secondOwner = std::make_shared<Owner>();
        std::shared_ptr<const void> firstToken(
            firstOwner,
            static_cast<const void*>(&firstOwner->first));
        std::shared_ptr<const void> firstAlias(
            firstOwner,
            static_cast<const void*>(&firstOwner->second));
        std::shared_ptr<const void> secondToken(
            secondOwner,
            static_cast<const void*>(secondOwner.get()));

        const StableInstanceHandleResult first =
            allocator.Resolve(firstToken);
        const StableInstanceHandleResult alias =
            allocator.Resolve(firstAlias);
        const StableInstanceHandleResult second =
            allocator.Resolve(secondToken);
        Check(first.Succeeded() && alias.Succeeded() &&
                first.stableInstanceHandle == 1u &&
                alias.stableInstanceHandle == first.stableInstanceHandle,
            "aliases of one ownership control block retain one stable handle");
        Check(second.Succeeded() && second.stableInstanceHandle == 2u,
            "a distinct owner receives a distinct monotonic handle");
        Check(allocator.TrackedOwnerCount() == 2u,
            "allocator tracks ownership identities rather than pointer aliases");

        firstToken.reset();
        firstAlias.reset();
        firstOwner.reset();
        allocator.PruneExpired();
        Check(allocator.TrackedOwnerCount() == 1u,
            "expired control blocks can be pruned without recycling handles");

        auto replacementOwner = std::make_shared<Owner>();
        std::shared_ptr<const void> replacementToken(
            replacementOwner,
            static_cast<const void*>(replacementOwner.get()));
        const StableInstanceHandleResult replacement =
            allocator.Resolve(replacementToken);
        Check(replacement.Succeeded() &&
                replacement.stableInstanceHandle == 3u,
            "replacement owners cannot inherit a destroyed owner's identity");

        StableInstanceHandleAllocator exhausted(
            std::numeric_limits<uint64_t>::max());
        const StableInstanceHandleResult last =
            exhausted.Resolve(secondToken);
        auto overflowOwner = std::make_shared<Owner>();
        std::shared_ptr<const void> overflowToken(
            overflowOwner,
            static_cast<const void*>(overflowOwner.get()));
        const StableInstanceHandleResult overflow =
            exhausted.Resolve(overflowToken);
        Check(last.Succeeded() &&
                last.stableInstanceHandle ==
                    std::numeric_limits<uint64_t>::max() &&
                exhausted.Exhausted() &&
                overflow.reason == FallbackReason::CapacityOverflow,
            "stable-handle exhaustion fails without wrapping to zero");

        StableInstanceHandleAllocator invalid(0u);
        Check(invalid.Resolve(secondToken).reason ==
                FallbackReason::InvalidHandleEncoding,
            "zero is reserved and cannot seed the stable-handle allocator");
        Check(allocator.Resolve({}).reason ==
                FallbackReason::TransientMappingFailure,
            "a missing ownership token cannot create a stable identity");
    }

    void TestGeometryHandleLifecycle()
    {
        CompactGeometryHandleRegistry registry(4u);
        std::array<CompactGeometryIdentityDesc, 2> identities = {{
            MakeIdentity(100u, 0u),
            MakeIdentity(100u, 1u)
        }};

        const CompactGeometryHandleReconcileResult initial =
            registry.Reconcile(10u, identities.data(), identities.size());
        Check(initial.Succeeded() && initial.assignments.size() == 2u &&
                registry.ActiveEntryCount() == 2u &&
                registry.TopologyEpoch() == 10u,
            "initial scene reconciliation publishes every logical geometry");
        if (!initial.Succeeded() || initial.assignments.size() != 2u)
            return;

        const uint32_t firstHandle =
            initial.assignments[0].geometryInstanceHandle;
        const uint32_t secondHandle =
            initial.assignments[1].geometryInstanceHandle;
        Check(firstHandle != secondHandle,
            "submeshes of one stable instance receive distinct GPU handles");

        const CompactGeometryHandleReconcileResult unchanged =
            registry.Reconcile(10u, identities.data(), identities.size());
        Check(unchanged.Succeeded() && unchanged.assignments.size() == 2u &&
                unchanged.assignments[0].geometryInstanceHandle ==
                    firstHandle &&
                unchanged.assignments[1].geometryInstanceHandle ==
                    secondHandle,
            "unchanged topology and LOD preserve GPU handle identity");

        identities[0].deformationRevision = 8u;
        identities[0].materialRevision = 9u;
        const CompactGeometryHandleReconcileResult metadata =
            registry.Reconcile(10u, identities.data(), identities.size());
        Check(metadata.Succeeded() && metadata.assignments.size() == 2u,
            "metadata-only reconciliation remains complete");
        if (!metadata.Succeeded() || metadata.assignments.size() != 2u)
            return;
        const RegistryLookupResult metadataLookup =
            registry.Inspect(metadata.assignments[0].geometryInstanceHandle);
        Check(metadataLookup.Succeeded() &&
                metadata.assignments[0].geometryInstanceHandle ==
                    firstHandle &&
                metadataLookup.entry.deformationRevision == 8u &&
                metadataLookup.entry.materialRevision == 9u,
            "deformation and material revisions update metadata without changing ownership");

        const GpuVisibilityKey key = {
            metadata.assignments[0].geometryInstanceHandle,
            17u
        };
        const SurfaceResolutionResult resolved = registry.ResolveSurfaceKey(
            key,
            MakeValidation(10u, 4u, 8u, 9u));
        Check(resolved.Succeeded() &&
                resolved.key.stableInstanceHandle == 100u &&
                resolved.key.geometryIndex == 0u &&
                resolved.key.geometryLocalPrimitiveIndex == 17u,
            "GPU visibility resolves losslessly to the logical SurfaceKey");
        Check(registry.ResolveSurfaceKey(
                key,
                MakeValidation(10u, 4u, 8u, 6u)).reason ==
                    FallbackReason::MaterialRevisionMismatch,
            "stale material metadata invalidates compact reconstruction");

        identities[0].deformationRevision = 10u;
        identities[0].materialRevision = 11u;
        identities[0].deformationSupported = false;
        identities[0].materialSupported = false;
        const CompactGeometryHandleReconcileResult fallbackMetadata =
            registry.Reconcile(10u, identities.data(), identities.size());
        const RegistryLookupResult fallbackLookup =
            fallbackMetadata.Succeeded()
            ? registry.Inspect(
                fallbackMetadata.assignments[0]
                    .geometryInstanceHandle)
            : RegistryLookupResult{};
        Check(fallbackMetadata.Succeeded() &&
                fallbackMetadata.assignments.size() == 2u &&
                fallbackMetadata.assignments[0]
                    .geometryInstanceHandle == firstHandle &&
                fallbackLookup.Succeeded() &&
                !fallbackLookup.entry.deformationSupported &&
                !fallbackLookup.entry.materialSupported &&
                registry.ResolveSurfaceKey(
                    { firstHandle, 17u },
                    MakeValidation(10u, 4u, 10u, 11u)).reason ==
                        FallbackReason::UnsupportedDeformation,
            "payload eligibility changes update metadata without changing the surface generation");

        identities[0].deformationRevision = 12u;
        identities[0].materialRevision = 13u;
        identities[0].deformationSupported = true;
        identities[0].materialSupported = true;
        const CompactGeometryHandleReconcileResult reconstructedAgain =
            registry.Reconcile(10u, identities.data(), identities.size());
        Check(reconstructedAgain.Succeeded() &&
                reconstructedAgain.assignments[0]
                    .geometryInstanceHandle == firstHandle,
            "returning to reconstructable payload metadata preserves the same handle");

        identities[0].topologyRevision = 4u;
        const CompactGeometryHandleReconcileResult topologyChanged =
            registry.Reconcile(10u, identities.data(), identities.size());
        Check(topologyChanged.Succeeded() &&
                topologyChanged.assignments.size() == 2u &&
                topologyChanged.assignments[0].geometryInstanceHandle !=
                    firstHandle &&
                registry.Inspect(firstHandle).reason ==
                    FallbackReason::GenerationMismatch,
            "topology changes retire the prior generation before reuse");
        if (!topologyChanged.Succeeded() ||
            topologyChanged.assignments.size() != 2u)
        {
            return;
        }

        const uint32_t topologyHandle =
            topologyChanged.assignments[0].geometryInstanceHandle;
        identities[0].lodRevision = 5u;
        const CompactGeometryHandleReconcileResult lodChanged =
            registry.Reconcile(10u, identities.data(), identities.size());
        Check(lodChanged.Succeeded() &&
                lodChanged.assignments.size() == 2u &&
                lodChanged.assignments[0].geometryInstanceHandle !=
                    topologyHandle &&
                registry.Inspect(topologyHandle).reason ==
                    FallbackReason::GenerationMismatch,
            "LOD changes also manufacture a new surface generation");

        const CompactGeometryIdentityDesc firstOnly = identities[0];
        const CompactGeometryHandleReconcileResult removed =
            registry.Reconcile(10u, &firstOnly, 1u);
        Check(removed.Succeeded() && registry.ActiveEntryCount() == 1u &&
                registry.Inspect(secondHandle).reason ==
                    FallbackReason::GenerationMismatch,
            "removed scene geometry is invalidated instead of remaining addressable");
    }

    void TestReplacementAtExactCapacity()
    {
        CompactGeometryHandleRegistry registry(2u);
        std::array<CompactGeometryIdentityDesc, 2> initialIdentities = {{
            MakeIdentity(10u, 0u),
            MakeIdentity(11u, 0u)
        }};
        const CompactGeometryHandleReconcileResult initial =
            registry.Reconcile(
                5u,
                initialIdentities.data(),
                initialIdentities.size());
        Check(initial.Succeeded() && initial.assignments.size() == 2u,
            "exact-capacity replacement test requires a full registry");
        if (!initial.Succeeded() || initial.assignments.size() != 2u)
            return;

        std::array<CompactGeometryIdentityDesc, 2> replacements = {{
            initialIdentities[0],
            MakeIdentity(12u, 0u)
        }};
        const CompactGeometryHandleReconcileResult replaced =
            registry.Reconcile(
                5u,
                replacements.data(),
                replacements.size());
        Check(replaced.Succeeded() && replaced.assignments.size() == 2u &&
                registry.ActiveEntryCount() == 2u &&
                replaced.assignments[0].geometryInstanceHandle ==
                    initial.assignments[0].geometryInstanceHandle &&
                replaced.assignments[1].geometryInstanceHandle !=
                    initial.assignments[1].geometryInstanceHandle &&
                registry.Inspect(
                    initial.assignments[1].geometryInstanceHandle).reason ==
                        FallbackReason::GenerationMismatch,
            "absent identities retire before registration at exact capacity");
    }

    void TestReconciliationFailureIsTransactional()
    {
        CompactGeometryHandleRegistry registry(2u);
        CompactGeometryIdentityDesc identity = MakeIdentity(20u, 3u);
        const CompactGeometryHandleReconcileResult initial =
            registry.Reconcile(7u, &identity, 1u);
        Check(initial.Succeeded(),
            "transaction test requires an initial valid mapping");
        if (!initial.Succeeded())
            return;

        const uint32_t initialHandle =
            initial.assignments[0].geometryInstanceHandle;
        const std::array<CompactGeometryIdentityDesc, 2> duplicate = {{
            identity,
            identity
        }};
        const CompactGeometryHandleReconcileResult collision =
            registry.Reconcile(7u, duplicate.data(), duplicate.size());
        Check(collision.reason ==
                FallbackReason::TransientMappingCollision &&
                registry.ActiveEntryCount() == 1u &&
                registry.Inspect(initialHandle).Succeeded(),
            "duplicate identity failure leaves the published registry unchanged");

        std::array<CompactGeometryIdentityDesc, 3> overflow = {{
            identity,
            MakeIdentity(21u, 0u),
            MakeIdentity(22u, 0u)
        }};
        const CompactGeometryHandleReconcileResult overflowResult =
            registry.Reconcile(7u, overflow.data(), overflow.size());
        Check(overflowResult.reason == FallbackReason::CapacityOverflow &&
                registry.Inspect(initialHandle).Succeeded(),
            "capacity failure is transactional");

        Check(registry.Reconcile(7u, nullptr, 1u).reason ==
                FallbackReason::TransientMappingFailure &&
                registry.Inspect(initialHandle).Succeeded(),
            "missing identity input is transactional");
        Check(registry.Reconcile(0u, &identity, 1u).reason ==
                FallbackReason::TopologyEpochMismatch &&
                registry.TopologyEpoch() == 7u,
            "zero or regressed topology epochs cannot replace live state");
        Check(registry.Reconcile(6u, &identity, 1u).reason ==
                FallbackReason::TopologyEpochMismatch &&
                registry.TopologyEpoch() == 7u,
            "older topology epochs cannot roll the registry backward");

        CompactGeometryIdentityDesc invalid = identity;
        invalid.stableInstanceHandle = 0u;
        Check(registry.Reconcile(7u, &invalid, 1u).reason ==
                FallbackReason::InvalidHandleEncoding &&
                registry.Inspect(initialHandle).Succeeded(),
            "reserved logical identities fail before changing live mappings");
    }

    void TestWiderEpochReset()
    {
        CompactGeometryHandleRegistry registry(1u);
        CompactGeometryIdentityDesc identity = MakeIdentity(70u, 2u);
        const CompactGeometryHandleReconcileResult first =
            registry.Reconcile(100u, &identity, 1u);
        const CompactGeometryHandleReconcileResult reset =
            registry.Reconcile(101u, &identity, 1u);
        Check(first.Succeeded() && first.assignments.size() == 1u &&
                reset.Succeeded() && reset.assignments.size() == 1u &&
                first.assignments[0].geometryInstanceHandle ==
                    reset.assignments[0].geometryInstanceHandle,
            "a synchronized wider epoch reset may reuse the numeric packed handle");
        if (!first.Succeeded() || first.assignments.size() != 1u ||
            !reset.Succeeded() || reset.assignments.size() != 1u)
            return;

        const GpuVisibilityKey reusedKey = {
            reset.assignments[0].geometryInstanceHandle,
            0u
        };
        Check(registry.ResolveSurfaceKey(
                reusedKey,
                MakeValidation(100u, 4u, 5u, 6u)).reason ==
                    FallbackReason::TopologyEpochMismatch,
            "the wider epoch rejects stale use even when packed bits repeat");
        Check(registry.ResolveSurfaceKey(
                reusedKey,
                MakeValidation(101u, 4u, 5u, 6u)).Succeeded(),
            "the same packed handle is valid only under the published epoch");

        const CompactGeometryHandleReconcileResult cleared =
            registry.Reconcile(101u, nullptr, 0u);
        Check(cleared.Succeeded() && registry.ActiveEntryCount() == 0u &&
                registry.Inspect(reusedKey.geometryInstanceHandle).reason ==
                    FallbackReason::GenerationMismatch,
            "reconciling an empty live set retires every old handle");
    }

    void TestGenerationExhaustionRequiresWiderEpoch()
    {
        CompactGeometryHandleRegistry registry(1u);
        CompactGeometryIdentityDesc identity = MakeIdentity(90u, 0u);
        identity.topologyRevision = 0u;
        CompactGeometryHandleReconcileResult result =
            registry.Reconcile(200u, &identity, 1u);
        Check(result.Succeeded() && result.assignments.size() == 1u,
            "generation-exhaustion test requires an initial mapping");
        if (!result.Succeeded() || result.assignments.size() != 1u)
            return;

        uint32_t lastValidHandle =
            result.assignments[0].geometryInstanceHandle;
        bool reachedMaximumGeneration = true;
        for (uint32_t revision = 1u;
            revision < MaximumGeometryInstanceGeneration;
            ++revision)
        {
            identity.topologyRevision = revision;
            result = registry.Reconcile(200u, &identity, 1u);
            if (!result.Succeeded() || result.assignments.size() != 1u)
            {
                reachedMaximumGeneration = false;
                break;
            }
            lastValidHandle =
                result.assignments[0].geometryInstanceHandle;
        }
        const DecodedGeometryInstanceHandle maximum =
            DecodeGeometryInstanceHandle(lastValidHandle);
        Check(reachedMaximumGeneration && maximum.Succeeded() &&
                maximum.generation == MaximumGeometryInstanceGeneration,
            "same-epoch topology reuse reaches the last legal generation exactly");
        if (!reachedMaximumGeneration || !maximum.Succeeded())
            return;

        identity.topologyRevision = MaximumGeometryInstanceGeneration;
        const CompactGeometryHandleReconcileResult exhausted =
            registry.Reconcile(200u, &identity, 1u);
        Check(exhausted.reason == FallbackReason::GenerationExhausted &&
                registry.RequiresEpochReset() &&
                registry.Inspect(lastValidHandle).Succeeded(),
            "generation exhaustion latches a reset while retaining the last published mapping");
        Check(registry.Reconcile(200u, &identity, 1u).reason ==
                FallbackReason::RegistryResetRequired,
            "same-epoch replacement remains blocked after exhaustion");

        const CompactGeometryHandleReconcileResult reset =
            registry.Reconcile(201u, &identity, 1u);
        Check(reset.Succeeded() && !registry.RequiresEpochReset() &&
                registry.TopologyEpoch() == 201u,
            "a synchronized wider epoch clears the exhaustion latch");
    }
}

int main()
{
    TestGpuSceneAbi();
    TestFiniteMaterialTextureFormats();
    TestMaterialTextureDescriptions();
    TestMaterialTextureSemanticAllowlist();
    TestDenseDescriptorPlanning();
    TestMaterialConstantTextureRemapping();
    TestLosslessMaterialFingerprint();
    TestSnapshotVersionOrdering();
    TestCompactEligibilityFallbackLatch();
    TestCompactMaterialFeatureSlice();
    TestGeometryValidation();
    TestWholeFrameEligibility();
    TestStableInstanceHandles();
    TestGeometryHandleLifecycle();
    TestReplacementAtExactCapacity();
    TestReconciliationFailureIsTransactional();
    TestWiderEpochReset();
    TestGenerationExhaustionRequiresWiderEpoch();

    if (g_Passed)
    {
        std::cout <<
            "UVSR adaptive MSAA compact scene validation passed\n";
    }
    return g_Passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
