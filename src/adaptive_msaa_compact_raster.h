/*
* Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

// UVSR modification and exact provenance:
// - The draw/pass interface is a UVSR-owned sibling adaptation of
//   donut/include/donut/render/GBufferFillPass.h and GeometryPasses.h at
//   pinned Donut commit bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 (MIT).
// - The visibility-only target topology and separate opaque/alpha-tested
//   raster paths are adapted from The Forge Visibility_Buffer sample at
//   commit a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0), especially
//   VisibilityBufferPass.{vert,frag}.fsl and
//   VisibilityBufferPassAlpha.{vert,frag}.fsl.
// - UVSR replaces The Forge's transient draw-local key with the frozen exact
//   uint2(geometryInstanceHandle, geometryLocalPrimitiveIndex) SurfaceKey ABI.
// Complete third-party notices are packaged under licenses/.

#pragma once

#include "adaptive_msaa_gbuffer_pass.h"
#include "adaptive_msaa_raster_cb.h"
#include "adaptive_msaa_scene.h"

static_assert(
    uvsr::adaptive_msaa::ClearedVisibilityWord ==
        ADAPTIVE_MSAA_CLEARED_VISIBILITY_WORD,
    "CPU and raster-shader clear encodings must remain identical");

#include <donut/render/GBufferFillPass.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace donut::engine
{
    struct BufferGroup;
    struct MeshGeometry;
    struct MeshInfo;
    class CommonRenderPasses;
    class FramebufferFactory;
    class IView;
    class SceneGraph;
    class SceneGraphNode;
    class ShaderFactory;
}

namespace donut::render
{
    class InstancedOpaqueDrawStrategy;
}

namespace uvsr::adaptive_msaa
{
    struct CompactClipBoundsConditioningInput;
    struct CompactTriangleConditioning;
    enum class TextureQualificationReason : uint32_t;
    struct TextureQualificationValidationRequest;

    // Only request-local sample-count and viewport-parity failures may keep
    // the immutable compact scene alive and demote texture-bearing draws.
    // Certificate, live-identity, artifact, schema, matrix, and record
    // failures are transaction-fatal even when the retained token/revision
    // pair still matches.
    [[nodiscard]] bool IsCompactTextureQualificationViewLocalFailure(
        TextureQualificationReason reason);

    struct CompactRasterCpuIndexProof
    {
        uint64_t firstIndex = 0u;
        uint64_t indexCount = 0u;
        uint64_t firstVertex = 0u;
        uint64_t vertexCount = 0u;
        // Monotonic, process-unique identity of the exact loader-time index,
        // position, texcoord, packed normal/tangent, range, and topology
        // capture. This is deliberately not a content hash: equality cannot
        // silently accept a collision.
        uint64_t contentRevision = 0u;
        uint32_t maximumIndex = InvalidVisibilityWord;
        std::array<float, 3> positionBoundsMin{};
        std::array<float, 3> positionBoundsMax{};
        bool positionBoundsValid = false;
    };

    struct CompactRasterCpuIndexProofBuildResult;

    class CompactRasterCpuIndexProofs final
    {
    public:
        ~CompactRasterCpuIndexProofs();

        CompactRasterCpuIndexProofs(
            const CompactRasterCpuIndexProofs&) = delete;
        CompactRasterCpuIndexProofs& operator=(
            const CompactRasterCpuIndexProofs&) = delete;

        [[nodiscard]] bool IsForSceneGraph(
            const donut::engine::SceneGraph* sceneGraph) const;
        [[nodiscard]] uint64_t BuildRevision() const;
        [[nodiscard]] size_t GeometryCount() const;
        [[nodiscard]] bool Find(
            const donut::engine::MeshInfo* mesh,
            const donut::engine::MeshGeometry* geometry,
            const donut::engine::BufferGroup* buffers,
            CompactRasterCpuIndexProof& output) const;
        [[nodiscard]] bool EvaluateTriangleConditioning(
            const donut::engine::MeshInfo* mesh,
            const donut::engine::MeshGeometry* geometry,
            const donut::engine::BufferGroup* buffers,
            const CompactClipBoundsConditioningInput& input,
            CompactTriangleConditioning& output) const;

    private:
        CompactRasterCpuIndexProofs();

        struct Impl;
        std::unique_ptr<Impl> m_Impl;

        friend struct CompactRasterCpuIndexProofBuildResult;
        friend CompactRasterCpuIndexProofBuildResult
            BuildCompactRasterCpuIndexProofs(
                const donut::engine::SceneGraph& sceneGraph);
    };

    struct CompactRasterCpuIndexProofBuildResult
    {
        std::shared_ptr<const CompactRasterCpuIndexProofs> proofs;
        FallbackReason reason = FallbackReason::ReconstructionFailure;

        [[nodiscard]] bool Succeeded() const
        {
            return proofs && reason == FallbackReason::None;
        }
    };

    // Builds one constant-size public proof per reconstruction-candidate
    // triangle while Donut's CPU importer arrays still exist. Geometries with
    // unavailable CPU streams are deliberately omitted so exact GPU identity
    // preflight can route them to the full G-buffer. Private exact stream
    // captures remain owned by the proof set so same-pointer mutations are
    // rejected before upload; each captured proof then survives the normal
    // upload that frees the importer arrays.
    [[nodiscard]] CompactRasterCpuIndexProofBuildResult
        BuildCompactRasterCpuIndexProofs(
            const donut::engine::SceneGraph& sceneGraph);

    struct CompactRasterByteRange
    {
        uint64_t byteOffset = 0u;
        uint64_t byteSize = 0u;
        uint32_t elementStride = 0u;
    };

    struct CompactClipBoundsConditioning
    {
        bool safe = false;
        // The same finite object transform is also applied to packed SNORM
        // normals and tangents. This proves every component in [-1, 1] has a
        // finite binary32 result under Direct3D's permitted dot expansion.
        bool directionTransformFinite = false;
        float minimumClipW = 0.f;
        float maximumClipW = 0.f;
        float maximumAbsoluteNdc = 0.f;
        float maximumAbsoluteWindow = 0.f;
        float maximumForwardError = 0.f;
    };

    // Exact binary32 operands used by AdaptiveMsaaTransformPosition and the
    // subsequent clip-to-window expression. objectToWorld is the three-row
    // float3x4 produced by affineToColumnMajor; worldToClip is indexed as
    // mul(float4(worldPosition, 1), worldToClip).
    struct CompactClipBoundsConditioningInput
    {
        std::array<float, 3> positionBoundsMin{};
        std::array<float, 3> positionBoundsMax{};
        std::array<std::array<float, 4>, 3> objectToWorld{};
        std::array<std::array<float, 4>, 4> worldToClip{};
        std::array<float, 2> clipToWindowScale{};
        std::array<float, 2> clipToWindowBias{};
        // UVSR's compact path currently proves only the ordinary Direct3D
        // viewport depth mapping. Non-default depth ranges fail closed.
        float viewportMinimumDepth = 0.f;
        float viewportMaximumDepth = 1.f;
    };

    struct CompactTriangleConditioningInput
    {
        std::array<std::array<float, 3>, 3> objectPositions{};
        std::array<std::array<float, 2>, 3> objectTexCoords{};
        std::array<std::array<float, 4>, 3> objectToWorld{};
        std::array<std::array<float, 4>, 4> worldToClip{};
        std::array<float, 2> clipToWindowScale{};
        std::array<float, 2> clipToWindowBias{};
        float viewportMinimumDepth = 0.f;
        float viewportMaximumDepth = 1.f;
    };

    struct CompactTriangleConditioning
    {
        bool safe = false;
        uint64_t triangleCount = 0u;
        float minimumAbsoluteProjectedArea = 0.f;
        uint64_t minimumAbsoluteSnappedArea = 0u;
        float minimumSnapTieDistance = 0.f;
        // Conservative bounds on values a D32_FLOAT (including the float
        // depth plane of D32_FLOAT_S8X24) Texture2DMS.Load may return for this
        // primitive after fixed-function interpolation, viewport clamping,
        // and storage conversion.
        float minimumStoredDeviceDepth = 0.f;
        float maximumStoredDeviceDepth = 0.f;
        float storedDeviceDepthErrorMargin = 0.f;
        // Conservative maxima/minimum for every covered-sample and adjacent
        // pixel-center operand used by CompactInterpolateTriangle and
        // CompactGetAnalyticCoarseGradients. These make the shader's finite/range
        // mirrors unreachable for an unchanged prepared transaction.
        float maximumAbsoluteAffineBarycentric = 0.f;
        float minimumPerspectiveDenominator = 0.f;
        float maximumAbsolutePerspectiveBarycentric = 0.f;
        float maximumAbsoluteTexCoord = 0.f;
        float maximumAbsoluteTexCoordGradient = 0.f;
    };

    // Proves the complete two-stage precise shader transform, strict
    // perspective divide, and viewport expression with conservative D3D12
    // binary32 forward-error intervals. Canonical X/Y/Z clip-plane crossings
    // remain legal; eye-plane, overflow, FTZ, and compact raster-domain
    // hazards fail closed.
    [[nodiscard]] CompactClipBoundsConditioning
        EvaluateCompactClipBoundsConditioning(
            const CompactClipBoundsConditioningInput& input);

    // Bitwise transaction identity for the exact raster proof operands. This
    // intentionally distinguishes signed zero and every NaN payload; Submit
    // accepts only the exact matrices and viewport conversion proven before
    // command recording.
    [[nodiscard]] bool SameCompactRasterTransformIdentity(
        const CompactClipBoundsConditioningInput& prepared,
        const CompactClipBoundsConditioningInput& current);

    // Proves that one unclipped triangle has a stable Direct3D 16.8 snap,
    // nonzero post-snap orientation, and a conservatively bounded continuous
    // interpolation denominator. A clip-plane touch/crossing is rejected;
    // generated hardware clip vertices are not reconstructed by this path.
    [[nodiscard]] CompactTriangleConditioning
        EvaluateCompactTriangleConditioning(
            const CompactTriangleConditioningInput& input);

    // Device-independent description of one exact visible DrawItem and the
    // immutable compact-scene records that must resolve it. All source values
    // remain wide or signed until validation proves lossless GPU narrowing.
    struct CompactRasterDrawPreflightDesc
    {
        uint32_t sourceViewIndex = InvalidVisibilityWord;
        uint32_t sourceDrawIndex = InvalidVisibilityWord;

        bool geometryPresent = false;
        bool instanceMeshMatches = false;
        bool bufferGroupMatchesMesh = false;
        bool meshUsesTriangleList = false;
        bool geometryUsesTriangleList = false;
        bool deformationSupported = false;
        bool materialSupported = false;
        bool buffersPresent = false;
        bool instanceBufferValid = false;
        bool indexBufferValid = false;
        bool vertexBufferValid = false;
        bool fullGBufferRasterSupported = false;
        bool alphaClassificationMatches = false;
        bool cullModeMatches = false;
        bool oneByOneShadingRate = false;
        bool viewConditioningProofPresent = false;
        bool viewConditioningSafe = false;
        bool directionTransformFinite = false;
        bool triangleConditioningProofPresent = false;
        bool triangleConditioningSafe = false;
        float minimumClipW = 0.f;
        float maximumAbsoluteNdc = 0.f;
        float minimumAbsoluteProjectedArea = 0.f;
        uint64_t minimumAbsoluteSnappedArea = 0u;
        float minimumSnapTieDistance = 0.f;
        float minimumStoredDeviceDepth = 0.f;
        float maximumStoredDeviceDepth = 0.f;
        float storedDeviceDepthErrorMargin = 0.f;
        float maximumAbsoluteAffineBarycentric = 0.f;
        float minimumPerspectiveDenominator = 0.f;
        float maximumAbsolutePerspectiveBarycentric = 0.f;
        float maximumAbsoluteTexCoord = 0.f;
        float maximumAbsoluteTexCoordGradient = 0.f;

        int64_t instanceIndex = -1;
        int64_t firstGeometryInstanceIndex = -1;
        uint64_t instanceCount = 0u;
        uint64_t instanceGeometryCount = 0u;
        uint64_t geometryLocalIndex = 0u;
        uint64_t handleMapCount = 0u;
        uint32_t mappedGeometryInstanceHandle = InvalidVisibilityWord;

        uint64_t instanceBufferByteSize = 0u;
        uint32_t instanceBufferStride = 0u;
        uint64_t indexBufferByteSize = 0u;
        uint64_t vertexBufferByteSize = 0u;
        uint64_t meshFirstIndex = 0u;
        uint64_t geometryFirstIndex = 0u;
        uint64_t indexCount = 0u;
        uint64_t meshFirstVertex = 0u;
        uint64_t geometryFirstVertex = 0u;
        uint64_t vertexCount = 0u;
        CompactRasterByteRange positionRange;
        CompactRasterByteRange previousPositionRange;
        CompactRasterByteRange texCoordRange;
        CompactRasterByteRange normalRange;
        CompactRasterByteRange tangentRange;

        bool immutableIndexProofPresent = false;
        uint64_t proofFirstIndex = 0u;
        uint64_t proofIndexCount = 0u;
        uint64_t proofFirstVertex = 0u;
        uint64_t proofVertexCount = 0u;
        uint64_t proofContentRevision = 0u;
        uint32_t proofMaximumIndex = InvalidVisibilityWord;

        uint64_t geometryInstanceRecordCount = 0u;
        bool geometryInstanceRecordValid = false;
        bool geometryInstanceDeformationSupported = false;
        bool geometryInstanceMaterialSupported = false;
        uint32_t recordGeneration = 0u;
        uint32_t recordInstanceIndex = InvalidVisibilityWord;
        uint32_t recordGeometryIndex = InvalidVisibilityWord;
        uint32_t recordGeometryRecordIndex = InvalidVisibilityWord;
        uint32_t recordPrimitiveCount = 0u;
        uint64_t recordStableInstanceHandle = 0u;
        CompactGeometryPayloadClass recordPayloadClass =
            CompactGeometryPayloadClass::Invalid;
        FallbackReason recordPayloadFallbackReason =
            FallbackReason::ReconstructionFailure;

        uint64_t expectedTopologyEpoch = 0u;
        uint64_t recordTopologyEpoch = 0u;
        uint32_t expectedTopologyRevision = 0u;
        uint32_t recordTopologyRevision = 0u;
        uint32_t expectedLodRevision = 0u;
        uint32_t recordLodRevision = 0u;
        uint32_t expectedDeformationRevision = 0u;
        uint32_t recordDeformationRevision = 0u;
        uint32_t expectedMaterialRevision = 0u;
        uint32_t recordMaterialRevision = 0u;

        uint64_t geometryRecordCount = 0u;
        uint64_t materialRecordCount = 0u;
        uint64_t textureDescriptorCount = 0u;
        uint64_t rawBufferDescriptorCount = 0u;
        uint64_t descriptorCount = 0u;
        uint32_t recordIndexBufferDescriptor = InvalidVisibilityWord;
        uint32_t recordVertexBufferDescriptor = InvalidVisibilityWord;
        uint32_t recordIndexOffset = 0u;
        uint32_t recordPositionOffset = 0u;
        uint32_t recordPreviousPositionOffset = 0u;
        uint32_t recordTexCoordOffset = 0u;
        uint32_t recordNormalOffset = 0u;
        uint32_t recordTangentOffset = 0u;
        uint32_t recordIndexCount = 0u;
        uint32_t recordVertexCount = 0u;
        uint32_t recordMaterialIndex = InvalidVisibilityWord;
        // A view-local texture qualification failure does not invalidate the
        // immutable surface identity. It converts only draws whose material
        // record actually samples textures to the exact local full-G-buffer
        // payload; untextured records remain reconstructable.
        uint32_t recordMaterialTextureMask = 0u;
        bool textureReconstructionQualified = true;
        uint32_t expectedGeometryFlags = CompactGeometryFlag_None;
        uint32_t recordGeometryFlags = CompactGeometryFlag_None;
    };

    struct CompactRasterValidatedDraw
    {
        uint32_t instanceIndex = 0u;
        uint32_t geometryLocalIndex = 0u;
        uint32_t startIndexLocation = 0u;
        uint32_t startVertexLocation = 0u;
        uint32_t indexCount = 0u;
        uint32_t primitiveCount = 0u;
        uint32_t geometryInstanceHandle = InvalidVisibilityWord;
        uint32_t geometryRecordIndex = InvalidVisibilityWord;
    };

    struct CompactRasterDrawValidationResult
    {
        CompactRasterValidatedDraw draw;
        FallbackReason reason = FallbackReason::ReconstructionFailure;

        [[nodiscard]] bool Succeeded() const
        {
            return reason == FallbackReason::None;
        }
    };

    enum class CompactRasterDrawClass : uint32_t
    {
        Reconstruct = 0u,
        FullGBufferFallback = 1u,
        Fatal = 2u
    };

    enum class CompactRasterFallbackProvenance : uint32_t
    {
        None = 0u,
        ImmutableSnapshot = 1u,
        LocalPerView = 2u
    };

    struct CompactRasterDrawClassificationResult
    {
        CompactRasterValidatedDraw draw;
        CompactRasterDrawClass drawClass = CompactRasterDrawClass::Fatal;
        CompactRasterFallbackProvenance fallbackProvenance =
            CompactRasterFallbackProvenance::None;
        FallbackReason reason = FallbackReason::ReconstructionFailure;

        [[nodiscard]] bool Succeeded() const
        {
            return drawClass != CompactRasterDrawClass::Fatal;
        }

        [[nodiscard]] bool Reconstructs() const
        {
            return drawClass == CompactRasterDrawClass::Reconstruct;
        }

        [[nodiscard]] bool UsesFullGBufferFallback() const
        {
            return drawClass == CompactRasterDrawClass::FullGBufferFallback;
        }

        [[nodiscard]] bool UsesLocalFullGBufferFallback() const
        {
            return UsesFullGBufferFallback() &&
                fallbackProvenance ==
                    CompactRasterFallbackProvenance::LocalPerView;
        }
    };

    struct CompactRasterPreflightResult
    {
        bool eligible = false;
        FallbackReason reason = FallbackReason::ReconstructionFailure;
        uint32_t failingView = InvalidVisibilityWord;
        uint32_t failingDraw = InvalidVisibilityWord;
        uint64_t drawCount = 0u;
        uint64_t triangleCount = 0u;
        uint64_t reconstructDrawCount = 0u;
        uint64_t reconstructTriangleCount = 0u;
        uint64_t fallbackDrawCount = 0u;
        uint64_t fallbackTriangleCount = 0u;
        bool transientViewConditioningFailure = false;
        // A valid fallback-class draw was found, but the caller supplied only
        // compact key/class/depth targets. This is a bounded topology-upgrade
        // request, not a fatal scene failure and must not enter the revision
        // fallback latch.
        bool mixedTopologyRequired = false;

        [[nodiscard]] bool Succeeded() const
        {
            return eligible && reason == FallbackReason::None;
        }
    };

    [[nodiscard]] CompactRasterDrawValidationResult
        ValidateCompactRasterDraw(
            const CompactRasterDrawPreflightDesc& description);

    // Classifies one draw against the final compact-scene identity record.
    // Reconstruction requires the complete immutable CPU and descriptor
    // proof. The ordinary full-G-buffer fallback requires only exact raster
    // ownership, source ranges, handle generation/epoch, and draw support;
    // mapping, capacity, non-triangle, and revision failures remain fatal.
    [[nodiscard]] CompactRasterDrawClassificationResult
        ClassifyCompactRasterDraw(
            const CompactRasterDrawPreflightDesc& description);

    [[nodiscard]] bool IsCompactVisibilityClassFormat(
        nvrhi::Format format);

    // CPU mirror of the shader's payload/provenance gate. Immutable markers
    // must match a scene-published fallback record and stored reason exactly;
    // local markers must name one of the four CPU-proven reasons while the
    // immutable record remains reconstructable with no stored fallback.
    [[nodiscard]] FallbackReason
        ValidateCompactRasterFallbackVisibilityClass(
            CompactGeometryPayloadClass payloadClass,
            FallbackReason payloadFallbackReason,
            uint32_t visibilityClass);

    struct CompactRasterPreparedBindingDesc
    {
        uint32_t indexBindingByteOffset = 0u;
        bool indexBindingIsR32Uint = false;
        uint32_t drawStartIndexLocation = 0u;
        uint32_t drawStartVertexLocation = 0u;
        uint32_t drawStartInstanceLocation = 0u;
        uint32_t pushStartInstanceLocation = 0u;
        uint32_t pushStartVertexLocation = 0u;
        uint32_t pushPositionOffset = 0u;
        uint32_t pushPreviousPositionOffset = 0u;
        uint32_t pushTexCoordOffset = 0u;
        uint32_t pushNormalOffset = 0u;
        uint32_t pushTangentOffset = 0u;
        uint32_t pushVisibilityClass =
            ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND;
    };

    // Verifies the exact relation between the proven source ranges, raw-
    // buffer push constants, and the IA index base. Compact raster always
    // uses IA base vertex zero; its logical firstVertex lives only in the
    // raw-load push constant and must remain identical to reconstruction.
    [[nodiscard]] FallbackReason ValidateCompactRasterPreparedBinding(
        const CompactRasterDrawPreflightDesc& description,
        const CompactRasterValidatedDraw& validatedDraw,
        const CompactRasterPreparedBindingDesc& binding);

    struct CompactRasterCoverageMetadata
    {
        bool valid = false;
        // InvalidVisibilityWord means Reference centroid interpolation uses
        // pixel center because the original primitive covers every sample.
        uint32_t centroidSample = InvalidVisibilityWord;
    };

    // CPU mirror of the production HLSL metadata rule. finalSurfaceMask comes
    // only from final depth-tested SurfaceKey ownership; primitiveCoverageMask
    // is never identity and may only prove that the final mask is a nonzero
    // subset. Reference centroid selection always uses original primitive
    // coverage, before depth/stencil survivors are removed.
    [[nodiscard]] CompactRasterCoverageMetadata
        EvaluateCompactRasterCoverageMetadata(
            uint32_t sampleCount,
            uint32_t primitiveCoverageMask,
            uint32_t finalSurfaceMask);

    // Visits the supplied exact visible sequence once, partitions every valid
    // draw without reordering, and returns the first fatal identity failure.
    // No prefix is submitted when any fatal entry exists.
    [[nodiscard]] CompactRasterPreflightResult PreflightCompactRasterDraws(
        const CompactRasterDrawPreflightDesc* descriptions,
        size_t descriptionCount);

    // Converts a successful mixed draw classification into a non-latching
    // CompactOnly -> CompactMixed topology request when fallback attachments
    // have not yet been allocated.
    [[nodiscard]] CompactRasterPreflightResult
        ValidateCompactRasterTopologyAvailability(
            const CompactRasterPreflightResult& preflight,
            bool mixedTopologyAvailable);

    // CPU mirrors of the current PBR alpha decision, used by focused ABI tests.
    // The opacity texture has precedence over base/diffuse alpha, exactly as
    // EvaluateSceneMaterial in the pinned Donut shader.
    [[nodiscard]] float EvaluateCompactRasterOpacity(
        float materialOpacity,
        bool useOpacityTexture,
        float opacityTextureValue,
        bool useBaseOrDiffuseTexture,
        float baseOrDiffuseAlpha);

    [[nodiscard]] bool CompactRasterAlphaTestPasses(
        bool materialDomainIsOpaque,
        float saturatedOpacity,
        float alphaCutoff);

    class AdaptiveMsaaCompactVisibilityPass final
        : public donut::render::GBufferFillPass
    {
    public:
        static constexpr uint32_t InvalidGeometryLocalIndex =
            ADAPTIVE_MSAA_INVALID_GEOMETRY_LOCAL_INDEX;

        class Context final : public donut::render::GBufferFillPass::Context
        {
        public:
            uint32_t geometryLocalIndex = InvalidGeometryLocalIndex;
        };

        AdaptiveMsaaCompactVisibilityPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<donut::engine::CommonRenderPasses>&
                commonPasses,
            nvrhi::IBuffer* geometryInstanceHandleMap);

        void Init(
            donut::engine::ShaderFactory& shaderFactory,
            const CreateParameters& parameters) override;

        void SetGeometryInstanceHandleMap(
            nvrhi::IBuffer* geometryInstanceHandleMap);
        [[nodiscard]] nvrhi::IBuffer* GetGeometryInstanceHandleMap() const;
        [[nodiscard]] bool HasValidGeometryInstanceHandleMap() const;
        [[nodiscard]] uint64_t PreparationEpoch() const;

        bool SetupMaterial(
            donut::render::GeometryPassContext& context,
            const donut::engine::Material* material,
            nvrhi::RasterCullMode cullMode,
            nvrhi::GraphicsState& state) override;

        void SetGeometryLocalIndex(
            Context& context,
            uint32_t geometryLocalIndex) const;

        void SetPushConstants(
            donut::render::GeometryPassContext& context,
            nvrhi::ICommandList* commandList,
            nvrhi::GraphicsState& state,
            nvrhi::DrawArguments& arguments) override;

    protected:
        nvrhi::ShaderHandle CreateVertexShader(
            donut::engine::ShaderFactory& shaderFactory,
            const CreateParameters& parameters) override;

        nvrhi::ShaderHandle CreatePixelShader(
            donut::engine::ShaderFactory& shaderFactory,
            const CreateParameters& parameters,
            bool alphaTested) override;

        nvrhi::BindingLayoutHandle CreateInputBindingLayout() override;
        nvrhi::BindingSetHandle CreateInputBindingSet(
            const donut::engine::BufferGroup* bufferGroup) override;
        nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(
            PipelineKey key,
            const nvrhi::FramebufferInfo& framebufferInfo) override;

    private:
        void AdvancePreparationEpoch();

        nvrhi::BufferHandle m_GeometryInstanceHandleMap;
        nvrhi::ShaderHandle m_AlphaTestedVertexShader;
        uint64_t m_PreparationEpoch = 1u;
        bool m_Initialized = false;
    };

    struct CompactRasterSubmissionResult
    {
        CompactRasterPreflightResult preflight;
        uint64_t submittedTriangles = 0u;
        uint64_t submittedReconstructDraws = 0u;
        uint64_t submittedReconstructTriangles = 0u;
        uint64_t submittedFallbackDraws = 0u;
        uint64_t submittedFallbackTriangles = 0u;
    };

    struct CompactRasterViewTransformIdentity
    {
        std::array<float, 16> worldToClip{};
        std::array<float, 2> clipToWindowScale{};
        std::array<float, 2> clipToWindowBias{};
    };

    [[nodiscard]] CompactRasterViewTransformIdentity
        CaptureCompactRasterViewTransformIdentity(
            const donut::engine::IView& view);
    [[nodiscard]] bool SameCompactRasterViewTransformIdentity(
        const CompactRasterViewTransformIdentity& prepared,
        const CompactRasterViewTransformIdentity& current);
    [[nodiscard]] bool SameCompactRasterViewportState(
        const nvrhi::ViewportState& prepared,
        const nvrhi::ViewportState& current);

    struct CompactRasterAcceptedDepthInterval
    {
        bool valid = false;
        bool hasGeometry = false;
        // Views with no Reconstruct draws use the exact
        // { valid=true, hasGeometry=false, 0,0,0 } convention, even when
        // class-marked full-G-buffer fallback draws are present.
        // Reconstruction-bearing intervals exclude zero because it is the
        // compact reverse-Z clear/background value for that class.
        float minimumStoredDeviceDepth = 0.f;
        float maximumStoredDeviceDepth = 0.f;
        float conservativeErrorMargin = 0.f;
    };

    // Immutable public proof consumed by compact shade preparation before the
    // move-only raster token is submitted. It keys the interval to the exact
    // child view, exact RG32_UINT key, R8/R32_UINT class, D32/D32S8 depth,
    // optional full-G-buffer fallback attachment subresources, sample layout,
    // viewport, and bitwise transform identity already validated by the raster
    // transaction. The renderer latches the same proof immediately before
    // opening its command list; Submit accepts only that latched single-use
    // transaction and performs no live rediscovery.
    struct CompactRasterViewProof
    {
        uint32_t sourceViewIndex = InvalidVisibilityWord;
        const donut::engine::IView* view = nullptr;
        nvrhi::TextureHandle surfaceKeyTarget;
        nvrhi::TextureHandle visibilityClassTarget;
        nvrhi::TextureHandle depthTarget;
        nvrhi::TextureSubresourceSet surfaceKeySubresources;
        nvrhi::TextureSubresourceSet visibilityClassSubresources;
        nvrhi::TextureSubresourceSet depthSubresources;
        nvrhi::Format surfaceKeyFormat = nvrhi::Format::UNKNOWN;
        nvrhi::Format visibilityClassFormat = nvrhi::Format::UNKNOWN;
        nvrhi::Format depthFormat = nvrhi::Format::UNKNOWN;
        // Preserve the sanitized user request independently from the
        // hardware-resolved resource count. A qualified 16x request may not
        // inherit an 8x certificate after render-target demotion.
        uint32_t requestedSampleCount = 0u;
        uint32_t sampleCount = 0u;
        uint32_t sampleQuality = 0u;
        nvrhi::ViewportState viewport;
        CompactRasterViewTransformIdentity transformIdentity;
        // This interval applies only to Reconstruct samples. Full-G-buffer
        // fallback samples carry an explicit class and exact key, so a valid
        // far-plane depth of zero cannot alias cleared background.
        CompactRasterAcceptedDepthInterval acceptedDepth;
        bool fullGBufferFallbackPresent = false;
        bool fallbackMayStoreZeroDepth = false;
        std::array<nvrhi::TextureHandle, 5u> fallbackGBufferTargets;
        std::array<nvrhi::TextureSubresourceSet, 5u>
            fallbackGBufferSubresources;
        nvrhi::TextureHandle fallbackMotionTarget;
        nvrhi::TextureSubresourceSet fallbackMotionSubresources;
    };

    struct CompactRasterPrepareParameters
    {
        const donut::engine::ICompositeView* compositeView = nullptr;
        const donut::engine::ICompositeView* compositeViewPrev = nullptr;
        donut::engine::FramebufferFactory* framebufferFactory = nullptr;
        nvrhi::ITexture* expectedSurfaceKeyTarget = nullptr;
        nvrhi::ITexture* expectedVisibilityClassTarget = nullptr;
        nvrhi::ITexture* expectedDepthTarget = nullptr;
        // Both mixed objects are null for compact-only topology and non-null
        // together after the bounded CompactOnly -> CompactMixed upgrade.
        donut::engine::FramebufferFactory*
            mixedFallbackFramebufferFactory = nullptr;
        uvsr::AdaptiveMsaaGBufferFillPass* mixedFallbackPass = nullptr;
        std::array<nvrhi::ITexture*, 5u>
            expectedFallbackGBufferTargets{};
        nvrhi::ITexture* expectedFallbackMotionTarget = nullptr;
        std::shared_ptr<const donut::engine::SceneGraph> sceneGraph;
        std::shared_ptr<donut::engine::SceneGraphNode> rootNode;
        donut::render::InstancedOpaqueDrawStrategy* drawStrategy = nullptr;
        AdaptiveMsaaCompactVisibilityPass* pass = nullptr;
        std::shared_ptr<const CompactSceneSnapshot> compactScene;
        // Required only when compactScene retained qualified texture
        // descriptors. The authority outlives this immediate prepare/submit
        // transaction and is rechecked for every view during both phases.
        const TextureQualificationCertificateAuthority*
            textureQualificationAuthority = nullptr;
        uint32_t requestedSampleCount = 0u;
        std::shared_ptr<const CompactRasterCpuIndexProofs> cpuIndexProofs;
        const char* passEvent = nullptr;
        bool materialEvents = false;
    };

    [[nodiscard]] FallbackReason
        BuildCompactRasterTextureQualificationRequest(
            const CompactRasterViewProof& proof,
            TextureQualificationValidationRequest& request);

    // Opaque single-use transaction. It is submittable only after a complete
    // successful Prepare call. Copying is prohibited; moving transfers the
    // only submission token and leaves the source non-submittable.
    class CompactRasterPreparedSubmission final
    {
    public:
        CompactRasterPreparedSubmission();
        ~CompactRasterPreparedSubmission();

        CompactRasterPreparedSubmission(
            CompactRasterPreparedSubmission&& other) noexcept;
        CompactRasterPreparedSubmission& operator=(
            CompactRasterPreparedSubmission&& other) noexcept;

        CompactRasterPreparedSubmission(
            const CompactRasterPreparedSubmission&) = delete;
        CompactRasterPreparedSubmission& operator=(
            const CompactRasterPreparedSubmission&) = delete;

        [[nodiscard]] bool IsSubmittable() const;
        [[nodiscard]] const CompactRasterPreflightResult& Preflight() const;
        [[nodiscard]] const std::vector<CompactRasterViewProof>&
            ViewProofs() const;
        // Command-free final transaction check. The renderer calls this
        // before opening a command list so any scene, view, resource, or
        // binding mutation can select Reference in the same invocation.
        // Donut scene/material mutation is render-thread-owned: callers must
        // keep mutation publication closed from this call through Submit.
        // Unsynchronized external Material writes are a C++ data race and are
        // outside the supported transaction contract.
        [[nodiscard]] CompactRasterPreflightResult ValidateForSubmission(
            const AdaptiveMsaaCompactVisibilityPass& pass) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        CompactRasterPreflightResult m_Preflight;

        friend CompactRasterPreparedSubmission
            PrepareAdaptiveMsaaCompactVisibilityCompositeView(
                const CompactRasterPrepareParameters& parameters);
        friend CompactRasterSubmissionResult
            SubmitAdaptiveMsaaCompactVisibilityCompositeView(
                nvrhi::ICommandList* commandList,
                CompactRasterPreparedSubmission&& prepared,
                AdaptiveMsaaCompactVisibilityPass& pass,
                AdaptiveMsaaCompactVisibilityPass::Context& passContext);
    };

    // Captures every child view's exact opaque DrawItem sequence, validates
    // every framebuffer/snapshot/draw, and creates every required binding and
    // PSO. It never accepts or touches a command list. The captured sequence
    // is retained directly, so Submit performs no draw-strategy replay. The
    // composite view and its child views must remain alive and unchanged until
    // the immediate Submit; observable view state is revalidated before Submit
    // records its first command.
    [[nodiscard]] CompactRasterPreparedSubmission
        PrepareAdaptiveMsaaCompactVisibilityCompositeView(
            const CompactRasterPrepareParameters& parameters);

    // Consumes the only successful preparation token before validating the
    // command list/pass. Consequently every call, successful or not, leaves
    // the caller's object non-submittable and no transaction can draw twice.
    // This is the only compact-raster API that records command-list work.
    [[nodiscard]] CompactRasterSubmissionResult
        SubmitAdaptiveMsaaCompactVisibilityCompositeView(
            nvrhi::ICommandList* commandList,
            CompactRasterPreparedSubmission&& prepared,
            AdaptiveMsaaCompactVisibilityPass& pass,
            AdaptiveMsaaCompactVisibilityPass::Context& passContext);
}
