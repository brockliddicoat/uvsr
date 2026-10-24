#include "scene_catalog.h"

#include <donut/core/vfs/VFS.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    class TemporaryDirectory
    {
    private:
        std::filesystem::path m_Path;

    public:
        TemporaryDirectory()
        {
            const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            m_Path = std::filesystem::temp_directory_path()
                / ("uvsr_scene_catalog_" + std::to_string(nonce));
            std::filesystem::create_directories(m_Path / "intel_sponza/components");
            std::filesystem::create_directories(m_Path / "intel_sponza/components/ivy");
            std::filesystem::create_directories(m_Path / "standalone");
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_Path, error);
        }

        const std::filesystem::path& GetPath() const
        {
            return m_Path;
        }
    };

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << text;
        Require(stream.good(), "failed to write scene-catalog test fixture");
    }

    std::string Generic(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    const uvsr::SceneCatalogEntry* FindByDisplayName(
        const std::vector<uvsr::SceneCatalogEntry>& catalog,
        const std::string& displayName)
    {
        const auto match = std::find_if(catalog.begin(), catalog.end(), [&displayName](const auto& entry)
        {
            return entry.DisplayName == displayName;
        });
        return match == catalog.end() ? nullptr : &*match;
    }

    std::vector<std::string> Flatten(const std::vector<uvsr::SceneCatalogEntry>& catalog)
    {
        std::vector<std::string> result;
        for (const auto& entry : catalog)
        {
            result.push_back(entry.DisplayName);
            result.push_back(entry.FileName);
        }
        return result;
    }
}

int main()
{
    try
    {
        TemporaryDirectory temporary;
        const std::filesystem::path root = temporary.GetPath();
        const std::filesystem::path intel = root / "intel_sponza";
        const std::filesystem::path components = intel / "components";

        const std::filesystem::path part1 = components / "part1.glb";
        const std::filesystem::path part2 = components / "part2.glb";
        const std::filesystem::path curtains = components / "curtains.glb";
        const std::filesystem::path ivy1 = components / "ivy/ivy1.glb";
        const std::filesystem::path ivy2 = components / "ivy/ivy2.glb";
        const std::filesystem::path mainDescriptor = intel / "main.scene.json";
        const std::filesystem::path plainDescriptor = intel / "plain.scene.json";
        const std::filesystem::path fallbackDescriptor = intel / "fallback.scene.json";
        const std::filesystem::path standalone = root / "standalone/example.glb";

        WriteText(mainDescriptor,
            R"({"displayName":"Intel PBR Sponza","models":["components/part1.glb","components/../components/part2.glb","components/curtains.glb","components/ivy/ivy1.glb","components/ivy/ivy2.glb"],"graph":[]})");
        WriteText(plainDescriptor,
            R"({"displayName":"Intel PBR Sponza - Plain","models":["components/part1.glb","components/part2.glb"],"graph":[]})");
        WriteText(fallbackDescriptor,
            R"({"displayName":"   ","models":[],"graph":[]})");

        std::vector<std::string> discovered = {
            Generic(part2),
            Generic(plainDescriptor),
            Generic(ivy2),
            Generic(standalone),
            Generic(mainDescriptor),
            Generic(curtains),
            Generic(fallbackDescriptor),
            Generic(ivy1),
            Generic(part1),
            Generic(part1), // Duplicate discovery must not duplicate a picker entry.
        };

        donut::vfs::NativeFileSystem fileSystem;
        const auto catalog = uvsr::BuildSceneCatalog(fileSystem, root, discovered);

        Require(catalog.size() == 4, "catalog must hide all descriptor-owned components");
        Require(FindByDisplayName(catalog, "Intel PBR Sponza") != nullptr,
            "main descriptor must use its friendly display name");
        Require(FindByDisplayName(catalog, "Intel PBR Sponza - Plain") != nullptr,
            "plain descriptor must remain visible when it shares architecture components");
        Require(FindByDisplayName(catalog, "intel_sponza/fallback.scene.json") != nullptr,
            "empty displayName must fall back to the relative descriptor path");
        Require(FindByDisplayName(catalog, "standalone/example.glb") != nullptr,
            "unreferenced standalone GLB must remain visible");

        Require(uvsr::FindSceneCatalogEntry(catalog, Generic(mainDescriptor)) != nullptr,
            "exact main-descriptor lookup must succeed independently of similar names");
        Require(uvsr::FindSceneCatalogEntry(catalog, Generic(part1)) == nullptr,
            "hidden component must not resolve as a catalog entry");
        Require(uvsr::FindSceneCatalogEntry(catalog, Generic(ivy1)) == nullptr,
            "nested ivy component must not resolve as a catalog entry");

        Require(uvsr::MakeSceneDisplayName(root, standalone) == "standalone/example.glb",
            "in-tree scene display names must remain relative");
        const std::filesystem::path similarlyPrefixedDirectory =
            root.parent_path() / (root.filename().string() + "_backup");
        const std::filesystem::path externalScene = similarlyPrefixedDirectory / "external.glb";
        Require(uvsr::MakeSceneDisplayName(root, externalScene) == Generic(externalScene),
            "a sibling path sharing the scene-directory prefix must remain external");

        std::reverse(discovered.begin(), discovered.end());
        const auto reversedCatalog = uvsr::BuildSceneCatalog(fileSystem, root, discovered);
        Require(Flatten(catalog) == Flatten(reversedCatalog),
            "catalog order must not depend on VFS enumeration order");

#ifdef _WIN32
        std::string upperCasePath = Generic(mainDescriptor);
        std::transform(upperCasePath.begin(), upperCasePath.end(), upperCasePath.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::toupper(c));
        });
        Require(uvsr::FindSceneCatalogEntry(catalog, upperCasePath) != nullptr,
            "Windows catalog lookup must follow native case-insensitive path semantics");
#endif

        std::cout << "scene catalog reference tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "scene catalog reference tests failed: " << error.what() << '\n';
        return 1;
    }
}
