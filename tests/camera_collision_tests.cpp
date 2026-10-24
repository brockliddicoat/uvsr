#include "camera_collision.h"
#include "renderer_scene.h"
#include <new>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error collision tests require exception-disabled compilation
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            fprintf(stderr, "collision structure: %s\n", message);
            exit(1);
        }
    }

    void HashWord(uint64_t& hash, uint32_t value)
    {
        for (unsigned byte = 0; byte < 4; ++byte)
        {
            hash ^= (value >> (byte * 8)) & 255;
            hash *= 1099511628211ull;
        }
    }

    void HashPoint(uint64_t& hash, uvsr::gpu_contract::Float3 point)
    {
        const float lanes[3]{point.x, point.y, point.z};
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            uint32_t bits;
            memcpy(&bits, &lanes[axis], sizeof(bits));
            HashWord(hash, bits);
        }
    }
}

namespace uvsr
{
    struct CameraCollisionTestAccess
    {
        static void Check(const CameraCollisionWorld& world, uint32_t count)
        {
            Require(world.m_TriangleCount == count && world.m_NodeCount != 0, "input count changed");
            uint32_t pending[32];
            unsigned pendingCount = 1;
            pending[0] = 0;
            uint32_t expectedNode = 0;
            uint32_t expectedTriangle = 0;
            uint64_t hash = 14695981039346656037ull;
            while (pendingCount)
            {
                const uint32_t index = pending[--pendingCount];
                Require(index == expectedNode++ && index < world.m_NodeCount, "preorder or child reference changed");
                const auto& node = world.m_Nodes[index];
                HashPoint(hash, node.minimum);
                HashPoint(hash, node.maximum);
                HashWord(hash, node.firstTriangle);
                HashWord(hash, node.triangleCount);
                HashWord(hash, node.leftChild);
                HashWord(hash, node.rightChild);
                if (node.triangleCount == 0)
                {
                    Require(node.leftChild == index + 1 && node.rightChild > node.leftChild &&
                        node.rightChild < world.m_NodeCount, "children are not distinct forward references");
                    Require(pendingCount + 2 <= 32, "test traversal exhausted its validated uint32 bound");
                    pending[pendingCount++] = node.rightChild;
                    pending[pendingCount++] = node.leftChild;
                }
                else
                {
                    Require(node.firstTriangle == expectedTriangle &&
                        node.triangleCount <= count - expectedTriangle, "leaf ranges overlap or have holes");
                    expectedTriangle += node.triangleCount;
                }
            }
            Require(expectedNode == world.m_NodeCount && expectedTriangle == count, "tree has unreachable data");
            if (count == 17)
            {
                const uint32_t expected[5][4] = {
                    { 0, 0, 1, 2 }, { 0, 8, 0, 0 }, { 8, 0, 3, 4 },
                    { 8, 4, 0, 0 }, { 12, 5, 0, 0 } };
                Require(world.m_NodeCount == 5, "17-triangle fixture node count changed");
                for (unsigned index = 0; index < 5; ++index)
                {
                    const auto& node = world.m_Nodes[index];
                    Require(node.firstTriangle == expected[index][0] && node.triangleCount == expected[index][1] &&
                        node.leftChild == expected[index][2] && node.rightChild == expected[index][3],
                        "literal left-first preorder fixture changed");
                }
            }
            for (size_t index = 0; index < world.m_TriangleCount; ++index)
            {
                const auto& triangle = world.m_Triangles[index];
                HashPoint(hash, triangle.a);
                HashPoint(hash, triangle.b);
                HashPoint(hash, triangle.c);
            }
            // evidence for a fixed toolset, not a cross-STL permutation promise.
            const uint32_t fixtureCounts[]{1, 8, 9, 17, 128, 257, 8193, 129};
            const uint64_t expectedHashes[]{0xbb530f6b48d4b1c4ull, 0xaa5961de95524634ull,
                0x5fe19e56d6ca1026ull, 0xf962efefe0c720efull, 0xc4f008b47c561c99ull,
                0xa82744daa835411aull, 0x839be27c25ee6565ull, 0x0ccafad77077a944ull};
            for (unsigned fixture = 0; fixture < 8; ++fixture)
                if (count == fixtureCounts[fixture])
                    Require(hash == expectedHashes[fixture], "fixed-toolset triangle/tree bytes changed");
            printf("triangles=%u nodes=%zu preorder_fnv=%016llx\n", count,
                world.m_NodeCount, static_cast<unsigned long long>(hash));
        }

