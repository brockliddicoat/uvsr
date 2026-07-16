#include "sponza_camera_preset.h"

#include <donut/core/vfs/VFS.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace donut::math;

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    std::uint32_t FloatBits(float value)
    {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
}

int main(int argc, const char* const* argv)
{
    try
    {
        Require(argc == 2, "expected the production Intel Sponza asset directory");
        const auto nonce = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const std::filesystem::path directory = std::filesystem::temp_directory_path()
            / ("uvsr_sponza_camera_" + std::to_string(nonce));
        std::filesystem::create_directories(directory / "intel_sponza/components");
        const std::filesystem::path decoratedPath =
            directory / "intel_sponza/intel_pbr_sponza.scene.json";
        const std::filesystem::path plainPath =
            directory / "intel_sponza/intel_pbr_sponza_plain.scene.json";
        const std::filesystem::path unrelatedPath = directory / "unrelated.scene.json";
        const std::filesystem::path oldExactPath =
            directory / "intel_sponza/intel_pbr_sponza_old_exact.scene.json";
        const std::filesystem::path similarlyNamedPath =
            directory / "intel_sponza/intel_pbr_sponza_copy.scene.json";
        {
            std::ofstream(decoratedPath)
                << R"({"displayName":"PBR Sponza Decorated","cameraPreset":"intel-pbr-sponza-courtyard-simplified-v1"})";
            std::ofstream(plainPath)
                << R"({"displayName":"PBR Sponza Plain","cameraPreset":"intel-pbr-sponza-courtyard-simplified-v1"})";
            std::ofstream(unrelatedPath)
                << R"({"cameraPreset":"another-camera"})";
            std::ofstream(oldExactPath)
                << R"({"displayName":"Retired Exact Camera","cameraPreset":"intel-pbr-sponza-courtyard-v1"})";
            std::ofstream(similarlyNamedPath)
                << R"({"displayName":"Intel PBR Sponza Copy"})";
        }

        donut::vfs::NativeFileSystem fileSystem;
        const auto& simplified = uvsr::GetDefaultSponzaCameraPreset();
        const auto* simplifiedByLocation = uvsr::FindSponzaCameraPreset(
            uvsr::SponzaCameraLocation::SimplifiedApproximation);
        const auto* decorated = uvsr::FindStandardSponzaCameraPreset(
            fileSystem,
            decoratedPath);
        const auto* plain = uvsr::FindStandardSponzaCameraPreset(
            fileSystem,
            plainPath);

        Require(decorated == &simplified,
            "PBR Sponza Decorated must resolve the simplified default camera");
        Require(plain == &simplified,
            "PBR Sponza Plain must resolve the same simplified default camera");
        Require(simplifiedByLocation == &simplified,
            "Benchmark Position 1 must be the sole stored camera preset");
        Require(uvsr::SelectableSponzaCameraLocations.size() == 2 &&
            uvsr::SelectableSponzaCameraLocations[0] ==
                uvsr::SponzaCameraLocation::Free &&
            uvsr::SelectableSponzaCameraLocations[1] ==
                uvsr::SponzaCameraLocation::SimplifiedApproximation,
            "the camera-location menu must always expose Free followed by the stored preset");
        Require(uvsr::FindSponzaCameraPreset(
            uvsr::SponzaCameraLocation::Free) == nullptr &&
            uvsr::FindSponzaCameraPreset(
                uvsr::SponzaCameraLocation::Count) == nullptr,
            "Free and the enum sentinel must not resolve to stored camera presets");

        const std::filesystem::path productionDirectory = argv[1];
        Require(uvsr::FindStandardSponzaCameraPreset(
            fileSystem,
            productionDirectory / "intel_pbr_sponza.scene.json") == &simplified,
            "the Decorated source-tree descriptor must declare the simplified default camera");
        Require(uvsr::FindStandardSponzaCameraPreset(
            fileSystem,
            productionDirectory / "intel_pbr_sponza_plain.scene.json") == &simplified,
            "the Plain source-tree descriptor must declare the simplified default camera");
        Require(uvsr::FindStandardSponzaCameraPreset(
            fileSystem,
            directory / "intel_sponza/components/intel_pbr_sponza_flat_roof_part1.glb") == nullptr,
            "an architecture component must not inherit the standardized scene camera");
        Require(uvsr::FindStandardSponzaCameraPreset(
            fileSystem,
            unrelatedPath) == nullptr,
            "an unrelated descriptor must not inherit the standardized scene camera");
        Require(uvsr::FindStandardSponzaCameraPreset(
            fileSystem,
            oldExactPath) == nullptr,
            "a descriptor using the retired Exact Capture identifier must be rejected");
        Require(uvsr::FindStandardSponzaCameraPreset(
            fileSystem,
            similarlyNamedPath) == nullptr,
            "a similarly named descriptor must not inherit the standardized scene camera");

        Require(std::string(simplified.Id) ==
            "intel-pbr-sponza-courtyard-simplified-v1",
            "Benchmark Position 1 must expose its stable identifier");
        Require(std::string(simplified.Label) == "Benchmark Position 1" &&
            std::string(uvsr::GetSponzaCameraLocationLabel(
                uvsr::SponzaCameraLocation::SimplifiedApproximation)) ==
                "Benchmark Position 1",
            "the stored preset must expose the Benchmark Position 1 dropdown label");
        Require(std::string(uvsr::GetSponzaCameraLocationLabel(
            uvsr::SponzaCameraLocation::Free)) == "Free",
            "a camera that leaves its recalled pose must display Free");

        Require(simplified.ReferenceWidth == 1920u &&
            simplified.ReferenceHeight == 1080u,
            "benchmark framing must retain the canonical 1920 by 1080 viewport");
        Require(simplified.VerticalFovDegrees == 60.f,
            "benchmark framing must retain the canonical vertical field of view");

        Require(FloatBits(simplified.Position.x) == 0x41300000u &&
            FloatBits(simplified.Position.y) == 0x40F66666u &&
            FloatBits(simplified.Position.z) == 0xC00CCCCDu,
            "simplified camera position must retain the requested float32 approximation");
        Require(FloatBits(simplified.Direction.x) == 0xBF3504F3u &&
            FloatBits(simplified.Direction.y) == 0x00000000u &&
            FloatBits(simplified.Direction.z) == 0x3F3504F3u &&
            FloatBits(simplified.Up.x) == 0x00000000u &&
            FloatBits(simplified.Up.y) == 0x3F800000u &&
            FloatBits(simplified.Up.z) == 0x00000000u &&
            FloatBits(simplified.Right.x) == 0xBF3504F3u &&
            FloatBits(simplified.Right.y) == 0x00000000u &&
            FloatBits(simplified.Right.z) == 0xBF3504F3u,
            "simplified camera basis must use the normalized 45-degree float32 values");
        Require(std::abs(length(simplified.Direction) - 1.f) < 1e-6f &&
            std::abs(length(simplified.Up) - 1.f) < 1e-6f &&
            std::abs(length(simplified.Right) - 1.f) < 1e-6f,
            "simplified camera basis vectors must be unit length");
        Require(std::abs(dot(simplified.Direction, simplified.Up)) < 1e-6f &&
            std::abs(dot(simplified.Direction, simplified.Right)) < 1e-6f &&
            std::abs(dot(simplified.Up, simplified.Right)) < 1e-6f &&
            length(cross(simplified.Direction, simplified.Up) - simplified.Right) < 1e-6f,
            "simplified camera basis must be orthonormal and right-handed");

        Require(uvsr::ResolveSponzaCameraLocation(
            uvsr::SponzaCameraLocation::Free,
            false) == uvsr::SponzaCameraLocation::SimplifiedApproximation &&
            uvsr::ResolveSponzaCameraLocation(
                uvsr::SponzaCameraLocation::SimplifiedApproximation,
                false) == uvsr::SponzaCameraLocation::SimplifiedApproximation,
            "scene loading must resolve Free and the named location to the sole preset");
        Require(uvsr::ResolveSponzaCameraLocation(
            uvsr::SponzaCameraLocation::Free,
            true) == uvsr::SponzaCameraLocation::SimplifiedApproximation &&
            uvsr::ResolveSponzaCameraLocation(
                uvsr::SponzaCameraLocation::SimplifiedApproximation,
                true) == uvsr::SponzaCameraLocation::SimplifiedApproximation,
            "benchmark mode must force the sole simplified preset");

        Require(uvsr::IsSponzaCameraAtPreset(
            simplified,
            simplified.Position,
            simplified.Direction,
            simplified.Up),
            "the recalled simplified pose must remain identified by its name");
        Require(!uvsr::IsSponzaCameraAtPreset(
            simplified,
            simplified.Position + float3(0.001f, 0.f, 0.f),
            simplified.Direction,
            simplified.Up),
            "moving away from a recalled pose must identify the camera as Free");
        Require(!uvsr::IsSponzaCameraAtPreset(
            simplified,
            simplified.Position,
            normalize(simplified.Direction + float3(0.f, 0.001f, 0.f)),
            simplified.Up),
            "rotating away from a recalled pose must identify the camera as Free");

        std::error_code cleanupError;
        std::filesystem::remove_all(directory, cleanupError);

        std::cout << "Sponza camera preset reference tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Sponza camera preset reference tests failed: " << error.what() << '\n';
        return 1;
    }
}
