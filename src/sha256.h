#pragma once

#include <stddef.h>
#include <stdint.h>

namespace uvsr
{
    enum class Sha256Stage
    {
        None,
        InvalidInput,
        OpenFile,
        AllocateReadBuffer,
        ReadFile,
        CloseFile,
        OpenProvider,
        CreateHash,
        HashData,
        FinishHash
    };

    struct Sha256Fault
    {
        Sha256Stage stage = Sha256Stage::None;
        size_t occurrence = 1u;
    };

    struct Sha256Digest
    {
        char text[65]{};
    };

    struct Sha256Result
    {
        Sha256Stage stage = Sha256Stage::None;
        uint32_t nativeCode = 0u;
    };

    // inputs are borrowed for the call. failure preserves the output digest;
    // nativeCode holds a Win32 error or the failing CNG operation's NTSTATUS.
    [[nodiscard]] bool Sha256(const void* input, size_t size,
        Sha256Digest& output, Sha256Result& result, Sha256Fault fault = {}) noexcept;
    [[nodiscard]] bool Sha256File(const wchar_t* path,
        Sha256Digest& output, Sha256Result& result, Sha256Fault fault = {}) noexcept;
}
