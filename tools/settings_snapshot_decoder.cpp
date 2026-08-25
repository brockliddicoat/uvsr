#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    void PrintUsage()
    {
        std::cerr << "usage: uvsr-settings-snapshot <code> "
            "[--catalog <path>] [--json]\n";
    }
}

int main(int argumentCount, char** arguments)
{
    using namespace uvsr;
    if (argumentCount < 2)
    {
        PrintUsage();
        return EXIT_FAILURE;
    }

    const std::string code(arguments[1]);
    bool json = false;
    std::vector<std::filesystem::path> catalogs;
    for (int index = 2; index < argumentCount; ++index)
    {
        const std::string_view argument(arguments[index]);
        if (argument == "--json")
        {
            json = true;
        }
        else if (argument == "--catalog" && index + 1 < argumentCount)
        {
            catalogs.emplace_back(arguments[++index]);
        }
        else
        {
            PrintUsage();
            return EXIT_FAILURE;
        }
    }

    try
    {
        if (catalogs.empty())
            catalogs = GetDefaultSettingsSnapshotCatalogPaths(code.substr(0u, 4u));
        const DecodedSettings settings = DecodeSettingsSnapshot(code, catalogs);
        if (json)
        {
            std::cout << FormatDecodedSettingsJson(settings);
        }
        else
        {
            for (const auto& [name, value] : settings)
                std::cout << name << '=' << value << '\n';
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
