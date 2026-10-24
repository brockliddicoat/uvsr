#pragma once

#include "adaptive_msaa_texture_qualification.h"

#include <array>
#include <cstddef>
#include <cstdint>

#ifndef UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC
#define UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC 0
#endif

struct ID3D12Device;

namespace nvrhi
{
    struct SamplerDesc;
    class ISampler;
}

namespace uvsr::adaptive_msaa
{
    inline constexpr wchar_t TextureQualificationBuildManifestFileName[] =
        L"uvsr_adaptive_msaa_build.manifest";
    inline constexpr wchar_t TextureQualificationCertificateFileName[] =
        L"uvsr_compact_texture_qualification.tqc";

    inline constexpr uint32_t TextureQualificationRuntimeSchema = 1u;
    inline constexpr uint32_t TextureQualificationFeatureFormatSchema = 1u;
    inline constexpr uint32_t TextureQualificationSemanticFormatSchema = 1u;
    inline constexpr uint32_t TextureQualificationMatrixRuntimeSchema = 1u;

    enum TextureQualificationEvidenceBits : uint32_t
    {
        TextureQualificationEvidence_RasterSampleDescriptorParity = 1u << 0u,
        TextureQualificationEvidence_GuideCoverageParity = 1u << 1u,
        TextureQualificationEvidence_GuideDepthError = 1u << 2u,
        TextureQualificationEvidence_GuideDiffuseError = 1u << 3u,
        TextureQualificationEvidence_GuideMaterialError = 1u << 4u,
        TextureQualificationEvidence_GuideNormalError = 1u << 5u,
        TextureQualificationEvidence_GuideEmissiveError = 1u << 6u,
        TextureQualificationEvidence_GuideAmbientOcclusionError = 1u << 7u,
        TextureQualificationEvidence_GuideMotionError = 1u << 8u,
        TextureQualificationEvidence_ReferenceSignalDiscrimination = 1u << 9u,
        TextureQualificationEvidence_FiniteOutputs = 1u << 10u,
        TextureQualificationEvidence_ViewportOriginParity = 1u << 11u,
        TextureQualificationEvidence_AlphaCoverageParity = 1u << 12u,
        TextureQualificationEvidence_ValidationClean = 1u << 13u,
        TextureQualificationEvidence_FeatureFormatSupport = 1u << 14u
    };

    inline constexpr uint32_t TextureQualificationRequiredEvidenceMask =
        (1u << 15u) - 1u;
    inline constexpr std::array<uint32_t, 3u>
        TextureQualificationRequiredEvidenceMasks = {
            TextureQualificationRequiredEvidenceMask,
            TextureQualificationRequiredEvidenceMask,
            TextureQualificationRequiredEvidenceMask
        };

    enum class TextureQualificationRuntimeReason : uint32_t
    {
        None = 0u,
        InvalidArgument,
        PathMissing,
        PathTooLong,
        FileOpenFailed,
        FileIsNotRegular,
        FileIsReparsePoint,
        FileSizeInvalid,
        FileReadFailed,
        FileChangedWhileHashing,
        HashProviderUnavailable,
        HashPropertyQueryFailed,
        HashStateAllocationFailed,
        HashCreationFailed,
        HashDataFailed,
        HashFinishFailed,
        AggregateSetEmpty,
        AggregateLabelMissing,
        AggregateLabelInvalid,
        AggregateLabelOrderInvalid,
        AggregateFileEmpty,
        ModuleNotLoaded,
        ModulePathUnavailable,
        ModuleNameMismatch,
        DxgiFactoryUnavailable,
        AdapterUnavailable,
        AdapterDescriptionUnavailable,
        AdapterLuidMismatch,
        AdapterIsSoftware,
        DriverVersionUnavailable,
        D3d12DeviceMissing,
        FeatureQueryFailed,
        RequiredShaderFeatureMissing,
        FormatQueryFailed,
        SamplerMissing,
        SamplerFieldInvalid,
        CompiledSchemaHashFailed,
        CertificateReadFailed,
        CertificateParseFailed,
        CertificateValidationFailed,
        ContentRevisionZero,
        ContentRevisionNotMonotonic,
        LeaseTokenExhausted,
        LeaseTokenMismatch,
        AtomicTemporaryNameExhausted,
        AtomicCreateFailed,
        AtomicWriteFailed,
        AtomicFlushFailed,
        AtomicReplaceFailed,
        Count
    };

