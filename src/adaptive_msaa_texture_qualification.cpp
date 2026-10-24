#include "adaptive_msaa_texture_qualification.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace uvsr::adaptive_msaa
{
    namespace
    {
        inline constexpr size_t WireHeaderByteCount = 24u;
        inline constexpr size_t WireAdapterByteCount = 32u;
        inline constexpr size_t WireRuntimeByteCount =
            4u * TextureQualificationSha256ByteCount;
        inline constexpr size_t WireArtifactByteCount =
            4u * TextureQualificationSha256ByteCount;
        inline constexpr size_t WireFeatureFormatByteCount =
            TextureQualificationSha256ByteCount;
        inline constexpr size_t WireSamplerByteCount = 14u * sizeof(uint32_t);
        inline constexpr size_t WireSemanticFormatByteCount =
            TextureQualificationSha256ByteCount;
        inline constexpr size_t WireMatrixByteCount =
            sizeof(uint32_t) + TextureQualificationSha256ByteCount +
            3u * sizeof(uint32_t);
        inline constexpr size_t WireSampleRecordsByteCount =
            3u * 3u * sizeof(uint32_t);

        static_assert(
            WireHeaderByteCount + WireAdapterByteCount +
                WireRuntimeByteCount + WireArtifactByteCount +
                WireFeatureFormatByteCount + WireSamplerByteCount +
                WireSemanticFormatByteCount + WireMatrixByteCount +
                WireSampleRecordsByteCount ==
                    TextureQualificationWireReservedOffset);
        static_assert(
            TextureQualificationWireReservedOffset +
                TextureQualificationWireReservedByteCount ==
                    TextureQualificationWireDigestOffset);
        static_assert(
            TextureQualificationWireDigestOffset +
                TextureQualificationSha256ByteCount ==
                    TextureQualificationCertificateByteCount);

        class BoundedWriter final
        {
        public:
            BoundedWriter(uint8_t* destination, size_t capacity)
                : m_Destination(destination)
                , m_Capacity(capacity)
            {
            }

            void Byte(uint8_t value)
            {
                if (!CanWrite(1u))
                    return;
                m_Destination[m_Position++] = value;
            }

            void U32(uint32_t value)
            {
                for (uint32_t byte = 0u; byte < 4u; ++byte)
                    Byte(static_cast<uint8_t>(value >> (byte * 8u)));
            }

            void U64(uint64_t value)
            {
                for (uint32_t byte = 0u; byte < 8u; ++byte)
                    Byte(static_cast<uint8_t>(value >> (byte * 8u)));
            }

            template<size_t ByteCount>
            void Bytes(const std::array<uint8_t, ByteCount>& bytes)
            {
                for (uint8_t byte : bytes)
                    Byte(byte);
            }

            void ZeroBytes(size_t byteCount)
            {
                for (size_t byte = 0u; byte < byteCount; ++byte)
                    Byte(0u);
            }

            [[nodiscard]] bool Succeeded() const
            {
                return m_Succeeded;
            }

            [[nodiscard]] size_t Position() const
            {
                return m_Position;
            }

        private:
            [[nodiscard]] bool CanWrite(size_t byteCount)
            {
                if (!m_Succeeded || m_Position > m_Capacity ||
                    byteCount > m_Capacity - m_Position)
                {
                    m_Succeeded = false;
                    return false;
                }
                return true;
            }

            uint8_t* m_Destination = nullptr;
            size_t m_Capacity = 0u;
            size_t m_Position = 0u;
            bool m_Succeeded = true;
        };

        class BoundedReader final
        {
        public:
            BoundedReader(const uint8_t* source, size_t byteCount)
                : m_Source(source)
                , m_ByteCount(byteCount)
            {
            }

            [[nodiscard]] uint8_t Byte()
            {
                if (!CanRead(1u))
                    return 0u;
                return m_Source[m_Position++];
            }

            [[nodiscard]] uint32_t U32()
            {
                uint32_t value = 0u;
                for (uint32_t byte = 0u; byte < 4u; ++byte)
                    value |= uint32_t(Byte()) << (byte * 8u);
                return value;
            }

            [[nodiscard]] uint64_t U64()
            {
                uint64_t value = 0u;
                for (uint32_t byte = 0u; byte < 8u; ++byte)
                    value |= uint64_t(Byte()) << (byte * 8u);
                return value;
            }

            template<size_t ByteCount>
            void Bytes(std::array<uint8_t, ByteCount>& bytes)
            {
                for (uint8_t& byte : bytes)
                    byte = Byte();
            }

            void Skip(size_t byteCount)
            {
                for (size_t byte = 0u; byte < byteCount; ++byte)
                    (void)Byte();
            }

            [[nodiscard]] bool Succeeded() const
            {
                return m_Succeeded;
            }

            [[nodiscard]] size_t Position() const
            {
                return m_Position;
            }

        private:
            [[nodiscard]] bool CanRead(size_t byteCount)
            {
                if (!m_Succeeded || m_Position > m_ByteCount ||
                    byteCount > m_ByteCount - m_Position)
                {
                    m_Succeeded = false;
                    return false;
                }
                return true;
            }

            const uint8_t* m_Source = nullptr;
            size_t m_ByteCount = 0u;
            size_t m_Position = 0u;
            bool m_Succeeded = true;
        };

        class BCryptAlgorithm final
        {
        public:
            ~BCryptAlgorithm()
            {
                if (handle != nullptr)
                    BCryptCloseAlgorithmProvider(handle, 0u);
            }

            BCRYPT_ALG_HANDLE handle = nullptr;
        };

        class BCryptHash final
        {
        public:
            ~BCryptHash()
            {
                if (handle != nullptr)
                    BCryptDestroyHash(handle);
            }

            BCRYPT_HASH_HANDLE handle = nullptr;
        };

        [[nodiscard]] bool Failed(NTSTATUS status)
        {
            return status < 0;
        }

        [[nodiscard]] bool IsZeroDigest(
            const TextureQualificationSha256& digest)
        {
            uint8_t accumulated = 0u;
            for (uint8_t byte : digest)
                accumulated |= byte;
            return accumulated == 0u;
        }

        [[nodiscard]] bool DigestsEqual(
            const TextureQualificationSha256& left,
            const TextureQualificationSha256& right)
        {
            uint8_t difference = 0u;
            for (size_t byte = 0u; byte < left.size(); ++byte)
                difference |= left[byte] ^ right[byte];
            return difference == 0u;
        }

        [[nodiscard]] uint32_t ReadU32At(
            const uint8_t* bytes,
            size_t offset)
        {
            uint32_t value = 0u;
            for (uint32_t byte = 0u; byte < 4u; ++byte)
                value |= uint32_t(bytes[offset + byte]) << (byte * 8u);
            return value;
        }

        void WriteAdapter(
            BoundedWriter& writer,
            const TextureQualificationAdapterIdentity& adapter)
        {
            writer.U32(adapter.luidLowPart);
            writer.U32(adapter.luidHighPartBits);
            writer.U32(adapter.vendorId);
            writer.U32(adapter.deviceId);
            writer.U32(adapter.subsystemId);
            writer.U32(adapter.revision);
            writer.U64(adapter.driverVersion);
        }

        void ReadAdapter(
            BoundedReader& reader,
            TextureQualificationAdapterIdentity& adapter)
        {
            adapter.luidLowPart = reader.U32();
            adapter.luidHighPartBits = reader.U32();
            adapter.vendorId = reader.U32();
            adapter.deviceId = reader.U32();
            adapter.subsystemId = reader.U32();
            adapter.revision = reader.U32();
            adapter.driverVersion = reader.U64();
        }

        void WriteRuntime(
            BoundedWriter& writer,
            const TextureQualificationD3dRuntimeIdentity& runtime)
        {
            writer.Bytes(runtime.dxgiModuleSha256);
            writer.Bytes(runtime.d3d12ModuleSha256);
            writer.Bytes(runtime.d3d12CoreModuleSha256);
            writer.Bytes(runtime.dxilModuleSha256);
        }

        void ReadRuntime(
            BoundedReader& reader,
            TextureQualificationD3dRuntimeIdentity& runtime)
        {
            reader.Bytes(runtime.dxgiModuleSha256);
            reader.Bytes(runtime.d3d12ModuleSha256);
            reader.Bytes(runtime.d3d12CoreModuleSha256);
            reader.Bytes(runtime.dxilModuleSha256);
        }

        void WriteArtifacts(
            BoundedWriter& writer,
            const TextureQualificationArtifactIdentity& artifacts)
        {
            writer.Bytes(artifacts.rendererExecutableSha256);
            writer.Bytes(artifacts.rendererBuildManifestSha256);
            writer.Bytes(artifacts.qualificationTestExecutableSha256);
            writer.Bytes(artifacts.qualificationTestDxilSetSha256);
        }

        void ReadArtifacts(
            BoundedReader& reader,
            TextureQualificationArtifactIdentity& artifacts)
        {
            reader.Bytes(artifacts.rendererExecutableSha256);
            reader.Bytes(artifacts.rendererBuildManifestSha256);
            reader.Bytes(artifacts.qualificationTestExecutableSha256);
            reader.Bytes(artifacts.qualificationTestDxilSetSha256);
        }

        void WriteSampler(
            BoundedWriter& writer,
            const TextureQualificationSamplerIdentity& sampler)
        {
            writer.U32(sampler.schema);
            for (uint32_t bits : sampler.borderColorBits)
                writer.U32(bits);
            writer.U32(sampler.maxAnisotropyBits);
            writer.U32(sampler.mipBiasBits);
            writer.U32(sampler.minFilter);
            writer.U32(sampler.magFilter);
            writer.U32(sampler.mipFilter);
            writer.U32(sampler.addressU);
            writer.U32(sampler.addressV);
            writer.U32(sampler.addressW);
            writer.U32(sampler.reductionType);
        }

        void ReadSampler(
            BoundedReader& reader,
            TextureQualificationSamplerIdentity& sampler)
        {
            sampler.schema = reader.U32();
            for (uint32_t& bits : sampler.borderColorBits)
                bits = reader.U32();
            sampler.maxAnisotropyBits = reader.U32();
            sampler.mipBiasBits = reader.U32();
            sampler.minFilter = reader.U32();
            sampler.magFilter = reader.U32();
            sampler.mipFilter = reader.U32();
            sampler.addressU = reader.U32();
            sampler.addressV = reader.U32();
            sampler.addressW = reader.U32();
            sampler.reductionType = reader.U32();
        }

        void WriteMatrix(
            BoundedWriter& writer,
            const TextureQualificationMatrixIdentity& matrix)
        {
            writer.U32(matrix.schema);
            writer.Bytes(matrix.schemaSha256);
            for (uint32_t mask : matrix.requiredQualificationMasks)
                writer.U32(mask);
        }

        void ReadMatrix(
            BoundedReader& reader,
            TextureQualificationMatrixIdentity& matrix)
        {
            matrix.schema = reader.U32();
            reader.Bytes(matrix.schemaSha256);
            for (uint32_t& mask : matrix.requiredQualificationMasks)
                mask = reader.U32();
        }

        [[nodiscard]] float FloatFromBits(uint32_t bits)
        {
            float value = 0.f;
            static_assert(sizeof(value) == sizeof(bits));
            std::memcpy(&value, &bits, sizeof(value));
            return value;
        }

        [[nodiscard]] bool IsCompleteSampler(
            const TextureQualificationSamplerIdentity& sampler)
        {
            if (sampler.schema != TextureQualificationSamplerSchema ||
                sampler.minFilter > 1u ||
                sampler.magFilter > 1u ||
                sampler.mipFilter > 1u ||
                sampler.addressU > 4u ||
                sampler.addressV > 4u ||
                sampler.addressW > 4u ||
                sampler.reductionType > 3u)
            {
                return false;
            }

            const float maxAnisotropy =
                FloatFromBits(sampler.maxAnisotropyBits);
            const float mipBias = FloatFromBits(sampler.mipBiasBits);
            if (!std::isfinite(maxAnisotropy) ||
                maxAnisotropy < 1.f || maxAnisotropy > 16.f ||
                !std::isfinite(mipBias))
            {
                return false;
            }
            for (uint32_t bits : sampler.borderColorBits)
            {
                if (!std::isfinite(FloatFromBits(bits)))
                    return false;
            }
            return true;
        }

        [[nodiscard]] TextureQualificationReason CheckIdentityComplete(
            const TextureQualificationLiveIdentity& identity,
            bool expected)
        {
            if ((identity.adapter.luidLowPart == 0u &&
                    identity.adapter.luidHighPartBits == 0u) ||
                identity.adapter.vendorId == 0u ||
                identity.adapter.deviceId == 0u ||
                identity.adapter.driverVersion == 0u)
            {
                return expected ?
                    TextureQualificationReason::ExpectedAdapterIdentityMissing :
                    TextureQualificationReason::CertificateAdapterIdentityMissing;
            }

            if (IsZeroDigest(identity.runtime.dxgiModuleSha256) ||
                IsZeroDigest(identity.runtime.d3d12ModuleSha256) ||
                IsZeroDigest(identity.runtime.d3d12CoreModuleSha256) ||
                IsZeroDigest(identity.runtime.dxilModuleSha256))
            {
                return expected ?
                    TextureQualificationReason::ExpectedRuntimeIdentityMissing :
                    TextureQualificationReason::CertificateRuntimeIdentityMissing;
            }

            if (IsZeroDigest(identity.artifacts.rendererExecutableSha256) ||
                IsZeroDigest(identity.artifacts.rendererBuildManifestSha256) ||
                IsZeroDigest(
                    identity.artifacts.qualificationTestExecutableSha256) ||
                IsZeroDigest(identity.artifacts.qualificationTestDxilSetSha256))
            {
                return expected ?
                    TextureQualificationReason::ExpectedArtifactIdentityMissing :
                    TextureQualificationReason::CertificateArtifactIdentityMissing;
            }

            if (IsZeroDigest(identity.featureFormatSetSha256))
            {
                return expected ?
                    TextureQualificationReason::ExpectedFeatureFormatIdentityMissing :
                    TextureQualificationReason::CertificateFeatureFormatIdentityMissing;
            }

            if (!IsCompleteSampler(identity.sampler))
            {
                return expected ?
                    TextureQualificationReason::ExpectedSamplerIdentityMissing :
                    TextureQualificationReason::CertificateSamplerIdentityMissing;
            }

            if (IsZeroDigest(identity.semanticFormatSchemaSha256))
            {
                return expected ?
                    TextureQualificationReason::ExpectedSemanticFormatSchemaMissing :
                    TextureQualificationReason::CertificateSemanticFormatSchemaMissing;
            }

            if (identity.matrix.schema == 0u ||
                IsZeroDigest(identity.matrix.schemaSha256))
            {
                return expected ?
                    TextureQualificationReason::ExpectedMatrixSchemaMissing :
                    TextureQualificationReason::CertificateMatrixSchemaMissing;
            }
            for (uint32_t mask : identity.matrix.requiredQualificationMasks)
            {
                if (mask == 0u)
                {
                    return expected ?
                        TextureQualificationReason::ExpectedMatrixSchemaMissing :
                        TextureQualificationReason::CertificateMatrixSchemaMissing;
                }
            }
            return TextureQualificationReason::None;
        }

        [[nodiscard]] bool SamplersEqual(
            const TextureQualificationSamplerIdentity& left,
            const TextureQualificationSamplerIdentity& right)
        {
            return left.schema == right.schema &&
                left.borderColorBits == right.borderColorBits &&
                left.maxAnisotropyBits == right.maxAnisotropyBits &&
                left.mipBiasBits == right.mipBiasBits &&
                left.minFilter == right.minFilter &&
                left.magFilter == right.magFilter &&
                left.mipFilter == right.mipFilter &&
                left.addressU == right.addressU &&
                left.addressV == right.addressV &&
                left.addressW == right.addressW &&
                left.reductionType == right.reductionType;
        }

        [[nodiscard]] TextureQualificationValidationResult Reject(
            TextureQualificationReason reason)
        {
            TextureQualificationValidationResult result;
            result.reason = reason;
            return result;
        }
    }

    TextureQualificationReason ComputeTextureQualificationSha256(
        const uint8_t* bytes,
        size_t byteCount,
        TextureQualificationSha256& digest) noexcept
    {
        digest.fill(0u);
        if (bytes == nullptr && byteCount != 0u)
            return TextureQualificationReason::HashInputMissing;

        try
        {
            BCryptAlgorithm algorithm;
            if (Failed(BCryptOpenAlgorithmProvider(
                    &algorithm.handle,
                    BCRYPT_SHA256_ALGORITHM,
                    nullptr,
                    0u)))
            {
                return TextureQualificationReason::Sha256ProviderUnavailable;
            }

            DWORD hashObjectByteCount = 0u;
            DWORD propertyByteCount = 0u;
            if (Failed(BCryptGetProperty(
                    algorithm.handle,
                    BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&hashObjectByteCount),
                    sizeof(hashObjectByteCount),
                    &propertyByteCount,
                    0u)) ||
                propertyByteCount != sizeof(hashObjectByteCount) ||
                hashObjectByteCount == 0u)
            {
                return TextureQualificationReason::Sha256PropertyQueryFailed;
            }

            DWORD hashByteCount = 0u;
            propertyByteCount = 0u;
            if (Failed(BCryptGetProperty(
                    algorithm.handle,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&hashByteCount),
                    sizeof(hashByteCount),
                    &propertyByteCount,
                    0u)) ||
                propertyByteCount != sizeof(hashByteCount) ||
                hashByteCount != digest.size())
            {
                return TextureQualificationReason::Sha256PropertyQueryFailed;
            }

            std::vector<uint8_t> hashObject(hashObjectByteCount);
            BCryptHash hash;
            if (Failed(BCryptCreateHash(
                    algorithm.handle,
                    &hash.handle,
                    hashObject.data(),
                    static_cast<ULONG>(hashObject.size()),
                    nullptr,
                    0u,
                    0u)))
            {
                return TextureQualificationReason::Sha256HashCreationFailed;
            }

            size_t offset = 0u;
            while (offset < byteCount)
            {
                const size_t remaining = byteCount - offset;
                const ULONG chunk = static_cast<ULONG>(std::min<size_t>(
                    remaining,
                    std::numeric_limits<ULONG>::max()));
                if (Failed(BCryptHashData(
                        hash.handle,
                        const_cast<PUCHAR>(bytes + offset),
                        chunk,
                        0u)))
                {
                    return TextureQualificationReason::Sha256HashDataFailed;
                }
                offset += chunk;
            }

            if (Failed(BCryptFinishHash(
                    hash.handle,
                    digest.data(),
                    static_cast<ULONG>(digest.size()),
                    0u)))
            {
                digest.fill(0u);
                return TextureQualificationReason::Sha256HashFinishFailed;
            }
        }
        catch (const std::bad_alloc&)
        {
            digest.fill(0u);
            return TextureQualificationReason::Sha256StateAllocationFailed;
        }
        catch (...)
        {
            digest.fill(0u);
            return TextureQualificationReason::Sha256StateAllocationFailed;
        }

        return TextureQualificationReason::None;
    }

    const char* GetTextureQualificationReasonName(
        TextureQualificationReason reason) noexcept
    {
#define UVSR_TEXTURE_QUALIFICATION_REASON_NAME(name) \
        case TextureQualificationReason::name: return #name
        switch (reason)
        {
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(None);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(HashInputMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(SerializationOutputMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(SerializationOutputTooSmall);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Sha256ProviderUnavailable);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Sha256PropertyQueryFailed);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Sha256StateAllocationFailed);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Sha256HashCreationFailed);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Sha256HashDataFailed);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Sha256HashFinishFailed);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateTruncated);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateHasTrailingBytes);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(MagicMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(SchemaMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(EndianMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(DeclaredSizeMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(ReservedBytesNotZero);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(IntegrityDigestMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(ExpectedAdapterIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(ExpectedRuntimeIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(ExpectedArtifactIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(ExpectedFeatureFormatIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(ExpectedSamplerIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(ExpectedSemanticFormatSchemaMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(ExpectedMatrixSchemaMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateAdapterIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateRuntimeIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateArtifactIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateFeatureFormatIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateSamplerIdentityMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateSemanticFormatSchemaMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(CertificateMatrixSchemaMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(AdapterLuidMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(AdapterHardwareMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(AdapterDriverMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(DxgiRuntimeModuleMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(D3d12RuntimeModuleMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(D3d12CoreRuntimeModuleMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(DxilRuntimeModuleMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(RendererExecutableMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(RendererBuildManifestMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(QualificationTestExecutableMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(QualificationTestDxilSetMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(FeatureFormatSetMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(SamplerIdentityMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(SemanticFormatSchemaMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(MatrixSchemaMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(MatrixRequiredMaskMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(QualificationRecordOrderMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(QualificationMaskMissing);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(QualificationMaskMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(QualificationParityMaskMalformed);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(IncompleteViewportParityQualification);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Requested16Resolved8Unsupported);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Requested16Unsupported);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Resolved16Unsupported);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(UnsupportedRequestedSampleCount);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(UnsupportedResolvedSampleCount);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(RequestedResolvedSampleCountMismatch);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(InvalidViewportOriginParity);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(ViewportOriginParityNotQualified);
            UVSR_TEXTURE_QUALIFICATION_REASON_NAME(Count);
        }
#undef UVSR_TEXTURE_QUALIFICATION_REASON_NAME
        return "Unknown";
    }

    TextureQualificationStatus SerializeTextureQualificationCertificate(
        const TextureQualificationCertificate& certificate,
        uint8_t* destination,
        size_t destinationByteCount) noexcept
    {
        TextureQualificationStatus status;
        if (destination == nullptr)
        {
            status.reason =
                TextureQualificationReason::SerializationOutputMissing;
            return status;
        }
        if (destinationByteCount < TextureQualificationCertificateByteCount)
        {
            status.reason =
                TextureQualificationReason::SerializationOutputTooSmall;
            return status;
        }

        BoundedWriter writer(destination, TextureQualificationCertificateByteCount);
        writer.Bytes(TextureQualificationCertificateMagic);
        writer.U32(TextureQualificationCertificateSchema);
        writer.U32(TextureQualificationLittleEndianTag);
        writer.U32(static_cast<uint32_t>(
            TextureQualificationCertificateByteCount));
        writer.U32(0u);

        WriteAdapter(writer, certificate.identity.adapter);
        WriteRuntime(writer, certificate.identity.runtime);
        WriteArtifacts(writer, certificate.identity.artifacts);
        writer.Bytes(certificate.identity.featureFormatSetSha256);
        WriteSampler(writer, certificate.identity.sampler);
        writer.Bytes(certificate.identity.semanticFormatSchemaSha256);
        WriteMatrix(writer, certificate.identity.matrix);
        for (const TextureQualificationSampleRecord& sample :
            certificate.samples)
        {
            writer.U32(sample.sampleCount);
            writer.U32(sample.qualificationMask);
            writer.U32(sample.viewportOriginParityMask);
        }
        writer.ZeroBytes(TextureQualificationWireReservedByteCount);

        if (!writer.Succeeded() ||
            writer.Position() != TextureQualificationWireDigestOffset)
        {
            std::fill_n(
                destination,
                TextureQualificationCertificateByteCount,
                uint8_t(0u));
            status.reason = TextureQualificationReason::DeclaredSizeMismatch;
            return status;
        }

        TextureQualificationSha256 digest;
        status.reason = ComputeTextureQualificationSha256(
            destination,
            TextureQualificationWireDigestOffset,
            digest);
        if (status.reason != TextureQualificationReason::None)
        {
            std::fill_n(
                destination,
                TextureQualificationCertificateByteCount,
                uint8_t(0u));
            return status;
        }
        writer.Bytes(digest);
        if (!writer.Succeeded() ||
            writer.Position() != TextureQualificationCertificateByteCount)
        {
            std::fill_n(
                destination,
                TextureQualificationCertificateByteCount,
                uint8_t(0u));
            status.reason = TextureQualificationReason::DeclaredSizeMismatch;
            return status;
        }

        status.reason = TextureQualificationReason::None;
        status.byteCount = TextureQualificationCertificateByteCount;
        return status;
    }

    TextureQualificationStatus ParseTextureQualificationCertificate(
        const uint8_t* source,
        size_t sourceByteCount,
        TextureQualificationCertificate& certificate) noexcept
    {
        certificate = {};
        TextureQualificationStatus status;
        if (source == nullptr)
        {
            status.reason = TextureQualificationReason::CertificateMissing;
            return status;
        }
        if (sourceByteCount < TextureQualificationCertificateByteCount)
        {
            status.reason = TextureQualificationReason::CertificateTruncated;
            return status;
        }
        if (sourceByteCount > TextureQualificationCertificateByteCount)
        {
            status.reason =
                TextureQualificationReason::CertificateHasTrailingBytes;
            return status;
        }

        for (size_t byte = 0u;
            byte < TextureQualificationCertificateMagic.size();
            ++byte)
        {
            if (source[byte] != TextureQualificationCertificateMagic[byte])
            {
                status.reason = TextureQualificationReason::MagicMismatch;
                return status;
            }
        }
        if (ReadU32At(source, 8u) !=
            TextureQualificationCertificateSchema)
        {
            status.reason = TextureQualificationReason::SchemaMismatch;
            return status;
        }
        if (ReadU32At(source, 12u) != TextureQualificationLittleEndianTag)
        {
            status.reason = TextureQualificationReason::EndianMismatch;
            return status;
        }
        if (ReadU32At(source, 16u) !=
            TextureQualificationCertificateByteCount)
        {
            status.reason = TextureQualificationReason::DeclaredSizeMismatch;
            return status;
        }
        for (size_t byte = 0u; byte < 4u; ++byte)
        {
            if (source[TextureQualificationWireHeaderReservedOffset + byte] !=
                0u)
            {
                status.reason =
                    TextureQualificationReason::ReservedBytesNotZero;
                return status;
            }
        }
        for (size_t byte = 0u;
            byte < TextureQualificationWireReservedByteCount;
            ++byte)
        {
            if (source[TextureQualificationWireReservedOffset + byte] != 0u)
            {
                status.reason =
                    TextureQualificationReason::ReservedBytesNotZero;
                return status;
            }
        }

        TextureQualificationSha256 expectedDigest;
        status.reason = ComputeTextureQualificationSha256(
            source,
            TextureQualificationWireDigestOffset,
            expectedDigest);
        if (status.reason != TextureQualificationReason::None)
            return status;
        TextureQualificationSha256 storedDigest;
        for (size_t byte = 0u; byte < storedDigest.size(); ++byte)
        {
            storedDigest[byte] =
                source[TextureQualificationWireDigestOffset + byte];
        }
        if (!DigestsEqual(expectedDigest, storedDigest))
        {
            status.reason = TextureQualificationReason::IntegrityDigestMismatch;
            return status;
        }

        TextureQualificationCertificate parsed;
        BoundedReader reader(source, TextureQualificationWireDigestOffset);
        reader.Skip(TextureQualificationCertificateMagic.size());
        (void)reader.U32();
        (void)reader.U32();
        (void)reader.U32();
        (void)reader.U32();
        ReadAdapter(reader, parsed.identity.adapter);
        ReadRuntime(reader, parsed.identity.runtime);
        ReadArtifacts(reader, parsed.identity.artifacts);
        reader.Bytes(parsed.identity.featureFormatSetSha256);
        ReadSampler(reader, parsed.identity.sampler);
        reader.Bytes(parsed.identity.semanticFormatSchemaSha256);
        ReadMatrix(reader, parsed.identity.matrix);
        for (TextureQualificationSampleRecord& sample : parsed.samples)
        {
            sample.sampleCount = reader.U32();
            sample.qualificationMask = reader.U32();
            sample.viewportOriginParityMask = reader.U32();
        }
        reader.Skip(TextureQualificationWireReservedByteCount);
        if (!reader.Succeeded() ||
            reader.Position() != TextureQualificationWireDigestOffset)
        {
            status.reason = TextureQualificationReason::DeclaredSizeMismatch;
            return status;
        }

        certificate = parsed;
        status.reason = TextureQualificationReason::None;
        status.byteCount = TextureQualificationCertificateByteCount;
        return status;
    }

    TextureQualificationValidationResult
        ValidateTextureQualificationCertificate(
            const TextureQualificationCertificate* certificate,
            const TextureQualificationLiveIdentity& expectedLiveIdentity,
            const TextureQualificationValidationRequest& request) noexcept
    {
        if (certificate == nullptr)
            return Reject(TextureQualificationReason::CertificateMissing);

        if (request.requestedSampleCount == 16u &&
            request.resolvedSampleCount == 8u)
        {
            return Reject(
                TextureQualificationReason::Requested16Resolved8Unsupported);
        }
        if (request.requestedSampleCount == 16u)
            return Reject(TextureQualificationReason::Requested16Unsupported);
        if (request.resolvedSampleCount == 16u)
            return Reject(TextureQualificationReason::Resolved16Unsupported);

        const auto IsSupportedSampleCount = [](uint32_t sampleCount)
        {
            return sampleCount == 2u || sampleCount == 4u ||
                sampleCount == 8u;
        };
        if (!IsSupportedSampleCount(request.requestedSampleCount))
        {
            return Reject(
                TextureQualificationReason::UnsupportedRequestedSampleCount);
        }
        if (!IsSupportedSampleCount(request.resolvedSampleCount))
        {
            return Reject(
                TextureQualificationReason::UnsupportedResolvedSampleCount);
        }
        if (request.requestedSampleCount != request.resolvedSampleCount)
        {
            return Reject(
                TextureQualificationReason::RequestedResolvedSampleCountMismatch);
        }
        if (request.viewportOriginXParity > 1u ||
            request.viewportOriginYParity > 1u)
        {
            return Reject(
                TextureQualificationReason::InvalidViewportOriginParity);
        }

        TextureQualificationReason reason = CheckIdentityComplete(
            expectedLiveIdentity,
            true);
        if (reason != TextureQualificationReason::None)
            return Reject(reason);
        reason = CheckIdentityComplete(certificate->identity, false);
        if (reason != TextureQualificationReason::None)
            return Reject(reason);

        const TextureQualificationAdapterIdentity& actualAdapter =
            certificate->identity.adapter;
        const TextureQualificationAdapterIdentity& expectedAdapter =
            expectedLiveIdentity.adapter;
        if (actualAdapter.luidLowPart != expectedAdapter.luidLowPart ||
            actualAdapter.luidHighPartBits !=
                expectedAdapter.luidHighPartBits)
        {
            return Reject(TextureQualificationReason::AdapterLuidMismatch);
        }
        if (actualAdapter.vendorId != expectedAdapter.vendorId ||
            actualAdapter.deviceId != expectedAdapter.deviceId ||
            actualAdapter.subsystemId != expectedAdapter.subsystemId ||
            actualAdapter.revision != expectedAdapter.revision)
        {
            return Reject(TextureQualificationReason::AdapterHardwareMismatch);
        }
        if (actualAdapter.driverVersion != expectedAdapter.driverVersion)
            return Reject(TextureQualificationReason::AdapterDriverMismatch);

        const TextureQualificationD3dRuntimeIdentity& actualRuntime =
            certificate->identity.runtime;
        const TextureQualificationD3dRuntimeIdentity& expectedRuntime =
            expectedLiveIdentity.runtime;
        if (!DigestsEqual(
                actualRuntime.dxgiModuleSha256,
                expectedRuntime.dxgiModuleSha256))
        {
            return Reject(
                TextureQualificationReason::DxgiRuntimeModuleMismatch);
        }
        if (!DigestsEqual(
                actualRuntime.d3d12ModuleSha256,
                expectedRuntime.d3d12ModuleSha256))
        {
            return Reject(
                TextureQualificationReason::D3d12RuntimeModuleMismatch);
        }
        if (!DigestsEqual(
                actualRuntime.d3d12CoreModuleSha256,
                expectedRuntime.d3d12CoreModuleSha256))
        {
            return Reject(
                TextureQualificationReason::D3d12CoreRuntimeModuleMismatch);
        }
        if (!DigestsEqual(
                actualRuntime.dxilModuleSha256,
                expectedRuntime.dxilModuleSha256))
        {
            return Reject(
                TextureQualificationReason::DxilRuntimeModuleMismatch);
        }

        const TextureQualificationArtifactIdentity& actualArtifacts =
            certificate->identity.artifacts;
        const TextureQualificationArtifactIdentity& expectedArtifacts =
            expectedLiveIdentity.artifacts;
        if (!DigestsEqual(
                actualArtifacts.rendererExecutableSha256,
                expectedArtifacts.rendererExecutableSha256))
        {
            return Reject(
                TextureQualificationReason::RendererExecutableMismatch);
        }
        if (!DigestsEqual(
                actualArtifacts.rendererBuildManifestSha256,
                expectedArtifacts.rendererBuildManifestSha256))
        {
            return Reject(
                TextureQualificationReason::RendererBuildManifestMismatch);
        }
        if (!DigestsEqual(
                actualArtifacts.qualificationTestExecutableSha256,
                expectedArtifacts.qualificationTestExecutableSha256))
        {
            return Reject(
                TextureQualificationReason::QualificationTestExecutableMismatch);
        }
        if (!DigestsEqual(
                actualArtifacts.qualificationTestDxilSetSha256,
                expectedArtifacts.qualificationTestDxilSetSha256))
        {
            return Reject(
                TextureQualificationReason::QualificationTestDxilSetMismatch);
        }

        if (!DigestsEqual(
                certificate->identity.featureFormatSetSha256,
                expectedLiveIdentity.featureFormatSetSha256))
        {
            return Reject(TextureQualificationReason::FeatureFormatSetMismatch);
        }
        if (!SamplersEqual(
                certificate->identity.sampler,
                expectedLiveIdentity.sampler))
        {
            return Reject(TextureQualificationReason::SamplerIdentityMismatch);
        }
        if (!DigestsEqual(
                certificate->identity.semanticFormatSchemaSha256,
                expectedLiveIdentity.semanticFormatSchemaSha256))
        {
            return Reject(
                TextureQualificationReason::SemanticFormatSchemaMismatch);
        }
        if (certificate->identity.matrix.schema !=
                expectedLiveIdentity.matrix.schema ||
            !DigestsEqual(
                certificate->identity.matrix.schemaSha256,
                expectedLiveIdentity.matrix.schemaSha256))
        {
            return Reject(TextureQualificationReason::MatrixSchemaMismatch);
        }
        if (certificate->identity.matrix.requiredQualificationMasks !=
            expectedLiveIdentity.matrix.requiredQualificationMasks)
        {
            return Reject(
                TextureQualificationReason::MatrixRequiredMaskMismatch);
        }

        for (size_t index = 0u; index < certificate->samples.size(); ++index)
        {
            const TextureQualificationSampleRecord& sample =
                certificate->samples[index];
            if (sample.sampleCount != TextureQualificationSampleCounts[index])
            {
                return Reject(
                    TextureQualificationReason::QualificationRecordOrderMismatch);
            }
            if (sample.qualificationMask == 0u)
            {
                return Reject(
                    TextureQualificationReason::QualificationMaskMissing);
            }
            if (sample.qualificationMask !=
                certificate->identity.matrix.requiredQualificationMasks[index])
            {
                return Reject(
                    TextureQualificationReason::QualificationMaskMismatch);
            }
            if ((sample.viewportOriginParityMask &
                    ~TextureQualificationViewportParityMask) != 0u)
            {
                return Reject(
                    TextureQualificationReason::QualificationParityMaskMalformed);
            }
            if (sample.viewportOriginParityMask !=
                TextureQualificationViewportParityMask)
            {
                return Reject(
                    TextureQualificationReason::
                        IncompleteViewportParityQualification);
            }
        }

        size_t selectedIndex = 0u;
        while (TextureQualificationSampleCounts[selectedIndex] !=
            request.resolvedSampleCount)
        {
            ++selectedIndex;
        }
        const TextureQualificationSampleRecord& selected =
            certificate->samples[selectedIndex];
        const uint32_t parityIndex = request.viewportOriginXParity |
            (request.viewportOriginYParity << 1u);
        if ((selected.viewportOriginParityMask & (1u << parityIndex)) == 0u)
        {
            return Reject(
                TextureQualificationReason::ViewportOriginParityNotQualified);
        }

        TextureQualificationValidationResult result;
        result.accepted = true;
        result.reason = TextureQualificationReason::None;
        result.sampleCount = selected.sampleCount;
        result.qualificationMask = selected.qualificationMask;
        result.viewportOriginParityIndex = parityIndex;
        return result;
    }
}
