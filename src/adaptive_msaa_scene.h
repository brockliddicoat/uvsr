#pragma once

#include "adaptive_msaa.h"

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#ifndef UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC
#define UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC 0
#endif

struct MaterialConstants;

namespace donut::engine
{
    class Scene;
    class TextureCache;
    enum class MaterialDomain : uint8_t;
    struct LoadedTexture;
    struct Material;
}

namespace uvsr::adaptive_msaa
{
    class TextureQualificationCertificateAuthority;

    // These register spaces are part of the compact-visibility binding ABI.
    // One immutable descriptor table is exposed as two typed unbounded arrays.
    // Slots are densely type-partitioned: textures occupy [0, T), followed by
    // raw buffers in [T, T + R). The existing counts therefore prove the
    // descriptor kind before either typed HLSL array is indexed.
    inline constexpr uint32_t CompactTextureRegisterSpace = 6u;
    inline constexpr uint32_t CompactRawBufferRegisterSpace = 7u;
    inline constexpr uint32_t DefaultCompactDescriptorCapacity = 1u << 20u;
    inline constexpr nvrhi::CommandQueue CompactSceneCommandQueue =
        nvrhi::CommandQueue::Graphics;

    enum class CompactMaterialTextureSemantic : uint32_t
    {
        BaseOrDiffuse = 0u,
        MetalRoughOrSpecular,
        Emissive,
        Normal,
        Occlusion,
        Transmission,
        Opacity,
        Count
    };

    inline constexpr size_t CompactMaterialTextureSemanticCount =
        static_cast<size_t>(CompactMaterialTextureSemantic::Count);

    // CPU-side descriptor planning uses stable resource identities and is
    // independent of NVRHI object lifetime. Runtime code retains the matching
    // handles separately in the immutable snapshot. Texture slots are always
    // emitted before raw-buffer slots so the two unbounded HLSL arrays cannot
    // alias even when the same numeric slot is presented to both spaces.
    class CompactDescriptorSlotPlanner final
    {
    public:
        explicit CompactDescriptorSlotPlanner(uint32_t capacity);

        [[nodiscard]] FallbackReason AddTexture(
            uintptr_t resourceIdentity,
            uint32_t& descriptor);
        [[nodiscard]] FallbackReason AddRawBuffer(
            uintptr_t resourceIdentity,
            uint32_t& descriptor);

        [[nodiscard]] uint32_t Capacity() const;
        [[nodiscard]] uint32_t TextureCount() const;
        [[nodiscard]] uint32_t RawBufferCount() const;
        [[nodiscard]] uint32_t DescriptorCount() const;
        [[nodiscard]] bool RawPhaseStarted() const;
        [[nodiscard]] FallbackReason ValidatePartition() const;

    private:
        uint32_t m_Capacity = 0u;
        bool m_RawPhase = false;
        std::vector<uintptr_t> m_TextureIdentities;
        std::vector<uintptr_t> m_RawBufferIdentities;
    };

    // A compact material texture must be a finite normalized, filterable,
    // non-multisampled 2D shader resource. Floating-point and integer formats
    // remain on the local full-G-buffer path because their decoded values are
    // not bounded by the texture format contract used by reconstruction.
    [[nodiscard]] FallbackReason ValidateCompactMaterialTexture(
        const nvrhi::TextureDesc& description,
        nvrhi::FormatSupport formatSupport,
        bool finalizedLoadTimeMipRenderTarget = false);

    // Narrow first-slice semantic allowlist for a future target-qualified
    // textured compact path. Normal, opacity/alpha, and transmission remain
    // closed even when their storage format is generically sampleable.
    [[nodiscard]] bool IsCompactMaterialTextureSemanticFormatSupported(
        CompactMaterialTextureSemantic semantic,
        nvrhi::Format format);
    [[nodiscard]] FallbackReason ValidateCompactMaterialTextureForSemantic(
        CompactMaterialTextureSemantic semantic,
        const nvrhi::TextureDesc& description,
        nvrhi::FormatSupport formatSupport,
        bool finalizedLoadTimeMipRenderTarget = false);

