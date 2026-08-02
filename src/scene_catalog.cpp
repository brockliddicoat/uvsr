#include "scene_catalog.h"

#include <donut/core/json.h>
#include <donut/core/vfs/VFS.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace
{
    std::string NormalizePath(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }

    std::string ComparisonKey(std::string value)
    {
#ifdef _WIN32
        // Native Windows paths are case-insensitive. Descriptor authors should
        // not be forced to reproduce on-disk casing merely to keep component
        // models out of UVSR's scene picker.
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
#endif
        return value;
    }

    bool EndsWithCaseInsensitive(std::string_view value, std::string_view suffix)
    {
        if (suffix.size() > value.size())
            return false;

        const size_t offset = value.size() - suffix.size();
        for (size_t index = 0; index < suffix.size(); ++index)
        {
            const unsigned char left = static_cast<unsigned char>(value[offset + index]);
            const unsigned char right = static_cast<unsigned char>(suffix[index]);
            if (std::tolower(left) != std::tolower(right))
                return false;
        }
        return true;
    }

    bool IsDescriptor(std::string_view fileName)
    {
        return EndsWithCaseInsensitive(fileName, ".scene.json");
    }

    bool HasVisibleText(const std::string& text)
    {
        return std::any_of(text.begin(), text.end(), [](unsigned char character)
        {
            return !std::isspace(character);
        });
    }

    std::optional<std::array<float, 3>> ReadFiniteFloat3(
        const Json::Value& value)
    {
        if (!value.isArray() || value.size() != 3u)
            return std::nullopt;

        std::array<float, 3> result{};
        for (Json::ArrayIndex index = 0u; index < 3u; ++index)
        {
            if (!value[index].isNumeric())
                return std::nullopt;

            result[index] = value[index].asFloat();
            if (!std::isfinite(result[index]))
                return std::nullopt;
        }
        return result;
    }

    double LengthSquared(const std::array<float, 3>& value)
    {
        return double(value[0]) * double(value[0]) +
            double(value[1]) * double(value[1]) +
            double(value[2]) * double(value[2]);
    }

    std::optional<std::array<float, 3>> NormalizeCameraVector(
        const std::array<float, 3>& value)
    {
        const double lengthSquared = LengthSquared(value);
        if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-8)
            return std::nullopt;

        const double inverseLength = 1.0 / std::sqrt(lengthSquared);
        return std::array<float, 3>{
            float(double(value[0]) * inverseLength),
            float(double(value[1]) * inverseLength),
            float(double(value[2]) * inverseLength)
        };
    }

    double CrossLengthSquared(
        const std::array<float, 3>& left,
        const std::array<float, 3>& right)
    {
        const double x =
            double(left[1]) * double(right[2]) -
            double(left[2]) * double(right[1]);
        const double y =
            double(left[2]) * double(right[0]) -
            double(left[0]) * double(right[2]);
        const double z =
            double(left[0]) * double(right[1]) -
            double(left[1]) * double(right[0]);
        return x * x + y * y + z * z;
    }

    std::optional<uvsr::SceneInitialCamera> ReadInitialCamera(
        const Json::Value& document)
    {
        const Json::Value& camera = document["initialCamera"];
        if (!camera.isObject())
            return std::nullopt;

        const auto position = ReadFiniteFloat3(camera["position"]);
        const auto direction = ReadFiniteFloat3(camera["direction"]);
        const auto up = ReadFiniteFloat3(camera["up"]);
        const auto normalizedDirection =
            direction ? NormalizeCameraVector(*direction) : std::nullopt;
        const auto normalizedUp =
            up ? NormalizeCameraVector(*up) : std::nullopt;
        if (!position || !normalizedDirection || !normalizedUp ||
            CrossLengthSquared(*normalizedDirection, *normalizedUp) <= 1e-8)
        {
            return std::nullopt;
        }

        float verticalFovDegrees = 60.f;
        const Json::Value& verticalFov = camera["verticalFovDegrees"];
        if (!verticalFov.isNull())
        {
            if (!verticalFov.isNumeric())
                return std::nullopt;
            verticalFovDegrees = verticalFov.asFloat();
            if (!std::isfinite(verticalFovDegrees) ||
                verticalFovDegrees <= 1.f ||
                verticalFovDegrees >= 179.f)
            {
                return std::nullopt;
            }
        }

        return uvsr::SceneInitialCamera{
            *position,
            *normalizedDirection,
            *normalizedUp,
            verticalFovDegrees
        };
    }
}

