#include "adaptive_msaa_texture_qualification_runtime.h"

#include <Windows.h>
#include <bcrypt.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <nvrhi/nvrhi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

namespace
{
    using namespace uvsr::adaptive_msaa;
    using Microsoft::WRL::ComPtr;

    constexpr size_t FileReadChunkByteCount = 64u * 1024u;
    constexpr size_t MaximumCanonicalLabelByteCount = 1024u;
    constexpr size_t MaximumWindowsPathCharacterCount = 32767u;

    constexpr std::array<DXGI_FORMAT,
        TextureQualificationFeatureFormatRecordCount>
        FeatureFormats = {
            DXGI_FORMAT_R32G32_UINT,
            DXGI_FORMAT_R8_UINT,
            DXGI_FORMAT_R32_UINT,
            DXGI_FORMAT_D32_FLOAT,
            DXGI_FORMAT_R32_FLOAT,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R8G8_UNORM,
            DXGI_FORMAT_R8_UNORM,
            DXGI_FORMAT_BC1_UNORM_SRGB,
            DXGI_FORMAT_BC3_UNORM_SRGB,
            DXGI_FORMAT_BC4_UNORM,
            DXGI_FORMAT_BC5_UNORM,
            DXGI_FORMAT_BC7_UNORM_SRGB,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16B16A16_SNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16_FLOAT
        };

    constexpr char AggregateDomain[] =
        "UVSR-TEXTURE-QUALIFICATION-LABELED-FILE-SET-V1";
    constexpr char FeatureFormatDomain[] =
        "UVSR-TEXTURE-QUALIFICATION-FEATURE-FORMAT-SET-V1";

#if UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC
    constexpr char SemanticFormatManifest[] =
        "uvsr.compact-texture.semantic-format-schema=2\n"
        "profile=sponza-rgba8-4x-poc-v1\n"
        "sample-count=4-only\n"
        "domain=opaque-only\n"
        "geometry=triangle-list,undeformed,single-sided\n"
        "alpha-tested=local-full-g-buffer\n"
        "opacity-texture=local-full-g-buffer\n"
        "normal-texture=RGBA8_UNORM\n"
        "transmission=local-full-g-buffer\n"
        "base-or-diffuse=SRGBA8_UNORM|BC1_UNORM_SRGB\n"
        "metal-rough-or-specular=RG8_UNORM|RGBA8_UNORM|BC5_UNORM\n"
        "emissive=SRGBA8_UNORM|BC7_UNORM_SRGB\n"
        "occlusion=R8_UNORM|BC4_UNORM\n"
        "resource=non-rt-or-finalized-load-time-mip-rt-capable-srv\n"
        "sampler=live-exact-nvrhi-samplerdesc-schema1\n";

    constexpr char MatrixManifest[] =
        "uvsr.compact-texture.matrix-schema=2\n"
        "profile=sponza-rgba8-4x-poc-v1\n"
        "sample-counts=2,4,8\n"
        "requested-16=closed\n"
        "requested-16-resolved-8=closed\n"
        "viewport-origin-parities=00,10,01,11\n"
        "viewport-extents=odd-and-even\n"
        "rows=G1,G2,G3,G4,G5,G6,G7,G8,A2,A4,A8,P00,P10,P01,P11\n"
        "4x-uncompressed-metal-rough=RGBA8_UNORM\n"
        "4x-uncompressed-normal=RGBA8_UNORM\n"
        "4x-uncompressed-resource=rt-capable-authored-mip-srv\n"
        "depth-p99=0.000001\n"
        "depth-max=0.00001\n"
        "quantized-guide-p99=1/255+1/2048\n"
        "other-guide-p99=1/1024\n"
        "all-guide-max=1/128\n"
        "mismatch-count=0\n"
        "nonfinite-count=0\n"
        "validation-errors=0\n"
        "evidence-mask=0x00007fff\n";
#else
    constexpr char SemanticFormatManifest[] =
        "uvsr.compact-texture.semantic-format-schema=1\n"
        "domain=opaque-only\n"
        "geometry=triangle-list,undeformed,single-sided\n"
        "alpha-tested=local-full-g-buffer\n"
        "opacity-texture=local-full-g-buffer\n"
        "normal-texture=local-full-g-buffer\n"
        "transmission=local-full-g-buffer\n"
        "base-or-diffuse=SRGBA8_UNORM|BC1_UNORM_SRGB\n"
        "metal-rough-or-specular=RG8_UNORM|BC5_UNORM\n"
        "emissive=SRGBA8_UNORM|BC7_UNORM_SRGB\n"
        "occlusion=R8_UNORM|BC4_UNORM\n"
        "sampler=live-exact-nvrhi-samplerdesc-schema1\n";

    constexpr char MatrixManifest[] =
        "uvsr.compact-texture.matrix-schema=1\n"
        "sample-counts=2,4,8\n"
        "requested-16=closed\n"
        "requested-16-resolved-8=closed\n"
        "viewport-origin-parities=00,10,01,11\n"
        "viewport-extents=odd-and-even\n"
        "rows=G1,G2,G3,G4,G5,G6,G7,G8,A2,A4,A8,P00,P10,P01,P11\n"
        "depth-p99=0.000001\n"
        "depth-max=0.00001\n"
        "quantized-guide-p99=1/255+1/2048\n"
        "other-guide-p99=1/1024\n"
        "all-guide-max=1/128\n"
        "mismatch-count=0\n"
        "nonfinite-count=0\n"
        "validation-errors=0\n"
        "evidence-mask=0x00007fff\n";
#endif

    HMODULE PinnedDxilModule = nullptr;

    TextureQualificationRuntimeStatus Success(uint64_t byteCount = 0u)
    {
        TextureQualificationRuntimeStatus result;
        result.reason = TextureQualificationRuntimeReason::None;
        result.byteCount = byteCount;
        return result;
    }

    TextureQualificationRuntimeStatus Fail(
        TextureQualificationRuntimeReason reason,
        uint32_t systemError = 0u,
        int32_t nativeResult = 0)
    {
        TextureQualificationRuntimeStatus result;
        result.reason = reason;
        result.systemError = systemError;
        result.nativeResult = nativeResult;
        return result;
    }

    struct ProcessAuthorityState final
    {
        SRWLOCK lock{};
        uint64_t nextToken = 1u;
        uint64_t lastContentRevision = 0u;
        bool tokenExhausted = false;
    };

    ProcessAuthorityState& GetProcessAuthorityState() noexcept
    {
        static ProcessAuthorityState state;
        return state;
    }

    size_t BoundedWideLength(const wchar_t* value, size_t limit) noexcept
    {
        if (!value)
            return 0u;
        size_t length = 0u;
        while (length < limit && value[length] != L'\0')
            ++length;
        return length;
    }

    uint64_t AllocateTemporaryCounter() noexcept
    {
        static std::atomic<uint64_t> nextCounter{ 1u };
        uint64_t current = nextCounter.load(std::memory_order_relaxed);
        while (current != 0u)
        {
            const uint64_t next =
                current == (std::numeric_limits<uint64_t>::max)()
                ? 0u
                : current + 1u;
            if (nextCounter.compare_exchange_weak(
                    current,
                    next,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed))
            {
                return current;
            }
        }
        return 0u;
    }

    class Sha256Stream final
    {
    public:
        Sha256Stream() = default;
        Sha256Stream(const Sha256Stream&) = delete;
        Sha256Stream& operator=(const Sha256Stream&) = delete;

        ~Sha256Stream()
        {
            if (m_Hash)
                BCryptDestroyHash(m_Hash);
            if (m_Provider)
                BCryptCloseAlgorithmProvider(m_Provider, 0u);
        }