    // Pure feature-slice gate shared by scene construction and CPU tests.
    // The currently admitted production slice is undeformed elsewhere and
    // opaque, single-sided metallic-roughness here; qualified textures are
    // further restricted by the semantic/format checks above.
    [[nodiscard]] FallbackReason ValidateCompactMaterialFeatureSlice(
        const MaterialConstants& constants,
        donut::engine::MaterialDomain domain,
        bool supportAlphaTestedMaterials,
        bool qualifiedTexturesActive);

    struct CompactMaterialTextureDescriptorRecord
    {
        std::array<int32_t, CompactMaterialTextureSemanticCount> descriptors = {
            -1, -1, -1, -1, -1, -1, -1
        };
        uint32_t enabledMask = 0u;
        uint32_t finalizedLoadTimeMipRenderTargetMask = 0u;
    };

    inline constexpr size_t CompactMaterialCanonicalConstantWordCount =
        13u * 16u / sizeof(uint32_t);
    using CompactMaterialCanonicalConstants = std::array<
        uint32_t,
        CompactMaterialCanonicalConstantWordCount>;

    struct CompactTextureQualificationIdentity
    {
        uint64_t contentRevision = 0u;
        uint64_t qualificationToken = 0u;

        [[nodiscard]] bool IsValid() const
        {
            return contentRevision != 0u && qualificationToken != 0u;
        }

        [[nodiscard]] bool IsEmpty() const
        {
            return contentRevision == 0u && qualificationToken == 0u;
        }

        [[nodiscard]] bool operator ==(
            const CompactTextureQualificationIdentity& other) const
        {
            return contentRevision == other.contentRevision &&
                qualificationToken == other.qualificationToken;
        }

        [[nodiscard]] bool operator !=(
            const CompactTextureQualificationIdentity& other) const
        {
            return !(*this == other);
        }
    };

    // Lossless live-state identity for one shader-visible material texture.
    // Every enabled identity carries the exact immutable-content revision and
    // certificate-authority token retained by its compact scene snapshot.
    struct CompactMaterialTextureIdentity
    {
        std::shared_ptr<const donut::engine::LoadedTexture>
            loadedTextureKeepAlive;
        nvrhi::TextureHandle textureKeepAlive;
        nvrhi::ITexture* resourceIdentity = nullptr;
        int32_t compactDescriptor = -1;

        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t depth = 0u;
        uint32_t arraySize = 0u;
        uint32_t mipLevels = 0u;
        uint32_t sampleCount = 0u;
        uint32_t sampleQuality = 0u;
        nvrhi::Format format = nvrhi::Format::UNKNOWN;
        nvrhi::TextureDimension dimension =
            nvrhi::TextureDimension::Unknown;
        bool isShaderResource = false;
        bool isRenderTarget = false;
        bool isUav = false;
        bool isTypeless = false;
        bool isVirtual = false;
        bool isTiled = false;
        bool finalizedLoadTimeMipRenderTarget = false;
        nvrhi::SharedResourceFlags sharedResourceFlags =
            nvrhi::SharedResourceFlags::None;

        uint64_t contentRevision = 0u;
        uint64_t qualificationToken = 0u;
    };

    struct CompactMaterialFingerprint
    {
        std::shared_ptr<const donut::engine::Material> materialKeepAlive;
        CompactMaterialCanonicalConstants canonicalConstants{};
        uint32_t enabledTextureMask = 0u;
        std::array<
            CompactMaterialTextureIdentity,
            CompactMaterialTextureSemanticCount> textures;
    };

    // Texture descriptor indices are snapshot-local and padding is not
    // shader-visible state. Neutralize exactly those words, preserving every
    // remaining bit including signed zero and subnormal encodings.
    [[nodiscard]] CompactMaterialCanonicalConstants
        CanonicalizeCompactMaterialConstants(
            const MaterialConstants& constants);

