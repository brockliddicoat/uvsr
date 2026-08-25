#include "engine_identity.h"

#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    [[nodiscard]] constexpr bool IsLowerHex(std::string_view value)
    {
        if (value.empty())
            return false;
        for (const char character : value)
        {
            if (!((character >= '0' && character <= '9') ||
                    (character >= 'a' && character <= 'f')))
            {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string FormatVersion(uvsr::EngineVersion version)
    {
        return std::to_string(version.major) + "." +
            std::to_string(version.minor) + "." +
            std::to_string(version.patch) + "." +
            std::to_string(version.build);
    }

    void WriteText(
        const std::filesystem::path& path,
        const std::string& text)
    {
        std::filesystem::create_directories(path.parent_path());
        const std::filesystem::path temporary = path.string() + ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
                throw std::runtime_error("cannot write " + temporary.string());
            stream << text;
            if (!stream)
                throw std::runtime_error("cannot finish " + temporary.string());
        }

        std::error_code error;
        if (std::filesystem::is_regular_file(path, error))
        {
            std::string currentText;
            std::string candidateText;
            {
                std::ifstream current(path, std::ios::binary);
                std::ifstream candidate(temporary, std::ios::binary);
                currentText.assign(
                    std::istreambuf_iterator<char>(current),
                    std::istreambuf_iterator<char>());
                candidateText.assign(
                    std::istreambuf_iterator<char>(candidate),
                    std::istreambuf_iterator<char>());
            }
            if (currentText == candidateText)
            {
                std::filesystem::remove(temporary);
                return;
            }
        }
        error.clear();
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error)
            throw std::runtime_error("cannot replace " + path.string());
    }

    [[nodiscard]] constexpr bool IsCanonicalSourceIdentity(
        std::string_view value) noexcept
    {
        if (value.size() == 40u)
            return IsLowerHex(value);
        constexpr std::string_view DirtyMarker = "-dirty-";
        return value.size() == 40u + DirtyMarker.size() + 12u &&
            IsLowerHex(value.substr(0u, 40u)) &&
            value.substr(40u, DirtyMarker.size()) == DirtyMarker &&
            IsLowerHex(value.substr(40u + DirtyMarker.size()));
    }

    static_assert(IsCanonicalSourceIdentity(
        "0123456789abcdef0123456789abcdef01234567"));
    static_assert(IsCanonicalSourceIdentity(
        "0123456789abcdef0123456789abcdef01234567-dirty-89abcdef0123"));
    static_assert(!IsCanonicalSourceIdentity(
        "0123456789abcdef0123456789abcdef01234567-dirty-89ABCDEF0123"));

    [[nodiscard]] constexpr bool IsCanonicalConfiguration(
        std::string_view value) noexcept
    {
        return value == "Release" || value == "Debug" ||
            value == "RelWithDebInfo" || value == "MinSizeRel";
    }

    static_assert(IsCanonicalConfiguration("Release"));
    static_assert(IsCanonicalConfiguration("Debug"));
    static_assert(!IsCanonicalConfiguration("release"));

    [[nodiscard]] std::string BuildCpp(
        std::string_view sourceIdentity,
        bool production,
        std::string_view configuration)
    {
        const std::string hash(uvsr::GetSettingsNumberHashText());
        const std::string version = FormatVersion(uvsr::CurrentEngineVersion);
        const std::string productVersion = version + "+" + hash;
        const std::string commit(sourceIdentity.substr(0u, 40u));
        const std::string identity(sourceIdentity);
        const std::string clean = sourceIdentity.size() == 40u
            ? "true" : "false";
        return
            "#include \"build_identity.h\"\n\n"
            "namespace uvsr\n{\n"
            "    std::string_view GetBuiltSourceCommit() noexcept\n"
            "    {\n        return \"" + commit + "\";\n    }\n\n"
            "    std::string_view GetBuiltSourceIdentity() noexcept\n"
            "    {\n        return \"" + identity + "\";\n    }\n\n"
            "    bool IsBuiltSourceTreeClean() noexcept\n"
            "    {\n        return " + clean + ";\n    }\n\n"
            "    bool IsBuiltProduction() noexcept\n"
            "    {\n        return " +
                std::string(production ? "true" : "false") + ";\n    }\n\n"
            "    std::string_view GetBuiltConfiguration() noexcept\n"
            "    {\n        return \"" + std::string(configuration) +
                "\";\n    }\n\n"
            "    std::string_view GetBuiltSettingsNumberHash() noexcept\n"
            "    {\n        return \"" + hash + "\";\n    }\n\n"
            "    std::string_view GetBuiltEngineVersion() noexcept\n"
            "    {\n        return \"" + version + "\";\n    }\n\n"
            "    std::string_view GetBuiltEngineProductVersion() noexcept\n"
            "    {\n        return \"" + productVersion + "\";\n    }\n"
            "}\n";
    }

    [[nodiscard]] std::string BuildResource(
        std::string_view sourceIdentity,
        bool production,
        std::string_view configuration)
    {
        const uvsr::EngineVersion version = uvsr::CurrentEngineVersion;
        const std::string versionText = FormatVersion(version);
        const std::string hash(uvsr::GetSettingsNumberHashText());
        const std::string commit(sourceIdentity.substr(0u, 40u));
        return
            "#include <winver.h>\n\n"
            "1 VERSIONINFO\n"
            "FILEVERSION " + std::to_string(version.major) + "," +
                std::to_string(version.minor) + "," +
                std::to_string(version.patch) + "," +
                std::to_string(version.build) + "\n"
            "PRODUCTVERSION " + std::to_string(version.major) + "," +
                std::to_string(version.minor) + "," +
                std::to_string(version.patch) + "," +
                std::to_string(version.build) + "\n"
            "FILEFLAGSMASK VS_FFI_FILEFLAGSMASK\n"
            "FILEFLAGS 0\n"
            "FILEOS VOS_NT_WINDOWS32\n"
            "FILETYPE VFT_APP\n"
            "FILESUBTYPE VFT2_UNKNOWN\n"
            "BEGIN\n"
            "  BLOCK \"StringFileInfo\"\n"
            "  BEGIN\n"
            "    BLOCK \"040904b0\"\n"
            "    BEGIN\n"
            "      VALUE \"CompanyName\", \"UVSR\"\n"
            "      VALUE \"FileDescription\", \"UVSR Engine\"\n"
            "      VALUE \"FileVersion\", \"" + versionText + "\"\n"
            "      VALUE \"InternalName\", \"uvsr-engine\"\n"
            "      VALUE \"OriginalFilename\", \"uvsr-engine.exe\"\n"
            "      VALUE \"ProductName\", \"UVSR Engine\"\n"
            "      VALUE \"ProductVersion\", \"" + versionText + "+" + hash + "\"\n"
            "      VALUE \"BuildConfiguration\", \"" +
                std::string(configuration) + "\"\n"
            "      VALUE \"ProductionBuild\", \"" +
                std::string(production ? "true" : "false") + "\"\n"
            "      VALUE \"SourceCommit\", \"" + commit + "\"\n"
            "      VALUE \"SourceIdentity\", \"" +
                std::string(sourceIdentity) + "\"\n"
            "      VALUE \"SettingsNumberHash\", \"" + hash + "\"\n"
            "    END\n"
            "  END\n"
            "  BLOCK \"VarFileInfo\"\n"
            "  BEGIN\n"
            "    VALUE \"Translation\", 0x0409, 1200\n"
            "  END\n"
            "END\n";
    }
}

