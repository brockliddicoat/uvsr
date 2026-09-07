#include "renderer_environment_math.h"
#include "image_based_lighting_sources.h"
#include "json_document.h"
#include "noise_settings.h"
#include "sha256.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace uvsr;
    using Float3 = DirectX::XMFLOAT3;
    constexpr std::array<Float3, 6> Directions{
        Float3{ 1, 0, 0 }, Float3{ -1, 0, 0 }, Float3{ 0, 1, 0 },
        Float3{ 0, -1, 0 }, Float3{ 0, 0, 1 }, Float3{ 0, 0, -1 }
    };
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    float Luminance(Float3 value) { return value.x * .2126f + value.y * .7152f + value.z * .0722f; }

    struct Golden
    {
        uvsr::ImageBasedLightingSource source;
        const char* label;
        int width, height;
        float exposure, average;
        std::array<float, 6> cardinal;
    };
    using Source = uvsr::ImageBasedLightingSource;
    constexpr Golden Goldens[] = {
        { Source::Kloppenheim03Day, "Day", 2048, 1024, -2.75f, .596833f,
            { .848334f, .217871f, 1.274790f, .241058f, .682905f, .316039f } },
        { Source::SnowField2BrightOvercast, "Bright Overcast", 2048, 1024, -2.5f, .596266f,
            { .723949f, .514316f, .831480f, .327507f, .626910f, .553433f } },
        { Source::FarmFieldSoftDay, "Soft Day", 2048, 1024, -3.25f, .915582f,
            { .984211f, .768096f, 1.365800f, .518125f, 1.097660f, .759603f } },
        { Source::Kloppenheim07Night, "Night", 2048, 1024, -5.f, .248443f,
            { .350646f, .185071f, .328826f, .133035f, .278766f, .214314f } },
        { Source::QwantaniStarryNight, "Starry Night", 2048, 1024, -6.5f, .711093f,
            { 1.297200f, .348247f, .687959f, .337647f, 1.240820f, .354681f } },
        { Source::QuadrangleCloudy, "Cloudy", 1024, 512, -3.f, .619093f,
            { .628781f, .625060f, 1.359170f, .118846f, .489059f, .493639f } }
    };

    void CheckEnvironments(const std::filesystem::path& root)
    {
        using namespace uvsr;
        Require(ImageBasedLightingSourceCatalog.size() == std::size(Goldens),
            "retained environment catalog membership changed");
        std::set<std::string> paths, tokens, labels;
        unsigned nightCount = 0;
        for (const auto& golden : Goldens)
        {
            const auto& info = GetImageBasedLightingSourceInfo(golden.source);
            Require(info.relativePath && *info.relativePath && paths.insert(info.relativePath).second &&
                info.canonicalToken && *info.canonicalToken && tokens.insert(info.canonicalToken).second &&
                info.displayName && labels.insert(info.displayName).second &&
                std::string_view(info.displayName) == golden.label &&
                std::abs(info.defaultExposureStops - golden.exposure) < 1e-6f,
                "environment identity or calibrated exposure changed");
            nightCount += info.night;
            const auto path = root / info.relativePath;
            int width = 0, height = 0, channels = 0;
            float* pixels = stbi_loadf(path.string().c_str(), &width, &height, &channels, 3);
            Require(pixels && width == golden.width && height == golden.height && channels >= 3,
                "retained HDR failed to decode with its exact RGB dimensions");
            const auto projection = ProjectRendererDiffuseEnvironmentLatLongRgb(
                pixels, static_cast<unsigned>(width), static_cast<unsigned>(height));
            stbi_image_free(pixels);
            Require(bool(projection) && std::abs(projection->averageLuminance - golden.average) <= 5e-4f,
                "production HDR projection differs from its source golden");
            std::array<float, 6> cardinal{};
            for (unsigned index = 0; index < Directions.size(); ++index)
            {
                const auto response = EvaluateRendererEnvironmentSh(projection->sh, Directions[index]);
                Require(std::isfinite(response.x) && std::isfinite(response.y) && std::isfinite(response.z) &&
                    response.x >= 0 && response.y >= 0 && response.z >= 0,
                    "production HDR response is nonfinite or negative");
                cardinal[index] = Luminance(response);
                Require(std::abs(cardinal[index] - golden.cardinal[index]) <= 1e-3f,
                    "production HDR orientation differs from its cardinal golden");
            }
            const float calibrated = projection->averageLuminance * std::exp2(info.defaultExposureStops);
            const auto [minimum, maximum] = std::minmax_element(cardinal.begin(), cardinal.end());
            Require((info.night ? calibrated >= .005f && calibrated <= .012f
                    : calibrated >= .06f && calibrated <= .12f) &&
                *maximum - *minimum > projection->averageLuminance * .02f,
                "HDR lost its day/night calibration or directional contrast");
        }
        Require(nightCount == 2, "dedicated night environments changed");

        std::vector<float> constant(64 * 32 * 3);
        for (size_t index = 0; index < constant.size(); index += 3)
        {
            constant[index] = .2f; constant[index + 1] = .4f; constant[index + 2] = .8f;
        }
        const auto projected = ProjectRendererDiffuseEnvironmentLatLongRgb(constant.data(), 64, 32);
        Require(bool(projected), "constant environment did not project");
        for (const auto direction : Directions)
        {
            const auto response = EvaluateRendererEnvironmentSh(projected->sh, direction);
            Require(std::abs(response.x - .2f) < .002f && std::abs(response.y - .4f) < .002f &&
                std::abs(response.z - .8f) < .002f, "diffuse projection applied an extra pi normalization");
        }
        std::vector<float> black(8 * 4 * 3);
        Require(!ProjectRendererDiffuseEnvironmentLatLongRgb(nullptr, 8, 4) &&
            !ProjectRendererDiffuseEnvironmentLatLongRgb(black.data(), 2, 1) &&
            !ProjectRendererDiffuseEnvironmentLatLongRgb(black.data(), 8, 2) &&
            !ProjectRendererDiffuseEnvironmentLatLongRgb(black.data(), 8, 4),
            "invalid or zero-energy environment was accepted");
    }

    const uvsr::json::Value& Member(const uvsr::json::Value& value, std::string_view name,
        uvsr::json::Value::Kind kind)
    {
        const auto* member = value.Find(name);
        Require(member && member->kind == kind, "missing or mistyped noise manifest field");
        return *member;
    }

    struct ExpectedNoiseAsset
    {
        uvsr::NoisePattern pattern;
        uvsr::NoiseResolution resolution;
        const char* fileName;
        const char* sha256;
    };

    constexpr std::array<ExpectedNoiseAsset, 12> ExpectedNoiseAssets = {{
        { uvsr::NoisePattern::SpatialWhite, uvsr::NoiseResolution::Size64,
            "spatial-white-64x64x1-r8.bin",
            "fe4cb771dcf6631e45d10e416794abc6cb263143eb9b66626651994aa5125de8" },
        { uvsr::NoisePattern::SpatialBlue, uvsr::NoiseResolution::Size64,
            "spatial-blue-64x64x1-r8.bin",
            "88d47915ec8a00a1e0e806440e91ee2c20db7aa22794eee8016e9fda30013e46" },
        { uvsr::NoisePattern::SpatiotemporalBlue, uvsr::NoiseResolution::Size64,
            "spatiotemporal-blue-64x64x64-r8.bin",
            "c637a502c36359aeb0193d718a00ec286b0fe1c8499fb173631fad58f7c6c7fc" },
        { uvsr::NoisePattern::SpatialWhite, uvsr::NoiseResolution::Size128,
            "spatial-white-128x128x1-r8.bin",
            "753043935f1cb58d35e4cf651e11397a7b1e18fa99296f627d2597f20ca2cc22" },
        { uvsr::NoisePattern::SpatialBlue, uvsr::NoiseResolution::Size128,
            "spatial-blue-128x128x1-r8.bin",
            "f0c18c9d5869eb6d5afabecf7a3969efa41be7378e0429d1ddd8747bbb4d8ff1" },
        { uvsr::NoisePattern::SpatiotemporalBlue, uvsr::NoiseResolution::Size128,
            "spatiotemporal-blue-128x128x64-r8.bin",
            "bed4f4bf7705885db4af2becf8409cf688042d6709cbd8092ce0b316a64b63dc" },
        { uvsr::NoisePattern::SpatialWhite, uvsr::NoiseResolution::Size256,
            "spatial-white-256x256x1-r8.bin",
            "e7e03f79f879fed6dac81b0dd4735ffc0c46f8c76ef0d89b4eb9ea4058625932" },
        { uvsr::NoisePattern::SpatialBlue, uvsr::NoiseResolution::Size256,
            "spatial-blue-256x256x1-r8.bin",
            "c7eb79c2217d79da5a670bea361ab07a04f450babf7ea043c18557690052c972" },
        { uvsr::NoisePattern::SpatiotemporalBlue, uvsr::NoiseResolution::Size256,
            "spatiotemporal-blue-256x256x64-r8.bin",
            "da89bc55d2b825ee6d27899a45f1678ed99637c1527e5228eea4d4896c3056ae" },
        { uvsr::NoisePattern::SpatialWhite, uvsr::NoiseResolution::Size512,
            "spatial-white-512x512x1-r8.bin",
            "3672e6338bdf1ac366387f0db8d336361dac292007b2b14da0c3ae5fba260ab5" },
        { uvsr::NoisePattern::SpatialBlue, uvsr::NoiseResolution::Size512,
            "spatial-blue-512x512x1-r8.bin",
            "5d2618fd124f1c73a56d73a7ad45418c08e4b6d032896fd2b1f11b029aa9c4fb" },
        { uvsr::NoisePattern::SpatiotemporalBlue, uvsr::NoiseResolution::Size512,
            "spatiotemporal-blue-512x512x64-r8.bin",
            "c4292f0e2d3d57d49334bdfe665b4aeff8ddf86ca101a6cb5364dbf2fb00a703" }
    }};

    void CheckNoiseAssets(const std::filesystem::path& root)
    {
        using Kind = uvsr::json::Value::Kind;
        const auto manifest = uvsr::json::Read(root / "manifest.json", 64u * 1024u);
        Require(manifest.kind == Kind::Object && manifest.object.size() == 4 &&
            !Member(manifest, "source", Kind::String).string.empty(), "noise manifest fields changed");
        Require(Member(manifest, "algorithm", Kind::String).string == "uvsr-spectral-stbn-v1" &&
            Member(manifest, "seed", Kind::Number).number == 1431720786,
            "retained noise construction identity");
        const auto& assets = Member(manifest, "assets", Kind::Array);
        Require(assets.kind == Kind::Array && assets.array.size() == ExpectedNoiseAssets.size(),
            "manifest retains all twelve noise assets");
        std::set<std::string> names;
        for (const auto& asset : assets.array)
        {
            Require(asset.kind == Kind::Object && asset.object.size() == 8, "noise asset fields changed");
            Require(names.insert(Member(asset, "file", Kind::String).string).second,
                "manifest asset names are unique");
        }
        for (const auto& expected : ExpectedNoiseAssets)
        {
            const uint32_t width = GetNoiseResolutionValue(expected.resolution);
            const uint32_t layers = GetNoiseLayerCount(expected.pattern);
            const uint64_t bytes = static_cast<uint64_t>(width) * width * layers;
            Require(std::string(GetNoiseAssetFileName(expected.pattern, expected.resolution)) ==
                expected.fileName && names.count(expected.fileName) == 1u,
                "every selectable noise mode maps to a retained asset");
            const auto entry = std::find_if(assets.array.begin(), assets.array.end(), [&](const auto& asset) {
                return Member(asset, "file", Kind::String).string == expected.fileName;
            });
            Require(entry != assets.array.end(), "retained asset has a manifest entry");
            Require(Member(*entry, "width", Kind::Number).number == width &&
                Member(*entry, "height", Kind::Number).number == width &&
                Member(*entry, "layers", Kind::Number).number == layers &&
                Member(*entry, "bytes", Kind::Number).number == static_cast<double>(bytes) &&
                Member(*entry, "format", Kind::String).string == "R8_UNORM" &&
                Member(*entry, "pattern", Kind::String).string == GetNoisePatternLabel(expected.pattern),
                "manifest dimensions, format and mode match the renderer's asset selection");
            Require(std::filesystem::file_size(root / expected.fileName) == bytes &&
                Member(*entry, "sha256", Kind::String).string == expected.sha256 &&
                Sha256File(root / expected.fileName) == expected.sha256,
                "reviewed spatial and temporal noise bytes are unchanged");
        }
        size_t binaries = 0u;
        for (const auto& entry : std::filesystem::directory_iterator(root))
            binaries += entry.is_regular_file() && entry.path().extension() == ".bin" ? 1u : 0u;
        Require(binaries == ExpectedNoiseAssets.size(), "noise directory contains only retained binaries");
    }

    void CheckNoiseSelection()
    {
        const NoiseSettings global{ NoisePattern::SpatiotemporalBlue, NoiseResolution::Size128, true };
        NoiseOverrideSettings overrideSettings;
        overrideSettings.custom = { NoisePattern::SpatialWhite, NoiseResolution::Size64, false };
        Require(ResolveNoiseSettings(global, overrideSettings) == global,
            "disabled override inherits the temporal sequence");
        overrideSettings.specifyNoise = true;
        Require(ResolveNoiseSettings(global, overrideSettings) == overrideSettings.custom,
            "enabled override preserves its chosen mode, size and animation");
        for (const auto& asset : ExpectedNoiseAssets)
            Require(IsValidNoiseSettings({ asset.pattern, asset.resolution, true }) &&
                IsValidNoiseSettings({ asset.pattern, asset.resolution, false }),
                "all twelve assets remain selectable with temporal animation on or off");
        Require(!IsValidNoiseSettings({ NoisePattern::Count, NoiseResolution::Size64, true }) &&
            !IsValidNoiseSettings({ NoisePattern::SpatialWhite, static_cast<NoiseResolution>(63u), true }),
            "invalid noise identifiers are rejected");
    }
}

void CheckLightingAssets(const std::filesystem::path& environmentRoot, const std::filesystem::path& noiseRoot)
{
    CheckEnvironments(environmentRoot);
    CheckNoiseAssets(noiseRoot);
    CheckNoiseSelection();
}