    // Captures the already-written final material record, then proves a fresh
    // live FillConstantBuffer result still matches it. Validation repeats the
    // same lossless comparison immediately before command recording.
    [[nodiscard]] FallbackReason CaptureCompactMaterialFingerprint(
        const std::shared_ptr<const donut::engine::Material>& material,
        const MaterialConstants& finalConstants,
        const CompactMaterialTextureDescriptorRecord& descriptorRecord,
        CompactMaterialFingerprint& fingerprint,
        const CompactTextureQualificationIdentity& qualificationIdentity);
    [[nodiscard]] FallbackReason ValidateCompactMaterialFingerprint(
        const CompactMaterialFingerprint& fingerprint,
        const std::shared_ptr<const donut::engine::Material>& material,
        const CompactMaterialTextureDescriptorRecord& descriptorRecord,
        const CompactTextureQualificationIdentity& qualificationIdentity);

    // Replaces Donut's transient bindless texture indices with the immutable
    // compact-scene descriptor slots selected for this snapshot. The update is
    // transactional: a missing enabled identity or capacity failure leaves the
    // constants, descriptor record, and slot planner unchanged.
    [[nodiscard]] FallbackReason PlanCompactMaterialTextureDescriptors(
        MaterialConstants& constants,
        const std::array<
            uintptr_t,
            CompactMaterialTextureSemanticCount>& resourceIdentities,
        CompactDescriptorSlotPlanner& slotPlanner,
        CompactMaterialTextureDescriptorRecord& descriptorRecord);

    enum CompactGeometryFlags : uint32_t
    {
        CompactGeometryFlag_None = 0u,
        CompactGeometryFlag_Skinned = 1u << 0u,
        CompactGeometryFlag_AlphaTested = 1u << 1u,
        CompactGeometryFlag_DoubleSided = 1u << 2u
    };

    enum CompactGeometryInstanceFlags : uint32_t
    {
        CompactGeometryInstanceFlag_None = 0u,
        CompactGeometryInstanceFlag_Valid = 1u << 0u,
        CompactGeometryInstanceFlag_DeformationSupported = 1u << 1u,
        CompactGeometryInstanceFlag_MaterialSupported = 1u << 2u
    };

    enum class CompactGeometryPayloadClass : uint32_t
    {
        Invalid = 0u,
        Reconstruct = 1u,
        FullGBufferFallback = 2u
    };

    // Indexed by the slot decoded from geometryInstanceHandle. Generation and
    // epoch are validated before any indirection is followed.
    struct CompactGeometryInstanceGpuRecord
    {
        uint32_t generation = 0u;
        uint32_t geometryIndex = InvalidVisibilityWord;
        uint32_t instanceIndex = InvalidVisibilityWord;
        uint32_t geometryRecordIndex = InvalidVisibilityWord;

        uint32_t stableInstanceHandleLow = 0u;
        uint32_t stableInstanceHandleHigh = 0u;
        uint32_t topologyEpochLow = 0u;
        uint32_t topologyEpochHigh = 0u;

        uint32_t topologyRevision = 0u;
        uint32_t lodRevision = 0u;
        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;

        uint32_t primitiveCount = 0u;
        uint32_t flags = CompactGeometryInstanceFlag_None;
        uint32_t payloadClass = static_cast<uint32_t>(
            CompactGeometryPayloadClass::Invalid);
        uint32_t payloadFallbackReason = static_cast<uint32_t>(
            FallbackReason::ReconstructionFailure);
    };

    // Byte offsets address Donut's public raw BufferGroup streams. Static
    // geometry aliases previousPositionOffset to positionOffset; skinned
    // geometry has a distinct previous-position stream.
    struct CompactGeometryGpuRecord
    {
        uint32_t indexBufferDescriptor = InvalidVisibilityWord;
        uint32_t vertexBufferDescriptor = InvalidVisibilityWord;
        uint32_t indexOffset = 0u;
        uint32_t positionOffset = 0u;

        uint32_t previousPositionOffset = 0u;
        uint32_t texCoordOffset = 0u;
        uint32_t normalOffset = 0u;
        uint32_t tangentOffset = 0u;

