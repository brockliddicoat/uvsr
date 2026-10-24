#include "renderer_import_scene.h"
#include "import_geometry_fixture.h"
#include "import_geometry_math_fixture.h"

#include <Windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;
    using namespace uvsr::test_fixture;
    using Attribute = RendererSceneVertexAttribute;
    constexpr uint32_t invalid = InvalidSceneIndex;
    size_t rejected = 0;

    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "import geometry check failed: %s\n", reason);
        exit(1);
    }
    void Good(ImportResult result, const char* reason)
    {
        if (result) return;
        fprintf(stderr, "import geometry check failed: %s: %s, object %u, index %zu, parser %u\n",
            reason, ImportErrorText(result.error), unsigned(result.object), result.index, result.parserCode);
        exit(1);
    }
    void Bad(ImportResult result, ImportError expected, const char* reason)
    {
        if (result.error != expected)
        {
            fprintf(stderr, "import geometry rejection mismatch: %s: expected %s, got %s, object %u, index %zu\n",
                reason, ImportErrorText(expected), ImportErrorText(result.error), unsigned(result.object), result.index);
            exit(1);
        }
        ++rejected;
    }

    struct Text
    {
        char* data = nullptr;
        size_t count = 0, capacity = 0;
        explicit Text(size_t bytes = 131072) : data(static_cast<char*>(malloc(bytes))), capacity(bytes)
        { Require(data != nullptr, "fixture text allocation"); data[0] = 0; }
        ~Text() { free(data); }
        void Append(const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            const int written = vsnprintf(data + count, capacity - count, format, args);
            va_end(args);
            Require(written >= 0 && size_t(written) < capacity - count, "fixture text capacity");
            count += size_t(written);
        }
    };

    struct Fixture
    {
        struct Accessor { size_t offset, size, count, stride; unsigned component; const char* shape; bool normalized; } accessors[32]{};
        uint8_t bytes[16384]{};
        size_t byteCount = 0, accessorCount = 0;
        Text json;
        size_t Add(const void* data, size_t size, size_t count, unsigned component, const char* shape,
            bool normalized = false, size_t stride = 0)
        {
            byteCount = (byteCount + 3) & ~size_t(3);
            Require(accessorCount < 32 && size <= sizeof(bytes) - byteCount, "fixture accessor capacity");
            const size_t index = accessorCount++;
            accessors[index] = {byteCount, size, count, stride, component, shape, normalized};
            memcpy(bytes + byteCount, data, size);
            byteCount += size;
            return index;
        }
        void Parse(ImportDocument& document, const char* body)
        {
            json.Append("{\"asset\":{\"version\":\"2.0\"},\"scene\":0");
            if (byteCount)
            {
                json.Append(",\"buffers\":[{\"uri\":\"fixture.bin\",\"byteLength\":%zu}],\"bufferViews\":[", byteCount);
                for (size_t i = 0; i < accessorCount; ++i)
                {
                    const auto& a = accessors[i];
                    json.Append("%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu", i ? "," : "", a.offset, a.size);
                    if (a.stride) json.Append(",\"byteStride\":%zu", a.stride);
                    json.Append("}");
                }
                json.Append("],\"accessors\":[");
                for (size_t i = 0; i < accessorCount; ++i)
                {
                    const auto& a = accessors[i];
                    json.Append("%s{\"bufferView\":%zu,\"componentType\":%u,\"count\":%zu,\"type\":\"%s\",\"normalized\":%s}",
                        i ? "," : "", i, a.component, a.count, a.shape, a.normalized ? "true" : "false");
                }
                json.Append("]");
            }
            json.Append("%s}", body);
            Good(document.Parse({reinterpret_cast<const uint8_t*>(json.data), json.count}), "parse geometry fixture");
            if (byteCount) Good(document.SupplyBuffer(0, {bytes, byteCount}), "supply geometry fixture");
        }
    };

    ImportSceneOptions Options()
    { ImportSceneOptions value; value.modelName = {"fixture.gltf", 12}; value.generation = 37; return value; }

    template<class T> T Read(ArrayView<const uint8_t> bytes, size_t offset)
    {
        Require(offset <= bytes.count && sizeof(T) <= bytes.count - offset, "fixture byte read in bounds");
        T value{}; memcpy(&value, bytes.data + offset, sizeof(T)); return value;
    }
    template<class T> T Vertex(const RendererSceneView& scene, const ImportGeometry& geometry, uint32_t group, Attribute attribute, uint32_t index)
    {
        const auto& layout = scene.bufferGroups.data[group];
        return Read<T>(geometry.Buffer(group).vertices, size_t(layout.attributes[uint32_t(attribute)].offset) + size_t(index) * sizeof(T));
    }
    bool TextEquals(const RendererSceneView& scene, RendererSceneString span, const char* expected)
    { const auto text = RendererSceneText(scene, span); return text.count == strlen(expected) && memcmp(text.data, expected, text.count) == 0; }

    const float triangle[]{0,0,0, 1,0,0, 0,1,0};
    const float normals[]{0,0,1, 0,0,1, 0,0,1};
    const float uvs[]{0,0, 1,0, 0,1};

    void OrderingAndOwnership()
    {
        Fixture fixture;
        for (uint32_t m = 0; m < 3; ++m)
        {
            float positions[9]; memcpy(positions, triangle, sizeof(positions));
            for (uint32_t i = 0; i < 3; ++i) positions[i * 3] += float(m * 10);
            fixture.Add(positions, sizeof(positions), 3, 5126, "VEC3");
        }
        ImportDocument document;
        fixture.Parse(document, R"(,"meshes":[{"name":"source zero","primitives":[{"attributes":{"POSITION":0}}]},{"name":"source one","primitives":[{"attributes":{"POSITION":1}}]},{"name":"unused","primitives":[{"attributes":{"POSITION":2}}]}],"nodes":[{"name":"parent","mesh":0,"translation":[1,2,3],"children":[3]},{"name":"first","mesh":1,"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]},{"name":"unused"},{"name":"child","mesh":1}],"scenes":[{"nodes":[1,0]}])");
        RendererScene scene;
        ImportGeometry geometry;
        auto options = Options();
        char name[]{"fixture.gltf"}; options.modelName = {name, 12};
        Good(ConvertImportScene(document, options, scene, geometry), "source/hierarchy/instance ordering");
        CompareImportGeometryReference({fixture.json.data, fixture.json.count}, {fixture.bytes, fixture.byteCount}, scene.View(), geometry);
        document.Reset(); memset(fixture.bytes, 0xcc, sizeof(fixture.bytes)); memset(fixture.json.data, 0xee, fixture.json.count); memset(name, 0xdd, 12);
        const auto view = scene.View();
        Require(view.nodes.count == 4 && view.meshes.count == 2 && view.instances.count == 3 && view.bufferGroups.count == 1, "selected graph counts");
        Require(TextEquals(view, view.nodes.data[0].name, "fixture.gltf") && TextEquals(view, view.meshes.data[0].name, "source one"), "owned names and first-use mesh order");
        Require(view.meshes.data[0].vertexOffset == 3 && view.meshes.data[1].vertexOffset == 0, "payload keeps source mesh offsets");
        Require(view.nodes.data[0].firstChildIndex == 1 && view.nodes.data[1].nextSiblingIndex == 2 &&
            view.nodes.data[2].firstChildIndex == 3 && view.nodes.data[3].parentIndex == 2, "root and sibling preorder");
        Require(view.nodes.data[1].hasLocalTransform && view.nodes.data[2].hasLocalTransform && !view.nodes.data[3].hasLocalTransform,
            "authored identity presence retained");
        for (uint32_t i = 0; i < 3; ++i)
            Require(view.instances.data[i].nodeIndex == i + 1 && view.instances.data[i].meshIndex == (i == 1 ? 1u : 0u), "native unskinned registration");
        const auto child = view.nodes.data[3];
        Require(child.world.translation[0] == 1 && child.world.translation[1] == 2 && child.world.translation[2] == 3 &&
            memcmp(&child.world, &child.previousWorld, sizeof(child.world)) == 0, "inherited transform and initial history");
        Require(Vertex<gpu_contract::Float3>(view, geometry, 0, Attribute::Position, 6).x == 20, "unreferenced source payload retained");
        for (uint32_t i = 0; i < 9; ++i) Require(Read<uint32_t>(geometry.Buffer(0).indices, i * 4) == i % 3, "generated local indices");
        Require(geometry.Buffer(0).indexOwner == 0 && geometry.StorageBytes() > 0 && geometry.ConversionScratchBytes() > 0, "owned storage accounting");
        Bad(ConvertImportScene(document, options, scene, geometry), ImportError::InvalidState, "populated outputs rejected");
        ImportGeometry moved(static_cast<ImportGeometry&&>(geometry));
        Require(geometry.BufferCount() == 0 && moved.BufferCount() == 1, "geometry move constructor");
        geometry = static_cast<ImportGeometry&&>(moved);
        Require(moved.BufferCount() == 0 && Vertex<gpu_contract::Float3>(view, geometry, 0, Attribute::Position, 6).x == 20, "moved bytes still valid");
        geometry = static_cast<ImportGeometry&&>(geometry);
        Require(geometry.BufferCount() == 1, "self move preserves geometry");
        geometry.Reset(); geometry.Reset(); scene.Reset();
    }

    void PackingAndTangents()
    {
        for (uint32_t mode = 0; mode < 6; ++mode)
        {
            Fixture fixture;
            fixture.Add(triangle, sizeof(triangle), 3, 5126, "VEC3");
            fixture.Add(normals, sizeof(normals), 3, 5126, "VEC3");
            float uv[6]; memcpy(uv, uvs, sizeof(uv));
            if (mode == 1) uv[2] = -1;
            if (mode == 2) memset(uv, 0, sizeof(uv));
            fixture.Add(uv, sizeof(uv), 3, 5126, "VEC2");
            float tangent[12]{2,0,0,-1, 1,2,3,1, -3,2,1,-1};
            if (mode == 3) fixture.Add(tangent, sizeof(tangent), 3, 5126, "VEC4");
            uint32_t indices[]{0,1,2};
            if (mode == 4) { indices[1] = 2; indices[2] = 1; }
            const uint16_t indices16[]{uint16_t(indices[0]),uint16_t(indices[1]),uint16_t(indices[2])};
            if (mode >= 4) fixture.Add(indices16, sizeof(indices16), 3, 5123, "SCALAR");
            Text body;
            body.Append(R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2%s}%s%s}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}])",
                mode == 3 ? ",\"TANGENT\":3" : "", mode >= 4 ? ",\"indices\":3" : "", mode == 5 ? ",\"mode\":3" : "");
            ImportDocument document; fixture.Parse(document, body.data);
            RendererScene scene; ImportGeometry geometry;
            Good(ConvertImportScene(document, Options(), scene, geometry), "packing/tangent conversion");
            if (mode != 2) CompareImportGeometryReference({fixture.json.data, fixture.json.count}, {fixture.bytes, fixture.byteCount}, scene.View(), geometry);
            const auto& expected = ImportTangentReferences[mode];
            for (uint32_t v = 0; v < 3; ++v)
            {
                Require(Vertex<uint32_t>(scene.View(), geometry, 0, Attribute::Normal, v) == 0x007f0000, "normal packing renormalizes xyz");
                const uint32_t actual = Vertex<uint32_t>(scene.View(), geometry, 0, Attribute::Tangent, v);
                if (actual != expected[v]) fprintf(stderr, "tangent mode %u vertex %u: expected %08x, actual %08x\n", mode, v, expected[v], actual);
                Require(actual == expected[v], "captured tangent bytes and sign");
            }
            if (mode == 0) Require(expected[0] == 0x8100007f, "literal tangent sign");
            if (mode == 1) Require(expected[0] == 0x7f000081, "literal mirrored UV tangent sign");
            if (mode == 3) Require(expected[0] == 0xc100007f, "authored tangent fourth lane uses xyz length");
        }
    }

    void MatrixTransforms()
    {
        static_assert(sizeof(ImportTransformReferences) / sizeof(ImportTransformReferences[0]) == 96);
        for (uint32_t test = 0; test < 96; ++test)
        {
            const auto& reference = ImportTransformReferences[test];
            const auto& matrix = reference.matrix;
            Text json;
            json.Append(R"({"asset":{"version":"2.0"},"nodes":[{"matrix":[)");
            for (uint32_t i = 0; i < 16; ++i) json.Append("%s%.9g", i ? "," : "", double(matrix[i]));
            json.Append(R"(]}],"scenes":[{"nodes":[0]}]})");
            ImportDocument document;
            Good(document.Parse({reinterpret_cast<const uint8_t*>(json.data), json.count}), "parse matrix oracle");
            RendererScene scene; ImportGeometry geometry;
            Good(ConvertImportScene(document, Options(), scene, geometry), "matrix node conversion");
            const auto& expected = reference.transform;
            const auto& actual = scene.View().nodes.data[1].transform;
            Require(memcmp(&expected, &actual, sizeof(expected)) == 0, "captured affine decomposition exact");
        }
    }

    void AttributesAndPrimitives()
    {
        for (uint32_t width = 0; width < 3; ++width)
        {
            Fixture fixture;
            const float positions[]{0,0,0,99, 1,0,0,99, 0,1,0,99, 1,1,0,99};
            fixture.Add(positions, sizeof(positions), 4, 5126, "VEC3", false, 16);
            const int8_t normalBytes[]{0,0,127,99, 0,0,127,99, 0,0,127,99, 0,0,127,99};
            fixture.Add(normalBytes, sizeof(normalBytes), 4, 5120, "VEC3", true, 4);
            const uint16_t texcoords[]{0,0, 65535,0, 0,65535, 65535,65535};
            fixture.Add(texcoords, sizeof(texcoords), 4, 5123, "VEC2", true);
            const uint8_t indices8[]{2,0,3}; const uint16_t indices16[]{2,0,3}; const uint32_t indices32[]{2,0,3};
            if (width == 0) fixture.Add(indices8, sizeof(indices8), 3, 5121, "SCALAR");
            else if (width == 1) fixture.Add(indices16, sizeof(indices16), 3, 5123, "SCALAR");
            else fixture.Add(indices32, sizeof(indices32), 3, 5125, "SCALAR");
            ImportDocument document;
            fixture.Parse(document, R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3},{"attributes":{"POSITION":0},"mode":1},{"attributes":{"POSITION":0},"mode":3}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}])");
            RendererScene scene; ImportGeometry geometry;
            Good(ConvertImportScene(document, Options(), scene, geometry), "strided and normalized geometry");
            const auto view = scene.View();
            Require(view.geometries.count == 3 && view.meshes.data[0].vertexCount == 12 && view.meshes.data[0].indexCount == 11, "mixed primitive counts");
            Require(view.geometries.data[0].primitive == RendererScenePrimitive::Triangles &&
                view.geometries.data[1].primitive == RendererScenePrimitive::Lines && view.geometries.data[2].primitive == RendererScenePrimitive::LineStrip,
                "retained primitive modes");
            Require(view.geometries.data[1].vertexOffsetInMesh == 4 && view.geometries.data[1].indexOffsetInMesh == 3 &&
                view.geometries.data[2].vertexOffsetInMesh == 8 && view.geometries.data[2].indexOffsetInMesh == 7, "mixed primitive offsets");
            for (uint32_t v = 0; v < 4; ++v)
            {
                const auto actual = Vertex<gpu_contract::Float3>(view, geometry, 0, Attribute::Position, v);
                Require(actual.x == positions[v*4] && actual.y == positions[v*4+1] && actual.z == 0, "strided position lanes exclude padding");
                Require(Vertex<uint32_t>(view, geometry, 0, Attribute::Normal, v) == 0x007f0000, "normalized signed normals");
                const auto uv = Vertex<gpu_contract::Float2>(view, geometry, 0, Attribute::TexCoord0, v);
                Require(uv.x == float(texcoords[v*2])/65535 && uv.y == float(texcoords[v*2+1])/65535, "normalized unsigned UVs");
                Require(Vertex<uint32_t>(view, geometry, 0, Attribute::Normal, v + 4) == 0 &&
                    Vertex<gpu_contract::Float2>(view, geometry, 0, Attribute::TexCoord0, v + 4).x == 0, "absent attributes use zero lanes");
            }
            for (uint32_t i = 0; i < 3; ++i) Require(Read<uint32_t>(geometry.Buffer(0).indices, i * 4) == indices32[i], "authored index widths");
            for (uint32_t i = 3; i < 11; ++i) Require(Read<uint32_t>(geometry.Buffer(0).indices, i * 4) == (i - 3) % 4, "nonindexed lines and strips");
        }

        const char* json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"sparse.bin","byteLength":32}],"bufferViews":[{"buffer":0,"byteLength":2},{"buffer":0,"byteOffset":4,"byteLength":24},{"buffer":0,"byteOffset":28,"byteLength":4}],"accessors":[{"componentType":5126,"count":3,"type":"VEC3","sparse":{"count":2,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":1}}},{"componentType":5123,"count":3,"type":"SCALAR","sparse":{"count":2,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":2}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}]})";
        uint8_t bytes[32]{1,2};
        const float positions[]{1,0,0,0,1,0}; memcpy(bytes + 4, positions, sizeof(positions));
        bytes[28] = 1; bytes[30] = 2;
        ImportDocument document;
        Good(document.Parse({reinterpret_cast<const uint8_t*>(json), strlen(json)}), "parse sparse geometry");
        Good(document.SupplyBuffer(0, bytes), "supply sparse geometry");
        RendererScene scene; ImportGeometry geometry;
        Good(ConvertImportScene(document, Options(), scene, geometry), "sparse positions and indices");
        for (uint32_t v = 0; v < 3; ++v)
        {
            const auto value = Vertex<gpu_contract::Float3>(scene.View(), geometry, 0, Attribute::Position, v);
            Require(value.x == triangle[v*3] && value.y == triangle[v*3+1] && value.z == 0 &&
                Read<uint32_t>(geometry.Buffer(0).indices, v * 4) == v, "sparse geometry values");
        }
    }

    void SkinsAndMorphs()
    {
        for (uint32_t mode = 0; mode < 4; ++mode)
        {
            Fixture fixture;
            fixture.Add(triangle, sizeof(triangle), 3, 5126, "VEC3");
            fixture.Add(normals, sizeof(normals), 3, 5126, "VEC3");
            fixture.Add(uvs, sizeof(uvs), 3, 5126, "VEC2");
            const uint8_t joints8[]{0,1,0,0, 0,1,0,0, 0,1,0,0};
            const uint16_t joints16[]{0,1,0,0, 0,1,0,0, 0,1,0,0};
            if (mode == 1) fixture.Add(joints8, sizeof(joints8), 3, 5121, "VEC4");
            else fixture.Add(joints16, sizeof(joints16), 3, 5123, "VEC4");
            const float weights[]{0.25f,0.75f,0,0, 0.25f,0.75f,0,0, 0.25f,0.75f,0,0};
            const uint8_t weights8[]{64,191,0,0, 64,191,0,0, 64,191,0,0};
            const uint16_t weights16[]{16384,49151,0,0, 16384,49151,0,0, 16384,49151,0,0};
            if (mode == 1) fixture.Add(weights8, sizeof(weights8), 3, 5121, "VEC4", true);
            else if (mode == 2) fixture.Add(weights16, sizeof(weights16), 3, 5123, "VEC4", true);
            else fixture.Add(weights, sizeof(weights), 3, 5126, "VEC4");
            float binds[32]{};
            for (uint32_t j = 0; j < 2; ++j) for (uint32_t lane = 0; lane < 16; lane += 5) binds[j * 16 + lane] = 1;
            binds[12] = -0.5f;
            if (mode != 3) fixture.Add(binds, sizeof(binds), 2, 5126, "MAT4");
            Text body;
            body.Append(R"(,"meshes":[{"name":"prototype","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"JOINTS_0":3,"WEIGHTS_0":4}}]},{"name":"static","primitives":[{"attributes":{"POSITION":0}}]}],"skins":[{"joints":[2,3]%s}],"nodes":[{"mesh":0,"skin":0,"translation":[10,0,0]},{"mesh":1},{"translation":[11,0,0]},{"translation":[12,0,0]},{"mesh":0,"skin":0,"translation":[20,0,0]}],"scenes":[{"nodes":[0,1,2,3,4]}])",
                mode != 3 ? ",\"inverseBindMatrices\":5" : "");
            ImportDocument document; fixture.Parse(document, body.data);
            RendererScene scene; ImportGeometry geometry;
            Good(ConvertImportScene(document, Options(), scene, geometry), "skin conversion");
            if (mode != 3) CompareImportGeometryReference({fixture.json.data, fixture.json.count}, {fixture.bytes, fixture.byteCount}, scene.View(), geometry);
            if (mode == 0)
            {
                for (uint32_t allocator = 0; allocator < 2; ++allocator)
                {
                    uint32_t failures = 0;
                    for (uint32_t ordinal = 0; ordinal < 80; ++ordinal)
                    {
                        RendererScene retryScene; ImportGeometry retryGeometry;
                        if (allocator == 0) SetImportAllocationFailureCountdown(ordinal);
                        else SetRendererSceneAllocationFailure(ordinal + 1);
                        const auto result = ConvertImportScene(document, Options(), retryScene, retryGeometry);
                        SetImportAllocationFailureCountdown(-1); SetRendererSceneAllocationFailure(0);
                        if (result) break;
                        Bad(result, ImportError::OutOfMemory, "skin conversion allocation fault");
                        Require(!retryScene.StorageBytes() && !retryGeometry.StorageBytes(), "skin failure leaves both owners empty");
                        Good(ConvertImportScene(document, Options(), retryScene, retryGeometry), "skin allocation retry");
                        const auto derived = retryGeometry.Buffer(2);
                        Require(derived.jointMatrices.count == 2 && derived.jointMatrices.data[0].values[12] == -9.5f &&
                            derived.indices.data == retryGeometry.Buffer(1).indices.data && !derived.vertices.data,
                            "retried palette, index alias and uninitialized GPU target contract");
                        ++failures;
                    }
                    Require(failures > 8 && failures < 80, "all skin owner allocations reached success");
                    printf("skin allocation faults: %s %u; each retried\n", allocator == 0 ? "import" : "canonical", failures);
                }
            }
            document.Reset();
            const auto view = scene.View();
            Require(view.instances.count == 3 && view.meshes.count == 4 && view.bufferGroups.count == 3 && view.joints.count == 4, "skin counts");
            Require(view.instances.data[0].nodeIndex == 1 && view.instances.data[1].nodeIndex == 2 && view.instances.data[2].nodeIndex == 5,
                "completed orphaned tree registers instances in preorder");
            Require(view.instances.data[0].meshIndex == 0 && view.instances.data[1].meshIndex == 2 && view.instances.data[2].meshIndex == 3,
                "prototype insertion leaves independent mesh and instance indices");
            Require(view.meshes.data[0].skinPrototypeIndex == 1 && view.meshes.data[3].skinPrototypeIndex == 1 && view.meshes.data[1].isSkinPrototype,
                "one shared skin prototype and two independent derived meshes");
            Require(geometry.Buffer(0).indices.data == geometry.Buffer(1).indices.data && geometry.Buffer(2).indices.data == geometry.Buffer(1).indices.data &&
                geometry.Buffer(0).indexOwner == 1 && geometry.Buffer(1).indexOwner == 1 && geometry.Buffer(2).indexOwner == 1, "index allocation shared without copying");
            for (uint32_t vertex = 3; vertex < 6; ++vertex)
            {
                Require(Vertex<uint64_t>(view, geometry, 1, Attribute::JointIndices, vertex) == 0,
                    "unused static joint lanes are initialized");
                const auto weight = Vertex<gpu_contract::Float4>(view, geometry, 1, Attribute::JointWeights, vertex);
                Require(weight.x == 0 && weight.y == 0 && weight.z == 0 && weight.w == 0,
                    "unused static weight lanes are initialized");
            }
            for (uint32_t group = 0; group <= 2; group += 2)
            {
                const float relative = group == 0 ? 0.0f : -10.0f;
                const float firstJoint = relative + (mode == 3 ? 1.0f : 0.5f), secondJoint = relative + 2;
                const auto derived = geometry.Buffer(group);
                Require(derived.vertices.count == 0 && derived.vertices.data == nullptr && derived.jointMatrices.count == 2 &&
                    derived.skinInstanceIndex == group, "derived vertices require initial GPU skinning");
                for (uint32_t joint = 0; joint < 2; ++joint)
                {
                    gpu_contract::Float4x4 expected{};
                    for (uint32_t lane = 0; lane < 16; lane += 5) expected.values[lane] = 1;
                    expected.values[12] = joint == 0 ? firstJoint : secondJoint;
                    Require(memcmp(&derived.jointMatrices.data[joint], &expected, sizeof(expected)) == 0,
                        "owned initial joint matrices survive parser destruction");
                }
            }
            const float w0 = mode == 1 ? 64.0f/255 : mode == 2 ? 16384.0f/65535 : 0.25f;
            const float w1 = mode == 1 ? 191.0f/255 : mode == 2 ? 49151.0f/65535 : 0.75f;
            const auto weight = Vertex<gpu_contract::Float4>(view, geometry, 1, Attribute::JointWeights, 0);
            Require(weight.x == w0 && weight.y == w1 && weight.z == 0 && weight.w == 0, "source skin weights preserve normalized conversion");
            for (uint32_t i = 0; i < 4; ++i) Require(view.joints.data[i].nodeIndex == 3 + (i % 2), "joint references survive parser destruction");
            Require(view.joints.data[0].inverseBind.values[12] == (mode == 3 ? 0 : -0.5f), "authored or implicit inverse bind");
        }

        Fixture fixture;
        fixture.Add(triangle, sizeof(triangle), 3, 5126, "VEC3");
        const float firstMorph[]{-4,0,0, -3,0,0, -4,1,0}, secondMorph[]{7,0,0, 8,0,0, 7,1,0};
        fixture.Add(firstMorph, sizeof(firstMorph), 3, 5126, "VEC3");
        fixture.Add(secondMorph, sizeof(secondMorph), 3, 5126, "VEC3");
        const float radius[]{0.25f,0.5f,0.25f}; fixture.Add(radius, sizeof(radius), 3, 5126, "SCALAR");
        ImportDocument document;
        fixture.Parse(document, R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0,"_RADIUS":3},"targets":[{"POSITION":1},{"NORMAL":1}]}]},{"primitives":[{"attributes":{"POSITION":0},"targets":[{"POSITION":2},{"NORMAL":2}]}]}],"nodes":[{"mesh":1},{"mesh":0}],"scenes":[{"nodes":[0,1]}])");
        RendererScene scene; ImportGeometry geometry;
        Good(ConvertImportScene(document, Options(), scene, geometry), "morph/radius conversion");
        const auto view = scene.View();
        Require(view.morphRanges.count == 4 && geometry.Buffer(0).morphs.count == 384, "model-wide morph frames");
        for (uint32_t i = 0; i < 4; ++i) Require(view.morphRanges.data[i].offset == i * 96 && view.morphRanges.data[i].size == 96, "morph ranges own distinct frame bytes");
        Require(Read<gpu_contract::Float4>(geometry.Buffer(0).morphs, 0).x == -4 &&
            Read<gpu_contract::Float4>(geometry.Buffer(0).morphs, 192 + 48).x == 7, "morph payload follows source mesh vertex offsets");
        for (uint32_t frame = 1; frame < 4; frame += 2)
            for (uint32_t lane = 0; lane < 24; ++lane) Require(Read<float>(geometry.Buffer(0).morphs, frame * 96 + lane * 4) == 0, "omitted position target frame zero-filled");
        Require(view.meshes.data[0].morphTargetAnimation && view.meshes.data[1].morphTargetAnimation &&
            view.meshes.data[1].objectBounds.minimum.x == -4 && view.meshes.data[1].objectBounds.maximum.z == 0.5f, "retained morph/radius import envelopes");
        Require(Vertex<float>(view, geometry, 0, Attribute::CurveRadius, 1) == 0.5f && Vertex<float>(view, geometry, 0, Attribute::CurveRadius, 3) == 0,
            "mixed radius storage is bounded and absent lanes are zero");
    }

    void RejectJson(const char* json, ImportError error, const char* reason)
    {
        ImportDocument document;
        Good(document.Parse({reinterpret_cast<const uint8_t*>(json), strlen(json)}), "parse rejection fixture");
        RendererScene scene; ImportGeometry geometry;
        Bad(ConvertImportScene(document, Options(), scene, geometry), error, reason);
        Require(scene.StorageBytes() == 0 && geometry.StorageBytes() == 0, "rejection leaves both outputs empty");
    }

    void MalformedGeometry()
    {
        const struct Case
        {
            const char* primitives;
            const char* nodes;
            const char* skins;
            const char* roots;
            ImportError error;
            const char* reason;
        } cases[]{
            {R"([{"attributes":{"POSITION":0},"mode":0}])", nullptr,nullptr,nullptr, ImportError::UnsupportedData,"unsupported primitive mode"},
            {R"([{"attributes":{"POSITION":0},"indices":2}])", nullptr,nullptr,nullptr, ImportError::InvalidIndex,"out-of-range primitive index"},
            {R"([{"attributes":{"POSITION":99}}])", nullptr,nullptr,nullptr, ImportError::InvalidIndex,"bad attribute accessor"},
            {R"([{"attributes":{"NORMAL":1}}])", nullptr,nullptr,nullptr, ImportError::InvalidAccessor,"missing positions"},
            {R"([{"attributes":{"POSITION":4}}])", nullptr,nullptr,nullptr, ImportError::InvalidAccessor,"wrong position shape"},
            {R"([{"attributes":{"POSITION":0,"NORMAL":1}}])", nullptr,nullptr,nullptr, ImportError::InvalidAccessor,"attribute count mismatch"},
            {R"([{"attributes":{"POSITION":0,"JOINTS_0":3}}])", nullptr,nullptr,nullptr, ImportError::InvalidAccessor,"joints without weights"},
            {R"([{"attributes":{"POSITION":0,"WEIGHTS_0":4}}])", nullptr,nullptr,nullptr, ImportError::InvalidAccessor,"weights without joints"},
            {R"([{"attributes":{"POSITION":0,"JOINTS_0":6,"WEIGHTS_0":4}}])", R"([{"mesh":0,"skin":0},{}])",R"([{"joints":[1]}])","0,1", ImportError::InvalidIndex,"joint attribute exceeds skin range"},
            {R"([{"attributes":{"POSITION":0,"JOINTS_0":3,"WEIGHTS_0":7}}])", R"([{"mesh":0,"skin":0},{}])",R"([{"joints":[1]}])","0,1", ImportError::InvalidData,"negative joint weight"},
            {R"([{"attributes":{"POSITION":0,"JOINTS_0":3,"WEIGHTS_0":4}}])", R"([{"mesh":0,"skin":0},{}])",R"([{"joints":[1],"inverseBindMatrices":0}])","0,1", ImportError::InvalidAccessor,"wrong inverse bind shape"},
            {R"([{"attributes":{"POSITION":0,"JOINTS_0":3,"WEIGHTS_0":4}}])", R"([{"mesh":0,"skin":0},{},{}])",R"([{"joints":[1,2],"inverseBindMatrices":5}])","0,1,2", ImportError::InvalidAccessor,"short inverse bind accessor"},
            {R"([{"attributes":{"POSITION":0,"JOINTS_0":3,"WEIGHTS_0":4}}])", R"([{"mesh":0,"skin":0},{}])",R"([{"joints":[1,1]}])","0,1", ImportError::InvalidHierarchy,"duplicate skin joint"},
            {R"([{"attributes":{"POSITION":0,"JOINTS_0":3,"WEIGHTS_0":4}}])", R"([{"mesh":0,"skin":0},{}])",R"([{"joints":[1]}])","0", ImportError::InvalidIndex,"joint outside selected scene"},
            {R"([{"attributes":{"POSITION":0,"JOINTS_0":3,"WEIGHTS_0":4}}])", R"([{"mesh":0,"skin":0},{}])",R"([{"joints":[1],"skeleton":99}])","0,1", ImportError::InvalidIndex,"bad skeleton reference"},
            {R"([{"attributes":{"POSITION":0}}])", R"([{"mesh":0,"skin":99}])",nullptr,nullptr, ImportError::InvalidIndex,"bad skin reference"},
            {R"([{"attributes":{"POSITION":0},"targets":[{"POSITION":0}]},{"attributes":{"POSITION":0},"targets":[{"POSITION":0},{"POSITION":0}]}])",nullptr,nullptr,nullptr,ImportError::InvalidData,"inconsistent morph target counts"},
            {R"([{"attributes":{"POSITION":0},"targets":[{"POSITION":1}]}])",nullptr,nullptr,nullptr,ImportError::InvalidAccessor,"morph vertex count mismatch"},
            {R"([{"attributes":{"POSITION":0},"targets":[{"POSITION":99}]}])",nullptr,nullptr,nullptr,ImportError::InvalidIndex,"bad morph accessor"},
            {R"([{"attributes":{"POSITION":0},"indices":0}])",nullptr,nullptr,nullptr,ImportError::InvalidAccessor,"float vector indices rejected"},
            {R"([{"attributes":{"POSITION":0,"JOINTS_0":3,"WEIGHTS_0":4}}])", R"([{"mesh":0,"skin":0}])",R"([{"joints":[99]}])",nullptr, ImportError::InvalidIndex,"bad joint node reference"},
            {R"([{"attributes":{"POSITION":0},"material":0}])",nullptr,nullptr,nullptr,ImportError::InvalidIndex,"bad material reference"},
            {R"([{"attributes":{"POSITION":0}}])", R"([{"mesh":0,"skin":0},{}])",R"([{"joints":[1]}])","0,1", ImportError::InvalidAccessor,"skin primitive requires joint attributes"},
            {R"([{"attributes":{"POSITION":0},"mode":1}])",nullptr,nullptr,nullptr,ImportError::InvalidData,"odd line index count"},
            {R"([{"attributes":{"POSITION":9},"mode":3}])",nullptr,nullptr,nullptr,ImportError::InvalidData,"short line strip"}
        };
        for (const auto& test : cases)
        {
            Fixture fixture;
            fixture.Add(triangle, sizeof(triangle), 3, 5126, "VEC3");
            fixture.Add(normals, 24, 2, 5126, "VEC3");
            const uint16_t badIndices[]{0,1,3}; fixture.Add(badIndices, sizeof(badIndices), 3, 5123, "SCALAR");
            const uint8_t joints[12]{}; fixture.Add(joints, sizeof(joints), 3, 5121, "VEC4");
            const float weights[]{1,0,0,0, 1,0,0,0, 1,0,0,0}; fixture.Add(weights, sizeof(weights), 3, 5126, "VEC4");
            const float bind[]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; fixture.Add(bind, sizeof(bind), 1, 5126, "MAT4");
            const uint8_t invalidJoints[]{3,0,0,0, 3,0,0,0, 3,0,0,0}; fixture.Add(invalidJoints, sizeof(invalidJoints), 3, 5121, "VEC4");
            const float invalidWeights[]{-1,0,0,0, -1,0,0,0, -1,0,0,0}; fixture.Add(invalidWeights, sizeof(invalidWeights), 3, 5126, "VEC4");
            fixture.Add(normals, sizeof(normals), 3, 5126, "VEC3");
            fixture.Add(triangle, 12, 1, 5126, "VEC3");
            Text body;
            body.Append(R"(,"meshes":[{"primitives":%s}],"nodes":%s,"scenes":[{"nodes":[%s]}])",
                test.primitives, test.nodes ? test.nodes : R"([{"mesh":0}])", test.roots ? test.roots : "0");
            if (test.skins) body.Append(",\"skins\":%s", test.skins);
            ImportDocument document; fixture.Parse(document, body.data);
            RendererScene scene; ImportGeometry geometry;
            Bad(ConvertImportScene(document, Options(), scene, geometry), test.error, test.reason);
            Require(!scene.StorageBytes() && !geometry.StorageBytes(), "malformed geometry has no publication");
        }
        for (uint32_t width = 0; width < 2; ++width)
        {
            Text json;
            json.Append(R"({"asset":{"version":"2.0"},"buffers":[{"uri":"indices.bin","byteLength":%u}],"bufferViews":[{"buffer":0,"byteLength":%u}],"accessors":[{"componentType":5126,"count":%u,"type":"VEC3"},{"bufferView":0,"componentType":%u,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}]})",
                width ? 6 : 3, width ? 6 : 3, width ? 65536 : 256, width ? 5123 : 5121);
            ImportDocument document;
            Good(document.Parse({reinterpret_cast<const uint8_t*>(json.data), json.count}), "parse reserved index fixture");
            const uint8_t indices8[]{0,1,255}, indices16[]{0,0,1,0,255,255};
            Good(document.SupplyBuffer(0, {width ? indices16 : indices8, width ? 6u : 3u}), "supply reserved index fixture");
            RendererScene scene; ImportGeometry geometry;
            Bad(ConvertImportScene(document, Options(), scene, geometry), ImportError::InvalidIndex, "component restart value rejected even below vertex count");
            Require(!scene.StorageBytes() && !geometry.StorageBytes(), "reserved index failure has no publication");
        }
    }

    void RejectionsAndExhaustion()
    {
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{"children":[1]},{"children":[0]}],"scenes":[{"nodes":[]}]})", ImportError::Cycle, "disconnected cycle");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{"children":[0]}],"scenes":[{"nodes":[0]}]})", ImportError::Cycle, "self cycle");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{"children":[2]},{"children":[2]},{}],"scenes":[{"nodes":[0,1]}]})", ImportError::InvalidHierarchy, "multiple parents");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{"children":[1,1]},{}],"scenes":[{"nodes":[0]}]})", ImportError::InvalidHierarchy, "duplicate child");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{}],"scenes":[{"nodes":[0,0]}]})", ImportError::InvalidHierarchy, "duplicate root");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{"children":[1]},{}],"scenes":[{"nodes":[1]}]})", ImportError::InvalidHierarchy, "root with parent");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{"children":[1]}],"scenes":[{"nodes":[0]}]})", ImportError::InvalidIndex, "invalid child index");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{}],"scenes":[{"nodes":[1]}]})", ImportError::InvalidIndex, "invalid root index");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}]})", ImportError::InvalidIndex, "invalid mesh index");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{"skin":0}],"scenes":[{"nodes":[0]}]})", ImportError::InvalidIndex, "skin without mesh");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{}]})", ImportError::InvalidIndex, "nodes without selected scene");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{}],"scenes":[{"nodes":[0]}],"scene":1})", ImportError::InvalidIndex, "invalid default scene");
        RejectJson(R"({"asset":{"version":"2.0"},"nodes":[{"matrix":[1,0,0,1,0,1,0,0,0,0,1,0,0,0,0,1]}],"scenes":[{"nodes":[0]}]})", ImportError::InvalidData, "non-affine matrix");
        const char* mixed = R"({"asset":{"version":"2.0"},"nodes":[{"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],"translation":[0,0,0]}]})";
        ImportDocument malformed;
        Bad(malformed.Parse({reinterpret_cast<const uint8_t*>(mixed), strlen(mixed)}), ImportError::InvalidData, "matrix and TRS cannot coexist");

        Fixture fixture;
        fixture.Add(triangle, sizeof(triangle), 3, 5126, "VEC3");
        fixture.Add(normals, sizeof(normals), 3, 5126, "VEC3");
        fixture.Add(uvs, sizeof(uvs), 3, 5126, "VEC2");
        ImportDocument document;
        fixture.Parse(document, R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}])");
        RendererScene scene; ImportGeometry geometry;
        ImportDocument missingBuffer;
        Good(missingBuffer.Parse({reinterpret_cast<const uint8_t*>(fixture.json.data), fixture.json.count}), "parse unresolved external geometry");
        Bad(ConvertImportScene(missingBuffer, Options(), scene, geometry), ImportError::BufferUnavailable, "geometry propagates missing source buffer");
        Require(!scene.StorageBytes() && !geometry.StorageBytes(), "unresolved geometry is not published");
        auto options = Options(); options.maxScratchBytes = 1;
        Bad(ConvertImportScene(document, options, scene, geometry), ImportError::Workspace, "explicit scratch capacity");
        options = Options(); options.maxGeometryBytes = 1;
        Bad(ConvertImportScene(document, options, scene, geometry), ImportError::Capacity, "explicit geometry capacity");
        options = Options(); options.sceneIndex = 1;
        Bad(ConvertImportScene(document, options, scene, geometry), ImportError::InvalidIndex, "explicit scene index");
        options = Options(); options.generation = 0;
        Bad(ConvertImportScene(document, options, scene, geometry), ImportError::InvalidState, "zero generation");
        options = Options(); options.modelName = {nullptr, 7};
        Bad(ConvertImportScene(document, options, scene, geometry), ImportError::InvalidInput, "invalid name borrow");
        options = Options();
        Good(ConvertImportScene(document, options, scene, geometry), "measure exact required capacities");
        const size_t scratchBytes = geometry.ConversionScratchBytes(), geometryBytes = geometry.StorageBytes();
        scene.Reset(); geometry.Reset();
        options.maxScratchBytes = scratchBytes; options.maxGeometryBytes = geometryBytes;
        Good(ConvertImportScene(document, options, scene, geometry), "exact capacity bounds succeed");
        scene.Reset(); geometry.Reset();
        --options.maxScratchBytes;
        Bad(ConvertImportScene(document, options, scene, geometry), ImportError::Workspace, "late workspace exhaustion preserves both owners");
        Require(!scene.StorageBytes() && !geometry.StorageBytes(), "late workspace failure has no publication");
        options.maxScratchBytes = scratchBytes; --options.maxGeometryBytes;
        Bad(ConvertImportScene(document, options, scene, geometry), ImportError::Capacity, "one-byte geometry capacity shortage");
        options = Options();
        for (int64_t ordinal = 0; ordinal < 3; ++ordinal)
        {
            SetImportAllocationFailureCountdown(ordinal);
            const auto result = document.Parse({reinterpret_cast<const uint8_t*>(fixture.json.data), fixture.json.count});
            SetImportAllocationFailureCountdown(-1);
            Bad(result, ImportError::OutOfMemory, "node metadata, document and buffer-table allocation faults");
            Good(ConvertImportScene(document, options, scene, geometry), "failed parse retains supplied bytes and node metadata");
            scene.Reset(); geometry.Reset();
        }
        size_t faults = 0;
        for (int64_t ordinal = 0; ordinal < 80; ++ordinal)
        {
            SetImportAllocationFailureCountdown(ordinal);
            const auto result = ConvertImportScene(document, options, scene, geometry);
            SetImportAllocationFailureCountdown(-1);
            if (result) { scene.Reset(); geometry.Reset(); break; }
            Bad(result, ImportError::OutOfMemory, "conversion allocation fault");
            Require(scene.StorageBytes() == 0 && geometry.StorageBytes() == 0, "allocation failure has no publication");
            Good(ConvertImportScene(document, options, scene, geometry), "allocation retry on same document and owners");
            Require(Vertex<uint32_t>(scene.View(), geometry, 0, Attribute::Tangent, 0) == 0x8100007f, "retry retains geometry values");
            scene.Reset(); geometry.Reset(); ++faults;
        }
        Require(faults > 12 && faults < 80, "every conversion allocation reached success");
        size_t sceneFaults = 0;
        for (uint32_t ordinal = 1; ordinal < 40; ++ordinal)
        {
            SetRendererSceneAllocationFailure(ordinal);
            const auto result = ConvertImportScene(document, options, scene, geometry);
            SetRendererSceneAllocationFailure(0);
            if (result) { scene.Reset(); geometry.Reset(); break; }
            Bad(result, ImportError::OutOfMemory, "canonical allocation fault");
            Require(scene.StorageBytes() == 0 && geometry.StorageBytes() == 0, "canonical failure has no publication");
            Good(ConvertImportScene(document, options, scene, geometry), "canonical allocation retry");
            scene.Reset(); geometry.Reset(); ++sceneFaults;
        }
        Require(sceneFaults > 8 && sceneFaults < 40, "every canonical allocation reached success");
        printf("allocation faults: conversion %zu, canonical %zu; each retried on preserved document\n", faults, sceneFaults);
    }

    DWORD WINAPI DeepAndWide(void*)
    {
        ULONG_PTR low = 0, high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        Require(high > low && high - low <= 65536, "actual conversion stack reservation");
        constexpr uint32_t count = 100000;
        for (uint32_t wide = 0; wide < 2; ++wide)
        {
            Text json(16000000);
            json.Append(R"({"asset":{"version":"2.0"},"nodes":[)");
            for (uint32_t i = 0; i < count; ++i)
            {
                json.Append("%s{\"name\":\"node%u\"", i ? "," : "", i);
                if (i % 1024 == 0) json.Append(R"(,"translation":[1,0,0])");
                if ((!wide && i + 1 < count) || (wide && i == 0))
                {
                    json.Append(R"(,"children":[)");
                    if (wide) for (uint32_t child = 1; child < count; ++child) json.Append("%s%u", child > 1 ? "," : "", child);
                    else json.Append("%u", i + 1);
                    json.Append("]");
                }
                json.Append("}");
            }
            json.Append(R"(],"scenes":[{"nodes":[0]}]})");
            ImportDocument document;
            Good(document.Parse({reinterpret_cast<const uint8_t*>(json.data), json.count}), "deep/wide parser input");
            RendererScene scene; ImportGeometry geometry;
            Good(ConvertImportScene(document, Options(), scene, geometry), "deep/wide checked conversion");
            document.Reset();
            const auto view = scene.View();
            Require(view.nodes.count == count + 1 && view.preorder.count == count + 1 && !geometry.BufferCount(), "complete graph without geometry allocations");
            for (uint32_t n = 1; n <= count; ++n)
            {
                const auto& node = view.nodes.data[n];
                const uint32_t parent = n == 1 ? 0 : (wide ? 1 : n - 1);
                const uint32_t child = n < count && (!wide || n == 1) ? n + 1 : invalid;
                const uint32_t sibling = wide && n > 1 && n < count ? n + 1 : invalid;
                const double expectedX = wide ? 1 + (n > 1 && (n - 1) % 1024 == 0 ? 1 : 0) : (n - 1) / 1024 + 1;
                Require(node.parentIndex == parent && node.firstChildIndex == child && node.nextSiblingIndex == sibling &&
                    node.preorderIndex == n && view.preorder.data[n] == n && node.subtreeEnd == (wide && n > 1 ? n + 1 : count + 1), "every hierarchy link and traversal position");
                Require(node.world.translation[0] == expectedX && node.previousWorld.translation[0] == expectedX, "deep/wide transforms and initial history");
            }
            RendererSceneTransform moved; moved.translation[0] = 2;
            Require(scene.SetTransform({view.generation, 0}, moved).Succeeded(), "deep/wide parent edit");
            const auto movedView = scene.View();
            for (uint32_t n = 1; n <= count; ++n)
                Require(movedView.nodes.data[n].world.translation[0] == movedView.nodes.data[n].previousWorld.translation[0] + 2, "deep/wide iterative edit preserves previous");
            Require(scene.AdvancePreviousTransforms().Succeeded(), "deep/wide previous commit");
            printf("import graph %s %u nodes, %llu-byte stack, %zu scratch bytes, %zu scene bytes\n",
                wide ? "width" : "depth", count, static_cast<unsigned long long>(high-low), geometry.ConversionScratchBytes(), scene.StorageBytes());
            scene.Reset(); geometry.Reset();
        }
        return 0;
    }
}

int main()
{
    OrderingAndOwnership();
    PackingAndTangents();
    MatrixTransforms();
    AttributesAndPrimitives();
    SkinsAndMorphs();
    MalformedGeometry();
    RejectionsAndExhaustion();
    HANDLE worker = CreateThread(nullptr, 65536, DeepAndWide, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    Require(worker != nullptr, "small-stack thread creation");
    Require(WaitForSingleObject(worker, 30000) == WAIT_OBJECT_0, "small-stack conversion completes");
    DWORD code = 1;
    Require(GetExitCodeThread(worker, &code) && code == 0, "small-stack conversion succeeds");
    CloseHandle(worker);
    FinishImportGeometryReference();
    printf("import geometry conversion passed; %zu explicit rejections; 96 captured matrix controls; 100000-node deep/wide graphs\n", rejected);
    return 0;
}
