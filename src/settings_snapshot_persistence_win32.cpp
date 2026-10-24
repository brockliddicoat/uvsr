#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <ShlObj.h>

#include "settings_snapshot_persistence_win32.h"
#include "settings_snapshot.h"
#include "settings_snapshot_internal.h"
#include "file_bytes.h"
#include "file_write.h"

#include <string.h>
#include <wchar.h>

namespace uvsr
{
    namespace
    {
#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
        thread_local bool useWriteRoot = false;
        thread_local const wchar_t* writeRoot = nullptr;
#endif
        struct KnownFolder
        {
            PWSTR path = nullptr;
            ~KnownFolder() noexcept { CoTaskMemFree(path); }
        };
        bool Fail(SettingsSnapshotError& error, SettingsSnapshotErrorCode code,
            const char* message, uint32_t native = 0) noexcept
        {
            error = {};
            error.code = code;
            error.message = message;
            error.nativeCode = native;
            return false;
        }
        struct PathFailure
        {
            const char* reason;
            const wchar_t* path;
        };
        bool EmitPathFailure(json::OutputWriter& writer, const void* context) noexcept
        {
            const auto& failure = *static_cast<const PathFailure*>(context);
            if (!writer.Raw(failure.reason) || !writer.Raw(" at ")) return false;
            const wchar_t* text = failure.path;
            size_t left = wcslen(text);
            while (left)
            {
                int count = left > 256 ? 256 : int(left);
                if (size_t(count) < left && text[count - 1] >= 0xd800 && text[count - 1] <= 0xdbff) --count;
                char bytes[1024];
                const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                    text, count, bytes, sizeof(bytes), nullptr, nullptr);
                if (!size) return writer.Reject(json::ErrorCode::InvalidInput, "invalid catalog path text");
                if (!writer.Raw({bytes, size_t(size)})) return false;
                left -= size_t(count);
                text += count;
            }
            return true;
        }
        bool FileFailure(SettingsSnapshotError& error, SettingsSnapshotErrorCode code,
            const char* reason, const wchar_t* path, uint32_t native) noexcept
        {
            (void)Fail(error, code, reason, native);
            const PathFailure detail{reason, path};
            json::EncodedText text(EmitPathFailure, &detail);
            if (text.IsValid()) error.detail = static_cast<json::EncodedText&&>(text);
            return false;
        }
        const char* WriteReason(FileWriteError error) noexcept
        {
            switch (error)
            {
            case FileWriteError::InvalidPath: return "snapshot catalog destination path is empty";
            case FileWriteError::InvalidInput: return "invalid snapshot catalog byte range";
            case FileWriteError::TooLarge: return "snapshot catalog capacity overflow";
            case FileWriteError::OutOfMemory: return "cannot allocate snapshot catalog write storage";
            case FileWriteError::Directory: return "cannot create snapshot catalog directory";
            case FileWriteError::Create: return "cannot create snapshot catalog temporary file";
            case FileWriteError::Write: return "cannot write snapshot catalog temporary file";
            case FileWriteError::Flush: return "cannot flush snapshot catalog temporary file";
            case FileWriteError::Close: return "cannot close snapshot catalog temporary file";
            case FileWriteError::Publish: return "cannot atomically publish snapshot catalog";
            default: return "snapshot catalog write failed";
            }
        }
    }

    bool GetSettingsSnapshotCatalogWritePath(SettingsSnapshotCatalogLocation location,
        WindowsPath& output, SettingsSnapshotError& error) noexcept
    {
        error = {};
        WindowsPath executable;
        KnownFolder knownFolder;
        const wchar_t* base = nullptr;
        const wchar_t* subdirectory = nullptr;
        WindowsPathResult pathError;
        if (location == SettingsSnapshotCatalogLocation::ExecutableState)
        {
            if (!GetExecutableDirectoryWide(executable, pathError))
                return Fail(error, pathError.error == WindowsPathError::Allocation
                    ? SettingsSnapshotErrorCode::OutOfMemory : SettingsSnapshotErrorCode::Path,
                    "cannot locate snapshot catalog executable directory", pathError.nativeCode);
            base = executable.Data();
            subdirectory = L"state";
        }
        else if (location == SettingsSnapshotCatalogLocation::Installed)
        {
            HRESULT status = S_OK;
#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
            if (useWriteRoot)
            {
                base = writeRoot;
                if (!base || !*base) status = E_FAIL;
            }
            else
#endif
            {
                status = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &knownFolder.path);
                base = knownFolder.path;
            }
            if (FAILED(status) || !base || !*base)
                return Fail(error, SettingsSnapshotErrorCode::Path,
                    "cannot locate snapshot catalog local app data directory", uint32_t(status));
            subdirectory = L"UVSR";
        }
        else return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot catalog location");
        WindowsPath directory;
        if (!JoinWindowsRelativePath(base, subdirectory, directory, pathError))
            return Fail(error, pathError.error == WindowsPathError::Allocation
                ? SettingsSnapshotErrorCode::OutOfMemory : SettingsSnapshotErrorCode::Path,
                "cannot prepare snapshot catalog directory", pathError.nativeCode);
        wchar_t filename[] = L"settings-snapshots-v0000.txt";
        constexpr size_t versionOffset = sizeof("settings-snapshots-v") - 1;
        for (size_t index = 0; index < 4; ++index) filename[versionOffset + index] = wchar_t(SettingsSnapshotVersionText[index]);
        if (!JoinWindowsRelativePath(directory.Data(), filename, output, pathError))
            return Fail(error, pathError.error == WindowsPathError::Allocation
                ? SettingsSnapshotErrorCode::OutOfMemory : SettingsSnapshotErrorCode::Path,
                "cannot prepare snapshot catalog write path", pathError.nativeCode);
        return true;
    }

    bool PersistSettingsSnapshotCatalog(const wchar_t* path, std::string_view code,
        std::string_view canonical, SettingsSnapshotError& error) noexcept
    {
        json::EncodedText previousDetail(static_cast<json::EncodedText&&>(error.detail));
        error = {};
        if (!path || !*path) return Fail(error, SettingsSnapshotErrorCode::Path, "snapshot catalog destination path is empty");
        if (!settings_snapshot_detail::ValidText(code) || !settings_snapshot_detail::ValidText(canonical))
            return Fail(error, SettingsSnapshotErrorCode::InvalidInput, "invalid snapshot text range");
        FileBytes existing;
        FileReadResult readError;
        if (!ReadFileBytes(path, UINT64_MAX, existing, readError) && readError.error != FileReadError::Missing)
            return FileFailure(error, readError.error == FileReadError::OutOfMemory ? SettingsSnapshotErrorCode::OutOfMemory
                : readError.error == FileReadError::TooLarge ? SettingsSnapshotErrorCode::Capacity : SettingsSnapshotErrorCode::Catalog,
                "cannot read settings snapshot catalog", path, readError.systemCode);
        json::EncodedText section;
        if (!FormatSettingsSnapshotCatalogSection(code, canonical, section, error)) return false;
        const std::string_view prior{existing.Data(), existing.Size()};
        if (prior.find({section.Data(), section.Size()}) != std::string_view::npos) return true;
        // the opening is the first part of the section; avoid a second text owner.
        const size_t openingSize = code.size() + 3;
        if (prior.find({section.Data(), openingSize}) != std::string_view::npos)
            return Fail(error, SettingsSnapshotErrorCode::Collision, "settings snapshot catalog contains a conflicting entry");
        char header[] = "# UVSR Settings Snapshot Catalog v0000\n";
        constexpr size_t versionOffset = sizeof("# UVSR Settings Snapshot Catalog v") - 1;
        for (size_t index = 0; index < 4; ++index) header[versionOffset + index] = SettingsSnapshotVersionText[index];
        const FileWriteSpan chunks[] = {
            {existing.Size() ? existing.Data() : header, existing.Size() ? existing.Size() : sizeof(header) - 1},
            {section.Data(), section.Size()}
        };
        FileWriteResult writeError;
        if (WriteFileBytesAtomically(path, chunks, 2, writeError)) return true;
        (void)FileFailure(error, writeError.error == FileWriteError::OutOfMemory ? SettingsSnapshotErrorCode::OutOfMemory
            : writeError.error == FileWriteError::TooLarge ? SettingsSnapshotErrorCode::Capacity : SettingsSnapshotErrorCode::Catalog,
            WriteReason(writeError.error), path, writeError.systemCode);
        error.cleanupCode = writeError.cleanupCode;
        return false;
    }

#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
    void SetSettingsSnapshotWriteRootForTests(const wchar_t* path) noexcept { useWriteRoot = true; writeRoot = path; }
    void ClearSettingsSnapshotWriteRootForTests() noexcept { useWriteRoot = false; writeRoot = nullptr; }
#endif
}