        uint32_t indexCount = 0u;
        uint32_t vertexCount = 0u;
        uint32_t materialIndex = InvalidVisibilityWord;
        uint32_t flags = CompactGeometryFlag_None;
    };

    struct CompactSceneGpuConstants
    {
        uint32_t topologyEpochLow = 0u;
        uint32_t topologyEpochHigh = 0u;
        uint32_t topologyRevision = 0u;
        uint32_t lodRevision = 0u;

        uint32_t geometryInstanceRecordCount = 0u;
        uint32_t geometryRecordCount = 0u;
        uint32_t materialRecordCount = 0u;
        uint32_t instanceHandleCount = 0u;

        uint32_t instanceBufferDescriptor = InvalidVisibilityWord;
        uint32_t materialBufferDescriptor = InvalidVisibilityWord;
        uint32_t rawBufferDescriptorCount = 0u;
        uint32_t textureDescriptorCount = 0u;

        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;
        uint32_t descriptorCount = 0u;
        uint32_t reserved0 = 0u;
    };

    static_assert(sizeof(CompactGeometryInstanceGpuRecord) == 64u);
    static_assert(offsetof(
        CompactGeometryInstanceGpuRecord,
        payloadClass) == 56u);
    static_assert(offsetof(
        CompactGeometryInstanceGpuRecord,
        payloadFallbackReason) == 60u);
    static_assert(sizeof(CompactGeometryGpuRecord) == 48u);
    static_assert(sizeof(CompactSceneGpuConstants) == 64u);

    [[nodiscard]] inline bool HasExactCompactDescriptorPartition(
        const CompactSceneGpuConstants& constants)
    {
        return constants.textureDescriptorCount <=
                constants.descriptorCount &&
            constants.rawBufferDescriptorCount ==
                constants.descriptorCount -
                    constants.textureDescriptorCount;
    }

    [[nodiscard]] inline bool IsCompactTextureDescriptor(
        const CompactSceneGpuConstants& constants,
        uint32_t descriptor)
    {
        return HasExactCompactDescriptorPartition(constants) &&
            descriptor != InvalidVisibilityWord &&
            descriptor < constants.textureDescriptorCount;
    }

    [[nodiscard]] inline bool IsCompactRawBufferDescriptor(
        const CompactSceneGpuConstants& constants,
        uint32_t descriptor)
    {
        return HasExactCompactDescriptorPartition(constants) &&
            descriptor != InvalidVisibilityWord &&
            descriptor >= constants.textureDescriptorCount &&
            descriptor < constants.descriptorCount;
    }

    struct CompactByteRange
    {
        uint64_t byteOffset = 0u;
        uint64_t byteSize = 0u;
    };

    // Device-independent description used by both runtime preflight and
    // focused tests. All arithmetic is deliberately 64-bit so a source that
    // cannot be represented by the 32-bit GPU records fails before narrowing.
    struct CompactGeometryValidationDesc
    {
        bool geometrySupported = true;
        bool deformationSupported = true;
        bool materialSupported = true;
        bool indexBufferAvailable = true;
        bool vertexBufferAvailable = true;
        bool previousPositionRequired = false;

        uint64_t instanceIndex = 0u;
        uint64_t instanceCount = 1u;
        uint64_t flattenedHandleIndex = 0u;
        uint64_t handleMapCount = 1u;
        uint64_t materialIndex = 0u;
        uint64_t materialCount = 1u;

        uint64_t indexBufferByteSize = 0u;
        uint64_t vertexBufferByteSize = 0u;
        uint64_t firstIndex = 0u;
        uint64_t indexCount = 0u;
        uint64_t firstVertex = 0u;
        uint64_t vertexCount = 0u;

        CompactByteRange positionRange;
        CompactByteRange previousPositionRange;
        CompactByteRange texCoordRange;
        CompactByteRange normalRange;
        CompactByteRange tangentRange;
    };

    struct CompactSceneEligibility
    {
        bool eligible = false;
        FallbackReason reason = FallbackReason::ReconstructionFailure;
        uint32_t failingGeometry = InvalidVisibilityWord;

