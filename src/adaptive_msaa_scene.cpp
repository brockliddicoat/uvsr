#include "adaptive_msaa_scene.h"
#include "adaptive_msaa_texture_qualification_runtime.h"
#include "adaptive_msaa_raster_cb.h"

// Implementation provenance:
// - UVSR-native code informed by the resource-flow architecture of The Forge
//   Visibility_Buffer sample at commit
//   a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0).
// - Public scene and descriptor APIs come from UVSR's pinned Donut/NVRHI
//   dependency (MIT); that dependency is not modified.
// No external source text is copied into this file.

#include <donut/engine/Scene.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>

using namespace donut::math;
#include <donut/shaders/material_cb.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
    using namespace donut::engine;
    using namespace uvsr::adaptive_msaa;

    constexpr uint64_t IndexStride = sizeof(uint32_t);
    constexpr uint64_t PositionStride = sizeof(float) * 3u;
    constexpr uint64_t TexCoordStride = sizeof(float) * 2u;
    constexpr uint64_t PackedDirectionStride = sizeof(uint32_t);
    constexpr uint64_t InstanceDataStride = 7u * 16u;
    constexpr uint64_t MaximumGpuByteEnd =
        uint64_t(std::numeric_limits<uint32_t>::max()) + 1u;

    static_assert(sizeof(MaterialConstants) == 13u * 16u);
    static_assert(std::is_trivially_copyable_v<MaterialConstants>);
    static_assert(std::is_standard_layout_v<MaterialConstants>);
    static_assert(offsetof(
        MaterialConstants,
        baseOrDiffuseTextureIndex) == 76u);
    static_assert(offsetof(
        MaterialConstants,
        metalRoughOrSpecularTextureIndex) == 80u);
    static_assert(offsetof(MaterialConstants, emissiveTextureIndex) == 84u);
    static_assert(offsetof(MaterialConstants, normalTextureIndex) == 88u);
    static_assert(offsetof(MaterialConstants, occlusionTextureIndex) == 92u);
    static_assert(offsetof(
        MaterialConstants,
        transmissionTextureIndex) == 96u);
    static_assert(offsetof(MaterialConstants, opacityTextureIndex) == 100u);
    static_assert(offsetof(MaterialConstants, padding1) == 112u);

    bool IsFiniteFloat2(float2 value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y);
    }

    bool IsFiniteFloat3(float3 value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    bool AreCompactMaterialConstantsFinite(
        const MaterialConstants& material)
    {
        return IsFiniteFloat3(material.baseOrDiffuseColor) &&
            IsFiniteFloat3(material.specularColor) &&
            IsFiniteFloat3(material.emissiveColor) &&
            std::isfinite(material.opacity) &&
            std::isfinite(material.roughness) &&
            std::isfinite(material.metalness) &&
            std::isfinite(material.normalTextureScale) &&
            std::isfinite(material.occlusionStrength) &&
            std::isfinite(material.alphaCutoff) &&
            std::isfinite(material.transmissionFactor) &&
            IsFiniteFloat2(material.normalTextureTransformScale) &&
            std::isfinite(material.sssScale) &&
            IsFiniteFloat3(material.sssTransmissionColor) &&
            std::isfinite(material.sssAnisotropy) &&
            IsFiniteFloat3(material.sssScatteringColor) &&
            IsFiniteFloat3(material.hairBaseColor) &&
            std::isfinite(material.hairMelanin) &&
            std::isfinite(material.hairMelaninRedness) &&
            std::isfinite(material.hairLongitudinalRoughness) &&
            std::isfinite(material.hairAzimuthalRoughness) &&
            std::isfinite(material.hairIor) &&
            std::isfinite(material.hairCuticleAngle) &&
            IsFiniteFloat3(material.hairDiffuseReflectionTint) &&
            std::isfinite(material.hairDiffuseReflectionWeight);
    }

    bool IsCompactNormalTextureTransformScaleSupported(
        const MaterialConstants& material)
    {
        if ((material.flags & MaterialFlags_UseNormalTexture) == 0)
            return true;

        return std::abs(material.normalTextureScale) <=
                AdaptiveMsaaCompactMaximumAbsoluteNormalMapScale &&
            std::abs(material.normalTextureTransformScale.x) <=
                AdaptiveMsaaCompactMaximumAbsoluteNormalTextureUvScale &&
            std::abs(material.normalTextureTransformScale.y) <=
                AdaptiveMsaaCompactMaximumAbsoluteNormalTextureUvScale;
    }

    void ClearMaterialConstants(MaterialConstants& constants)
    {
        static_assert(std::is_trivially_copyable_v<MaterialConstants>);
        std::memset(&constants, 0, sizeof(constants));
    }

    const std::shared_ptr<LoadedTexture>& GetCompactMaterialTexture(
        const Material& material,
        CompactMaterialTextureSemantic semantic)
    {
        switch (semantic)
        {
        case CompactMaterialTextureSemantic::BaseOrDiffuse:
            return material.baseOrDiffuseTexture;
        case CompactMaterialTextureSemantic::MetalRoughOrSpecular:
            return material.metalRoughOrSpecularTexture;
        case CompactMaterialTextureSemantic::Emissive:
            return material.emissiveTexture;
        case CompactMaterialTextureSemantic::Normal:
            return material.normalTexture;
        case CompactMaterialTextureSemantic::Occlusion:
            return material.occlusionTexture;
        case CompactMaterialTextureSemantic::Transmission:
            return material.transmissionTexture;
        case CompactMaterialTextureSemantic::Opacity:
            return material.opacityTexture;
        default:
            return material.baseOrDiffuseTexture;
        }
    }

    bool CompactMaterialTextureEnabled(
        const Material& material,
        CompactMaterialTextureSemantic semantic)
    {
        switch (semantic)
        {
        case CompactMaterialTextureSemantic::BaseOrDiffuse:
            return material.enableBaseOrDiffuseTexture;
        case CompactMaterialTextureSemantic::MetalRoughOrSpecular:
            return material.enableMetalRoughOrSpecularTexture;
        case CompactMaterialTextureSemantic::Emissive:
            return material.enableEmissiveTexture;
        case CompactMaterialTextureSemantic::Normal:
            return material.enableNormalTexture;
        case CompactMaterialTextureSemantic::Occlusion:
            return material.enableOcclusionTexture;
        case CompactMaterialTextureSemantic::Transmission:
            return material.enableTransmissionTexture;
        case CompactMaterialTextureSemantic::Opacity:
            return material.enableOpacityTexture;
        default:
            return false;
        }
    }

    uint32_t CompactMaterialTextureFlag(
        CompactMaterialTextureSemantic semantic)
    {
        switch (semantic)
        {
        case CompactMaterialTextureSemantic::BaseOrDiffuse:
            return MaterialFlags_UseBaseOrDiffuseTexture;
        case CompactMaterialTextureSemantic::MetalRoughOrSpecular:
            return MaterialFlags_UseMetalRoughOrSpecularTexture;
        case CompactMaterialTextureSemantic::Emissive:
            return MaterialFlags_UseEmissiveTexture;
        case CompactMaterialTextureSemantic::Normal:
            return MaterialFlags_UseNormalTexture;
        case CompactMaterialTextureSemantic::Occlusion:
            return MaterialFlags_UseOcclusionTexture;
        case CompactMaterialTextureSemantic::Transmission:
            return MaterialFlags_UseTransmissionTexture;
        case CompactMaterialTextureSemantic::Opacity:
            return MaterialFlags_UseOpacityTexture;
        default:
            return 0u;
        }
    }

    int& CompactMaterialTextureIndex(
        MaterialConstants& constants,
        CompactMaterialTextureSemantic semantic)
    {
        switch (semantic)
        {
        case CompactMaterialTextureSemantic::BaseOrDiffuse:
            return constants.baseOrDiffuseTextureIndex;
        case CompactMaterialTextureSemantic::MetalRoughOrSpecular:
            return constants.metalRoughOrSpecularTextureIndex;
        case CompactMaterialTextureSemantic::Emissive:
            return constants.emissiveTextureIndex;
        case CompactMaterialTextureSemantic::Normal:
            return constants.normalTextureIndex;
        case CompactMaterialTextureSemantic::Occlusion:
            return constants.occlusionTextureIndex;
        case CompactMaterialTextureSemantic::Transmission:
            return constants.transmissionTextureIndex;
        case CompactMaterialTextureSemantic::Opacity:
            return constants.opacityTextureIndex;
        default:
            return constants.baseOrDiffuseTextureIndex;
        }
    }

    uint32_t CompactMaterialEnabledTextureMask(
        const MaterialConstants& constants);

    FallbackReason ValidateCompactReconstructionMaterial(
        const Material& material,
        bool supportAlphaTestedMaterials,
        bool qualifiedTexturesActive,
        nvrhi::IDevice* device,
        MaterialConstants& constants)
    {
        ClearMaterialConstants(constants);
        material.FillConstantBuffer(constants, false);
        if (!device)
            return FallbackReason::UnsupportedMaterial;
        return ValidateCompactMaterialFeatureSlice(
            constants,
            material.domain,
            supportAlphaTestedMaterials,
            qualifiedTexturesActive);
    }

    bool TryAdd(uint64_t first, uint64_t second, uint64_t& result)
    {
        if (first > std::numeric_limits<uint64_t>::max() - second)
            return false;
        result = first + second;
        return true;
    }

    bool TryMultiply(uint64_t first, uint64_t second, uint64_t& result)
    {
        if (first != 0u &&
            second > std::numeric_limits<uint64_t>::max() / first)
        {
            return false;
        }
        result = first * second;
        return true;
    }

    FallbackReason ValidateElementRange(
        const CompactByteRange& range,
        uint64_t bufferByteSize,
        uint64_t firstElement,
        uint64_t elementCount,
        uint64_t elementStride)
    {
        if (range.byteSize == 0u)
            return FallbackReason::ReconstructionFailure;

        uint64_t rangeEnd = 0u;
        uint64_t firstByteInRange = 0u;
        uint64_t byteCount = 0u;
        uint64_t firstByte = 0u;
        uint64_t byteEnd = 0u;
        if (!TryAdd(range.byteOffset, range.byteSize, rangeEnd) ||
            !TryMultiply(firstElement, elementStride, firstByteInRange) ||
            !TryMultiply(elementCount, elementStride, byteCount) ||
            !TryAdd(range.byteOffset, firstByteInRange, firstByte) ||
            !TryAdd(firstByte, byteCount, byteEnd))
        {
            return FallbackReason::CapacityOverflow;
        }

        if (firstByte < range.byteOffset ||
            byteEnd > rangeEnd ||
            byteEnd > bufferByteSize)
        {
            return FallbackReason::ReconstructionFailure;
        }
        if (byteEnd > MaximumGpuByteEnd)
            return FallbackReason::CapacityOverflow;
        return FallbackReason::None;
    }

    CompactSceneEligibility MakeEligibility(
        FallbackReason reason,
        uint32_t failingGeometry = InvalidVisibilityWord)
    {
        CompactSceneEligibility result;
        result.reason = reason;
        result.failingGeometry = failingGeometry;
        result.eligible = reason == FallbackReason::None;
        return result;
    }

    uint32_t LowWord(uint64_t value)
    {
        return uint32_t(value & 0xffffffffull);
    }

    uint32_t HighWord(uint64_t value)
    {
        return uint32_t(value >> 32u);
    }

    uint32_t NarrowToUint32(uint64_t value)
    {
        return static_cast<uint32_t>(value);
    }

    CompactByteRange ToCompactRange(const nvrhi::BufferRange& range)
    {
        return { range.byteOffset, range.byteSize };
    }

    struct PendingGeometry
    {
        std::shared_ptr<MeshInstance> instance;
        std::shared_ptr<MeshInfo> mesh;
        std::shared_ptr<MeshGeometry> geometry;
        uint64_t stableInstanceHandle = 0u;
        uint32_t instanceIndex = 0u;
        uint32_t geometryIndex = 0u;
        uint32_t flattenedHandleIndex = 0u;
        uint32_t materialIndex = 0u;
        uint32_t reconstructionMaterialIndex = InvalidVisibilityWord;
        uint32_t geometryRecordIndex = InvalidVisibilityWord;
        bool skinned = false;
        CompactGeometryPayloadDecision payloadDecision;
        CompactGeometryValidationDesc validation;
    };

    struct PlannedDescriptor
    {
        enum class Kind
        {
            Texture,
            RawBuffer
        };

        Kind kind = Kind::RawBuffer;
        uint32_t slot = 0u;
        nvrhi::TextureHandle texture;
        nvrhi::BufferHandle buffer;
    };

    class DescriptorPlan
    {
    public:
        explicit DescriptorPlan(uint32_t capacity)
            : m_Slots(capacity)
        {
        }

        FallbackReason AddTexture(
            nvrhi::ITexture* texture,
            uint32_t& descriptor)
        {
            if (!texture)
                return FallbackReason::UnsupportedMaterial;
            const uint32_t previousCount = m_Slots.DescriptorCount();
            const FallbackReason reason = m_Slots.AddTexture(
                reinterpret_cast<uintptr_t>(texture), descriptor);
            if (reason != FallbackReason::None)
                return reason;
            if (descriptor < previousCount)
            {
                return descriptor < m_Descriptors.size() &&
                    m_Descriptors[descriptor].kind ==
                        PlannedDescriptor::Kind::Texture &&
                    m_Descriptors[descriptor].texture.Get() == texture
                    ? FallbackReason::None
                    : FallbackReason::TransientMappingFailure;
            }
            PlannedDescriptor planned;
            planned.kind = PlannedDescriptor::Kind::Texture;
            planned.slot = descriptor;
            planned.texture = texture;
            m_Descriptors.push_back(std::move(planned));
            return FallbackReason::None;
        }

        FallbackReason AddRawBuffer(
            nvrhi::IBuffer* buffer,
            uint32_t& descriptor)
        {
            if (!buffer)
                return FallbackReason::ReconstructionFailure;
            const uint32_t previousCount = m_Slots.DescriptorCount();
            const FallbackReason reason = m_Slots.AddRawBuffer(
                reinterpret_cast<uintptr_t>(buffer), descriptor);
            if (reason != FallbackReason::None)
                return reason;
            if (descriptor < previousCount)
            {
                return descriptor < m_Descriptors.size() &&
                    m_Descriptors[descriptor].kind ==
                        PlannedDescriptor::Kind::RawBuffer &&
                    m_Descriptors[descriptor].buffer.Get() == buffer
                    ? FallbackReason::None
                    : FallbackReason::TransientMappingFailure;
            }
            PlannedDescriptor planned;
            planned.kind = PlannedDescriptor::Kind::RawBuffer;
            planned.slot = descriptor;
            planned.buffer = buffer;
            m_Descriptors.push_back(std::move(planned));
            return FallbackReason::None;
        }

        [[nodiscard]] const std::vector<PlannedDescriptor>& Descriptors() const
        {
            return m_Descriptors;
        }

        [[nodiscard]] uint32_t TextureCount() const
        {
            return m_Slots.TextureCount();
        }

        [[nodiscard]] uint32_t RawBufferCount() const
        {
            return m_Slots.RawBufferCount();
        }

        [[nodiscard]] uint32_t DescriptorCount() const
        {
            return m_Slots.DescriptorCount();
        }

        [[nodiscard]] FallbackReason ValidatePartition() const
        {
            const FallbackReason slotReason =
                m_Slots.ValidatePartition();
            if (slotReason != FallbackReason::None ||
                m_Descriptors.size() != m_Slots.DescriptorCount())
                return slotReason == FallbackReason::None
                    ? FallbackReason::TransientMappingFailure
                    : slotReason;

            for (size_t index = 0u;
                index < m_Descriptors.size();
                ++index)
            {
                const PlannedDescriptor& descriptor =
                    m_Descriptors[index];
                const PlannedDescriptor::Kind expectedKind =
                    index < m_Slots.TextureCount()
                    ? PlannedDescriptor::Kind::Texture
                    : PlannedDescriptor::Kind::RawBuffer;
                if (descriptor.slot != index ||
                    descriptor.kind != expectedKind)
                {
                    return FallbackReason::TransientMappingFailure;
                }
            }
            return FallbackReason::None;
        }

        [[nodiscard]] const CompactDescriptorSlotPlanner& SlotPlanner() const
        {
            return m_Slots;
        }

    private:
        CompactDescriptorSlotPlanner m_Slots;
        std::vector<PlannedDescriptor> m_Descriptors;
    };

    FallbackReason BuildCompactMaterialRecord(
        const Material& material,
        bool supportAlphaTestedMaterials,
        bool qualifiedTexturesActive,
        TextureCache* finalizedTextureCache,
        nvrhi::IDevice* device,
        DescriptorPlan& descriptorPlan,
        MaterialConstants& constants,
        CompactMaterialTextureDescriptorRecord& descriptorRecord)
    {
        const FallbackReason validation =
            ValidateCompactReconstructionMaterial(
                material,
                supportAlphaTestedMaterials,
                qualifiedTexturesActive,
                device,
                constants);
        if (validation != FallbackReason::None)
            return validation;

        std::array<uintptr_t, CompactMaterialTextureSemanticCount>
            resourceIdentities{};
        uint32_t finalizedLoadTimeMipRenderTargetMask = 0u;
        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            const auto semantic = static_cast<
                CompactMaterialTextureSemantic>(semanticIndex);
            const uint32_t flag = CompactMaterialTextureFlag(semantic);
            if ((constants.flags & flag) == 0u)
                continue;

            const std::shared_ptr<LoadedTexture>& loadedTexture =
                GetCompactMaterialTexture(material, semantic);
            if (!loadedTexture || !loadedTexture->texture)
                return FallbackReason::TransientMappingFailure;
            const nvrhi::TextureDesc& description =
                loadedTexture->texture->getDesc();
            const bool finalizedLoadTimeMipRenderTarget =
#if UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC
                description.isRenderTarget && description.mipLevels > 1u &&
                finalizedTextureCache &&
                finalizedTextureCache->IsTextureFinalized(loadedTexture);
#else
                false;
#endif
            const FallbackReason textureReason =
                ValidateCompactMaterialTextureForSemantic(
                    semantic,
                    description,
                    device->queryFormatSupport(description.format),
                    finalizedLoadTimeMipRenderTarget);
            if (textureReason != FallbackReason::None)
                return textureReason;
            if (finalizedLoadTimeMipRenderTarget)
            {
                finalizedLoadTimeMipRenderTargetMask |=
                    1u << semanticIndex;
            }
            resourceIdentities[semanticIndex] = reinterpret_cast<uintptr_t>(
                loadedTexture->texture.Get());
        }

        CompactDescriptorSlotPlanner plannedSlots =
            descriptorPlan.SlotPlanner();
        const FallbackReason planReason =
            PlanCompactMaterialTextureDescriptors(
                constants,
                resourceIdentities,
                plannedSlots,
                descriptorRecord);
        if (planReason != FallbackReason::None)
            return planReason;
        descriptorRecord.finalizedLoadTimeMipRenderTargetMask =
            finalizedLoadTimeMipRenderTargetMask;

        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            if ((descriptorRecord.enabledMask &
                    (1u << semanticIndex)) == 0u)
                continue;
            const auto semantic = static_cast<
                CompactMaterialTextureSemantic>(semanticIndex);
            const std::shared_ptr<LoadedTexture>& loadedTexture =
                GetCompactMaterialTexture(material, semantic);
            uint32_t descriptor = InvalidVisibilityWord;
            const FallbackReason reason = descriptorPlan.AddTexture(
                loadedTexture->texture,
                descriptor);
            if (reason != FallbackReason::None)
                return reason;
            if (descriptor != uint32_t(
                    descriptorRecord.descriptors[semanticIndex]))
                return FallbackReason::TransientMappingFailure;
        }
        return FallbackReason::None;
    }

    FallbackReason ValidateCompactMaterialDescriptorRecord(
        const CompactMaterialTextureDescriptorRecord& record,
        uint32_t textureDescriptorCount)
    {
        constexpr uint32_t ValidMask =
            (1u << CompactMaterialTextureSemanticCount) - 1u;
        if ((record.enabledMask & ~ValidMask) != 0u)
            return FallbackReason::TransientMappingFailure;
        if ((record.finalizedLoadTimeMipRenderTargetMask &
                ~record.enabledMask) != 0u)
        {
            return FallbackReason::TransientMappingFailure;
        }
        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            const bool enabled =
                (record.enabledMask & (1u << semanticIndex)) != 0u;
            const int32_t descriptor = record.descriptors[semanticIndex];
            if ((enabled &&
                    (descriptor < 0 ||
                        uint32_t(descriptor) >= textureDescriptorCount)) ||
                (!enabled && descriptor != -1))
            {
                return FallbackReason::TransientMappingFailure;
            }
        }
        return FallbackReason::None;
    }

    uint32_t CompactMaterialEnabledTextureMask(
        const MaterialConstants& constants)
    {
        uint32_t result = 0u;
        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            const auto semantic = static_cast<
                CompactMaterialTextureSemantic>(semanticIndex);
            if ((constants.flags &
                    CompactMaterialTextureFlag(semantic)) != 0u)
            {
                result |= 1u << semanticIndex;
            }
        }
        return result;
    }

    CompactMaterialTextureIdentity CaptureCompactTextureIdentity(
        const std::shared_ptr<LoadedTexture>& loadedTexture,
        int32_t compactDescriptor,
        const CompactTextureQualificationIdentity& qualificationIdentity,
        bool finalizedLoadTimeMipRenderTarget)
    {
        CompactMaterialTextureIdentity result;
        if (!loadedTexture || !loadedTexture->texture)
            return result;

        result.loadedTextureKeepAlive = loadedTexture;
        result.textureKeepAlive = loadedTexture->texture;
        result.resourceIdentity = loadedTexture->texture.Get();
        result.compactDescriptor = compactDescriptor;
        const nvrhi::TextureDesc& description =
            loadedTexture->texture->getDesc();
        result.width = description.width;
        result.height = description.height;
        result.depth = description.depth;
        result.arraySize = description.arraySize;
        result.mipLevels = description.mipLevels;
        result.sampleCount = description.sampleCount;
        result.sampleQuality = description.sampleQuality;
        result.format = description.format;
        result.dimension = description.dimension;
        result.isShaderResource = description.isShaderResource;
        result.isRenderTarget = description.isRenderTarget;
        result.isUav = description.isUAV;
        result.isTypeless = description.isTypeless;
        result.isVirtual = description.isVirtual;
        result.isTiled = description.isTiled;
        result.finalizedLoadTimeMipRenderTarget =
            finalizedLoadTimeMipRenderTarget;
        result.sharedResourceFlags = description.sharedResourceFlags;
        result.contentRevision = qualificationIdentity.contentRevision;
        result.qualificationToken =
            qualificationIdentity.qualificationToken;
        return result;
    }

    bool SameCompactTextureDescription(
        const CompactMaterialTextureIdentity& expected,
        const nvrhi::TextureDesc& current)
    {
        return expected.width == current.width &&
            expected.height == current.height &&
            expected.depth == current.depth &&
            expected.arraySize == current.arraySize &&
            expected.mipLevels == current.mipLevels &&
            expected.sampleCount == current.sampleCount &&
            expected.sampleQuality == current.sampleQuality &&
            expected.format == current.format &&
            expected.dimension == current.dimension &&
            expected.isShaderResource == current.isShaderResource &&
            expected.isRenderTarget == current.isRenderTarget &&
            expected.isUav == current.isUAV &&
            expected.isTypeless == current.isTypeless &&
            expected.isVirtual == current.isVirtual &&
            expected.isTiled == current.isTiled &&
            expected.sharedResourceFlags == current.sharedResourceFlags;
    }

    FallbackReason ValidateCompactMaterialFingerprintStructure(
        const CompactMaterialFingerprint& fingerprint,
        const CompactMaterialTextureDescriptorRecord& descriptorRecord,
        const CompactTextureQualificationIdentity& qualificationIdentity)
    {
        const FallbackReason descriptorReason =
            ValidateCompactMaterialDescriptorRecord(
                descriptorRecord,
                uint32_t(INT_MAX));
        if (descriptorReason != FallbackReason::None ||
            !fingerprint.materialKeepAlive ||
            fingerprint.enabledTextureMask != descriptorRecord.enabledMask)
        {
            return FallbackReason::TransientMappingFailure;
        }

        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            const bool enabled = (fingerprint.enabledTextureMask &
                (1u << semanticIndex)) != 0u;
            const CompactMaterialTextureIdentity& texture =
                fingerprint.textures[semanticIndex];
            if (!enabled)
            {
                if (texture.loadedTextureKeepAlive ||
                    texture.textureKeepAlive ||
                    texture.resourceIdentity ||
                    texture.compactDescriptor != -1 ||
                    texture.width != 0u || texture.height != 0u ||
                    texture.depth != 0u || texture.arraySize != 0u ||
                    texture.mipLevels != 0u ||
                    texture.sampleCount != 0u ||
                    texture.sampleQuality != 0u ||
                    texture.format != nvrhi::Format::UNKNOWN ||
                    texture.dimension !=
                        nvrhi::TextureDimension::Unknown ||
                    texture.isShaderResource ||
                    texture.isRenderTarget || texture.isUav ||
                    texture.isTypeless || texture.isVirtual ||
                    texture.isTiled ||
                    texture.finalizedLoadTimeMipRenderTarget ||
                    texture.sharedResourceFlags !=
                        nvrhi::SharedResourceFlags::None ||
                    texture.contentRevision != 0u ||
                    texture.qualificationToken != 0u)
                {
                    return FallbackReason::TransientMappingFailure;
                }
                continue;
            }
            if (!qualificationIdentity.IsValid())
                return FallbackReason::TextureQualificationMismatch;
            if (texture.contentRevision !=
                    qualificationIdentity.contentRevision)
            {
                return FallbackReason::TextureContentRevisionMismatch;
            }
            if (texture.qualificationToken !=
                    qualificationIdentity.qualificationToken)
            {
                return FallbackReason::TextureQualificationMismatch;
            }
            if (!texture.loadedTextureKeepAlive ||
                !texture.textureKeepAlive ||
                !texture.resourceIdentity ||
                texture.textureKeepAlive.Get() !=
                    texture.resourceIdentity ||
                texture.compactDescriptor !=
                    descriptorRecord.descriptors[semanticIndex] ||
                ValidateCompactMaterialTextureForSemantic(
                    static_cast<CompactMaterialTextureSemantic>(
                        semanticIndex),
                    texture.textureKeepAlive->getDesc(),
                    nvrhi::FormatSupport::Texture |
                        nvrhi::FormatSupport::ShaderSample,
                    texture.finalizedLoadTimeMipRenderTarget) !=
                    FallbackReason::None ||
                !SameCompactTextureDescription(
                    texture,
                    texture.textureKeepAlive->getDesc()))
            {
                return FallbackReason::TransientMappingFailure;
            }
        }
        return FallbackReason::None;
    }

    template <typename Record>
    nvrhi::BufferHandle CreateRecordBuffer(
        nvrhi::IDevice* device,
        nvrhi::ICommandList* commandList,
        const std::vector<Record>& records,
        const char* debugName)
    {
        if (!device || !commandList || records.empty())
            return nullptr;

        uint64_t byteSize = 0u;
        if (!TryMultiply(records.size(), sizeof(Record), byteSize))
            return nullptr;

        nvrhi::BufferDesc desc;
        desc.byteSize = byteSize;
        desc.debugName = debugName;
        desc.structStride = sizeof(Record);
        desc.canHaveRawViews = true;
        desc.initialState = nvrhi::ResourceStates::ShaderResource;
        desc.keepInitialState = true;
        nvrhi::BufferHandle buffer = device->createBuffer(desc);
        if (!buffer)
            return nullptr;

        commandList->writeBuffer(
            buffer,
            records.data(),
            static_cast<size_t>(byteSize));
        return buffer;
    }

    std::shared_ptr<const void> MakeOwnerToken(
        const std::shared_ptr<MeshInstance>& instance)
    {
        return std::shared_ptr<const void>(
            instance,
            static_cast<const void*>(instance.get()));
    }
}

