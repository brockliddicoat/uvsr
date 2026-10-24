#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace uvsr::adaptive_msaa
{
    inline constexpr size_t TextureQualificationSha256ByteCount = 32u;
    using TextureQualificationSha256 = std::array<
        uint8_t,
        TextureQualificationSha256ByteCount>;

    // The wire schema is intentionally fixed-size. It is not a dump of these
    // C++ structures: serialization writes every integer explicitly in little
    // endian and writes every digest byte verbatim. Bumping the schema is
    // required before adding, removing, or reinterpreting a field.
    inline constexpr std::array<uint8_t, 8u>
        TextureQualificationCertificateMagic = {
            'U', 'V', 'S', 'R', 'T', 'Q', 'C', '1'
        };
    inline constexpr uint32_t TextureQualificationCertificateSchema = 1u;
    inline constexpr uint32_t TextureQualificationLittleEndianTag =
        0x01020304u;
    inline constexpr size_t TextureQualificationWireHeaderReservedOffset =
        20u;
    inline constexpr size_t TextureQualificationWireReservedOffset = 516u;
    inline constexpr size_t TextureQualificationWireReservedByteCount = 16u;
    inline constexpr size_t TextureQualificationWireDigestOffset = 532u;
    inline constexpr size_t TextureQualificationCertificateByteCount = 564u;

    inline constexpr uint32_t TextureQualificationViewportParityMask = 0x0fu;
    inline constexpr uint32_t TextureQualificationSamplerSchema = 1u;
    inline constexpr std::array<uint32_t, 3u>
        TextureQualificationSampleCounts = { 2u, 4u, 8u };

    enum class TextureQualificationReason : uint32_t
    {
        None = 0u,

        CertificateMissing,
        HashInputMissing,
        SerializationOutputMissing,
        SerializationOutputTooSmall,
        Sha256ProviderUnavailable,
        Sha256PropertyQueryFailed,
        Sha256StateAllocationFailed,
        Sha256HashCreationFailed,
        Sha256HashDataFailed,
        Sha256HashFinishFailed,

        CertificateTruncated,
        CertificateHasTrailingBytes,
        MagicMismatch,
        SchemaMismatch,
        EndianMismatch,
        DeclaredSizeMismatch,
        ReservedBytesNotZero,
        IntegrityDigestMismatch,

        ExpectedAdapterIdentityMissing,
        ExpectedRuntimeIdentityMissing,
        ExpectedArtifactIdentityMissing,
        ExpectedFeatureFormatIdentityMissing,
        ExpectedSamplerIdentityMissing,
        ExpectedSemanticFormatSchemaMissing,
        ExpectedMatrixSchemaMissing,

        CertificateAdapterIdentityMissing,
        CertificateRuntimeIdentityMissing,
        CertificateArtifactIdentityMissing,
        CertificateFeatureFormatIdentityMissing,
        CertificateSamplerIdentityMissing,
        CertificateSemanticFormatSchemaMissing,
        CertificateMatrixSchemaMissing,

        AdapterLuidMismatch,
        AdapterHardwareMismatch,
        AdapterDriverMismatch,
        DxgiRuntimeModuleMismatch,
        D3d12RuntimeModuleMismatch,
        D3d12CoreRuntimeModuleMismatch,
        DxilRuntimeModuleMismatch,
        RendererExecutableMismatch,
        RendererBuildManifestMismatch,
        QualificationTestExecutableMismatch,
        QualificationTestDxilSetMismatch,
        FeatureFormatSetMismatch,
        SamplerIdentityMismatch,
        SemanticFormatSchemaMismatch,
        MatrixSchemaMismatch,
        MatrixRequiredMaskMismatch,

        QualificationRecordOrderMismatch,
        QualificationMaskMissing,
        QualificationMaskMismatch,
        QualificationParityMaskMalformed,
        IncompleteViewportParityQualification,

        Requested16Resolved8Unsupported,
        Requested16Unsupported,
        Resolved16Unsupported,
        UnsupportedRequestedSampleCount,
        UnsupportedResolvedSampleCount,
        RequestedResolvedSampleCountMismatch,
        InvalidViewportOriginParity,
        ViewportOriginParityNotQualified,
        Count
    };

    struct TextureQualificationStatus
    {
        TextureQualificationReason reason =
            TextureQualificationReason::CertificateMissing;
        size_t byteCount = 0u;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == TextureQualificationReason::None;
        }
    };

    struct TextureQualificationAdapterIdentity
    {
        // Preserve both raw LUID words. The high word is deliberately unsigned
        // here so wire encoding does not depend on signed-integer conversion.
        uint32_t luidLowPart = 0u;
        uint32_t luidHighPartBits = 0u;
        uint32_t vendorId = 0u;
        uint32_t deviceId = 0u;
        uint32_t subsystemId = 0u;
        uint32_t revision = 0u;

        // The exact 64-bit value returned for the selected adapter, not a
        // formatted or truncated marketing version.
        uint64_t driverVersion = 0u;
    };

    struct TextureQualificationD3dRuntimeIdentity
    {
        // Hash the exact loaded module image bytes. A missing module is not
        // represented by a wildcard: a zero digest makes the identity invalid.
        TextureQualificationSha256 dxgiModuleSha256{};
        TextureQualificationSha256 d3d12ModuleSha256{};
        TextureQualificationSha256 d3d12CoreModuleSha256{};
        TextureQualificationSha256 dxilModuleSha256{};
    };

    struct TextureQualificationArtifactIdentity
    {
        TextureQualificationSha256 rendererExecutableSha256{};
        TextureQualificationSha256 rendererBuildManifestSha256{};
        TextureQualificationSha256 qualificationTestExecutableSha256{};

        // This is the deterministic, length-framed aggregate identity of
        // every production and test DXIL bundle loaded by the qualification
        // matrix plus its ordered normalized-path manifest. A path-only
        // manifest is insufficient: every bundle's exact bytes participate.
        TextureQualificationSha256 qualificationTestDxilSetSha256{};
    };

    struct TextureQualificationSamplerIdentity
    {
        // Schema 1 is the exact NVRHI SamplerDesc field set below. Floating
        // values are stored as caller-provided IEEE-754 bit patterns so NaN
        // payloads, signed zero, and rounding cannot alias.
        uint32_t schema = 0u;
        std::array<uint32_t, 4u> borderColorBits{};
        uint32_t maxAnisotropyBits = 0u;
        uint32_t mipBiasBits = 0u;
        uint32_t minFilter = 0u;
        uint32_t magFilter = 0u;
        uint32_t mipFilter = 0u;
        uint32_t addressU = 0u;
        uint32_t addressV = 0u;
        uint32_t addressW = 0u;
        uint32_t reductionType = 0u;
    };

    struct TextureQualificationMatrixIdentity
    {
        uint32_t schema = 0u;

        // Canonical manifest hash for matrix rows, authored inputs, frozen
        // error limits, viewport-origin definitions, and pass/fail semantics.
        TextureQualificationSha256 schemaSha256{};

        // Ordered exactly like TextureQualificationSampleCounts. The validator
        // requires a nonzero exact mask for every count; a subset or superset
        // cannot silently qualify under a different matrix interpretation.
        std::array<uint32_t, 3u> requiredQualificationMasks{};
    };

    struct TextureQualificationLiveIdentity
    {
        TextureQualificationAdapterIdentity adapter;
        TextureQualificationD3dRuntimeIdentity runtime;
        TextureQualificationArtifactIdentity artifacts;

        // Canonical manifest hash for every feature query, semantic resource
        // format, and 2x/4x/8x sample-quality value used by the matrix. This is
        // deliberately not a device-name string or a loosely ordered set.
        TextureQualificationSha256 featureFormatSetSha256{};
        TextureQualificationSamplerIdentity sampler;
        TextureQualificationSha256 semanticFormatSchemaSha256{};
        TextureQualificationMatrixIdentity matrix;
    };

    struct TextureQualificationSampleRecord
    {
        uint32_t sampleCount = 0u;
        uint32_t qualificationMask = 0u;
        uint32_t viewportOriginParityMask = 0u;
    };

    struct TextureQualificationCertificate
    {
        TextureQualificationLiveIdentity identity;
        std::array<TextureQualificationSampleRecord, 3u> samples{};
    };

    struct TextureQualificationValidationRequest
    {
        uint32_t requestedSampleCount = 0u;
        uint32_t resolvedSampleCount = 0u;

        // These are the two actual low bits of the tested viewport origin,
        // each constrained to zero or one. Keeping both explicit prevents a
        // caller from presenting an already-collapsed, ambiguous index.
        uint32_t viewportOriginXParity = 0u;
        uint32_t viewportOriginYParity = 0u;
    };

    struct TextureQualificationValidationResult
    {
        bool accepted = false;
        TextureQualificationReason reason =
            TextureQualificationReason::CertificateMissing;
        uint32_t sampleCount = 0u;
        uint32_t qualificationMask = 0u;
        uint32_t viewportOriginParityIndex = 0u;

        [[nodiscard]] bool Succeeded() const
        {
            return accepted && reason == TextureQualificationReason::None;
        }
    };

    // Pure byte primitive used by the serializer and focused known-vector
    // tests. It performs no file access, caching, certificate issuance, GPU
    // work, readback, or dual rendering.
    [[nodiscard]] TextureQualificationReason
        ComputeTextureQualificationSha256(
            const uint8_t* bytes,
            size_t byteCount,
            TextureQualificationSha256& digest) noexcept;

    [[nodiscard]] const char* GetTextureQualificationReasonName(
        TextureQualificationReason reason) noexcept;

    // Serializes exactly TextureQualificationCertificateByteCount bytes. The
    // destination may be larger, but only the fixed schema size is written and
    // reported. A trailing digest authenticates every preceding byte.
    [[nodiscard]] TextureQualificationStatus
        SerializeTextureQualificationCertificate(
            const TextureQualificationCertificate& certificate,
            uint8_t* destination,
            size_t destinationByteCount) noexcept;

    // Parsing is transactional and exact-size: failure leaves output at its
    // fail-closed default. No trailing extension bytes are accepted.
    [[nodiscard]] TextureQualificationStatus
        ParseTextureQualificationCertificate(
            const uint8_t* source,
            size_t sourceByteCount,
            TextureQualificationCertificate& certificate) noexcept;

    // Compares a parsed certificate with a freshly acquired exact live
    // identity. Acquisition and file I/O deliberately stay outside this core.
    // The certificate must completely qualify 2x, 4x, and 8x, even when the
    // current request selects only one count. 16x and 16x-to-8x demotion never
    // inherit an 8x certificate.
    [[nodiscard]] TextureQualificationValidationResult
        ValidateTextureQualificationCertificate(
            const TextureQualificationCertificate* certificate,
            const TextureQualificationLiveIdentity& expectedLiveIdentity,
            const TextureQualificationValidationRequest& request) noexcept;
}
