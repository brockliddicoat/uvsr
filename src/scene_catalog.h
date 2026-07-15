#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace donut::vfs
{
    class IFileSystem;
}

namespace uvsr
{
    struct SceneCatalogEntry
    {
        // FileName is the normalized native path passed to Donut's scene
        // loader. DisplayName is UI-only metadata and is never used to locate
        // an asset, so friendly labels cannot make command-line paths
        // ambiguous.
        std::string FileName;
        std::string DisplayName;
    };

    std::string MakeSceneDisplayName(
        const std::filesystem::path& sceneDirectory,
        const std::filesystem::path& fileName);

    std::vector<SceneCatalogEntry> BuildSceneCatalog(
        donut::vfs::IFileSystem& fileSystem,
        const std::filesystem::path& sceneDirectory,
        const std::vector<std::string>& discoveredSceneFiles);

    const SceneCatalogEntry* FindSceneCatalogEntry(
        const std::vector<SceneCatalogEntry>& catalog,
        std::string_view fileName);
}