std::string uvsr::MakeSceneDisplayName(
    const std::filesystem::path& sceneDirectory,
    const std::filesystem::path& fileName)
{
    const std::filesystem::path normalizedDirectory = sceneDirectory.lexically_normal();
    const std::filesystem::path normalizedFileName = fileName.lexically_normal();

    auto directoryComponent = normalizedDirectory.begin();
    auto fileComponent = normalizedFileName.begin();
    for (; directoryComponent != normalizedDirectory.end(); ++directoryComponent, ++fileComponent)
    {
        if (fileComponent == normalizedFileName.end()
            || ComparisonKey(directoryComponent->generic_string())
                != ComparisonKey(fileComponent->generic_string()))
        {
            return normalizedFileName.generic_string();
        }
    }

    std::filesystem::path relative;
    for (; fileComponent != normalizedFileName.end(); ++fileComponent)
        relative /= *fileComponent;

    return relative.empty() ? normalizedFileName.generic_string() : relative.generic_string();
}

std::vector<uvsr::SceneCatalogEntry> uvsr::BuildSceneCatalog(
    donut::vfs::IFileSystem& fileSystem,
    const std::filesystem::path& sceneDirectory,
    const std::vector<std::string>& discoveredSceneFiles)
{
    // Donut deliberately exposes every loadable file it discovers. UVSR adds
    // only product-level catalog behavior here: a descriptor can name its
    // launcher and claim its component models, while unrelated glTF/GLB files
    // remain available exactly as before.
    std::unordered_map<std::string, std::string> descriptorDisplayNames;
    std::unordered_map<std::string, SceneInitialCamera> descriptorInitialCameras;
    std::unordered_set<std::string> descriptorComponents;

    for (const std::string& discovered : discoveredSceneFiles)
    {
        const std::string normalized = NormalizePath(discovered);
        if (!IsDescriptor(normalized))
            continue;

        Json::Value document;
        if (!donut::json::LoadFromFile(fileSystem, normalized, document) || !document.isObject())
            continue;

        const Json::Value& displayName = document["displayName"];
        if (displayName.isString() && HasVisibleText(displayName.asString()))
            descriptorDisplayNames.emplace(ComparisonKey(normalized), displayName.asString());

        if (const auto initialCamera = ReadInitialCamera(document))
        {
            descriptorInitialCameras.emplace(
                ComparisonKey(normalized),
                *initialCamera);
        }

        const Json::Value& models = document["models"];
        if (!models.isArray())
            continue;

        const std::filesystem::path descriptorDirectory =
            std::filesystem::path(normalized).parent_path();
        for (const Json::Value& model : models)
        {
            if (!model.isString() || model.asString().empty())
                continue;

            const std::string component = NormalizePath(
                descriptorDirectory / std::filesystem::path(model.asString()));
            descriptorComponents.insert(ComparisonKey(component));
        }
    }

    std::vector<SceneCatalogEntry> catalog;
    std::unordered_set<std::string> includedFiles;
    catalog.reserve(discoveredSceneFiles.size());

    for (const std::string& discovered : discoveredSceneFiles)
    {
        const std::string normalized = NormalizePath(discovered);
        const std::string key = ComparisonKey(normalized);
        const bool descriptor = IsDescriptor(normalized);

        // A descriptor is always a launcher, even if another descriptor lists
        // it accidentally. Donut's model array does not support nested scene
        // descriptors, so hiding one would only make the bad reference harder
        // to diagnose.
        if (!descriptor && descriptorComponents.find(key) != descriptorComponents.end())
            continue;
        if (!includedFiles.insert(key).second)
            continue;

        SceneCatalogEntry entry;
        entry.FileName = normalized;
        const auto descriptorName = descriptorDisplayNames.find(key);
        entry.DisplayName = descriptorName != descriptorDisplayNames.end()
            ? descriptorName->second
            : MakeSceneDisplayName(sceneDirectory, normalized);
        const auto descriptorCamera = descriptorInitialCameras.find(key);
        if (descriptorCamera != descriptorInitialCameras.end())
            entry.InitialCamera = descriptorCamera->second;
        catalog.push_back(std::move(entry));
    }

    std::sort(catalog.begin(), catalog.end(), [](const SceneCatalogEntry& left, const SceneCatalogEntry& right)
    {
        const std::string leftName = ComparisonKey(left.DisplayName);
        const std::string rightName = ComparisonKey(right.DisplayName);
        if (leftName != rightName)
            return leftName < rightName;
        return ComparisonKey(left.FileName) < ComparisonKey(right.FileName);
    });

    return catalog;
}

const uvsr::SceneCatalogEntry* uvsr::FindSceneCatalogEntry(
    const std::vector<SceneCatalogEntry>& catalog,
    std::string_view fileName)
{
    const std::string key = ComparisonKey(NormalizePath(std::filesystem::path(std::string(fileName))));
    const auto match = std::find_if(catalog.begin(), catalog.end(), [&key](const SceneCatalogEntry& entry)
    {
        return ComparisonKey(entry.FileName) == key;
    });
    return match == catalog.end() ? nullptr : &*match;
}
