#include "settings_snapshot_schema.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace
{
    void PrintHex16(std::uint64_t value)
    {
        std::cout << std::hex << std::nouppercase << std::setfill('0')
                  << std::setw(16) << value;
    }
}

int main(int argumentCount, char** arguments)
{
    using namespace uvsr;

    bool checkOnly = false;
    bool json = false;
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument(arguments[index]);
        if (argument == "--check")
            checkOnly = true;
        else if (argument == "--json")
            json = true;
        else
        {
            std::cerr << "unknown argument: " << argument << '\n';
            return EXIT_FAILURE;
        }
    }

    const SettingsSnapshotSchemaFingerprint fingerprint =
        CurrentSettingsSnapshotSchemaFingerprint;
    const std::uint16_t registeredVersion =
        ResolveSettingsSnapshotSchemaVersion(fingerprint);
    const std::uint16_t suggestedVersion = registeredVersion != 0u
        ? registeredVersion
        : GetNextAvailableSettingsSnapshotSchemaVersion();
    if (suggestedVersion == 0u)
    {
        std::cerr << "settings snapshot schema version space is exhausted\n";
        return EXIT_FAILURE;
    }

    if (json)
    {
        std::cout << "{\"fingerprint\":\"";
        PrintHex16(fingerprint.high);
        PrintHex16(fingerprint.low);
        std::cout << "\",\"registered_version\":\""
                  << std::setw(4) << registeredVersion
                  << "\",\"suggested_version\":\""
                  << std::setw(4) << suggestedVersion << "\"}\n";
    }
    else
    {
        std::cout << "fingerprint=";
        PrintHex16(fingerprint.high);
        PrintHex16(fingerprint.low);
        std::cout << " registered-version=" << std::setw(4)
                  << registeredVersion << " suggested-version="
                  << std::setw(4) << suggestedVersion << '\n';
        if (registeredVersion == 0u)
        {
            std::cout << "UVSR_SETTINGS_SNAPSHOT_SCHEMA_VERSION(0x"
                      << std::setw(4) << suggestedVersion << "u, 0x";
            PrintHex16(fingerprint.high);
            std::cout << "ull, 0x";
            PrintHex16(fingerprint.low);
            std::cout << "ull)\n";
        }
    }

    return checkOnly && registeredVersion == 0u
        ? EXIT_FAILURE
        : EXIT_SUCCESS;
}
