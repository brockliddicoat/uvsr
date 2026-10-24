#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace donut::vfs
{
    class IFileSystem;
}

namespace uvsr
{
    struct SceneInitialCamera
    {
        std::array<float, 3> Position{};
        std::array<float, 3> Direction{ 0.f, 0.f, -1.f };
        std::array<float, 3> Up{ 0.f, 1.f, 0.f };
        float VerticalFovDegrees = 60.f;
    };

    struct SceneCatalogEntry
    {
        // FileName is the normalized native path passed to Donut's scene
        // loader. DisplayName is UI-only metadata and is never used to locate
        // an asset, so friendly labels cannot make command-line paths
        // ambiguous.
        std::string FileName;
        std::string DisplayName;
        std::optional<SceneInitialCamera> InitialCamera;
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
