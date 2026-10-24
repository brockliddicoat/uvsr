#pragma once

#include "settings_snapshot_decoder.h"
#include "windows_executable_path.h"

namespace uvsr
{
    // production writes use the Windows known folder. decoder search roots have
    // their separate environment-based contract. failure preserves output.
    [[nodiscard]] bool GetSettingsSnapshotCatalogWritePath(SettingsSnapshotCatalogLocation location,
        WindowsPath& output, SettingsSnapshotError& error) noexcept;

    // code and canonical bytes come from one published controller snapshot and
    // remain borrowed through the complete read, append and atomic publication.
    [[nodiscard]] bool PersistSettingsSnapshotCatalog(const wchar_t* path, std::string_view code,
        std::string_view canonical, SettingsSnapshotError& error) noexcept;

#if defined(UVSR_SETTINGS_SNAPSHOT_TEST_HOOKS)
    // the supplied known-folder stand-in stays borrowed until explicitly cleared.
    // null or empty text simulates lookup failure without querying installed state.
    void SetSettingsSnapshotWriteRootForTests(const wchar_t* path) noexcept;
    void ClearSettingsSnapshotWriteRootForTests() noexcept;
#endif
}