        TextureQualificationRuntimeStatus Initialize()
        {
            NTSTATUS nativeStatus = BCryptOpenAlgorithmProvider(
                &m_Provider,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                0u);
            if (nativeStatus < 0)
            {
                return Fail(
                    TextureQualificationRuntimeReason::HashProviderUnavailable,
                    0u,
                    nativeStatus);
            }

            ULONG objectByteCount = 0u;
            ULONG resultByteCount = 0u;
            nativeStatus = BCryptGetProperty(
                m_Provider,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectByteCount),
                sizeof(objectByteCount),
                &resultByteCount,
                0u);
            if (nativeStatus < 0 || resultByteCount != sizeof(objectByteCount))
            {
                return Fail(
                    TextureQualificationRuntimeReason::HashPropertyQueryFailed,
                    0u,
                    nativeStatus);
            }
            ULONG digestByteCount = 0u;
            nativeStatus = BCryptGetProperty(
                m_Provider,
                BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&digestByteCount),
                sizeof(digestByteCount),
                &resultByteCount,
                0u);
            if (nativeStatus < 0 || resultByteCount != sizeof(digestByteCount) ||
                digestByteCount != TextureQualificationSha256ByteCount)
            {
                return Fail(
                    TextureQualificationRuntimeReason::HashPropertyQueryFailed,
                    0u,
                    nativeStatus);
            }

