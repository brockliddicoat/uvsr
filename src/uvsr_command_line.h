#pragma once

#include <optional>
#include <string>

namespace uvsr
{
    struct UvsrStartupOptions
    {
        std::optional<int> width;
        std::optional<int> height;
        std::optional<int> adapterIndex;
        bool fullscreen = false;
        bool debugValidation = false;
        std::string sceneName;
        std::string settingsSnapshotCode;
    };

    [[nodiscard]] bool ParseUvsrCommandLine(
        int argc,
        const char* const* argv,
        UvsrStartupOptions& options,
        std::string& error);
}