namespace uvsr::adaptive_msaa
{
    FallbackReason ValidateCompactMaterialFeatureSlice(
        const MaterialConstants& constants,
        donut::engine::MaterialDomain domain,
        bool supportAlphaTestedMaterials,
        bool qualifiedTexturesActive)
    {
        const bool supportedDomain =
            domain == MaterialDomain::Opaque ||
            (supportAlphaTestedMaterials &&
                domain == MaterialDomain::AlphaTested);
        constexpr uint32_t UnsupportedFirstSliceFlags =
            MaterialFlags_Hair |
            MaterialFlags_DoubleSided |
            MaterialFlags_UseSpecularGlossModel |
            MaterialFlags_SubsurfaceScattering;
        if (!supportedDomain ||
            !AreCompactMaterialConstantsFinite(constants) ||
            !IsCompactNormalTextureTransformScaleSupported(constants) ||
            (constants.flags & UnsupportedFirstSliceFlags) != 0u ||
            constants.transmissionFactor != 0.f)
        {
            return FallbackReason::UnsupportedMaterial;
        }

        // A format allowlist alone is not evidence that compute
        // reconstruction matches raster derivatives or that uploaded texels
        // stayed unchanged. Textures open only while the builder holds an
        // authority-validated immutable-content lease; exact semantic and
        // resource checks still run while descriptors are assembled.
        if (CompactMaterialEnabledTextureMask(constants) != 0u &&
            !qualifiedTexturesActive)
        {
            return FallbackReason::UnsupportedMaterial;
        }
        return FallbackReason::None;
    }

