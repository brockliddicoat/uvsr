#include "screen_space_directional_shadows.h"

#include <algorithm>
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
        settings.debugView =
            ScreenSpaceShadowDebugView::RayBounds;

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

        for (uint32_t reach : ScreenSpaceShadowTraceReaches)
        {
            settings.length = ScreenSpaceShadowLength(reach);
            for (uint32_t hard :
                ScreenSpaceShadowHardSampleCounts)
            {
                settings.hardShadowSamples = hard;
                for (uint32_t fade :
                    ScreenSpaceShadowFadeSampleCounts)
                {
                    settings.fadeOutSamples = fade;
                    assert(
                        IsScreenSpaceShadowConfigurationSupported(
                            settings));
                }
            }
        }

        settings.hardShadowSamples = 3u;
        assert(
            !IsScreenSpaceShadowConfigurationSupported(settings));
    }

    void TestProjectiveDepthSlope()
    {
        constexpr double scaleX = 960.0;
        constexpr double scaleY = -540.0;
        constexpr double epsilon = 1e-11;

        for (int caseIndex = 0; caseIndex < 32; ++caseIndex)
        {
            const double receiverW =
                0.8 + 0.11 * double(caseIndex + 1);
            const double receiverX =
                (-0.7 + 0.041 * double(caseIndex)) * receiverW;
            const double receiverY =
                (0.5 - 0.029 * double(caseIndex)) * receiverW;
            const double receiverDepth =
                0.08 + 0.021 * double(caseIndex);
            const double receiverZ =
                receiverDepth * receiverW;

            const double directionX =
                -0.45 + 0.017 * double(caseIndex);
            const double directionY =
                0.31 - 0.013 * double(caseIndex);
            const double directionZ =
                -0.12 + 0.009 * double(caseIndex);
            const double directionW =
                -0.07 + 0.004 * double(caseIndex);

            const double receiverNdcX =
                receiverX / receiverW;
            const double receiverNdcY =
                receiverY / receiverW;
            const double tangentX =
                (directionX -
                    receiverNdcX * directionW) *
                scaleX;
            const double tangentY =
                (directionY -
                    receiverNdcY * directionW) *
                scaleY;
            const double tangentMajorLength =
                std::max(
                    std::abs(tangentX),
                    std::abs(tangentY));
            assert(tangentMajorLength > epsilon);

            const double directionPixelX =
                tangentX / tangentMajorLength;
            const double directionPixelY =
                tangentY / tangentMajorLength;
            const double depthPerStep =
                (directionZ -
                    receiverDepth * directionW) /
                tangentMajorLength;

            const double rayParameter =
                0.05 + 0.011 * double(caseIndex);
            const double exactW =
                receiverW + rayParameter * directionW;
            assert(std::abs(exactW) > epsilon);
            const double exactNdcX =
                (receiverX +
                    rayParameter * directionX) /
                exactW;
            const double exactNdcY =
                (receiverY +
                    rayParameter * directionY) /
                exactW;
            const double exactDepth =
                (receiverZ +
                    rayParameter * directionZ) /
                exactW;
            const double pixelDeltaX =
                (exactNdcX - receiverNdcX) * scaleX;
            const double pixelDeltaY =
                (exactNdcY - receiverNdcY) * scaleY;
            const bool xMajor =
                std::abs(directionPixelX) >=
                std::abs(directionPixelY);
            const double signedStepDistance = xMajor
                ? pixelDeltaX / directionPixelX
                : pixelDeltaY / directionPixelY;
            const double predictedDepth =
                receiverDepth +
                signedStepDistance * depthPerStep;
            assert(std::abs(predictedDepth - exactDepth) < epsilon);
        }
    }

    void TestMajorAxisDenseRaster()
    {
        constexpr std::array<std::array<double, 2>, 12>
            tangents = {{
                {{ 1.0, 0.0 }},
                {{ 1.0, 0.25 }},
                {{ 1.0, 0.5 }},
                {{ 1.0, 1.0 }},
                {{ -1.0, 0.25 }},
                {{ -1.0, -1.0 }},
                {{ 0.0, 1.0 }},
                {{ 0.25, 1.0 }},
                {{ 0.5, 1.0 }},
                {{ 1.0, -1.0 }},
                {{ 0.25, -1.0 }},
                {{ -1.0, -0.5 }}
            }};

        for (const auto& tangent : tangents)
        {
            const double majorLength = std::max(
                std::abs(tangent[0]),
                std::abs(tangent[1]));
            const double stepX = tangent[0] / majorLength;
            const double stepY = tangent[1] / majorLength;
            const bool xMajor =
                std::abs(stepX) >= std::abs(stepY);
            const int majorSign =
                (xMajor ? stepX : stepY) < 0.0 ? -1 : 1;

            for (const uint32_t reach :
                ScreenSpaceShadowTraceReaches)
            {
                int previousMajor = 200;
                for (uint32_t sampleIndex = 0u;
                    sampleIndex < reach;
                    ++sampleIndex)
                {
                    const double distance =
                        double(sampleIndex + 1u);
                    const int rasterX = int(std::floor(
                        200.5 + stepX * distance - 0.5));
                    const int rasterY = int(std::floor(
                        200.5 + stepY * distance - 0.5));
                    const int rasterMajor =
                        xMajor ? rasterX : rasterY;
                    assert(
                        rasterMajor ==
                        previousMajor + majorSign);
                    previousMajor = rasterMajor;
                }
                assert(
                    std::abs(previousMajor - 200) ==
                    int(reach));
            }
        }
    }

    void TestMinorAxisInterpolationContract()
    {
        const auto interpolationWeight = [](
            double sampleMinorCoordinate)
        {
            const double grid =
                sampleMinorCoordinate - 0.5;
            return grid - std::floor(grid);
        };
        assert(std::abs(
            interpolationWeight(10.5) - 0.0) < 1e-12);
        assert(std::abs(
            interpolationWeight(10.75) - 0.25) < 1e-12);
        assert(std::abs(
            interpolationWeight(11.0) - 0.5) < 1e-12);

        const auto interpolate = [](
            double primary,
            double neighbor,
            double weight)
        {
            return primary +
                (neighbor - primary) * weight;
        };
        assert(std::abs(
            interpolate(0.2, 0.6, 0.25) - 0.3) < 1e-12);

        const auto isEdge = [](
            double primary,
            double neighbor,
            double farDepth,
            double threshold)
        {
            const double remaining = std::min(
                std::abs(primary - farDepth),
                std::abs(neighbor - farDepth));
            return std::abs(neighbor - primary) >
                threshold * remaining;
        };
        assert(!isEdge(0.50, 0.51, 0.0, 0.10));
        assert(isEdge(0.50, 0.10, 0.0, 0.10));

        const auto pointFallback = [](
            double primary,
            double neighbor,
            double weight)
        {
            return weight < 0.5 ? primary : neighbor;
        };
        assert(pointFallback(0.2, 0.8, 0.49) == 0.2);
        assert(pointFallback(0.2, 0.8, 0.50) == 0.8);
    }

    void TestDenseTraceContract()
    {
        for (uint32_t reach : ScreenSpaceShadowTraceReaches)
        {
            assert(reach >= 1u);
            uint32_t lastDistance = 0u;
            for (uint32_t sampleIndex = 0u;
                sampleIndex < reach;
                ++sampleIndex)
            {
                const uint32_t distance = sampleIndex + 1u;
                assert(distance == lastDistance + 1u);
                lastDistance = distance;
            }
            assert(lastDistance == reach);

            constexpr uint32_t hardCount = 4u;
            constexpr uint32_t fadeCount = 8u;
            const uint32_t bodyCount =
                reach - hardCount - fadeCount;
            assert(
                hardCount + bodyCount + fadeCount ==
                reach);
            assert(reach - fadeCount >= hardCount);
        }

        const auto remainingDepth = [](
            float sceneDepth,
            bool reverseDepth)
        {
            const float farDepth = reverseDepth ? 0.f : 1.f;
            return std::abs(sceneDepth - farDepth);
        };
        assert(std::abs(
            remainingDepth(0.2f, true) -
            remainingDepth(0.8f, false)) < 1e-6f);
        assert(std::abs(
            remainingDepth(0.01f, true) * 0.005f -
            0.00005f) < 1e-8f);
        assert(std::abs(
            remainingDepth(0.8f, true) * 0.005f -
            0.004f) < 1e-8f);

        const auto precisionOffset = [](
            float receiverDepth,
            bool reverseDepth)
        {
            const float farDepth = reverseDepth ? 0.f : 1.f;
            return receiverDepth +
                (receiverDepth - farDepth) / 65535.f;
        };
        assert(precisionOffset(0.5f, true) > 0.5f);
        assert(precisionOffset(0.5f, false) < 0.5f);
    }

    void TestShaderSourceContract(
        const std::filesystem::path& sourceDirectory)
    {
        const std::string shader = ReadTextFile(
            sourceDirectory /
            "screen_space_directional_shadows_cs.hlsl");
        assert(!shader.empty());
        assert(shader.find(
            "kMaximumTraceSamples = 960u") !=
            std::string::npos);
        assert(shader.find(
            "g_Shadow.traceSampleCount") !=
            std::string::npos);
        assert(shader.find(
            "float tangentMajorLength") !=
            std::string::npos);
        assert(shader.find("rsqrt(tangentLengthSquared)") ==
            std::string::npos);
        assert(shader.find(
            "kTraceChunkSamples = 32u") !=
            std::string::npos);
        assert(shader.find(
            "groupshared float s_DepthCache") !=
            std::string::npos);
        assert(shader.find(
            "WaveActiveSum(localDirectReadCount)") !=
            std::string::npos);
        assert(shader.find(
            "cachePixelCount * 4u <=") !=
            std::string::npos);
        assert(shader.find(
            "samplePosition - 0.5f") !=
            std::string::npos);
        assert(shader.find(
            "lerp(") !=
            std::string::npos);
        assert(shader.find(
            "uint fadeStart = sampleCount - fadeCount;") !=
            std::string::npos);
        assert(shader.find(
            "softEvidence * 0.25f") !=
            std::string::npos);
        assert(shader.find(
            "g_Shadow.surfaceThickness * remainingDepth") !=
            std::string::npos);
        assert(shader.find(
            "g_Shadow.depthDiscontinuityThreshold *") !=
            std::string::npos);
        assert(shader.find(
            "(receiverDepth - GetFarDepth()) / 65535.0f") !=
            std::string::npos);
        assert(shader.find(
            "samplePixel + traceAxis") !=
            std::string::npos);
        assert(shader.find(
            "g_Shadow.debugView == 0u") !=
            std::string::npos);

        const size_t mainOffset = shader.find("void main(");
        const size_t firstBarrier = shader.find(
            "GroupMemoryBarrierWithGroupSync()",
            mainOffset);
        assert(mainOffset != std::string::npos);
        assert(firstBarrier != std::string::npos);
        assert(shader.substr(
            mainOffset,
            firstBarrier - mainOffset).find("return;") ==
            std::string::npos);
    }
}

int main(int argc, char** argv)
{
    assert(argc >= 2);
    TestDefaultSettings();
    TestPresetResetAndEnableIndependence();
    TestLengthPresetsAndSupportedConfigurations();
    TestProjectiveDepthSlope();
    TestMajorAxisDenseRaster();
    TestMinorAxisInterpolationContract();
    TestDenseTraceContract();
    TestShaderSourceContract(argv[1]);
    return 0;
}