    struct TextureQualificationRuntimeStatus
    {
        TextureQualificationRuntimeReason reason =
            TextureQualificationRuntimeReason::InvalidArgument;
        TextureQualificationReason certificateReason =
            TextureQualificationReason::None;
        uint32_t systemError = 0u;
        int32_t nativeResult = 0;
        uint64_t byteCount = 0u;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == TextureQualificationRuntimeReason::None;
        }
    };

    struct TextureQualificationLabeledFile
    {
        // Labels are canonical UTF-8 path-like identifiers, not filesystem
        // paths. Callers provide them in strictly increasing byte order.
        const char* label = nullptr;
        const wchar_t* path = nullptr;
    };

    struct TextureQualificationD3dRuntimeFileInput
    {
        const wchar_t* dxgiPath = nullptr;
        const wchar_t* d3d12Path = nullptr;
        const wchar_t* d3d12CorePath = nullptr;
        const wchar_t* dxilPath = nullptr;
    };

    struct TextureQualificationLoadedD3dModules
    {
        // These are HMODULE values kept opaque in the public header.
        void* dxgi = nullptr;
        void* d3d12 = nullptr;
        void* d3d12Core = nullptr;
        void* dxil = nullptr;
    };

    inline constexpr size_t TextureQualificationFeatureFormatRecordCount =
        17u;

    struct TextureQualificationFeatureFormatRecord
    {
        uint32_t dxgiFormat = 0u;
        uint32_t formatQuerySucceeded = 0u;
        uint32_t support1 = 0u;
        uint32_t support2 = 0u;
        std::array<uint32_t, 3u> qualityQuerySucceeded{};
        std::array<uint32_t, 3u> qualityLevelCount{};
    };

    struct TextureQualificationFeatureFormatSnapshot
    {
        uint32_t schema = 0u;
        uint32_t highestShaderModel = 0u;
        uint32_t resourceBindingTier = 0u;
        uint32_t waveOps = 0u;
        uint32_t waveLaneCountMin = 0u;
        uint32_t waveLaneCountMax = 0u;
        uint32_t int64ShaderOps = 0u;
        std::array<
            TextureQualificationFeatureFormatRecord,
            TextureQualificationFeatureFormatRecordCount> formats{};
    };

    struct TextureQualificationArtifactPathInput
    {
        const wchar_t* rendererExecutablePath = nullptr;
        const wchar_t* rendererBuildManifestPath = nullptr;
        const wchar_t* qualificationTestExecutablePath = nullptr;

        // This one set contains every production and test shader blob loaded
        // by the qualification matrix. The digest is stored in the core field
        // named qualificationTestDxilSetSha256.
        const TextureQualificationLabeledFile* matrixShaderFiles = nullptr;
        size_t matrixShaderFileCount = 0u;
    };

    struct TextureQualificationLiveIdentityInput
    {
        ID3D12Device* d3d12Device = nullptr;
        nvrhi::ISampler* materialSampler = nullptr;
        TextureQualificationLoadedD3dModules loadedModules;
        TextureQualificationArtifactPathInput artifacts;
    };

    struct TextureQualificationCertificateReadResult
    {
        TextureQualificationRuntimeStatus status;
        TextureQualificationCertificate certificate;
        TextureQualificationSha256 fileSha256{};
    };

    [[nodiscard]] const char* GetTextureQualificationRuntimeReasonName(
        TextureQualificationRuntimeReason reason) noexcept;

    // Hashes the exact bytes of one regular non-reparse-point file. The file
    // is opened without write/delete sharing and its identity is checked again
    // after hashing so concurrent staging fails closed.
    [[nodiscard]] TextureQualificationRuntimeStatus
        ComputeTextureQualificationFileSha256(
            const wchar_t* path,
            TextureQualificationSha256& digest) noexcept;

    // Domain-separated canonical aggregate:
    // magic, schema, record count, then for each record a little-endian label
    // length, label bytes, file byte length, and exact file bytes.
    [[nodiscard]] TextureQualificationRuntimeStatus
        ComputeTextureQualificationLabeledFileSetSha256(
            const TextureQualificationLabeledFile* files,
            size_t fileCount,
            TextureQualificationSha256& digest) noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        PinTextureQualificationDxilModule() noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        ResolveTextureQualificationLoadedD3dModules(
            TextureQualificationLoadedD3dModules& modules) noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        AcquireTextureQualificationD3dRuntimeIdentity(
            const TextureQualificationLoadedD3dModules& modules,
            TextureQualificationD3dRuntimeIdentity& identity) noexcept;

    // File-input seam for CPU tests and offline tooling. Production should use
    // the loaded-module overload above.
    [[nodiscard]] TextureQualificationRuntimeStatus
        AcquireTextureQualificationD3dRuntimeIdentityFromFiles(
            const TextureQualificationD3dRuntimeFileInput& input,
            TextureQualificationD3dRuntimeIdentity& identity) noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        AcquireTextureQualificationAdapterIdentity(
            ID3D12Device* device,
            TextureQualificationAdapterIdentity& identity) noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        CaptureTextureQualificationSamplerIdentity(
            const nvrhi::SamplerDesc& description,
            TextureQualificationSamplerIdentity& identity) noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        CaptureTextureQualificationSamplerIdentity(
            nvrhi::ISampler* sampler,
            TextureQualificationSamplerIdentity& identity) noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        AcquireTextureQualificationFeatureFormatSnapshot(
            ID3D12Device* device,
            TextureQualificationFeatureFormatSnapshot& snapshot) noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        ComputeTextureQualificationFeatureFormatDigest(
            const TextureQualificationFeatureFormatSnapshot& snapshot,
            TextureQualificationSha256& digest) noexcept;

    [[nodiscard]] uint32_t GetTextureQualificationFeatureFormatDxgiFormat(
        size_t index) noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        GetTextureQualificationCompiledSchemaIdentity(
            TextureQualificationSha256& semanticFormatSchemaSha256,
            TextureQualificationMatrixIdentity& matrix) noexcept;

    [[nodiscard]] TextureQualificationRuntimeStatus
        AcquireTextureQualificationLiveIdentity(
            const TextureQualificationLiveIdentityInput& input,
            TextureQualificationLiveIdentity& identity) noexcept;

    [[nodiscard]] TextureQualificationCertificateReadResult
        ReadTextureQualificationCertificateFile(
            const wchar_t* path) noexcept;

    // Explicit issuer-only operation. Ordinary runtime code should call only
    // the read/acquisition APIs. The replacement is same-directory and atomic.
    [[nodiscard]] TextureQualificationRuntimeStatus
        WriteTextureQualificationCertificateFileAtomically(
            const wchar_t* path,
            const TextureQualificationCertificate& certificate) noexcept;

    struct TextureQualificationCertificateLease
    {
        uint64_t token = 0u;
        uint64_t contentRevision = 0u;
        TextureQualificationCertificate certificate;
        TextureQualificationLiveIdentity liveIdentity;
        TextureQualificationValidationRequest request;
        TextureQualificationValidationResult validation;

        [[nodiscard]] bool IsValid() const
        {
            return token != 0u && contentRevision != 0u &&
                validation.Succeeded();
        }
    };

    struct TextureQualificationLeaseResult
    {
        TextureQualificationRuntimeStatus status;
        uint64_t token = 0u;
        uint64_t contentRevision = 0u;

        [[nodiscard]] bool Succeeded() const
        {
            return status.Succeeded() && token != 0u &&
                contentRevision != 0u;
        }
    };

    struct TextureQualificationAuthoritySeed
    {
        uint64_t nextToken = 1u;
        uint64_t lastContentRevision = 0u;
    };

    class TextureQualificationCertificateAuthority final
    {
    public:
        TextureQualificationCertificateAuthority() noexcept;

        // The seeded constructor is an isolated deterministic seam for token
        // exhaustion tests; it does not participate in the process allocator.
        // Production must use the default constructor.
        explicit TextureQualificationCertificateAuthority(
            TextureQualificationAuthoritySeed seed) noexcept;

        TextureQualificationCertificateAuthority(
            const TextureQualificationCertificateAuthority&) = delete;
        TextureQualificationCertificateAuthority& operator=(
            const TextureQualificationCertificateAuthority&) = delete;
        TextureQualificationCertificateAuthority(
            TextureQualificationCertificateAuthority&&) = delete;
        TextureQualificationCertificateAuthority& operator=(
            TextureQualificationCertificateAuthority&&) = delete;

        [[nodiscard]] TextureQualificationLeaseResult Acquire(
            const uint8_t* certificateBytes,
            size_t certificateByteCount,
            const TextureQualificationLiveIdentity& liveIdentity,
            const TextureQualificationValidationRequest& request,
            uint64_t contentRevision) noexcept;

        [[nodiscard]] TextureQualificationLeaseResult Acquire(
            const TextureQualificationCertificate& certificate,
            const TextureQualificationLiveIdentity& liveIdentity,
            const TextureQualificationValidationRequest& request,
            uint64_t contentRevision) noexcept;

        [[nodiscard]] TextureQualificationRuntimeStatus Revoke(
            uint64_t token) noexcept;

        [[nodiscard]] const TextureQualificationCertificateLease*
            ActiveLease() const noexcept;
        // Revalidates a per-view sample-count/parity request against the
        // retained certificate and live identity without minting a token or
        // consuming a content revision.
        [[nodiscard]] TextureQualificationValidationResult ValidateActive(
            const TextureQualificationValidationRequest& request) const
            noexcept;
        [[nodiscard]] bool IsActive(
            uint64_t token,
            uint64_t contentRevision) const noexcept;
        // Default authorities report process-wide allocation state. Seeded
        // test authorities report only their isolated deterministic state.
        [[nodiscard]] bool TokenExhausted() const noexcept;
        [[nodiscard]] uint64_t LastContentRevision() const noexcept;

    private:
        [[nodiscard]] TextureQualificationLeaseResult CommitValidated(
            const TextureQualificationCertificate& certificate,
            const TextureQualificationLiveIdentity& liveIdentity,
            const TextureQualificationValidationRequest& request,
            const TextureQualificationValidationResult& validation,
            uint64_t contentRevision) noexcept;

        uint64_t m_NextToken = 1u;
        uint64_t m_LastContentRevision = 0u;
        bool m_UsesProcessState = false;
        bool m_TokenExhausted = false;
        bool m_HasActiveLease = false;
        TextureQualificationCertificateLease m_ActiveLease;
    };
}
