#include "sponza_camera_preset.h"

#include <donut/core/json.h>
#include <donut/core/vfs/VFS.h>

#include <cctype>
#include <string>
#include <string_view>

namespace
{
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
}

const uvsr::SponzaCameraPreset& uvsr::GetDefaultSponzaCameraPreset()
{
    // Keep the user's rounded 45-degree pose as the sole stored location. The
    // supplied +/-0.7 direction and right components are normalized so the
    // view basis remains orthonormal instead of scaling the rendered image.
    static const SponzaCameraPreset preset{
        "intel-pbr-sponza-courtyard-simplified-v1",
        "Benchmark Position 1",
        donut::math::float3(11.f, 7.7f, -2.2f),
        donut::math::float3(-0.707106769f, 0.f, 0.707106769f),
        donut::math::float3(0.f, 1.f, 0.f),
        donut::math::float3(-0.707106769f, 0.f, -0.707106769f),
        60.f,
        1920u,
        1080u
    };
    return preset;
}

const char* uvsr::GetSponzaCameraLocationLabel(SponzaCameraLocation location)
{
    if (const SponzaCameraPreset* preset = FindSponzaCameraPreset(location))
        return preset->Label;

    return "Free";
}

const uvsr::SponzaCameraPreset* uvsr::FindSponzaCameraPreset(
    SponzaCameraLocation location)
{
    if (location == SponzaCameraLocation::SimplifiedApproximation)
        return &GetDefaultSponzaCameraPreset();

    return nullptr;
}

uvsr::SponzaCameraLocation uvsr::ResolveSponzaCameraLocation(
    SponzaCameraLocation selectedLocation,
    bool benchmarkCameraRequested)
{
    if (benchmarkCameraRequested ||
        selectedLocation != SponzaCameraLocation::SimplifiedApproximation)
    {
        return SponzaCameraLocation::SimplifiedApproximation;
    }

    return selectedLocation;
}

bool uvsr::IsSponzaCameraAtPreset(
    const SponzaCameraPreset& preset,
    donut::math::float3 position,
    donut::math::float3 direction,
    donut::math::float3 up)
{
    constexpr float PoseEpsilon = 1e-5f;
    constexpr float PoseEpsilonSquared = PoseEpsilon * PoseEpsilon;
    return donut::math::lengthSquared(position - preset.Position) <= PoseEpsilonSquared &&
        donut::math::lengthSquared(direction - preset.Direction) <= PoseEpsilonSquared &&
        donut::math::lengthSquared(up - preset.Up) <= PoseEpsilonSquared;
}

const uvsr::SponzaCameraPreset* uvsr::FindStandardSponzaCameraPreset(
    donut::vfs::IFileSystem& fileSystem,
    const std::filesystem::path& currentScene)
{
    const std::string normalizedScene = currentScene.lexically_normal().generic_string();
    if (!EndsWithCaseInsensitive(normalizedScene, ".scene.json"))
        return nullptr;

    Json::Value document;
    if (!donut::json::LoadFromFile(fileSystem, normalizedScene, document) || !document.isObject())
        return nullptr;

    const Json::Value& cameraPreset = document["cameraPreset"];
    const SponzaCameraPreset& preset = GetDefaultSponzaCameraPreset();
    if (cameraPreset.isString() && cameraPreset.asString() == preset.Id)
        return &preset;

    return nullptr;
}