        [[nodiscard]] bool Succeeded() const
        {
            return eligible && reason == FallbackReason::None;
        }
    };

    struct CompactGeometryPayloadDecision
    {
        CompactGeometryPayloadClass payloadClass =
            CompactGeometryPayloadClass::Invalid;
        FallbackReason reason = FallbackReason::ReconstructionFailure;

        [[nodiscard]] bool Succeeded() const
        {
            return payloadClass != CompactGeometryPayloadClass::Invalid;
        }
    };

    struct CompactScenePartitionSummary
    {
        uint32_t identityGeometryCount = 0u;
        uint32_t reconstructGeometryCount = 0u;
        uint32_t fallbackGeometryCount = 0u;
        uint32_t reconstructMaterialRecordCount = 0u;
        FallbackReason firstFallbackReason = FallbackReason::None;

        [[nodiscard]] bool IsMixed() const
        {
            return reconstructGeometryCount != 0u &&
                fallbackGeometryCount != 0u;
        }

        [[nodiscard]] bool IsAllFallback() const
        {
            return identityGeometryCount != 0u &&
                reconstructGeometryCount == 0u &&
                fallbackGeometryCount == identityGeometryCount;
        }
    };

    [[nodiscard]] FallbackReason ValidateCompactGeometry(
        const CompactGeometryValidationDesc& geometry);

    // Unsupported deformation or material features retain exact ownership
    // identity and select the ordinary full G-buffer payload. Structural,
    // mapping, primitive, and capacity failures remain fatal.
    [[nodiscard]] CompactGeometryPayloadDecision
        ClassifyCompactGeometryPayload(
            const CompactGeometryValidationDesc& geometry);

    // Compact reconstruction must never discover NaN/Inf material samples
    // after visibility has replaced the full G-buffer. Normalized texture
    // formats have finite decoded float values; integer textures do not match
    // the filtered Texture2D<float4> binding contract, and floating-point or
    // depth/stencil formats require the full G-buffer payload until their
    // texels are independently proven finite.
    [[nodiscard]] bool IsCompactFiniteMaterialTextureFormat(
        nvrhi::Format format);

    // Returns the first fatal structural failure in source order. Expected
    // unsupported deformation/material records remain successful identity
    // entries and are counted in the full G-buffer fallback partition.
    [[nodiscard]] CompactSceneEligibility EvaluateCompactSceneEligibility(
        const CompactGeometryValidationDesc* geometries,
        size_t geometryCount,
        uint64_t descriptorCount,
        uint64_t descriptorCapacity,
        uint64_t geometryInstanceCapacity,
        CompactScenePartitionSummary* partitionSummary = nullptr);

    struct StableInstanceHandleResult
    {
        uint64_t stableInstanceHandle = 0u;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    // Control-block identity avoids pointer-address aliasing when a destroyed
    // scene instance and its replacement occupy the same address. Handles are
    // monotonic and never reused during the allocator's lifetime.
    class StableInstanceHandleAllocator
    {
    public:
        explicit StableInstanceHandleAllocator(
            uint64_t firstHandle = 1u);

        [[nodiscard]] StableInstanceHandleResult Resolve(
            const std::shared_ptr<const void>& owner);

        void PruneExpired();
        [[nodiscard]] size_t TrackedOwnerCount() const;
        [[nodiscard]] bool Exhausted() const;

    private:
        struct OwnerEntry
        {
            std::weak_ptr<const void> owner;
            uint64_t stableInstanceHandle = 0u;
        };

        std::vector<OwnerEntry> m_Owners;
        uint64_t m_NextHandle = 1u;
        bool m_Exhausted = false;
        FallbackReason m_ConfigurationReason = FallbackReason::None;
    };

    struct CompactGeometryIdentityDesc
    {
        uint64_t stableInstanceHandle = 0u;
        uint32_t geometryIndex = 0u;
        uint32_t topologyRevision = 0u;
        uint32_t lodRevision = 0u;
        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;
        bool deformationSupported = true;
        bool materialSupported = true;
    };