        static void Same(const CameraCollisionWorld& left, const CameraCollisionWorld& right)
        {
            Require(left.m_TriangleCount == right.m_TriangleCount && left.m_NodeCount == right.m_NodeCount,
                "canonical extraction changed triangle/tree counts");
            Require(memcmp(left.m_Triangles, right.m_Triangles, left.m_TriangleCount * sizeof(CameraCollisionWorld::Triangle)) == 0 &&
                memcmp(left.m_Nodes, right.m_Nodes, left.m_NodeCount * sizeof(CameraCollisionWorld::BvhNode)) == 0,
                "canonical extraction changed triangle order, transform math or tree bytes");
        }

        static void CheckCapacityFailure()
        {
            CameraCollisionWorld candidate;
            Require(candidate.AllocateTriangles(17) == CameraCollisionBuildError::None, "fixture allocation failed");
            for (unsigned index = 0; index < 17; ++index)
            {
                const float x = float(index);
                candidate.m_Triangles[candidate.m_TriangleCount++] = {{x, 0, 0}, {x, 1, 0}, {x, 0, 1}};
            }
            candidate.m_Nodes = new (std::nothrow) CameraCollisionWorld::BvhNode[1];
            Require(candidate.m_Nodes != nullptr, "fixture node allocation failed");
            candidate.m_NodeCapacity = 1;
            Require(candidate.BuildNodes(1) == CameraCollisionBuildError::CapacityExceeded &&
                candidate.m_NodeCount == 1, "node capacity failure grew storage or reported success");
        }
    };
}

namespace
{
    using namespace uvsr;

