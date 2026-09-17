#include "color_lut_asset_path.h"
#include "settings_snapshot_storage.h"
#include <string.h>

namespace uvsr
{
    bool ColorLutAssetPath::Prepare(const wchar_t* directory, ToneMappingLut lut,
        SettingsSnapshotError& error) noexcept
    {
        const char* name = ToneMappingLutFilename(lut);
        const size_t size = strlen(name);
        wchar_t wide[sizeof(m_DebugName)];
        if (size <= 5 || size >= sizeof(m_DebugName) || strcmp(name + size - 5, ".cube"))
        {
            error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Invalid film LUT asset", {}};
            return false;
        }
        for (size_t index = 0; index < size; ++index)
        {
            const auto value = static_cast<unsigned char>(name[index]);
            if (value >= 128)
            {
                error = {SettingsSnapshotErrorCode::InvalidInput, 0, 0, "Invalid film LUT filename", {}};
                return false;
            }
            wide[index] = wchar_t(value);
        }
        wide[size] = L'\0';
        WindowsPath candidate;
        WindowsPathResult joined;
        if (!JoinWindowsRelativePath(directory, wide, candidate, joined))
        {
            error = {joined.error == WindowsPathError::Allocation ? SettingsSnapshotErrorCode::OutOfMemory :
                SettingsSnapshotErrorCode::Path, joined.nativeCode, 0, "Could not prepare film LUT path", {}};
            return false;
        }
        m_Path = static_cast<WindowsPath&&>(candidate);
        memcpy(m_DebugName, name, size - 5);
        m_DebugName[size - 5] = '\0';
        error = {};
        return true;
    }
}
