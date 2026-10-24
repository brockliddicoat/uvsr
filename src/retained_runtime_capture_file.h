#pragma once

#include "windows_executable_path.h"
#include "windows_path_text.h"
#include <string_view>

namespace uvsr
{
    struct RuntimeOutputEvidence;
    enum class RuntimeCaptureFileError : uint8_t
    {
        None, InvalidInput, TooLarge, OutOfMemory, Path, Encoding, Open, Read, Close
    };
    struct RuntimeCaptureFileResult
    {
        RuntimeCaptureFileError error = RuntimeCaptureFileError::None;
        uint32_t code = 0;
        uint32_t cleanupCode = 0;
    };

    // counted inputs are copied and sanitized during this call. failure leaves
    // the existing native path intact; encoding follows the filesystem mode.
    [[nodiscard]] bool BuildRuntimeCapturePath(size_t caseIndex, std::string_view caseName,
        std::string_view phase, WindowsPath& output, RuntimeCaptureFileResult& result) noexcept;
    [[nodiscard]] bool GetRuntimeCaptureStem(const WindowsPath& path,
        WindowsPathText& output, WindowsPathTextResult& result) noexcept;

    // reads synchronously with the former stream's sharing mode. only the encoded
    // evidence fields are replaced. read/close failures retain measured header
    // fields and size, but never publish a partial pixel fingerprint. CRT errors
    // use errno; path construction errors use the native conversion/query code.
    [[nodiscard]] bool AnalyzeRuntimeCaptureBmp(const wchar_t* path, bool captured,
        bool directoryReady, RuntimeOutputEvidence& evidence,
        RuntimeCaptureFileResult& result) noexcept;
}
