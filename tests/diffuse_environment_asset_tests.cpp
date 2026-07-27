#include "diffuse_environment_math.h"
#include "image_based_lighting_sources.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

#ifndef UVSR_TEST_ENVIRONMENT_DIRECTORY
#error UVSR_TEST_ENVIRONMENT_DIRECTORY must name the environment asset root.
#endif

namespace
{
    constexpr dm::float3 LuminanceWeights(
        0.2126f, 0.7152f, 0.0722f);
    constexpr std::array<dm::float3, 6> CardinalDirections = {
        dm::float3(1.f, 0.f, 0.f),
        dm::float3(-1.f, 0.f, 0.f),
        dm::float3(0.f, 1.f, 0.f),
        dm::float3(0.f, -1.f, 0.f),
        dm::float3(0.f, 0.f, 1.f),
        dm::float3(0.f, 0.f, -1.f)
    };

    struct EnvironmentGolden
    {
        const char* label;
        uvsr::ImageBasedLightingSource source;
        int width;
        int height;
        float expectedDefaultExposureStops;
        float expectedAverageLuminance;
        std::array<float, 6> expectedCardinalLuminance;
    };

    constexpr std::array<EnvironmentGolden, 6> Environments = {{
        {
            "Kloppenheim 03 Day",
            uvsr::ImageBasedLightingSource::Kloppenheim03Day,
            2048,
            1024,
            -2.75f,
            0.596833f,
            {
                0.848334f,
                0.217871f,
                1.274790f,
                0.241058f,
                0.682905f,
                0.316039f
            }
        },
        {
            "Snow Field 2 Bright Overcast",
            uvsr::ImageBasedLightingSource::SnowField2BrightOvercast,
            2048,
            1024,
            -2.5f,
            0.596266f,
            {
                0.723949f,
                0.514316f,
                0.831480f,
                0.327507f,
                0.626910f,
                0.553433f
            }
        },
        {
            "Farm Field Soft Day",
            uvsr::ImageBasedLightingSource::FarmFieldSoftDay,
            2048,
            1024,
            -3.25f,
            0.915582f,
            {
                0.984211f,
                0.768096f,
                1.365800f,
                0.518125f,
                1.097660f,
                0.759603f
            }
        },
        {
            "Kloppenheim 07 Night",
            uvsr::ImageBasedLightingSource::Kloppenheim07Night,
            2048,
            1024,
            -5.f,
            0.248443f,
            {
                0.350646f,
                0.185071f,
                0.328826f,
                0.133035f,
                0.278766f,
                0.214314f
            }
        },
        {
            "Qwantani Starry Night",
            uvsr::ImageBasedLightingSource::QwantaniStarryNight,
            2048,
            1024,
            -6.5f,
            0.711093f,
            {
                1.297200f,
                0.348247f,
                0.687959f,
                0.337647f,
                1.240820f,
                0.354681f
            }
        },
        {
            "Quadrangle Cloudy Legacy",
            uvsr::ImageBasedLightingSource::QuadrangleCloudyLegacy,
            1024,
            512,
            -3.f,
            0.619093f,
            {
                0.628781f,
                0.625060f,
                1.359170f,
                0.118846f,
                0.489059f,
                0.493639f
            }
        }
    }};

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr
                << "Image-based-lighting asset validation failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    float Luminance(dm::float3 color)
    {
        return dot(color, LuminanceWeights);
    }
}

