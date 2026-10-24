#pragma once

#include "retained_runtime_message.h"
#include "settings_value.h"
#include "windows_path_text.h"

namespace uvsr
{
    struct RetainedRuntimeProvenance
    {
        std::string_view settingsHash;
        std::string_view engineVersion;
        std::string_view sourceCommit;
        std::string_view sourceIdentity;
        std::string_view configuration;
        SettingsSnapshotText packagePath;
        WindowsPathText executablePath;
        SettingsSnapshotText executableSha256;
        bool sourceClean = false;
        bool production = false;
        bool debugLayerRequested = false;
        bool nvrhiValidationRequested = false;
    };

    // static build fields borrow generated literals. environment/path text is owned.
    // every return publishes this attempt, including partial failure; messages are static.
    [[nodiscard]] bool PrepareRetainedRuntimeProvenance(RetainedRuntimeProvenance& output,
        bool debugValidation, RetainedRuntimeMessage& failure) noexcept;
}