            try
            {
                m_Object.resize(objectByteCount);
            }
            catch (...)
            {
                return Fail(
                    TextureQualificationRuntimeReason::HashStateAllocationFailed);
            }
            nativeStatus = BCryptCreateHash(
                m_Provider,
                &m_Hash,
                m_Object.data(),
                static_cast<ULONG>(m_Object.size()),
                nullptr,
                0u,
                0u);
            return nativeStatus >= 0
                ? Success()
                : Fail(
                    TextureQualificationRuntimeReason::HashCreationFailed,
                    0u,
                    nativeStatus);
        }

        TextureQualificationRuntimeStatus Update(
            const void* bytes,
            size_t byteCount)
        {
            if (byteCount == 0u)
                return Success();
            if (!m_Hash || bytes == nullptr ||
                byteCount > (std::numeric_limits<ULONG>::max)())
            {
                return Fail(TextureQualificationRuntimeReason::HashDataFailed);
            }
            const NTSTATUS nativeStatus = BCryptHashData(
                m_Hash,
                const_cast<PUCHAR>(static_cast<const UCHAR*>(bytes)),
                static_cast<ULONG>(byteCount),
                0u);
            return nativeStatus >= 0
                ? Success(byteCount)
                : Fail(
                    TextureQualificationRuntimeReason::HashDataFailed,
                    0u,
                    nativeStatus);
        }

        TextureQualificationRuntimeStatus Finish(
            TextureQualificationSha256& digest)
        {
            digest = {};
            if (!m_Hash)
                return Fail(TextureQualificationRuntimeReason::HashFinishFailed);
            const NTSTATUS nativeStatus = BCryptFinishHash(
                m_Hash,
                digest.data(),
                static_cast<ULONG>(digest.size()),
                0u);
            if (nativeStatus < 0)
            {
                digest = {};
                return Fail(
                    TextureQualificationRuntimeReason::HashFinishFailed,
                    0u,
                    nativeStatus);
            }
            return Success(digest.size());
        }

    private:
        BCRYPT_ALG_HANDLE m_Provider = nullptr;
        BCRYPT_HASH_HANDLE m_Hash = nullptr;
        std::vector<uint8_t> m_Object;
    };

    struct OpenedRegularFile final
    {
        HANDLE handle = INVALID_HANDLE_VALUE;
        BY_HANDLE_FILE_INFORMATION before{};
        uint64_t size = 0u;

        ~OpenedRegularFile()
        {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
        }
    };

    bool SameFileInformation(
        const BY_HANDLE_FILE_INFORMATION& first,
        const BY_HANDLE_FILE_INFORMATION& second)
    {
        return first.dwVolumeSerialNumber == second.dwVolumeSerialNumber &&
            first.nFileIndexHigh == second.nFileIndexHigh &&
            first.nFileIndexLow == second.nFileIndexLow &&
            first.nFileSizeHigh == second.nFileSizeHigh &&
            first.nFileSizeLow == second.nFileSizeLow &&
            first.ftLastWriteTime.dwHighDateTime ==
                second.ftLastWriteTime.dwHighDateTime &&
            first.ftLastWriteTime.dwLowDateTime ==
                second.ftLastWriteTime.dwLowDateTime;
    }

    TextureQualificationRuntimeStatus OpenRegularFile(
        const wchar_t* path,
        OpenedRegularFile& file)
    {
        if (!path || path[0] == L'\0')
            return Fail(TextureQualificationRuntimeReason::PathMissing);
        const size_t pathLength = BoundedWideLength(
            path, MaximumWindowsPathCharacterCount);
        if (pathLength == MaximumWindowsPathCharacterCount)
            return Fail(TextureQualificationRuntimeReason::PathTooLong);

        file.handle = CreateFileW(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
                FILE_FLAG_OPEN_REPARSE_POINT,
            nullptr);
        if (file.handle == INVALID_HANDLE_VALUE)
        {
            return Fail(
                TextureQualificationRuntimeReason::FileOpenFailed,
                GetLastError());
        }
        if (!GetFileInformationByHandle(file.handle, &file.before))
        {
            return Fail(
                TextureQualificationRuntimeReason::FileIsNotRegular,
                GetLastError());
        }
        if ((file.before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u)
            return Fail(TextureQualificationRuntimeReason::FileIsNotRegular);
        if ((file.before.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u)
            return Fail(TextureQualificationRuntimeReason::FileIsReparsePoint);
        file.size = (uint64_t(file.before.nFileSizeHigh) << 32u) |
            uint64_t(file.before.nFileSizeLow);
        return Success(file.size);
    }

    TextureQualificationRuntimeStatus CheckFileUnchanged(
        OpenedRegularFile& file)
    {
        BY_HANDLE_FILE_INFORMATION after{};
        if (!GetFileInformationByHandle(file.handle, &after))
        {
            return Fail(
                TextureQualificationRuntimeReason::FileChangedWhileHashing,
                GetLastError());
        }
        return SameFileInformation(file.before, after)
            ? Success(file.size)
            : Fail(TextureQualificationRuntimeReason::FileChangedWhileHashing);
    }

    TextureQualificationRuntimeStatus StreamFile(
        OpenedRegularFile& file,
        Sha256Stream& stream)
    {
        std::array<uint8_t, FileReadChunkByteCount> buffer{};
        uint64_t total = 0u;
        while (total < file.size)
        {
            const DWORD requested = static_cast<DWORD>(std::min<uint64_t>(
                buffer.size(), file.size - total));
            DWORD received = 0u;
            if (!ReadFile(
                    file.handle,
                    buffer.data(),
                    requested,
                    &received,
                    nullptr) ||
                received == 0u)
            {
                return Fail(
                    TextureQualificationRuntimeReason::FileReadFailed,
                    GetLastError());
            }
            TextureQualificationRuntimeStatus status = stream.Update(
                buffer.data(), received);
            if (!status.Succeeded())
                return status;
            total += received;
        }
        if (total != file.size)
            return Fail(TextureQualificationRuntimeReason::FileReadFailed);
        const TextureQualificationRuntimeStatus unchanged =
            CheckFileUnchanged(file);
        return unchanged.Succeeded() ? Success(total) : unchanged;
    }

    std::array<uint8_t, 4u> LittleEndian32(uint32_t value)
    {
        return {
            uint8_t(value),
            uint8_t(value >> 8u),
            uint8_t(value >> 16u),
            uint8_t(value >> 24u)
        };
    }

    std::array<uint8_t, 8u> LittleEndian64(uint64_t value)
    {
        return {
            uint8_t(value),
            uint8_t(value >> 8u),
            uint8_t(value >> 16u),
            uint8_t(value >> 24u),
            uint8_t(value >> 32u),
            uint8_t(value >> 40u),
            uint8_t(value >> 48u),
            uint8_t(value >> 56u)
        };
    }

    TextureQualificationRuntimeStatus UpdateU32(
        Sha256Stream& stream,
        uint32_t value)
    {
        const auto bytes = LittleEndian32(value);
        return stream.Update(bytes.data(), bytes.size());
    }

    TextureQualificationRuntimeStatus UpdateU64(
        Sha256Stream& stream,
        uint64_t value)
    {
        const auto bytes = LittleEndian64(value);
        return stream.Update(bytes.data(), bytes.size());
    }

    bool IsCanonicalLabel(const char* label, size_t& length)
    {
        length = 0u;
        if (!label)
            return false;
        while (length < MaximumCanonicalLabelByteCount && label[length] != '\0')
            ++length;
        if (length == 0u || length == MaximumCanonicalLabelByteCount ||
            label[0] == '/' || label[length - 1u] == '/')
        {
            return false;
        }

        size_t segmentStart = 0u;
        for (size_t index = 0u; index <= length; ++index)
        {
            if (index < length)
            {
                const unsigned char character =
                    static_cast<unsigned char>(label[index]);
                const bool allowed =
                    (character >= 'a' && character <= 'z') ||
                    (character >= '0' && character <= '9') ||
                    character == '.' || character == '/' ||
                    character == '_' || character == '-';
                if (!allowed)
                    return false;
            }
            if (index == length || label[index] == '/')
            {
                const size_t segmentLength = index - segmentStart;
                if (segmentLength == 0u ||
                    (segmentLength == 1u && label[segmentStart] == '.') ||
                    (segmentLength == 2u && label[segmentStart] == '.' &&
                        label[segmentStart + 1u] == '.'))
                {
                    return false;
                }
                segmentStart = index + 1u;
            }
        }
        return true;
    }

    TextureQualificationRuntimeStatus GetLoadedModulePath(
        void* opaqueModule,
        const wchar_t* expectedBaseName,
        std::wstring& path)
    {
        if (!opaqueModule)
            return Fail(TextureQualificationRuntimeReason::ModuleNotLoaded);
        std::array<wchar_t, MaximumWindowsPathCharacterCount> buffer{};
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(
            static_cast<HMODULE>(opaqueModule),
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0u)
        {
            return Fail(
                TextureQualificationRuntimeReason::ModulePathUnavailable,
                GetLastError());
        }
        if (length >= buffer.size() - 1u)
            return Fail(TextureQualificationRuntimeReason::PathTooLong);
        try
        {
            path.assign(buffer.data(), length);
        }
        catch (...)
        {
            return Fail(TextureQualificationRuntimeReason::PathTooLong);
        }
        const size_t slash = path.find_last_of(L"\\/");
        const wchar_t* baseName = path.c_str() +
            (slash == std::wstring::npos ? 0u : slash + 1u);
        if (_wcsicmp(baseName, expectedBaseName) != 0)
            return Fail(TextureQualificationRuntimeReason::ModuleNameMismatch);
        return Success();
    }

    uint32_t FloatBits(float value)
    {
        uint32_t result = 0u;
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    void AppendU32(std::vector<uint8_t>& bytes, uint32_t value)
    {
        const auto encoded = LittleEndian32(value);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }

    TextureQualificationRuntimeStatus ComputeManifestDigest(
        const char* manifest,
        size_t byteCount,
        TextureQualificationSha256& digest)
    {
        const TextureQualificationReason reason =
            ComputeTextureQualificationSha256(
                reinterpret_cast<const uint8_t*>(manifest),
                byteCount,
                digest);
        if (reason == TextureQualificationReason::None)
            return Success(byteCount);
        digest = {};
        TextureQualificationRuntimeStatus status = Fail(
            TextureQualificationRuntimeReason::CompiledSchemaHashFailed);
        status.certificateReason = reason;
        return status;
    }

    TextureQualificationRuntimeStatus ReadExactCertificateBytes(
        const wchar_t* path,
        std::array<uint8_t,
            TextureQualificationCertificateByteCount>& bytes)
    {
        bytes.fill(0u);
        OpenedRegularFile file;
        TextureQualificationRuntimeStatus status = OpenRegularFile(path, file);
        if (!status.Succeeded())
            return status;
        if (file.size != TextureQualificationCertificateByteCount)
            return Fail(TextureQualificationRuntimeReason::FileSizeInvalid);

        uint64_t total = 0u;
        while (total < bytes.size())
        {
            DWORD received = 0u;
            const DWORD requested = static_cast<DWORD>(bytes.size() - total);
            if (!ReadFile(
                    file.handle,
                    bytes.data() + total,
                    requested,
                    &received,
                    nullptr) ||
                received == 0u)
            {
                bytes.fill(0u);
                return Fail(
                    TextureQualificationRuntimeReason::CertificateReadFailed,
                    GetLastError());
            }
            total += received;
        }
        status = CheckFileUnchanged(file);
        if (!status.Succeeded())
        {
            bytes.fill(0u);
            return status;
        }
        return Success(total);
    }
}

namespace uvsr::adaptive_msaa
{
    const char* GetTextureQualificationRuntimeReasonName(
        TextureQualificationRuntimeReason reason) noexcept
    {
#define UVSR_TQ_RUNTIME_REASON_NAME(value) \
        case TextureQualificationRuntimeReason::value: return #value
        switch (reason)
        {
            UVSR_TQ_RUNTIME_REASON_NAME(None);
            UVSR_TQ_RUNTIME_REASON_NAME(InvalidArgument);
            UVSR_TQ_RUNTIME_REASON_NAME(PathMissing);
            UVSR_TQ_RUNTIME_REASON_NAME(PathTooLong);
            UVSR_TQ_RUNTIME_REASON_NAME(FileOpenFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(FileIsNotRegular);
            UVSR_TQ_RUNTIME_REASON_NAME(FileIsReparsePoint);
            UVSR_TQ_RUNTIME_REASON_NAME(FileSizeInvalid);
            UVSR_TQ_RUNTIME_REASON_NAME(FileReadFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(FileChangedWhileHashing);
            UVSR_TQ_RUNTIME_REASON_NAME(HashProviderUnavailable);
            UVSR_TQ_RUNTIME_REASON_NAME(HashPropertyQueryFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(HashStateAllocationFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(HashCreationFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(HashDataFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(HashFinishFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(AggregateSetEmpty);
            UVSR_TQ_RUNTIME_REASON_NAME(AggregateLabelMissing);
            UVSR_TQ_RUNTIME_REASON_NAME(AggregateLabelInvalid);
            UVSR_TQ_RUNTIME_REASON_NAME(AggregateLabelOrderInvalid);
            UVSR_TQ_RUNTIME_REASON_NAME(AggregateFileEmpty);
            UVSR_TQ_RUNTIME_REASON_NAME(ModuleNotLoaded);
            UVSR_TQ_RUNTIME_REASON_NAME(ModulePathUnavailable);
            UVSR_TQ_RUNTIME_REASON_NAME(ModuleNameMismatch);
            UVSR_TQ_RUNTIME_REASON_NAME(DxgiFactoryUnavailable);
            UVSR_TQ_RUNTIME_REASON_NAME(AdapterUnavailable);
            UVSR_TQ_RUNTIME_REASON_NAME(AdapterDescriptionUnavailable);
            UVSR_TQ_RUNTIME_REASON_NAME(AdapterLuidMismatch);
            UVSR_TQ_RUNTIME_REASON_NAME(AdapterIsSoftware);
            UVSR_TQ_RUNTIME_REASON_NAME(DriverVersionUnavailable);
            UVSR_TQ_RUNTIME_REASON_NAME(D3d12DeviceMissing);
            UVSR_TQ_RUNTIME_REASON_NAME(FeatureQueryFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(RequiredShaderFeatureMissing);
            UVSR_TQ_RUNTIME_REASON_NAME(FormatQueryFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(SamplerMissing);
            UVSR_TQ_RUNTIME_REASON_NAME(SamplerFieldInvalid);
            UVSR_TQ_RUNTIME_REASON_NAME(CompiledSchemaHashFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(CertificateReadFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(CertificateParseFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(CertificateValidationFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(ContentRevisionZero);
            UVSR_TQ_RUNTIME_REASON_NAME(ContentRevisionNotMonotonic);
            UVSR_TQ_RUNTIME_REASON_NAME(LeaseTokenExhausted);
            UVSR_TQ_RUNTIME_REASON_NAME(LeaseTokenMismatch);
            UVSR_TQ_RUNTIME_REASON_NAME(AtomicTemporaryNameExhausted);
            UVSR_TQ_RUNTIME_REASON_NAME(AtomicCreateFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(AtomicWriteFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(AtomicFlushFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(AtomicReplaceFailed);
            UVSR_TQ_RUNTIME_REASON_NAME(Count);
        }
#undef UVSR_TQ_RUNTIME_REASON_NAME
        return "Unknown";
    }

    TextureQualificationRuntimeStatus ComputeTextureQualificationFileSha256(
        const wchar_t* path,
        TextureQualificationSha256& digest) noexcept
    {
        digest = {};
        OpenedRegularFile file;
        TextureQualificationRuntimeStatus status = OpenRegularFile(path, file);
        if (!status.Succeeded())
            return status;
        Sha256Stream stream;
        status = stream.Initialize();
        if (!status.Succeeded())
            return status;
        status = StreamFile(file, stream);
        if (!status.Succeeded())
            return status;
        status = stream.Finish(digest);
        if (!status.Succeeded())
            digest = {};
        else
            status.byteCount = file.size;
        return status;
    }

    TextureQualificationRuntimeStatus
        ComputeTextureQualificationLabeledFileSetSha256(
            const TextureQualificationLabeledFile* files,
            size_t fileCount,
            TextureQualificationSha256& digest) noexcept
    {
        digest = {};
        if (!files || fileCount == 0u)
            return Fail(TextureQualificationRuntimeReason::AggregateSetEmpty);
        if (fileCount > (std::numeric_limits<uint32_t>::max)())
            return Fail(TextureQualificationRuntimeReason::InvalidArgument);

        Sha256Stream stream;
        TextureQualificationRuntimeStatus status = stream.Initialize();
        if (!status.Succeeded())
            return status;
        status = stream.Update(AggregateDomain, sizeof(AggregateDomain) - 1u);
        if (!status.Succeeded())
            return status;
        status = UpdateU32(stream, TextureQualificationRuntimeSchema);
        if (!status.Succeeded())
            return status;
        status = UpdateU32(stream, static_cast<uint32_t>(fileCount));
        if (!status.Succeeded())
            return status;

        uint64_t totalFileBytes = 0u;
        const char* previousLabel = nullptr;
        for (size_t index = 0u; index < fileCount; ++index)
        {
            size_t labelLength = 0u;
            if (!files[index].label)
                return Fail(TextureQualificationRuntimeReason::AggregateLabelMissing);
            if (!IsCanonicalLabel(files[index].label, labelLength))
                return Fail(TextureQualificationRuntimeReason::AggregateLabelInvalid);
            if (previousLabel && std::strcmp(previousLabel, files[index].label) >= 0)
            {
                return Fail(
                    TextureQualificationRuntimeReason::AggregateLabelOrderInvalid);
            }
            previousLabel = files[index].label;

            OpenedRegularFile file;
            status = OpenRegularFile(files[index].path, file);
            if (!status.Succeeded())
                return status;
            if (file.size == 0u)
                return Fail(TextureQualificationRuntimeReason::AggregateFileEmpty);
            if (totalFileBytes >
                (std::numeric_limits<uint64_t>::max)() - file.size)
            {
                return Fail(TextureQualificationRuntimeReason::FileSizeInvalid);
            }
            totalFileBytes += file.size;

            status = UpdateU32(stream, static_cast<uint32_t>(labelLength));
            if (!status.Succeeded())
                return status;
            status = stream.Update(files[index].label, labelLength);
            if (!status.Succeeded())
                return status;
            status = UpdateU64(stream, file.size);
            if (!status.Succeeded())
                return status;
            status = StreamFile(file, stream);
            if (!status.Succeeded())
                return status;
        }
        status = stream.Finish(digest);
        if (status.Succeeded())
            status.byteCount = totalFileBytes;
        else
            digest = {};
        return status;
    }

    TextureQualificationRuntimeStatus PinTextureQualificationDxilModule() noexcept
    {
        if (PinnedDxilModule)
            return Success();

        std::array<wchar_t, MaximumWindowsPathCharacterCount> executable{};
        SetLastError(ERROR_SUCCESS);
        const DWORD executableLength = GetModuleFileNameW(
            nullptr,
            executable.data(),
            static_cast<DWORD>(executable.size()));
        if (executableLength == 0u)
        {
            return Fail(
                TextureQualificationRuntimeReason::ModulePathUnavailable,
                GetLastError());
        }
        if (executableLength >= executable.size() - 1u)
            return Fail(TextureQualificationRuntimeReason::PathTooLong);

        std::wstring stagedPath(executable.data(), executableLength);
        const size_t slash = stagedPath.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return Fail(TextureQualificationRuntimeReason::ModulePathUnavailable);
        stagedPath.resize(slash + 1u);
        stagedPath += L"dxil.dll";

        HMODULE module = GetModuleHandleW(L"dxil.dll");
        if (module)
        {
            std::wstring loadedPath;
            TextureQualificationRuntimeStatus status = GetLoadedModulePath(
                module, L"dxil.dll", loadedPath);
            if (!status.Succeeded())
                return status;
            if (CompareStringOrdinal(
                    loadedPath.c_str(), -1,
                    stagedPath.c_str(), -1,
                    TRUE) != CSTR_EQUAL)
            {
                return Fail(TextureQualificationRuntimeReason::ModuleNameMismatch);
            }
        }
        else
        {
            SetLastError(ERROR_SUCCESS);
            module = LoadLibraryExW(
                stagedPath.c_str(),
                nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                    LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module)
            {
                return Fail(
                    TextureQualificationRuntimeReason::ModuleNotLoaded,
                    GetLastError());
            }
        }

        PinnedDxilModule = module;
        return Success();
    }

    TextureQualificationRuntimeStatus ResolveTextureQualificationLoadedD3dModules(
        TextureQualificationLoadedD3dModules& modules) noexcept
    {
        modules = {};
        modules.dxgi = GetModuleHandleW(L"dxgi.dll");
        modules.d3d12 = GetModuleHandleW(L"d3d12.dll");
        modules.d3d12Core = GetModuleHandleW(L"D3D12Core.dll");
        modules.dxil = GetModuleHandleW(L"dxil.dll");
        return modules.dxgi && modules.d3d12 && modules.d3d12Core && modules.dxil
            ? Success()
            : Fail(TextureQualificationRuntimeReason::ModuleNotLoaded);
    }

    TextureQualificationRuntimeStatus
        AcquireTextureQualificationD3dRuntimeIdentityFromFiles(
            const TextureQualificationD3dRuntimeFileInput& input,
            TextureQualificationD3dRuntimeIdentity& identity) noexcept
    {
        identity = {};
        TextureQualificationD3dRuntimeIdentity candidate;
        TextureQualificationRuntimeStatus status =
            ComputeTextureQualificationFileSha256(
                input.dxgiPath, candidate.dxgiModuleSha256);
        if (!status.Succeeded())
            return status;
        if (status.byteCount == 0u)
            return Fail(TextureQualificationRuntimeReason::FileSizeInvalid);
        status = ComputeTextureQualificationFileSha256(
            input.d3d12Path, candidate.d3d12ModuleSha256);
        if (!status.Succeeded())
            return status;
        if (status.byteCount == 0u)
            return Fail(TextureQualificationRuntimeReason::FileSizeInvalid);
        status = ComputeTextureQualificationFileSha256(
            input.d3d12CorePath, candidate.d3d12CoreModuleSha256);
        if (!status.Succeeded())
            return status;
        if (status.byteCount == 0u)
            return Fail(TextureQualificationRuntimeReason::FileSizeInvalid);
        status = ComputeTextureQualificationFileSha256(
            input.dxilPath, candidate.dxilModuleSha256);
        if (!status.Succeeded())
            return status;
        if (status.byteCount == 0u)
            return Fail(TextureQualificationRuntimeReason::FileSizeInvalid);
        identity = candidate;
        return Success();
    }

    TextureQualificationRuntimeStatus AcquireTextureQualificationD3dRuntimeIdentity(
        const TextureQualificationLoadedD3dModules& modules,
        TextureQualificationD3dRuntimeIdentity& identity) noexcept
    {
        identity = {};
        std::wstring dxgiPath;
        std::wstring d3d12Path;
        std::wstring d3d12CorePath;
        std::wstring dxilPath;
        TextureQualificationRuntimeStatus status = GetLoadedModulePath(
            modules.dxgi, L"dxgi.dll", dxgiPath);
        if (!status.Succeeded())
            return status;
        status = GetLoadedModulePath(modules.d3d12, L"d3d12.dll", d3d12Path);
        if (!status.Succeeded())
            return status;
        status = GetLoadedModulePath(
            modules.d3d12Core, L"D3D12Core.dll", d3d12CorePath);
        if (!status.Succeeded())
            return status;
        status = GetLoadedModulePath(modules.dxil, L"dxil.dll", dxilPath);
        if (!status.Succeeded())
            return status;
        const TextureQualificationD3dRuntimeFileInput input = {
            dxgiPath.c_str(),
            d3d12Path.c_str(),
            d3d12CorePath.c_str(),
            dxilPath.c_str()
        };
        return AcquireTextureQualificationD3dRuntimeIdentityFromFiles(
            input, identity);
    }

    TextureQualificationRuntimeStatus AcquireTextureQualificationAdapterIdentity(
        ID3D12Device* device,
        TextureQualificationAdapterIdentity& identity) noexcept
    {
        identity = {};
        if (!device)
            return Fail(TextureQualificationRuntimeReason::D3d12DeviceMissing);
        const LUID deviceLuid = device->GetAdapterLuid();

        ComPtr<IDXGIFactory4> factory;
        HRESULT nativeResult = CreateDXGIFactory2(
            0u, IID_PPV_ARGS(&factory));
        if (FAILED(nativeResult) || !factory)
        {
            return Fail(
                TextureQualificationRuntimeReason::DxgiFactoryUnavailable,
                0u,
                nativeResult);
        }
        ComPtr<IDXGIAdapter1> adapter;
        nativeResult = factory->EnumAdapterByLuid(
            deviceLuid, IID_PPV_ARGS(&adapter));
        if (FAILED(nativeResult) || !adapter)
        {
            return Fail(
                TextureQualificationRuntimeReason::AdapterUnavailable,
                0u,
                nativeResult);
        }
        DXGI_ADAPTER_DESC1 description{};
        nativeResult = adapter->GetDesc1(&description);
        if (FAILED(nativeResult))
        {
            return Fail(
                TextureQualificationRuntimeReason::AdapterDescriptionUnavailable,
                0u,
                nativeResult);
        }
        if (description.AdapterLuid.LowPart != deviceLuid.LowPart ||
            description.AdapterLuid.HighPart != deviceLuid.HighPart)
        {
            return Fail(TextureQualificationRuntimeReason::AdapterLuidMismatch);
        }
        if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u)
            return Fail(TextureQualificationRuntimeReason::AdapterIsSoftware);

        LARGE_INTEGER driverVersion{};
        nativeResult = adapter->CheckInterfaceSupport(
            __uuidof(IDXGIDevice), &driverVersion);
        if (FAILED(nativeResult))
        {
            return Fail(
                TextureQualificationRuntimeReason::DriverVersionUnavailable,
                0u,
                nativeResult);
        }

        TextureQualificationAdapterIdentity candidate;
        candidate.luidLowPart = deviceLuid.LowPart;
        candidate.luidHighPartBits = static_cast<uint32_t>(deviceLuid.HighPart);
        candidate.vendorId = description.VendorId;
        candidate.deviceId = description.DeviceId;
        candidate.subsystemId = description.SubSysId;
        candidate.revision = description.Revision;
        candidate.driverVersion =
            (uint64_t(uint32_t(driverVersion.HighPart)) << 32u) |
            uint64_t(driverVersion.LowPart);
        identity = candidate;
        return Success();
    }

    TextureQualificationRuntimeStatus CaptureTextureQualificationSamplerIdentity(
        const nvrhi::SamplerDesc& description,
        TextureQualificationSamplerIdentity& identity) noexcept
    {
        identity = {};
        const std::array<float, 6u> finiteValues = {
            description.borderColor.r,
            description.borderColor.g,
            description.borderColor.b,
            description.borderColor.a,
            description.maxAnisotropy,
            description.mipBias
        };
        if (!std::all_of(
                finiteValues.begin(), finiteValues.end(),
                [](float value) { return std::isfinite(value); }) ||
            !(description.maxAnisotropy >= 1.f) ||
            description.maxAnisotropy > 16.f ||
            static_cast<uint32_t>(description.addressU) > 4u ||
            static_cast<uint32_t>(description.addressV) > 4u ||
            static_cast<uint32_t>(description.addressW) > 4u ||
            static_cast<uint32_t>(description.reductionType) > 3u)
        {
            return Fail(TextureQualificationRuntimeReason::SamplerFieldInvalid);
        }

        TextureQualificationSamplerIdentity candidate;
        candidate.schema = TextureQualificationSamplerSchema;
        candidate.borderColorBits = {
            FloatBits(description.borderColor.r),
            FloatBits(description.borderColor.g),
            FloatBits(description.borderColor.b),
            FloatBits(description.borderColor.a)
        };
        candidate.maxAnisotropyBits = FloatBits(description.maxAnisotropy);
        candidate.mipBiasBits = FloatBits(description.mipBias);
        candidate.minFilter = description.minFilter ? 1u : 0u;
        candidate.magFilter = description.magFilter ? 1u : 0u;
        candidate.mipFilter = description.mipFilter ? 1u : 0u;
        candidate.addressU = static_cast<uint32_t>(description.addressU);
        candidate.addressV = static_cast<uint32_t>(description.addressV);
        candidate.addressW = static_cast<uint32_t>(description.addressW);
        candidate.reductionType =
            static_cast<uint32_t>(description.reductionType);
        identity = candidate;
        return Success();
    }

    TextureQualificationRuntimeStatus CaptureTextureQualificationSamplerIdentity(
        nvrhi::ISampler* sampler,
        TextureQualificationSamplerIdentity& identity) noexcept
    {
        if (!sampler)
        {
            identity = {};
            return Fail(TextureQualificationRuntimeReason::SamplerMissing);
        }
        return CaptureTextureQualificationSamplerIdentity(
            sampler->getDesc(), identity);
    }

    uint32_t GetTextureQualificationFeatureFormatDxgiFormat(
        size_t index) noexcept
    {
        return index < FeatureFormats.size()
            ? static_cast<uint32_t>(FeatureFormats[index])
            : 0u;
    }

    TextureQualificationRuntimeStatus
        AcquireTextureQualificationFeatureFormatSnapshot(
            ID3D12Device* device,
            TextureQualificationFeatureFormatSnapshot& snapshot) noexcept
    {
        snapshot = {};
        if (!device)
            return Fail(TextureQualificationRuntimeReason::D3d12DeviceMissing);

        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        HRESULT nativeResult = device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
        if (FAILED(nativeResult))
        {
            return Fail(
                TextureQualificationRuntimeReason::FeatureQueryFailed,
                0u,
                nativeResult);
        }
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
        nativeResult = device->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));
        if (FAILED(nativeResult))
        {
            return Fail(
                TextureQualificationRuntimeReason::FeatureQueryFailed,
                0u,
                nativeResult);
        }
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel = {
            D3D_SHADER_MODEL_6_5
        };
        nativeResult = device->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
        if (FAILED(nativeResult))
        {
            return Fail(
                TextureQualificationRuntimeReason::FeatureQueryFailed,
                0u,
                nativeResult);
        }
        if (!options1.WaveOps || !options1.Int64ShaderOps ||
            shaderModel.HighestShaderModel < D3D_SHADER_MODEL_6_5)
        {
            return Fail(
                TextureQualificationRuntimeReason::RequiredShaderFeatureMissing);
        }

        TextureQualificationFeatureFormatSnapshot candidate;
        candidate.schema = TextureQualificationFeatureFormatSchema;
        candidate.highestShaderModel =
            static_cast<uint32_t>(shaderModel.HighestShaderModel);
        candidate.resourceBindingTier =
            static_cast<uint32_t>(options.ResourceBindingTier);
        candidate.waveOps = options1.WaveOps ? 1u : 0u;
        candidate.waveLaneCountMin = options1.WaveLaneCountMin;
        candidate.waveLaneCountMax = options1.WaveLaneCountMax;
        candidate.int64ShaderOps = options1.Int64ShaderOps ? 1u : 0u;

        for (size_t index = 0u; index < FeatureFormats.size(); ++index)
        {
            TextureQualificationFeatureFormatRecord& record =
                candidate.formats[index];
            record.dxgiFormat = static_cast<uint32_t>(FeatureFormats[index]);
            D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{};
            formatSupport.Format = FeatureFormats[index];
            nativeResult = device->CheckFeatureSupport(
                D3D12_FEATURE_FORMAT_SUPPORT,
                &formatSupport,
                sizeof(formatSupport));
            if (FAILED(nativeResult))
            {
                return Fail(
                    TextureQualificationRuntimeReason::FormatQueryFailed,
                    0u,
                    nativeResult);
            }
            record.formatQuerySucceeded = 1u;
            record.support1 = static_cast<uint32_t>(formatSupport.Support1);
            record.support2 = static_cast<uint32_t>(formatSupport.Support2);

            for (size_t sampleIndex = 0u;
                sampleIndex < TextureQualificationSampleCounts.size();
                ++sampleIndex)
            {
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality{};
                quality.Format = FeatureFormats[index];
                quality.SampleCount =
                    TextureQualificationSampleCounts[sampleIndex];
                quality.Flags =
                    D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
                nativeResult = device->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                    &quality,
                    sizeof(quality));
                if (FAILED(nativeResult))
                {
                    return Fail(
                        TextureQualificationRuntimeReason::FormatQueryFailed,
                        0u,
                        nativeResult);
                }
                record.qualityQuerySucceeded[sampleIndex] = 1u;
                record.qualityLevelCount[sampleIndex] =
                    quality.NumQualityLevels;
            }
        }
        snapshot = candidate;
        return Success();
    }

    TextureQualificationRuntimeStatus
        ComputeTextureQualificationFeatureFormatDigest(
            const TextureQualificationFeatureFormatSnapshot& snapshot,
            TextureQualificationSha256& digest) noexcept
    {
        digest = {};
        if (snapshot.schema != TextureQualificationFeatureFormatSchema ||
            snapshot.waveOps != 1u || snapshot.int64ShaderOps != 1u ||
            snapshot.highestShaderModel <
                static_cast<uint32_t>(D3D_SHADER_MODEL_6_5) ||
            snapshot.resourceBindingTier == 0u ||
            snapshot.waveLaneCountMin == 0u ||
            snapshot.waveLaneCountMax < snapshot.waveLaneCountMin)
        {
            return Fail(TextureQualificationRuntimeReason::FeatureQueryFailed);
        }

        std::vector<uint8_t> canonical;
        try
        {
            canonical.reserve(1024u);
            canonical.insert(
                canonical.end(),
                FeatureFormatDomain,
                FeatureFormatDomain + sizeof(FeatureFormatDomain) - 1u);
            AppendU32(canonical, snapshot.schema);
            AppendU32(canonical, snapshot.highestShaderModel);
            AppendU32(canonical, snapshot.resourceBindingTier);
            AppendU32(canonical, snapshot.waveOps);
            AppendU32(canonical, snapshot.waveLaneCountMin);
            AppendU32(canonical, snapshot.waveLaneCountMax);
            AppendU32(canonical, snapshot.int64ShaderOps);
            AppendU32(canonical, static_cast<uint32_t>(snapshot.formats.size()));
            for (size_t index = 0u; index < snapshot.formats.size(); ++index)
            {
                const TextureQualificationFeatureFormatRecord& record =
                    snapshot.formats[index];
                if (record.dxgiFormat !=
                        static_cast<uint32_t>(FeatureFormats[index]) ||
                    record.formatQuerySucceeded != 1u)
                {
                    return Fail(
                        TextureQualificationRuntimeReason::FormatQueryFailed);
                }
                AppendU32(canonical, record.dxgiFormat);
                AppendU32(canonical, record.formatQuerySucceeded);
                AppendU32(canonical, record.support1);
                AppendU32(canonical, record.support2);
                for (size_t sampleIndex = 0u;
                    sampleIndex < TextureQualificationSampleCounts.size();
                    ++sampleIndex)
                {
                    if (record.qualityQuerySucceeded[sampleIndex] != 1u)
                    {
                        return Fail(
                            TextureQualificationRuntimeReason::FormatQueryFailed);
                    }
                    AppendU32(
                        canonical,
                        TextureQualificationSampleCounts[sampleIndex]);
                    AppendU32(
                        canonical,
                        record.qualityQuerySucceeded[sampleIndex]);
                    AppendU32(
                        canonical,
                        record.qualityLevelCount[sampleIndex]);
                }
            }
        }
        catch (...)
        {
            return Fail(
                TextureQualificationRuntimeReason::HashStateAllocationFailed);
        }

        const TextureQualificationReason reason =
            ComputeTextureQualificationSha256(
                canonical.data(), canonical.size(), digest);
        if (reason != TextureQualificationReason::None)
        {
            digest = {};
            TextureQualificationRuntimeStatus status = Fail(
                TextureQualificationRuntimeReason::HashFinishFailed);
            status.certificateReason = reason;
            return status;
        }
        return Success(canonical.size());
    }

    TextureQualificationRuntimeStatus GetTextureQualificationCompiledSchemaIdentity(
        TextureQualificationSha256& semanticFormatSchemaSha256,
        TextureQualificationMatrixIdentity& matrix) noexcept
    {
        semanticFormatSchemaSha256 = {};
        matrix = {};
        TextureQualificationRuntimeStatus status = ComputeManifestDigest(
            SemanticFormatManifest,
            sizeof(SemanticFormatManifest) - 1u,
            semanticFormatSchemaSha256);
        if (!status.Succeeded())
            return status;
        TextureQualificationSha256 matrixDigest;
        status = ComputeManifestDigest(
            MatrixManifest,
            sizeof(MatrixManifest) - 1u,
            matrixDigest);
        if (!status.Succeeded())
        {
            semanticFormatSchemaSha256 = {};
            return status;
        }
        matrix.schema = TextureQualificationMatrixRuntimeSchema;
        matrix.schemaSha256 = matrixDigest;
        matrix.requiredQualificationMasks =
            TextureQualificationRequiredEvidenceMasks;
        return Success();
    }

    TextureQualificationRuntimeStatus AcquireTextureQualificationLiveIdentity(
        const TextureQualificationLiveIdentityInput& input,
        TextureQualificationLiveIdentity& identity) noexcept
    {
        identity = {};
        if (!input.d3d12Device || !input.materialSampler)
            return Fail(TextureQualificationRuntimeReason::InvalidArgument);

        TextureQualificationLiveIdentity candidate;
        TextureQualificationRuntimeStatus status =
            AcquireTextureQualificationAdapterIdentity(
                input.d3d12Device, candidate.adapter);
        if (!status.Succeeded())
            return status;
        status = AcquireTextureQualificationD3dRuntimeIdentity(
            input.loadedModules, candidate.runtime);
        if (!status.Succeeded())
            return status;
        status = ComputeTextureQualificationFileSha256(
            input.artifacts.rendererExecutablePath,
            candidate.artifacts.rendererExecutableSha256);
        if (!status.Succeeded())
            return status;
        if (status.byteCount == 0u)
            return Fail(TextureQualificationRuntimeReason::FileSizeInvalid);
        status = ComputeTextureQualificationFileSha256(
            input.artifacts.rendererBuildManifestPath,
            candidate.artifacts.rendererBuildManifestSha256);
        if (!status.Succeeded())
            return status;
        if (status.byteCount == 0u)
            return Fail(TextureQualificationRuntimeReason::FileSizeInvalid);
        status = ComputeTextureQualificationFileSha256(
            input.artifacts.qualificationTestExecutablePath,
            candidate.artifacts.qualificationTestExecutableSha256);
        if (!status.Succeeded())
            return status;
        if (status.byteCount == 0u)
            return Fail(TextureQualificationRuntimeReason::FileSizeInvalid);
        status = ComputeTextureQualificationLabeledFileSetSha256(
            input.artifacts.matrixShaderFiles,
            input.artifacts.matrixShaderFileCount,
            candidate.artifacts.qualificationTestDxilSetSha256);
        if (!status.Succeeded())
            return status;

        TextureQualificationFeatureFormatSnapshot snapshot;
        status = AcquireTextureQualificationFeatureFormatSnapshot(
            input.d3d12Device, snapshot);
        if (!status.Succeeded())
            return status;
        status = ComputeTextureQualificationFeatureFormatDigest(
            snapshot, candidate.featureFormatSetSha256);
        if (!status.Succeeded())
            return status;
        status = CaptureTextureQualificationSamplerIdentity(
            input.materialSampler, candidate.sampler);
        if (!status.Succeeded())
            return status;
        status = GetTextureQualificationCompiledSchemaIdentity(
            candidate.semanticFormatSchemaSha256,
            candidate.matrix);
        if (!status.Succeeded())
            return status;

        identity = candidate;
        return Success();
    }

    TextureQualificationCertificateReadResult ReadTextureQualificationCertificateFile(
        const wchar_t* path) noexcept
    {
        TextureQualificationCertificateReadResult result;
        std::array<uint8_t, TextureQualificationCertificateByteCount> bytes{};
        result.status = ReadExactCertificateBytes(path, bytes);
        if (!result.status.Succeeded())
            return result;

        const TextureQualificationReason hashReason =
            ComputeTextureQualificationSha256(
                bytes.data(), bytes.size(), result.fileSha256);
        if (hashReason != TextureQualificationReason::None)
        {
            result.fileSha256 = {};
            result.status = Fail(
                TextureQualificationRuntimeReason::HashFinishFailed);
            result.status.certificateReason = hashReason;
            return result;
        }
        const TextureQualificationStatus parse =
            ParseTextureQualificationCertificate(
                bytes.data(), bytes.size(), result.certificate);
        if (!parse.Succeeded())
        {
            result.certificate = {};
            result.status = Fail(
                TextureQualificationRuntimeReason::CertificateParseFailed);
            result.status.certificateReason = parse.reason;
            return result;
        }
        result.status = Success(bytes.size());
        return result;
    }

    TextureQualificationRuntimeStatus
        WriteTextureQualificationCertificateFileAtomically(
            const wchar_t* path,
            const TextureQualificationCertificate& certificate) noexcept
    {
        if (!path || path[0] == L'\0')
            return Fail(TextureQualificationRuntimeReason::PathMissing);
        const size_t pathLength = BoundedWideLength(
            path, MaximumWindowsPathCharacterCount);
        if (pathLength == MaximumWindowsPathCharacterCount)
            return Fail(TextureQualificationRuntimeReason::PathTooLong);

        TextureQualificationValidationRequest selfCheckRequest;
        selfCheckRequest.requestedSampleCount =
            TextureQualificationSampleCounts.front();
        selfCheckRequest.resolvedSampleCount =
            TextureQualificationSampleCounts.front();
        const TextureQualificationValidationResult selfCheck =
            ValidateTextureQualificationCertificate(
                &certificate,
                certificate.identity,
                selfCheckRequest);
        if (!selfCheck.Succeeded())
        {
            TextureQualificationRuntimeStatus status = Fail(
                TextureQualificationRuntimeReason::CertificateValidationFailed);
            status.certificateReason = selfCheck.reason;
            return status;
        }

        std::array<uint8_t, TextureQualificationCertificateByteCount> bytes{};
        const TextureQualificationStatus serialized =
            SerializeTextureQualificationCertificate(
                certificate, bytes.data(), bytes.size());
        if (!serialized.Succeeded())
        {
            TextureQualificationRuntimeStatus status = Fail(
                TextureQualificationRuntimeReason::CertificateValidationFailed);
            status.certificateReason = serialized.reason;
            return status;
        }

        const uint64_t counter = AllocateTemporaryCounter();
        if (counter == 0u)
        {
            return Fail(
                TextureQualificationRuntimeReason::AtomicTemporaryNameExhausted);
        }
        std::wstring temporaryPath;
        try
        {
            temporaryPath = path;
            temporaryPath += L".tmp.";
            temporaryPath += std::to_wstring(GetCurrentProcessId());
            temporaryPath += L".";
            temporaryPath += std::to_wstring(counter);
        }
        catch (...)
        {
            return Fail(TextureQualificationRuntimeReason::PathTooLong);
        }
        if (temporaryPath.size() >= MaximumWindowsPathCharacterCount)
            return Fail(TextureQualificationRuntimeReason::PathTooLong);

        HANDLE file = CreateFileW(
            temporaryPath.c_str(),
            GENERIC_WRITE,
            0u,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return Fail(
                TextureQualificationRuntimeReason::AtomicCreateFailed,
                GetLastError());
        }

        TextureQualificationRuntimeStatus status = Success();
        size_t total = 0u;
        while (total < bytes.size())
        {
            DWORD written = 0u;
            if (!WriteFile(
                    file,
                    bytes.data() + total,
                    static_cast<DWORD>(bytes.size() - total),
                    &written,
                    nullptr) ||
                written == 0u)
            {
                status = Fail(
                    TextureQualificationRuntimeReason::AtomicWriteFailed,
                    GetLastError());
                break;
            }
            total += written;
        }
        if (status.Succeeded() && !FlushFileBuffers(file))
        {
            status = Fail(
                TextureQualificationRuntimeReason::AtomicFlushFailed,
                GetLastError());
        }
        if (!CloseHandle(file) && status.Succeeded())
        {
            status = Fail(
                TextureQualificationRuntimeReason::AtomicFlushFailed,
                GetLastError());
        }
        if (status.Succeeded() && !MoveFileExW(
                temporaryPath.c_str(),
                path,
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            status = Fail(
                TextureQualificationRuntimeReason::AtomicReplaceFailed,
                GetLastError());
        }
        if (!status.Succeeded())
            DeleteFileW(temporaryPath.c_str());
        else
            status.byteCount = bytes.size();
        return status;
    }

    TextureQualificationCertificateAuthority::
        TextureQualificationCertificateAuthority() noexcept
        : m_UsesProcessState(true)
    {
    }

    TextureQualificationCertificateAuthority::
        TextureQualificationCertificateAuthority(
            TextureQualificationAuthoritySeed seed) noexcept
        : m_NextToken(seed.nextToken)
        , m_LastContentRevision(seed.lastContentRevision)
        , m_TokenExhausted(seed.nextToken == 0u)
    {
    }

    TextureQualificationLeaseResult
        TextureQualificationCertificateAuthority::Acquire(
            const uint8_t* certificateBytes,
            size_t certificateByteCount,
            const TextureQualificationLiveIdentity& liveIdentity,
            const TextureQualificationValidationRequest& request,
            uint64_t contentRevision) noexcept
    {
        TextureQualificationLeaseResult result;
        if (contentRevision == 0u)
        {
            result.status = Fail(
                TextureQualificationRuntimeReason::ContentRevisionZero);
            return result;
        }
        if (contentRevision <= LastContentRevision())
        {
            result.status = Fail(
                TextureQualificationRuntimeReason::ContentRevisionNotMonotonic);
            return result;
        }
        TextureQualificationCertificate parsed;
        const TextureQualificationStatus parse =
            ParseTextureQualificationCertificate(
                certificateBytes, certificateByteCount, parsed);
        if (!parse.Succeeded())
        {
            result.status = Fail(
                TextureQualificationRuntimeReason::CertificateParseFailed);
            result.status.certificateReason = parse.reason;
            return result;
        }
        return Acquire(parsed, liveIdentity, request, contentRevision);
    }

    TextureQualificationLeaseResult
        TextureQualificationCertificateAuthority::Acquire(
            const TextureQualificationCertificate& certificate,
            const TextureQualificationLiveIdentity& liveIdentity,
            const TextureQualificationValidationRequest& request,
            uint64_t contentRevision) noexcept
    {
        TextureQualificationLeaseResult result;
        if (contentRevision == 0u)
        {
            result.status = Fail(
                TextureQualificationRuntimeReason::ContentRevisionZero);
            return result;
        }
        if (contentRevision <= LastContentRevision())
        {
            result.status = Fail(
                TextureQualificationRuntimeReason::ContentRevisionNotMonotonic);
            return result;
        }
        const TextureQualificationValidationResult validation =
            ValidateTextureQualificationCertificate(
                &certificate, liveIdentity, request);
        if (!validation.Succeeded())
        {
            result.status = Fail(
                TextureQualificationRuntimeReason::CertificateValidationFailed);
            result.status.certificateReason = validation.reason;
            return result;
        }
        return CommitValidated(
            certificate,
            liveIdentity,
            request,
            validation,
            contentRevision);
    }

    TextureQualificationLeaseResult
        TextureQualificationCertificateAuthority::CommitValidated(
            const TextureQualificationCertificate& certificate,
            const TextureQualificationLiveIdentity& liveIdentity,
            const TextureQualificationValidationRequest& request,
            const TextureQualificationValidationResult& validation,
            uint64_t contentRevision) noexcept
    {
        TextureQualificationLeaseResult result;
        if (!m_UsesProcessState &&
            (m_TokenExhausted || m_NextToken == 0u))
        {
            result.status = Fail(
                TextureQualificationRuntimeReason::LeaseTokenExhausted);
            return result;
        }

        TextureQualificationCertificateLease candidate;
        candidate.contentRevision = contentRevision;
        candidate.certificate = certificate;
        candidate.liveIdentity = liveIdentity;
        candidate.request = request;
        candidate.validation = validation;
        if (candidate.contentRevision == 0u || !candidate.validation.Succeeded())
        {
            result.status = Fail(
                TextureQualificationRuntimeReason::CertificateValidationFailed);
            return result;
        }

        uint64_t token = 0u;
        if (m_UsesProcessState)
        {
            ProcessAuthorityState& process = GetProcessAuthorityState();
            AcquireSRWLockExclusive(&process.lock);
            if (contentRevision <= process.lastContentRevision)
            {
                ReleaseSRWLockExclusive(&process.lock);
                result.status = Fail(
                    TextureQualificationRuntimeReason::
                        ContentRevisionNotMonotonic);
                return result;
            }
            if (process.tokenExhausted || process.nextToken == 0u)
            {
                ReleaseSRWLockExclusive(&process.lock);
                result.status = Fail(
                    TextureQualificationRuntimeReason::LeaseTokenExhausted);
                return result;
            }
            token = process.nextToken;
            if (process.nextToken ==
                (std::numeric_limits<uint64_t>::max)())
            {
                process.nextToken = 0u;
                process.tokenExhausted = true;
            }
            else
            {
                ++process.nextToken;
            }
            process.lastContentRevision = contentRevision;
            ReleaseSRWLockExclusive(&process.lock);
        }
        else
        {
            if (m_TokenExhausted || m_NextToken == 0u)
            {
                result.status = Fail(
                    TextureQualificationRuntimeReason::LeaseTokenExhausted);
                return result;
            }
            token = m_NextToken;
            if (m_NextToken == (std::numeric_limits<uint64_t>::max)())
            {
                m_NextToken = 0u;
                m_TokenExhausted = true;
            }
            else
            {
                ++m_NextToken;
            }
        }
        candidate.token = token;
        m_LastContentRevision = contentRevision;
        m_ActiveLease = candidate;
        m_HasActiveLease = true;
        result.status = Success();
        result.token = candidate.token;
        result.contentRevision = contentRevision;
        return result;
    }

    TextureQualificationRuntimeStatus
        TextureQualificationCertificateAuthority::Revoke(
            uint64_t token) noexcept
    {
        if (!m_HasActiveLease || token == 0u ||
            token != m_ActiveLease.token)
        {
            return Fail(TextureQualificationRuntimeReason::LeaseTokenMismatch);
        }
        m_ActiveLease = {};
        m_HasActiveLease = false;
        return Success();
    }

    const TextureQualificationCertificateLease*
        TextureQualificationCertificateAuthority::ActiveLease() const noexcept
    {
        return m_HasActiveLease ? &m_ActiveLease : nullptr;
    }

    TextureQualificationValidationResult
        TextureQualificationCertificateAuthority::ValidateActive(
            const TextureQualificationValidationRequest& request) const
            noexcept
    {
        if (!m_HasActiveLease)
        {
            TextureQualificationValidationResult result;
            result.reason = TextureQualificationReason::CertificateMissing;
            return result;
        }
        return ValidateTextureQualificationCertificate(
            &m_ActiveLease.certificate,
            m_ActiveLease.liveIdentity,
            request);
    }

    bool TextureQualificationCertificateAuthority::IsActive(
        uint64_t token,
        uint64_t contentRevision) const noexcept
    {
        return m_HasActiveLease && token != 0u && contentRevision != 0u &&
            m_ActiveLease.token == token &&
            m_ActiveLease.contentRevision == contentRevision;
    }

    bool TextureQualificationCertificateAuthority::TokenExhausted() const noexcept
    {
        if (m_UsesProcessState)
        {
            ProcessAuthorityState& process = GetProcessAuthorityState();
            AcquireSRWLockShared(&process.lock);
            const bool exhausted = process.tokenExhausted;
            ReleaseSRWLockShared(&process.lock);
            return exhausted;
        }
        return m_TokenExhausted;
    }

    uint64_t TextureQualificationCertificateAuthority::
        LastContentRevision() const noexcept
    {
        if (m_UsesProcessState)
        {
            ProcessAuthorityState& process = GetProcessAuthorityState();
            AcquireSRWLockShared(&process.lock);
            const uint64_t revision = process.lastContentRevision;
            ReleaseSRWLockShared(&process.lock);
            return revision;
        }
        return m_LastContentRevision;
    }
}
