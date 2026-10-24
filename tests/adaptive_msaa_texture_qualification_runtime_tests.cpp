#include "adaptive_msaa_texture_qualification_runtime.h"

#include <nvrhi/nvrhi.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace uvsr::adaptive_msaa;

    [[noreturn]] void FailCheck(
        const char* expression,
        int line)
    {
        std::cerr <<
            "Adaptive MSAA texture qualification runtime check failed at " <<
            line << ": " << expression << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const char* expression, int line)
    {
        if (!condition)
            FailCheck(expression, line);
    }

    // Keep the focused checks active in Release, where the CRT assert macro is
    // compiled out. Retaining the familiar call sites also keeps compound
    // checks readable without hiding their source expression from a failure.
#undef assert
#define assert(expression) \
    Require(static_cast<bool>(expression), #expression, __LINE__)

    class TemporaryDirectory final
    {
    public:
        TemporaryDirectory()
        {
            m_Path = std::filesystem::temp_directory_path() /
                (L"uvsr-texture-qualification-runtime-" +
                    std::to_wstring(GetCurrentProcessId()) + L"-" +
                    std::to_wstring(GetTickCount64()));
            std::filesystem::create_directories(m_Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_Path, error);
        }

        const std::filesystem::path& Path() const
        {
            return m_Path;
        }

    private:
        std::filesystem::path m_Path;
    };

    void WriteBytes(
        const std::filesystem::path& path,
        const std::vector<uint8_t>& bytes,
        bool append = false)
    {
        std::ofstream stream(
            path,
            std::ios::binary |
                (append ? std::ios::app : std::ios::trunc));
        assert(stream);
        if (!bytes.empty())
        {
            stream.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        }
        assert(stream.good());
    }

    void FillDigest(TextureQualificationSha256& digest, uint8_t seed)
    {
        for (size_t index = 0u; index < digest.size(); ++index)
            digest[index] = uint8_t(seed + index * 13u + 1u);
    }

    uint32_t FloatBits(float value)
    {
        uint32_t bits = 0u;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    TextureQualificationLiveIdentity MakeIdentity()
    {
        TextureQualificationLiveIdentity identity;
        identity.adapter.luidLowPart = 1u;
        identity.adapter.luidHighPartBits = 2u;
        identity.adapter.vendorId = 0x10deu;
        identity.adapter.deviceId = 0x1234u;
        identity.adapter.subsystemId = 0x5678u;
        identity.adapter.revision = 3u;
        identity.adapter.driverVersion = 0x0001000200030004ull;
        FillDigest(identity.runtime.dxgiModuleSha256, 1u);
        FillDigest(identity.runtime.d3d12ModuleSha256, 2u);
        FillDigest(identity.runtime.d3d12CoreModuleSha256, 3u);
        FillDigest(identity.runtime.dxilModuleSha256, 4u);
        FillDigest(identity.artifacts.rendererExecutableSha256, 5u);
        FillDigest(identity.artifacts.rendererBuildManifestSha256, 6u);
        FillDigest(
            identity.artifacts.qualificationTestExecutableSha256, 7u);
        FillDigest(identity.artifacts.qualificationTestDxilSetSha256, 8u);
        FillDigest(identity.featureFormatSetSha256, 9u);
        identity.sampler.schema = TextureQualificationSamplerSchema;
        identity.sampler.borderColorBits.fill(FloatBits(1.f));
        identity.sampler.maxAnisotropyBits = FloatBits(16.f);
        identity.sampler.mipBiasBits = FloatBits(0.f);
        identity.sampler.minFilter = 1u;
        identity.sampler.magFilter = 1u;
        identity.sampler.mipFilter = 1u;
        identity.sampler.addressU =
            static_cast<uint32_t>(nvrhi::SamplerAddressMode::Wrap);
        identity.sampler.addressV = identity.sampler.addressU;
        identity.sampler.addressW = identity.sampler.addressU;
        identity.sampler.reductionType =
            static_cast<uint32_t>(nvrhi::SamplerReductionType::Standard);
        const TextureQualificationRuntimeStatus schemas =
            GetTextureQualificationCompiledSchemaIdentity(
                identity.semanticFormatSchemaSha256,
                identity.matrix);
        assert(schemas.Succeeded());
        return identity;
    }

    TextureQualificationCertificate MakeCertificate(
        const TextureQualificationLiveIdentity& identity)
    {
        TextureQualificationCertificate certificate;
        certificate.identity = identity;
        for (size_t index = 0u; index < certificate.samples.size(); ++index)
        {
            certificate.samples[index].sampleCount =
                TextureQualificationSampleCounts[index];
            certificate.samples[index].qualificationMask =
                identity.matrix.requiredQualificationMasks[index];
            certificate.samples[index].viewportOriginParityMask =
                TextureQualificationViewportParityMask;
        }
        return certificate;
    }

    std::array<uint8_t, TextureQualificationCertificateByteCount> Serialize(
        const TextureQualificationCertificate& certificate)
    {
        std::array<uint8_t, TextureQualificationCertificateByteCount> bytes{};
        const TextureQualificationStatus status =
            SerializeTextureQualificationCertificate(
                certificate, bytes.data(), bytes.size());
        assert(status.Succeeded());
        return bytes;
    }

    TextureQualificationValidationRequest Request(
        uint32_t sampleCount = 2u,
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

    bool DigestsDiffer(
        const TextureQualificationSha256& first,
        const TextureQualificationSha256& second)
    {
        return !std::equal(first.begin(), first.end(), second.begin());
    }

    void AppendU32(std::vector<uint8_t>& bytes, uint32_t value)
    {
        bytes.push_back(uint8_t(value));
        bytes.push_back(uint8_t(value >> 8u));
        bytes.push_back(uint8_t(value >> 16u));
        bytes.push_back(uint8_t(value >> 24u));
    }

    void AppendU64(std::vector<uint8_t>& bytes, uint64_t value)
    {
        for (uint32_t shift = 0u; shift < 64u; shift += 8u)
            bytes.push_back(uint8_t(value >> shift));
    }

    void TestFileAndAggregateHashing(const TemporaryDirectory& temporary)
    {
        const std::filesystem::path firstPath = temporary.Path() / L"a.bin";
        const std::filesystem::path secondPath = temporary.Path() / L"b.bin";
        WriteBytes(firstPath, { 'a', 'b', 'c' });
        WriteBytes(secondPath, { 0u, 1u, 2u, 3u });

        TextureQualificationSha256 fileDigest;
        TextureQualificationRuntimeStatus status =
            ComputeTextureQualificationFileSha256(
                firstPath.c_str(), fileDigest);
        assert(status.Succeeded() && status.byteCount == 3u);
        constexpr std::array<uint8_t, 32u> AbcSha256 = {
            0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
            0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
            0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
            0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu
        };
        assert(fileDigest == AbcSha256);

        const std::array<TextureQualificationLabeledFile, 2u> files = {{
            { "production/a.bin", firstPath.c_str() },
            { "test/b.bin", secondPath.c_str() }
        }};
        TextureQualificationSha256 aggregate;
        TextureQualificationSha256 repeated;
        status = ComputeTextureQualificationLabeledFileSetSha256(
            files.data(), files.size(), aggregate);
        assert(status.Succeeded() && status.byteCount == 7u);

        constexpr char AggregateDomain[] =
            "UVSR-TEXTURE-QUALIFICATION-LABELED-FILE-SET-V1";
        std::vector<uint8_t> canonical(
            AggregateDomain,
            AggregateDomain + sizeof(AggregateDomain) - 1u);
        AppendU32(canonical, TextureQualificationRuntimeSchema);
        AppendU32(canonical, 2u);
        constexpr char FirstLabel[] = "production/a.bin";
        AppendU32(canonical, sizeof(FirstLabel) - 1u);
        canonical.insert(
            canonical.end(), FirstLabel, FirstLabel + sizeof(FirstLabel) - 1u);
        AppendU64(canonical, 3u);
        canonical.insert(canonical.end(), { 'a', 'b', 'c' });
        constexpr char SecondLabel[] = "test/b.bin";
        AppendU32(canonical, sizeof(SecondLabel) - 1u);
        canonical.insert(
            canonical.end(),
            SecondLabel,
            SecondLabel + sizeof(SecondLabel) - 1u);
        AppendU64(canonical, 4u);
        canonical.insert(canonical.end(), { 0u, 1u, 2u, 3u });
        TextureQualificationSha256 independentlyFramed;
        assert(ComputeTextureQualificationSha256(
            canonical.data(), canonical.size(), independentlyFramed) ==
            TextureQualificationReason::None);
        assert(aggregate == independentlyFramed);
        status = ComputeTextureQualificationLabeledFileSetSha256(
            files.data(), files.size(), repeated);
        assert(status.Succeeded() && aggregate == repeated);

        const std::array<TextureQualificationLabeledFile, 2u> reordered = {{
            files[1], files[0]
        }};
        status = ComputeTextureQualificationLabeledFileSetSha256(
            reordered.data(), reordered.size(), repeated);
        assert(status.reason ==
            TextureQualificationRuntimeReason::AggregateLabelOrderInvalid);

        const std::array<TextureQualificationLabeledFile, 2u> relabeled = {{
            { "production/a-renamed.bin", firstPath.c_str() }, files[1]
        }};
        status = ComputeTextureQualificationLabeledFileSetSha256(
            relabeled.data(), relabeled.size(), repeated);
        assert(status.Succeeded() && DigestsDiffer(aggregate, repeated));

        WriteBytes(secondPath, { 0u, 1u, 2u, 4u });
        status = ComputeTextureQualificationLabeledFileSetSha256(
            files.data(), files.size(), repeated);
        assert(status.Succeeded() && DigestsDiffer(aggregate, repeated));

        const TextureQualificationLabeledFile invalidLabel = {
            "../escape.bin", firstPath.c_str()
        };
        status = ComputeTextureQualificationLabeledFileSetSha256(
            &invalidLabel, 1u, repeated);
        assert(status.reason ==
            TextureQualificationRuntimeReason::AggregateLabelInvalid);

        status = ComputeTextureQualificationFileSha256(
            temporary.Path().c_str(), repeated);
        assert(!status.Succeeded());

        const std::filesystem::path emptyPath = temporary.Path() / L"empty.bin";
        WriteBytes(emptyPath, {});
        const TextureQualificationLabeledFile empty = {
            "production/empty.bin", emptyPath.c_str()
        };
        status = ComputeTextureQualificationLabeledFileSetSha256(
            &empty, 1u, repeated);
        assert(status.reason ==
            TextureQualificationRuntimeReason::AggregateFileEmpty);
    }

    void TestRuntimeFileIdentity(const TemporaryDirectory& temporary)
    {
        const std::array<const wchar_t*, 4u> names = {
            L"dxgi.fake", L"d3d12.fake", L"d3d12core.fake", L"dxil.fake"
        };
        std::array<std::filesystem::path, 4u> paths;
        for (size_t index = 0u; index < paths.size(); ++index)
        {
            paths[index] = temporary.Path() / names[index];
            WriteBytes(paths[index], { uint8_t(index + 1u) });
        }
        const TextureQualificationD3dRuntimeFileInput input = {
            paths[0].c_str(), paths[1].c_str(),
            paths[2].c_str(), paths[3].c_str()
        };
        TextureQualificationD3dRuntimeIdentity identity;
        TextureQualificationRuntimeStatus status =
            AcquireTextureQualificationD3dRuntimeIdentityFromFiles(
                input, identity);
        assert(status.Succeeded());
        assert(!std::all_of(
            identity.d3d12CoreModuleSha256.begin(),
            identity.d3d12CoreModuleSha256.end(),
            [](uint8_t value) { return value == 0u; }));

        TextureQualificationD3dRuntimeFileInput missing = input;
        missing.dxilPath = nullptr;
        TextureQualificationD3dRuntimeIdentity rejected;
        status = AcquireTextureQualificationD3dRuntimeIdentityFromFiles(
            missing, rejected);
        assert(!status.Succeeded());
        assert(rejected.dxgiModuleSha256 == TextureQualificationSha256{});

        WriteBytes(paths[3], {});
        status = AcquireTextureQualificationD3dRuntimeIdentityFromFiles(
            input, rejected);
        assert(status.reason ==
            TextureQualificationRuntimeReason::FileSizeInvalid);
        assert(rejected.dxgiModuleSha256 == TextureQualificationSha256{});
    }

    void TestSamplerAndFeatureSnapshots()
    {
        nvrhi::SamplerDesc sampler;
        sampler.setAllFilters(true)
            .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
            .setMaxAnisotropy(16.f);
        constexpr uint32_t NegativeZeroBits = 0x80000000u;
        std::memcpy(
            &sampler.mipBias,
            &NegativeZeroBits,
            sizeof(NegativeZeroBits));
        TextureQualificationSamplerIdentity identity;
        TextureQualificationRuntimeStatus status =
            CaptureTextureQualificationSamplerIdentity(sampler, identity);
        assert(status.Succeeded());
        assert(identity.schema == TextureQualificationSamplerSchema);
        assert(identity.maxAnisotropyBits == FloatBits(16.f));
        assert(identity.mipBiasBits == NegativeZeroBits);
        assert(identity.addressU ==
            static_cast<uint32_t>(nvrhi::SamplerAddressMode::Wrap));

        sampler.maxAnisotropy = std::numeric_limits<float>::infinity();
        status = CaptureTextureQualificationSamplerIdentity(sampler, identity);
        assert(status.reason ==
            TextureQualificationRuntimeReason::SamplerFieldInvalid);
        sampler.maxAnisotropy = 17.f;
        status = CaptureTextureQualificationSamplerIdentity(sampler, identity);
        assert(status.reason ==
            TextureQualificationRuntimeReason::SamplerFieldInvalid);

        TextureQualificationFeatureFormatSnapshot snapshot;
        snapshot.schema = TextureQualificationFeatureFormatSchema;
        snapshot.highestShaderModel = 0x65u;
        snapshot.resourceBindingTier = 3u;
        snapshot.waveOps = 1u;
        snapshot.waveLaneCountMin = 32u;
        snapshot.waveLaneCountMax = 32u;
        snapshot.int64ShaderOps = 1u;
        for (size_t index = 0u; index < snapshot.formats.size(); ++index)
        {
            auto& format = snapshot.formats[index];
            format.dxgiFormat =
                GetTextureQualificationFeatureFormatDxgiFormat(index);
            assert(format.dxgiFormat != 0u);
            format.formatQuerySucceeded = 1u;
            format.support1 = uint32_t(0x100u + index);
            format.support2 = uint32_t(0x200u + index);
            format.qualityQuerySucceeded.fill(1u);
            format.qualityLevelCount = {
                uint32_t(index + 1u),
                uint32_t(index + 2u),
                uint32_t(index + 3u)
            };
        }
        TextureQualificationSha256 first;
        TextureQualificationSha256 second;
        status = ComputeTextureQualificationFeatureFormatDigest(
            snapshot, first);
        assert(status.Succeeded());
        snapshot.formats[0].support1 ^= 1u;
        status = ComputeTextureQualificationFeatureFormatDigest(
            snapshot, second);
        assert(status.Succeeded() && DigestsDiffer(first, second));
        std::swap(snapshot.formats[0], snapshot.formats[1]);
        status = ComputeTextureQualificationFeatureFormatDigest(
            snapshot, second);
        assert(status.reason ==
            TextureQualificationRuntimeReason::FormatQueryFailed);

        TextureQualificationSha256 semantic;
        TextureQualificationMatrixIdentity matrix;
        status = GetTextureQualificationCompiledSchemaIdentity(
            semantic, matrix);
        assert(status.Succeeded() &&
            matrix.schema == TextureQualificationMatrixRuntimeSchema &&
            matrix.requiredQualificationMasks ==
                TextureQualificationRequiredEvidenceMasks);
        assert(!std::all_of(
            semantic.begin(), semantic.end(),
            [](uint8_t value) { return value == 0u; }));
    }

    void TestCertificateFileAuthority(const TemporaryDirectory& temporary)
    {
        const TextureQualificationLiveIdentity identity = MakeIdentity();
        const TextureQualificationCertificate certificate =
            MakeCertificate(identity);
        const auto bytes = Serialize(certificate);
        const std::filesystem::path certificatePath =
            temporary.Path() / TextureQualificationCertificateFileName;

        TextureQualificationRuntimeStatus status =
            WriteTextureQualificationCertificateFileAtomically(
                certificatePath.c_str(), certificate);
        assert(status.Succeeded() &&
            status.byteCount == TextureQualificationCertificateByteCount);
        TextureQualificationCertificateReadResult read =
            ReadTextureQualificationCertificateFile(certificatePath.c_str());
        assert(read.status.Succeeded());
        assert(read.certificate.identity.adapter.driverVersion ==
            identity.adapter.driverVersion);
        TextureQualificationSha256 expectedFileDigest;
        assert(ComputeTextureQualificationSha256(
            bytes.data(), bytes.size(), expectedFileDigest) ==
            TextureQualificationReason::None);
        assert(read.fileSha256 == expectedFileDigest);

        WriteBytes(certificatePath, { 0u }, true);
        read = ReadTextureQualificationCertificateFile(certificatePath.c_str());
        assert(read.status.reason ==
            TextureQualificationRuntimeReason::FileSizeInvalid);
        status = WriteTextureQualificationCertificateFileAtomically(
            certificatePath.c_str(), certificate);
        assert(status.Succeeded());

        TextureQualificationCertificate invalidCertificate = certificate;
        invalidCertificate.samples[0].qualificationMask = 0u;
        status = WriteTextureQualificationCertificateFileAtomically(
            certificatePath.c_str(), invalidCertificate);
        assert(status.reason ==
            TextureQualificationRuntimeReason::CertificateValidationFailed);
        read = ReadTextureQualificationCertificateFile(
            certificatePath.c_str());
        assert(read.status.Succeeded() &&
            read.certificate.samples[0].qualificationMask ==
                TextureQualificationRequiredEvidenceMask);

        TextureQualificationCertificateAuthority authority;
        TextureQualificationLeaseResult lease = authority.Acquire(
            bytes.data(), bytes.size(), identity, Request(), 0u);
        assert(lease.status.reason ==
            TextureQualificationRuntimeReason::ContentRevisionZero);
        assert(authority.ActiveLease() == nullptr);

        lease = authority.Acquire(
            bytes.data(), bytes.size(), identity, Request(), 1u);
        assert(lease.Succeeded() && lease.token == 1u);
        assert(authority.IsActive(1u, 1u));
        assert(authority.ActiveLease() &&
            authority.ActiveLease()->certificate.identity.adapter.vendorId ==
                identity.adapter.vendorId &&
            authority.ActiveLease()->liveIdentity.artifacts.
                rendererExecutableSha256 ==
                identity.artifacts.rendererExecutableSha256);

        for (uint32_t sampleCount : { 2u, 4u, 8u })
        {
            for (uint32_t parity = 0u; parity < 4u; ++parity)
            {
                const TextureQualificationValidationResult perView =
                    authority.ValidateActive(Request(
                        sampleCount, parity & 1u, parity >> 1u));
                assert(perView.Succeeded());
                assert(authority.IsActive(1u, 1u));
            }
        }
        const TextureQualificationValidationResult rejected16 =
            authority.ValidateActive(Request(16u));
        assert(!rejected16.Succeeded() &&
            rejected16.reason ==
                TextureQualificationReason::Requested16Unsupported);
        assert(authority.IsActive(1u, 1u));
        TextureQualificationValidationRequest demoted16 = Request(8u);
        demoted16.requestedSampleCount = 16u;
        const TextureQualificationValidationResult rejectedDemoted16 =
            authority.ValidateActive(demoted16);
        assert(!rejectedDemoted16.Succeeded() &&
            rejectedDemoted16.reason ==
                TextureQualificationReason::Requested16Resolved8Unsupported);
        assert(authority.IsActive(1u, 1u));
        TextureQualificationValidationRequest resolved16 = Request(8u);
        resolved16.resolvedSampleCount = 16u;
        const TextureQualificationValidationResult rejectedResolved16 =
            authority.ValidateActive(resolved16);
        assert(!rejectedResolved16.Succeeded() &&
            rejectedResolved16.reason ==
                TextureQualificationReason::Resolved16Unsupported);
        assert(authority.IsActive(1u, 1u));

        std::array<uint8_t, TextureQualificationCertificateByteCount>
            corrupted = bytes;
        corrupted[0] ^= 1u;
        lease = authority.Acquire(
            corrupted.data(), corrupted.size(), identity, Request(), 2u);
        assert(!lease.Succeeded());
        assert(authority.IsActive(1u, 1u));
        assert(authority.LastContentRevision() == 1u);

        TextureQualificationLiveIdentity wrongIdentity = identity;
        ++wrongIdentity.adapter.driverVersion;
        lease = authority.Acquire(
            certificate, wrongIdentity, Request(), 2u);
        assert(!lease.Succeeded() &&
            lease.status.certificateReason ==
                TextureQualificationReason::AdapterDriverMismatch);
        assert(authority.IsActive(1u, 1u));
        assert(authority.LastContentRevision() == 1u);

        lease = authority.Acquire(certificate, identity, Request(), 1u);
        assert(lease.status.reason ==
            TextureQualificationRuntimeReason::ContentRevisionNotMonotonic);
        assert(authority.IsActive(1u, 1u));

        lease = authority.Acquire(certificate, identity, Request(4u, 1u), 2u);
        assert(lease.Succeeded() && lease.token == 2u);
        assert(!authority.IsActive(1u, 1u));
        assert(authority.IsActive(2u, 2u));
        assert(authority.ActiveLease() &&
            authority.ActiveLease()->request.requestedSampleCount == 4u &&
            authority.ActiveLease()->request.viewportOriginXParity == 1u &&
            authority.ActiveLease()->validation.Succeeded());
        status = authority.Revoke(1u);
        assert(status.reason ==
            TextureQualificationRuntimeReason::LeaseTokenMismatch);
        assert(authority.IsActive(2u, 2u));
        status = authority.Revoke(2u);
        assert(status.Succeeded() && authority.ActiveLease() == nullptr);
        assert(!authority.ValidateActive(Request()).Succeeded());

        lease = authority.Acquire(certificate, identity, Request(8u), 3u);
        assert(lease.Succeeded() && lease.token == 3u);

        TextureQualificationCertificateAuthority nextAuthority;
        lease = nextAuthority.Acquire(
            certificate, identity, Request(), 3u);
        assert(lease.status.reason ==
            TextureQualificationRuntimeReason::ContentRevisionNotMonotonic);
        lease = nextAuthority.Acquire(
            certificate, identity, Request(), 4u);
        assert(lease.Succeeded() && lease.token == 4u);

        TextureQualificationCertificateAuthority requested16Authority(
            TextureQualificationAuthoritySeed{ 1u, 0u });
        TextureQualificationValidationRequest sixteen = Request(16u);
        lease = requested16Authority.Acquire(
            certificate, identity, sixteen, 1u);
        assert(!lease.Succeeded() &&
            lease.status.certificateReason ==
                TextureQualificationReason::Requested16Unsupported);
        assert(requested16Authority.ActiveLease() == nullptr);

        TextureQualificationCertificateAuthority exhaustion(
            TextureQualificationAuthoritySeed{
                (std::numeric_limits<uint64_t>::max)(), 40u });
        lease = exhaustion.Acquire(
            certificate, identity, Request(), 41u);
        assert(lease.Succeeded() &&
            lease.token == (std::numeric_limits<uint64_t>::max)() &&
            exhaustion.TokenExhausted());
        const auto* maximumLease = exhaustion.ActiveLease();
        assert(maximumLease && maximumLease->contentRevision == 41u);
        lease = exhaustion.Acquire(
            certificate, identity, Request(4u), 42u);
        assert(lease.status.reason ==
            TextureQualificationRuntimeReason::LeaseTokenExhausted);
        assert(exhaustion.ActiveLease() &&
            exhaustion.ActiveLease()->token ==
                (std::numeric_limits<uint64_t>::max)() &&
            exhaustion.LastContentRevision() == 41u);

        TextureQualificationCertificateAuthority alreadyExhausted(
            TextureQualificationAuthoritySeed{ 0u, 0u });
        lease = alreadyExhausted.Acquire(
            certificate, identity, Request(), 1u);
        assert(lease.status.reason ==
            TextureQualificationRuntimeReason::LeaseTokenExhausted);
    }
}

int main()
{
    TemporaryDirectory temporary;
    TestFileAndAggregateHashing(temporary);
    TestRuntimeFileIdentity(temporary);
    TestSamplerAndFeatureSnapshots();
    TestCertificateFileAuthority(temporary);
    std::cout << "Adaptive MSAA texture qualification runtime tests passed\n";
    return 0;
}
