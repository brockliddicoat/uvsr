#pragma once

#include "windows_path_text.h"

namespace uvsr
{
    class SceneCatalog;
    struct SceneCatalogEntry;
    struct SettingsSnapshotError;

    inline constexpr const wchar_t* RetainedSceneRelativePaths[] = {
        L"bistro_interior_retextured/bistro_interior_retextured.scene.json",
        L"san_miguel_retextured/san_miguel_retextured.scene.json"
    };
    inline constexpr size_t RetainedSceneFileCount = sizeof(RetainedSceneRelativePaths) / sizeof(*RetainedSceneRelativePaths);

    // path[size] is a readable terminator, with no earlier NUL. directory is
    // likewise terminated; both inputs are borrowed only during the call.
    [[nodiscard]] bool ValidateRetainedSceneFile(const wchar_t* path, size_t size,
        SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool PrepareRetainedSceneName(const wchar_t* path, size_t size,
        WindowsPathText& output, SettingsSnapshotError& error) noexcept;
    [[nodiscard]] bool FindRetainedScene(const SceneCatalog& catalog, const wchar_t* directory,
        size_t index, const SceneCatalogEntry*& output, SettingsSnapshotError& error) noexcept;
}
