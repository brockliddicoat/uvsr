#include "adaptive_msaa_texture_qualification.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
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
            std::cerr <<
                "Adaptive MSAA texture qualification validation failed: " <<
                message << '\n';
            g_Passed = false;
        }
    }

    [[nodiscard]] TextureQualificationSha256 MakeDigest(uint8_t seed)
    {
        TextureQualificationSha256 digest;
        for (size_t byte = 0u; byte < digest.size(); ++byte)
        {
            digest[byte] = static_cast<uint8_t>(
                uint32_t(seed) + uint32_t(byte) * 17u + 1u);
        }
        return digest;
    }

    [[nodiscard]] TextureQualificationLiveIdentity MakeLiveIdentity()
    {
        TextureQualificationLiveIdentity identity;
        identity.adapter.luidLowPart = 0x11223344u;
        identity.adapter.luidHighPartBits = 0x89abcdefu;
        identity.adapter.vendorId = 0x10deu;
        identity.adapter.deviceId = 0x2757u;
        identity.adapter.subsystemId = 0x16c21043u;
        identity.adapter.revision = 0xa1u;
        identity.adapter.driverVersion = 0x002000000010002fu;

        identity.runtime.dxgiModuleSha256 = MakeDigest(1u);
        identity.runtime.d3d12ModuleSha256 = MakeDigest(2u);
        identity.runtime.d3d12CoreModuleSha256 = MakeDigest(3u);
        identity.runtime.dxilModuleSha256 = MakeDigest(4u);

        identity.artifacts.rendererExecutableSha256 = MakeDigest(5u);
        identity.artifacts.rendererBuildManifestSha256 = MakeDigest(6u);
        identity.artifacts.qualificationTestExecutableSha256 = MakeDigest(7u);
        identity.artifacts.qualificationTestDxilSetSha256 = MakeDigest(8u);
        identity.featureFormatSetSha256 = MakeDigest(9u);

        identity.sampler.schema = TextureQualificationSamplerSchema;
        identity.sampler.borderColorBits = {
            0x3f800000u,
            0x3f800000u,
            0x3f800000u,
            0x3f800000u
        };
        identity.sampler.maxAnisotropyBits = 0x41800000u;
        identity.sampler.mipBiasBits = 0u;
        identity.sampler.minFilter = 1u;
        identity.sampler.magFilter = 1u;
        identity.sampler.mipFilter = 1u;
        identity.sampler.addressU = 1u;
        identity.sampler.addressV = 1u;
        identity.sampler.addressW = 1u;
        identity.sampler.reductionType = 0u;

        identity.semanticFormatSchemaSha256 = MakeDigest(10u);
        identity.matrix.schema = 3u;
        identity.matrix.schemaSha256 = MakeDigest(11u);
        identity.matrix.requiredQualificationMasks = {
            0x00000541u,
            0x0000023au,
            0x00000484u
        };
        return identity;
    }

    [[nodiscard]] TextureQualificationCertificate MakeCertificate()
    {
        TextureQualificationCertificate certificate;
        certificate.identity = MakeLiveIdentity();
        for (size_t index = 0u; index < certificate.samples.size(); ++index)
        {
            certificate.samples[index].sampleCount =
                TextureQualificationSampleCounts[index];
            certificate.samples[index].qualificationMask =
                certificate.identity.matrix.requiredQualificationMasks[index];
            certificate.samples[index].viewportOriginParityMask =
                TextureQualificationViewportParityMask;
        }
        return certificate;
    }

    [[nodiscard]] TextureQualificationValidationRequest MakeRequest(
        uint32_t sampleCount = 4u,
        uint32_t xParity = 0u,
        uint32_t yParity = 0u)
    {
        TextureQualificationValidationRequest request;
        request.requestedSampleCount = sampleCount;
        request.resolvedSampleCount = sampleCount;
        request.viewportOriginXParity = xParity;
        request.viewportOriginYParity = yParity;
        return request;
    }

    [[nodiscard]] TextureQualificationReason Validate(
        const TextureQualificationCertificate& certificate,
        const TextureQualificationLiveIdentity& expected,
        const TextureQualificationValidationRequest& request = MakeRequest())
    {
        return ValidateTextureQualificationCertificate(
            &certificate,
            expected,
            request).reason;
    }

    void CheckCertificateMutation(
        const std::string& label,
        TextureQualificationReason expectedReason,
        const std::function<void(TextureQualificationCertificate&)>& mutate)
    {
        TextureQualificationCertificate certificate = MakeCertificate();
        mutate(certificate);
        std::array<uint8_t, TextureQualificationCertificateByteCount> bytes{};
        const TextureQualificationStatus serialized =
            SerializeTextureQualificationCertificate(
                certificate,
                bytes.data(),
                bytes.size());
        TextureQualificationCertificate parsed;
        const TextureQualificationStatus parsedStatus =
            ParseTextureQualificationCertificate(
                bytes.data(),
                bytes.size(),
                parsed);
        Check(serialized.Succeeded() && parsedStatus.Succeeded(),
            label + " must retain a valid digest while mutating its field");
        const TextureQualificationReason reason = Validate(
            parsed,
            MakeLiveIdentity());
        Check(reason == expectedReason,
            label + " produced " + GetTextureQualificationReasonName(reason) +
                ", expected " +
                GetTextureQualificationReasonName(expectedReason));
    }

    void CheckExpectedMutation(
        const std::string& label,
        TextureQualificationReason expectedReason,
        const std::function<void(TextureQualificationLiveIdentity&)>& mutate)
    {
        TextureQualificationLiveIdentity expected = MakeLiveIdentity();
        mutate(expected);
        const TextureQualificationReason reason = Validate(
            MakeCertificate(),
            expected);
        Check(reason == expectedReason,
            label + " produced " + GetTextureQualificationReasonName(reason) +
                ", expected " +
                GetTextureQualificationReasonName(expectedReason));
    }

    [[nodiscard]] std::array<
        uint8_t,
        TextureQualificationCertificateByteCount> Serialize(
            const TextureQualificationCertificate& certificate)
    {
        std::array<uint8_t, TextureQualificationCertificateByteCount> bytes{};
        const TextureQualificationStatus status =
            SerializeTextureQualificationCertificate(
                certificate,
                bytes.data(),
                bytes.size());
        Check(status.Succeeded() && status.byteCount == bytes.size(),
            "valid certificate serialization failed");
        return bytes;
    }

    void TestSha256ByteApi()
    {
        static constexpr std::array<uint8_t, 3u> Abc = {
            uint8_t('a'), uint8_t('b'), uint8_t('c')
        };
        static constexpr TextureQualificationSha256 ExpectedAbc = {
            0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
            0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
            0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
            0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu
        };
        static constexpr TextureQualificationSha256 ExpectedEmpty = {
            0xe3u, 0xb0u, 0xc4u, 0x42u, 0x98u, 0xfcu, 0x1cu, 0x14u,
            0x9au, 0xfbu, 0xf4u, 0xc8u, 0x99u, 0x6fu, 0xb9u, 0x24u,
            0x27u, 0xaeu, 0x41u, 0xe4u, 0x64u, 0x9bu, 0x93u, 0x4cu,
            0xa4u, 0x95u, 0x99u, 0x1bu, 0x78u, 0x52u, 0xb8u, 0x55u
        };

        TextureQualificationSha256 digest;
        Check(ComputeTextureQualificationSha256(
                Abc.data(), Abc.size(), digest) ==
                    TextureQualificationReason::None &&
                digest == ExpectedAbc,
            "BCrypt SHA-256 must match the abc known vector");
        Check(ComputeTextureQualificationSha256(nullptr, 0u, digest) ==
                    TextureQualificationReason::None &&
                digest == ExpectedEmpty,
            "BCrypt SHA-256 must accept and correctly hash an empty byte span");
        digest.fill(0xffu);
        Check(ComputeTextureQualificationSha256(nullptr, 1u, digest) ==
                    TextureQualificationReason::HashInputMissing &&
                digest == TextureQualificationSha256{},
            "a missing nonempty hash input fails closed and clears output");
    }

    void TestFixedWireRoundTrip()
    {
        static_assert(TextureQualificationWireHeaderReservedOffset == 20u);
        static_assert(TextureQualificationWireReservedOffset == 516u);
        static_assert(TextureQualificationWireDigestOffset == 532u);
        static_assert(TextureQualificationCertificateByteCount == 564u);

        const TextureQualificationCertificate original = MakeCertificate();
        const auto bytes = Serialize(original);
        Check(bytes[8u] == 1u && bytes[9u] == 0u &&
                bytes[10u] == 0u && bytes[11u] == 0u &&
                bytes[12u] == 0x04u && bytes[13u] == 0x03u &&
                bytes[14u] == 0x02u && bytes[15u] == 0x01u &&
                bytes[16u] == 0x34u && bytes[17u] == 0x02u &&
                bytes[18u] == 0u && bytes[19u] == 0u,
            "wire header integers must use the frozen little-endian encoding");
        Check(bytes[24u] == 0x44u && bytes[25u] == 0x33u &&
                bytes[26u] == 0x22u && bytes[27u] == 0x11u,
            "adapter integers must use manual little-endian encoding");
        Check(bytes[48u] == 0x2fu && bytes[49u] == 0x00u &&
                bytes[50u] == 0x10u && bytes[51u] == 0x00u &&
                bytes[52u] == 0x00u && bytes[53u] == 0x00u &&
                bytes[54u] == 0x20u && bytes[55u] == 0x00u,
            "64-bit driver identity must use manual little-endian encoding");

        TextureQualificationCertificate parsed;
        const TextureQualificationStatus parsedStatus =
            ParseTextureQualificationCertificate(
                bytes.data(),
                bytes.size(),
                parsed);
        Check(parsedStatus.Succeeded() &&
                parsedStatus.byteCount == bytes.size(),
            "fixed certificate bytes must parse successfully");
        Check(Validate(parsed, MakeLiveIdentity()) ==
                TextureQualificationReason::None,
            "round-tripped certificate must retain its complete identity");
        Check(Serialize(parsed) == bytes,
            "parse and reserialize must be byte deterministic");

        std::array<uint8_t, TextureQualificationCertificateByteCount + 19u>
            oversized;
        oversized.fill(0xa5u);
        const TextureQualificationStatus oversizedStatus =
            SerializeTextureQualificationCertificate(
                original,
                oversized.data(),
                oversized.size());
        Check(oversizedStatus.Succeeded() &&
                oversizedStatus.byteCount ==
                    TextureQualificationCertificateByteCount,
            "a larger destination still reports exactly the fixed wire size");
        for (size_t byte = TextureQualificationCertificateByteCount;
            byte < oversized.size();
            ++byte)
        {
            Check(oversized[byte] == 0xa5u,
                "serialization must not write beyond the fixed certificate");
        }
    }

    void TestParserRejectsMalformedBytes()
    {
        const auto valid = Serialize(MakeCertificate());
        TextureQualificationCertificate parsed = MakeCertificate();

        Check(ParseTextureQualificationCertificate(
                nullptr, 0u, parsed).reason ==
                    TextureQualificationReason::CertificateMissing &&
                parsed.identity.adapter.vendorId == 0u,
            "missing input fails transactionally");
        for (size_t size = 0u; size < valid.size(); ++size)
        {
            const TextureQualificationStatus status =
                ParseTextureQualificationCertificate(
                    valid.data(), size, parsed);
            Check(status.reason ==
                    TextureQualificationReason::CertificateTruncated,
                "every proper prefix must be rejected as truncated");
        }

        std::vector<uint8_t> extra(valid.begin(), valid.end());
        extra.push_back(0u);
        Check(ParseTextureQualificationCertificate(
                extra.data(), extra.size(), parsed).reason ==
                    TextureQualificationReason::CertificateHasTrailingBytes,
            "one trailing byte must be rejected");

        const auto CheckByteMutation = [&valid, &parsed](
            size_t offset,
            TextureQualificationReason expected,
            const std::string& label)
        {
            auto mutated = valid;
            mutated[offset] ^= 0x80u;
            const TextureQualificationReason actual =
                ParseTextureQualificationCertificate(
                    mutated.data(), mutated.size(), parsed).reason;
            Check(actual == expected &&
                    parsed.identity.adapter.vendorId == 0u &&
                    parsed.samples[0u].sampleCount == 0u,
                label + " produced " +
                    GetTextureQualificationReasonName(actual) +
                    ", expected " +
                    GetTextureQualificationReasonName(expected));
        };
        CheckByteMutation(0u, TextureQualificationReason::MagicMismatch,
            "magic mutation");
        CheckByteMutation(8u, TextureQualificationReason::SchemaMismatch,
            "schema mutation");
        CheckByteMutation(12u, TextureQualificationReason::EndianMismatch,
            "endian mutation");
        CheckByteMutation(16u, TextureQualificationReason::DeclaredSizeMismatch,
            "declared-size mutation");
        CheckByteMutation(
            TextureQualificationWireHeaderReservedOffset,
            TextureQualificationReason::ReservedBytesNotZero,
            "header-reserved mutation");
        CheckByteMutation(
            TextureQualificationWireReservedOffset,
            TextureQualificationReason::ReservedBytesNotZero,
            "body-reserved mutation");
        CheckByteMutation(24u,
            TextureQualificationReason::IntegrityDigestMismatch,
            "identity-body mutation");
        CheckByteMutation(
            TextureQualificationWireDigestOffset,
            TextureQualificationReason::IntegrityDigestMismatch,
            "digest mutation");

        std::array<uint8_t, TextureQualificationCertificateByteCount - 1u>
            tooSmall;
        tooSmall.fill(0xa5u);
        Check(SerializeTextureQualificationCertificate(
                MakeCertificate(), nullptr,
                TextureQualificationCertificateByteCount).reason ==
                    TextureQualificationReason::SerializationOutputMissing,
            "missing serialization output must fail closed");
        Check(SerializeTextureQualificationCertificate(
                MakeCertificate(), tooSmall.data(), tooSmall.size()).reason ==
                    TextureQualificationReason::SerializationOutputTooSmall,
            "undersized serialization output must fail closed");
        for (uint8_t byte : tooSmall)
        {
            Check(byte == 0xa5u,
                "undersized serialization must not partially modify output");
        }
    }

    void TestExactIdentityMutations()
    {
        CheckCertificateMutation("LUID low",
            TextureQualificationReason::AdapterLuidMismatch,
            [](auto& c) { c.identity.adapter.luidLowPart ^= 1u; });
        CheckCertificateMutation("LUID high",
            TextureQualificationReason::AdapterLuidMismatch,
            [](auto& c) { c.identity.adapter.luidHighPartBits ^= 1u; });
        CheckCertificateMutation("vendor",
            TextureQualificationReason::AdapterHardwareMismatch,
            [](auto& c) { c.identity.adapter.vendorId ^= 1u; });
        CheckCertificateMutation("device",
            TextureQualificationReason::AdapterHardwareMismatch,
            [](auto& c) { c.identity.adapter.deviceId ^= 1u; });
        CheckCertificateMutation("subsystem",
            TextureQualificationReason::AdapterHardwareMismatch,
            [](auto& c) { c.identity.adapter.subsystemId ^= 1u; });
        CheckCertificateMutation("revision",
            TextureQualificationReason::AdapterHardwareMismatch,
            [](auto& c) { c.identity.adapter.revision ^= 1u; });
        CheckCertificateMutation("driver",
            TextureQualificationReason::AdapterDriverMismatch,
            [](auto& c) { c.identity.adapter.driverVersion ^= 1u; });

        CheckCertificateMutation("DXGI runtime hash",
            TextureQualificationReason::DxgiRuntimeModuleMismatch,
            [](auto& c) { c.identity.runtime.dxgiModuleSha256[0u] ^= 1u; });
        CheckCertificateMutation("D3D12 runtime hash",
            TextureQualificationReason::D3d12RuntimeModuleMismatch,
            [](auto& c) { c.identity.runtime.d3d12ModuleSha256[0u] ^= 1u; });
        CheckCertificateMutation("D3D12Core runtime hash",
            TextureQualificationReason::D3d12CoreRuntimeModuleMismatch,
            [](auto& c) {
                c.identity.runtime.d3d12CoreModuleSha256[0u] ^= 1u;
            });
        CheckCertificateMutation("DXIL runtime hash",
            TextureQualificationReason::DxilRuntimeModuleMismatch,
            [](auto& c) { c.identity.runtime.dxilModuleSha256[0u] ^= 1u; });

        CheckCertificateMutation("renderer executable hash",
            TextureQualificationReason::RendererExecutableMismatch,
            [](auto& c) {
                c.identity.artifacts.rendererExecutableSha256[0u] ^= 1u;
            });
        CheckCertificateMutation("renderer build manifest hash",
            TextureQualificationReason::RendererBuildManifestMismatch,
            [](auto& c) {
                c.identity.artifacts.rendererBuildManifestSha256[0u] ^= 1u;
            });
        CheckCertificateMutation("qualification test executable hash",
            TextureQualificationReason::QualificationTestExecutableMismatch,
            [](auto& c) {
                c.identity.artifacts.qualificationTestExecutableSha256[0u] ^=
                    1u;
            });
        CheckCertificateMutation("qualification test DXIL-set hash",
            TextureQualificationReason::QualificationTestDxilSetMismatch,
            [](auto& c) {
                c.identity.artifacts.qualificationTestDxilSetSha256[0u] ^= 1u;
            });
        CheckCertificateMutation("feature-format hash",
            TextureQualificationReason::FeatureFormatSetMismatch,
            [](auto& c) { c.identity.featureFormatSetSha256[0u] ^= 1u; });

        const auto CheckSamplerMutation = [](const std::string& label,
            const std::function<void(TextureQualificationSamplerIdentity&)>&
                mutate)
        {
            CheckCertificateMutation(label,
                TextureQualificationReason::SamplerIdentityMismatch,
                [&mutate](auto& c) { mutate(c.identity.sampler); });
        };
        for (size_t channel = 0u; channel < 4u; ++channel)
        {
            CheckSamplerMutation(
                "sampler border channel " + std::to_string(channel),
                [channel](auto& sampler) {
                    sampler.borderColorBits[channel] = 0x3f000000u;
                });
        }
        CheckSamplerMutation("sampler max anisotropy",
            [](auto& s) { s.maxAnisotropyBits = 0x41000000u; });
        CheckSamplerMutation("sampler mip bias",
            [](auto& s) { s.mipBiasBits = 0x3f000000u; });
        CheckSamplerMutation("sampler min filter",
            [](auto& s) { s.minFilter = 0u; });
        CheckSamplerMutation("sampler mag filter",
            [](auto& s) { s.magFilter = 0u; });
        CheckSamplerMutation("sampler mip filter",
            [](auto& s) { s.mipFilter = 0u; });
        CheckSamplerMutation("sampler address U",
            [](auto& s) { s.addressU = 2u; });
        CheckSamplerMutation("sampler address V",
            [](auto& s) { s.addressV = 2u; });
        CheckSamplerMutation("sampler address W",
            [](auto& s) { s.addressW = 2u; });
        CheckSamplerMutation("sampler reduction",
            [](auto& s) { s.reductionType = 1u; });
        CheckCertificateMutation("sampler schema",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler.schema = 2u; });

        CheckCertificateMutation("semantic-format schema hash",
            TextureQualificationReason::SemanticFormatSchemaMismatch,
            [](auto& c) {
                c.identity.semanticFormatSchemaSha256[0u] ^= 1u;
            });
        CheckCertificateMutation("matrix schema",
            TextureQualificationReason::MatrixSchemaMismatch,
            [](auto& c) { ++c.identity.matrix.schema; });
        CheckCertificateMutation("matrix schema hash",
            TextureQualificationReason::MatrixSchemaMismatch,
            [](auto& c) { c.identity.matrix.schemaSha256[0u] ^= 1u; });
        for (size_t count = 0u; count < 3u; ++count)
        {
            CheckCertificateMutation(
                "matrix required mask " + std::to_string(count),
                TextureQualificationReason::MatrixRequiredMaskMismatch,
                [count](auto& c) {
                    c.identity.matrix.requiredQualificationMasks[count] ^= 1u;
                });
        }
    }

    void TestMissingAndMalformedIdentities()
    {
        Check(ValidateTextureQualificationCertificate(
                nullptr, MakeLiveIdentity(), MakeRequest()).reason ==
                    TextureQualificationReason::CertificateMissing,
            "a missing certificate pointer must fail closed");
        const TextureQualificationCertificate defaultCertificate;
        Check(Validate(defaultCertificate, MakeLiveIdentity()) ==
                TextureQualificationReason::CertificateAdapterIdentityMissing,
            "a default-constructed certificate must remain closed");
        Check(Validate(MakeCertificate(), TextureQualificationLiveIdentity{}) ==
                TextureQualificationReason::ExpectedAdapterIdentityMissing,
            "a default-constructed expected live identity must remain closed");

        CheckCertificateMutation("missing certificate adapter",
            TextureQualificationReason::CertificateAdapterIdentityMissing,
            [](auto& c) { c.identity.adapter = {}; });
        CheckCertificateMutation("missing certificate runtime",
            TextureQualificationReason::CertificateRuntimeIdentityMissing,
            [](auto& c) { c.identity.runtime = {}; });
        CheckCertificateMutation("missing certificate artifacts",
            TextureQualificationReason::CertificateArtifactIdentityMissing,
            [](auto& c) { c.identity.artifacts = {}; });
        CheckCertificateMutation("missing certificate feature formats",
            TextureQualificationReason::CertificateFeatureFormatIdentityMissing,
            [](auto& c) { c.identity.featureFormatSetSha256 = {}; });
        CheckCertificateMutation("missing certificate sampler",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler = {}; });
        CheckCertificateMutation("missing certificate semantic formats",
            TextureQualificationReason::CertificateSemanticFormatSchemaMissing,
            [](auto& c) { c.identity.semanticFormatSchemaSha256 = {}; });
        CheckCertificateMutation("missing certificate matrix",
            TextureQualificationReason::CertificateMatrixSchemaMissing,
            [](auto& c) { c.identity.matrix = {}; });

        const auto CheckZeroRuntimeDigest = [](const std::string& label,
            const std::function<void(
                TextureQualificationD3dRuntimeIdentity&)>& clear)
        {
            CheckCertificateMutation(label,
                TextureQualificationReason::CertificateRuntimeIdentityMissing,
                [&clear](auto& c) { clear(c.identity.runtime); });
        };
        CheckZeroRuntimeDigest("missing DXGI runtime digest",
            [](auto& i) { i.dxgiModuleSha256 = {}; });
        CheckZeroRuntimeDigest("missing D3D12 runtime digest",
            [](auto& i) { i.d3d12ModuleSha256 = {}; });
        CheckZeroRuntimeDigest("missing D3D12Core runtime digest",
            [](auto& i) { i.d3d12CoreModuleSha256 = {}; });
        CheckZeroRuntimeDigest("missing DXIL runtime digest",
            [](auto& i) { i.dxilModuleSha256 = {}; });

        const auto CheckZeroArtifactDigest = [](const std::string& label,
            const std::function<void(
                TextureQualificationArtifactIdentity&)>& clear)
        {
            CheckCertificateMutation(label,
                TextureQualificationReason::CertificateArtifactIdentityMissing,
                [&clear](auto& c) { clear(c.identity.artifacts); });
        };
        CheckZeroArtifactDigest("missing renderer executable digest",
            [](auto& i) { i.rendererExecutableSha256 = {}; });
        CheckZeroArtifactDigest("missing renderer manifest digest",
            [](auto& i) { i.rendererBuildManifestSha256 = {}; });
        CheckZeroArtifactDigest("missing qualification executable digest",
            [](auto& i) { i.qualificationTestExecutableSha256 = {}; });
        CheckZeroArtifactDigest("missing qualification DXIL-set digest",
            [](auto& i) { i.qualificationTestDxilSetSha256 = {}; });
        CheckCertificateMutation("missing matrix schema digest",
            TextureQualificationReason::CertificateMatrixSchemaMissing,
            [](auto& c) { c.identity.matrix.schemaSha256 = {}; });

        CheckExpectedMutation("missing expected adapter",
            TextureQualificationReason::ExpectedAdapterIdentityMissing,
            [](auto& i) { i.adapter = {}; });
        CheckExpectedMutation("missing expected runtime",
            TextureQualificationReason::ExpectedRuntimeIdentityMissing,
            [](auto& i) { i.runtime = {}; });
        CheckExpectedMutation("missing expected artifacts",
            TextureQualificationReason::ExpectedArtifactIdentityMissing,
            [](auto& i) { i.artifacts = {}; });
        CheckExpectedMutation("missing expected feature formats",
            TextureQualificationReason::ExpectedFeatureFormatIdentityMissing,
            [](auto& i) { i.featureFormatSetSha256 = {}; });
        CheckExpectedMutation("missing expected sampler",
            TextureQualificationReason::ExpectedSamplerIdentityMissing,
            [](auto& i) { i.sampler = {}; });
        CheckExpectedMutation("missing expected semantic formats",
            TextureQualificationReason::ExpectedSemanticFormatSchemaMissing,
            [](auto& i) { i.semanticFormatSchemaSha256 = {}; });
        CheckExpectedMutation("missing expected matrix",
            TextureQualificationReason::ExpectedMatrixSchemaMissing,
            [](auto& i) { i.matrix = {}; });

        CheckCertificateMutation("nonfinite sampler border",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) {
                c.identity.sampler.borderColorBits[0u] = 0x7fc00000u;
            });
        CheckCertificateMutation("invalid sampler address",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler.addressU = 5u; });
        CheckCertificateMutation("NaN sampler anisotropy",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) {
                c.identity.sampler.maxAnisotropyBits = 0x7fc00000u;
            });
        CheckCertificateMutation("sub-one sampler anisotropy",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) {
                c.identity.sampler.maxAnisotropyBits = 0x3f000000u;
            });
        CheckCertificateMutation("over-sixteen sampler anisotropy",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) {
                c.identity.sampler.maxAnisotropyBits = 0x41880000u;
            });
        CheckCertificateMutation("infinite sampler mip bias",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler.mipBiasBits = 0x7f800000u; });
        CheckCertificateMutation("invalid sampler min filter",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler.minFilter = 2u; });
        CheckCertificateMutation("invalid sampler mag filter",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler.magFilter = 2u; });
        CheckCertificateMutation("invalid sampler mip filter",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler.mipFilter = 2u; });
        CheckCertificateMutation("invalid sampler address V",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler.addressV = 5u; });
        CheckCertificateMutation("invalid sampler address W",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler.addressW = 5u; });
        CheckCertificateMutation("invalid sampler reduction",
            TextureQualificationReason::CertificateSamplerIdentityMissing,
            [](auto& c) { c.identity.sampler.reductionType = 4u; });
        CheckCertificateMutation("zero matrix required mask",
            TextureQualificationReason::CertificateMatrixSchemaMissing,
            [](auto& c) {
                c.identity.matrix.requiredQualificationMasks[1u] = 0u;
            });
    }

    void TestQualificationRecordsAndSampleRequests()
    {
        const TextureQualificationCertificate valid = MakeCertificate();
        const TextureQualificationLiveIdentity identity = MakeLiveIdentity();
        for (size_t countIndex = 0u;
            countIndex < TextureQualificationSampleCounts.size();
            ++countIndex)
        {
            for (uint32_t yParity = 0u; yParity < 2u; ++yParity)
            {
                for (uint32_t xParity = 0u; xParity < 2u; ++xParity)
                {
                    const TextureQualificationValidationResult result =
                        ValidateTextureQualificationCertificate(
                            &valid,
                            identity,
                            MakeRequest(
                                TextureQualificationSampleCounts[countIndex],
                                xParity,
                                yParity));
                    const uint32_t expectedParity =
                        xParity | (yParity << 1u);
                    Check(result.Succeeded() &&
                            result.sampleCount ==
                                TextureQualificationSampleCounts[countIndex] &&
                            result.qualificationMask ==
                                identity.matrix.requiredQualificationMasks[
                                    countIndex] &&
                            result.viewportOriginParityIndex == expectedParity,
                        "2x/4x/8x must accept each of four qualified real "
                        "viewport-origin parities");
                }
            }
        }

        for (size_t count = 0u; count < 3u; ++count)
        {
            CheckCertificateMutation(
                "record order " + std::to_string(count),
                TextureQualificationReason::QualificationRecordOrderMismatch,
                [count](auto& c) { ++c.samples[count].sampleCount; });
            CheckCertificateMutation(
                "missing record qualification " + std::to_string(count),
                TextureQualificationReason::QualificationMaskMissing,
                [count](auto& c) { c.samples[count].qualificationMask = 0u; });
            CheckCertificateMutation(
                "mismatched record qualification " + std::to_string(count),
                TextureQualificationReason::QualificationMaskMismatch,
                [count](auto& c) { c.samples[count].qualificationMask ^= 1u; });
            CheckCertificateMutation(
                "incomplete parity record " + std::to_string(count),
                TextureQualificationReason::
                    IncompleteViewportParityQualification,
                [count](auto& c) {
                    c.samples[count].viewportOriginParityMask = 0x07u;
                });
            CheckCertificateMutation(
                "malformed parity record " + std::to_string(count),
                TextureQualificationReason::QualificationParityMaskMalformed,
                [count](auto& c) {
                    c.samples[count].viewportOriginParityMask = 0x1fu;
                });
        }

        TextureQualificationValidationRequest request = MakeRequest(8u);
        request.requestedSampleCount = 16u;
        Check(Validate(valid, identity, request) ==
                TextureQualificationReason::Requested16Resolved8Unsupported,
            "requested 16x resolved to 8x must never inherit 8x qualification");
        request = MakeRequest(16u);
        Check(Validate(valid, identity, request) ==
                TextureQualificationReason::Requested16Unsupported,
            "native 16x remains unsupported by this certificate schema");
        request = MakeRequest(8u);
        request.resolvedSampleCount = 16u;
        Check(Validate(valid, identity, request) ==
                TextureQualificationReason::Resolved16Unsupported,
            "a 16x resolved resource remains unsupported");
        request = MakeRequest(1u);
        Check(Validate(valid, identity, request) ==
                TextureQualificationReason::UnsupportedRequestedSampleCount,
            "1x cannot use a compact texture qualification certificate");
        request = MakeRequest(2u);
        request.resolvedSampleCount = 1u;
        Check(Validate(valid, identity, request) ==
                TextureQualificationReason::UnsupportedResolvedSampleCount,
            "unsupported resolved counts fail before any mask lookup");
        request = MakeRequest(2u);
        request.resolvedSampleCount = 4u;
        Check(Validate(valid, identity, request) ==
                TextureQualificationReason::
                    RequestedResolvedSampleCountMismatch,
            "a supported but demoted sample count cannot borrow another record");
        request = MakeRequest(4u);
        request.viewportOriginXParity = 2u;
        Check(Validate(valid, identity, request) ==
                TextureQualificationReason::InvalidViewportOriginParity,
            "viewport X parity must be an actual low bit");
        request = MakeRequest(4u);
        request.viewportOriginYParity = 2u;
        Check(Validate(valid, identity, request) ==
                TextureQualificationReason::InvalidViewportOriginParity,
            "viewport Y parity must be an actual low bit");
    }

    void TestReasonNames()
    {
        for (uint32_t value = 0u;
            value < static_cast<uint32_t>(TextureQualificationReason::Count);
            ++value)
        {
            const char* name = GetTextureQualificationReasonName(
                static_cast<TextureQualificationReason>(value));
            Check(name != nullptr && std::string(name) != "Unknown",
                "every detailed CPU result reason must have a stable name");
        }
        Check(std::string(GetTextureQualificationReasonName(
                static_cast<TextureQualificationReason>(0xffffffffu))) ==
                    "Unknown",
            "out-of-schema result reasons must remain visibly unknown");
    }
}

int main()
{
    TestSha256ByteApi();
    TestFixedWireRoundTrip();
    TestParserRejectsMalformedBytes();
    TestExactIdentityMutations();
    TestMissingAndMalformedIdentities();
    TestQualificationRecordsAndSampleRequests();
    TestReasonNames();

    if (g_Passed)
    {
        std::cout <<
            "UVSR adaptive MSAA texture qualification validation passed\n";
    }
    return g_Passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