    void CheckPackedPositions()
    {
        RendererSceneNode node;
        RendererSceneMesh mesh;
        mesh.bufferGroupIndex = 0;
        mesh.geometries = {0, 1};
        RendererSceneInstance instance;
        instance.meshIndex = instance.nodeIndex = 0;
        RendererSceneGeometry geometry;
        geometry.indexCount = 6;
        RendererSceneBufferGroup group;
        RendererSceneView view;
        view.generation = 9;
        view.nodes = {&node, 1};
        view.meshes = {&mesh, 1};
        view.instances = {&instance, 1};
        view.geometries = {&geometry, 1};
        view.bufferGroups = {&group, 1};
        const CameraCollisionWorld::Triangle expected{{1, 0, 0}, {1, 1, 0}, {1, 0, 1}};
        CameraCollisionWorld reference, candidate;
        Require(reference.Build({&expected, 1}) == CameraCollisionBuildError::None, "packed reference");
        const uint32_t counts[]{3, 5, 6, 8};
        for (uint32_t count : counts)
        {
            mesh.vertexCount = geometry.vertexCount = count;
            uint32_t indices[]{0, 1, 2, 0, 1, count};
            alignas(float) unsigned char storage[128]{};
            // deliberately unaligned. zero padding would form a valid second
            // triangle if an out-of-geometry index could read a phantom point.
            auto* positions = storage + 1;
            memcpy(positions, &expected, sizeof(expected));
            const size_t payload = size_t(count) * sizeof(CameraCollisionWorld::Point);
            const size_t padded = (payload + 15) & ~size_t(15);
            CameraCollisionSourceBuffers source{{reinterpret_cast<const unsigned char*>(indices), sizeof(indices)},
                {positions, padded}};
            Require(candidate.BuildFromScene(view, {&source, 1}) == CameraCollisionBuildError::None,
                "GPU position padding rejected");
            CameraCollisionTestAccess::Same(reference, candidate);
            source.positions.count = payload - 1;
            Require(candidate.BuildFromScene(view, {&source, 1}) == CameraCollisionBuildError::InvalidSource,
                "missing real position byte escaped");
            CameraCollisionTestAccess::Same(reference, candidate);
        }
        const CameraCollisionWorld::Triangle second{{2, 0, 0}, {2, 1, 0}, {2, 0, 1}};
        const CameraCollisionWorld::Triangle two[]{expected, second};
        unsigned char positions[80]{};
        memcpy(positions, two, sizeof(two));
        const uint32_t indices[]{0, 1, 2, 0, 1, 3, 0, 1, 2};
        RendererSceneGeometry geometries[]{geometry, geometry};
        geometries[0].vertexCount = 3;
        geometries[1].indexOffsetInMesh = 6;
        geometries[1].vertexOffsetInMesh = 3;
        geometries[1].vertexCount = geometries[1].indexCount = 3;
        mesh.vertexCount = 6;
        mesh.geometries.count = 2;
        view.geometries = geometries;
        const CameraCollisionSourceBuffers source{{reinterpret_cast<const unsigned char*>(indices), sizeof(indices)}, positions};
        Require(reference.Build(two) == CameraCollisionBuildError::None &&
            candidate.BuildFromScene(view, {&source, 1}) == CameraCollisionBuildError::None,
            "adjacent primitive fixture");
        CameraCollisionTestAccess::Same(reference, candidate);
        printf("packed collision: 0/4/8/12 padding bytes, unaligned positions, 4 missing-byte rejections and phantom/adjacent primitive omission pass\n");
    }

