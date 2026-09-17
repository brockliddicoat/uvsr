#pragma once

#include "windows_executable_path.h"
#include "tone_mapping_settings.h"

namespace uvsr
{
    struct SettingsSnapshotError;

    // one synchronous load owns its native filename and ASCII debug stem. both
    // are published together, after the checked join has succeeded.
    class ColorLutAssetPath
    {
    public:
        [[nodiscard]] bool Prepare(const wchar_t* directory, ToneMappingLut lut,
            SettingsSnapshotError& error) noexcept;
        [[nodiscard]] const WindowsPath& Path() const noexcept { return m_Path; }
        [[nodiscard]] const char* DebugName() const noexcept { return m_DebugName; }
    private:
        WindowsPath m_Path;
        char m_DebugName[sizeof("UVSR_Kodak_Portra_400.cube")]{};
    };
}
