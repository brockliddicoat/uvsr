#include "sha256.h"

#include <Windows.h>
#include <bcrypt.h>
#include <limits.h>
#include <stdlib.h>

namespace uvsr
{
    namespace
    {
        bool Fail(Sha256Result& result, Sha256Stage stage, uint32_t code) noexcept
        {
            result = { stage, code };
            return false;
        }

        struct FaultState
        {
            Sha256Fault fault;
            size_t occurrences = 0u;

            bool Check(Sha256Stage stage, Sha256Result& result) noexcept
            {
                if (stage == fault.stage && ++occurrences == fault.occurrence)
                    return Fail(result, stage, ERROR_OPERATION_ABORTED);
                return true;
            }
        };

        class HashState
        {
        public:
            ~HashState() noexcept
            {
                if (m_Hash)
                    BCryptDestroyHash(m_Hash);
                if (m_Algorithm)
                    BCryptCloseAlgorithmProvider(m_Algorithm, 0u);
            }

            bool Initialize(FaultState& fault, Sha256Result& result) noexcept
            {
                if (!fault.Check(Sha256Stage::OpenProvider, result))
                    return false;
                NTSTATUS status = BCryptOpenAlgorithmProvider(
                    &m_Algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0u);
                if (status < 0)
                    return Fail(result, Sha256Stage::OpenProvider, static_cast<uint32_t>(status));
                if (!fault.Check(Sha256Stage::CreateHash, result))
                    return false;
                // CNG owns the object buffer until BCryptDestroyHash.
                status = BCryptCreateHash(m_Algorithm, &m_Hash, nullptr, 0u, nullptr, 0u, 0u);
                return status >= 0 || Fail(result, Sha256Stage::CreateHash, static_cast<uint32_t>(status));
            }

            bool Update(const void* input, size_t size,
                FaultState& fault, Sha256Result& result) noexcept
            {
                const auto* data = static_cast<const unsigned char*>(input);
                while (size)
                {
                    const ULONG chunk = size > ULONG_MAX ? ULONG_MAX : static_cast<ULONG>(size);
                    if (!fault.Check(Sha256Stage::HashData, result))
                        return false;
                    const NTSTATUS status = BCryptHashData(m_Hash,
                        const_cast<unsigned char*>(data), chunk, 0u);
                    if (status < 0)
                        return Fail(result, Sha256Stage::HashData, static_cast<uint32_t>(status));
                    data += chunk;
                    size -= chunk;
                }
                return true;
            }

            bool Finish(Sha256Digest& output,
                FaultState& fault, Sha256Result& result) noexcept
            {
                unsigned char digest[32]{};
                if (!fault.Check(Sha256Stage::FinishHash, result))
                    return false;
                const NTSTATUS status = BCryptFinishHash(m_Hash, digest, sizeof(digest), 0u);
                if (status < 0)
                    return Fail(result, Sha256Stage::FinishHash, static_cast<uint32_t>(status));
                constexpr char hex[] = "0123456789abcdef";
                for (size_t index = 0u; index < sizeof(digest); ++index)
                {
                    output.text[index * 2u] = hex[digest[index] >> 4u];
                    output.text[index * 2u + 1u] = hex[digest[index] & 0x0fu];
                }
                output.text[64] = '\0';
                return true;
            }

        private:
            BCRYPT_ALG_HANDLE m_Algorithm = nullptr;
            BCRYPT_HASH_HANDLE m_Hash = nullptr;
        };

        struct FileState
        {
            HANDLE handle = INVALID_HANDLE_VALUE;
            void* buffer = nullptr;

            ~FileState() noexcept
            {
                free(buffer);
                if (handle != INVALID_HANDLE_VALUE)
                    CloseHandle(handle);
            }

            bool Close(FaultState& fault, Sha256Result& result) noexcept
            {
                const HANDLE closing = handle;
                handle = INVALID_HANDLE_VALUE;
                if (!CloseHandle(closing))
                    return Fail(result, Sha256Stage::CloseFile, GetLastError());
                return fault.Check(Sha256Stage::CloseFile, result);
            }
        };
    }

    bool Sha256(const void* input, size_t size,
        Sha256Digest& output, Sha256Result& result, Sha256Fault fault) noexcept
    {
        result = {};
        if ((!input && size) || size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(input))
            return Fail(result, Sha256Stage::InvalidInput, ERROR_INVALID_PARAMETER);
        FaultState faults{ fault };
        HashState state;
        Sha256Digest candidate;
        if (!state.Initialize(faults, result) ||
            !state.Update(input, size, faults, result) || !state.Finish(candidate, faults, result))
            return false;
        output = candidate;
        return true;
    }

    bool Sha256File(const wchar_t* path,
        Sha256Digest& output, Sha256Result& result, Sha256Fault fault) noexcept
    {
        result = {};
        if (!path || !*path)
            return Fail(result, Sha256Stage::InvalidInput, ERROR_INVALID_PARAMETER);
        FaultState faults{ fault };
        if (!faults.Check(Sha256Stage::OpenFile, result))
            return false;
        FileState file;
        file.handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file.handle == INVALID_HANDLE_VALUE)
            return Fail(result, Sha256Stage::OpenFile, GetLastError());
        HashState state;
        if (!state.Initialize(faults, result) || !faults.Check(Sha256Stage::AllocateReadBuffer, result))
            return false;
        constexpr DWORD bufferSize = 1024u * 1024u;
        file.buffer = malloc(bufferSize);
        if (!file.buffer)
            return Fail(result, Sha256Stage::AllocateReadBuffer, ERROR_NOT_ENOUGH_MEMORY);
        for (;;)
        {
            if (!faults.Check(Sha256Stage::ReadFile, result))
                return false;
            DWORD count = 0u;
            if (!ReadFile(file.handle, file.buffer, bufferSize, &count, nullptr))
                return Fail(result, Sha256Stage::ReadFile, GetLastError());
            if (!count)
                break;
            if (!state.Update(file.buffer, count, faults, result))
                return false;
        }
        Sha256Digest candidate;
        if (!state.Finish(candidate, faults, result) || !file.Close(faults, result))
            return false;
        output = candidate;
        return true;
    }
}
