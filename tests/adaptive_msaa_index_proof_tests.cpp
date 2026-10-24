#include "adaptive_msaa_compact_raster.h"

#include <donut/engine/SceneGraph.h>
#include <donut/engine/SceneTypes.h>

#include <cstddef>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    using namespace donut::engine;
    using namespace uvsr::adaptive_msaa;

    bool g_Passed = true;

    void Check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr <<
                "Adaptive MSAA index-proof validation failed: " <<
                message << '\n';
            g_Passed = false;
        }
    }

    struct SceneFixture
    {
        std::shared_ptr<SceneGraph> graph = std::make_shared<SceneGraph>();
        std::shared_ptr<SceneGraphNode> root =
            std::make_shared<SceneGraphNode>();
        std::shared_ptr<Material> material = std::make_shared<Material>();
        std::shared_ptr<BufferGroup> buffers =
            std::make_shared<BufferGroup>();
        std::shared_ptr<MeshInfo> mesh = std::make_shared<MeshInfo>();
        std::shared_ptr<MeshGeometry> firstGeometry =
            std::make_shared<MeshGeometry>();
        std::shared_ptr<MeshGeometry> secondGeometry =
            std::make_shared<MeshGeometry>();
        std::shared_ptr<MeshInstance> instance;

        SceneFixture()
        {
            // The first three values are outside both geometry ranges. Their
            // deliberately invalid values prove capture respects mesh plus
            // geometry offsets instead of scanning unrelated indices.
            buffers->indexData = {
                99u, 99u, 99u,
                0u, 4u, 2u, 2u, 3u, 1u,
                2u, 0u, 1u
            };
            buffers->positionData = {
                // This filler is outside both geometry vertex ranges. It
                // proves the loader honors mesh and geometry vertex origins.
                dm::float3(999.f, 999.f, 999.f),
                dm::float3(-2.f, -1.f, 0.f),
                dm::float3(1.f, -3.f, 2.f),
                dm::float3(0.f, 4.f, -1.f),
                dm::float3(3.f, 2.f, 5.f),
                dm::float3(-4.f, 1.f, 3.f)
            };
            buffers->texcoord1Data = {
                dm::float2(99.f, 99.f),
                dm::float2(0.f, 0.f),
                dm::float2(1.f, 0.f),
                dm::float2(0.f, 1.f),
                dm::float2(1.f, 1.f),
                dm::float2(0.5f, 0.5f)
            };
            buffers->normalData = {
                0u, 0x007f0000u, 0x007f0000u,
                0x007f0000u, 0x007f0000u, 0x007f0000u
            };
            buffers->tangentData = {
                0u, 0x7f00007fu, 0x7f00007fu,
                0x7f00007fu, 0x7f00007fu, 0x7f00007fu
            };

            firstGeometry->material = material;
            firstGeometry->indexOffsetInMesh = 1u;
            firstGeometry->numIndices = 6u;
            firstGeometry->numVertices = 5u;

            secondGeometry->material = material;
            secondGeometry->indexOffsetInMesh = 7u;
            secondGeometry->vertexOffsetInMesh = 1u;
            secondGeometry->numIndices = 3u;
            secondGeometry->numVertices = 3u;

            mesh->type = MeshType::Triangles;
            mesh->buffers = buffers;
            mesh->indexOffset = 2u;
            mesh->vertexOffset = 1u;
            mesh->totalIndices =
                static_cast<uint32_t>(buffers->indexData.size());
            mesh->geometries = { firstGeometry, secondGeometry };

            graph->SetRootNode(root);
            instance = std::make_shared<MeshInstance>(mesh);
            graph->AttachLeafNode(root, instance);
        }
    };

    void TestPublicProofIsConstantSize()
    {
        static_assert(std::is_standard_layout_v<
            CompactRasterCpuIndexProof>);
        static_assert(std::is_trivially_copyable_v<
            CompactRasterCpuIndexProof>);
        static_assert(sizeof(CompactRasterCpuIndexProof) == 72u);

        Check(sizeof(CompactRasterCpuIndexProof) == 72u,
            "one public proof remains fixed-size while carrying an exact monotonic content revision");
    }

    void TestValidCaptureAndPostUploadSurvival()
    {
        SceneFixture fixture;
        const CompactRasterCpuIndexProofBuildResult build =
            BuildCompactRasterCpuIndexProofs(*fixture.graph);
        Check(build.Succeeded(),
            "valid disjoint geometry ranges must produce immutable proofs");
        if (!build.Succeeded())
            return;

        Check(build.proofs->IsForSceneGraph(fixture.graph.get()),
            "the proof set must retain its exact source SceneGraph identity");
        Check(build.proofs->BuildRevision() != 0u,
            "the proof set must publish a nonzero immutable build revision");
        Check(build.proofs->GeometryCount() == 2u,
            "one proof must be captured for each triangle geometry");

        CompactRasterCpuIndexProof first;
        CompactRasterCpuIndexProof second;
        Check(build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                first),
            "the first exact geometry identity must resolve");
        Check(build.proofs->Find(
                fixture.mesh.get(),
                fixture.secondGeometry.get(),
                fixture.buffers.get(),
                second),
            "the second exact geometry identity must resolve");
        Check(first.firstIndex == 3u && first.indexCount == 6u &&
                first.firstVertex == 1u &&
                first.vertexCount == 5u &&
                first.contentRevision != 0u &&
                first.maximumIndex == 4u &&
                first.positionBoundsValid &&
                first.positionBoundsMin ==
                    std::array<float, 3>{ -4.f, -3.f, -1.f } &&
                first.positionBoundsMax ==
                    std::array<float, 3>{ 3.f, 4.f, 5.f },
            "the first geometry must capture its exact range and maximum index");
        Check(second.firstIndex == 9u && second.indexCount == 3u &&
                second.firstVertex == 2u &&
                second.vertexCount == 3u &&
                second.contentRevision != 0u &&
                second.contentRevision != first.contentRevision &&
                second.maximumIndex == 2u &&
                second.positionBoundsValid &&
                second.positionBoundsMin ==
                    std::array<float, 3>{ 0.f, -3.f, -1.f } &&
                second.positionBoundsMax ==
                    std::array<float, 3>{ 3.f, 4.f, 5.f },
            "the second geometry must capture its exact range and maximum index");

        // Match the staged Donut uploader's release operation. No source index
        // value remains available after this point.
        std::vector<uint32_t>().swap(fixture.buffers->indexData);
        std::vector<dm::float3>().swap(fixture.buffers->positionData);
        std::vector<dm::float2>().swap(fixture.buffers->texcoord1Data);
        std::vector<uint32_t>().swap(fixture.buffers->normalData);
        std::vector<uint32_t>().swap(fixture.buffers->tangentData);
        Check(fixture.buffers->indexData.empty(),
            "the fixture must model post-upload CPU index disposal");
        Check(fixture.buffers->positionData.empty(),
            "the fixture must model post-upload CPU position disposal");
        Check(fixture.buffers->texcoord1Data.empty() &&
                fixture.buffers->normalData.empty() &&
                fixture.buffers->tangentData.empty(),
            "the fixture must model post-upload CPU shading-stream disposal");

        CompactRasterCpuIndexProof afterUpload;
        Check(build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                afterUpload) &&
                afterUpload.firstIndex == first.firstIndex &&
                afterUpload.indexCount == first.indexCount &&
                afterUpload.firstVertex == first.firstVertex &&
                afterUpload.vertexCount == first.vertexCount &&
                afterUpload.contentRevision == first.contentRevision &&
                afterUpload.maximumIndex == first.maximumIndex &&
                afterUpload.positionBoundsValid &&
                afterUpload.positionBoundsMin == first.positionBoundsMin &&
                afterUpload.positionBoundsMax == first.positionBoundsMax,
            "the fixed proof must survive after BufferGroup::indexData is released");

        const CompactRasterCpuIndexProofBuildResult lateBuild =
            BuildCompactRasterCpuIndexProofs(*fixture.graph);
        Check(lateBuild.Succeeded() &&
                lateBuild.proofs->GeometryCount() == 0u,
            "proof capture after importer disposal publishes an empty exact set so otherwise valid draws can use local full-G-buffer fallback");
        CompactRasterCpuIndexProof unavailable;
        Check(!lateBuild.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                unavailable),
            "a fallback-only geometry cannot manufacture a reconstruction proof after CPU data is gone");
    }

    void TestExactIdentityAndReplacementRejection()
    {
        SceneFixture fixture;
        const CompactRasterCpuIndexProofBuildResult build =
            BuildCompactRasterCpuIndexProofs(*fixture.graph);
        Check(build.Succeeded(),
            "identity rejection requires a valid baseline proof set");
        if (!build.Succeeded())
            return;

        CompactRasterCpuIndexProof output;
        const std::shared_ptr<MeshInfo> replacementMesh =
            std::make_shared<MeshInfo>();
        const std::shared_ptr<MeshGeometry> replacementGeometry =
            std::make_shared<MeshGeometry>();
        const std::shared_ptr<BufferGroup> replacementBuffers =
            std::make_shared<BufferGroup>();
        const std::shared_ptr<SceneGraph> replacementGraph =
            std::make_shared<SceneGraph>();

        Check(!build.proofs->IsForSceneGraph(replacementGraph.get()),
            "a replacement SceneGraph pointer must not inherit proofs");
        Check(!build.proofs->Find(
                replacementMesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                output),
            "a replacement mesh pointer must not inherit an existing geometry proof");
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                replacementGeometry.get(),
                fixture.buffers.get(),
                output),
            "a replacement geometry pointer must not alias an existing proof");
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                replacementBuffers.get(),
                output),
            "a replacement BufferGroup pointer must not inherit an existing proof");
        Check(output.firstIndex == 0u && output.indexCount == 0u &&
                output.firstVertex == 0u &&
                output.vertexCount == 0u &&
                output.contentRevision == 0u &&
                output.maximumIndex == InvalidVisibilityWord &&
                !output.positionBoundsValid,
            "failed identity lookup must clear its public proof output");
    }

    void TestSamePointerMutationAndRevisionRejection()
    {
        SceneFixture fixture;
        const CompactRasterCpuIndexProofBuildResult build =
            BuildCompactRasterCpuIndexProofs(*fixture.graph);
        Check(build.Succeeded(),
            "mutation rejection requires a valid baseline proof set");
        if (!build.Succeeded())
            return;

        CompactRasterCpuIndexProof output;
        const uint64_t firstIndex = uint64_t(fixture.mesh->indexOffset) +
            fixture.firstGeometry->indexOffsetInMesh;
        const uint32_t savedIndex = fixture.buffers->indexData[
            static_cast<size_t>(firstIndex)];
        fixture.buffers->indexData[static_cast<size_t>(firstIndex)] = 1u;
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                output),
            "same-pointer in-place index replacement invalidates the loader proof");
        fixture.buffers->indexData[static_cast<size_t>(firstIndex)] =
            savedIndex;

        const dm::float3 savedPosition = fixture.buffers->positionData[1];
        fixture.buffers->positionData[1].x =
            std::nextafter(savedPosition.x, 0.f);
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                output),
            "same-pointer in-place position replacement invalidates the loader proof");
        fixture.buffers->positionData[1] = savedPosition;

        const dm::float2 savedTexCoord = fixture.buffers->texcoord1Data[1];
        fixture.buffers->texcoord1Data[1].x =
            std::nextafter(savedTexCoord.x, 1.f);
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                output),
            "same-pointer raw texcoord replacement invalidates the finite shading-stream proof");
        fixture.buffers->texcoord1Data[1] = savedTexCoord;

        const uint32_t savedNormal = fixture.buffers->normalData[1];
        fixture.buffers->normalData[1] ^= 1u;
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                output),
            "same-pointer packed normal replacement invalidates exact stream identity");
        fixture.buffers->normalData[1] = savedNormal;

        const uint32_t savedTangent = fixture.buffers->tangentData[1];
        fixture.buffers->tangentData[1] ^= 1u;
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                output),
            "same-pointer packed tangent replacement invalidates exact stream identity");
        fixture.buffers->tangentData[1] = savedTangent;

        fixture.mesh->indexOffset++;
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                output),
            "same-pointer mesh topology mutation cannot reuse an old range proof");
        fixture.mesh->indexOffset--;

        fixture.firstGeometry->vertexOffsetInMesh++;
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                output),
            "same-pointer LOD stream-origin replacement cannot reuse an old proof");
        fixture.firstGeometry->vertexOffsetInMesh--;

        const std::shared_ptr<Material> savedMaterial =
            fixture.firstGeometry->material;
        fixture.firstGeometry->material = std::make_shared<Material>();
        Check(!build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                output),
            "same-pointer material/topology replacement invalidates exact geometry identity");
        fixture.firstGeometry->material = savedMaterial;

        CompactRasterCpuIndexProof oldProof;
        Check(build.proofs->Find(
                fixture.mesh.get(),
                fixture.firstGeometry.get(),
                fixture.buffers.get(),
                oldProof),
            "restoring every exact source operand restores the original immutable proof");
        const CompactRasterCpuIndexProofBuildResult rebuilt =
            BuildCompactRasterCpuIndexProofs(*fixture.graph);
        CompactRasterCpuIndexProof rebuiltProof;
        Check(rebuilt.Succeeded() &&
                rebuilt.proofs->BuildRevision() !=
                    build.proofs->BuildRevision() &&
                rebuilt.proofs->Find(
                    fixture.mesh.get(),
                    fixture.firstGeometry.get(),
                    fixture.buffers.get(),
                    rebuiltProof) &&
                rebuiltProof.contentRevision != 0u &&
                rebuiltProof.contentRevision !=
                    oldProof.contentRevision,
            "a rebuilt same-pointer scene publishes fresh non-aliasing proof revisions");
    }

    void TestBadRangeAndIndexRejection()
    {
        SceneFixture badRange;
        badRange.secondGeometry->indexOffsetInMesh = 8u;
        const CompactRasterCpuIndexProofBuildResult rangeBuild =
            BuildCompactRasterCpuIndexProofs(*badRange.graph);
        Check(!rangeBuild.Succeeded() && !rangeBuild.proofs &&
                rangeBuild.reason == FallbackReason::ReconstructionFailure,
            "a geometry range extending beyond source indexData must fail without publishing partial proofs");

        SceneFixture badIndex;
        const uint64_t firstIndex = uint64_t(badIndex.mesh->indexOffset) +
            badIndex.firstGeometry->indexOffsetInMesh;
        badIndex.buffers->indexData[static_cast<size_t>(firstIndex + 1u)] =
            badIndex.firstGeometry->numVertices;
        const CompactRasterCpuIndexProofBuildResult indexBuild =
            BuildCompactRasterCpuIndexProofs(*badIndex.graph);
        Check(!indexBuild.Succeeded() && !indexBuild.proofs &&
                indexBuild.reason == FallbackReason::ReconstructionFailure,
            "an index equal to vertexCount must fail without publishing partial proofs");

        SceneFixture nonFinite;
        nonFinite.buffers->positionData[2].x =
            std::numeric_limits<float>::infinity();
        const CompactRasterCpuIndexProofBuildResult nonFiniteBuild =
            BuildCompactRasterCpuIndexProofs(*nonFinite.graph);
        Check(!nonFiniteBuild.Succeeded() && !nonFiniteBuild.proofs &&
                nonFiniteBuild.reason ==
                    FallbackReason::ReconstructionFailure,
            "non-finite indexed positions cannot produce trusted compact bounds");

        SceneFixture nonFiniteTexCoord;
        nonFiniteTexCoord.buffers->texcoord1Data[2].y =
            std::numeric_limits<float>::quiet_NaN();
        const CompactRasterCpuIndexProofBuildResult nonFiniteTexCoordBuild =
            BuildCompactRasterCpuIndexProofs(*nonFiniteTexCoord.graph);
        Check(!nonFiniteTexCoordBuild.Succeeded() &&
                !nonFiniteTexCoordBuild.proofs &&
                nonFiniteTexCoordBuild.reason ==
                    FallbackReason::ReconstructionFailure,
            "non-finite raw texcoords cannot enter analytic-gradient reconstruction");

        SceneFixture missingShadingStream;
        missingShadingStream.buffers->normalData.clear();
        const CompactRasterCpuIndexProofBuildResult missingStreamBuild =
            BuildCompactRasterCpuIndexProofs(*missingShadingStream.graph);
        Check(missingStreamBuild.Succeeded() &&
                missingStreamBuild.proofs->GeometryCount() == 0u,
            "a missing CPU-only shading stream omits reconstruction proofs while preserving full-G-buffer fallback eligibility");

        SceneFixture subnormal;
        for (size_t index = 1u;
            index < subnormal.buffers->positionData.size();
            ++index)
        {
            subnormal.buffers->positionData[index].x =
                (index & 1u) != 0u
                    ? std::numeric_limits<float>::denorm_min()
                    : -std::numeric_limits<float>::denorm_min();
        }
        const CompactRasterCpuIndexProofBuildResult subnormalBuild =
            BuildCompactRasterCpuIndexProofs(*subnormal.graph);
        CompactRasterCpuIndexProof subnormalProof;
        Check(subnormalBuild.Succeeded() &&
                subnormalBuild.proofs->Find(
                    subnormal.mesh.get(),
                    subnormal.firstGeometry.get(),
                    subnormal.buffers.get(),
                    subnormalProof) &&
                subnormalProof.positionBoundsMin[0] == 0.f &&
                subnormalProof.positionBoundsMax[0] == 0.f,
            "loader-time bounds canonicalize D3D FTZ position inputs before proof capture");
    }
}

int main()
{
    TestPublicProofIsConstantSize();
    TestValidCaptureAndPostUploadSurvival();
    TestExactIdentityAndReplacementRejection();
    TestSamePointerMutationAndRevisionRejection();
    TestBadRangeAndIndexRejection();

    if (g_Passed)
    {
        std::cout <<
            "UVSR adaptive MSAA loader index-proof validation passed\n";
    }
    return g_Passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
