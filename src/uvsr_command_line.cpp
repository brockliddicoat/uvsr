#include "uvsr_command_line.h"

#include "command_line_options.h"
#include "settings_snapshot_decoder.h"

#include <cstring>
#include <limits>
#include <string>

namespace uvsr
{
    bool ParseUvsrCommandLine(
        int argc,
        const char* const* argv,
        UvsrStartupOptions& options,
        std::string& error)
    {
        options = {};
        error.clear();
        if (argc < 0 || (argc > 0 && !argv))
        {
            error = "command-line arguments are unavailable";
            return false;
        }

        for (int index = 1; index < argc; ++index)
        {
            const char* argument = argv[index];
            if (!argument)
            {
                error = "command-line argument is null";
                return false;
            }

            const auto readInteger = [&]
            (
                const char* option,
                int minimum,
                std::optional<int>& value)
            {
                int parsedValue = 0;
                if (index + 1 >= argc || !argv[index + 1] ||
                    !ParseCommandLineInt(
                        argv[index + 1],
                        minimum,
                        std::numeric_limits<int>::max(),
                        parsedValue))
                {
                    error = std::string(option) +
                        " requires an exact integer of at least " +
                        std::to_string(minimum);
                    return false;
                }
                value = parsedValue;
                ++index;
                return true;
            };

            if (std::strcmp(argument, "-width") == 0)
            {
                if (!readInteger(argument, 1, options.width))
                    return false;
            }
            else if (std::strcmp(argument, "-height") == 0)
            {
                if (!readInteger(argument, 1, options.height))
                    return false;
            }
            else if (std::strcmp(argument, "-fullscreen") == 0)
            {
                options.fullscreen = true;
            }
#if defined(UVSR_BUILD_TESTING)
            else if (std::strcmp(argument, "-debug") == 0)
            {
                options.debugValidation = true;
            }
#endif
            else if (std::strcmp(argument, "-adapter") == 0)
            {
                if (!readInteger(argument, 0, options.adapterIndex))
                    return false;
            }
            else if (std::strcmp(argument, "--settings-snapshot") == 0)
            {
                if (index + 1 >= argc || !argv[index + 1])
                {
                    error =
                        "--settings-snapshot requires one 32-character code";
                    return false;
                }
                if (!options.settingsSnapshotCode.empty())
                {
                    error =
                        "--settings-snapshot may be specified only once";
                    return false;
                }
                std::string validationError;
                if (!ValidateSettingsSnapshotLoadCode(
                        argv[index + 1],
                        validationError))
                {
                    error = "invalid --settings-snapshot value: " +
                        validationError;
                    return false;
                }
                options.settingsSnapshotCode = argv[++index];
            }
#if defined(UVSR_BUILD_TESTING)
            else if (std::strcmp(
                    argument,
                    "--verify-settings-contract") == 0 ||
                std::strcmp(
                    argument,
                    "--verify-retained-runtime") == 0)
            {
                // The developer engine consumes these after ordinary startup.
            }
#endif
            else if (argument[0] != '-')
            {
                options.sceneName = argument;
            }
            else
            {
                error = "unknown command-line option '" +
                    std::string(argument) + "'";
                return false;
            }
        }
        return true;
    }
}