    void CheckCanonicalExtraction()
    {
        RendererSceneNode nodes[2];
        nodes[0].world.translation[0] = 10;
        nodes[1].world.linear[0] = -2;
        RendererSceneGeometry geometries[2];
        geometries[0].indexCount = 3;
        geometries[0].vertexCount = 4;
        geometries[0].indexOffsetInMesh = 1;
        geometries[0].vertexOffsetInMesh = 1;
        geometries[1] = geometries[0];
        geometries[1].indexOffsetInMesh = 4;
        RendererSceneMesh meshes[2];
        meshes[0].geometries = {0, 2};
        meshes[0].bufferGroupIndex = 0;
        meshes[0].indexOffset = 2;
        meshes[0].vertexOffset = 1;
        meshes[0].vertexCount = 5;
        meshes[1].skinPrototypeIndex = 0;
        meshes[1].bufferGroupIndex = 1;
        RendererSceneInstance instances[2];
        instances[0].meshIndex = 1;
        instances[0].nodeIndex = 0;
        instances[1].meshIndex = 0;
        instances[1].nodeIndex = 1;
        RendererSceneBufferGroup groups[2];
        RendererSceneView view;
        view.generation = 7;
        view.nodes = nodes;
        view.meshes = meshes;
        view.instances = instances;
        view.geometries = geometries;
        view.bufferGroups = groups;
        const uint32_t indices[]{99, 99, 99, 0, 1, 2, 2, 1, 3};
        const float positions[][3]{{99, 99, 99}, {99, 99, 99}, {1, 0, 0}, {1, 1, 0}, {1, 0, 1}, {1, 1, 1}};
        CameraCollisionSourceBuffers sources[2];
        sources[0] = {{reinterpret_cast<const unsigned char*>(indices), sizeof(indices)},
            {reinterpret_cast<const unsigned char*>(positions), sizeof(positions)}};
        const uint32_t order[]{1, 0};
        const CameraCollisionWorld::Triangle expected[]{
            {{-2, 0, 0}, {-2, 1, 0}, {-2, 0, 1}}, {{-2, 0, 1}, {-2, 1, 0}, {-2, 1, 1}},
            {{11, 0, 0}, {11, 1, 0}, {11, 0, 1}}, {{11, 0, 1}, {11, 1, 0}, {11, 1, 1}}};
        CameraCollisionWorld reference, candidate;
        Require(reference.Build(expected) == CameraCollisionBuildError::None, "reference build failed");
        Require(candidate.BuildFromScene(view, sources, order) == CameraCollisionBuildError::None, "canonical build failed");
        CameraCollisionTestAccess::Same(reference, candidate);
        alignas(uint32_t) unsigned char packedIndices[sizeof(indices) + 1];
        memcpy(packedIndices + 1, indices, sizeof(indices));
        const auto alignedIndices = sources[0].indices;
        sources[0].indices = {packedIndices + 1, sizeof(indices)};
        Require(candidate.BuildFromScene(view, sources, order) == CameraCollisionBuildError::None,
            "packed unaligned index bytes do not require typed array objects");
        CameraCollisionTestAccess::Same(reference, candidate);
        sources[0].indices = alignedIndices;
        // omit invalid individual triples while retaining the surrounding valid
        // triangles in their original order, including skin-prototype instances.
        const uint32_t filteredIndices[]{99, 99, 99, 0, 1, 2, 99, 1, 2, 0, 0, 0, 4, 1, 2, 2, 1, 3};
        float filteredPositions[7][3]{};
        memcpy(filteredPositions, positions, sizeof(positions));
        const uint32_t nan = 0x7fc00000u;
        memcpy(&filteredPositions[6][0], &nan, sizeof(nan));
        CameraCollisionSourceBuffers filteredSources[]{
            {{reinterpret_cast<const unsigned char*>(filteredIndices), sizeof(filteredIndices)},
                {reinterpret_cast<const unsigned char*>(filteredPositions), sizeof(filteredPositions)}}, {}};
        geometries[0].indexCount = 15;
        geometries[0].vertexCount = 5;
        meshes[0].vertexCount = 6;
        geometries[1].primitive = RendererScenePrimitive::Lines;
        Require(candidate.BuildFromScene(view, filteredSources, order) == CameraCollisionBuildError::None,
            "individual malformed or unusable triangle failed the scene");
        CameraCollisionTestAccess::Same(reference, candidate);
        geometries[0].indexCount = 3;
        geometries[0].vertexCount = 4;
        meshes[0].vertexCount = 5;
        geometries[1].primitive = RendererScenePrimitive::Triangles;
        for (uint32_t ordinal = 1; ordinal <= 3; ++ordinal)
        {
            SetCameraCollisionAllocationFailure(ordinal);
            Require(candidate.BuildFromScene(view, sources, order) == CameraCollisionBuildError::AllocationFailed,
                "scene allocation fault escaped");
            CameraCollisionTestAccess::Same(reference, candidate);
        }
        SetCameraCollisionAllocationFailure(0);
        for (unsigned fault = 0; fault < 17; ++fault)
        {
            RendererSceneView malformed = view;
            auto source = sources[0];
            uint32_t badOrder[]{1, 0};
            CameraCollisionSourceBuffers badSources[]{source, {}};
            switch (fault)
            {
            case 0: malformed.generation = 0; break;
            case 1: malformed.nodes.data = nullptr; break;
            case 2: badOrder[1] = 1; break;
            case 3: badOrder[1] = 2; break;
            case 4: badSources[0].positions.count -= 1; break;
            case 5: badSources[0].positions.data = nullptr; break;
            case 6: badSources[0].indices.count = 4 * sizeof(uint32_t); break;
            case 7: instances[0].nodeIndex = 2; break;
            case 8: instances[0].meshIndex = 2; break;
            case 9: meshes[1].skinPrototypeIndex = 2; break;
            case 10: meshes[0].bufferGroupIndex = 2; break;
            case 11: meshes[0].geometries.count = 3; break;
            case 12: nodes[0].world.linear[0] = DBL_MAX; break;
            case 13: nodes[0].world.translation[0] = -DBL_MAX; break;
            case 14: --badSources[0].indices.count; break;
            case 15: ++geometries[0].vertexCount; break;
            case 16: meshes[0].vertexCount = 0; break;
            }
            Require(candidate.BuildFromScene(malformed, badSources, badOrder) == CameraCollisionBuildError::InvalidSource,
                "malformed canonical source escaped");
            CameraCollisionTestAccess::Same(reference, candidate);
            instances[0].nodeIndex = 0; instances[0].meshIndex = 1;
            meshes[1].skinPrototypeIndex = 0; meshes[0].bufferGroupIndex = 0; meshes[0].geometries.count = 2;
            nodes[0].world.linear[0] = 1; nodes[0].world.translation[0] = 10;
            geometries[0].vertexCount = 4; meshes[0].vertexCount = 5;
        }
        for (uint32_t ordinal = 1; ordinal <= 2; ++ordinal)
        {
            SetCameraCollisionAllocationFailure(ordinal);
            Require(candidate.Build(expected) == CameraCollisionBuildError::AllocationFailed, "copy allocation fault escaped");
            CameraCollisionTestAccess::Same(reference, candidate);
        }
        SetCameraCollisionAllocationFailure(0);
        CameraCollisionWorld moved(static_cast<CameraCollisionWorld&&>(candidate));
        Require(candidate.Empty() && candidate.GetTriangleCapacity() == 0, "move left ownership behind");
        CameraCollisionTestAccess::Same(reference, moved);
        candidate = static_cast<CameraCollisionWorld&&>(moved);
        Require(moved.Empty() && moved.GetNodeCapacity() == 0, "move assignment left ownership behind");
        const auto triangleCapacity = candidate.GetTriangleCapacity(), nodeCapacity = candidate.GetNodeCapacity();
        candidate.Clear();
        Require(candidate.Empty() && candidate.GetTriangleCapacity() == triangleCapacity && candidate.GetNodeCapacity() == nodeCapacity,
            "Clear changed retained capacity");
        Require(candidate.Build({}) == CameraCollisionBuildError::None && candidate.GetTriangleCapacity() == 0 && candidate.GetNodeCapacity() == 0,
            "empty replacement retained old arrays");
        printf("canonical collision: transforms, skin prototype, offsets, permutation, 17 invalid sources, 5 allocation faults and moves pass\n");
    }
}