int main(int argumentCount, char** arguments)
{
    if (argumentCount != 11 ||
        std::string_view(arguments[1]) != "--cpp" ||
        std::string_view(arguments[3]) != "--rc" ||
        std::string_view(arguments[5]) != "--source-identity" ||
        std::string_view(arguments[7]) != "--production" ||
        std::string_view(arguments[9]) != "--configuration")
    {
        std::cerr << "usage: uvsr-build-identity --cpp <path> --rc <path> "
            "--source-identity <commit[-dirty-digest]> "
            "--production <true|false> "
            "--configuration <Release|Debug|RelWithDebInfo|MinSizeRel>\n";
        return EXIT_FAILURE;
    }

    const std::string_view sourceIdentity(arguments[6]);
    const std::string_view productionText(arguments[8]);
    const std::string_view configuration(arguments[10]);
    if (!IsCanonicalSourceIdentity(sourceIdentity))
    {
        std::cerr << "source identity must be a 40-digit lowercase commit or "
            "that commit plus -dirty- and 12 lowercase hex digits\n";
        return EXIT_FAILURE;
    }
    if (productionText != "true" && productionText != "false")
    {
        std::cerr << "production must be exactly true or false\n";
        return EXIT_FAILURE;
    }
    if (!IsCanonicalConfiguration(configuration))
    {
        std::cerr << "configuration must be exactly Release, Debug, "
            "RelWithDebInfo, or MinSizeRel\n";
        return EXIT_FAILURE;
    }
    const bool production = productionText == "true";
    if (production && sourceIdentity.size() != 40u)
    {
        std::cerr << "production identity requires a clean source tree\n";
        return EXIT_FAILURE;
    }
    if (production && configuration != "Release")
    {
        std::cerr << "production identity requires Release configuration\n";
        return EXIT_FAILURE;
    }

    try
    {
        WriteText(arguments[2],
            BuildCpp(sourceIdentity, production, configuration));
        WriteText(arguments[4],
            BuildResource(sourceIdentity, production, configuration));
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "settings-hash=" << uvsr::GetSettingsNumberHashText()
              << " engine-version="
              << FormatVersion(uvsr::CurrentEngineVersion) << '\n';
    return EXIT_SUCCESS;
}