    struct CompactGeometryHandleAssignment
    {
        CompactGeometryIdentityDesc identity;
        uint32_t geometryInstanceHandle = InvalidVisibilityWord;
    };

    struct CompactGeometryHandleReconcileResult
    {
        std::vector<CompactGeometryHandleAssignment> assignments;
        FallbackReason reason = FallbackReason::None;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    // Reconciliation is transactional: failures leave prior mappings
    // addressable. Generation exhaustion additionally latches the required
    // wider-epoch reset and blocks any same-epoch replacement.
    class CompactGeometryHandleRegistry
    {
    public:
        explicit CompactGeometryHandleRegistry(uint32_t capacity);

        [[nodiscard]] CompactGeometryHandleReconcileResult Reconcile(
            uint64_t topologyEpoch,
            const CompactGeometryIdentityDesc* identities,
            size_t identityCount);

        [[nodiscard]] RegistryLookupResult Inspect(
            uint32_t geometryInstanceHandle) const;
        [[nodiscard]] SurfaceResolutionResult ResolveSurfaceKey(
            const GpuVisibilityKey& key,
            const RegistryValidation& validation) const;

        [[nodiscard]] uint32_t Capacity() const;
        [[nodiscard]] uint32_t ActiveEntryCount() const;
        [[nodiscard]] uint64_t TopologyEpoch() const;
        [[nodiscard]] bool RequiresEpochReset() const;

    private:
        struct Identity
        {
            uint64_t stableInstanceHandle = 0u;
            uint32_t geometryIndex = 0u;

            [[nodiscard]] bool operator <(const Identity& other) const;
        };

        struct Mapping
        {
            uint32_t geometryInstanceHandle = InvalidVisibilityWord;
            uint32_t topologyRevision = 0u;
            uint32_t lodRevision = 0u;
        };

        SurfaceRegistry m_Registry;
        std::map<Identity, Mapping> m_Mappings;
        bool m_RequiresEpochReset = false;
    };

    struct CompactSceneBuildParameters
    {
        uint64_t topologyEpoch = 0u;
        uint32_t topologyRevision = 0u;
        uint32_t lodRevision = 0u;
        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;
        bool supportSkinnedMeshes = true;
        bool supportAlphaTestedMaterials = false;
        // A null/empty pair keeps textured materials on the exact local full
        // G-buffer path. A nonempty pair must be active in this authority at
        // build time and is copied into every textured fingerprint.
        const TextureQualificationCertificateAuthority*
            textureQualificationAuthority = nullptr;
        CompactTextureQualificationIdentity textureQualification;
        donut::engine::TextureCache* finalizedTextureCache = nullptr;
    };

    struct CompactEligibilityRevisionIdentity
    {
        uint64_t topologyEpoch = 0u;
        uint32_t topologyRevision = 0u;
        uint32_t lodRevision = 0u;
        uint32_t deformationRevision = 0u;
        uint32_t materialRevision = 0u;
        uint64_t textureContentRevision = 0u;
        uint64_t textureQualificationToken = 0u;
    };

    // A command-free compact preflight failure suppresses only another attempt
    // against the exact scene revisions that produced it. Relevant scene
    // revision changes therefore permit one fresh attempt without weakening
    // the independent snapshot-staleness state.
    class CompactEligibilityFallbackLatch final
    {
    public:
        void Clear();
        void Record(
            const CompactEligibilityRevisionIdentity& identity,
            FallbackReason reason);
        [[nodiscard]] FallbackReason Reason(
            const CompactEligibilityRevisionIdentity& identity) const;

    private:
        CompactEligibilityRevisionIdentity m_Identity;
        FallbackReason m_Reason = FallbackReason::None;
    };

    struct CompactSceneSnapshotVersion
    {
        uint64_t topologyEpoch = 0u;
        uint64_t buildSerial = 0u;
    };

    // A topology epoch change dominates the per-builder serial. Within one
    // epoch, publication is strictly newest-only; clearing a publisher does
    // not make an older snapshot eligible again.
    [[nodiscard]] FallbackReason ValidateCompactSceneSnapshotReplacement(
        const CompactSceneSnapshotVersion& candidate,
        const CompactSceneSnapshotVersion& highestPublished);

