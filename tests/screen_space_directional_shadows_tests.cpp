#include "screen_space_directional_shadows.h"
#include "bend_sss_cpu.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace uvsr;

#undef assert
#define assert(...) \
    do { \
        if (!(static_cast<bool>(__VA_ARGS__))) \
        { \
            std::fprintf( \
                stderr, \
                "%s:%d: assertion failed: %s\n", \
                __FILE__, \
                __LINE__, \
                #__VA_ARGS__); \
            std::abort(); \
        } \
    } while (false)

namespace
{
    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }

    void AssertDefault(
        const ScreenSpaceDirectionalShadowSettings& settings,
        bool expectedEnabled)
    {
        assert(settings.enabled == expectedEnabled);
        assert(settings.preset == ScreenSpaceShadowPreset::Default);
        assert(settings.length == ScreenSpaceShadowLength::Pixels60);
        assert(settings.hardShadowSamples == 4u);
        assert(settings.fadeOutSamples == 8u);
        assert(std::abs(settings.surfaceThickness - 0.005f) < 1e-8f);
        assert(std::abs(settings.bilinearThreshold - 0.02f) < 1e-8f);
        assert(settings.shadowContrast == 4.f);
        assert(!settings.ignoreEdgePixels);
        assert(!settings.usePrecisionOffset);
        assert(!settings.bilinearSamplingOffsetMode);
        assert(!settings.useEarlyOut);
        assert(settings.debugView == ScreenSpaceShadowDebugView::None);
        assert(IsScreenSpaceShadowConfigurationSupported(settings));
    }

    void TestDefaultSettings()
    {
        AssertDefault(ScreenSpaceDirectionalShadowSettings{}, false);
    }

    void TestPresetResetAndEnableIndependence()
    {
        ScreenSpaceDirectionalShadowSettings settings;
        settings.enabled = true;
        settings.length = ScreenSpaceShadowLength::Pixels480;
        settings.surfaceThickness = 0.04f;
        settings.bilinearThreshold = 0.09f;
        settings.shadowContrast = 12.f;
        settings.hardShadowSamples = 8u;
        settings.fadeOutSamples = 16u;
        settings.ignoreEdgePixels = true;
        settings.usePrecisionOffset = true;
        settings.bilinearSamplingOffsetMode = true;
        settings.useEarlyOut = true;
        settings.debugView = ScreenSpaceShadowDebugView::Wave;

        ApplyScreenSpaceShadowPreset(
            settings,
            ScreenSpaceShadowPreset::Default);
        AssertDefault(settings, true);
    }

    void TestLengthPresetsAndSupportedConfigurations()
    {
        ScreenSpaceDirectionalShadowSettings settings;
        ApplyScreenSpaceShadowPreset(
            settings,
            ScreenSpaceShadowPreset::Long);
        assert(settings.length == ScreenSpaceShadowLength::Pixels240);
        assert(settings.preset == ScreenSpaceShadowPreset::Long);

        ApplyScreenSpaceShadowPreset(
            settings,
            ScreenSpaceShadowPreset::MaximumValidation);
        assert(settings.length == ScreenSpaceShadowLength::Pixels960);
        assert(settings.preset ==
            ScreenSpaceShadowPreset::MaximumValidation);

        uint32_t variantCount = 0u;
        for (uint32_t reach : ScreenSpaceShadowTraceReaches)
        {
            settings.length = ScreenSpaceShadowLength(reach);
            for (uint32_t hard : ScreenSpaceShadowHardSampleCounts)
            {
                settings.hardShadowSamples = hard;
                for (uint32_t fade : ScreenSpaceShadowFadeSampleCounts)
                {
                    settings.fadeOutSamples = fade;
                    assert(
                        IsScreenSpaceShadowConfigurationSupported(
                            settings));
                    ++variantCount;
                }
            }
        }
        assert(variantCount == 45u);

        settings.hardShadowSamples = 3u;
        assert(
            !IsScreenSpaceShadowConfigurationSupported(settings));
    }

    void AssertDispatchList(
        const std::array<float, 4>& projectedLight,
        const std::array<int, 2>& viewport)
    {
        float light[4] = {
            projectedLight[0],
            projectedLight[1],
            projectedLight[2],
            projectedLight[3]
        };
        int viewportSize[2] = {
            viewport[0],
            viewport[1]
        };
        int minimumBounds[2] = { 0, 0 };
        int maximumBounds[2] = {
            viewport[0],
            viewport[1]
        };

        const Bend::DispatchList list = Bend::BuildDispatchList(
            light,
            viewportSize,
            minimumBounds,
            maximumBounds,
            false,
            64);
        assert(list.DispatchCount >= 1);
        assert(list.DispatchCount <= 8);
        assert(list.LightCoordinate_Shader[3] ==
            (projectedLight[3] > 0.f ? 1.f : -1.f));
        for (float coordinate : list.LightCoordinate_Shader)
            assert(std::isfinite(coordinate));

        for (int index = 0; index < list.DispatchCount; ++index)
        {
            const Bend::DispatchData& dispatch = list.Dispatch[index];
            assert(dispatch.WaveCount[0] > 0);
            assert(dispatch.WaveCount[1] > 0);
            assert(dispatch.WaveCount[2] > 0);
        }
    }

    void TestReleasedCpuDispatchContract()
    {
        constexpr std::array<std::array<float, 4>, 5> projectedLights = {{
            {{ 0.25f, -0.4f, 0.5f, 1.f }},
            {{ 0.25f, -0.4f, 0.5f, -1.f }},
            {{ 500.f, -250.f, 0.5f, 1.f }},
            {{ -0.2f, 0.7f, -0.1f, 1e-8f }},
            {{ -0.2f, 0.7f, -0.1f, -1e-8f }}
        }};
        constexpr std::array<std::array<int, 2>, 2> viewports = {{
            {{ 1920, 1080 }},
            {{ 1901, 1069 }}
        }};

        for (const auto& viewport : viewports)
        {
            for (const auto& projectedLight : projectedLights)
                AssertDispatchList(projectedLight, viewport);
        }
    }

    void TestSourceAndLicenseContract(
        const std::filesystem::path& repositoryRoot)
    {
        const std::filesystem::path sourceDirectory =
            repositoryRoot / "src";
        const std::filesystem::path upstreamDirectory =
            repositoryRoot / "third_party" / "bend_sss" / "upstream";

        const std::string shader = ReadTextFile(
            sourceDirectory /
            "screen_space_directional_shadows_cs.hlsl");
        const std::string shaderConfig = ReadTextFile(
            sourceDirectory /
            "screen_space_directional_shadows_shaders.cfg");
        const std::string adapter = ReadTextFile(
            sourceDirectory /
            "screen_space_directional_shadows.cpp");
        const std::string cpuSource = ReadTextFile(
            upstreamDirectory / "bend_sss_cpu.h");
        const std::string gpuSource = ReadTextFile(
            upstreamDirectory / "bend_sss_gpu.h");
        const std::string license = ReadTextFile(
            repositoryRoot / "third_party" / "licenses" /
            "Apache-2.0.txt");
        const std::string attributes = ReadTextFile(
            repositoryRoot / ".gitattributes");
        const std::string cmake = ReadTextFile(
            repositoryRoot / "CMakeLists.txt");

        assert(shader.find("#define WAVE_SIZE 64") != std::string::npos);
        assert(shader.find("bend_sss_gpu.h") != std::string::npos);
        assert(shader.find("[numthreads(WAVE_SIZE, 1, 1)]") !=
            std::string::npos);
        assert(shader.find("WriteScreenSpaceShadow(") !=
            std::string::npos);
        assert(shader.find("kTraceChunkSamples") == std::string::npos);
        assert(shader.find("[numthreads(8, 8, 1)]") == std::string::npos);

        assert(shaderConfig.find(
            "SAMPLE_COUNT={60,120,240,480,960}") !=
            std::string::npos);
        assert(shaderConfig.find(
            "HARD_SHADOW_SAMPLES={0,4,8}") !=
            std::string::npos);
        assert(shaderConfig.find(
            "FADE_OUT_SAMPLES={0,8,16}") !=
            std::string::npos);

        assert(adapter.find("Bend::BuildDispatchList(") !=
            std::string::npos);
        assert(adapter.find(
            "m_PointBorderSamplers[reverseDepth ? 0u : 1u]") !=
            std::string::npos);
        assert(adapter.find("clearTextureFloat(") != std::string::npos);
        assert(adapter.find("lightDirectionLengthSquared <= 1e-12f") !=
            std::string::npos);
        assert(adapter.find("ScreenSpaceShadowDebugView::Edge") !=
            std::string::npos);
        assert(adapter.find("ScreenSpaceShadowDebugView::Thread") !=
            std::string::npos);
        assert(adapter.find("ScreenSpaceShadowDebugView::Wave") !=
            std::string::npos);
        assert(adapter.find("Bend Screen-Space") == std::string::npos);

        const size_t dispatchLoop = adapter.find(
            "for (int dispatchIndex = 0;");
        const size_t writeBuffer = adapter.find(
            "commandList->writeBuffer(",
            dispatchLoop);
        const size_t setComputeState = adapter.find(
            "commandList->setComputeState(state);",
            dispatchLoop);
        assert(dispatchLoop != std::string::npos);
        assert(writeBuffer != std::string::npos);
        assert(setComputeState != std::string::npos);
        assert(writeBuffer < setComputeState);

        constexpr const char* copyrightNotice =
            "Copyright 2023 Sony Interactive Entertainment.";
        assert(cpuSource.find(copyrightNotice) != std::string::npos);
        assert(gpuSource.find(copyrightNotice) != std::string::npos);
        assert(cpuSource.find("Licensed under the Apache License") !=
            std::string::npos);
        assert(gpuSource.find("Licensed under the Apache License") !=
            std::string::npos);
        assert(std::filesystem::file_size(
            upstreamDirectory / "bend_sss_cpu.h") == 12335u);
        assert(std::filesystem::file_size(
            upstreamDirectory / "bend_sss_gpu.h") == 25289u);

        assert(license.find("Apache License") != std::string::npos);
        assert(license.find("4. Redistribution.") != std::string::npos);
        assert(license.find("END OF TERMS AND CONDITIONS") !=
            std::string::npos);
        assert(attributes.find(
            "third_party/bend_sss/upstream/bend_sss_cpu.h -text -whitespace") !=
            std::string::npos);
        assert(attributes.find(
            "third_party/bend_sss/upstream/bend_sss_gpu.h -text -whitespace") !=
            std::string::npos);
        assert(attributes.find(
            "third_party/licenses/Apache-2.0.txt -text -whitespace") !=
            std::string::npos);
        assert(cmake.find(
            "licenses/Apache-2.0.txt") != std::string::npos);
    }
}

int main(int argc, char** argv)
{
    assert(argc >= 2);
    TestDefaultSettings();
    TestPresetResetAndEnableIndependence();
    TestLengthPresetsAndSupportedConfigurations();
    TestReleasedCpuDispatchContract();
    TestSourceAndLicenseContract(argv[1]);
    return 0;
}
