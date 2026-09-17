#include "retained_runtime_capture_file.h"
#include "retained_runtime_diagnostic.h"
#include "scene_catalog_path.h"

#include <Windows.h>
#include <cctype>
#include <errno.h>
#include <limits.h>
#include <share.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <utility>

namespace uvsr
{
    namespace
    {
        bool Fail(RuntimeCaptureFileResult& result, RuntimeCaptureFileError error,
            uint32_t code = 0) noexcept
        {
            result = {error, code, 0};
            return false;
        }
        bool PathFailure(RuntimeCaptureFileResult& result, WindowsPathResult path) noexcept
        {
            return Fail(result, path.error == WindowsPathError::Allocation
                ? RuntimeCaptureFileError::OutOfMemory : RuntimeCaptureFileError::Path,
                path.nativeCode);
        }
        struct Buffer
        {
            void* data;
            explicit Buffer(void* value) noexcept : data(value) {}
            Buffer(const Buffer&) = delete;
            Buffer& operator=(const Buffer&) = delete;
            ~Buffer() noexcept { free(data); }
        };
        bool ValidText(std::string_view text) noexcept
        {
            return (!text.size() || text.data()) && text.size() <= size_t(PTRDIFF_MAX) &&
                text.size() <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(text.data());
        }
        char SafeByte(char value) noexcept
        {
            return std::isalnum(static_cast<unsigned char>(value)) || value == '-' || value == '_'
                ? value : '-';
        }
        uint32_t ReadU32(const unsigned char* header, size_t size, size_t offset) noexcept
        {
            if (offset > size || size - offset < 4) return 0;
            return uint32_t(header[offset]) | (uint32_t(header[offset + 1]) << 8) |
                (uint32_t(header[offset + 2]) << 16) | (uint32_t(header[offset + 3]) << 24);
        }
        struct Fingerprint
        {
            uint64_t hash = 1469598103934665603ull;
            unsigned char minimum = 0xff;
            unsigned char maximum = 0;
            void Add(const unsigned char* bytes, size_t count) noexcept
            {
                for (size_t index = 0; index < count; ++index)
                {
                    const unsigned char byte = bytes[index];
                    if (byte < minimum) minimum = byte;
                    if (byte > maximum) maximum = byte;
                    hash ^= uint64_t(byte);
                    hash *= 1099511628211ull;
                }
            }
        };
    }

    bool BuildRuntimeCapturePath(size_t caseIndex, std::string_view caseName,
        std::string_view phase, WindowsPath& output, RuntimeCaptureFileResult& result) noexcept
    {
        result = {};
        if (!ValidText(caseName) || !ValidText(phase))
            return Fail(result, RuntimeCaptureFileError::InvalidInput);
        char prefix[27];
        const int prefixSize = snprintf(prefix, sizeof(prefix), "case-%zu-", caseIndex);
        if (prefixSize < 0 || size_t(prefixSize) >= sizeof(prefix))
            return Fail(result, RuntimeCaptureFileError::TooLarge);
        const size_t fixed = size_t(prefixSize) + 1 + 4;
        if (caseName.size() > size_t(INT_MAX) - fixed ||
            phase.size() > size_t(INT_MAX) - fixed - caseName.size())
            return Fail(result, RuntimeCaptureFileError::TooLarge);
        const size_t size = fixed + caseName.size() + phase.size();
        Buffer narrow{malloc(size + 1)};
        if (!narrow.data) return Fail(result, RuntimeCaptureFileError::OutOfMemory);
        auto* filename = static_cast<char*>(narrow.data);
        memcpy(filename, prefix, size_t(prefixSize));
        size_t cursor = size_t(prefixSize);
        for (size_t index = 0; index < caseName.size(); ++index) filename[cursor++] = SafeByte(caseName[index]);
        filename[cursor++] = '-';
        for (size_t index = 0; index < phase.size(); ++index) filename[cursor++] = SafeByte(phase[index]);
        memcpy(filename + cursor, ".bmp", 5);
        const std::string_view text(filename, size);
        catalog_path::ConversionPlan plan;
        catalog_path::Error encoding;
        if (!catalog_path::MeasureDecode(text, catalog_path::Encoding::Filesystem, plan, encoding))
            return Fail(result, RuntimeCaptureFileError::Encoding, encoding.nativeCode);
        if (plan.size >= size_t(PTRDIFF_MAX) / sizeof(wchar_t))
            return Fail(result, RuntimeCaptureFileError::TooLarge);
        Buffer wide{malloc((plan.size + 1) * sizeof(wchar_t))};
        if (!wide.data) return Fail(result, RuntimeCaptureFileError::OutOfMemory);
        size_t written = 0;
        if (!catalog_path::Decode(text, plan, static_cast<wchar_t*>(wide.data),
                plan.size + 1, written, encoding))
            return Fail(result, RuntimeCaptureFileError::Encoding, encoding.nativeCode);
        wchar_t processDirectory[33];
        if (swprintf_s(processDirectory, L"uvsr-retained-runtime-%lu", GetCurrentProcessId()) < 0)
            return Fail(result, RuntimeCaptureFileError::TooLarge);
        WindowsPath temporary, directory, completed;
        WindowsPathResult path;
        if (!GetTemporaryDirectoryWide(temporary, path) ||
            !JoinWindowsRelativePath(temporary.Data(), processDirectory, directory, path) ||
            !JoinWindowsRelativePath(directory.Data(), static_cast<wchar_t*>(wide.data), completed, path))
            return PathFailure(result, path);
        output = std::move(completed);
        return true;
    }