    class CompactSceneSnapshot final
    {
    public:
        ~CompactSceneSnapshot() = default;

        CompactSceneSnapshot(const CompactSceneSnapshot&) = delete;
        CompactSceneSnapshot& operator=(
            const CompactSceneSnapshot&) = delete;

        [[nodiscard]] const CompactSceneEligibility& Eligibility() const;
        [[nodiscard]] const CompactSceneGpuConstants& GpuConstants() const;
        [[nodiscard]] const CompactScenePartitionSummary&
            PartitionSummary() const;
        [[nodiscard]] uint64_t TopologyEpoch() const;
        [[nodiscard]] uint64_t BuildSerial() const;
        [[nodiscard]] CompactSceneSnapshotVersion Version() const;
        [[nodiscard]] CompactTextureQualificationIdentity
            TextureQualification() const;
        [[nodiscard]] bool RequiresTextureQualification() const;
        [[nodiscard]] nvrhi::CommandQueue RequiredCommandQueue() const;

        [[nodiscard]] nvrhi::IBindingLayout* BindlessLayout() const;
        [[nodiscard]] nvrhi::IDescriptorTable* DescriptorTable() const;
        [[nodiscard]] nvrhi::IBuffer* GeometryInstanceBuffer() const;
        [[nodiscard]] nvrhi::IBuffer* GeometryBuffer() const;
        [[nodiscard]] nvrhi::IBuffer* MaterialBuffer() const;
        [[nodiscard]] nvrhi::IBuffer* InstanceHandleBuffer() const;
        [[nodiscard]] nvrhi::IBuffer* SceneInstanceBuffer() const;

        // Descriptor tables deliberately perform no state tracking in NVRHI.
        // This pure check lets command-free frame preflight prove that the
        // later transition call cannot fail after scene-buffer writes begin.
        [[nodiscard]] FallbackReason ValidateReadResources() const;

        // Call this from a command list on RequiredCommandQueue() before
        // binding the compact snapshot; a subsequent pipeline state change or
        // explicit commitBarriers() submits the transitions.
        [[nodiscard]] FallbackReason PrepareReadStates(
            nvrhi::ICommandList* commandList) const;

        // Records only the state transitions captured by a successful
        // command-free ValidateReadResources check. The renderer uses this
        // after its final pre-open transaction revalidation, so command
        // recording contains no second recoverable validation branch.
        void RecordPreparedReadStateTransitions(
            nvrhi::ICommandList* commandList) const;

        [[nodiscard]] const std::vector<
            CompactGeometryInstanceGpuRecord>&
            CpuGeometryInstanceRecords() const;
        [[nodiscard]] const std::vector<CompactGeometryGpuRecord>&
            CpuGeometryRecords() const;
        [[nodiscard]] const std::vector<uint32_t>&
            CpuInstanceHandles() const;
        [[nodiscard]] const std::vector<
            CompactMaterialTextureDescriptorRecord>&
            CpuMaterialTextureDescriptors() const;
        [[nodiscard]] const std::vector<CompactMaterialFingerprint>&
            CpuMaterialFingerprints() const;

    private:
        friend class CompactSceneBuilder;

        CompactSceneSnapshot() = default;

        CompactSceneEligibility m_Eligibility;
        CompactSceneGpuConstants m_GpuConstants;
        CompactScenePartitionSummary m_PartitionSummary;
        CompactSceneSnapshotVersion m_Version;
        CompactTextureQualificationIdentity m_TextureQualification;
        nvrhi::CommandQueue m_RequiredCommandQueue =
            CompactSceneCommandQueue;

        nvrhi::BindingLayoutHandle m_BindlessLayout;
        nvrhi::DescriptorTableHandle m_DescriptorTable;
        nvrhi::BufferHandle m_GeometryInstanceBuffer;
        nvrhi::BufferHandle m_GeometryBuffer;
        nvrhi::BufferHandle m_MaterialBuffer;
        nvrhi::BufferHandle m_InstanceHandleBuffer;
        nvrhi::BufferHandle m_SceneInstanceBuffer;

