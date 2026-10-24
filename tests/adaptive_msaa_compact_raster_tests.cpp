#include "adaptive_msaa_compact_raster.h"
#include "adaptive_msaa_texture_qualification.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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
            std::cerr << "Adaptive MSAA compact raster validation failed: "
                << message << '\n';
            g_Passed = false;
        }
    }

    struct ValidFixture
    {
        CompactRasterDrawPreflightDesc draw;

        ValidFixture()
        {
            const HandlePackResult handle =
                PackGeometryInstanceHandle(2u, 7u);
            Check(handle.Succeeded(),
                "the valid fixture handle must pack exactly");

            draw.sourceViewIndex = 3u;
            draw.sourceDrawIndex = 5u;
            draw.geometryPresent = true;
            draw.instanceMeshMatches = true;
            draw.bufferGroupMatchesMesh = true;
            draw.meshUsesTriangleList = true;
            draw.geometryUsesTriangleList = true;
            draw.deformationSupported = true;
            draw.materialSupported = true;
            draw.buffersPresent = true;
            draw.instanceBufferValid = true;
            draw.indexBufferValid = true;
            draw.vertexBufferValid = true;
            draw.fullGBufferRasterSupported = true;
            draw.alphaClassificationMatches = true;
            draw.cullModeMatches = true;
            draw.oneByOneShadingRate = true;
            draw.viewConditioningProofPresent = true;
            draw.viewConditioningSafe = true;
            draw.directionTransformFinite = true;
            draw.triangleConditioningProofPresent = true;
            draw.triangleConditioningSafe = true;
            draw.minimumClipW = 1.f;
            draw.maximumAbsoluteNdc = 2.f;
            draw.minimumAbsoluteProjectedArea = 1.f;
            draw.minimumAbsoluteSnappedArea = 65536u;
            draw.minimumSnapTieDistance = 0.001f;
            draw.minimumStoredDeviceDepth = 0.4f;
            draw.maximumStoredDeviceDepth = 0.6f;
            draw.storedDeviceDepthErrorMargin = 1.0e-6f;
            draw.maximumAbsoluteAffineBarycentric = 2.f;
            draw.minimumPerspectiveDenominator = 0.5f;
            draw.maximumAbsolutePerspectiveBarycentric = 2.f;
            draw.maximumAbsoluteTexCoord = 4.f;
            draw.maximumAbsoluteTexCoordGradient = 1.f;

            draw.instanceIndex = 1;
            draw.firstGeometryInstanceIndex = 2;
            draw.instanceCount = 4u;
            draw.instanceGeometryCount = 3u;
            draw.geometryLocalIndex = 1u;
            draw.handleMapCount = 8u;
            draw.mappedGeometryInstanceHandle = handle.handle;

            draw.instanceBufferByteSize = 448u;
            draw.instanceBufferStride = 112u;
            draw.indexBufferByteSize = 256u;
            draw.vertexBufferByteSize = 2048u;
            draw.meshFirstIndex = 2u;
            draw.geometryFirstIndex = 1u;
            draw.indexCount = 6u;
            draw.meshFirstVertex = 1u;
            draw.geometryFirstVertex = 2u;
            draw.vertexCount = 4u;
            draw.positionRange = { 64u, 128u, 12u };
            draw.previousPositionRange = { 256u, 128u, 12u };
            draw.texCoordRange = { 512u, 96u, 8u };
            draw.normalRange = { 768u, 48u, 4u };
            draw.tangentRange = { 896u, 48u, 4u };
            draw.immutableIndexProofPresent = true;
            draw.proofFirstIndex = 3u;
            draw.proofIndexCount = 6u;
            draw.proofFirstVertex = 3u;
            draw.proofVertexCount = 4u;
            draw.proofContentRevision = 17u;
            draw.proofMaximumIndex = 3u;

            draw.geometryInstanceRecordCount = 3u;
            draw.geometryInstanceRecordValid = true;
            draw.geometryInstanceDeformationSupported = true;
            draw.geometryInstanceMaterialSupported = true;
            draw.recordGeneration = 7u;
            draw.recordInstanceIndex = 1u;
            draw.recordGeometryIndex = 1u;
            draw.recordGeometryRecordIndex = 2u;
            draw.recordPrimitiveCount = 2u;
            draw.recordStableInstanceHandle = 0x445566778899aabbull;
            draw.recordPayloadClass =
                CompactGeometryPayloadClass::Reconstruct;
            draw.recordPayloadFallbackReason = FallbackReason::None;

            draw.expectedTopologyEpoch = 0x123456789abcdef0ull;
            draw.recordTopologyEpoch = draw.expectedTopologyEpoch;
            draw.expectedTopologyRevision = 11u;
            draw.recordTopologyRevision = 11u;
            draw.expectedLodRevision = 12u;
            draw.recordLodRevision = 12u;
            draw.expectedDeformationRevision = 13u;
            draw.recordDeformationRevision = 13u;
            draw.expectedMaterialRevision = 14u;
            draw.recordMaterialRevision = 14u;

            draw.geometryRecordCount = 3u;
            draw.materialRecordCount = 2u;
            draw.textureDescriptorCount = 4u;
            draw.rawBufferDescriptorCount = 4u;
            draw.descriptorCount = 8u;
            draw.recordIndexBufferDescriptor = 4u;
            // Raw descriptors occupy [textureDescriptorCount,
            // descriptorCount), so sparse resource reuse may still select
            // the last slot in that exact typed range.
            draw.recordVertexBufferDescriptor = 7u;
            draw.recordIndexOffset = 12u;
            draw.recordPositionOffset = 100u;
            draw.recordPreviousPositionOffset = 292u;
            draw.recordTexCoordOffset = 536u;
            draw.recordNormalOffset = 780u;
            draw.recordTangentOffset = 908u;
            draw.recordIndexCount = 6u;
            draw.recordVertexCount = 4u;
            draw.recordMaterialIndex = 1u;
            draw.expectedGeometryFlags =
                CompactGeometryFlag_DoubleSided;
            draw.recordGeometryFlags = draw.expectedGeometryFlags;
        }
    };

    void ExpectReason(
        const CompactRasterDrawPreflightDesc& draw,
        FallbackReason expected,
        const std::string& message)
    {
        Check(ValidateCompactRasterDraw(draw).reason == expected, message);
    }

    void TestValidDrawAndExactKeyMapping()
    {
        ValidFixture fixture;
        const CompactRasterDrawValidationResult result =
            ValidateCompactRasterDraw(fixture.draw);
        Check(result.Succeeded(),
            "a fully bounded exact visible triangle draw must pass");
        Check(result.draw.instanceIndex == 1u &&
                result.draw.geometryLocalIndex == 1u &&
                result.draw.startIndexLocation == 3u &&
                result.draw.startVertexLocation == 3u &&
                result.draw.indexCount == 6u &&
                result.draw.primitiveCount == 2u &&
                result.draw.geometryInstanceHandle ==
                    fixture.draw.mappedGeometryInstanceHandle &&
                result.draw.geometryRecordIndex == 2u,
            "validated draw arguments must preserve the exact flattened handle and geometry-local primitive domain");

        for (uint32_t primitive = 0u; primitive < 2u; ++primitive)
        {
            const GpuVisibilityKeyResult raster = MakeGpuVisibilityKey(
                result.draw.geometryInstanceHandle,
                primitive);
            Check(raster.Succeeded() &&
                    raster.key.geometryInstanceHandle ==
                        fixture.draw.mappedGeometryInstanceHandle &&
                    raster.key.geometryLocalPrimitiveIndex == primitive,
                "SV_PrimitiveID must remain geometry-local under the validated one-geometry draw");
        }
    }

    void TestStableFailClosedReasons()
    {
        const ValidFixture valid;
        CompactRasterDrawPreflightDesc changed = valid.draw;
        changed.geometryPresent = false;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "missing draw identity fails before later fields");

        changed = valid.draw;
        changed.geometryUsesTriangleList = false;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "non-triangle geometry cannot enter compact visibility");

        changed = valid.draw;
        changed.deformationSupported = false;
        ExpectReason(changed, FallbackReason::UnsupportedDeformation,
            "unsupported deformation rejects compact reconstruction");

        changed = valid.draw;
        changed.alphaClassificationMatches = false;
        ExpectReason(changed, FallbackReason::UnsupportedMaterial,
            "an alpha-domain mismatch cannot submit a compact prefix");

        changed = valid.draw;
        changed.materialSupported = false;
        ExpectReason(changed, FallbackReason::UnsupportedMaterial,
            "unqualified alpha-tested material rejects compact reconstruction");

        changed = valid.draw;
        changed.viewConditioningSafe = false;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "an eye-plane or conditioning hazard cannot reach command recording");

        changed = valid.draw;
        changed.directionTransformFinite = false;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "an unproven normal/tangent transform cannot reach compact shading");

        changed = valid.draw;
        changed.triangleConditioningSafe = false;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "a clip, snap, area, or orientation hazard cannot reach command recording");

        changed = valid.draw;
        changed.minimumAbsoluteSnappedArea =
            AdaptiveMsaaCompactMinimumSnappedTriangleArea - 1u;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "a post-snap sliver below the stable area floor rejects reconstruction");

        changed = valid.draw;
        changed.minimumStoredDeviceDepth = 0.f;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "stored depth zero is reserved for cleared reverse-Z background and cannot belong to a compact key");

        changed = valid.draw;
        changed.maximumStoredDeviceDepth =
            std::nextafter(1.f, 2.f);
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "a stored-depth interval outside the D32 viewport range fails closed");

        changed = valid.draw;
        changed.minimumStoredDeviceDepth = 0.7f;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "an inverted stored-depth interval cannot reach compact shading");

        changed = valid.draw;
        changed.storedDeviceDepthErrorMargin =
            std::numeric_limits<float>::quiet_NaN();
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "a non-finite D32 interpolation/storage margin fails closed");

        changed = valid.draw;
        changed.maximumAbsoluteAffineBarycentric =
            std::nextafter(
                AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric,
                std::numeric_limits<float>::infinity());
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "unbounded covered-sample barycentric arithmetic rejects reconstruction");

        changed = valid.draw;
        changed.minimumPerspectiveDenominator =
            std::nextafter(
                AdaptiveMsaaCompactMinimumPerspectiveDenominator,
                0.f);
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "a perspective denominator below the operation-safe floor cannot reach a shader reciprocal");

        changed = valid.draw;
        changed.maximumAbsoluteTexCoordGradient =
            std::numeric_limits<float>::infinity();
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "non-finite or overflowing SampleGrad operands fail during command-free preflight");

        changed = valid.draw;
        changed.indexBufferValid = false;
        ExpectReason(changed, FallbackReason::FormatUnsupported,
            "non-R32 raw index data is rejected explicitly");

        changed = valid.draw;
        changed.instanceIndex = -1;
        ExpectReason(changed, FallbackReason::TransientMappingFailure,
            "negative public scene indices cannot narrow into GPU identity");

        changed = valid.draw;
        changed.mappedGeometryInstanceHandle = InvalidVisibilityWord;
        ExpectReason(changed, FallbackReason::InvalidSentinel,
            "the clear/invalid sentinel cannot become covered ownership");

        changed = valid.draw;
        changed.mappedGeometryInstanceHandle = 2u;
        ExpectReason(changed, FallbackReason::InvalidHandleEncoding,
            "generation zero cannot masquerade as a live mapping");

        changed = valid.draw;
        changed.recordGeneration = 8u;
        ExpectReason(changed, FallbackReason::GenerationMismatch,
            "slot reuse with another generation is stale");

        changed = valid.draw;
        changed.recordStableInstanceHandle = 0u;
        ExpectReason(changed, FallbackReason::TransientMappingFailure,
            "a compact record without stable instance ownership is invalid");

        changed = valid.draw;
        changed.recordTopologyEpoch++;
        ExpectReason(changed, FallbackReason::TopologyEpochMismatch,
            "topology epoch changes invalidate the complete draw");

        changed = valid.draw;
        changed.recordLodRevision++;
        ExpectReason(changed, FallbackReason::LodRevisionMismatch,
            "LOD invalidation remains metadata rather than key identity");

        changed = valid.draw;
        changed.recordDeformationRevision++;
        ExpectReason(changed, FallbackReason::DeformationRevisionMismatch,
            "deformation revision changes are detected before submission");

        changed = valid.draw;
        changed.recordMaterialRevision++;
        ExpectReason(changed, FallbackReason::MaterialRevisionMismatch,
            "material revision changes are detected before submission");

        changed = valid.draw;
        changed.recordInstanceIndex++;
        ExpectReason(changed, FallbackReason::TransientMappingFailure,
            "a handle resolving to a different instance is rejected");

        changed = valid.draw;
        changed.indexCount = 5u;
        ExpectReason(changed, FallbackReason::PrimitiveOverflow,
            "partial triangles cannot redefine SV_PrimitiveID bounds");

        changed = valid.draw;
        changed.recordIndexBufferDescriptor = 8u;
        ExpectReason(changed, FallbackReason::TransientMappingFailure,
            "descriptor overflow cannot reach reconstruction");

        changed = valid.draw;
        changed.recordIndexBufferDescriptor = 3u;
        ExpectReason(changed, FallbackReason::TransientMappingFailure,
            "an in-range texture descriptor cannot be read as a raw index buffer");

        changed = valid.draw;
        changed.rawBufferDescriptorCount = 3u;
        ExpectReason(changed, FallbackReason::TransientMappingFailure,
            "descriptor partition count drift fails before typed indexing");

        changed = valid.draw;
        changed.recordPositionOffset++;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "GPU-scene stream offsets must match exact source streams");

        changed = valid.draw;
        changed.proofMaximumIndex = 4u;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "the immutable loader-time maximum index must fit the geometry");

        changed = valid.draw;
        changed.immutableIndexProofPresent = false;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "a draw cannot outlive or bypass its loader-time index proof");

        changed = valid.draw;
        changed.proofFirstIndex++;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "the loader-time proof must name the exact draw range");

        changed = valid.draw;
        changed.proofFirstVertex++;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "the loader-time proof must name the exact vertex stream origin");

        changed = valid.draw;
        changed.proofContentRevision = 0u;
        ExpectReason(changed, FallbackReason::ReconstructionFailure,
            "a loader proof without an exact monotonic content revision is invalid");

        changed = valid.draw;
        changed.meshFirstVertex = std::numeric_limits<uint64_t>::max();
        ExpectReason(changed, FallbackReason::CapacityOverflow,
            "wide source offset arithmetic must never wrap");
    }

    CompactRasterPreparedBindingDesc MakeValidBinding(
        const ValidFixture& fixture)
    {
        CompactRasterPreparedBindingDesc binding;
        binding.indexBindingByteOffset = 0u;
        binding.indexBindingIsR32Uint = true;
        binding.drawStartIndexLocation =
            static_cast<uint32_t>(fixture.draw.proofFirstIndex);
        binding.drawStartVertexLocation = 0u;
        binding.drawStartInstanceLocation = 0u;
        binding.pushStartInstanceLocation =
            static_cast<uint32_t>(fixture.draw.instanceIndex);
        binding.pushStartVertexLocation =
            static_cast<uint32_t>(fixture.draw.proofFirstVertex);
        binding.pushPositionOffset = static_cast<uint32_t>(
            fixture.draw.positionRange.byteOffset);
        binding.pushPreviousPositionOffset = static_cast<uint32_t>(
            fixture.draw.previousPositionRange.byteOffset);
        binding.pushTexCoordOffset = static_cast<uint32_t>(
            fixture.draw.texCoordRange.byteOffset);
        binding.pushNormalOffset = static_cast<uint32_t>(
            fixture.draw.normalRange.byteOffset);
        binding.pushTangentOffset = static_cast<uint32_t>(
            fixture.draw.tangentRange.byteOffset);
        binding.pushVisibilityClass =
            ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT;
        return binding;
    }

    void TestExactPreparedBindingProof()
    {
        const ValidFixture fixture;
        const CompactRasterDrawValidationResult validation =
            ValidateCompactRasterDraw(fixture.draw);
        Check(validation.Succeeded(),
            "binding proof requires the valid source fixture");
        if (!validation.Succeeded())
            return;

        CompactRasterPreparedBindingDesc binding =
            MakeValidBinding(fixture);
        Check(ValidateCompactRasterPreparedBinding(
                fixture.draw, validation.draw, binding) ==
                    FallbackReason::None,
            "raw stream bases, IA index base, and pushed firstVertex must form one exact binding proof");

        binding.pushPositionOffset++;
        Check(ValidateCompactRasterPreparedBinding(
                fixture.draw, validation.draw, binding) ==
                    FallbackReason::ReconstructionFailure,
            "a raw position stream base mismatch fails before submission");
        binding = MakeValidBinding(fixture);
        binding.drawStartVertexLocation = 1u;
        Check(ValidateCompactRasterPreparedBinding(
                fixture.draw, validation.draw, binding) ==
                    FallbackReason::ReconstructionFailure,
            "IA base vertex must remain zero when firstVertex is pushed for raw loads");
        binding = MakeValidBinding(fixture);
        binding.pushStartVertexLocation++;
        Check(ValidateCompactRasterPreparedBinding(
                fixture.draw, validation.draw, binding) ==
                    FallbackReason::ReconstructionFailure,
            "the pushed raw-load firstVertex cannot drift from the immutable proof");
        binding = MakeValidBinding(fixture);
        binding.indexBindingByteOffset = 4u;
        Check(ValidateCompactRasterPreparedBinding(
                fixture.draw, validation.draw, binding) ==
                    FallbackReason::ReconstructionFailure,
            "an IA byte base cannot be added silently to the proven startIndex");
        binding = MakeValidBinding(fixture);
        binding.indexBindingIsR32Uint = false;
        Check(ValidateCompactRasterPreparedBinding(
                fixture.draw, validation.draw, binding) ==
                    FallbackReason::ReconstructionFailure,
            "the prepared IA format must remain exact R32_UINT");
        binding = MakeValidBinding(fixture);
        binding.pushVisibilityClass =
            ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND;
        Check(ValidateCompactRasterPreparedBinding(
                fixture.draw, validation.draw, binding) ==
                    FallbackReason::ReconstructionFailure,
            "a reconstruct draw cannot submit with background or fallback class state");
    }

    void TestPrimitiveCoverageMetadata()
    {
        for (const uint32_t sampleCount : { 2u, 4u, 8u, 16u })
        {
            const uint32_t fullMask = (1u << sampleCount) - 1u;
            const uint32_t survivingMask = 1u << (sampleCount - 1u);
            const CompactRasterCoverageMetadata fullPrimitive =
                EvaluateCompactRasterCoverageMetadata(
                    sampleCount,
                    fullMask,
                    survivingMask);
            Check(fullPrimitive.valid &&
                    fullPrimitive.centroidSample ==
                        InvalidVisibilityWord,
                "full original primitive coverage keeps Reference centroid at pixel center despite partial depth survivors");

            const uint32_t firstPrimitiveSample = 1u;
            const uint32_t survivingPrimitiveSample =
                sampleCount - 1u;
            const uint32_t partialPrimitiveMask = sampleCount > 2u
                ? (1u << firstPrimitiveSample) |
                    (1u << survivingPrimitiveSample)
                : (1u << firstPrimitiveSample);
            const CompactRasterCoverageMetadata partialPrimitive =
                EvaluateCompactRasterCoverageMetadata(
                    sampleCount,
                    partialPrimitiveMask,
                    1u << survivingPrimitiveSample);
            Check(partialPrimitive.valid &&
                    partialPrimitive.centroidSample ==
                        firstPrimitiveSample,
                "partial primitive coverage retains the lowest originally covered centroid sample even when depth occludes it");

            const uint32_t outsidePrimitiveSample = 0u;
            Check(!EvaluateCompactRasterCoverageMetadata(
                    sampleCount,
                    partialPrimitiveMask,
                    1u << outsidePrimitiveSample).valid,
                "analytic coverage cannot invent or replace final SurfaceKey ownership");
            Check(!EvaluateCompactRasterCoverageMetadata(
                    sampleCount,
                    partialPrimitiveMask,
                    0u).valid,
                "a zero final ownership mask is never a reconstructable surface");
        }
        Check(!EvaluateCompactRasterCoverageMetadata(
                1u, 1u, 1u).valid,
            "unsupported sample counts cannot manufacture coverage metadata");
    }

    CompactClipBoundsConditioningInput MakeIdentityConditioningInput()
    {
        CompactClipBoundsConditioningInput input;
        input.positionBoundsMin = { 0.f, 0.f, 0.f };
        input.positionBoundsMax = { 0.f, 0.f, 0.f };
        input.objectToWorld[0][0] = 1.f;
        input.objectToWorld[1][1] = 1.f;
        input.objectToWorld[2][2] = 1.f;
        input.worldToClip[0][0] = 1.f;
        input.worldToClip[1][1] = 1.f;
        input.worldToClip[2][2] = 1.f;
        input.worldToClip[3][3] = 1.f;
        input.clipToWindowScale = { 1.f, 1.f };
        input.clipToWindowBias = { 0.f, 0.f };
        return input;
    }

    CompactClipBoundsConditioningInput ScaleProjection(
        CompactClipBoundsConditioningInput input,
        float scale)
    {
        for (std::array<float, 4>& row : input.worldToClip)
        {
            for (float& component : row)
                component *= scale;
        }
        return input;
    }

    CompactTriangleConditioningInput MakeTriangleConditioningInput()
    {
        CompactTriangleConditioningInput input;
        input.objectPositions = {{
            {{ -0.5f, -0.5f, 0.5f }},
            {{ 0.5f, -0.5f, 0.5f }},
            {{ 0.0f, 0.5f, 0.5f }}
        }};
        input.objectTexCoords = {{
            {{ 0.f, 0.f }},
            {{ 1.f, 0.f }},
            {{ 0.5f, 1.f }}
        }};
        input.objectToWorld[0][0] = 1.f;
        input.objectToWorld[1][1] = 1.f;
        input.objectToWorld[2][2] = 1.f;
        input.worldToClip[0][0] = 1.f;
        input.worldToClip[1][1] = 1.f;
        input.worldToClip[2][2] = 1.f;
        input.worldToClip[3][3] = 1.f;
        input.clipToWindowScale = { 100.f, -100.f };
        input.clipToWindowBias = { 640.f, 360.f };
        return input;
    }

    void TestTriangleClipSnapAndOrientationProof()
    {
        CompactTriangleConditioningInput ordinary =
            MakeTriangleConditioningInput();
        const CompactTriangleConditioning ordinaryResult =
            EvaluateCompactTriangleConditioning(ordinary);
        Check(ordinaryResult.safe &&
                ordinaryResult.triangleCount == 1u &&
                ordinaryResult.minimumAbsoluteProjectedArea > 1.f &&
                ordinaryResult.minimumAbsoluteSnappedArea >= 65536u &&
                ordinaryResult.minimumSnapTieDistance > 0.f &&
                ordinaryResult.minimumStoredDeviceDepth > 0.f &&
                ordinaryResult.minimumStoredDeviceDepth <= 0.5f &&
                ordinaryResult.maximumStoredDeviceDepth >= 0.5f &&
                ordinaryResult.maximumStoredDeviceDepth <= 1.f &&
                ordinaryResult.storedDeviceDepthErrorMargin > 0.f &&
                ordinaryResult.maximumAbsoluteAffineBarycentric > 0.f &&
                ordinaryResult.maximumAbsoluteAffineBarycentric <=
                    AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric &&
                ordinaryResult.minimumPerspectiveDenominator >=
                    AdaptiveMsaaCompactMinimumPerspectiveDenominator &&
                ordinaryResult.maximumAbsolutePerspectiveBarycentric <=
                    AdaptiveMsaaCompactMaximumAbsolutePerspectiveBarycentric &&
                ordinaryResult.maximumAbsoluteTexCoord <=
                    AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient &&
                ordinaryResult.maximumAbsoluteTexCoordGradient <=
                    AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient,
            "a well-conditioned interior triangle proves raster/depth identity and every barycentric, UV, gradient, and SampleGrad operand");

        std::swap(
            ordinary.objectPositions[1],
            ordinary.objectPositions[2]);
        Check(EvaluateCompactTriangleConditioning(ordinary).safe,
            "reversed winding remains provable when continuous and snapped orientation agree");

        const std::array<std::array<float, 3>, 6> clipContacts = {{
            {{ -1.f, 0.f, 0.5f }},
            {{ 1.f, 0.f, 0.5f }},
            {{ 0.f, -1.f, 0.5f }},
            {{ 0.f, 1.f, 0.5f }},
            {{ 0.f, 0.f, 0.f }},
            {{ 0.f, 0.f, 1.f }}
        }};
        for (size_t plane = 0u; plane < clipContacts.size(); ++plane)
        {
            CompactTriangleConditioningInput clipped =
                MakeTriangleConditioningInput();
            clipped.objectPositions[0] = clipContacts[plane];
            Check(!EvaluateCompactTriangleConditioning(clipped).safe,
                "every canonical clip-plane touch rejects generated fixed-point vertices before raster");
        }

        CompactTriangleConditioningInput snapTie =
            MakeTriangleConditioningInput();
        snapTie.clipToWindowScale = { 1.f, 1.f };
        snapTie.clipToWindowBias = { 0.f, 0.f };
        snapTie.objectPositions[0][0] = 1.f / 512.f;
        Check(!EvaluateCompactTriangleConditioning(snapTie).safe,
            "a half-subpixel snap tie plus its one-ULP transform interval rejects reconstruction");

        CompactTriangleConditioningInput snappedDegenerate =
            MakeTriangleConditioningInput();
        snappedDegenerate.clipToWindowScale = { 1.f, 1.f };
        snappedDegenerate.clipToWindowBias = { 0.f, 0.f };
        snappedDegenerate.objectPositions = {{
            {{ 0.f, 0.f, 0.5f }},
            {{ 0.003f, 0.f, 0.5f }},
            {{ 0.006f, 0.001f, 0.5f }}
        }};
        Check(!EvaluateCompactTriangleConditioning(
                snappedDegenerate).safe,
            "a nonzero continuous sliver that collapses after 16.8 snap is rejected");

        CompactTriangleConditioningInput grazing =
            MakeTriangleConditioningInput();
        grazing.objectPositions[2] =
            {{ 0.5f, -0.5f + 1.0e-10f, 0.5f }};
        Check(!EvaluateCompactTriangleConditioning(grazing).safe,
            "a grazing near-zero projected denominator cannot reach CompactInterpolateTriangle");

        CompactTriangleConditioningInput nondefaultDepth =
            MakeTriangleConditioningInput();
        nondefaultDepth.viewportMinimumDepth = 0.1f;
        Check(!EvaluateCompactTriangleConditioning(nondefaultDepth).safe,
            "a non-default viewport depth range has no compact D32 reconstruction proof");

        CompactTriangleConditioningInput nonfiniteUv =
            MakeTriangleConditioningInput();
        nonfiniteUv.objectTexCoords[1][0] =
            std::numeric_limits<float>::infinity();
        Check(!EvaluateCompactTriangleConditioning(nonfiniteUv).safe,
            "non-finite source UV data cannot enter analytic reconstruction");

        CompactTriangleConditioningInput overflowingUv =
            MakeTriangleConditioningInput();
        overflowingUv.objectTexCoords[0] = {{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max() }};
        Check(!EvaluateCompactTriangleConditioning(overflowingUv).safe,
            "finite UV inputs whose interpolation or gradient products can overflow select Reference pre-open");

        CompactTriangleConditioningInput overflowingDenominatorSquare =
            MakeTriangleConditioningInput();
        for (std::array<float, 4>& row :
            overflowingDenominatorSquare.worldToClip)
        {
            for (float& component : row)
                component *= 3.0e-19f;
        }
        Check(EvaluateCompactTriangleConditioning(
                overflowingDenominatorSquare).safe,
            "coarse finite gradients do not invent the retired analytic denominator-square overflow");
    }

    void TestClipBoundsConditioning()
    {
        CompactClipBoundsConditioningInput clipped =
            MakeIdentityConditioningInput();
        clipped.positionBoundsMin = { -2.f, -3.f, -1.f };
        clipped.positionBoundsMax = { 2.f, 3.f, 2.f };
        clipped.clipToWindowScale = { 100.f, -100.f };
        CompactClipBoundsConditioning conditioning =
            EvaluateCompactClipBoundsConditioning(clipped);
        Check(conditioning.safe && conditioning.minimumClipW < 1.f &&
                conditioning.maximumClipW > 1.f &&
                conditioning.maximumAbsoluteNdc >= 3.f &&
                conditioning.maximumAbsoluteNdc < 3.01f &&
                conditioning.maximumAbsoluteWindow > 300.f &&
                conditioning.maximumForwardError > 0.f,
            "ordinary canonical clipping has a finite conservative two-stage forward-error proof");

        CompactClipBoundsConditioningInput clipPlane =
            MakeIdentityConditioningInput();
        clipPlane.positionBoundsMin = { 1.f, 0.f, 1.f };
        clipPlane.positionBoundsMax = clipPlane.positionBoundsMin;
        Check(EvaluateCompactClipBoundsConditioning(clipPlane).safe,
            "exact canonical clip-plane contact is not rejected");

        CompactClipBoundsConditioningInput eyePlane =
            MakeIdentityConditioningInput();
        eyePlane.positionBoundsMin = { 0.f, 0.f, -1.f };
        eyePlane.positionBoundsMax = { 0.f, 0.f, 1.f };
        eyePlane.worldToClip[2][3] = 1.f;
        eyePlane.worldToClip[3][3] = 0.f;
        Check(!EvaluateCompactClipBoundsConditioning(eyePlane).safe,
            "an eye-plane crossing rejects reconstruction");
        eyePlane.positionBoundsMin[2] = 0.f;
        Check(!EvaluateCompactClipBoundsConditioning(eyePlane).safe,
            "an eye-plane touch rejects reconstruction");

        const std::array<float, 3> homogeneousScales = {
            std::ldexp(1.f, -16),
            1.f,
            std::ldexp(1.f, 16)
        };
        std::array<float, 3> homogeneousWindowBounds{};
        for (uint32_t index = 0u;
            index < homogeneousScales.size();
            ++index)
        {
            const CompactClipBoundsConditioning scaled =
                EvaluateCompactClipBoundsConditioning(
                    ScaleProjection(clipped, homogeneousScales[index]));
            Check(scaled.safe,
                "equivalent homogeneous projection scales remain eligible when their complete interval is normal");
            homogeneousWindowBounds[index] =
                scaled.maximumAbsoluteWindow;
        }
        Check(std::abs(homogeneousWindowBounds[0] -
                    homogeneousWindowBounds[1]) < 0.1f &&
                std::abs(homogeneousWindowBounds[1] -
                    homogeneousWindowBounds[2]) < 0.1f,
            "homogeneous scaling preserves the proven projected viewport domain");
        Check(!EvaluateCompactClipBoundsConditioning(
                ScaleProjection(
                    clipped,
                    std::numeric_limits<float>::denorm_min())).safe,
            "a projection that becomes zero under D3D FTZ rejects reconstruction");
        Check(!EvaluateCompactClipBoundsConditioning(
                ScaleProjection(clipped, std::ldexp(1.f, 126))).safe,
            "a projection outside the reciprocal normal-range proof rejects reconstruction");

        CompactClipBoundsConditioningInput cancellation =
            MakeIdentityConditioningInput();
        cancellation.positionBoundsMin = { -1.f, 0.f, 0.f };
        cancellation.positionBoundsMax = { 1.f, 0.f, 0.f };
        cancellation.objectToWorld[0][3] = 16777216.f;
        cancellation.worldToClip[0][3] = 1.f;
        cancellation.worldToClip[1][3] = 0.f;
        cancellation.worldToClip[2][3] = 0.f;
        cancellation.worldToClip[3][3] = -16777214.f;
        Check(!EvaluateCompactClipBoundsConditioning(cancellation).safe,
            "large two-stage cancellation cannot hide a W-sign uncertainty from forward error");

        CompactClipBoundsConditioningInput fusedOneUlpW =
            MakeIdentityConditioningInput();
        fusedOneUlpW.positionBoundsMin = { 1.f, 0.f, 0.f };
        fusedOneUlpW.positionBoundsMax =
            fusedOneUlpW.positionBoundsMin;
        fusedOneUlpW.worldToClip[0][3] = 1.f;
        fusedOneUlpW.worldToClip[3][3] =
            std::nextafter(-1.f, 0.f);
        Check(!EvaluateCompactClipBoundsConditioning(fusedOneUlpW).safe,
            "a legal one-ULP fused dot result cannot turn near-cancelled W into an accepted reciprocal domain");

        CompactClipBoundsConditioningInput worldOverflow =
            MakeIdentityConditioningInput();
        worldOverflow.positionBoundsMin = { 1.f, 0.f, 0.f };
        worldOverflow.positionBoundsMax = worldOverflow.positionBoundsMin;
        worldOverflow.objectToWorld[0][0] =
            std::numeric_limits<float>::max();
        worldOverflow.objectToWorld[0][3] =
            -std::numeric_limits<float>::max();
        Check(!EvaluateCompactClipBoundsConditioning(worldOverflow).safe,
            "first-stage overflow is rejected even when exact terms cancel");

        CompactClipBoundsConditioningInput directionOverflow =
            MakeIdentityConditioningInput();
        directionOverflow.positionBoundsMin = { 0.f, 0.f, 0.f };
        directionOverflow.positionBoundsMax =
            directionOverflow.positionBoundsMin;
        directionOverflow.objectToWorld[0][0] =
            std::numeric_limits<float>::max();
        Check(!EvaluateCompactClipBoundsConditioning(directionOverflow).safe &&
                !EvaluateCompactClipBoundsConditioning(directionOverflow).
                    directionTransformFinite,
            "packed normal/tangent inputs cannot overflow a finite prepared object transform");

        CompactClipBoundsConditioningInput clipOverflow =
            MakeIdentityConditioningInput();
        clipOverflow.positionBoundsMin = { 1.f, 0.f, 0.f };
        clipOverflow.positionBoundsMax = clipOverflow.positionBoundsMin;
        clipOverflow.worldToClip[0][0] =
            std::numeric_limits<float>::max();
        clipOverflow.worldToClip[3][0] =
            -std::numeric_limits<float>::max();
        Check(!EvaluateCompactClipBoundsConditioning(clipOverflow).safe,
            "second-stage overflow is rejected even when exact terms cancel");

        CompactClipBoundsConditioningInput varyingW =
            MakeIdentityConditioningInput();
        varyingW.positionBoundsMin = { -1.f, -1.f, -0.5f };
        varyingW.positionBoundsMax = { 1.f, 1.f, 0.5f };
        varyingW.worldToClip[2][3] = 1.f;
        varyingW.worldToClip[3][3] = 2.f;
        Check(EvaluateCompactClipBoundsConditioning(varyingW).safe,
            "positive varying W uses linear-fractional corner extrema without rejecting a normal perspective volume");

        CompactClipBoundsConditioningInput viewport =
            MakeIdentityConditioningInput();
        viewport.positionBoundsMin = { 32766.f, 0.f, 0.f };
        viewport.positionBoundsMax = viewport.positionBoundsMin;
        Check(EvaluateCompactClipBoundsConditioning(viewport).safe,
            "positive viewport coordinates with a full error margin remain eligible");
        viewport.positionBoundsMin[0] = 32767.f;
        viewport.positionBoundsMax[0] = 32767.f;
        Check(!EvaluateCompactClipBoundsConditioning(viewport).safe,
            "the positive viewport boundary fails when forward error can leave the D3D domain");
        viewport.positionBoundsMin[0] = -32767.f;
        viewport.positionBoundsMax[0] = -32767.f;
        Check(EvaluateCompactClipBoundsConditioning(viewport).safe,
            "negative viewport coordinates with a full error margin remain eligible");
        viewport.positionBoundsMin[0] = -32768.f;
        viewport.positionBoundsMax[0] = -32768.f;
        Check(!EvaluateCompactClipBoundsConditioning(viewport).safe,
            "the negative viewport boundary fails when forward error can leave the D3D domain");

        CompactClipBoundsConditioningInput viewportOrigin =
            MakeIdentityConditioningInput();
        viewportOrigin.positionBoundsMin = { -2.f, -2.f, -1.f };
        viewportOrigin.positionBoundsMax = { 2.f, 2.f, 1.f };
        viewportOrigin.clipToWindowScale = { 100.f, -50.f };
        viewportOrigin.clipToWindowBias = { 640.f, 360.f };
        Check(EvaluateCompactClipBoundsConditioning(viewportOrigin).safe,
            "nonzero viewport origins and inverted Y scales are included in the proof");
        viewportOrigin.clipToWindowBias[0] = 40000.f;
        Check(!EvaluateCompactClipBoundsConditioning(viewportOrigin).safe,
            "viewport origin can conservatively force Reference before fixed-point reconstruction");

        CompactClipBoundsConditioningInput depthDomain =
            MakeIdentityConditioningInput();
        depthDomain.positionBoundsMin = { 0.f, 0.f, 16383.f };
        depthDomain.positionBoundsMax = depthDomain.positionBoundsMin;
        Check(EvaluateCompactClipBoundsConditioning(depthDomain).safe,
            "a depth ratio with a complete interior error margin remains eligible");
        depthDomain.positionBoundsMin[2] = 16384.f;
        depthDomain.positionBoundsMax[2] = 16384.f;
        Check(!EvaluateCompactClipBoundsConditioning(depthDomain).safe,
            "the depth conditioning boundary fails when forward error can cross it");

        const CompactClipBoundsConditioning zeroResult =
            EvaluateCompactClipBoundsConditioning(
                MakeIdentityConditioningInput());
        CompactClipBoundsConditioningInput subnormalPosition =
            MakeIdentityConditioningInput();
        subnormalPosition.positionBoundsMin[0] =
            std::numeric_limits<float>::denorm_min();
        subnormalPosition.positionBoundsMax[0] =
            std::numeric_limits<float>::denorm_min();
        const CompactClipBoundsConditioning subnormalResult =
            EvaluateCompactClipBoundsConditioning(subnormalPosition);
        Check(zeroResult.safe && subnormalResult.safe &&
                zeroResult.maximumAbsoluteWindow ==
                    subnormalResult.maximumAbsoluteWindow,
            "D3D FTZ input canonicalization is deterministic at loader and proof time");

        CompactClipBoundsConditioningInput nonsymmetric =
            MakeIdentityConditioningInput();
        nonsymmetric.positionBoundsMin = { 1.f, 2.f, 3.f };
        nonsymmetric.positionBoundsMax = nonsymmetric.positionBoundsMin;
        nonsymmetric.objectToWorld = {{
            {{ 1.f, 2.f, 3.f, 4.f }},
            {{ 5.f, 6.f, 7.f, 8.f }},
            {{ 9.f, 10.f, 11.f, 12.f }}
        }};
        nonsymmetric.worldToClip = {{
            {{ 1.f, 2.f, 3.f, 0.01f }},
            {{ 4.f, 5.f, 6.f, 0.02f }},
            {{ 7.f, 8.f, 9.f, 0.03f }},
            {{ 10.f, 11.f, 12.f, 4.f }}
        }};
        const CompactClipBoundsConditioning orientation =
            EvaluateCompactClipBoundsConditioning(nonsymmetric);
        const double expectedW =
            18.0 * double(nonsymmetric.worldToClip[0][3]) +
            46.0 * double(nonsymmetric.worldToClip[1][3]) +
            74.0 * double(nonsymmetric.worldToClip[2][3]) + 4.0;
        Check(orientation.safe &&
                double(orientation.minimumClipW) < expectedW &&
                double(orientation.maximumClipW) > expectedW &&
                orientation.maximumClipW - orientation.minimumClipW <
                    0.01f,
            "nonsymmetric matrices prove the exact float3x4 row-dot then float4 row-vector orientation");

        CompactClipBoundsConditioningInput nonfinite =
            MakeIdentityConditioningInput();
        nonfinite.positionBoundsMin[0] =
            std::numeric_limits<float>::infinity();
        Check(!EvaluateCompactClipBoundsConditioning(nonfinite).safe,
            "non-finite position bounds fail closed");
        nonfinite = MakeIdentityConditioningInput();
        nonfinite.objectToWorld[0][0] =
            std::numeric_limits<float>::quiet_NaN();
        Check(!EvaluateCompactClipBoundsConditioning(nonfinite).safe,
            "non-finite object transforms fail closed");
        nonfinite = MakeIdentityConditioningInput();
        nonfinite.worldToClip[0][0] =
            std::numeric_limits<float>::infinity();
        Check(!EvaluateCompactClipBoundsConditioning(nonfinite).safe,
            "non-finite view transforms fail closed");
        nonfinite = MakeIdentityConditioningInput();
        nonfinite.clipToWindowScale[0] =
            std::numeric_limits<float>::quiet_NaN();
        Check(!EvaluateCompactClipBoundsConditioning(nonfinite).safe,
            "non-finite viewport scales fail closed");
        nonfinite = MakeIdentityConditioningInput();
        nonfinite.clipToWindowBias[0] =
            std::numeric_limits<float>::infinity();
        Check(!EvaluateCompactClipBoundsConditioning(nonfinite).safe,
            "non-finite viewport origins fail closed");
        nonfinite = MakeIdentityConditioningInput();
        nonfinite.clipToWindowScale[0] = 0.f;
        Check(!EvaluateCompactClipBoundsConditioning(nonfinite).safe,
            "a degenerate viewport scale fails closed");
        nonfinite = MakeIdentityConditioningInput();
        nonfinite.viewportMaximumDepth =
            std::nextafter(1.f, 0.f);
        Check(!EvaluateCompactClipBoundsConditioning(nonfinite).safe,
            "the compact D32 proof rejects a non-default viewport depth range");
    }

    void TestExactTransformTransactionIdentity()
    {
        const CompactClipBoundsConditioningInput prepared =
            MakeIdentityConditioningInput();
        CompactClipBoundsConditioningInput current = prepared;
        Check(SameCompactRasterTransformIdentity(prepared, current),
            "unchanged object, camera, projection, and viewport operands preserve the transaction");

        current.objectToWorld[0][3] =
            std::nextafter(current.objectToWorld[0][3], 1.f);
        Check(!SameCompactRasterTransformIdentity(prepared, current),
            "one-bit object-transform mutation invalidates preparation");
        current = prepared;
        current.worldToClip[2][1] =
            std::nextafter(current.worldToClip[2][1], 1.f);
        Check(!SameCompactRasterTransformIdentity(prepared, current),
            "one-bit camera/projection mutation invalidates preparation");
        current = prepared;
        current.clipToWindowScale[0] =
            std::nextafter(current.clipToWindowScale[0], 2.f);
        Check(!SameCompactRasterTransformIdentity(prepared, current),
            "one-bit viewport-scale mutation invalidates preparation");
        current = prepared;
        current.clipToWindowBias[1] = -0.f;
        Check(!SameCompactRasterTransformIdentity(prepared, current),
            "bitwise identity does not silently alias signed-zero viewport operands");
        current = prepared;
        current.viewportMaximumDepth =
            std::nextafter(current.viewportMaximumDepth, 0.f);
        Check(!SameCompactRasterTransformIdentity(prepared, current),
            "one-bit viewport-depth mutation invalidates preparation");
    }

    void TestWholeListTransaction()
    {
        ValidFixture first;
        ValidFixture second;
        second.draw.sourceViewIndex = 4u;
        second.draw.sourceDrawIndex = 7u;
        second.draw.recordMaterialRevision++;
        const std::array<CompactRasterDrawPreflightDesc, 2u> draws = {
            first.draw,
            second.draw
        };
        const CompactRasterPreflightResult rejected =
            PreflightCompactRasterDraws(draws.data(), draws.size());
        Check(!rejected.Succeeded() &&
                rejected.reason == FallbackReason::MaterialRevisionMismatch &&
                rejected.failingView == 4u &&
                rejected.failingDraw == 7u &&
                rejected.drawCount == 2u &&
                rejected.triangleCount == 0u,
            "one later invalid draw rejects the complete compact frame without accepting a prefix");

        second.draw.recordMaterialRevision--;
        const std::array<CompactRasterDrawPreflightDesc, 2u> validDraws = {
            first.draw,
            second.draw
        };
        const CompactRasterPreflightResult accepted =
            PreflightCompactRasterDraws(
                validDraws.data(),
                validDraws.size());
        Check(accepted.Succeeded() && accepted.drawCount == 2u &&
                accepted.triangleCount == 4u &&
                accepted.reconstructDrawCount == 2u &&
                accepted.reconstructTriangleCount == 4u &&
                accepted.fallbackDrawCount == 0u &&
                accepted.fallbackTriangleCount == 0u,
            "the exact validated list reports deterministic draw and triangle counts");

        second.draw.viewConditioningSafe = false;
        const std::array<CompactRasterDrawPreflightDesc, 2u>
            viewUnsafeDraws = { first.draw, second.draw };
        const CompactRasterPreflightResult viewFallback =
            PreflightCompactRasterDraws(
                viewUnsafeDraws.data(),
                viewUnsafeDraws.size());
        Check(viewFallback.Succeeded() &&
                viewFallback.transientViewConditioningFailure &&
                viewFallback.reconstructDrawCount == 1u &&
                viewFallback.reconstructTriangleCount == 2u &&
                viewFallback.fallbackDrawCount == 1u &&
                viewFallback.fallbackTriangleCount == 2u,
            "view conditioning selects one local full-G-buffer draw without rejecting the compact prefix or suffix");

        const CompactRasterPreflightResult topologyRequest =
            ValidateCompactRasterTopologyAvailability(
                viewFallback,
                false);
        Check(!topologyRequest.Succeeded() &&
                topologyRequest.reason == FallbackReason::None &&
                topologyRequest.mixedTopologyRequired &&
                topologyRequest.drawCount == viewFallback.drawCount &&
                topologyRequest.triangleCount ==
                    viewFallback.triangleCount,
            "a compact-only frame requests one bounded mixed-topology upgrade without producing a latchable failure reason");
        const CompactRasterPreflightResult topologyAvailable =
            ValidateCompactRasterTopologyAvailability(
                viewFallback,
                true);
        Check(topologyAvailable.Succeeded() &&
                !topologyAvailable.mixedTopologyRequired,
            "the same classified sequence remains eligible once mixed attachments are available");

        const CompactRasterPreflightResult empty =
            PreflightCompactRasterDraws(nullptr, 0u);
        Check(empty.Succeeded() && empty.drawCount == 0u &&
                empty.triangleCount == 0u,
            "an exact empty visible list is a valid compact no-op");
        Check(!PreflightCompactRasterDraws(nullptr, 1u).Succeeded(),
            "a missing non-empty list fails closed");
    }

    void TestMixedDrawClassification()
    {
        ValidFixture reconstruct;
        const CompactRasterDrawClassificationResult reconstruction =
            ClassifyCompactRasterDraw(reconstruct.draw);
        Check(reconstruction.Succeeded() &&
                reconstruction.Reconstructs() &&
                reconstruction.fallbackProvenance ==
                    CompactRasterFallbackProvenance::None &&
                reconstruction.reason == FallbackReason::None,
            "a complete dense reconstruction record stays on compact visibility");

        ValidFixture unqualifiedTextured;
        unqualifiedTextured.draw.recordMaterialTextureMask = 1u;
        unqualifiedTextured.draw.textureReconstructionQualified = false;
        const CompactRasterDrawClassificationResult texturedFallback =
            ClassifyCompactRasterDraw(unqualifiedTextured.draw);
        Check(texturedFallback.Succeeded() &&
                texturedFallback.UsesLocalFullGBufferFallback() &&
                texturedFallback.reason ==
                    FallbackReason::TextureQualificationMismatch &&
                texturedFallback.draw.geometryInstanceHandle ==
                    unqualifiedTextured.draw.mappedGeometryInstanceHandle,
            "an unqualified textured draw retains its exact key through the local full-G-buffer path");

        ValidFixture unqualifiedUntextured;
        unqualifiedUntextured.draw.textureReconstructionQualified = false;
        const CompactRasterDrawClassificationResult untexturedCompact =
            ClassifyCompactRasterDraw(unqualifiedUntextured.draw);
        Check(untexturedCompact.Succeeded() &&
                untexturedCompact.Reconstructs() &&
                untexturedCompact.reason == FallbackReason::None,
            "view-local texture qualification cannot demote an untextured compact draw");

        unqualifiedUntextured.draw.sourceDrawIndex = 6u;
        const std::array<CompactRasterDrawPreflightDesc, 2u>
            mixedQualificationDraws = {
                unqualifiedTextured.draw,
                unqualifiedUntextured.draw
            };
        const CompactRasterPreflightResult mixedQualification =
            PreflightCompactRasterDraws(
                mixedQualificationDraws.data(),
                mixedQualificationDraws.size());
        Check(mixedQualification.Succeeded() &&
                mixedQualification.reconstructDrawCount == 1u &&
                mixedQualification.reconstructTriangleCount == 2u &&
                mixedQualification.fallbackDrawCount == 1u &&
                mixedQualification.fallbackTriangleCount == 2u &&
                !mixedQualification.transientViewConditioningFailure,
            "an unqualified textured draw and untextured compact draw form one exact mixed-topology transaction");
        const CompactRasterPreflightResult mixedQualificationTopology =
            ValidateCompactRasterTopologyAvailability(
                mixedQualification,
                false);
        Check(!mixedQualificationTopology.Succeeded() &&
                mixedQualificationTopology.reason == FallbackReason::None &&
                mixedQualificationTopology.mixedTopologyRequired,
            "the per-view texture qualification split requests mixed attachments without latching a whole-scene failure");

        ValidFixture staleUnqualifiedTextured = unqualifiedTextured;
        ++staleUnqualifiedTextured.draw.recordGeneration;
        const CompactRasterDrawClassificationResult
            staleUnqualifiedTexturedResult =
                ClassifyCompactRasterDraw(
                    staleUnqualifiedTextured.draw);
        Check(!staleUnqualifiedTexturedResult.Succeeded() &&
                staleUnqualifiedTexturedResult.reason ==
                    FallbackReason::GenerationMismatch,
            "texture qualification policy cannot hide a stale generation before local fallback");

        ValidFixture corruptUnqualifiedTextured = unqualifiedTextured;
        ++corruptUnqualifiedTextured.draw.recordPositionOffset;
        const CompactRasterDrawClassificationResult
            corruptUnqualifiedTexturedResult =
                ClassifyCompactRasterDraw(
                    corruptUnqualifiedTextured.draw);
        Check(!corruptUnqualifiedTexturedResult.Succeeded() &&
                corruptUnqualifiedTexturedResult.reason ==
                    FallbackReason::ReconstructionFailure,
            "texture qualification policy cannot hide a corrupt reconstruction record before local fallback");

        ValidFixture materialFallback;
        materialFallback.draw.materialSupported = false;
        materialFallback.draw.geometryInstanceMaterialSupported = false;
        materialFallback.draw.recordPayloadClass =
            CompactGeometryPayloadClass::FullGBufferFallback;
        materialFallback.draw.recordPayloadFallbackReason =
            FallbackReason::UnsupportedMaterial;
        materialFallback.draw.recordGeometryRecordIndex =
            InvalidVisibilityWord;
        const CompactRasterDrawClassificationResult fallback =
            ClassifyCompactRasterDraw(materialFallback.draw);
        Check(fallback.Succeeded() &&
                fallback.UsesFullGBufferFallback() &&
                !fallback.UsesLocalFullGBufferFallback() &&
                fallback.fallbackProvenance ==
                    CompactRasterFallbackProvenance::ImmutableSnapshot &&
                fallback.reason == FallbackReason::UnsupportedMaterial &&
                fallback.draw.geometryInstanceHandle ==
                    materialFallback.draw.mappedGeometryInstanceHandle &&
                fallback.draw.geometryLocalIndex ==
                    materialFallback.draw.geometryLocalIndex,
            "a texture/material payload without reconstruction descriptors retains the exact final SurfaceKey through full-G-buffer fallback");

        ValidFixture secondFallback = materialFallback;
        secondFallback.draw.sourceDrawIndex = 6u;
        const std::array<CompactRasterDrawPreflightDesc, 2u>
            allFallbackDraws = {
                materialFallback.draw,
                secondFallback.draw
            };
        const CompactRasterPreflightResult allFallback =
            PreflightCompactRasterDraws(
                allFallbackDraws.data(),
                allFallbackDraws.size());
        Check(allFallback.Succeeded() &&
                allFallback.reconstructDrawCount == 0u &&
                allFallback.reconstructTriangleCount == 0u &&
                allFallback.fallbackDrawCount == 2u &&
                allFallback.fallbackTriangleCount == 4u,
            "an all-fallback scene remains a valid mixed compact transaction with zero logical reconstruction records");

        ValidFixture invalidFallback = materialFallback;
        invalidFallback.draw.recordPayloadFallbackReason =
            FallbackReason::Count;
        Check(!ClassifyCompactRasterDraw(invalidFallback.draw).Succeeded(),
            "a fallback payload cannot encode an out-of-ABI marker reason");
        invalidFallback = materialFallback;
        invalidFallback.draw.recordGeometryRecordIndex = 0u;
        Check(!ClassifyCompactRasterDraw(invalidFallback.draw).Succeeded(),
            "a fallback identity record cannot alias a dense reconstruction record");

        ValidFixture missingProof;
        missingProof.draw.immutableIndexProofPresent = false;
        const CompactRasterDrawClassificationResult proofFallback =
            ClassifyCompactRasterDraw(missingProof.draw);
        Check(proofFallback.Succeeded() &&
                proofFallback.UsesFullGBufferFallback() &&
                proofFallback.UsesLocalFullGBufferFallback() &&
                proofFallback.reason ==
                    FallbackReason::ReconstructionFailure,
            "missing CPU reconstruction data is a local fallback, not a whole-scene proof-build failure");

        ValidFixture viewFallback;
        viewFallback.draw.triangleConditioningSafe = false;
        const CompactRasterDrawClassificationResult conditioningFallback =
            ClassifyCompactRasterDraw(viewFallback.draw);
        Check(conditioningFallback.Succeeded() &&
                conditioningFallback.UsesFullGBufferFallback() &&
                conditioningFallback.UsesLocalFullGBufferFallback(),
            "tiny, clipped, steep, or otherwise unbounded triangles retain ordinary raster coverage locally");

        ValidFixture coarseShadingRate;
        coarseShadingRate.draw.oneByOneShadingRate = false;
        const CompactRasterDrawClassificationResult vrsFallback =
            ClassifyCompactRasterDraw(coarseShadingRate.draw);
        Check(vrsFallback.Succeeded() &&
                vrsFallback.UsesLocalFullGBufferFallback() &&
                vrsFallback.reason ==
                    FallbackReason::ReconstructionFailure,
            "enabled coarse VRS cannot enter per-pixel compact interpolation and falls back locally");

        ValidFixture localMaterialFallback;
        localMaterialFallback.draw.materialSupported = false;
        const CompactRasterDrawClassificationResult localMaterial =
            ClassifyCompactRasterDraw(localMaterialFallback.draw);
        Check(localMaterial.Succeeded() &&
                localMaterial.UsesLocalFullGBufferFallback() &&
                localMaterial.reason ==
                    FallbackReason::UnsupportedMaterial &&
                localMaterialFallback.draw.recordPayloadClass ==
                    CompactGeometryPayloadClass::Reconstruct &&
                localMaterialFallback.draw.recordPayloadFallbackReason ==
                    FallbackReason::None,
            "a CPU-local material rejection preserves the immutable reconstruct payload and emits local provenance");

        ValidFixture localDeformationFallback;
        localDeformationFallback.draw.deformationSupported = false;
        const CompactRasterDrawClassificationResult localDeformation =
            ClassifyCompactRasterDraw(localDeformationFallback.draw);
        Check(localDeformation.Succeeded() &&
                localDeformation.UsesLocalFullGBufferFallback() &&
                localDeformation.reason ==
                    FallbackReason::UnsupportedDeformation,
            "a CPU-local deformation rejection uses only the explicit local allowlist");

        ValidFixture stale;
        stale.draw.materialSupported = false;
        stale.draw.recordPayloadClass =
            CompactGeometryPayloadClass::FullGBufferFallback;
        stale.draw.recordPayloadFallbackReason =
            FallbackReason::UnsupportedMaterial;
        stale.draw.recordGeometryRecordIndex = InvalidVisibilityWord;
        ++stale.draw.recordGeneration;
        const CompactRasterDrawClassificationResult staleResult =
            ClassifyCompactRasterDraw(stale.draw);
        Check(!staleResult.Succeeded() &&
                staleResult.drawClass == CompactRasterDrawClass::Fatal &&
                staleResult.reason == FallbackReason::GenerationMismatch,
            "generation reuse remains fatal even when the draw would otherwise use full-G-buffer fallback");

        ValidFixture staleLocal;
        staleLocal.draw.immutableIndexProofPresent = false;
        ++staleLocal.draw.recordMaterialRevision;
        const CompactRasterDrawClassificationResult staleLocalResult =
            ClassifyCompactRasterDraw(staleLocal.draw);
        Check(!staleLocalResult.Succeeded() &&
                staleLocalResult.reason ==
                    FallbackReason::MaterialRevisionMismatch,
            "a local proof fallback cannot bypass immutable revision validation");

        ValidFixture corrupted;
        corrupted.draw.viewConditioningSafe = false;
        ++corrupted.draw.recordPositionOffset;
        const CompactRasterDrawClassificationResult corruptedResult =
            ClassifyCompactRasterDraw(corrupted.draw);
        Check(!corruptedResult.Succeeded() &&
                corruptedResult.drawClass == CompactRasterDrawClass::Fatal,
            "a simultaneous transient conditioning hazard cannot hide a corrupt dense reconstruction record");

        ValidFixture nonTriangle;
        nonTriangle.draw.geometryUsesTriangleList = false;
        Check(!ClassifyCompactRasterDraw(nonTriangle.draw).Succeeded(),
            "non-triangle geometry remains fatal instead of entering the fallback partition");

        Check(IsCompactVisibilityClassFormat(nvrhi::Format::R8_UINT) &&
                IsCompactVisibilityClassFormat(nvrhi::Format::R32_UINT) &&
                !IsCompactVisibilityClassFormat(
                    nvrhi::Format::RG32_UINT),
            "the class attachment accepts only the exact R8_UINT encoding or its validated R32_UINT format fallback");
        const uint32_t fallbackMarker =
            AdaptiveMsaaEncodeFallbackVisibilityClass(
                static_cast<uint32_t>(fallback.reason), false);
        Check((fallbackMarker &
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_FALLBACK_BIT) != 0u &&
                (fallbackMarker &
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_REASON_MASK) ==
                    static_cast<uint32_t>(fallback.reason) &&
                fallbackMarker !=
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID,
            "the marker preserves the stable fallback-reason ABI without becoming SurfaceKey identity");
    }

    void TestFallbackVisibilityClassProvenance()
    {
        static_assert(static_cast<uint32_t>(
            FallbackReason::UnsupportedDeformation) ==
                ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_DEFORMATION);
        static_assert(static_cast<uint32_t>(
            FallbackReason::UnsupportedMaterial) ==
                ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_MATERIAL);
        static_assert(static_cast<uint32_t>(
            FallbackReason::ReconstructionFailure) ==
                ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_RECONSTRUCTION_FAILURE);
        static_assert(static_cast<uint32_t>(
            FallbackReason::TextureQualificationMismatch) ==
                ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_TEXTURE_QUALIFICATION_MISMATCH);
        static_assert(static_cast<uint32_t>(
            FallbackReason::TextureQualificationMismatch) ==
                ADAPTIVE_MSAA_VISIBILITY_CLASS_MAX_REASON);

        const uint32_t immutable =
            AdaptiveMsaaEncodeFallbackVisibilityClass(
                static_cast<uint32_t>(FallbackReason::UnsupportedMaterial),
                false);
        const uint32_t local = AdaptiveMsaaEncodeFallbackVisibilityClass(
            static_cast<uint32_t>(FallbackReason::UnsupportedMaterial),
            true);
        const AdaptiveMsaaVisibilityClassDecodeResult immutableDecoded =
            AdaptiveMsaaDecodeVisibilityClass(immutable);
        const AdaptiveMsaaVisibilityClassDecodeResult localDecoded =
            AdaptiveMsaaDecodeVisibilityClass(local);
        const uint32_t localTextureQualification =
            AdaptiveMsaaEncodeFallbackVisibilityClass(
                static_cast<uint32_t>(
                    FallbackReason::TextureQualificationMismatch),
                true);
        const AdaptiveMsaaVisibilityClassDecodeResult
            localTextureQualificationDecoded =
                AdaptiveMsaaDecodeVisibilityClass(
                    localTextureQualification);
        Check(immutableDecoded.valid && immutableDecoded.fallback &&
                !immutableDecoded.localFallback &&
                immutableDecoded.fallbackReason ==
                    static_cast<uint32_t>(
                        FallbackReason::UnsupportedMaterial) &&
                localDecoded.valid && localDecoded.fallback &&
                localDecoded.localFallback &&
                localDecoded.fallbackReason ==
                    immutableDecoded.fallbackReason &&
                localTextureQualificationDecoded.valid &&
                localTextureQualificationDecoded.fallback &&
                localTextureQualificationDecoded.localFallback &&
                localTextureQualificationDecoded.fallbackReason ==
                    static_cast<uint32_t>(
                        FallbackReason::TextureQualificationMismatch) &&
                immutable != local,
            "fallback visibility encode/decode preserves the exact non-key provenance bit and reason");

        Check(ValidateCompactRasterFallbackVisibilityClass(
                    CompactGeometryPayloadClass::FullGBufferFallback,
                    FallbackReason::UnsupportedMaterial,
                    immutable) == FallbackReason::None &&
                ValidateCompactRasterFallbackVisibilityClass(
                    CompactGeometryPayloadClass::Reconstruct,
                    FallbackReason::None,
                    local) == FallbackReason::None &&
                ValidateCompactRasterFallbackVisibilityClass(
                    CompactGeometryPayloadClass::Reconstruct,
                    FallbackReason::None,
                    localTextureQualification) ==
                    FallbackReason::None,
            "immutable and local markers accept only their corresponding snapshot payload contracts");
        Check(ValidateCompactRasterFallbackVisibilityClass(
                    CompactGeometryPayloadClass::Reconstruct,
                    FallbackReason::None,
                    immutable) ==
                    FallbackReason::TransientMappingFailure &&
                ValidateCompactRasterFallbackVisibilityClass(
                    CompactGeometryPayloadClass::FullGBufferFallback,
                    FallbackReason::UnsupportedMaterial,
                    local) ==
                    FallbackReason::TransientMappingFailure,
            "swapped immutable/local provenance fails closed");

        const std::array<uint32_t, 6u> invalidClasses = {
            ADAPTIVE_MSAA_VISIBILITY_CLASS_FALLBACK_BIT,
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_FALLBACK_BIT,
            ADAPTIVE_MSAA_VISIBILITY_CLASS_FALLBACK_BIT |
                ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_FALLBACK_BIT |
                static_cast<uint32_t>(FallbackReason::UnsafeSharing),
            ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID,
            0x100u | immutable,
            ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND
        };
        for (const uint32_t invalidClass : invalidClasses)
        {
            Check(!AdaptiveMsaaDecodeVisibilityClass(invalidClass).valid &&
                    ValidateCompactRasterFallbackVisibilityClass(
                        CompactGeometryPayloadClass::Reconstruct,
                        FallbackReason::None,
                        invalidClass) ==
                        FallbackReason::ReconstructionFailure,
                "invalid fallback reason/provenance combinations fail closed");
        }
        Check(AdaptiveMsaaEncodeFallbackVisibilityClass(
                    static_cast<uint32_t>(FallbackReason::UnsafeSharing),
                    true) == ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID &&
                AdaptiveMsaaEncodeFallbackVisibilityClass(
                    static_cast<uint32_t>(FallbackReason::Count),
                    false) == ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID,
            "the encoder rejects local reasons outside the CPU allowlist and every out-of-ABI reason");
    }

    void TestExactPbrAlphaDecision()
    {
        Check(std::abs(EvaluateCompactRasterOpacity(
                0.8f, true, 0.25f, true, 0.9f) - 0.2f) < 1e-6f,
            "the dedicated opacity texture must override base alpha");
        Check(std::abs(EvaluateCompactRasterOpacity(
                0.8f, false, 0.25f, true, 0.5f) - 0.4f) < 1e-6f,
            "base/diffuse alpha is used only without an opacity texture");
        Check(std::abs(EvaluateCompactRasterOpacity(
                0.8f, false, 0.25f, false, 0.5f) - 0.8f) < 1e-6f,
            "material opacity is retained when no alpha texture is enabled");
        Check(EvaluateCompactRasterOpacity(
                2.f, false, 1.f, false, 1.f) == 1.f &&
                EvaluateCompactRasterOpacity(
                    -1.f, false, 1.f, false, 1.f) == 0.f,
            "opacity saturation matches EvaluateSceneMaterial");

        Check(CompactRasterAlphaTestPasses(false, 0.5f, 0.5f),
            "clip equality passes exactly");
        Check(!CompactRasterAlphaTestPasses(false, 0.499f, 0.5f),
            "a negative opacity-minus-cutoff clips");
        Check(CompactRasterAlphaTestPasses(true, 0.f, 1.f),
            "the opaque material domain bypasses alpha clipping");
    }

    void TestPreparedSubmissionTransactionContract()
    {
        static_assert(std::is_move_constructible_v<
            CompactRasterPreparedSubmission>);
        static_assert(std::is_move_assignable_v<
            CompactRasterPreparedSubmission>);
        static_assert(!std::is_copy_constructible_v<
            CompactRasterPreparedSubmission>);
        static_assert(!std::is_copy_assignable_v<
            CompactRasterPreparedSubmission>);

        using ViewProofAccessor =
            const std::vector<CompactRasterViewProof>&
                (CompactRasterPreparedSubmission::*)() const;
        static_assert(std::is_same_v<
            decltype(&CompactRasterPreparedSubmission::ViewProofs),
            ViewProofAccessor>);
        using ValidationAccessor = CompactRasterPreflightResult
            (CompactRasterPreparedSubmission::*)(
                const AdaptiveMsaaCompactVisibilityPass&) const;
        static_assert(std::is_same_v<
            decltype(
                &CompactRasterPreparedSubmission::ValidateForSubmission),
            ValidationAccessor>);

        using PrepareSignature = CompactRasterPreparedSubmission (*)(
            const CompactRasterPrepareParameters&);
        static_assert(std::is_same_v<
            decltype(&PrepareAdaptiveMsaaCompactVisibilityCompositeView),
            PrepareSignature>);

        using SubmitSignature = CompactRasterSubmissionResult (*)(
            nvrhi::ICommandList*,
            CompactRasterPreparedSubmission&&,
            AdaptiveMsaaCompactVisibilityPass&,
            AdaptiveMsaaCompactVisibilityPass::Context&);
        static_assert(std::is_same_v<
            decltype(&SubmitAdaptiveMsaaCompactVisibilityCompositeView),
            SubmitSignature>);
        static_assert(std::is_same_v<
            decltype(CompactRasterSubmissionResult::
                submittedReconstructDraws),
            uint64_t>);
        static_assert(std::is_same_v<
            decltype(CompactRasterSubmissionResult::
                submittedReconstructTriangles),
            uint64_t>);
        static_assert(std::is_same_v<
            decltype(CompactRasterSubmissionResult::
                submittedFallbackDraws),
            uint64_t>);
        static_assert(std::is_same_v<
            decltype(CompactRasterSubmissionResult::
                submittedFallbackTriangles),
            uint64_t>);

        CompactRasterPrepareParameters invalidParameters;
        CompactRasterPreparedSubmission rejected =
            PrepareAdaptiveMsaaCompactVisibilityCompositeView(
                invalidParameters);
        Check(!rejected.IsSubmittable() &&
                !rejected.Preflight().Succeeded() &&
                rejected.Preflight().reason ==
                    FallbackReason::ReconstructionFailure &&
                rejected.ViewProofs().empty(),
            "invalid preparation must never manufacture a submission token");

        CompactRasterPreparedSubmission moved = std::move(rejected);
        Check(!rejected.IsSubmittable() && !moved.IsSubmittable(),
            "moving a rejected transaction cannot make either object submittable");

        CompactRasterPreparedSubmission defaultConstructed;
        Check(!defaultConstructed.IsSubmittable() &&
                !defaultConstructed.Preflight().Succeeded(),
            "default construction cannot bypass transactional preparation");
    }

    void TestTextureQualificationRequestPreservesUserIntent()
    {
        for (const TextureQualificationReason requestLocalReason : {
                TextureQualificationReason::
                    Requested16Resolved8Unsupported,
                TextureQualificationReason::Requested16Unsupported,
                TextureQualificationReason::Resolved16Unsupported,
                TextureQualificationReason::
                    UnsupportedRequestedSampleCount,
                TextureQualificationReason::
                    UnsupportedResolvedSampleCount,
                TextureQualificationReason::
                    RequestedResolvedSampleCountMismatch,
                TextureQualificationReason::InvalidViewportOriginParity })
        {
            Check(IsCompactTextureQualificationViewLocalFailure(
                    requestLocalReason),
                "every explicitly request-local qualification reason must preserve untextured compact reconstruction");
        }
        for (const TextureQualificationReason fatalReason : {
                TextureQualificationReason::CertificateMissing,
                TextureQualificationReason::IntegrityDigestMismatch,
                TextureQualificationReason::AdapterDriverMismatch,
                TextureQualificationReason::RendererExecutableMismatch,
                TextureQualificationReason::MatrixSchemaMismatch,
                TextureQualificationReason::QualificationMaskMismatch,
                TextureQualificationReason::
                    IncompleteViewportParityQualification,
                TextureQualificationReason::
                    ViewportOriginParityNotQualified })
        {
            Check(!IsCompactTextureQualificationViewLocalFailure(
                    fatalReason),
                "certificate, identity, artifact, matrix, and evidence failures must invalidate the whole compact transaction");
        }

        CompactRasterViewProof proof;
        proof.requestedSampleCount = 16u;
        proof.sampleCount = 16u;
        proof.sampleQuality = 0u;
        proof.viewport.addViewport(nvrhi::Viewport(
            1.f,
            65.f,
            2.f,
            66.f,
            0.f,
            1.f));

        TextureQualificationValidationRequest request;
#if UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC
        Check(BuildCompactRasterTextureQualificationRequest(
                proof,
                request) == FallbackReason::TextureQualificationMismatch &&
                request.requestedSampleCount == 0u &&
                request.resolvedSampleCount == 0u,
            "the 4x proof keeps native 16x textured draws on their local full G-buffer payload");
#else
        Check(BuildCompactRasterTextureQualificationRequest(
                proof,
                request) == FallbackReason::None &&
                request.requestedSampleCount == 16u &&
                request.resolvedSampleCount == 16u &&
                request.viewportOriginXParity == 1u &&
                request.viewportOriginYParity == 0u,
            "compact raster qualification preserves a native requested and resolved 16x mode");
#endif

        proof.sampleCount = 8u;
#if UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC
        Check(BuildCompactRasterTextureQualificationRequest(
                proof,
                request) == FallbackReason::TextureQualificationMismatch,
            "the 4x proof keeps requested 16x/resolved 8x textured draws local");
#else
        Check(BuildCompactRasterTextureQualificationRequest(
                proof,
                request) == FallbackReason::None &&
                request.requestedSampleCount == 16u &&
                request.resolvedSampleCount == 8u &&
                request.viewportOriginXParity == 1u &&
                request.viewportOriginYParity == 0u,
            "compact raster qualification must preserve a requested 16x mode separately from its hardware-resolved 8x resources");
#endif

#if UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC
        proof.requestedSampleCount = 4u;
        proof.sampleCount = 4u;
        Check(BuildCompactRasterTextureQualificationRequest(
                proof,
                request) == FallbackReason::None &&
                request.requestedSampleCount == 4u &&
                request.resolvedSampleCount == 4u,
            "the default-off proof opens only exact requested and resolved 4x texture qualification");
#endif

        proof.sampleQuality = 1u;
        request.requestedSampleCount = 4u;
        Check(BuildCompactRasterTextureQualificationRequest(
                proof,
                request) ==
                    FallbackReason::TextureQualificationMismatch &&
                request.requestedSampleCount == 0u &&
                request.resolvedSampleCount == 0u,
            "the Q00 certificate must reject every nonzero MSAA quality and clear the output request");

        proof.sampleQuality = 0u;
        proof.viewport.viewports[0].minX = 1.5f;
        Check(BuildCompactRasterTextureQualificationRequest(
                proof,
                request) ==
                    FallbackReason::TextureQualificationMismatch,
            "fractional viewport origins cannot inherit integer-parity qualification evidence");

        proof.viewport.viewports[0].minX = 1.f;
        proof.requestedSampleCount = 0u;
        Check(BuildCompactRasterTextureQualificationRequest(
                proof,
                request) ==
                    FallbackReason::TextureQualificationMismatch,
            "a missing user-requested sample count must fail closed instead of copying the resolved resource count");
    }
}

int main()
{
    TestValidDrawAndExactKeyMapping();
    TestStableFailClosedReasons();
    TestExactPreparedBindingProof();
    TestPrimitiveCoverageMetadata();
    TestTriangleClipSnapAndOrientationProof();
    TestClipBoundsConditioning();
    TestExactTransformTransactionIdentity();
    TestMixedDrawClassification();
    TestFallbackVisibilityClassProvenance();
    TestWholeListTransaction();
    TestExactPbrAlphaDecision();
    TestPreparedSubmissionTransactionContract();
    TestTextureQualificationRequestPreservesUserIntent();

    if (g_Passed)
    {
        std::cout <<
            "UVSR adaptive MSAA compact raster validation passed\n";
    }
    return g_Passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