    CompactDescriptorSlotPlanner::CompactDescriptorSlotPlanner(
        uint32_t capacity)
        : m_Capacity(capacity)
    {
    }

    FallbackReason CompactDescriptorSlotPlanner::AddTexture(
        uintptr_t resourceIdentity,
        uint32_t& descriptor)
    {
        descriptor = InvalidVisibilityWord;
        if (resourceIdentity == 0u)
            return FallbackReason::UnsupportedMaterial;
        if (m_RawPhase)
            return FallbackReason::TransientMappingFailure;

        const auto found = std::find(
            m_TextureIdentities.begin(),
            m_TextureIdentities.end(),
            resourceIdentity);
        if (found != m_TextureIdentities.end())
        {
            descriptor = static_cast<uint32_t>(
                std::distance(m_TextureIdentities.begin(), found));
            return FallbackReason::None;
        }
        if (DescriptorCount() >= m_Capacity ||
            DescriptorCount() >= uint32_t(INT_MAX))
        {
            return FallbackReason::CapacityOverflow;
        }

        descriptor = DescriptorCount();
        m_TextureIdentities.push_back(resourceIdentity);
        return FallbackReason::None;
    }

    FallbackReason CompactDescriptorSlotPlanner::AddRawBuffer(
        uintptr_t resourceIdentity,
        uint32_t& descriptor)
    {
        descriptor = InvalidVisibilityWord;
        if (resourceIdentity == 0u)
            return FallbackReason::ReconstructionFailure;
        m_RawPhase = true;

        const auto found = std::find(
            m_RawBufferIdentities.begin(),
            m_RawBufferIdentities.end(),
            resourceIdentity);
        if (found != m_RawBufferIdentities.end())
        {
            descriptor = TextureCount() + static_cast<uint32_t>(
                std::distance(m_RawBufferIdentities.begin(), found));
            return FallbackReason::None;
        }
        if (DescriptorCount() >= m_Capacity ||
            DescriptorCount() >= uint32_t(INT_MAX))
        {
            return FallbackReason::CapacityOverflow;
        }

        descriptor = DescriptorCount();
        m_RawBufferIdentities.push_back(resourceIdentity);
        return FallbackReason::None;
    }

    uint32_t CompactDescriptorSlotPlanner::Capacity() const
    {
        return m_Capacity;
    }

    uint32_t CompactDescriptorSlotPlanner::TextureCount() const
    {
        return static_cast<uint32_t>(m_TextureIdentities.size());
    }

    uint32_t CompactDescriptorSlotPlanner::RawBufferCount() const
    {
        return static_cast<uint32_t>(m_RawBufferIdentities.size());
    }

    uint32_t CompactDescriptorSlotPlanner::DescriptorCount() const
    {
        return TextureCount() + RawBufferCount();
    }

    bool CompactDescriptorSlotPlanner::RawPhaseStarted() const
    {
        return m_RawPhase;
    }

    FallbackReason CompactDescriptorSlotPlanner::ValidatePartition() const
    {
        if (DescriptorCount() > m_Capacity ||
            DescriptorCount() > uint32_t(INT_MAX) ||
            (!m_RawBufferIdentities.empty() && !m_RawPhase))
        {
            return FallbackReason::CapacityOverflow;
        }
        std::set<uintptr_t> textures;
        std::set<uintptr_t> rawBuffers;
        for (uintptr_t identity : m_TextureIdentities)
        {
            if (identity == 0u || !textures.insert(identity).second)
                return FallbackReason::TransientMappingFailure;
        }
        for (uintptr_t identity : m_RawBufferIdentities)
        {
            if (identity == 0u || !rawBuffers.insert(identity).second)
                return FallbackReason::TransientMappingFailure;
        }
        return FallbackReason::None;
    }

    FallbackReason PlanCompactMaterialTextureDescriptors(
        MaterialConstants& constants,
        const std::array<
            uintptr_t,
            CompactMaterialTextureSemanticCount>& resourceIdentities,
        CompactDescriptorSlotPlanner& slotPlanner,
        CompactMaterialTextureDescriptorRecord& descriptorRecord)
    {
        MaterialConstants candidateConstants{};
        std::memcpy(
            &candidateConstants,
            &constants,
            sizeof(candidateConstants));
        CompactDescriptorSlotPlanner candidateSlots = slotPlanner;
        CompactMaterialTextureDescriptorRecord candidateRecord;

        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            const auto semantic = static_cast<
                CompactMaterialTextureSemantic>(semanticIndex);
            int& compactIndex = CompactMaterialTextureIndex(
                candidateConstants, semantic);
            compactIndex = -1;
            const uint32_t flag = CompactMaterialTextureFlag(semantic);
            if ((candidateConstants.flags & flag) == 0u)
                continue;

            uint32_t descriptor = InvalidVisibilityWord;
            const FallbackReason reason = candidateSlots.AddTexture(
                resourceIdentities[semanticIndex],
                descriptor);
            if (reason != FallbackReason::None)
                return reason;
            if (descriptor > uint32_t(INT_MAX))
                return FallbackReason::CapacityOverflow;

            compactIndex = static_cast<int>(descriptor);
            candidateRecord.descriptors[semanticIndex] = compactIndex;
            candidateRecord.enabledMask |= 1u << semanticIndex;
        }

        const FallbackReason partitionReason =
            candidateSlots.ValidatePartition();
        if (partitionReason != FallbackReason::None)
            return partitionReason;

