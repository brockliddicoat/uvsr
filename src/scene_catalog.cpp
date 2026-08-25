#include "scene_catalog.h"

#include "json_document.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <limits>
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
        const uvsr::json::Value* value)
    {
        using Kind = uvsr::json::Value::Kind;
        if (value == nullptr || value->kind != Kind::Array ||
            value->array.size() != 3u)
        {
            return std::nullopt;
        }

        std::array<float, 3> result{};
        for (size_t index = 0u; index < 3u; ++index)
        {
            const uvsr::json::Value& component = value->array[index];
            if (component.kind != Kind::Number ||
                component.number < -double(std::numeric_limits<float>::max()) ||
                component.number > double(std::numeric_limits<float>::max()))
            {
                return std::nullopt;
            }

            result[index] = static_cast<float>(component.number);
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
        const uvsr::json::Value& document)
    {
        using Kind = uvsr::json::Value::Kind;
        const uvsr::json::Value* camera = document.Find("initialCamera");
        if (camera == nullptr || camera->kind != Kind::Object)
            return std::nullopt;

        const auto position = ReadFiniteFloat3(camera->Find("position"));
        const auto direction = ReadFiniteFloat3(camera->Find("direction"));
        const auto up = ReadFiniteFloat3(camera->Find("up"));
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
        const uvsr::json::Value* verticalFov =
            camera->Find("verticalFovDegrees");
        if (verticalFov != nullptr && verticalFov->kind != Kind::Null)
        {
            if (verticalFov->kind != Kind::Number ||
                verticalFov->number < -double(std::numeric_limits<float>::max()) ||
                verticalFov->number > double(std::numeric_limits<float>::max()))
            {
                return std::nullopt;
            }
            verticalFovDegrees = static_cast<float>(verticalFov->number);
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
    std::filesystem::path normalizedDirectory = sceneDirectory.lexically_normal();
    if (normalizedDirectory.has_relative_path() &&
        normalizedDirectory.filename().empty())
    {
        normalizedDirectory = normalizedDirectory.parent_path();
    }
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
    const std::filesystem::path& sceneDirectory,
    const std::vector<std::string>& discoveredSceneFiles)
{
    // A descriptor can name its launcher and claim its component models, while
    // unrelated glTF/GLB files remain directly available.
    std::unordered_map<std::string, std::string> descriptorDisplayNames;
    std::unordered_map<std::string, SceneInitialCamera> descriptorInitialCameras;
    std::unordered_set<std::string> descriptorComponents;

    for (const std::string& discovered : discoveredSceneFiles)
    {
        const std::string normalized = NormalizePath(discovered);
        if (!IsDescriptor(normalized))
            continue;

        json::Value document;
        try
        {
            document = json::Read(normalized);
        }
        catch (const std::exception&)
        {
            continue;
        }
        if (document.kind != json::Value::Kind::Object)
            continue;

        const json::Value* displayName = document.Find("displayName");
        if (displayName != nullptr &&
            displayName->kind == json::Value::Kind::String &&
            HasVisibleText(displayName->string))
        {
            descriptorDisplayNames.emplace(
                ComparisonKey(normalized), displayName->string);
        }

        if (const auto initialCamera = ReadInitialCamera(document))
        {
            descriptorInitialCameras.emplace(
                ComparisonKey(normalized),
                *initialCamera);
        }

        const json::Value* models = document.Find("models");
        if (models == nullptr || models->kind != json::Value::Kind::Array)
            continue;

        const std::filesystem::path descriptorDirectory =
            std::filesystem::path(normalized).parent_path();
        for (const json::Value& model : models->array)
        {
            if (model.kind != json::Value::Kind::String || model.string.empty())
                continue;

            const std::string component = NormalizePath(
                descriptorDirectory / std::filesystem::path(model.string));
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

        // A descriptor is always a launcher. Hiding one because another
        // descriptor listed it would make the bad nested reference harder to
        // diagnose.
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
