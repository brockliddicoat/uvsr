#include "uvsr_command_line.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "UVSR command-line validation failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    bool Parse(
        std::initializer_list<const char*> arguments,
        uvsr::UvsrStartupOptions& options,
        std::string& error)
    {
        return uvsr::ParseUvsrCommandLine(
            static_cast<int>(arguments.size()),
            arguments.begin(),
            options,
            error);
    }
}

int main()
{
    using namespace uvsr;

    UvsrStartupOptions options;
    std::string error;
    Require(Parse({
            "uvsr-engine.exe",
            "-width", "1280",
            "-height", "720",
            "-adapter", "2",
            "-fullscreen",
            "Bistro Interior",
            "--settings-snapshot",
            "0007cbf29ce4842223256c62272e07bb"
        }, options, error),
        "ordinary retained options must parse");
    Require(options.width == 1280 && options.height == 720 &&
            options.adapterIndex == 2 && options.fullscreen &&
            options.sceneName == "Bistro Interior" &&
            options.settingsSnapshotCode ==
                "0007cbf29ce4842223256c62272e07bb",
        "parsed options must retain exact values");

    Require(!Parse({ "uvsr-engine.exe", "-width", "0" },
            options, error) && error.find("at least 1") != std::string::npos,
        "zero width must fail with its range");
    Require(!Parse({ "uvsr-engine.exe", "-adapter", "2gpu" },
            options, error),
        "adapter values must be exact integers");
    Require(!Parse({ "uvsr-engine.exe", "--settings-snapshot" },
            options, error),
        "a missing settings code must fail");
    Require(!Parse({
            "uvsr-engine.exe",
            "--settings-snapshot", "0007cbf29ce4842223256c62272e07bb",
            "--settings-snapshot", "0007cbf29ce4842223256c62272e07bb"
        }, options, error),
        "duplicate settings codes must fail");
    Require(!Parse({
            "uvsr-engine.exe",
            "--settings-snapshot", "0007not-a-canonical-code"
        }, options, error),
        "malformed settings codes must fail before device creation");
    Require(!Parse({ "uvsr-engine.exe", "-unknown" }, options, error) &&
            error.find("unknown command-line option") != std::string::npos,
        "unknown options must fail closed");

    Require(Parse({ "uvsr-engine.exe", "Bistro", "San Miguel" },
            options, error) && options.sceneName == "San Miguel",
        "ordinary scene arguments must retain left-to-right behavior");

#if defined(UVSR_BUILD_TESTING)
    Require(Parse({
            "uvsr-engine.exe", "-debug", "--verify-settings-contract"
        }, options, error) && options.debugValidation,
        "developer diagnostics must compose with debug validation");
#else
    Require(!Parse({ "uvsr-engine.exe", "-debug" }, options, error),
        "production command lines must not expose developer validation");
#endif

    std::cout << "UVSR command-line validation passed\n";
    return EXIT_SUCCESS;
}
