#include "adaptive_msaa_compact_shade.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
    using namespace uvsr::adaptive_msaa;

    bool g_Passed = true;

    void Check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "Adaptive compact shade validation failed: "
                << message << '\n';
            g_Passed = false;
        }
    }

    bool Near(float actual, float expected, float tolerance = 1e-5f)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    GpuVisibilityKey MakeKey(
        uint32_t slot = 0u,
        uint32_t generation = 1u,
        uint32_t primitive = 1u)
    {
        const HandlePackResult handle = PackGeometryInstanceHandle(
            slot, generation);
        Check(handle.Succeeded(), "fixture handle packing succeeds");
        const GpuVisibilityKeyResult key = MakeGpuVisibilityKey(
            handle.handle, primitive);
        Check(key.Succeeded(), "fixture visibility key packing succeeds");
        return key.key;
    }

    CompactShadeSurfaceValidationDesc MakeValidSurface()
    {
        CompactShadeSurfaceValidationDesc value;
        value.key = MakeKey();
        value.scene.topologyEpochLow = 0x76543210u;
        value.scene.topologyEpochHigh = 0x12345678u;
        value.scene.topologyRevision = 7u;
        value.scene.lodRevision = 8u;
        value.scene.deformationRevision = 9u;
        value.scene.materialRevision = 10u;
        value.scene.geometryInstanceRecordCount = 1u;
        value.scene.geometryRecordCount = 1u;
        value.scene.materialRecordCount = 1u;
        value.scene.instanceHandleCount = 1u;
        value.scene.instanceBufferDescriptor = 2u;
        value.scene.materialBufferDescriptor = 3u;
        value.scene.rawBufferDescriptorCount = 4u;
        value.scene.textureDescriptorCount = 2u;
        value.scene.descriptorCount = 6u;

        value.geometryInstance.generation = 1u;
        value.geometryInstance.geometryIndex = 0u;
        value.geometryInstance.instanceIndex = 0u;
        value.geometryInstance.geometryRecordIndex = 0u;
        value.geometryInstance.stableInstanceHandleLow = 1u;
        value.geometryInstance.topologyEpochLow =
            value.scene.topologyEpochLow;
        value.geometryInstance.topologyEpochHigh =
            value.scene.topologyEpochHigh;
        value.geometryInstance.topologyRevision =
            value.scene.topologyRevision;
        value.geometryInstance.lodRevision = value.scene.lodRevision;
        value.geometryInstance.deformationRevision =
            value.scene.deformationRevision;
        value.geometryInstance.materialRevision =
            value.scene.materialRevision;
        value.geometryInstance.primitiveCount = 2u;
        value.geometryInstance.flags =
            CompactGeometryInstanceFlag_Valid |
            CompactGeometryInstanceFlag_DeformationSupported |
            CompactGeometryInstanceFlag_MaterialSupported;
        value.geometryInstance.payloadClass = static_cast<uint32_t>(
            CompactGeometryPayloadClass::Reconstruct);
        value.geometryInstance.payloadFallbackReason = static_cast<uint32_t>(
            FallbackReason::None);

        value.geometry.indexBufferDescriptor = 4u;
        value.geometry.vertexBufferDescriptor = 5u;
        value.geometry.indexOffset = 0u;
        value.geometry.positionOffset = 0u;
        value.geometry.previousPositionOffset = 36u;
        value.geometry.texCoordOffset = 72u;
        value.geometry.normalOffset = 96u;
        value.geometry.tangentOffset = 108u;
        value.geometry.indexCount = 6u;
        value.geometry.vertexCount = 3u;
        value.geometry.materialIndex = 0u;

        value.geometryInstanceRecordCount = 1u;
        value.geometryInstanceRecordStride =
            sizeof(CompactGeometryInstanceGpuRecord);
        value.geometryRecordCount = 1u;
        value.geometryRecordStride =
            sizeof(CompactGeometryGpuRecord);
        value.instanceHandleCount = 1u;
        value.instanceHandleStride = sizeof(uint32_t);
        value.mappedGeometryInstanceHandle =
            value.key.geometryInstanceHandle;
        value.instanceGeometryCount = 1u;
        value.sceneInstanceBufferByteSize = 112u;
        value.materialBufferByteSize = 208u;
        value.indexBufferByteSize = 24u;
        value.vertexBufferByteSize = 120u;
        value.triangleVertexIndices = { 0u, 1u, 2u };
        return value;
    }

    void TestStandardSamplePatterns()
    {
        constexpr std::array<uint32_t, 4u> Counts = {
            2u, 4u, 8u, 16u
        };
        for (uint32_t count : Counts)
        {
            const Dx12StandardSamplePattern pattern =
                GetDx12StandardSamplePattern(count);
            Check(pattern.Succeeded() && pattern.sampleCount == count,
                "every supported standard-pattern fixture is available");
            for (uint32_t first = 0u; first < count; ++first)
            {
                const auto position = pattern.positions[first];
                Check(position.xSixteenths >= -8 &&
                        position.xSixteenths <= 7 &&
                        position.ySixteenths >= -8 &&
                        position.ySixteenths <= 7,
                    "standard sample coordinates remain signed sixteenths");
                for (uint32_t second = first + 1u;
                    second < count;
                    ++second)
                {
                    Check(position.xSixteenths !=
                            pattern.positions[second].xSixteenths ||
                            position.ySixteenths !=
                            pattern.positions[second].ySixteenths,
                        "standard sample fixture has no duplicate points");
                }
            }
        }
        const auto two = GetDx12StandardSamplePattern(2u);
        Check(two.positions[0].xSixteenths == 4 &&
                two.positions[0].ySixteenths == 4 &&
                two.positions[1].xSixteenths == -4 &&
                two.positions[1].ySixteenths == -4,
            "2x canonical fixture preserves Direct3D ordering");
        const auto four = GetDx12StandardSamplePattern(4u);
        Check(four.positions[0].xSixteenths == -2 &&
                four.positions[0].ySixteenths == -6 &&
                four.positions[3].xSixteenths == 2 &&
                four.positions[3].ySixteenths == 6,
            "4x canonical fixture preserves Direct3D ordering");
        constexpr std::array<Dx12StandardSamplePosition, 8u>
            Expected8 = {{
                { 1, -3 }, { -1, 3 }, { 5, 1 }, { -3, -5 },
                { -5, 5 }, { -7, -1 }, { 3, 7 }, { 7, -7 }
            }};
        constexpr std::array<Dx12StandardSamplePosition, 16u>
            Expected16 = {{
                { 1, 1 }, { -1, -3 }, { -3, 2 }, { 4, -1 },
                { -5, -2 }, { 2, 5 }, { 5, 3 }, { 3, -5 },
                { -2, 6 }, { 0, -7 }, { -4, -6 }, { -6, 4 },
                { -8, 0 }, { 7, -4 }, { 6, 7 }, { -7, -8 }
            }};
        const auto eight = GetDx12StandardSamplePattern(8u);
        const auto sixteen = GetDx12StandardSamplePattern(16u);
        for (size_t index = 0u; index < Expected8.size(); ++index)
        {
            Check(eight.positions[index].xSixteenths ==
                    Expected8[index].xSixteenths &&
                    eight.positions[index].ySixteenths ==
                    Expected8[index].ySixteenths,
                "8x canonical fixture preserves every ordered sample");
        }
        for (size_t index = 0u; index < Expected16.size(); ++index)
        {
            Check(sixteen.positions[index].xSixteenths ==
                    Expected16[index].xSixteenths &&
                    sixteen.positions[index].ySixteenths ==
                    Expected16[index].ySixteenths,
                "16x canonical fixture preserves every ordered sample");
        }
        Check(GetDx12StandardSamplePattern(1u).reason ==
                FallbackReason::SampleCountUnsupported &&
            GetDx12StandardSamplePattern(32u).sampleCount == 0u,
            "non-product sample counts fail closed");
    }

    void TestExactRasterCoverage()
    {
        constexpr float Subpixel =
            1.f / float(CompactRasterSubpixelScale);
        struct SnapFixture
        {
            float value;
            int32_t expected;
        };
        constexpr std::array<SnapFixture, 6u> SnapFixtures = {{
            { 0.5f * Subpixel, 0 },
            { 1.5f * Subpixel, 2 },
            { 2.5f * Subpixel, 2 },
            { -0.5f * Subpixel, 0 },
            { -1.5f * Subpixel, -2 },
            { -2.5f * Subpixel, -2 }
        }};
        for (const SnapFixture fixture : SnapFixtures)
        {
            const CompactRasterSnapResult snapped =
                SnapCompactRasterPosition({ fixture.value, 0.f });
            Check(snapped.Succeeded() &&
                    snapped.rasterPosition.x == fixture.expected,
                "16.8 snapping uses round-to-nearest-even for both signs");
        }
        Check(!SnapCompactRasterPosition({
                32767.f + Subpixel, 0.f }).Succeeded() &&
            !SnapCompactRasterPosition({
                -32768.f - Subpixel, 0.f }).Succeeded() &&
            !SnapCompactRasterPosition({
                std::numeric_limits<float>::infinity(), 0.f }).Succeeded(),
            "fixed-point snapping rejects out-of-domain and non-finite input");
        Check(!SnapCompactRasterPosition({
                std::nextafter(32767.f,
                    std::numeric_limits<float>::infinity()),
                0.f }).Succeeded() &&
            !SnapCompactRasterPosition({
                std::numeric_limits<float>::max(), 0.f }).Succeeded(),
            "unsnapped viewport bounds are checked before integer conversion");

        constexpr std::array<uint32_t, 4u> Counts = {
            2u, 4u, 8u, 16u
        };
        const std::array<CompactShadeFloat2, 3u> diagonalFirst = {{
            { 0.f, 0.f }, { 1.f, 0.f }, { 1.f, 1.f }
        }};
        const std::array<CompactShadeFloat2, 3u> diagonalSecond = {{
            { 0.f, 0.f }, { 1.f, 1.f }, { 0.f, 1.f }
        }};
        for (uint32_t count : Counts)
        {
            const Dx12StandardSamplePattern pattern =
                GetDx12StandardSamplePattern(count);
            const auto first = EvaluateCompactStandardRasterCoverage(
                diagonalFirst, 0, 0, pattern);
            const auto second = EvaluateCompactStandardRasterCoverage(
                diagonalSecond, 0, 0, pattern);
            const uint32_t fullMask = (1u << count) - 1u;
            Check(first.Succeeded() && second.Succeeded() &&
                    (first.coverageMask & second.coverageMask) == 0u &&
                    (first.coverageMask | second.coverageMask) == fullMask,
                "opposite diagonal edges own every standard sample exactly once");
            if (count == 2u)
            {
                Check(first.coverageMask == 0x3u &&
                        second.coverageMask == 0u,
                    "the canonical top-left owner, not its opposite, owns the 2x diagonal tie");
            }
        }

        const Dx12StandardSamplePattern pattern16 =
            GetDx12StandardSamplePattern(16u);
        const auto CheckSharedEdge = [&pattern16](
            const std::array<CompactShadeFloat2, 3u>& firstTriangle,
            const std::array<CompactShadeFloat2, 3u>& secondTriangle,
            const std::string& message)
        {
            const auto first = EvaluateCompactStandardRasterCoverage(
                firstTriangle, 0, 0, pattern16);
            const auto second = EvaluateCompactStandardRasterCoverage(
                secondTriangle, 0, 0, pattern16);
            Check(first.Succeeded() && second.Succeeded() &&
                    (first.coverageMask & second.coverageMask) == 0u &&
                    (first.coverageMask | second.coverageMask) == 0xffffu,
                message);
        };
        const std::array<CompactShadeFloat2, 3u> horizontalFirst = {{
            { -10.f, 0.5f }, { 10.f, 0.5f }, { 0.5f, -10.f }
        }};
        const std::array<CompactShadeFloat2, 3u> horizontalSecond = {{
            { -10.f, 0.5f }, { 0.5f, 10.f }, { 10.f, 0.5f }
        }};
        CheckSharedEdge(
            horizontalFirst,
            horizontalSecond,
            "opposite horizontal edges apply the D3D top-left rule exactly");
        const std::array<CompactShadeFloat2, 3u> verticalFirst = {{
            { 0.5f, -10.f }, { 0.5f, 10.f }, { -10.f, 0.5f }
        }};
        const std::array<CompactShadeFloat2, 3u> verticalSecond = {{
            { 0.5f, 10.f }, { 0.5f, -10.f }, { 10.f, 0.5f }
        }};
        CheckSharedEdge(
            verticalFirst,
            verticalSecond,
            "opposite vertical edges apply the D3D top-left rule exactly");
        const auto horizontalOwner =
            EvaluateCompactStandardRasterCoverage(
                horizontalSecond, 0, 0, pattern16);
        const auto verticalOwner =
            EvaluateCompactStandardRasterCoverage(
                verticalSecond, 0, 0, pattern16);
        Check((horizontalOwner.coverageMask & (1u << 12u)) != 0u,
            "the canonical lower triangle owns the horizontal 16x edge tie");
        Check((verticalOwner.coverageMask & (1u << 9u)) != 0u,
            "the canonical right triangle owns the vertical 16x edge tie");

        const std::array<CompactShadeFloat2, 3u> largeTriangle = {{
            { -2.f, -2.f }, { 4.f, -2.f }, { -2.f, 4.f }
        }};
        constexpr std::array<std::array<uint32_t, 3u>, 6u>
            Permutations = {{
                {{ 0u, 1u, 2u }}, {{ 1u, 2u, 0u }},
                {{ 2u, 0u, 1u }}, {{ 0u, 2u, 1u }},
                {{ 2u, 1u, 0u }}, {{ 1u, 0u, 2u }}
            }};
        for (size_t permutationIndex = 0u;
            permutationIndex < Permutations.size();
            ++permutationIndex)
        {
            std::array<CompactShadeFloat2, 3u> permuted{};
            for (size_t vertex = 0u; vertex < 3u; ++vertex)
            {
                permuted[vertex] = largeTriangle[
                    Permutations[permutationIndex][vertex]];
            }
            const auto normal = EvaluateCompactStandardRasterCoverage(
                permuted, 0, 0, pattern16, false);
            const auto mirrored = EvaluateCompactStandardRasterCoverage(
                permuted, 0, 0, pattern16, true);
            const bool positiveWinding = permutationIndex < 3u;
            Check(normal.Succeeded() && mirrored.Succeeded() &&
                    normal.coverageMask == 0xffffu &&
                    mirrored.coverageMask == normal.coverageMask &&
                    normal.frontFace == positiveWinding &&
                    mirrored.frontFace != normal.frontFace,
                "coverage is permutation-invariant while winding and mirroring control facing");
        }

        const std::array<CompactShadeFloat2, 3u> snapsDegenerate = {{
            { 0.f, 0.f },
            { 0.49f * Subpixel, 0.f },
            { 0.f, 0.49f * Subpixel }
        }};
        Check(!EvaluateCompactStandardRasterCoverage(
                snapsDegenerate, 0, 0, pattern16).Succeeded(),
            "triangles that become degenerate after 16.8 snapping fail closed");

        const std::array<CompactShadeFloat2, 3u> maximumTriangle = {{
            { -32768.f, -32768.f },
            { 32767.f, -32768.f },
            { -32768.f, 32767.f }
        }};
        const auto maximum = EvaluateCompactStandardRasterCoverage(
            maximumTriangle, -16384, -16384, pattern16);
        Check(maximum.Succeeded() && maximum.signedArea >
                int64_t(std::numeric_limits<uint32_t>::max()) &&
                maximum.signedArea < (int64_t(1) << 49) &&
                maximum.coverageMask == 0xffffu,
            "legal maximum viewport coordinates retain a 49-bit determinant");

        Dx12StandardSamplePattern invalidPattern;
        invalidPattern.sampleCount = 3u;
        Check(EvaluateCompactStandardRasterCoverage(
                largeTriangle, 0, 0, invalidPattern).reason ==
                FallbackReason::SampleCountUnsupported,
            "non-product raster sample counts fail closed");
    }

    void TestPerspectiveInterpolationAndGradients()
    {
        std::array<CompactShadeProjectedVertex, 3u> triangle = {{
            { { 0.f, 0.f }, 1.f, 0.2f, { 0.f, 0.f } },
            { { 2.f, 0.f }, 1.f, 0.4f, { 1.f, 0.f } },
            { { 0.f, 2.f }, 1.f, 0.6f, { 0.f, 1.f } }
        }};
        auto interpolation =
            ReconstructCompactPerspectiveInterpolation(
                triangle, { 0.5f, 0.5f });
        Check(interpolation.Succeeded(),
            "affine fixture reconstructs successfully");
        Check(Near(interpolation.affineBarycentrics[0], 0.5f) &&
                Near(interpolation.affineBarycentrics[1], 0.25f) &&
                Near(interpolation.affineBarycentrics[2], 0.25f) &&
                Near(interpolation.texCoord.x, 0.25f) &&
                Near(interpolation.texCoord.y, 0.25f) &&
                Near(interpolation.texCoordDx.x, 0.5f) &&
                Near(interpolation.texCoordDx.y, 0.f) &&
                Near(interpolation.texCoordDy.x, 0.f) &&
                Near(interpolation.texCoordDy.y, 0.5f) &&
                Near(interpolation.deviceDepth, 0.35f),
            "affine barycentrics, UV gradients, and device depth agree");

        const float majorScale = 1.875f;
        const float minorScale = 0.022097087f;
        const float offsetU = 4096.1171875f;
        const float offsetV = 2048.0703125f;
        const std::array<CompactShadeProjectedVertex, 3u>
            translatedAffineTriangle = {{
                {
                    { 1.f, 7.f },
                    1.f,
                    0.5f,
                    { offsetU, offsetV + minorScale }
                },
                {
                    { 7.f, 7.f },
                    1.f,
                    0.5f,
                    { offsetU + majorScale, offsetV + minorScale }
                },
                {
                    { 7.f, 1.f },
                    1.f,
                    0.5f,
                    { offsetU + majorScale, offsetV }
                }
            }};
        const auto translatedAffine =
            ReconstructCompactPerspectiveInterpolation(
                translatedAffineTriangle, { 4.5f, 6.5f });
        const float oneSixth = 1.f / 6.f;
        const float expectedMajorGradient =
            (translatedAffineTriangle[1].texCoord.x -
                translatedAffineTriangle[0].texCoord.x) * oneSixth;
        const float expectedMinorGradient =
            (translatedAffineTriangle[0].texCoord.y -
                translatedAffineTriangle[2].texCoord.y) * oneSixth;
        Check(translatedAffine.Succeeded() &&
                translatedAffine.texCoordDx.x == expectedMajorGradient &&
                translatedAffine.texCoordDx.y == 0.f &&
                translatedAffine.texCoordDy.x == 0.f &&
                translatedAffine.texCoordDy.y == expectedMinorGradient,
            "equal-W affine gradients retain exact edge-relative deltas with large UV offsets");

        triangle[1].reciprocalW = 0.5f;
        triangle[2].reciprocalW = 0.25f;
        interpolation = ReconstructCompactPerspectiveInterpolation(
            triangle, { 0.5f, 0.5f });
        Check(interpolation.Succeeded() &&
                Near(interpolation.perspectiveBarycentrics[0],
                    8.f / 11.f) &&
                Near(interpolation.perspectiveBarycentrics[1],
                    2.f / 11.f) &&
                Near(interpolation.perspectiveBarycentrics[2],
                    1.f / 11.f) &&
                Near(interpolation.texCoord.x, 2.f / 11.f) &&
                Near(interpolation.texCoord.y, 1.f / 11.f) &&
                Near(interpolation.texCoordDx.x, 52.f / 77.f) &&
                Near(interpolation.texCoordDx.y, 4.f / 77.f) &&
                Near(interpolation.texCoordDy.x, 12.f / 55.f) &&
                Near(interpolation.texCoordDy.y, 28.f / 55.f),
            "perspective correction and coarse 2x2 finite gradients match the reference pixel stamp");

        const auto topRight =
            ReconstructCompactPerspectiveInterpolation(
                triangle, { 1.5f, 0.5f });
        Check(topRight.Succeeded() &&
                Near(topRight.texCoord.x, 6.f / 7.f) &&
                Near(topRight.texCoord.y, 1.f / 7.f) &&
                Near(topRight.texCoordDx.x, interpolation.texCoordDx.x) &&
                Near(topRight.texCoordDx.y, interpolation.texCoordDx.y) &&
                Near(topRight.texCoordDy.x, interpolation.texCoordDy.x) &&
                Near(topRight.texCoordDy.y, interpolation.texCoordDy.y),
            "all lanes in one 2x2 stamp use the same coarse gradient pair");

        triangle[2].windowPosition = triangle[1].windowPosition;
        Check(!ReconstructCompactPerspectiveInterpolation(
                triangle, { 0.5f, 0.5f }).Succeeded(),
            "degenerate projected triangles fail closed");
        triangle[2].windowPosition = { 0.f, 2.f };
        triangle[1].reciprocalW =
            std::numeric_limits<float>::quiet_NaN();
        Check(!ReconstructCompactPerspectiveInterpolation(
                triangle, { 0.5f, 0.5f }).Succeeded(),
            "non-finite reconstruction input fails closed");
    }

    void TestTransformsFacingAndMotion()
    {
        CompactShadeAffineTransform transform;
        transform.rows = {{
            {{ 2.f, 0.f, 0.f, 10.f }},
            {{ 0.f, 3.f, 0.f, 20.f }},
            {{ 0.f, 0.f, 4.f, 30.f }}
        }};
        const CompactShadeFloat3 position = TransformCompactPosition(
            transform, { 1.f, 2.f, 3.f });
        const CompactShadeFloat3 direction = TransformCompactDirection(
            transform, { 1.f, 2.f, 3.f });
        Check(Near(position.x, 12.f) && Near(position.y, 26.f) &&
                Near(position.z, 42.f) && Near(direction.x, 2.f) &&
                Near(direction.y, 6.f) && Near(direction.z, 12.f),
            "position receives translation while normal/tangent direction does not");

        const std::array<CompactShadeFloat2, 3u> clockwiseWindow = {{
            { 0.f, 0.f }, { 1.f, 0.f }, { 0.f, 1.f }
        }};
        Check(DetermineCompactFrontFace(clockwiseWindow, false) &&
                !DetermineCompactFrontFace(clockwiseWindow, true),
            "view mirroring reverses the raster front-face convention");
        Check(!ShouldFlipCompactSurfaceNormals(
                false, true, { 0.f, 0.f, 1.f },
                { 0.f, 0.f, 1.f }) &&
            ShouldFlipCompactSurfaceNormals(
                false, false, { 0.f, 0.f, 1.f },
                { 0.f, 0.f, 1.f }) &&
            ShouldFlipCompactSurfaceNormals(
                true, true, { 0.f, 0.f, -1.f },
                { 0.f, 0.f, 1.f }) &&
            !ShouldFlipCompactSurfaceNormals(
                true, false, { 0.f, 0.f, 1.f },
                { 0.f, 0.f, 1.f }),
            "single-sided winding and double-sided view hemisphere remain distinct");

        CompactShadeMotionProjection motionInput;
        motionInput.currentWindowPosition = { 50.f, 40.f };
        motionInput.currentDeviceDepth = 0.25f;
        motionInput.previousClipPosition = { 0.5f, -0.25f, 0.4f, 2.f };
        motionInput.clipToWindowScale = { 100.f, -80.f };
        motionInput.clipToWindowBias = { 25.f, 20.f };
        motionInput.currentPixelOffset = { 0.25f, -0.25f };
        motionInput.previousPixelOffset = { -0.25f, 0.25f };
        const CompactShadeFloat4 motion =
            ReconstructCompactMotion(motionInput);
        Check(Near(motion.x, 0.5f) && Near(motion.y, -10.5f) &&
                Near(motion.z, -0.05f) && Near(motion.w, 1.f),
            "motion matches de-jittered current-to-previous pixel convention");
        motionInput.previousClipPosition.w = -1.f;
        const CompactShadeFloat4 invalidMotion =
            ReconstructCompactMotion(motionInput);
        Check(Near(invalidMotion.x, 0.f) &&
                Near(invalidMotion.y, 0.f) &&
                Near(invalidMotion.z, 0.f) &&
                Near(invalidMotion.w, 0.f),
            "previous positions behind the camera remain invalid motion");
    }

    void TestReferenceInterpolationLocations()
    {
        Check(SelectCompactReferenceCentroidSample(0x0fu, 4u) ==
                CompactReferencePixelCenter,
            "full coverage uses the pixel center for centroid varyings");
        Check(SelectCompactReferenceCentroidSample(0x0au, 4u) == 1u,
            "partial coverage selects the first covered sample");
        Check(SelectCompactReferenceCentroidSample(0x18u, 4u) == 3u,
            "bits above the active sample count are ignored");
        Check(SelectCompactReferenceCentroidSample(0u, 4u) ==
                CompactReferencePixelCenter,
            "empty masks cannot invent a centroid sample");

        // A steep textured triangle makes center and centroid interpolation
        // visibly different. UVSR intentionally keeps pos/prevPos/texCoord at
        // pixel center while using the selected point only for centroid
        // normal/tangent parity with Donut's SceneVertex contract.
        const std::array<CompactShadeProjectedVertex, 3u> steep = {{
            { { 0.f, 0.f }, 1.f, 0.9f, { 0.f, 0.f } },
            { { 16.f, 0.f }, 0.2f, 0.2f, { 8.f, 0.f } },
            { { 0.f, 1.f }, 0.1f, 0.1f, { 0.f, 1.f } }
        }};
        const auto center = ReconstructCompactPerspectiveInterpolation(
            steep, { 0.5f, 0.5f });
        const auto firstCovered =
            ReconstructCompactPerspectiveInterpolation(
                steep, { 0.625f, 0.125f });
        Check(center.Succeeded() && firstCovered.Succeeded() &&
                std::abs(center.texCoord.y -
                    firstCovered.texCoord.y) > 0.01f,
            "steep fixture detects accidental centroid UV reconstruction");
    }

    void TestSurfaceValidation()
    {
        const CompactShadeSurfaceValidationDesc valid =
            MakeValidSurface();
        Check(ValidateCompactShadeSurface(valid) ==
                FallbackReason::None,
            "fully coherent compact scene record validates");

        auto changed = valid;
        ++changed.geometryInstance.generation;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::GenerationMismatch,
            "generation reuse is detected before indirection");
        changed = valid;
        ++changed.geometryInstance.topologyEpochHigh;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::TopologyEpochMismatch,
            "wide topology epoch mismatch fails closed");
        changed = valid;
        ++changed.geometryInstance.lodRevision;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::LodRevisionMismatch,
            "LOD invalidation is explicit");
        changed = valid;
        ++changed.geometryInstance.deformationRevision;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::DeformationRevisionMismatch,
            "deformation invalidation is explicit");
        changed = valid;
        ++changed.geometryInstance.materialRevision;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::MaterialRevisionMismatch,
            "material invalidation is explicit");

        changed = valid;
        changed.geometryInstance.flags &=
            ~CompactGeometryInstanceFlag_DeformationSupported;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::UnsupportedDeformation,
            "unsupported deformation selects reference fallback");
        changed = valid;
        changed.geometryInstance.flags &=
            ~CompactGeometryInstanceFlag_MaterialSupported;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::UnsupportedMaterial,
            "unsupported material selects reference fallback");
        changed = valid;
        changed.geometryInstance.stableInstanceHandleLow = 0u;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::TransientMappingFailure,
            "zero stable instance identity fails closed");
        changed = valid;
        changed.geometryInstance.payloadClass = static_cast<uint32_t>(
            CompactGeometryPayloadClass::FullGBufferFallback);
        changed.geometryInstance.payloadFallbackReason =
            static_cast<uint32_t>(FallbackReason::UnsupportedMaterial);
        changed.geometryInstance.geometryRecordIndex = InvalidVisibilityWord;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::TransientMappingFailure,
            "fallback payload cannot enter reconstruction-only validation");

        changed = valid;
        changed.mappedGeometryInstanceHandle = MakeKey(
            0u, 2u, 1u).geometryInstanceHandle;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::TransientMappingFailure,
            "flattened handle-map aliasing is rejected");
        changed = valid;
        changed.key.geometryLocalPrimitiveIndex = 2u;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::PrimitiveOverflow,
            "geometry-local primitive overflow is rejected");
        changed = valid;
        changed.geometry.vertexBufferDescriptor =
            changed.scene.descriptorCount;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::TransientMappingFailure,
            "descriptor overflow is rejected before resource indexing");
        changed = valid;
        changed.geometry.indexBufferDescriptor = 1u;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::TransientMappingFailure,
            "an in-range texture descriptor cannot alias a raw geometry buffer");
        changed = valid;
        --changed.scene.rawBufferDescriptorCount;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::TransientMappingFailure,
            "descriptor partition count drift fails before resource indexing");
        changed = valid;
        changed.geometryInstanceRecordStride = 60u;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::FormatUnsupported,
            "record stride drift is rejected");

        changed = valid;
        changed.geometry.flags |= CompactGeometryFlag_Skinned;
        changed.geometry.previousPositionOffset =
            changed.geometry.positionOffset;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::UnsupportedDeformation,
            "skinned geometry requires a distinct previous-position stream");
        changed = valid;
        changed.triangleVertexIndices[2] = 3u;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::ReconstructionFailure,
            "out-of-range triangle index fails closed");
        changed = valid;
        changed.vertexBufferByteSize = 119u;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::ReconstructionFailure,
            "last-byte vertex stream truncation is detected");
        changed = valid;
        changed.geometry.indexOffset =
            std::numeric_limits<uint32_t>::max();
        changed.indexBufferByteSize =
            uint64_t(std::numeric_limits<uint32_t>::max()) + 1u;
        Check(ValidateCompactShadeSurface(changed) ==
                FallbackReason::ReconstructionFailure,
            "32-bit GPU address overflow fails closed");
    }

    void TestClosestSurfaceResolve()
    {
        std::array<CompactClosestSurfaceSample, 4u> samples{};
        const GpuVisibilityKey key = MakeKey();
        samples[0] = { key, 0.1f, true, FallbackReason::None };
        samples[1] = { key, 0.8f, true, FallbackReason::None };
        samples[2] = { key, 0.8f, true, FallbackReason::None };
        samples[3] = { key, 0.2f, true, FallbackReason::None };
        auto result = SelectCompactClosestSurface(
            samples.data(), 4u);
        Check(result.Succeeded() && result.foundSurface &&
                result.sampleIndex == 1u &&
                Near(result.deviceDepth, 0.8f),
            "reverse-Z resolve picks greatest depth with stable tie order");

        samples[3].key = {};
        samples[3].deviceDepth = 0.3f;
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.reason == FallbackReason::InvalidSentinel,
            "covered invalid key rejects the pixel instead of becoming sky");
        samples[3].deviceDepth = 0.f;
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.reason == FallbackReason::InvalidSentinel,
            "logical invalid sentinel is never reinterpreted as background");
        samples[3].key = {
            ClearedVisibilityWord, ClearedVisibilityWord };
        samples[3].deviceDepth = 0.3f;
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.reason == FallbackReason::InvalidHandleEncoding,
            "cleared key with covered depth fails as reserved generation");
        samples[3].deviceDepth =
            std::numeric_limits<float>::quiet_NaN();
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.reason == FallbackReason::ReconstructionFailure,
            "cleared key with non-finite depth fails transaction coherence");
        samples[3].key = { ClearedVisibilityWord, 1u };
        samples[3].deviceDepth = 0.f;
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.reason == FallbackReason::InvalidHandleEncoding,
            "partial cleared key is never background");
        samples[3].key = {
            ClearedVisibilityWord, ClearedVisibilityWord };
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.Succeeded() && result.sampleIndex == 1u,
            "exact reserved-generation clear remains valid background");

        samples[3] = { key, 0.f, true, FallbackReason::None };
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.Succeeded() && result.foundSurface &&
                result.sampleIndex == 1u,
            "exact reverse-Z far-plane hit remains covered at depth zero");
        samples[3].key.geometryLocalPrimitiveIndex =
            InvalidVisibilityWord;
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.reason == FallbackReason::InvalidSentinel,
            "partial sentinel at far-plane depth fails closed");
        samples[3] = { key, 0.2f, true, FallbackReason::None };

        samples[2].reconstructionValid = false;
        samples[2].reconstructionReason =
            FallbackReason::TopologyEpochMismatch;
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.reason == FallbackReason::TopologyEpochMismatch,
            "stale covered sample rejects even when another owner is valid");
        samples[2].reconstructionValid = true;
        samples[2].reconstructionReason = FallbackReason::None;
        samples[2].deviceDepth =
            std::numeric_limits<float>::quiet_NaN();
        result = SelectCompactClosestSurface(samples.data(), 4u);
        Check(result.reason == FallbackReason::ReconstructionFailure,
            "non-finite depth paired with a valid key fails closed");

        std::array<CompactClosestSurfaceSample, 4u> background{};
        for (CompactClosestSurfaceSample& sample : background)
        {
            sample.key = {
                ClearedVisibilityWord, ClearedVisibilityWord };
        }
        result = SelectCompactClosestSurface(background.data(), 4u);
        Check(result.Succeeded() && !result.foundSurface,
            "all-clear pixel preserves resolved background");
        Check(SelectCompactClosestSurface(samples.data(), 1u).reason ==
                FallbackReason::SampleCountUnsupported &&
            SelectCompactClosestSurface(nullptr, 4u).reason ==
                FallbackReason::ReconstructionFailure,
            "invalid resolve configuration fails closed");
    }

    void TestGroupingWeightAndReferenceFallback()
    {
        const GpuVisibilityKey key = MakeKey();
        std::array<SampleDescriptor, 4u> samples{};
        for (uint32_t index = 0u; index < 3u; ++index)
        {
            samples[index].covered = true;
            samples[index].packedKey = key;
            samples[index].barycentrics = { 0.2f, 0.3f };
            samples[index].frontFace = true;
            samples[index].linearDepth = 10.f;
            samples[index].worldPosition = { 0.f, 0.f, 0.f };
            samples[index].pixelFootprint = 1.f;
        }
        samples[2].linearDepth = 20.f;
        samples[2].worldPosition = { 1.f, 0.f, 0.f };
        const SampleGroupingResult grouped = GroupSamples(
            samples.data(), 4u);
        Check(grouped.reason == FallbackReason::None &&
                grouped.groupCount == 2u &&
                grouped.groups[0].representativeSample == 0u &&
                grouped.groups[0].sampleMask == 0x3u &&
                grouped.groups[0].sampleCount == 2u &&
                Near(grouped.groups[0].weight, 0.5f) &&
                grouped.groups[1].representativeSample == 2u &&
                grouped.groups[1].referenceFallback &&
                grouped.groups[1].fallbackReason ==
                    FallbackReason::UnsafeSharing &&
                Near(grouped.groups[1].weight, 0.25f) &&
                grouped.backgroundSampleCount == 1u &&
                Near(grouped.backgroundWeight, 0.25f) &&
                grouped.weightConserved &&
                Near(grouped.totalWeight, 1.f),
            "same key groups safely while incoherent sample becomes exact reference singleton");

        SampleGroupingOptions limited;
        limited.workListCapacity = 1u;
        const SampleGroupingResult overflow = GroupSamples(
            samples.data(), 4u, limited);
        Check(overflow.reason == FallbackReason::WorkListOverflow &&
                overflow.requiresFullReference &&
                overflow.groupCount == 3u &&
                overflow.weightConserved,
            "work-list overflow preserves all covered weights in reference fallback");
    }

    CompactShadeViewReconstructionProofInput MakeValidViewProof()
    {
        CompactShadeViewReconstructionProofInput input;
        input.worldToClip = {{
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f
        }};
        input.clipToWorld = input.worldToClip;
        input.worldToView = input.worldToClip;
        input.viewportOrigin = { 0.f, 0.f };
        input.viewportSize = { 64.f, 32.f };
        input.viewportSizeInv = { 1.f / 64.f, 1.f / 32.f };
        input.cameraDirectionOrPosition = { 0.f, 0.f, -5.f, 1.f };
        input.acceptedDepthValid = true;
        input.acceptedDepthHasGeometry = true;
        input.minimumStoredDeviceDepth = 0.25f;
        input.maximumStoredDeviceDepth = 0.75f;
        input.conservativeDepthErrorMargin = 1e-6f;
        return input;
    }

    void TestPreparedViewReconstructionProof()
    {
        const CompactShadeViewReconstructionProof valid =
            EvaluateCompactShadeViewReconstructionProof(
                MakeValidViewProof());
        Check(valid.safe && valid.reason == FallbackReason::None &&
                valid.minimumAbsoluteWorldW > 0.f &&
                valid.minimumAbsoluteViewW > 0.f &&
                std::isfinite(valid.maximumPixelFootprint),
            "finite inverse view and neighbor footprint are proven before commands");

        auto singularWorld = MakeValidViewProof();
        singularWorld.clipToWorld[15] = 0.f;
        Check(!EvaluateCompactShadeViewReconstructionProof(
                    singularWorld).safe,
            "singular clip-to-world inverse fails closed");

        auto nonFinite = MakeValidViewProof();
        nonFinite.worldToView[6] =
            std::numeric_limits<float>::quiet_NaN();
        Check(!EvaluateCompactShadeViewReconstructionProof(
                    nonFinite).safe,
            "non-finite view transform fails closed");

        auto crossingWorldW = MakeValidViewProof();
        crossingWorldW.worldToClip = {{
            0.f, 0.f, 0.f, 1.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            1.f, 0.f, 0.f, 0.f
        }};
        crossingWorldW.clipToWorld = crossingWorldW.worldToClip;
        Check(!EvaluateCompactShadeViewReconstructionProof(
                    crossingWorldW).safe,
            "homogeneous world W crossing zero over viewport neighbors is rejected");

        auto crossingViewW = MakeValidViewProof();
        crossingViewW.worldToView = {{
            0.f, 0.f, 0.f, 1.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            1.f, 0.f, 0.f, 0.f
        }};
        Check(!EvaluateCompactShadeViewReconstructionProof(
                    crossingViewW).safe,
            "homogeneous view W crossing zero fails before linear-depth division");

        auto overflowingWorld = MakeValidViewProof();
        overflowingWorld.worldToClip[0] = 1e-30f;
        overflowingWorld.clipToWorld[0] = 1e30f;
        Check(!EvaluateCompactShadeViewReconstructionProof(
                    overflowingWorld).safe,
            "inverse reconstruction with overflow-prone world range is rejected");

        auto invalidDepth = MakeValidViewProof();
        invalidDepth.minimumStoredDeviceDepth = 0.f;
        Check(!EvaluateCompactShadeViewReconstructionProof(
                    invalidDepth).safe,
            "geometry depth interval excludes background zero");
        invalidDepth = MakeValidViewProof();
        invalidDepth.minimumStoredDeviceDepth = 0.8f;
        invalidDepth.maximumStoredDeviceDepth = 0.2f;
        Check(!EvaluateCompactShadeViewReconstructionProof(
                    invalidDepth).safe,
            "reversed accepted-depth interval fails closed");

        auto invalidViewport = MakeValidViewProof();
        invalidViewport.viewportSizeInv[0] = 1.f / 63.f;
        Check(!EvaluateCompactShadeViewReconstructionProof(
                    invalidViewport).safe,
            "viewport reciprocal must be the exact captured binary32 value");

        auto empty = MakeValidViewProof();
        empty.acceptedDepthHasGeometry = false;
        empty.minimumStoredDeviceDepth = 0.f;
        empty.maximumStoredDeviceDepth = 0.f;
        empty.conservativeDepthErrorMargin = 0.f;
        empty.clipToWorld.fill(
            std::numeric_limits<float>::quiet_NaN());
        Check(EvaluateCompactShadeViewReconstructionProof(empty).safe,
            "canonical empty view needs no inverse divide proof");
        empty.minimumStoredDeviceDepth = -0.f;
        Check(!EvaluateCompactShadeViewReconstructionProof(empty).safe,
            "empty depth proof rejects negative zero identity drift");
    }

    void TestPreparedFrameOwnership()
    {
        const AdaptiveMsaaCompactShadePrepareParameters defaults;
        Check(defaults.guideRequired,
            "prepared compact shading preserves the guide path by default");

        static_assert(!std::is_copy_constructible_v<
            CompactShadePreparedFrame>);
        static_assert(!std::is_copy_assignable_v<
            CompactShadePreparedFrame>);
        static_assert(std::is_nothrow_move_constructible_v<
            CompactShadePreparedFrame>);
        static_assert(std::is_nothrow_move_assignable_v<
            CompactShadePreparedFrame>);

        CompactShadePreparedFrame first;
        Check(!first.IsSubmittable() && first.ViewCount() == 0u &&
                first.Reason() == FallbackReason::ReconstructionFailure,
            "default prepared frame is fail-closed");
        CompactShadePreparedFrame second(std::move(first));
        Check(!first.IsSubmittable() && !second.IsSubmittable(),
            "moving an invalid frame never manufactures submission authority");

        const CompactShadePreparedPhaseTransition guide =
            AdvanceCompactShadePreparedPhase(
                CompactShadePreparedPhase::Prepared,
                CompactShadePreparedPhase::GuideRecorded);
        const CompactShadePreparedPhaseTransition lighting =
            AdvanceCompactShadePreparedPhase(
                guide.phase,
                CompactShadePreparedPhase::Consumed);
        Check(guide.Succeeded() && lighting.Succeeded() &&
                lighting.phase == CompactShadePreparedPhase::Consumed,
            "prepared frame permits exactly guide then lighting consumption");
        const CompactShadePreparedPhaseTransition neutralNoGuideLighting =
            AdvanceCompactShadePreparedPhase(
                CompactShadePreparedPhase::Prepared,
                CompactShadePreparedPhase::Consumed,
                true);
        Check(neutralNoGuideLighting.Succeeded() &&
                neutralNoGuideLighting.phase ==
                    CompactShadePreparedPhase::Consumed,
            "an explicitly authorized no-guide token advances directly to lighting");
        Check(!AdvanceCompactShadePreparedPhase(
                    CompactShadePreparedPhase::Prepared,
                    CompactShadePreparedPhase::Consumed).Succeeded() &&
                !AdvanceCompactShadePreparedPhase(
                    CompactShadePreparedPhase::GuideRecorded,
                    CompactShadePreparedPhase::GuideRecorded).Succeeded() &&
                !AdvanceCompactShadePreparedPhase(
                    CompactShadePreparedPhase::Consumed,
                    CompactShadePreparedPhase::GuideRecorded).Succeeded() &&
                !AdvanceCompactShadePreparedPhase(
                    neutralNoGuideLighting.phase,
                    CompactShadePreparedPhase::Consumed,
                    true).Succeeded(),
            "prepared frame rejects skipped, repeated, and post-consume phases");
    }

    void TestPreparedLightingOutcomeSelection()
    {
        auto* texture = reinterpret_cast<nvrhi::ITexture*>(
            uintptr_t(0x1000u));
        auto* otherTexture = reinterpret_cast<nvrhi::ITexture*>(
            uintptr_t(0x2000u));
        auto* light = reinterpret_cast<donut::engine::Light*>(
            uintptr_t(0x3000u));
        auto* otherLight = reinterpret_cast<donut::engine::Light*>(
            uintptr_t(0x4000u));
        const uvsr::DirectionalLightVisibility neutral{};
        const uvsr::DirectionalLightVisibility predicted = {
            texture, light
        };

        struct OutcomeFixture
        {
            bool visibilitySuccess;
            uvsr::DirectionalLightVisibility actualDirectional;
            uint32_t expectedVariant;
            bool expectedDirectionalActive;
        };
        const std::array<OutcomeFixture, 4u> fixtures = {{
            { false, neutral, 0u, false },
            { true, neutral, 1u, false },
            { false, predicted, 2u, true },
            { true, predicted, 3u, true }
        }};
        for (const OutcomeFixture& fixture : fixtures)
        {
            const CompactShadeLightingOutcomeSelection selected =
                SelectCompactShadeLightingOutcome(
                    true,
                    fixture.visibilitySuccess,
                    predicted,
                    fixture.actualDirectional);
            Check(selected.Succeeded() &&
                    selected.variantIndex == fixture.expectedVariant &&
                    selected.visibilitySuccess ==
                        fixture.visibilitySuccess &&
                    selected.directionalActive ==
                        fixture.expectedDirectionalActive,
                "all four prepared visibility/directional outcomes select exact slots");
        }

        Check(!SelectCompactShadeLightingOutcome(
                    false, true, predicted, predicted).Succeeded(),
            "visibility success is rejected when no visibility outcome was requested");
        Check(!SelectCompactShadeLightingOutcome(
                    true,
                    true,
                    predicted,
                    { otherTexture, light }).Succeeded() &&
                !SelectCompactShadeLightingOutcome(
                    true,
                    true,
                    predicted,
                    { texture, otherLight }).Succeeded(),
            "active directional selection requires both exact predicted pointers");
        Check(!SelectCompactShadeLightingOutcome(
                    true,
                    false,
                    { texture, nullptr },
                    neutral).Succeeded() &&
                !SelectCompactShadeLightingOutcome(
                    true,
                    false,
                    predicted,
                    { texture, nullptr }).Succeeded(),
            "partial predicted or actual directional tuples fail closed");
        Check(!SelectCompactShadeLightingOutcome(
                    true,
                    false,
                    neutral,
                    predicted).Succeeded() &&
                SelectCompactShadeLightingOutcome(
                    true,
                    true,
                    neutral,
                    neutral).variantIndex == 1u,
            "neutral prediction cannot authorize active output but retains visibility variants");
        const CompactShadeLightingOutcomeSelection noGuideOutcome =
            SelectCompactShadeLightingOutcome(
                false, false, neutral, neutral);
        Check(noGuideOutcome.Succeeded() &&
                noGuideOutcome.variantIndex == 0u &&
                !noGuideOutcome.visibilitySuccess &&
                !noGuideOutcome.directionalActive,
            "a no-guide frame has exactly one fully neutral lighting outcome");
    }

    void TestPreparedDiagnosticsPublication()
    {
        constexpr uint32_t Capabilities =
            DiagnosticsCounterFallbackReasonsValid |
            DiagnosticsCounterGroupHistogramValid;
        Check(!ResolveCompactShadeDiagnosticsPublication(
                    false, true, 2u, 2u, Capabilities).produced &&
                !ResolveCompactShadeDiagnosticsPublication(
                    true, false, 2u, 2u, Capabilities).produced &&
                !ResolveCompactShadeDiagnosticsPublication(
                    true, true, 1u, 2u, Capabilities).produced,
            "diagnostics remain unpublished without preparation, exact outcome, and full dispatch");
        const CompactShadeDiagnosticsPublication publication =
            ResolveCompactShadeDiagnosticsPublication(
                true, true, 2u, 2u, Capabilities);
        Check(publication.produced &&
                publication.capabilityMask == Capabilities,
            "full exact prepared outcome publishes only its captured diagnostics capabilities");
    }

    void TestPreparationTopologySelection()
    {
        Check(
            static_cast<uint32_t>(SelectedCompactPreparationTopology) ==
                UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY,
            "the C++ topology identity matches the shader macro value");
        Check(
            CompactPreparationUsesSeparatedWorkLists ==
                (UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY == 1),
            "only the separated build advertises work-list scheduling");
        Check(CompactPreparationQueueCapacity == 128u,
            "the C++ accepted maximum matches the separated shader ballot bound");
        const CompactPreparationQueueDecision wave32 =
            EvaluateCompactPreparationQueue(128u, 32u, 27u);
        Check(wave32.reason == FallbackReason::None &&
                wave32.actualCapacity == 32u &&
                wave32.occupancy == 27u &&
                !wave32.overflow && !wave32.inlineFallback,
            "work-list capacity reports the actual wave32 limit");
        const CompactPreparationQueueDecision forcedOverflow =
            EvaluateCompactPreparationQueue(1u, 64u, 2u);
        Check(forcedOverflow.reason ==
                    FallbackReason::WorkListOverflow &&
                forcedOverflow.actualCapacity == 1u &&
                forcedOverflow.overflow &&
                forcedOverflow.inlineFallback,
            "a test-reduced capacity forces lossless inline fallback with the work-list reason");
        const CompactPreparationQueueDecision invalid =
            EvaluateCompactPreparationQueue(129u, 32u, 1u);
        Check(invalid.reason == FallbackReason::WorkListOverflow &&
                invalid.inlineFallback,
            "an invalid configured capacity fails closed to inline preparation");
    }

    std::filesystem::path FindCompactShadeSource()
    {
        const std::filesystem::path fromFile =
            std::filesystem::path(__FILE__).parent_path().parent_path() /
            "src" / "adaptive_msaa_compact_shade.cpp";
        if (std::filesystem::exists(fromFile))
            return fromFile;

        std::filesystem::path cursor =
            std::filesystem::current_path();
        for (uint32_t level = 0u; level < 8u; ++level)
        {
            const std::filesystem::path candidate = cursor / "src" /
                "adaptive_msaa_compact_shade.cpp";
            if (std::filesystem::exists(candidate))
                return candidate;
            if (!cursor.has_parent_path() ||
                cursor.parent_path() == cursor)
            {
                break;
            }
            cursor = cursor.parent_path();
        }
        return {};
    }

    void TestPreparedRecordingSourceContract()
    {
        const std::filesystem::path sourcePath =
            FindCompactShadeSource();
        Check(!sourcePath.empty(),
            "prepared recording source is available for contract audit");
        if (sourcePath.empty())
            return;
        std::ifstream stream(sourcePath, std::ios::binary);
        std::ostringstream contents;
        contents << stream.rdbuf();
        std::string source = contents.str();
        source.erase(
            std::remove(source.begin(), source.end(), '\r'),
            source.end());
        const std::string preparedSignature =
            "AdaptiveMsaaCompactShadePass::ResolveClosestSurface(\n"
            "            nvrhi::ICommandList* commandList,\n"
            "            CompactShadePreparedFrame& prepared)";
        const std::string nextSignature =
            "AdaptiveMsaaCompactShadePass::ResolveClosestSurface(\n"
            "            nvrhi::ICommandList* commandList,\n"
            "            const std::shared_ptr<const "
            "CompactSceneSnapshot>& snapshot,";
        const size_t begin = source.find(preparedSignature);
        const size_t end = begin == std::string::npos
            ? std::string::npos
            : source.find(nextSignature, begin + 1u);
        Check(begin != std::string::npos && end != std::string::npos &&
                end > begin,
            "prepared recording bodies are uniquely delimited");
        if (begin == std::string::npos || end == std::string::npos ||
            end <= begin)
        {
            return;
        }
        const std::string body = source.substr(begin, end - begin);
        constexpr std::array<const char*, 11u> Forbidden = {{
            "createBindingSet(",
            "createBuffer(",
            "GetOrCreateBindingSet(",
            "GetChildView(",
            "getDesc()",
            "FillPlanarViewConstants(",
            "FillLightConstants(",
            "FillLightProbeConstants(",
            "ValidateReadResources(",
            "PrepareReadStates(",
            "make_unique<"
        }};
        for (const char* forbidden : Forbidden)
        {
            Check(body.find(forbidden) == std::string::npos,
                std::string("prepared recording excludes ") +
                    forbidden);
        }
        Check(body.find("SelectCompactShadeLightingOutcome(") !=
                    std::string::npos &&
                body.find("lightingVariants[outcome.variantIndex]") !=
                    std::string::npos,
            "prepared lighting performs only exact captured outcome selection");
        Check(body.find("!implementation->validatedForSubmission") !=
                    std::string::npos &&
                body.find("!implementation->guideRequired &&\n"
                    "            !outcome.visibilitySuccess && "
                    "!outcome.directionalActive") !=
                    std::string::npos,
            "direct prepared lighting requires revalidation and skips "
            "guides only for a neutral no-guide token");
        Check(source.find(
                    "if (visibilityRequested && "
                    "!parameters.guideRequired)") !=
                    std::string::npos &&
                source.find(
                    "if (predictedDirectionalActive && "
                    "!parameters.guideRequired)") !=
                    std::string::npos &&
                source.find(
                    "if (guidePipeline)\n"
                    "            {\n"
                    "                nvrhi::BindingSetDesc "
                    "guideBindings;") !=
                    std::string::npos &&
                source.find(
                    "KeepTexture(parameters.lightingInputs.depth);") !=
                    std::string::npos &&
                source.find(
                    "rasterProof.surfaceKeyTarget.Get() !=\n"
                    "                    parameters.lightingInputs"
                    ".surfaceOwnership") !=
                    std::string::npos,
            "no-guide preparation rejects guide consumers and omits guide "
            "binding creation while retaining the lighting raster inputs");
    }

    void TestMixedFallbackShaderSourceContract()
    {
        const std::filesystem::path shadeSource = FindCompactShadeSource();
        Check(!shadeSource.empty(),
            "mixed-fallback shader sources are available for contract audit");
        if (shadeSource.empty())
            return;

        const std::filesystem::path sourceDirectory =
            shadeSource.parent_path();
        const auto ReadSource = [](const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            std::ostringstream contents;
            contents << stream.rdbuf();
            std::string source = contents.str();
            source.erase(
                std::remove(source.begin(), source.end(), '\r'),
                source.end());
            return source;
        };
        const std::string common = ReadSource(
            sourceDirectory / "adaptive_msaa_compact_shade_common.hlsli");
        const std::string resolve = ReadSource(
            sourceDirectory /
                "adaptive_msaa_compact_shade_resolve_cs.hlsl");
        const std::string lighting = ReadSource(
            sourceDirectory /
                "adaptive_msaa_compact_shade_lighting_cs.hlsl");

        Check(common.find(
                    "CompactVisibilityClassLocalFallbackBit") !=
                    std::string::npos &&
                common.find(
                    "geometryInstance.payloadClass ==\n"
                    "                CompactGeometryPayloadReconstruct") !=
                    std::string::npos &&
                common.find(
                    "geometryInstance.payloadClass ==\n"
                    "                CompactGeometryPayloadFullGBufferFallback") !=
                    std::string::npos &&
                common.find(
                    "geometryInstance.payloadFallbackReason == CompactReasonNone") !=
                    std::string::npos &&
                common.find(
                    "geometryInstance.payloadFallbackReason ==\n"
                    "                expectedFallbackReason") !=
                    std::string::npos,
            "shader ownership validation distinguishes local reconstruct payloads from exact immutable fallback payloads");
        Check(common.find(
                    "expectedFallbackReason == CompactReasonUnsupportedDeformation") !=
                    std::string::npos &&
                common.find(
                    "expectedFallbackReason == CompactReasonUnsupportedMaterial") !=
                    std::string::npos &&
                common.find(
                    "expectedFallbackReason == CompactReasonReconstructionFailure") !=
                    std::string::npos,
            "local shader fallback accepts only the CPU classifier's three proven reasons");
        Check(resolve.find(
                    "keys[sampleIndex], fallbackReason, localFallback") !=
                    std::string::npos &&
                lighting.find(
                    "keys[sampleIndex], fallbackReason, localFallback") !=
                    std::string::npos,
            "guide resolve and lighting both validate the decoded provenance against the same exact key");
        const size_t sameSurface = common.find(
            "bool CompactSameSurface(uint2 first, uint2 second)");
        const size_t clearedKeyHelper = common.find(
            "bool CompactIsClearedVisibilityKey(uint2 key)");
        Check(sameSurface != std::string::npos &&
                clearedKeyHelper != std::string::npos &&
                common.find(
                    "generation >= CompactMinimumGeneration &&",
                    sameSurface) < clearedKeyHelper &&
                common.find(
                    "generation <= CompactMaximumGeneration;",
                    sameSurface) < clearedKeyHelper,
            "shader SameSurface rejects the cleared key and every reserved generation before future grouping callers can share it");
        Check(resolve.find(
                    "!CompactIsClearedVisibilityKey(keys[sampleIndex]) ||") !=
                    std::string::npos &&
                resolve.find(
                    "asuint(depths[sampleIndex]) != 0u") !=
                    std::string::npos &&
                lighting.find(
                    "CompactIsClearedVisibilityKey(key) &&") !=
                    std::string::npos &&
                lighting.find(
                    "CompactFinite(depth) && asuint(depth) == 0u") !=
                    std::string::npos,
            "compact background requires the exact clear key, background class branch, and exact reverse-Z clear depth");
        Check(lighting.find(
                    "diagnosticsErrorBudgetReasons[sampleIndex] =\n"
                    "                    DiagnosticsErrorReasonNone;") !=
                std::string::npos,
            "successful singleton and per-sample lighting publishes an explicit accepted error-budget result");

        const size_t fallbackDepthBegin = common.find(
            "bool CompactValidFallbackDeviceDepth");
        const size_t interpolationBegin = fallbackDepthBegin ==
                std::string::npos
            ? std::string::npos
            : common.find("CompactInterpolation", fallbackDepthBegin);
        Check(fallbackDepthBegin != std::string::npos &&
                interpolationBegin != std::string::npos &&
                common.substr(
                    fallbackDepthBegin,
                    interpolationBegin - fallbackDepthBegin).find(
                        "deviceDepth >= 0.0f") != std::string::npos,
            "fallback identity containment retains finite depth zero for lighting");

        const size_t guidePredicate = resolve.find(
            "const bool validFallbackGuideOwner =");
        const size_t guideClear = guidePredicate == std::string::npos
            ? std::string::npos
            : resolve.find(
                "ClearCompactGuides(pixelPosition);", guidePredicate);
        const size_t guideWrite = resolve.find(
            "u_ResolvedDepth[pixelPosition] = ownerDepth;");
        Check(guidePredicate != std::string::npos &&
                resolve.find(
                    "CompactFinite(ownerDepth) && ownerDepth > 0.0f &&",
                    guidePredicate) != std::string::npos &&
                resolve.find(
                    "dot(fallbackNormal.xyz, fallbackNormal.xyz) > 1e-12f",
                    guidePredicate) != std::string::npos &&
                guideClear != std::string::npos &&
                guideWrite != std::string::npos &&
                guideClear < guideWrite,
            "every 2x/4x/8x/16x guide permutation clears all outputs before copying a depth-zero or degenerate-normal fallback owner");
    }

    void TestTexturedReconstructionSourceContract()
    {
        const std::filesystem::path shadeSource = FindCompactShadeSource();
        Check(!shadeSource.empty(),
            "textured compact shader sources are available for contract audit");
        if (shadeSource.empty())
            return;

        const std::filesystem::path sourceDirectory =
            shadeSource.parent_path();
        const auto ReadSource = [](const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            std::ostringstream contents;
            contents << stream.rdbuf();
            std::string source = contents.str();
            source.erase(
                std::remove(source.begin(), source.end(), '\r'),
                source.end());
            return source;
        };

        const std::string common = ReadSource(
            sourceDirectory / "adaptive_msaa_compact_shade_common.hlsli");
        const std::string resolve = ReadSource(
            sourceDirectory /
                "adaptive_msaa_compact_shade_resolve_cs.hlsl");
        const std::string lighting = ReadSource(
            sourceDirectory /
                "adaptive_msaa_compact_shade_lighting_cs.hlsl");
        const std::string visibility = ReadSource(
            sourceDirectory /
                "adaptive_msaa_compact_visibility_ps.hlsl");
        const std::string raster = ReadSource(
            sourceDirectory / "adaptive_msaa_compact_raster.cpp");
        const std::string shade = ReadSource(shadeSource);

        const size_t analyticGradientDefinition = common.find(
            "uint CompactGetAnalyticCoarseGradients(");
        const size_t analyticGradientCall = common.find(
            "result.reason = CompactGetAnalyticCoarseGradients(");
        Check(analyticGradientDefinition != std::string::npos &&
                analyticGradientCall != std::string::npos &&
                common.find(
                    "not a portable proof of Sample parity") !=
                    std::string::npos &&
                common.find(
                    "production enablement remains gated by the real full-G-buffer comparison") !=
                    std::string::npos,
            "analytic coarse gradients are named honestly and remain gated by the target-GPU full-G-buffer comparison");
        Check(common.find(
                    "material.normalTextureScale,\n"
                    "                AdaptiveMsaaCompactMaximumAbsoluteNormalMapScale") !=
                    std::string::npos &&
                common.find(
                    "AdaptiveMsaaCompactMaximumAbsoluteNormalTextureUvScale") !=
                    std::string::npos,
            "normal-texture reconstruction bounds amplitude and UV scale independently before sampling");
        Check(common.find(
                    "material.domain == MaterialDomain_AlphaTested") !=
                    std::string::npos &&
                common.find(
                    "geometryAlpha != materialAlpha") !=
                    std::string::npos &&
                common.find(
                    "opacity to re-discard or re-decide coverage") !=
                    std::string::npos &&
                common.find("clip(") == std::string::npos &&
                resolve.find("clip(") == std::string::npos &&
                lighting.find("clip(") == std::string::npos,
            "compute accepts only exact opaque/alpha domain pairs and never re-discards raster-finalized coverage");

        const size_t opacityTexture = visibility.find(
            "MaterialFlags_UseOpacityTexture");
        const size_t baseTexture = visibility.find(
            "MaterialFlags_UseBaseOrDiffuseTexture");
        const size_t saturation = visibility.find(
            "opacity = saturate(opacity);");
        const size_t cutoff = visibility.find(
            "clip(opacity - g_Material.alphaCutoff);");
        Check(opacityTexture != std::string::npos &&
                baseTexture != std::string::npos &&
                saturation != std::string::npos &&
                cutoff != std::string::npos &&
                opacityTexture < baseTexture &&
                baseTexture < saturation && saturation < cutoff &&
                visibility.find("else if", opacityTexture) < baseTexture &&
                visibility.find(
                    "t_Opacity.Sample(\n"
                    "            s_MaterialSampler,",
                    opacityTexture) != std::string::npos &&
                visibility.find(
                    "t_BaseOrDiffuse.Sample(\n"
                    "            s_MaterialSampler,",
                    baseTexture) != std::string::npos,
            "alpha-tested visibility preserves Donut opacity-texture precedence, saturation, cutoff, and the material sampler");

        Check(raster.find(
                    ": GBufferFillPass(device, commonPasses)") !=
                    std::string::npos &&
                raster.find(
                    "GBufferFillPass::SetupMaterial(") !=
                    std::string::npos &&
                shade.find(
                    "m_Impl->commonPasses\n"
                    "                            ->m_AnisotropicWrapSampler") !=
                    std::string::npos &&
                shade.find(
                    "m_Impl->commonPasses->m_AnisotropicWrapSampler") !=
                    std::string::npos,
            "visibility raster and compact compute both bind the shared CommonRenderPasses anisotropic-wrap sampler object");
    }
}

int main()
{
    TestStandardSamplePatterns();
    TestExactRasterCoverage();
    TestPerspectiveInterpolationAndGradients();
    TestTransformsFacingAndMotion();
    TestReferenceInterpolationLocations();
    TestSurfaceValidation();
    TestClosestSurfaceResolve();
    TestGroupingWeightAndReferenceFallback();
    TestPreparedViewReconstructionProof();
    TestPreparedFrameOwnership();
    TestPreparedLightingOutcomeSelection();
    TestPreparedDiagnosticsPublication();
    TestPreparationTopologySelection();
    TestPreparedRecordingSourceContract();
    TestMixedFallbackShaderSourceContract();
    TestTexturedReconstructionSourceContract();
    return g_Passed ? 0 : 1;
}