int main()
{
    using namespace uvsr;
    const uint32_t counts[] = {1, 8, 9, 17, 128, 257, 8193};
    for (uint32_t count : counts)
    {
        auto* triangles = new (std::nothrow) CameraCollisionWorld::Triangle[count];
        Require(triangles != nullptr, "fixture allocation failed");
        for (uint32_t index = 0; index < count; ++index)
        {
            const float x = float((index * 37) % 127);
            const float y = float((index * 17) % 61);
            const float z = float((index * 7) % 31);
            triangles[index] = {{x, y, z}, {x + .5f, y, z}, {x, y + .5f, z}};
        }
        CameraCollisionWorld world;
        Require(world.Build({triangles, count}) == CameraCollisionBuildError::None, "build failed");
        delete[] triangles;
        CameraCollisionTestAccess::Check(world, count);
    }
    CameraCollisionWorld::Triangle coincident[129];
    for (auto& triangle : coincident) triangle = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    CameraCollisionWorld world;
    Require(world.Build(coincident) == CameraCollisionBuildError::None, "coincident build failed");
    CameraCollisionTestAccess::Check(world, 129);
    CameraCollisionTestAccess::CheckCapacityFailure();
    CheckCanonicalExtraction();
    CheckPackedPositions();
    return 0;
}
