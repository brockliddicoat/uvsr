#pragma once

#include "settings_snapshot_storage.h"

namespace uvsr
{
    // decoded is a private, disposable candidate. failure may leave earlier
    // migrations applied; publish it only after the complete call succeeds.
    [[nodiscard]] bool ApplyLegacySettingMigrations(uint16_t version,
        DecodedSettings& decoded, SettingsSnapshotError& error) noexcept;
}