        std::vector<nvrhi::TextureHandle> m_TextureResources;
        std::vector<uint8_t> m_TextureFinalizedLoadTimeMipRenderTargets;
        std::vector<nvrhi::BufferHandle> m_RawBufferResources;
        std::vector<CompactGeometryInstanceGpuRecord>
            m_CpuGeometryInstanceRecords;
        std::vector<CompactGeometryGpuRecord> m_CpuGeometryRecords;
        std::vector<uint32_t> m_CpuInstanceHandles;
        std::vector<CompactMaterialTextureDescriptorRecord>
            m_CpuMaterialTextureDescriptors;
        std::vector<CompactMaterialFingerprint>
            m_CpuMaterialFingerprints;
    };

    struct CompactSceneBuildResult
    {
        std::shared_ptr<const CompactSceneSnapshot> snapshot;
        CompactSceneEligibility eligibility;

        [[nodiscard]] bool Succeeded() const
        {
            return snapshot != nullptr && eligibility.Succeeded();
        }
    };

    class CompactSceneBuilder final
    {
    public:
        CompactSceneBuilder(
            nvrhi::IDevice* device,
            uint32_t geometryInstanceCapacity,
            uint32_t descriptorCapacity =
                DefaultCompactDescriptorCapacity,
            nvrhi::CommandQueue commandQueue =
                CompactSceneCommandQueue);
        ~CompactSceneBuilder();

        CompactSceneBuilder(const CompactSceneBuilder&) = delete;
        CompactSceneBuilder& operator=(const CompactSceneBuilder&) = delete;

        [[nodiscard]] CompactSceneBuildResult Build(
            nvrhi::ICommandList* commandList,
            const donut::engine::Scene& scene,
            const CompactSceneBuildParameters& parameters);

        [[nodiscard]] FallbackReason ConfigurationReason() const;
        [[nodiscard]] uint32_t GeometryInstanceCapacity() const;
        [[nodiscard]] uint32_t DescriptorCapacity() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // Calls that replace or clear the active snapshot must occur after the
    // last command list using the old snapshot has been submitted on the
    // explicit publisher queue. The event query is then enqueued behind that
    // work, and the queue owns the old snapshot until completion is observed.
    class CompactSceneSnapshotPublisher final
    {
    public:
        explicit CompactSceneSnapshotPublisher(
            nvrhi::IDevice* device,
            nvrhi::CommandQueue commandQueue =
                CompactSceneCommandQueue);
        ~CompactSceneSnapshotPublisher();

        CompactSceneSnapshotPublisher(
            const CompactSceneSnapshotPublisher&) = delete;
        CompactSceneSnapshotPublisher& operator=(
            const CompactSceneSnapshotPublisher&) = delete;

        [[nodiscard]] FallbackReason PublishAtFrameBoundary(
            std::shared_ptr<const CompactSceneSnapshot> replacement);
        [[nodiscard]] FallbackReason ClearAtFrameBoundary();

        void PollRetired();
        void DrainRetired();

        [[nodiscard]] std::shared_ptr<const CompactSceneSnapshot>
            ActiveSnapshot() const;
        [[nodiscard]] size_t RetiredSnapshotCount() const;

    private:
        struct RetiredSnapshot
        {
            std::shared_ptr<const CompactSceneSnapshot> snapshot;
            nvrhi::EventQueryHandle completionQuery;
        };

        [[nodiscard]] FallbackReason RetireActiveAtFrameBoundary();

        nvrhi::DeviceHandle m_Device;
        nvrhi::CommandQueue m_CommandQueue = CompactSceneCommandQueue;
        std::shared_ptr<const CompactSceneSnapshot> m_Active;
        std::vector<RetiredSnapshot> m_Retired;
        CompactSceneSnapshotVersion m_HighestPublishedVersion;
        bool m_HasPublishedVersion = false;
    };
}