        std::memcpy(
            &constants,
            &candidateConstants,
            sizeof(constants));
        slotPlanner = std::move(candidateSlots);
        descriptorRecord = candidateRecord;
        return FallbackReason::None;
    }

    CompactMaterialCanonicalConstants CanonicalizeCompactMaterialConstants(
        const MaterialConstants& constants)
    {
        static_assert(sizeof(MaterialConstants) ==
            CompactMaterialCanonicalConstantWordCount * sizeof(uint32_t));
        MaterialConstants canonical = constants;
        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            CompactMaterialTextureIndex(
                canonical,
                static_cast<CompactMaterialTextureSemantic>(
                    semanticIndex)) = -1;
        }
        canonical.padding1.x = 0u;
        canonical.padding1.y = 0u;
        canonical.padding1.z = 0u;

        CompactMaterialCanonicalConstants result{};
        std::memcpy(result.data(), &canonical, sizeof(canonical));
        return result;
    }

    FallbackReason CaptureCompactMaterialFingerprint(
        const std::shared_ptr<const donut::engine::Material>& material,
        const MaterialConstants& finalConstants,
        const CompactMaterialTextureDescriptorRecord& descriptorRecord,
        CompactMaterialFingerprint& fingerprint,
        const CompactTextureQualificationIdentity& qualificationIdentity)
    {
        if (!material)
            return FallbackReason::TransientMappingFailure;
        if (!AreCompactMaterialConstantsFinite(finalConstants))
            return FallbackReason::UnsupportedMaterial;
        const FallbackReason descriptorReason =
            ValidateCompactMaterialDescriptorRecord(
                descriptorRecord,
                uint32_t(INT_MAX));
        if (descriptorReason != FallbackReason::None)
            return descriptorReason;

        CompactMaterialFingerprint candidate;
        candidate.materialKeepAlive = material;
        candidate.canonicalConstants =
            CanonicalizeCompactMaterialConstants(finalConstants);
        candidate.enabledTextureMask =
            CompactMaterialEnabledTextureMask(finalConstants);
        if (candidate.enabledTextureMask != descriptorRecord.enabledMask)
            return FallbackReason::TransientMappingFailure;
        if ((candidate.enabledTextureMask != 0u) !=
                qualificationIdentity.IsValid() ||
            (!qualificationIdentity.IsValid() &&
                !qualificationIdentity.IsEmpty()))
        {
            return FallbackReason::TextureQualificationMismatch;
        }
        MaterialConstants finalIndexCopy = finalConstants;
        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            const int32_t finalDescriptor = CompactMaterialTextureIndex(
                finalIndexCopy,
                static_cast<CompactMaterialTextureSemantic>(
                    semanticIndex));
            if (finalDescriptor !=
                descriptorRecord.descriptors[semanticIndex])
            {
                return FallbackReason::TransientMappingFailure;
            }
        }

        MaterialConstants liveConstants;
        ClearMaterialConstants(liveConstants);
        material->FillConstantBuffer(liveConstants, false);
        if (!AreCompactMaterialConstantsFinite(liveConstants) ||
            CanonicalizeCompactMaterialConstants(liveConstants) !=
                candidate.canonicalConstants ||
            CompactMaterialEnabledTextureMask(liveConstants) !=
                candidate.enabledTextureMask)
        {
            return FallbackReason::MaterialRevisionMismatch;
        }

        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            if ((candidate.enabledTextureMask &
                    (1u << semanticIndex)) == 0u)
            {
                continue;
            }
            const auto semantic = static_cast<
                CompactMaterialTextureSemantic>(semanticIndex);
            const std::shared_ptr<LoadedTexture>& loadedTexture =
                GetCompactMaterialTexture(*material, semantic);
            if (!loadedTexture || !loadedTexture->texture)
                return FallbackReason::MaterialRevisionMismatch;
            const int32_t compactDescriptor =
                descriptorRecord.descriptors[semanticIndex];
            if (compactDescriptor < 0)
                return FallbackReason::TransientMappingFailure;
            const FallbackReason textureReason =
                ValidateCompactMaterialTextureForSemantic(
                    semantic,
                    loadedTexture->texture->getDesc(),
                    nvrhi::FormatSupport::Texture |
                        nvrhi::FormatSupport::ShaderSample,
                    (descriptorRecord.
                        finalizedLoadTimeMipRenderTargetMask &
                        (1u << semanticIndex)) != 0u);
            if (textureReason != FallbackReason::None)
                return textureReason;
            candidate.textures[semanticIndex] =
                CaptureCompactTextureIdentity(
                    loadedTexture,
                    compactDescriptor,
                    qualificationIdentity,
                    (descriptorRecord.
                        finalizedLoadTimeMipRenderTargetMask &
                        (1u << semanticIndex)) != 0u);
        }

        fingerprint = std::move(candidate);
        return FallbackReason::None;
    }

    FallbackReason ValidateCompactMaterialFingerprint(
        const CompactMaterialFingerprint& fingerprint,
        const std::shared_ptr<const donut::engine::Material>& material,
        const CompactMaterialTextureDescriptorRecord& descriptorRecord,
        const CompactTextureQualificationIdentity& qualificationIdentity)
    {
        const FallbackReason structureReason =
            ValidateCompactMaterialFingerprintStructure(
                fingerprint,
                descriptorRecord,
                qualificationIdentity);
        if (structureReason != FallbackReason::None)
            return structureReason;
        if (!material ||
            fingerprint.materialKeepAlive.get() != material.get())
        {
            return FallbackReason::MaterialRevisionMismatch;
        }

        MaterialConstants liveConstants;
        ClearMaterialConstants(liveConstants);
        material->FillConstantBuffer(liveConstants, false);
        if (!AreCompactMaterialConstantsFinite(liveConstants) ||
            CanonicalizeCompactMaterialConstants(liveConstants) !=
                fingerprint.canonicalConstants ||
            CompactMaterialEnabledTextureMask(liveConstants) !=
                fingerprint.enabledTextureMask)
        {
            return FallbackReason::MaterialRevisionMismatch;
        }

        for (size_t semanticIndex = 0u;
            semanticIndex < CompactMaterialTextureSemanticCount;
            ++semanticIndex)
        {
            const bool enabled = (fingerprint.enabledTextureMask &
                (1u << semanticIndex)) != 0u;
            const CompactMaterialTextureIdentity& expected =
                fingerprint.textures[semanticIndex];
            if (!enabled)
                continue;

            const auto semantic = static_cast<
                CompactMaterialTextureSemantic>(semanticIndex);
            const std::shared_ptr<LoadedTexture>& currentTexture =
                GetCompactMaterialTexture(*material, semantic);
            if (!currentTexture || !currentTexture->texture ||
                currentTexture.get() !=
                    expected.loadedTextureKeepAlive.get() ||
                currentTexture->texture.Get() !=
                    expected.resourceIdentity ||
                !SameCompactTextureDescription(
                    expected,
                    currentTexture->texture->getDesc()))
            {
                return FallbackReason::MaterialRevisionMismatch;
            }
        }
        return FallbackReason::None;
    }

    bool IsCompactFiniteMaterialTextureFormat(nvrhi::Format format)
    {
        if (format == nvrhi::Format::UNKNOWN ||
            format == nvrhi::Format::COUNT)
        {
            return false;
        }
        // The bindless shader declares every material texture as
        // Texture2D<float4> and samples it through an anisotropic sampler.
        // Integer resources require a typed uint/int load path and therefore
        // cannot participate in this compact path. Normalized formats decode
        // to a finite bounded float domain; floating-point formats retain a
        // full G-buffer payload until their texel contents are independently
        // proven finite.
        return nvrhi::getFormatInfo(format).kind ==
            nvrhi::FormatKind::Normalized;
    }

    FallbackReason ValidateCompactMaterialTexture(
        const nvrhi::TextureDesc& description,
        nvrhi::FormatSupport formatSupport,
        bool finalizedLoadTimeMipRenderTarget)
    {
        const nvrhi::FormatSupport requiredSupport =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderSample;
        if (description.width == 0u || description.height == 0u ||
            description.depth != 1u || description.arraySize != 1u ||
            description.mipLevels == 0u || description.sampleCount != 1u ||
            description.sampleQuality != 0u ||
            description.dimension != nvrhi::TextureDimension::Texture2D ||
            !description.isShaderResource ||
            (description.isRenderTarget &&
                (!UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC ||
                    !finalizedLoadTimeMipRenderTarget ||
                    description.mipLevels <= 1u)) ||
            description.isUAV || description.isTypeless ||
            description.isVirtual || description.isTiled ||
            description.sharedResourceFlags !=
                nvrhi::SharedResourceFlags::None ||
            !IsCompactFiniteMaterialTextureFormat(description.format) ||
            (formatSupport & requiredSupport) != requiredSupport)
        {
            return FallbackReason::FormatUnsupported;
        }
        return FallbackReason::None;
    }

    bool IsCompactMaterialTextureSemanticFormatSupported(
        CompactMaterialTextureSemantic semantic,
        nvrhi::Format format)
    {
        switch (semantic)
        {
        case CompactMaterialTextureSemantic::BaseOrDiffuse:
            return format == nvrhi::Format::SRGBA8_UNORM ||
                format == nvrhi::Format::BC1_UNORM_SRGB;
        case CompactMaterialTextureSemantic::MetalRoughOrSpecular:
            return format == nvrhi::Format::RG8_UNORM ||
                format == nvrhi::Format::BC5_UNORM ||
                (UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC &&
                    format == nvrhi::Format::RGBA8_UNORM);
        case CompactMaterialTextureSemantic::Emissive:
            return format == nvrhi::Format::SRGBA8_UNORM ||
                format == nvrhi::Format::BC7_UNORM_SRGB;
        case CompactMaterialTextureSemantic::Occlusion:
            return format == nvrhi::Format::R8_UNORM ||
                format == nvrhi::Format::BC4_UNORM;
        case CompactMaterialTextureSemantic::Normal:
            return UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC &&
                format == nvrhi::Format::RGBA8_UNORM;
        case CompactMaterialTextureSemantic::Transmission:
        case CompactMaterialTextureSemantic::Opacity:
        case CompactMaterialTextureSemantic::Count:
            return false;
        default:
            return false;
        }
    }

    FallbackReason ValidateCompactMaterialTextureForSemantic(
        CompactMaterialTextureSemantic semantic,
        const nvrhi::TextureDesc& description,
        nvrhi::FormatSupport formatSupport,
        bool finalizedLoadTimeMipRenderTarget)
    {
        const FallbackReason resourceReason =
            ValidateCompactMaterialTexture(
                description,
                formatSupport,
                finalizedLoadTimeMipRenderTarget);
        if (resourceReason != FallbackReason::None)
            return resourceReason;
        return IsCompactMaterialTextureSemanticFormatSupported(
                semantic,
                description.format)
            ? FallbackReason::None
            : FallbackReason::FormatUnsupported;
    }

    FallbackReason ValidateCompactGeometry(
        const CompactGeometryValidationDesc& geometry)
    {
        if (!geometry.geometrySupported)
            return FallbackReason::ReconstructionFailure;
        if (!geometry.deformationSupported)
            return FallbackReason::UnsupportedDeformation;
        if (!geometry.materialSupported)
            return FallbackReason::UnsupportedMaterial;
        if (!geometry.indexBufferAvailable ||
            !geometry.vertexBufferAvailable)
        {
            return FallbackReason::ReconstructionFailure;
        }
        if (geometry.instanceIndex >= geometry.instanceCount ||
            geometry.flattenedHandleIndex >= geometry.handleMapCount ||
            geometry.materialIndex >= geometry.materialCount)
        {
            return FallbackReason::TransientMappingFailure;
        }
        if (geometry.indexCount == 0u ||
            geometry.vertexCount == 0u ||
            geometry.indexCount % 3u != 0u)
        {
            return FallbackReason::ReconstructionFailure;
        }

        const uint64_t primitiveCount = geometry.indexCount / 3u;
        if (primitiveCount >= InvalidVisibilityWord)
            return FallbackReason::PrimitiveOverflow;

        const CompactByteRange indexRange = {
            0u,
            geometry.indexBufferByteSize
        };
        FallbackReason reason = ValidateElementRange(
            indexRange,
            geometry.indexBufferByteSize,
            geometry.firstIndex,
            geometry.indexCount,
            IndexStride);
        if (reason != FallbackReason::None)
            return reason;

        const std::array<std::pair<CompactByteRange, uint64_t>, 4>
            requiredAttributes = {{
                { geometry.positionRange, PositionStride },
                { geometry.texCoordRange, TexCoordStride },
                { geometry.normalRange, PackedDirectionStride },
                { geometry.tangentRange, PackedDirectionStride }
            }};
        for (const auto& [range, stride] : requiredAttributes)
        {
            reason = ValidateElementRange(
                range,
                geometry.vertexBufferByteSize,
                geometry.firstVertex,
                geometry.vertexCount,
                stride);
            if (reason != FallbackReason::None)
                return reason;
        }

        if (geometry.previousPositionRequired)
        {
            if (geometry.previousPositionRange.byteSize == 0u)
                return FallbackReason::UnsupportedDeformation;
            reason = ValidateElementRange(
                geometry.previousPositionRange,
                geometry.vertexBufferByteSize,
                geometry.firstVertex,
                geometry.vertexCount,
                PositionStride);
            if (reason != FallbackReason::None)
                return FallbackReason::UnsupportedDeformation;
        }
        return FallbackReason::None;
    }

    CompactGeometryPayloadDecision ClassifyCompactGeometryPayload(
        const CompactGeometryValidationDesc& geometry)
    {
        CompactGeometryPayloadDecision result;
        result.reason = ValidateCompactGeometry(geometry);
        if (result.reason == FallbackReason::None)
        {
            result.payloadClass =
                CompactGeometryPayloadClass::Reconstruct;
        }
        else if (result.reason == FallbackReason::UnsupportedDeformation ||
            result.reason == FallbackReason::UnsupportedMaterial)
        {
            result.payloadClass =
                CompactGeometryPayloadClass::FullGBufferFallback;
        }
        return result;
    }

    CompactSceneEligibility EvaluateCompactSceneEligibility(
        const CompactGeometryValidationDesc* geometries,
        size_t geometryCount,
        uint64_t descriptorCount,
        uint64_t descriptorCapacity,
        uint64_t geometryInstanceCapacity,
        CompactScenePartitionSummary* partitionSummary)
    {
        CompactScenePartitionSummary summary;
        const auto Complete = [partitionSummary, &summary](
            CompactSceneEligibility eligibility)
        {
            if (partitionSummary)
                *partitionSummary = summary;
            return eligibility;
        };

        if (!geometries || geometryCount == 0u)
        {
            return Complete(
                MakeEligibility(FallbackReason::ReconstructionFailure));
        }
        if (geometryCount > geometryInstanceCapacity ||
            geometryCount >= InvalidVisibilityWord ||
            geometryInstanceCapacity > GeometryInstanceSlotCount ||
            descriptorCount > descriptorCapacity ||
            descriptorCount > uint64_t(INT_MAX) ||
            descriptorCapacity > uint64_t(INT_MAX))
        {
            return Complete(MakeEligibility(
                FallbackReason::CapacityOverflow));
        }

        summary.identityGeometryCount =
            static_cast<uint32_t>(geometryCount);

        for (size_t index = 0u; index < geometryCount; ++index)
        {
            const CompactGeometryPayloadDecision decision =
                ClassifyCompactGeometryPayload(geometries[index]);
            if (!decision.Succeeded())
            {
                const uint32_t failingGeometry =
                    index < InvalidVisibilityWord
                        ? static_cast<uint32_t>(index)
                        : InvalidVisibilityWord;
                return Complete(MakeEligibility(
                    decision.reason,
                    failingGeometry));
            }
            if (decision.payloadClass ==
                CompactGeometryPayloadClass::Reconstruct)
            {
                ++summary.reconstructGeometryCount;
            }
            else
            {
                ++summary.fallbackGeometryCount;
                if (summary.firstFallbackReason == FallbackReason::None)
                    summary.firstFallbackReason = decision.reason;
            }
        }
        return Complete(MakeEligibility(FallbackReason::None));
    }

    void CompactEligibilityFallbackLatch::Clear()
    {
        m_Identity = {};
        m_Reason = FallbackReason::None;
    }

    void CompactEligibilityFallbackLatch::Record(
        const CompactEligibilityRevisionIdentity& identity,
        FallbackReason reason)
    {
        if (reason == FallbackReason::None)
        {
            Clear();
            return;
        }
        m_Identity = identity;
        m_Reason = reason;
    }

    FallbackReason CompactEligibilityFallbackLatch::Reason(
        const CompactEligibilityRevisionIdentity& identity) const
    {
        const bool exactIdentity =
            identity.topologyEpoch == m_Identity.topologyEpoch &&
            identity.topologyRevision == m_Identity.topologyRevision &&
            identity.lodRevision == m_Identity.lodRevision &&
            identity.deformationRevision ==
                m_Identity.deformationRevision &&
            identity.materialRevision == m_Identity.materialRevision &&
            identity.textureContentRevision ==
                m_Identity.textureContentRevision &&
            identity.textureQualificationToken ==
                m_Identity.textureQualificationToken;
        return m_Reason != FallbackReason::None && exactIdentity
            ? m_Reason
            : FallbackReason::None;
    }

    FallbackReason ValidateCompactSceneSnapshotReplacement(
        const CompactSceneSnapshotVersion& candidate,
        const CompactSceneSnapshotVersion& highestPublished)
    {
        if (candidate.topologyEpoch == 0u ||
            candidate.buildSerial == 0u ||
            highestPublished.topologyEpoch == 0u ||
            highestPublished.buildSerial == 0u)
        {
            return FallbackReason::TransientMappingFailure;
        }
        if (candidate.topologyEpoch < highestPublished.topologyEpoch)
            return FallbackReason::TopologyEpochMismatch;
        if (candidate.topologyEpoch > highestPublished.topologyEpoch)
            return FallbackReason::None;
        return candidate.buildSerial > highestPublished.buildSerial
            ? FallbackReason::None
            : FallbackReason::TransientMappingFailure;
    }

    StableInstanceHandleAllocator::StableInstanceHandleAllocator(
        uint64_t firstHandle)
        : m_NextHandle(firstHandle)
    {
        if (firstHandle == 0u)
            m_ConfigurationReason = FallbackReason::InvalidHandleEncoding;
    }

    StableInstanceHandleResult StableInstanceHandleAllocator::Resolve(
        const std::shared_ptr<const void>& owner)
    {
        StableInstanceHandleResult result;
        if (m_ConfigurationReason != FallbackReason::None)
        {
            result.reason = m_ConfigurationReason;
            return result;
        }
        if (!owner)
        {
            result.reason = FallbackReason::TransientMappingFailure;
            return result;
        }

        PruneExpired();
        for (const OwnerEntry& entry : m_Owners)
        {
            if (!entry.owner.owner_before(owner) &&
                !owner.owner_before(entry.owner))
            {
                result.stableInstanceHandle =
                    entry.stableInstanceHandle;
                return result;
            }
        }
        if (m_Exhausted)
        {
            result.reason = FallbackReason::CapacityOverflow;
            return result;
        }

        result.stableInstanceHandle = m_NextHandle;
        m_Owners.push_back({ owner, m_NextHandle });
        if (m_NextHandle == std::numeric_limits<uint64_t>::max())
            m_Exhausted = true;
        else
            ++m_NextHandle;
        return result;
    }

    void StableInstanceHandleAllocator::PruneExpired()
    {
        m_Owners.erase(
            std::remove_if(
                m_Owners.begin(),
                m_Owners.end(),
                [](const OwnerEntry& entry)
                {
                    return entry.owner.expired();
                }),
            m_Owners.end());
    }

    size_t StableInstanceHandleAllocator::TrackedOwnerCount() const
    {
        return m_Owners.size();
    }

    bool StableInstanceHandleAllocator::Exhausted() const
    {
        return m_Exhausted;
    }

    CompactGeometryHandleRegistry::CompactGeometryHandleRegistry(
        uint32_t capacity)
        : m_Registry(capacity, 0u)
    {
    }

    bool CompactGeometryHandleRegistry::Identity::operator <(
        const Identity& other) const
    {
        if (stableInstanceHandle != other.stableInstanceHandle)
            return stableInstanceHandle < other.stableInstanceHandle;
        return geometryIndex < other.geometryIndex;
    }

    CompactGeometryHandleReconcileResult
        CompactGeometryHandleRegistry::Reconcile(
            uint64_t topologyEpoch,
            const CompactGeometryIdentityDesc* identities,
            size_t identityCount)
    {
        CompactGeometryHandleReconcileResult result;
        if (topologyEpoch == 0u)
        {
            result.reason = FallbackReason::TopologyEpochMismatch;
            return result;
        }
        if (identityCount > Capacity())
        {
            result.reason = FallbackReason::CapacityOverflow;
            return result;
        }
        if (identityCount != 0u && !identities)
        {
            result.reason = FallbackReason::TransientMappingFailure;
            return result;
        }

        SurfaceRegistry candidateRegistry = m_Registry;
        std::map<Identity, Mapping> candidateMappings = m_Mappings;
        bool candidateRequiresEpochReset = m_RequiresEpochReset;
        const uint64_t currentEpoch = candidateRegistry.TopologyEpoch();
        if (topologyEpoch < currentEpoch)
        {
            result.reason = FallbackReason::TopologyEpochMismatch;
            return result;
        }
        if (topologyEpoch > currentEpoch)
        {
            result.reason = candidateRegistry.ResetForTopologyEpoch(
                topologyEpoch);
            if (result.reason != FallbackReason::None)
                return result;
            candidateMappings.clear();
            candidateRequiresEpochReset = false;
        }
        else if (candidateRequiresEpochReset)
        {
            result.reason = FallbackReason::RegistryResetRequired;
            return result;
        }

        std::set<Identity> seen;
        for (size_t index = 0u; index < identityCount; ++index)
        {
            const CompactGeometryIdentityDesc& requested =
                identities[index];
            if (requested.stableInstanceHandle == 0u ||
                requested.geometryIndex == InvalidVisibilityWord)
            {
                result.reason = FallbackReason::InvalidHandleEncoding;
                return result;
            }

            const Identity identity = {
                requested.stableInstanceHandle,
                requested.geometryIndex
            };
            if (!seen.insert(identity).second)
            {
                result.reason =
                    FallbackReason::TransientMappingCollision;
                return result;
            }
        }

        // Retire absent identities before registering replacements so a scene
        // at exact capacity can exchange instances without a false overflow.
        for (auto iterator = candidateMappings.begin();
            iterator != candidateMappings.end();)
        {
            if (seen.find(iterator->first) != seen.end())
            {
                ++iterator;
                continue;
            }

            result.reason = candidateRegistry.InvalidateIdentity(
                iterator->second.geometryInstanceHandle);
            if (result.reason != FallbackReason::None)
            {
                if (result.reason ==
                    FallbackReason::GenerationExhausted)
                {
                    m_RequiresEpochReset = true;
                }
                return result;
            }
            iterator = candidateMappings.erase(iterator);
        }

        result.assignments.reserve(identityCount);
        for (size_t index = 0u; index < identityCount; ++index)
        {
            const CompactGeometryIdentityDesc& requested =
                identities[index];
            const Identity identity = {
                requested.stableInstanceHandle,
                requested.geometryIndex
            };

            RegistryEntryDesc registryEntry;
            registryEntry.stableInstanceHandle =
                requested.stableInstanceHandle;
            registryEntry.geometryIndex = requested.geometryIndex;
            registryEntry.topologyEpoch = topologyEpoch;
            registryEntry.lodRevision = requested.lodRevision;
            registryEntry.deformationRevision =
                requested.deformationRevision;
            registryEntry.materialRevision = requested.materialRevision;
            registryEntry.deformationSupported =
                requested.deformationSupported;
            registryEntry.materialSupported = requested.materialSupported;

            auto found = candidateMappings.find(identity);
            uint32_t handle = InvalidVisibilityWord;
            if (found != candidateMappings.end() &&
                (found->second.topologyRevision !=
                    requested.topologyRevision ||
                 found->second.lodRevision != requested.lodRevision))
            {
                result.reason = candidateRegistry.InvalidateIdentity(
                    found->second.geometryInstanceHandle);
                if (result.reason != FallbackReason::None)
                {
                    if (result.reason ==
                        FallbackReason::GenerationExhausted)
                    {
                        m_RequiresEpochReset = true;
                    }
                    return result;
                }
                candidateMappings.erase(found);
                // map::erase returns the next unrelated identity. Treat the
                // invalidated logical surface as absent so it is registered
                // with the incremented generation instead of aliasing that
                // neighbor's handle.
                found = candidateMappings.end();
            }

            if (found == candidateMappings.end())
            {
                const RegistryRegistrationResult registration =
                    candidateRegistry.Register(registryEntry);
                if (!registration.Succeeded())
                {
                    result.reason = registration.reason;
                    return result;
                }
                handle = registration.geometryInstanceHandle;
                candidateMappings.emplace(
                    identity,
                    Mapping{
                        handle,
                        requested.topologyRevision,
                        requested.lodRevision
                    });
            }
            else
            {
                handle = found->second.geometryInstanceHandle;
                result.reason =
                    candidateRegistry.UpdateNonIdentityMetadata(
                        handle,
                        requested.deformationRevision,
                        requested.materialRevision,
                        requested.deformationSupported,
                        requested.materialSupported);
                if (result.reason != FallbackReason::None)
                    return result;
            }

            result.assignments.push_back({ requested, handle });
        }

        m_Registry = std::move(candidateRegistry);
        m_Mappings = std::move(candidateMappings);
        m_RequiresEpochReset = candidateRequiresEpochReset;
        result.reason = FallbackReason::None;
        return result;
    }

    RegistryLookupResult CompactGeometryHandleRegistry::Inspect(
        uint32_t geometryInstanceHandle) const
    {
        return m_Registry.Inspect(geometryInstanceHandle);
    }

    SurfaceResolutionResult
        CompactGeometryHandleRegistry::ResolveSurfaceKey(
            const GpuVisibilityKey& key,
            const RegistryValidation& validation) const
    {
        return m_Registry.ResolveSurfaceKey(key, validation);
    }

    uint32_t CompactGeometryHandleRegistry::Capacity() const
    {
        return m_Registry.Capacity();
    }

    uint32_t CompactGeometryHandleRegistry::ActiveEntryCount() const
    {
        return m_Registry.ActiveEntryCount();
    }

    uint64_t CompactGeometryHandleRegistry::TopologyEpoch() const
    {
        return m_Registry.TopologyEpoch();
    }

    bool CompactGeometryHandleRegistry::RequiresEpochReset() const
    {
        return m_RequiresEpochReset || m_Registry.RequiresEpochReset();
    }

    const CompactSceneEligibility& CompactSceneSnapshot::Eligibility() const
    {
        return m_Eligibility;
    }

    const CompactSceneGpuConstants& CompactSceneSnapshot::GpuConstants() const
    {
        return m_GpuConstants;
    }

    const CompactScenePartitionSummary&
        CompactSceneSnapshot::PartitionSummary() const
    {
        return m_PartitionSummary;
    }

    uint64_t CompactSceneSnapshot::TopologyEpoch() const
    {
        return uint64_t(m_GpuConstants.topologyEpochLow) |
            (uint64_t(m_GpuConstants.topologyEpochHigh) << 32u);
    }

    uint64_t CompactSceneSnapshot::BuildSerial() const
    {
        return m_Version.buildSerial;
    }

    CompactSceneSnapshotVersion CompactSceneSnapshot::Version() const
    {
        return m_Version;
    }

    CompactTextureQualificationIdentity
        CompactSceneSnapshot::TextureQualification() const
    {
        return m_TextureQualification;
    }

    bool CompactSceneSnapshot::RequiresTextureQualification() const
    {
        return !m_TextureResources.empty();
    }

    nvrhi::CommandQueue
        CompactSceneSnapshot::RequiredCommandQueue() const
    {
        return m_RequiredCommandQueue;
    }

    nvrhi::IBindingLayout* CompactSceneSnapshot::BindlessLayout() const
    {
        return m_BindlessLayout;
    }

    nvrhi::IDescriptorTable* CompactSceneSnapshot::DescriptorTable() const
    {
        return m_DescriptorTable;
    }

    nvrhi::IBuffer* CompactSceneSnapshot::GeometryInstanceBuffer() const
    {
        return m_GeometryInstanceBuffer;
    }

    nvrhi::IBuffer* CompactSceneSnapshot::GeometryBuffer() const
    {
        return m_GeometryBuffer;
    }

    nvrhi::IBuffer* CompactSceneSnapshot::MaterialBuffer() const
    {
        return m_MaterialBuffer;
    }

    nvrhi::IBuffer* CompactSceneSnapshot::InstanceHandleBuffer() const
    {
        return m_InstanceHandleBuffer;
    }

    nvrhi::IBuffer* CompactSceneSnapshot::SceneInstanceBuffer() const
    {
        return m_SceneInstanceBuffer;
    }

    FallbackReason CompactSceneSnapshot::ValidateReadResources() const
    {
        if (!m_Eligibility.Succeeded() ||
            !m_DescriptorTable || !m_GeometryInstanceBuffer ||
            !m_GeometryBuffer || !m_MaterialBuffer ||
            !m_InstanceHandleBuffer || !m_SceneInstanceBuffer)
        {
            return FallbackReason::ReconstructionFailure;
        }

        const CompactSceneGpuConstants& constants = m_GpuConstants;
        if (!HasExactCompactDescriptorPartition(constants) ||
            m_DescriptorTable->getCapacity() < constants.descriptorCount ||
            m_TextureResources.size() !=
                constants.textureDescriptorCount ||
            m_TextureFinalizedLoadTimeMipRenderTargets.size() !=
                m_TextureResources.size() ||
            m_RawBufferResources.size() !=
                constants.rawBufferDescriptorCount ||
            m_CpuGeometryInstanceRecords.size() !=
                constants.geometryInstanceRecordCount ||
            m_CpuGeometryRecords.size() !=
                constants.geometryRecordCount ||
            m_CpuInstanceHandles.size() !=
                constants.instanceHandleCount ||
            m_CpuMaterialTextureDescriptors.size() !=
                constants.materialRecordCount ||
            m_CpuMaterialFingerprints.size() !=
                constants.materialRecordCount ||
            m_PartitionSummary.reconstructGeometryCount !=
                constants.geometryRecordCount ||
            m_PartitionSummary.reconstructMaterialRecordCount !=
                constants.materialRecordCount ||
            !IsCompactRawBufferDescriptor(
                constants, constants.instanceBufferDescriptor) ||
            !IsCompactRawBufferDescriptor(
                constants, constants.materialBufferDescriptor))
        {
            return FallbackReason::TransientMappingFailure;
        }
        if ((m_TextureResources.empty() &&
                !m_TextureQualification.IsEmpty()) ||
            (!m_TextureResources.empty() &&
                !m_TextureQualification.IsValid()))
        {
            return FallbackReason::TextureQualificationMismatch;
        }
        for (const CompactGeometryGpuRecord& geometry :
            m_CpuGeometryRecords)
        {
            if (!IsCompactRawBufferDescriptor(
                    constants, geometry.indexBufferDescriptor) ||
                !IsCompactRawBufferDescriptor(
                    constants, geometry.vertexBufferDescriptor))
            {
                return FallbackReason::TransientMappingFailure;
            }
        }

        uint32_t identityCount = 0u;
        uint32_t reconstructCount = 0u;
        uint32_t fallbackCount = 0u;
        for (const CompactGeometryInstanceGpuRecord& instance :
            m_CpuGeometryInstanceRecords)
        {
            if ((instance.flags &
                    CompactGeometryInstanceFlag_Valid) == 0u)
            {
                if (instance.payloadClass != static_cast<uint32_t>(
                        CompactGeometryPayloadClass::Invalid))
                {
                    return FallbackReason::TransientMappingFailure;
                }
                continue;
            }

            ++identityCount;
            const CompactGeometryPayloadClass payloadClass =
                static_cast<CompactGeometryPayloadClass>(
                    instance.payloadClass);
            const FallbackReason payloadReason =
                static_cast<FallbackReason>(
                    instance.payloadFallbackReason);
            if (payloadClass ==
                CompactGeometryPayloadClass::Reconstruct)
            {
                if (payloadReason != FallbackReason::None ||
                    instance.geometryRecordIndex >=
                        constants.geometryRecordCount)
                {
                    return FallbackReason::TransientMappingFailure;
                }
                ++reconstructCount;
            }
            else if (payloadClass ==
                CompactGeometryPayloadClass::FullGBufferFallback)
            {
                if (instance.geometryRecordIndex !=
                        InvalidVisibilityWord ||
                    (payloadReason !=
                            FallbackReason::UnsupportedDeformation &&
                     payloadReason !=
                            FallbackReason::UnsupportedMaterial &&
                     payloadReason !=
                            FallbackReason::CapacityOverflow &&
                     payloadReason !=
                            FallbackReason::FormatUnsupported &&
                     payloadReason !=
                            FallbackReason::ReconstructionFailure))
                {
                    return FallbackReason::TransientMappingFailure;
                }
                ++fallbackCount;
            }
            else
            {
                return FallbackReason::TransientMappingFailure;
            }
        }
        if (identityCount !=
                m_PartitionSummary.identityGeometryCount ||
            reconstructCount !=
                m_PartitionSummary.reconstructGeometryCount ||
            fallbackCount !=
                m_PartitionSummary.fallbackGeometryCount)
        {
            return FallbackReason::TransientMappingFailure;
        }

        for (size_t textureIndex = 0u;
            textureIndex < m_TextureResources.size();
            ++textureIndex)
        {
            const nvrhi::TextureHandle& texture =
                m_TextureResources[textureIndex];
            const nvrhi::FormatSupport requiredSupport =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::ShaderSample;
            if (!texture || ValidateCompactMaterialTexture(
                    texture->getDesc(),
                    requiredSupport,
                    m_TextureFinalizedLoadTimeMipRenderTargets[
                        textureIndex] != 0u) !=
                        FallbackReason::None)
                return FallbackReason::ReconstructionFailure;
        }
        for (size_t materialIndex = 0u;
            materialIndex < m_CpuMaterialTextureDescriptors.size();
            ++materialIndex)
        {
            const CompactMaterialTextureDescriptorRecord& material =
                m_CpuMaterialTextureDescriptors[materialIndex];
            if (ValidateCompactMaterialDescriptorRecord(
                    material,
                    constants.textureDescriptorCount) !=
                        FallbackReason::None)
                return FallbackReason::TransientMappingFailure;
            const FallbackReason fingerprintReason =
                ValidateCompactMaterialFingerprintStructure(
                    m_CpuMaterialFingerprints[materialIndex],
                    material,
                    m_TextureQualification);
            if (fingerprintReason != FallbackReason::None)
                return fingerprintReason;
        }
        for (const nvrhi::BufferHandle& buffer : m_RawBufferResources)
        {
            if (!buffer)
                return FallbackReason::ReconstructionFailure;
        }
        return FallbackReason::None;
    }

    FallbackReason CompactSceneSnapshot::PrepareReadStates(
        nvrhi::ICommandList* commandList) const
    {
        const FallbackReason validation = ValidateReadResources();
        if (!commandList)
            return FallbackReason::ReconstructionFailure;
        if (validation != FallbackReason::None)
            return validation;

        RecordPreparedReadStateTransitions(commandList);
        return FallbackReason::None;
    }

    void CompactSceneSnapshot::RecordPreparedReadStateTransitions(
        nvrhi::ICommandList* commandList) const
    {
        if (!commandList)
            return;
        for (const nvrhi::TextureHandle& texture : m_TextureResources)
        {
            commandList->setTextureState(
                texture,
                nvrhi::AllSubresources,
                nvrhi::ResourceStates::ShaderResource);
        }
        for (const nvrhi::BufferHandle& buffer : m_RawBufferResources)
        {
            commandList->setBufferState(
                buffer,
                nvrhi::ResourceStates::ShaderResource);
        }

        commandList->setBufferState(
            m_GeometryInstanceBuffer,
            nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(
            m_GeometryBuffer,
            nvrhi::ResourceStates::ShaderResource);
        commandList->setBufferState(
            m_InstanceHandleBuffer,
            nvrhi::ResourceStates::ShaderResource);
    }

    const std::vector<CompactGeometryInstanceGpuRecord>&
        CompactSceneSnapshot::CpuGeometryInstanceRecords() const
    {
        return m_CpuGeometryInstanceRecords;
    }

    const std::vector<CompactGeometryGpuRecord>&
        CompactSceneSnapshot::CpuGeometryRecords() const
    {
        return m_CpuGeometryRecords;
    }

    const std::vector<uint32_t>&
        CompactSceneSnapshot::CpuInstanceHandles() const
    {
        return m_CpuInstanceHandles;
    }

    const std::vector<CompactMaterialTextureDescriptorRecord>&
        CompactSceneSnapshot::CpuMaterialTextureDescriptors() const
    {
        return m_CpuMaterialTextureDescriptors;
    }

    const std::vector<CompactMaterialFingerprint>&
        CompactSceneSnapshot::CpuMaterialFingerprints() const
    {
        return m_CpuMaterialFingerprints;
    }

    struct CompactSceneBuilder::Impl
    {
        Impl(
            nvrhi::IDevice* device,
            uint32_t geometryCapacity,
            uint32_t descriptorCapacity,
            nvrhi::CommandQueue commandQueue)
            : device(device)
            , geometryInstanceCapacity(geometryCapacity)
            , descriptorCapacity(descriptorCapacity)
            , commandQueue(commandQueue)
            , geometryHandles(geometryCapacity)
        {
            if (!device)
            {
                configurationReason =
                    FallbackReason::ReconstructionFailure;
                return;
            }
            if (commandQueue != CompactSceneCommandQueue)
            {
                configurationReason = FallbackReason::FormatUnsupported;
                return;
            }
            if (geometryCapacity == 0u ||
                geometryCapacity > GeometryInstanceSlotCount ||
                descriptorCapacity == 0u ||
                descriptorCapacity > uint32_t(INT_MAX))
            {
                configurationReason = FallbackReason::CapacityOverflow;
                return;
            }

            nvrhi::BindlessLayoutDesc layoutDesc;
            layoutDesc
                .setVisibility(nvrhi::ShaderType::All)
                .setFirstSlot(0u)
                .setMaxCapacity(descriptorCapacity)
                .setLayoutType(
                    nvrhi::BindlessLayoutDesc::LayoutType::Immutable)
                .addRegisterSpace(
                    nvrhi::BindingLayoutItem::Texture_SRV(
                        CompactTextureRegisterSpace))
                .addRegisterSpace(
                    nvrhi::BindingLayoutItem::RawBuffer_SRV(
                        CompactRawBufferRegisterSpace));
            bindlessLayout = device->createBindlessLayout(layoutDesc);
            if (!bindlessLayout)
                configurationReason = FallbackReason::FormatUnsupported;
        }

        nvrhi::DeviceHandle device;
        uint32_t geometryInstanceCapacity = 0u;
        uint32_t descriptorCapacity = 0u;
        nvrhi::CommandQueue commandQueue = CompactSceneCommandQueue;
        FallbackReason configurationReason = FallbackReason::None;
        nvrhi::BindingLayoutHandle bindlessLayout;
        StableInstanceHandleAllocator stableInstances;
        CompactGeometryHandleRegistry geometryHandles;
        uint64_t nextBuildSerial = 1u;
    };

    CompactSceneBuilder::CompactSceneBuilder(
        nvrhi::IDevice* device,
        uint32_t geometryInstanceCapacity,
        uint32_t descriptorCapacity,
        nvrhi::CommandQueue commandQueue)
        : m_Impl(std::make_unique<Impl>(
            device,
            geometryInstanceCapacity,
            descriptorCapacity,
            commandQueue))
    {
    }

    CompactSceneBuilder::~CompactSceneBuilder() = default;

    CompactSceneBuildResult CompactSceneBuilder::Build(
        nvrhi::ICommandList* commandList,
        const donut::engine::Scene& scene,
        const CompactSceneBuildParameters& parameters)
    {
        CompactSceneBuildResult result;
        const auto Fail = [&result](
            FallbackReason reason,
            uint32_t failingGeometry = InvalidVisibilityWord)
        {
            result.snapshot.reset();
            result.eligibility = MakeEligibility(
                reason,
                failingGeometry);
            return result;
        };

        if (!m_Impl ||
            m_Impl->configurationReason != FallbackReason::None)
        {
            return Fail(m_Impl
                ? m_Impl->configurationReason
                : FallbackReason::ReconstructionFailure);
        }
        if (!commandList || parameters.topologyEpoch == 0u ||
            m_Impl->nextBuildSerial == 0u)
        {
            if (!commandList)
                return Fail(FallbackReason::ReconstructionFailure);
            return Fail(parameters.topologyEpoch == 0u
                ? FallbackReason::TopologyEpochMismatch
                : FallbackReason::CapacityOverflow);
        }
        const CompactTextureQualificationIdentity textureQualification =
            parameters.textureQualification;
        if ((!textureQualification.IsValid() &&
                !textureQualification.IsEmpty()) ||
            (textureQualification.IsValid() &&
                (!parameters.textureQualificationAuthority ||
                    !parameters.textureQualificationAuthority->IsActive(
                        textureQualification.qualificationToken,
                        textureQualification.contentRevision))))
        {
            return Fail(FallbackReason::TextureQualificationMismatch);
        }
        const bool qualifiedTexturesActive =
            textureQualification.IsValid();

        const std::shared_ptr<SceneGraph> sceneGraph =
            scene.GetSceneGraph();
        nvrhi::IBuffer* sceneInstanceBuffer = scene.GetInstanceBuffer();
        if (!sceneGraph || !sceneInstanceBuffer)
            return Fail(FallbackReason::ReconstructionFailure);

        StableInstanceHandleAllocator candidateStableInstances =
            m_Impl->stableInstances;
        CompactGeometryHandleRegistry candidateGeometryHandles =
            m_Impl->geometryHandles;
        candidateStableInstances.PruneExpired();

        const auto& sceneInstances = sceneGraph->GetMeshInstances();
        const uint64_t handleMapCount =
            sceneGraph->GetGeometryInstancesCount();
        const nvrhi::BufferDesc& sceneInstanceBufferDesc =
            sceneInstanceBuffer->getDesc();
        uint64_t requiredInstanceBufferBytes = 0u;
        uint64_t requiredHandleMapBytes = 0u;
        if (!sceneInstanceBufferDesc.canHaveRawViews)
            return Fail(FallbackReason::FormatUnsupported);
        if (handleMapCount >= InvalidVisibilityWord ||
            !TryMultiply(
                sceneInstances.size(),
                InstanceDataStride,
                requiredInstanceBufferBytes) ||
            !TryMultiply(
                handleMapCount,
                sizeof(uint32_t),
                requiredHandleMapBytes) ||
            requiredInstanceBufferBytes > MaximumGpuByteEnd ||
            requiredHandleMapBytes > MaximumGpuByteEnd)
        {
            return Fail(FallbackReason::CapacityOverflow);
        }
        if (requiredInstanceBufferBytes >
            sceneInstanceBufferDesc.byteSize)
        {
            return Fail(FallbackReason::TransientMappingFailure);
        }
        std::vector<PendingGeometry> pending;
        pending.reserve(static_cast<size_t>(handleMapCount));
        std::map<const Material*, uint32_t> materialIndices;
        std::vector<std::shared_ptr<const Material>> materials;
        std::vector<FallbackReason> materialSupportReasons;

        for (const std::shared_ptr<MeshInstance>& instance : sceneInstances)
        {
            if (!instance || !instance->GetMesh())
                return Fail(FallbackReason::TransientMappingFailure);
            if (instance->GetInstanceIndex() < 0 ||
                instance->GetGeometryInstanceIndex() < 0 ||
                uint64_t(instance->GetInstanceIndex()) >=
                    sceneInstances.size())
            {
                return Fail(FallbackReason::TransientMappingFailure);
            }

            const StableInstanceHandleResult stable =
                candidateStableInstances.Resolve(
                    MakeOwnerToken(instance));
            if (!stable.Succeeded())
                return Fail(stable.reason);

            const std::shared_ptr<MeshInfo>& mesh = instance->GetMesh();
            const bool skinned = mesh->skinPrototype != nullptr;
            const bool morphTarget = mesh->isMorphTargetAnimationMesh ||
                (mesh->skinPrototype &&
                    mesh->skinPrototype->isMorphTargetAnimationMesh);
            const std::shared_ptr<BufferGroup>& buffers = mesh->buffers;

            for (size_t geometryIndex = 0u;
                geometryIndex < mesh->geometries.size();
                ++geometryIndex)
            {
                const std::shared_ptr<MeshGeometry>& geometry =
                    mesh->geometries[geometryIndex];
                if (!geometry)
                    return Fail(FallbackReason::ReconstructionFailure);
                if (geometry->numIndices == 0u)
                    return Fail(FallbackReason::ReconstructionFailure);
                if (geometryIndex >= InvalidVisibilityWord)
                    return Fail(FallbackReason::CapacityOverflow);

                PendingGeometry item;
                item.instance = instance;
                item.mesh = mesh;
                item.geometry = geometry;
                item.stableInstanceHandle =
                    stable.stableInstanceHandle;
                item.instanceIndex =
                    static_cast<uint32_t>(instance->GetInstanceIndex());
                item.geometryIndex =
                    static_cast<uint32_t>(geometryIndex);
                item.skinned = skinned;

                uint64_t flattenedHandleIndex = 0u;
                if (!TryAdd(
                        uint64_t(instance->GetGeometryInstanceIndex()),
                        geometryIndex,
                        flattenedHandleIndex) ||
                    flattenedHandleIndex >= handleMapCount ||
                    flattenedHandleIndex >= InvalidVisibilityWord)
                {
                    return Fail(FallbackReason::TransientMappingFailure);
                }
                item.flattenedHandleIndex =
                    static_cast<uint32_t>(flattenedHandleIndex);

                const Material* material = geometry->material.get();
                if (!material)
                    return Fail(FallbackReason::ReconstructionFailure);
                if (material->domain != MaterialDomain::Opaque &&
                    material->domain != MaterialDomain::AlphaTested)
                {
                    // Compact visibility replaces only the opaque draw phase.
                    // Other material domains retain their existing renderer
                    // paths and leave this flattened map entry invalid.
                    continue;
                }
                auto materialIterator = materialIndices.find(material);
                if (materialIterator == materialIndices.end())
                {
                    if (materials.size() >= InvalidVisibilityWord)
                    {
                        return Fail(FallbackReason::CapacityOverflow);
                    }
                    materialIterator = materialIndices.emplace(
                        material,
                        static_cast<uint32_t>(materials.size())).first;
                    materials.push_back(geometry->material);
                    MaterialConstants validationConstants;
                    materialSupportReasons.push_back(
                        ValidateCompactReconstructionMaterial(
                            *material,
                            parameters.supportAlphaTestedMaterials,
                            qualifiedTexturesActive,
                            m_Impl->device,
                            validationConstants));
                }
                item.materialIndex = materialIterator->second;

                CompactGeometryValidationDesc& validation =
                    item.validation;
                validation.geometrySupported =
                    mesh->type == MeshType::Triangles &&
                    geometry->type ==
                        MeshGeometryPrimitiveType::Triangles;
                // Production reconstruction is intentionally narrower than
                // the ordinary opaque raster pass. Expected material and
                // deformation variants retain their ownership record and use
                // the exact full G-buffer payload instead of poisoning the
                // whole compact transaction.
                validation.deformationSupported = !morphTarget && !skinned;
                validation.materialSupported =
                    materialSupportReasons[item.materialIndex] ==
                        FallbackReason::None;
                validation.indexBufferAvailable = buffers &&
                    buffers->indexBuffer &&
                    buffers->indexBuffer->getDesc().canHaveRawViews &&
                    buffers->indexBuffer->getDesc().format ==
                        nvrhi::Format::R32_UINT;
                validation.vertexBufferAvailable = buffers &&
                    buffers->vertexBuffer &&
                    buffers->vertexBuffer->getDesc().canHaveRawViews;
                validation.previousPositionRequired = skinned;
                validation.instanceIndex = item.instanceIndex;
                validation.instanceCount = sceneInstances.size();
                validation.flattenedHandleIndex = flattenedHandleIndex;
                validation.handleMapCount = handleMapCount;
                validation.materialIndex = item.materialIndex;
                validation.materialCount = materials.size();
                validation.indexCount = geometry->numIndices;
                validation.vertexCount = geometry->numVertices;

                uint64_t firstIndex = 0u;
                uint64_t firstVertex = 0u;
                if (!TryAdd(
                        mesh->indexOffset,
                        geometry->indexOffsetInMesh,
                        firstIndex) ||
                    !TryAdd(
                        mesh->vertexOffset,
                        geometry->vertexOffsetInMesh,
                        firstVertex))
                {
                    return Fail(FallbackReason::CapacityOverflow);
                }
                validation.firstIndex = firstIndex;
                validation.firstVertex = firstVertex;

                if (validation.indexBufferAvailable)
                {
                    validation.indexBufferByteSize =
                        buffers->indexBuffer->getDesc().byteSize;
                }
                if (validation.vertexBufferAvailable)
                {
                    validation.vertexBufferByteSize =
                        buffers->vertexBuffer->getDesc().byteSize;
                    validation.positionRange = ToCompactRange(
                        buffers->getVertexBufferRange(
                            VertexAttribute::Position));
                    validation.previousPositionRange = ToCompactRange(
                        buffers->getVertexBufferRange(
                            VertexAttribute::PrevPosition));
                    validation.texCoordRange = ToCompactRange(
                        buffers->getVertexBufferRange(
                            VertexAttribute::TexCoord1));
                    validation.normalRange = ToCompactRange(
                        buffers->getVertexBufferRange(
                            VertexAttribute::Normal));
                    validation.tangentRange = ToCompactRange(
                        buffers->getVertexBufferRange(
                            VertexAttribute::Tangent));
                    if (!skinned)
                    {
                        validation.previousPositionRange =
                            validation.positionRange;
                    }
                }
                pending.push_back(std::move(item));
            }
        }

        for (PendingGeometry& item : pending)
            item.validation.materialCount = materials.size();

        std::vector<CompactGeometryValidationDesc> validationDescriptions;
        validationDescriptions.reserve(pending.size());
        for (const PendingGeometry& item : pending)
            validationDescriptions.push_back(item.validation);

        // Unsupported deformation/material metadata is retained in the
        // immutable snapshot so the exact draw can select its full G-buffer
        // payload. It is not a structural build failure. Validate every
        // address that the raster path can use, but do not require an unused
        // previous-position range for an unsupported reconstruction.
        std::vector<CompactGeometryValidationDesc>
            structuralValidationDescriptions = validationDescriptions;
        for (CompactGeometryValidationDesc& validation :
            structuralValidationDescriptions)
        {
            if (!validation.deformationSupported)
            {
                validation.deformationSupported = true;
                validation.previousPositionRequired = false;
            }
            validation.materialSupported = true;
        }

        result.eligibility = EvaluateCompactSceneEligibility(
            structuralValidationDescriptions.data(),
            structuralValidationDescriptions.size(),
            0u,
            m_Impl->descriptorCapacity,
            m_Impl->geometryInstanceCapacity);
        if (!result.eligibility.Succeeded())
            return result;

        for (PendingGeometry& item : pending)
        {
            item.payloadDecision =
                ClassifyCompactGeometryPayload(item.validation);
            if (item.payloadDecision.payloadClass ==
                    CompactGeometryPayloadClass::FullGBufferFallback &&
                item.payloadDecision.reason ==
                    FallbackReason::UnsupportedMaterial &&
                item.materialIndex < materialSupportReasons.size() &&
                materialSupportReasons[item.materialIndex] !=
                    FallbackReason::None)
            {
                item.payloadDecision.reason =
                    materialSupportReasons[item.materialIndex];
            }
            if (!item.payloadDecision.Succeeded())
            {
                return Fail(
                    item.payloadDecision.reason,
                    item.geometryIndex);
            }
        }

        std::vector<CompactGeometryIdentityDesc> identities;
        identities.reserve(pending.size());
        for (const PendingGeometry& item : pending)
        {
            CompactGeometryIdentityDesc identity;
            identity.stableInstanceHandle =
                item.stableInstanceHandle;
            identity.geometryIndex = item.geometryIndex;
            identity.topologyRevision = parameters.topologyRevision;
            identity.lodRevision = parameters.lodRevision;
            identity.deformationRevision =
                parameters.deformationRevision;
            identity.materialRevision = parameters.materialRevision;
            identity.deformationSupported =
                item.validation.deformationSupported;
            identity.materialSupported =
                item.validation.materialSupported;
            identities.push_back(identity);
        }
        const CompactGeometryHandleReconcileResult reconciliation =
            candidateGeometryHandles.Reconcile(
                parameters.topologyEpoch,
                identities.data(),
                identities.size());
        if (!reconciliation.Succeeded())
        {
            if (candidateGeometryHandles.RequiresEpochReset())
            {
                // Preserve only the reset latch; failed reconciliation kept
                // the candidate's previously published mappings intact.
                m_Impl->geometryHandles =
                    std::move(candidateGeometryHandles);
            }
            return Fail(reconciliation.reason);
        }

        if (m_Impl->descriptorCapacity < 2u)
            return Fail(FallbackReason::CapacityOverflow);

        DescriptorPlan descriptorPlan(m_Impl->descriptorCapacity);
        FallbackReason reason = FallbackReason::None;
        std::map<const Material*, uint32_t> reconstructionMaterialIndices;
        std::vector<std::shared_ptr<const Material>>
            reconstructionMaterials;
        std::vector<MaterialConstants> materialRecords;
        std::vector<CompactMaterialTextureDescriptorRecord>
            materialDescriptorRecords;
        uint32_t sceneInstanceBufferDescriptor = InvalidVisibilityWord;

        // Planning is transactional and restarts after a local capacity
        // demotion. Two slots are always reserved for the scene-instance and
        // compact-material raw buffers. Each geometry then reserves the final
        // material slot while tentatively adding its unique index/vertex
        // buffers. A material or geometry that does not fit keeps exact
        // ownership and uses its local full-G-buffer payload; it cannot strand
        // texture descriptors or turn a later raw write into a frame failure.
        bool descriptorPlanComplete = false;
        while (!descriptorPlanComplete)
        {
            DescriptorPlan candidate(m_Impl->descriptorCapacity);
            std::map<const Material*, uint32_t> candidateMaterialIndices;
            std::vector<std::shared_ptr<const Material>>
                candidateMaterials;
            std::vector<MaterialConstants> candidateMaterialRecords;
            std::vector<CompactMaterialTextureDescriptorRecord>
                candidateDescriptorRecords;
            bool restart = false;

            for (PendingGeometry& item : pending)
            {
                if (item.payloadDecision.payloadClass !=
                    CompactGeometryPayloadClass::Reconstruct)
                {
                    continue;
                }
                const Material* material = item.geometry->material.get();
                if (candidateMaterialIndices.find(material) !=
                    candidateMaterialIndices.end())
                {
                    continue;
                }
                if (candidateMaterials.size() >= InvalidVisibilityWord)
                    return Fail(FallbackReason::CapacityOverflow);

                DescriptorPlan materialCandidate = candidate;
                MaterialConstants constants;
                CompactMaterialTextureDescriptorRecord descriptorRecord;
                FallbackReason materialReason = BuildCompactMaterialRecord(
                    *material,
                    parameters.supportAlphaTestedMaterials,
                    qualifiedTexturesActive,
                    parameters.finalizedTextureCache,
                    m_Impl->device,
                    materialCandidate,
                    constants,
                    descriptorRecord);
                if (materialReason == FallbackReason::None &&
                    uint64_t(materialCandidate.DescriptorCount()) + 2u >
                        m_Impl->descriptorCapacity)
                {
                    materialReason = FallbackReason::CapacityOverflow;
                }
                if (materialReason != FallbackReason::None)
                {
                    for (PendingGeometry& matching : pending)
                    {
                        if (matching.payloadDecision.payloadClass ==
                                CompactGeometryPayloadClass::Reconstruct &&
                            matching.geometry->material.get() == material)
                        {
                            matching.payloadDecision.payloadClass =
                                CompactGeometryPayloadClass::
                                    FullGBufferFallback;
                            matching.payloadDecision.reason = materialReason;
                        }
                    }
                    restart = true;
                    break;
                }

                candidate = std::move(materialCandidate);
                const uint32_t materialIndex = static_cast<uint32_t>(
                    candidateMaterials.size());
                candidateMaterialIndices.emplace(material, materialIndex);
                candidateMaterials.push_back(item.geometry->material);
                candidateMaterialRecords.push_back(constants);
                candidateDescriptorRecords.push_back(descriptorRecord);
            }
            if (restart)
                continue;

            uint32_t candidateSceneDescriptor = InvalidVisibilityWord;
            reason = candidate.AddRawBuffer(
                sceneInstanceBuffer,
                candidateSceneDescriptor);
            if (reason != FallbackReason::None)
                return Fail(reason);

            for (PendingGeometry& item : pending)
            {
                if (item.payloadDecision.payloadClass !=
                    CompactGeometryPayloadClass::Reconstruct)
                {
                    continue;
                }
                DescriptorPlan geometryCandidate = candidate;
                uint32_t ignoredIndex = InvalidVisibilityWord;
                uint32_t ignoredVertex = InvalidVisibilityWord;
                reason = geometryCandidate.AddRawBuffer(
                    item.mesh->buffers->indexBuffer,
                    ignoredIndex);
                if (reason == FallbackReason::None)
                {
                    reason = geometryCandidate.AddRawBuffer(
                        item.mesh->buffers->vertexBuffer,
                        ignoredVertex);
                }
                if (reason == FallbackReason::None &&
                    uint64_t(geometryCandidate.DescriptorCount()) + 1u >
                        m_Impl->descriptorCapacity)
                {
                    reason = FallbackReason::CapacityOverflow;
                }
                if (reason != FallbackReason::None)
                {
                    if (reason != FallbackReason::CapacityOverflow)
                        return Fail(reason);
                    item.payloadDecision.payloadClass =
                        CompactGeometryPayloadClass::FullGBufferFallback;
                    item.payloadDecision.reason =
                        FallbackReason::CapacityOverflow;
                    restart = true;
                    break;
                }
                candidate = std::move(geometryCandidate);
            }
            if (restart)
                continue;

            descriptorPlan = std::move(candidate);
            reconstructionMaterialIndices =
                std::move(candidateMaterialIndices);
            reconstructionMaterials = std::move(candidateMaterials);
            materialRecords = std::move(candidateMaterialRecords);
            materialDescriptorRecords =
                std::move(candidateDescriptorRecords);
            sceneInstanceBufferDescriptor = candidateSceneDescriptor;
            descriptorPlanComplete = true;
        }

        for (PendingGeometry& item : pending)
        {
            if (item.payloadDecision.payloadClass ==
                CompactGeometryPayloadClass::Reconstruct)
            {
                const auto found = reconstructionMaterialIndices.find(
                    item.geometry->material.get());
                if (found == reconstructionMaterialIndices.end())
                    return Fail(FallbackReason::TransientMappingFailure);
                item.reconstructionMaterialIndex = found->second;
            }
        }

        if (reconstructionMaterials.size() != materialRecords.size() ||
            materialDescriptorRecords.size() != materialRecords.size())
        {
            return Fail(FallbackReason::TransientMappingFailure);
        }
        std::vector<CompactMaterialFingerprint> materialFingerprints;
        materialFingerprints.reserve(materialRecords.size());
        for (size_t materialIndex = 0u;
            materialIndex < materialRecords.size();
            ++materialIndex)
        {
            CompactMaterialFingerprint fingerprint;
            reason = CaptureCompactMaterialFingerprint(
                reconstructionMaterials[materialIndex],
                materialRecords[materialIndex],
                materialDescriptorRecords[materialIndex],
                fingerprint,
                materialDescriptorRecords[materialIndex].enabledMask != 0u
                    ? textureQualification
                    : CompactTextureQualificationIdentity{});
            if (reason != FallbackReason::None)
                return Fail(reason);
            materialFingerprints.push_back(std::move(fingerprint));
        }

        std::vector<MaterialConstants> physicalMaterialRecords =
            materialRecords;
        if (physicalMaterialRecords.empty())
            physicalMaterialRecords.emplace_back();
        if (descriptorPlan.Descriptors().size() >=
                m_Impl->descriptorCapacity ||
            descriptorPlan.Descriptors().size() >= uint64_t(INT_MAX))
        {
            return Fail(FallbackReason::CapacityOverflow);
        }
        nvrhi::BufferHandle materialBuffer = CreateRecordBuffer(
            m_Impl->device,
            commandList,
            physicalMaterialRecords,
            "Adaptive MSAA Compact Materials");
        if (!materialBuffer)
            return Fail(FallbackReason::ReconstructionFailure);

        uint32_t materialBufferDescriptor = InvalidVisibilityWord;
        reason = descriptorPlan.AddRawBuffer(
            materialBuffer,
            materialBufferDescriptor);
        if (reason != FallbackReason::None)
            return Fail(reason);
        reason = descriptorPlan.ValidatePartition();
        if (reason != FallbackReason::None)
            return Fail(reason);

        std::vector<CompactGeometryGpuRecord> geometryRecords;
        geometryRecords.reserve(pending.size());
        for (PendingGeometry& item : pending)
        {
            if (item.payloadDecision.payloadClass !=
                CompactGeometryPayloadClass::Reconstruct)
            {
                continue;
            }

            uint32_t indexDescriptor = InvalidVisibilityWord;
            uint32_t vertexDescriptor = InvalidVisibilityWord;
            reason = descriptorPlan.AddRawBuffer(
                item.mesh->buffers->indexBuffer,
                indexDescriptor);
            if (reason == FallbackReason::None)
            {
                reason = descriptorPlan.AddRawBuffer(
                    item.mesh->buffers->vertexBuffer,
                    vertexDescriptor);
            }
            if (reason != FallbackReason::None)
                return Fail(FallbackReason::TransientMappingFailure);

            const CompactGeometryValidationDesc& validation =
                item.validation;
            CompactGeometryGpuRecord record;
            record.indexBufferDescriptor = indexDescriptor;
            record.vertexBufferDescriptor = vertexDescriptor;
            record.indexOffset = NarrowToUint32(
                validation.firstIndex * IndexStride);
            record.positionOffset = NarrowToUint32(
                validation.positionRange.byteOffset +
                    validation.firstVertex * PositionStride);
            record.previousPositionOffset = NarrowToUint32(
                validation.previousPositionRange.byteOffset +
                    validation.firstVertex * PositionStride);
            record.texCoordOffset = NarrowToUint32(
                validation.texCoordRange.byteOffset +
                    validation.firstVertex * TexCoordStride);
            record.normalOffset = NarrowToUint32(
                validation.normalRange.byteOffset +
                    validation.firstVertex * PackedDirectionStride);
            record.tangentOffset = NarrowToUint32(
                validation.tangentRange.byteOffset +
                    validation.firstVertex * PackedDirectionStride);
            record.indexCount = item.geometry->numIndices;
            record.vertexCount = item.geometry->numVertices;
            record.materialIndex = item.reconstructionMaterialIndex;
            if (item.skinned)
                record.flags |= CompactGeometryFlag_Skinned;
            if (item.geometry->material->domain ==
                MaterialDomain::AlphaTested)
            {
                record.flags |= CompactGeometryFlag_AlphaTested;
            }
            if (item.geometry->material->doubleSided)
                record.flags |= CompactGeometryFlag_DoubleSided;
            item.geometryRecordIndex = static_cast<uint32_t>(
                geometryRecords.size());
            geometryRecords.push_back(record);
        }

        CompactScenePartitionSummary partitionSummary;
        result.eligibility = EvaluateCompactSceneEligibility(
            validationDescriptions.data(),
            validationDescriptions.size(),
            descriptorPlan.Descriptors().size(),
            m_Impl->descriptorCapacity,
            m_Impl->geometryInstanceCapacity,
            &partitionSummary);
        if (!result.eligibility.Succeeded())
            return result;
        partitionSummary = {};
        partitionSummary.identityGeometryCount = static_cast<uint32_t>(
            pending.size());
        for (const PendingGeometry& item : pending)
        {
            if (item.payloadDecision.payloadClass ==
                CompactGeometryPayloadClass::Reconstruct)
            {
                ++partitionSummary.reconstructGeometryCount;
            }
            else if (item.payloadDecision.payloadClass ==
                CompactGeometryPayloadClass::FullGBufferFallback)
            {
                ++partitionSummary.fallbackGeometryCount;
                if (partitionSummary.firstFallbackReason ==
                    FallbackReason::None)
                {
                    partitionSummary.firstFallbackReason =
                        item.payloadDecision.reason;
                }
            }
            else
            {
                return Fail(FallbackReason::TransientMappingFailure);
            }
        }
        partitionSummary.reconstructMaterialRecordCount =
            static_cast<uint32_t>(materialRecords.size());

        std::vector<uint32_t> instanceHandles(
            static_cast<size_t>(handleMapCount),
            InvalidVisibilityWord);
        uint32_t maximumSlot = 0u;
        for (size_t index = 0u;
            index < reconciliation.assignments.size();
            ++index)
        {
            const DecodedGeometryInstanceHandle decoded =
                DecodeGeometryInstanceHandle(
                    reconciliation.assignments[index]
                        .geometryInstanceHandle);
            if (!decoded.Succeeded())
                return Fail(decoded.reason);
            maximumSlot = std::max(maximumSlot, decoded.slot);

            const uint32_t mapIndex =
                pending[index].flattenedHandleIndex;
            if (instanceHandles[mapIndex] != InvalidVisibilityWord)
            {
                return Fail(
                    FallbackReason::TransientMappingCollision,
                    static_cast<uint32_t>(index));
            }
            instanceHandles[mapIndex] =
                reconciliation.assignments[index]
                    .geometryInstanceHandle;
        }

        std::vector<CompactGeometryInstanceGpuRecord>
            geometryInstanceRecords(maximumSlot + 1u);
        for (size_t index = 0u;
            index < reconciliation.assignments.size();
            ++index)
        {
            const CompactGeometryHandleAssignment& assignment =
                reconciliation.assignments[index];
            const DecodedGeometryInstanceHandle decoded =
                DecodeGeometryInstanceHandle(
                    assignment.geometryInstanceHandle);
            const RegistryLookupResult lookup =
                candidateGeometryHandles.Inspect(
                    assignment.geometryInstanceHandle);
            if (!decoded.Succeeded() || !lookup.Succeeded())
            {
                return Fail(decoded.Succeeded()
                    ? lookup.reason
                    : decoded.reason);
            }

            CompactGeometryInstanceGpuRecord& record =
                geometryInstanceRecords[decoded.slot];
            record.generation = decoded.generation;
            record.geometryIndex = assignment.identity.geometryIndex;
            record.instanceIndex = pending[index].instanceIndex;
            record.geometryRecordIndex =
                pending[index].geometryRecordIndex;
            record.stableInstanceHandleLow = LowWord(
                assignment.identity.stableInstanceHandle);
            record.stableInstanceHandleHigh = HighWord(
                assignment.identity.stableInstanceHandle);
            record.topologyEpochLow = LowWord(parameters.topologyEpoch);
            record.topologyEpochHigh = HighWord(parameters.topologyEpoch);
            record.topologyRevision = parameters.topologyRevision;
            record.lodRevision = parameters.lodRevision;
            record.deformationRevision =
                parameters.deformationRevision;
            record.materialRevision = parameters.materialRevision;
            record.primitiveCount =
                pending[index].geometry->numIndices / 3u;
            record.flags = CompactGeometryInstanceFlag_Valid;
            if (pending[index].validation.deformationSupported)
            {
                record.flags |=
                    CompactGeometryInstanceFlag_DeformationSupported;
            }
            if (pending[index].validation.materialSupported)
            {
                record.flags |=
                    CompactGeometryInstanceFlag_MaterialSupported;
            }
            record.payloadClass = static_cast<uint32_t>(
                pending[index].payloadDecision.payloadClass);
            record.payloadFallbackReason = static_cast<uint32_t>(
                pending[index].payloadDecision.reason);
        }

        nvrhi::BufferHandle geometryInstanceBuffer = CreateRecordBuffer(
            m_Impl->device,
            commandList,
            geometryInstanceRecords,
            "Adaptive MSAA Compact Geometry Instances");
        std::vector<CompactGeometryGpuRecord> physicalGeometryRecords =
            geometryRecords;
        if (physicalGeometryRecords.empty())
            physicalGeometryRecords.emplace_back();
        nvrhi::BufferHandle geometryBuffer = CreateRecordBuffer(
            m_Impl->device,
            commandList,
            physicalGeometryRecords,
            "Adaptive MSAA Compact Geometry");
        nvrhi::BufferHandle instanceHandleBuffer = CreateRecordBuffer(
            m_Impl->device,
            commandList,
            instanceHandles,
            "Adaptive MSAA Geometry Instance Handles");
        if (!geometryInstanceBuffer ||
            !geometryBuffer ||
            !instanceHandleBuffer)
        {
            return Fail(FallbackReason::ReconstructionFailure);
        }

        nvrhi::DescriptorTableHandle descriptorTable =
            m_Impl->device->createDescriptorTable(
                m_Impl->bindlessLayout);
        if (!descriptorTable)
            return Fail(FallbackReason::FormatUnsupported);

        m_Impl->device->resizeDescriptorTable(
            descriptorTable,
            static_cast<uint32_t>(
                descriptorPlan.Descriptors().size()),
            false);
        if (descriptorTable->getCapacity() <
            descriptorPlan.Descriptors().size())
        {
            return Fail(FallbackReason::CapacityOverflow);
        }

        for (const PlannedDescriptor& descriptor :
            descriptorPlan.Descriptors())
        {
            const bool written = descriptor.kind ==
                    PlannedDescriptor::Kind::Texture
                ? m_Impl->device->writeDescriptorTable(
                    descriptorTable,
                    nvrhi::BindingSetItem::Texture_SRV(
                        descriptor.slot,
                        descriptor.texture))
                : m_Impl->device->writeDescriptorTable(
                    descriptorTable,
                    nvrhi::BindingSetItem::RawBuffer_SRV(
                        descriptor.slot,
                        descriptor.buffer));
            if (!written)
                return Fail(FallbackReason::ReconstructionFailure);
        }

        std::shared_ptr<CompactSceneSnapshot> snapshot =
            std::shared_ptr<CompactSceneSnapshot>(
                new CompactSceneSnapshot());
        snapshot->m_Eligibility = MakeEligibility(FallbackReason::None);
        snapshot->m_PartitionSummary = partitionSummary;
        snapshot->m_Version = {
            parameters.topologyEpoch,
            m_Impl->nextBuildSerial
        };
        snapshot->m_TextureQualification =
            descriptorPlan.TextureCount() != 0u
            ? textureQualification
            : CompactTextureQualificationIdentity{};
        snapshot->m_RequiredCommandQueue = m_Impl->commandQueue;
        snapshot->m_BindlessLayout = m_Impl->bindlessLayout;
        snapshot->m_DescriptorTable = descriptorTable;
        snapshot->m_GeometryInstanceBuffer = geometryInstanceBuffer;
        snapshot->m_GeometryBuffer = geometryBuffer;
        snapshot->m_MaterialBuffer = materialBuffer;
        snapshot->m_InstanceHandleBuffer = instanceHandleBuffer;
        snapshot->m_SceneInstanceBuffer = sceneInstanceBuffer;
        snapshot->m_CpuGeometryInstanceRecords =
            std::move(geometryInstanceRecords);
        snapshot->m_CpuGeometryRecords = std::move(geometryRecords);
        snapshot->m_CpuInstanceHandles = std::move(instanceHandles);
        snapshot->m_CpuMaterialTextureDescriptors =
            std::move(materialDescriptorRecords);
        snapshot->m_CpuMaterialFingerprints =
            std::move(materialFingerprints);

        std::vector<uint8_t> finalizedTextureProofs(
            descriptorPlan.TextureCount(),
            uint8_t(0u));
        for (const CompactMaterialTextureDescriptorRecord& material :
            snapshot->m_CpuMaterialTextureDescriptors)
        {
            for (size_t semanticIndex = 0u;
                semanticIndex < CompactMaterialTextureSemanticCount;
                ++semanticIndex)
            {
                if ((material.finalizedLoadTimeMipRenderTargetMask &
                        (1u << semanticIndex)) == 0u)
                {
                    continue;
                }
                const int32_t descriptor =
                    material.descriptors[semanticIndex];
                if (descriptor < 0 ||
                    uint32_t(descriptor) >= finalizedTextureProofs.size())
                {
                    return Fail(FallbackReason::TransientMappingFailure);
                }
                finalizedTextureProofs[descriptor] = 1u;
            }
        }

        for (const PlannedDescriptor& descriptor :
            descriptorPlan.Descriptors())
        {
            if (descriptor.kind == PlannedDescriptor::Kind::Texture)
            {
                snapshot->m_TextureResources.push_back(descriptor.texture);
                snapshot->m_TextureFinalizedLoadTimeMipRenderTargets.push_back(
                    finalizedTextureProofs[descriptor.slot]);
            }
            else
                snapshot->m_RawBufferResources.push_back(descriptor.buffer);
        }

        CompactSceneGpuConstants& constants = snapshot->m_GpuConstants;
        constants.topologyEpochLow = LowWord(parameters.topologyEpoch);
        constants.topologyEpochHigh = HighWord(parameters.topologyEpoch);
        constants.topologyRevision = parameters.topologyRevision;
        constants.lodRevision = parameters.lodRevision;
        constants.geometryInstanceRecordCount = static_cast<uint32_t>(
            snapshot->m_CpuGeometryInstanceRecords.size());
        constants.geometryRecordCount = static_cast<uint32_t>(
            snapshot->m_CpuGeometryRecords.size());
        constants.materialRecordCount = static_cast<uint32_t>(
            materialRecords.size());
        constants.instanceHandleCount = static_cast<uint32_t>(
            snapshot->m_CpuInstanceHandles.size());
        constants.instanceBufferDescriptor =
            sceneInstanceBufferDescriptor;
        constants.materialBufferDescriptor = materialBufferDescriptor;
        constants.rawBufferDescriptorCount =
            descriptorPlan.RawBufferCount();
        constants.textureDescriptorCount =
            descriptorPlan.TextureCount();
        constants.deformationRevision =
            parameters.deformationRevision;
        constants.materialRevision = parameters.materialRevision;
        constants.descriptorCount = static_cast<uint32_t>(
            descriptorPlan.Descriptors().size());

        m_Impl->stableInstances = std::move(candidateStableInstances);
        m_Impl->geometryHandles = std::move(candidateGeometryHandles);
        m_Impl->nextBuildSerial = m_Impl->nextBuildSerial ==
                std::numeric_limits<uint64_t>::max()
            ? 0u
            : m_Impl->nextBuildSerial + 1u;
        result.snapshot = std::move(snapshot);
        result.eligibility = MakeEligibility(FallbackReason::None);
        return result;
    }

    FallbackReason CompactSceneBuilder::ConfigurationReason() const
    {
        return m_Impl
            ? m_Impl->configurationReason
            : FallbackReason::ReconstructionFailure;
    }

    uint32_t CompactSceneBuilder::GeometryInstanceCapacity() const
    {
        return m_Impl ? m_Impl->geometryInstanceCapacity : 0u;
    }

    uint32_t CompactSceneBuilder::DescriptorCapacity() const
    {
        return m_Impl ? m_Impl->descriptorCapacity : 0u;
    }

    CompactSceneSnapshotPublisher::CompactSceneSnapshotPublisher(
        nvrhi::IDevice* device,
        nvrhi::CommandQueue commandQueue)
        : m_Device(device)
        , m_CommandQueue(commandQueue)
    {
    }

    CompactSceneSnapshotPublisher::~CompactSceneSnapshotPublisher()
    {
        if (m_Active)
        {
            const FallbackReason reason =
                RetireActiveAtFrameBoundary();
            if (reason != FallbackReason::None && m_Device)
            {
                m_Device->waitForIdle();
                m_Active.reset();
            }
        }
        DrainRetired();
    }

    FallbackReason CompactSceneSnapshotPublisher::PublishAtFrameBoundary(
        std::shared_ptr<const CompactSceneSnapshot> replacement)
    {
        if (!m_Device || !replacement ||
            !replacement->Eligibility().Succeeded() ||
            !replacement->DescriptorTable() ||
            m_CommandQueue != CompactSceneCommandQueue ||
            replacement->RequiredCommandQueue() != m_CommandQueue)
        {
            return FallbackReason::ReconstructionFailure;
        }
        if (replacement == m_Active)
            return FallbackReason::None;

        if (m_HasPublishedVersion)
        {
            const FallbackReason versionReason =
                ValidateCompactSceneSnapshotReplacement(
                    replacement->Version(),
                    m_HighestPublishedVersion);
            if (versionReason != FallbackReason::None)
                return versionReason;
        }

        const FallbackReason reason = RetireActiveAtFrameBoundary();
        if (reason != FallbackReason::None)
            return reason;
        m_Active = std::move(replacement);
        m_HighestPublishedVersion = m_Active->Version();
        m_HasPublishedVersion = true;
        return FallbackReason::None;
    }

    FallbackReason CompactSceneSnapshotPublisher::ClearAtFrameBoundary()
    {
        return RetireActiveAtFrameBoundary();
    }

    FallbackReason
        CompactSceneSnapshotPublisher::RetireActiveAtFrameBoundary()
    {
        if (!m_Active)
            return FallbackReason::None;
        if (!m_Device)
            return FallbackReason::ReconstructionFailure;

        nvrhi::EventQueryHandle query = m_Device->createEventQuery();
        if (!query)
            return FallbackReason::ReconstructionFailure;

        // This signal is enqueued after the caller-submitted last use. The
        // retained shared_ptr and descriptor resource vectors stay alive until
        // PollRetired observes the signal.
        m_Device->setEventQuery(query, m_CommandQueue);
        m_Retired.push_back({ std::move(m_Active), query });
        m_Active.reset();
        return FallbackReason::None;
    }

    void CompactSceneSnapshotPublisher::PollRetired()
    {
        if (!m_Device)
            return;

        for (auto iterator = m_Retired.begin();
            iterator != m_Retired.end();)
        {
            if (!m_Device->pollEventQuery(iterator->completionQuery))
            {
                ++iterator;
                continue;
            }
            m_Device->resetEventQuery(iterator->completionQuery);
            iterator = m_Retired.erase(iterator);
        }
    }

    void CompactSceneSnapshotPublisher::DrainRetired()
    {
        if (!m_Device)
        {
            m_Retired.clear();
            return;
        }

        for (RetiredSnapshot& retired : m_Retired)
        {
            m_Device->waitEventQuery(retired.completionQuery);
            m_Device->resetEventQuery(retired.completionQuery);
        }
        m_Retired.clear();
    }

    std::shared_ptr<const CompactSceneSnapshot>
        CompactSceneSnapshotPublisher::ActiveSnapshot() const
    {
        return m_Active;
    }

    size_t CompactSceneSnapshotPublisher::RetiredSnapshotCount() const
    {
        return m_Retired.size();
    }
}
