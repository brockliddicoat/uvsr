#include "retained_scene_paths.h"
#include "windows_executable_path.h"
#include "windows_file_status.h"
#include "scene_catalog.h"
#include "settings_snapshot_storage.h"

namespace uvsr
{
    bool ValidateRetainedSceneFile(const wchar_t* path, size_t size, SettingsSnapshotError& error) noexcept
    {
        const WindowsFileStatus status = QueryWindowsRegularFile(path);
        if (status.regular)
        {
            error = {};
            return true;
        }
        WindowsPathText generic;
        WindowsPathTextResult encoded;
        if (!generic.Assign(path, size, WindowsPathTextForm::Generic,
                WindowsPathTextEncoding::Filesystem, encoded))
        {
            error = {encoded.error == WindowsPathTextError::Allocation ? SettingsSnapshotErrorCode::OutOfMemory :
                SettingsSnapshotErrorCode::Path, encoded.nativeCode, status.cleanupCode,
                "Could not encode unavailable retained scene path", {}};
            return false;
        }
        const std::string_view name(generic.Data(), generic.Size());
        const uint32_t code = status.nativeCode ? status.nativeCode : status.cleanupCode;
        if (code)
        {
            const WindowsErrorMessage message(code);
            error = ComposeSettingsSnapshotError({"Required retained scene descriptor is unavailable: ", name,
                " (", {message.Text(), message.Size()}, ")"}, SettingsSnapshotErrorCode::Path, code, status.cleanupCode);
        }
        else
            error = ComposeSettingsSnapshotError({"Required retained scene descriptor is unavailable: ", name,
                " (not a regular file)"}, SettingsSnapshotErrorCode::Path);
        return false;
    }

    bool PrepareRetainedSceneName(const wchar_t* path, size_t size,
        WindowsPathText& output, SettingsSnapshotError& error) noexcept
    {
        WindowsPathTextResult encoded;
        if (!output.Assign(path, size, WindowsPathTextForm::NormalizedGeneric,
                WindowsPathTextEncoding::Filesystem, encoded))
        {
            error = {encoded.error == WindowsPathTextError::Allocation ? SettingsSnapshotErrorCode::OutOfMemory :
                SettingsSnapshotErrorCode::Path, encoded.nativeCode, 0, "Could not encode retained scene path", {}};
            return false;
        }
        error = {};
        return true;
    }

    bool FindRetainedScene(const SceneCatalog& catalog, const wchar_t* directory,
        size_t index, const SceneCatalogEntry*& output, SettingsSnapshotError& error) noexcept
    {
        if (index >= RetainedSceneFileCount)
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Invalid retained scene index", {}};
            return false;
        }
        WindowsPath path;
        WindowsPathResult joined;
        if (!JoinWindowsRelativePath(directory, RetainedSceneRelativePaths[index], path, joined))
        {
            error = {joined.error == WindowsPathError::Allocation ? SettingsSnapshotErrorCode::OutOfMemory :
                SettingsSnapshotErrorCode::Path, joined.nativeCode, 0, "Could not prepare retained scene path", {}};
            return false;
        }
        WindowsPathText name;
        if (!PrepareRetainedSceneName(path.Data(), path.Size(), name, error)) return false;
        return FindSceneCatalogEntry(catalog, {name.Data(), name.Size()}, output, error);
    }
}