int main()
{
    const std::filesystem::path environmentDirectory =
        UVSR_TEST_ENVIRONMENT_DIRECTORY;
    static_assert(
        uvsr::ImageBasedLightingSourceCatalog.size() ==
            uint32_t(uvsr::ImageBasedLightingSource::Count));
    Require(
        uvsr::ImageBasedLightingSourceCatalog.size() == 6u,
        "the source catalog contains exactly six imported sources");

    std::set<std::string> catalogNames;
    std::set<std::string> importedPaths;
    uint32_t nightCount = 0u;
    for (const uvsr::ImageBasedLightingSourceInfo& info :
        uvsr::ImageBasedLightingSourceCatalog)
    {
        Require(
            info.displayName && info.displayName[0],
            "every catalog source has a visible unique name");
        Require(
            catalogNames.emplace(info.displayName).second,
            "catalog source names are unique");
        Require(
            std::isfinite(info.defaultExposureStops) &&
                info.defaultExposureStops >= -12.f &&
                info.defaultExposureStops <= 12.f,
            "every source default exposure is finite and in the UI range");
        Require(
            info.relativePath && info.relativePath[0],
            "every imported source has an asset path");
        Require(
            importedPaths.emplace(info.relativePath).second,
            "imported source paths are unique");
        if (info.night)
        {
            ++nightCount;
            Require(
                info.relativePath && info.relativePath[0],
                "night choices are dedicated imported radiance sources");
        }
    }
    Require(
        uvsr::ImageBasedLightingSourceCatalog.size() ==
            Environments.size(),
        "every imported catalog source has an asset golden");
    Require(nightCount == 2u,
        "the catalog contains two dedicated night choices");
    Require(
        &uvsr::GetImageBasedLightingSourceInfo(
            uvsr::ImageBasedLightingSource::Kloppenheim03Day) ==
            &uvsr::ImageBasedLightingSourceCatalog.front(),
        "Kloppenheim 03 is the stable default catalog source");

    for (const EnvironmentGolden& environment : Environments)
    {
        const uvsr::ImageBasedLightingSourceInfo& sourceInfo =
            uvsr::GetImageBasedLightingSourceInfo(environment.source);
        Require(
            std::abs(
                sourceInfo.defaultExposureStops -
                environment.expectedDefaultExposureStops) <= 1e-6f,
            "every imported source retains its calibrated default exposure");
        const std::filesystem::path path =
            environmentDirectory / sourceInfo.relativePath;
        int width = 0;
        int height = 0;
        int sourceChannels = 0;
        float* pixels = stbi_loadf(
            path.string().c_str(),
            &width,
            &height,
            &sourceChannels,
            3);
        Require(
            pixels != nullptr,
            "every checked-in HDRI decodes with stb_image");
        Require(
            width == environment.width &&
                height == environment.height,
            "every checked-in HDRI has its expected 2:1 dimensions");
        Require(
            sourceChannels >= 3,
            "every checked-in HDRI contains RGB radiance");

        const auto projection =
            uvsr::ProjectDiffuseEnvironmentLatLongRgb(
                pixels,
                uint32_t(width),
                uint32_t(height));
        stbi_image_free(pixels);
        Require(
            projection.has_value(),
            "every checked-in HDRI projects to finite positive SH9");

        std::array<float, CardinalDirections.size()>
            cardinalLuminance{};
        for (uint32_t index = 0u;
            index < CardinalDirections.size();
            ++index)
        {
            const dm::float3 response =
                uvsr::EvaluateDiffuseEnvironmentSh(
                    projection->sh,
                    CardinalDirections[index]);
            Require(
                std::isfinite(response.x) &&
                    std::isfinite(response.y) &&
                    std::isfinite(response.z) &&
                    all(response >= dm::float3(0.f)),
                "every cardinal diffuse response is finite and nonnegative");
            cardinalLuminance[index] = Luminance(response);
        }

        const auto [minimum, maximum] = std::minmax_element(
            cardinalLuminance.begin(),
            cardinalLuminance.end());
        Require(
            projection->averageLuminance > 0.f &&
                std::isfinite(projection->averageLuminance),
            "every projected HDRI has finite positive average energy");
        const float defaultScaledAverage =
            projection->averageLuminance *
            std::exp2(sourceInfo.defaultExposureStops);
        Require(
            sourceInfo.night
                ? defaultScaledAverage >= 0.005f &&
                    defaultScaledAverage <= 0.012f
                : defaultScaledAverage >= 0.06f &&
                    defaultScaledAverage <= 0.12f,
            "default exposures keep day and night radiance in calibrated bands");
        Require(
            *maximum - *minimum >
                projection->averageLuminance * 0.02f,
            "every HDRI retains useful cardinal directional contrast");

        {
            Require(
                environment.expectedAverageLuminance > 0.f,
                "every environment has a frozen source-derived golden");
            Require(
                std::abs(
                    projection->averageLuminance -
                    environment.expectedAverageLuminance) <= 5e-4f,
                "the projected HDRI average matches its checked-in golden");
            for (uint32_t index = 0u;
                index < cardinalLuminance.size();
                ++index)
            {
                Require(
                    std::abs(
                        cardinalLuminance[index] -
                        environment.expectedCardinalLuminance[index]) <=
                            1e-3f,
                    "the projected HDRI orientation matches its cardinal goldens");
            }
        }

        std::cout
            << environment.label
            << " average "
            << projection->averageLuminance
            << "; cardinal";
        for (float value : cardinalLuminance)
            std::cout << ' ' << value;
        std::cout << '\n';
    }
    return EXIT_SUCCESS;
}
