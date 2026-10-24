#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "settings_snapshot_decoder.h"
#include "settings_snapshot_internal.h"
#include "settings_snapshot.h"
#include "file_bytes.h"
#include "windows_executable_path.h"

#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <wchar.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
        thread_local const wchar_t* failedProbePath = nullptr;
        thread_local DWORD failedProbeCode = 0;
#endif
        bool Fail(SettingsSnapshotError& error, SettingsSnapshotErrorCode code,
            const char* message, DWORD native = 0) noexcept
        {
            error = {};
            error.code = code;
            error.message = message;
            error.nativeCode = native;
            return false;
        }
        bool TextFailure(const json::EncodedText& text, SettingsSnapshotError& error) noexcept
        {
            return Fail(error, text.Failure().code == json::ErrorCode::OutOfMemory
                ? SettingsSnapshotErrorCode::OutOfMemory
                : text.Failure().code == json::ErrorCode::Capacity ? SettingsSnapshotErrorCode::Capacity
                : SettingsSnapshotErrorCode::Format, text.Failure().message);
        }
        bool Join(const wchar_t* base, const wchar_t* suffix, WindowsPath& output,
            SettingsSnapshotError& error) noexcept
        {
            WindowsPathResult result;
            if (JoinWindowsRelativePath(base, suffix, output, result)) return true;
            return Fail(error, result.error == WindowsPathError::Allocation
                ? SettingsSnapshotErrorCode::OutOfMemory : SettingsSnapshotErrorCode::Path,
                "cannot prepare snapshot catalog path", result.nativeCode);
        }
        struct NativeHandle
        {
            HANDLE value = INVALID_HANDLE_VALUE;
            ~NativeHandle() noexcept { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
        };
        bool Probe(const wchar_t* path, bool directory, DWORD& error,
            DWORD cachedAttributes = INVALID_FILE_ATTRIBUTES) noexcept
        {
            error = 0;
#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
            if (failedProbePath && path && wcscmp(path, failedProbePath) == 0)
            {
                failedProbePath = nullptr;
                error = failedProbeCode;
                return false;
            }
#endif
            DWORD attributes = cachedAttributes == INVALID_FILE_ATTRIBUTES
                ? GetFileAttributesW(path) : cachedAttributes;
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                error = GetLastError();
                return false;
            }
            // status queries follow links, as the former filesystem status did.
            if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            {
                NativeHandle file;
                file.value = CreateFileW(path, FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
                if (file.value == INVALID_HANDLE_VALUE)
                {
                    error = GetLastError();
                    return false;
                }
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(file.value, &info))
                {
                    error = GetLastError();
                    return false;
                }
                if (GetFileType(file.value) != FILE_TYPE_DISK) return false;
                attributes = info.dwFileAttributes;
            }
            return bool(attributes & FILE_ATTRIBUTE_DIRECTORY) == directory;
        }
        struct CatalogReadError
        {
            const char* prefix;
            const wchar_t* path;
        };
        bool EmitReadError(json::OutputWriter& output, const void* context) noexcept
        {
            const auto& failure = *static_cast<const CatalogReadError*>(context);
            if (!output.Raw(failure.prefix)) return false;
            const wchar_t* position = failure.path;
            size_t remaining = wcslen(position);
            while (remaining)
            {
                int count = remaining > 256 ? 256 : int(remaining);
                if (size_t(count) < remaining && position[count - 1] >= 0xd800 &&
                    position[count - 1] <= 0xdbff) --count;
                char bytes[1024];
                const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                    position, count, bytes, sizeof(bytes), nullptr, nullptr);
                if (!size) return output.Reject(json::ErrorCode::InvalidInput, "invalid snapshot catalog path text");
                if (!output.Raw({bytes, size_t(size)})) return false;
                position += count;
                remaining -= size_t(count);
            }
            return true;
        }
        bool ReadError(const wchar_t* path, const FileReadResult& result,
            SettingsSnapshotError& error) noexcept
        {
            if (result.error == FileReadError::OutOfMemory)
                return Fail(error, SettingsSnapshotErrorCode::OutOfMemory,
                    "cannot allocate snapshot catalog bytes", result.systemCode);
            if (result.error == FileReadError::TooLarge)
                return Fail(error, SettingsSnapshotErrorCode::Capacity,
                    "snapshot catalog byte capacity overflow", result.systemCode);
            const CatalogReadError context{
                result.error == FileReadError::Missing || result.error == FileReadError::Open ||
                    result.error == FileReadError::NotRegular || result.error == FileReadError::InvalidPath
                    ? "snapshot catalog not found: " : "cannot read snapshot catalog: ", path ? path : L""};
            json::EncodedText detail(EmitReadError, &context);
            if (!detail.IsValid()) return TextFailure(detail, error);
            Fail(error, SettingsSnapshotErrorCode::Catalog, context.prefix, result.systemCode);
            error.detail = static_cast<json::EncodedText&&>(detail);
            return false;
        }
        bool NextLine(std::string_view text, size_t& offset, std::string_view& line) noexcept
        {
            if (offset >= text.size()) return false;
            const size_t newline = text.find('\n', offset);
            const size_t end = newline == std::string_view::npos ? text.size() : newline;
            line = text.substr(offset, end - offset);
            offset = newline == std::string_view::npos ? text.size() : newline + 1;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            return true;
        }
        bool IsMarker(std::string_view line, std::string_view code, bool closing) noexcept
        {
            const size_t prefix = closing ? 2 : 1;
            return line.size() >= prefix + 1 && line.front() == '[' && line.back() == ']' &&
                (!closing || line[1] == '/') && line.substr(prefix, line.size() - prefix - 1) == code;
        }
        bool EmitPayload(json::OutputWriter& output, const void* context) noexcept
        {
            const auto text = *static_cast<const std::string_view*>(context);
            size_t offset = 0;
            std::string_view line;
            while (NextLine(text, offset, line))
                if (!output.Raw({line.data(), line.size()}) || !output.Raw("\n")) return false;
            return true;
        }
        bool EmitUnterminated(json::OutputWriter& output, const void* context) noexcept
        {
            const auto code = *static_cast<const std::string_view*>(context);
            return output.Raw("snapshot catalog contains an unterminated ") &&
                output.Raw({code.data(), code.size()}) && output.Raw(" entry");
        }
        bool EmitCopy(json::OutputWriter& output, const void* context) noexcept
        {
            const auto text = *static_cast<const std::string_view*>(context);
            return output.Raw({text.data(), text.size()});
        }
        struct EnvironmentText
        {
            wchar_t* text = nullptr;
            ~EnvironmentText() noexcept { free(text); }
        };
        struct DirectorySearch
        {
            HANDLE value = INVALID_HANDLE_VALUE;
            ~DirectorySearch() noexcept { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
        };
    }

    bool GetDefaultSettingsSnapshotCatalogPaths(std::string_view version,
        SettingsSnapshotCatalogLocation location, SettingsSnapshotCatalogPaths& output,
        SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (version.size() != 4)
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "snapshot version must have four digits");
        if (!settings_snapshot_detail::ValidText(version))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot text range");
        char name[] = "settings-snapshots-v0000.txt";
        memcpy(name + sizeof("settings-snapshots-v") - 1, version.data(), 4);
        wchar_t nativeName[64];
        const UINT codePage = ___lc_codepage_func() == CP_UTF8 ? CP_UTF8
            : AreFileApisANSI() ? CP_ACP : CP_OEMCP;
        if (!MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
            name, int(sizeof(name)), nativeName, int(sizeof(nativeName) / sizeof(wchar_t))))
            return Fail(error, SettingsSnapshotErrorCode::Path, "cannot prepare snapshot catalog name", GetLastError());
        SettingsSnapshotCatalogPaths candidate;
        WindowsPath base;
        WindowsPath path;
        if (location == SettingsSnapshotCatalogLocation::ExecutableState)
        {
            WindowsPath directory;
            WindowsPathResult result;
            if (!GetExecutableDirectoryWide(directory, result))
                return Fail(error, result.error == WindowsPathError::Allocation
                    ? SettingsSnapshotErrorCode::OutOfMemory : SettingsSnapshotErrorCode::Path,
                    "UVSR could not identify its executable path.", result.nativeCode);
            if (!Join(directory.Data(), L"state", base, error) ||
                !Join(base.Data(), nativeName, path, error) || !candidate.Append(path.Data(), error)) return false;
        }
        else if (location == SettingsSnapshotCatalogLocation::Installed)
        {
            EnvironmentText local;
            size_t size = 0;
            if (_wdupenv_s(&local.text, &size, L"LOCALAPPDATA") != 0)
                return Fail(error, SettingsSnapshotErrorCode::Path, "cannot read LOCALAPPDATA");
            if (!local.text || size <= 1)
                return Fail(error, SettingsSnapshotErrorCode::Path,
                    "LOCALAPPDATA is unavailable; pass an explicit catalog path");
            if (!Join(local.text, L"UVSR", base, error) || !Join(base.Data(), nativeName, path, error) ||
                !candidate.Append(path.Data(), error)) return false;
            WindowsPath packages;
            if (!Join(local.text, L"Packages", packages, error)) return false;
            DWORD nativeError = 0;
            if (Probe(packages.Data(), true, nativeError))
            {
                WindowsPath pattern;
                if (!Join(packages.Data(), L"*", pattern, error)) return false;
                SettingsSnapshotCatalogPaths directories;
                DirectorySearch search;
                WIN32_FIND_DATAW entry{};
                search.value = FindFirstFileExW(pattern.Data(), FindExInfoBasic, &entry,
                    FindExSearchNameMatch, nullptr, 0);
                if (search.value == INVALID_HANDLE_VALUE)
                {
                    nativeError = GetLastError();
                    if (nativeError != ERROR_FILE_NOT_FOUND && nativeError != ERROR_NO_MORE_FILES)
                        return Fail(error, SettingsSnapshotErrorCode::Catalog,
                            "cannot inspect package-local catalogs", nativeError);
                }
                else
                {
                    do
                    {
                        if (wcscmp(entry.cFileName, L".") == 0 || wcscmp(entry.cFileName, L"..") == 0) continue;
                        WindowsPath directory;
                        if (!Join(packages.Data(), entry.cFileName, directory, error)) return false;
                        if (Probe(directory.Data(), true, nativeError, entry.dwFileAttributes))
                        {
                            if (!directories.Append(directory.Data(), error)) return false;
                        }
                        // an unusable entry is skipped; failure to advance still fails below.
                    } while (FindNextFileW(search.value, &entry));
                    nativeError = GetLastError();
                    if (nativeError != ERROR_NO_MORE_FILES)
                        return Fail(error, SettingsSnapshotErrorCode::Catalog,
                            "cannot inspect package-local catalogs", nativeError);
                }
                directories.Sort();
                for (size_t index = 0; index < directories.Count(); ++index)
                {
                    if (!Join(directories.Path(index), L"LocalCache\\Local\\UVSR", base, error) ||
                        !Join(base.Data(), nativeName, path, error)) return false;
                    if (Probe(path.Data(), false, nativeError) && !candidate.Append(path.Data(), error)) return false;
                }
            }
        }
        else return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot catalog location");
        output = static_cast<SettingsSnapshotCatalogPaths&&>(candidate);
        return true;
    }

    bool ReadMatchingSettingsSnapshots(const wchar_t* catalogPath, std::string_view code,
        SettingsSnapshotMatches& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(code))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot text range");
        FileBytes bytes;
        FileReadResult result;
        if (!ReadFileBytes(catalogPath, size_t(PTRDIFF_MAX) - 1, bytes, result))
            return ReadError(catalogPath, result, error);
        const std::string_view text(bytes.Data(), bytes.Size());
        SettingsSnapshotMatches candidate;
        size_t offset = 0;
        std::string_view line;
        while (NextLine(text, offset, line))
        {
            if (!IsMarker(line, code, false)) continue;
            const size_t start = offset;
            size_t end = offset;
            bool terminated = false;
            while (NextLine(text, offset, line))
            {
                if (IsMarker(line, code, true))
                {
                    terminated = true;
                    break;
                }
                end = offset;
            }
            if (!terminated)
            {
                json::EncodedText detail(EmitUnterminated, &code);
                if (!detail.IsValid()) return TextFailure(detail, error);
                Fail(error, SettingsSnapshotErrorCode::Catalog, "unterminated snapshot catalog entry");
                error.detail = static_cast<json::EncodedText&&>(detail);
                return false;
            }
            const auto payload = text.substr(start, end - start);
            json::EncodedText canonical(EmitPayload, &payload);
            if (!canonical.IsValid()) return TextFailure(canonical, error);
            if (!candidate.Append(static_cast<json::EncodedText&&>(canonical), error)) return false;
        }
        output = static_cast<SettingsSnapshotMatches&&>(candidate);
        return true;
    }

    bool DecodeSettingsSnapshot(std::string_view code, const SettingsSnapshotCatalogPaths& catalogPaths,
        DecodedSettings& output, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (!settings_snapshot_detail::ValidText(code) || !IsSettingsSnapshotCode(code))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "expected a registered 32-digit snapshot code");
        bool foundCatalog = false;
        bool collision = false;
        json::EncodedText canonical;
        for (size_t index = 0; index < catalogPaths.Count(); ++index)
        {
            DWORD nativeError = 0;
            if (!Probe(catalogPaths.Path(index), false, nativeError)) continue;
            foundCatalog = true;
            SettingsSnapshotMatches matches;
            if (!ReadMatchingSettingsSnapshots(catalogPaths.Path(index), code, matches, error)) return false;
            for (size_t match = 0; match < matches.Count(); ++match)
            {
                const auto text = matches.Text(match);
                if (!canonical.IsValid())
                {
                    canonical = json::EncodedText(EmitCopy, &text);
                    if (!canonical.IsValid()) return TextFailure(canonical, error);
                }
                else if (std::string_view(canonical.Data(), canonical.Size()) != text) collision = true;
            }
            // later read or termination errors take precedence over a collision.
        }
        if (!foundCatalog)
            return Fail(error, SettingsSnapshotErrorCode::Catalog, "no settings snapshot catalog was found");
        if (!canonical.IsValid())
            return Fail(error, SettingsSnapshotErrorCode::Catalog, "settings snapshot is absent from the catalogs");
        if (collision)
            return Fail(error, SettingsSnapshotErrorCode::Collision, "settings snapshot fingerprint collision");
        const std::string_view text(canonical.Data(), canonical.Size());
        if (BuildSettingsSnapshotCode(text, code.substr(0, 4)).View() != code)
            return Fail(error, SettingsSnapshotErrorCode::Fingerprint, "settings snapshot fingerprint check failed");
        return ParseSettingsSnapshot(text, output, error);
    }

#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
    void FailSettingsSnapshotCatalogProbeOnce(const wchar_t* path, uint32_t nativeCode) noexcept
    {
        failedProbePath = path;
        failedProbeCode = nativeCode;
    }
    void ClearSettingsSnapshotCatalogProbeFailure() noexcept
    {
        failedProbePath = nullptr;
        failedProbeCode = 0;
    }
#endif
}