    bool GetRuntimeCaptureStem(const WindowsPath& path, WindowsPathText& output,
        WindowsPathTextResult& result) noexcept
    {
        const size_t first = catalog_path::FilenameStart({path.Data(), path.Size()});
        const size_t count = path.Size() - first;
        if (count < 4 || wmemcmp(path.Data() + path.Size() - 4, L".bmp", 4) != 0)
        {
            result = {WindowsPathTextError::InvalidPath, 0};
            return false;
        }
        return output.Assign(path.Data() + first, count - 4, WindowsPathTextForm::Native,
            WindowsPathTextEncoding::Filesystem, result);
    }

    bool AnalyzeRuntimeCaptureBmp(const wchar_t* path, bool captured, bool directoryReady,
        RuntimeOutputEvidence& evidence, RuntimeCaptureFileResult& result) noexcept
    {
        result = {};
        evidence.valid = false;
        evidence.width = evidence.height = 0;
        evidence.encodedBytes = evidence.pixelBytes = 0;
        evidence.pixelHash = 1469598103934665603ull;
        evidence.minimumByte = evidence.maximumByte = 0;
        if (!captured) return true;
        if (!path || !*path) return Fail(result, RuntimeCaptureFileError::InvalidInput);
        FILE* file = _wfsopen(path, L"rb", _SH_DENYNO);
        if (!file) return Fail(result, RuntimeCaptureFileError::Open, uint32_t(errno));
        unsigned char header[54];
        unsigned char buffer[4096];
        size_t headerBytes = 0;
        uint32_t pixelOffset = 0;
        Fingerprint pixels;
        bool complete = true;
        for (;;)
        {
            const bool prefix = headerBytes < sizeof(header);
            unsigned char* destination = prefix ? header + headerBytes : buffer;
            const size_t capacity = prefix ? sizeof(header) - headerBytes : sizeof(buffer);
            errno = 0;
            const size_t received = fread(destination, 1, capacity, file);
            const int readCode = errno;
            const uint64_t first = evidence.encodedBytes;
            if (uint64_t(received) > UINT64_MAX - first)
            {
                complete = Fail(result, RuntimeCaptureFileError::TooLarge);
                break;
            }
            evidence.encodedBytes += received;
            if (prefix)
            {
                headerBytes += received;
                if (headerBytes == sizeof(header))
                {
                    pixelOffset = ReadU32(header, headerBytes, 10);
                    if (pixelOffset < headerBytes)
                        pixels.Add(header + pixelOffset, headerBytes - pixelOffset);
                }
            }
            else
            {
                const uint64_t skip = uint64_t(pixelOffset) > first ? uint64_t(pixelOffset) - first : 0;
                if (skip < received) pixels.Add(buffer + size_t(skip), received - size_t(skip));
            }
            if (ferror(file))
            {
                complete = Fail(result, RuntimeCaptureFileError::Read, uint32_t(readCode ? readCode : EIO));
                break;
            }
            if (feof(file)) break;
            if (!received)
            {
                complete = Fail(result, RuntimeCaptureFileError::Read, EIO);
                break;
            }
        }
        evidence.width = ReadU32(header, headerBytes, 18);
        evidence.height = ReadU32(header, headerBytes, 22);
        errno = 0;
        if (fclose(file) != 0)
        {
            const uint32_t closeCode = uint32_t(errno ? errno : EIO);
            if (complete) complete = Fail(result, RuntimeCaptureFileError::Close, closeCode);
            else result.cleanupCode = closeCode;
        }
        if (complete && evidence.encodedBytes >= sizeof(header) && header[0] == 'B' && header[1] == 'M' &&
            uint64_t(pixelOffset) < evidence.encodedBytes && evidence.width && evidence.height && directoryReady)
        {
            evidence.pixelBytes = evidence.encodedBytes - pixelOffset;
            evidence.pixelHash = pixels.hash;
            evidence.minimumByte = pixels.minimum;
            evidence.maximumByte = pixels.maximum;
            evidence.valid = true;
        }
        return complete;
    }
}
