#include "import_geometry_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;
    FILE* input = nullptr;
    unsigned comparisons = 0;

    void Require(bool condition, const char* reason)
    {
        if (condition) return;
        fprintf(stderr, "captured import comparison %u failed: %s\n", comparisons, reason);
        exit(1);
    }

    template<class T> void Read(T& value)
    {
        Require(fread(&value, 1, sizeof(value), input) == sizeof(value), "complete reference field");
    }

    uint32_t Integer()
    {
        uint32_t value;
        Read(value);
        return value;
    }

    bool Flag()
    {
        const uint32_t value = Integer();
        Require(value <= 1, "reference boolean");
        return value != 0;
    }

    void MatchBytes(const void* data, size_t count, const char* reason)
    {
        unsigned char expected[256];
        const auto* actual = static_cast<const unsigned char*>(data);
        while (count)
        {
            const size_t size = count < sizeof(expected) ? count : sizeof(expected);
            Require(fread(expected, 1, size, input) == size, "complete reference bytes");
            Require(memcmp(expected, actual, size) == 0, reason);
            actual += size;
            count -= size;
        }
    }

    void MatchSpan(const void* data, size_t count, const char* reason)
    {
        Require(Integer() == count, reason);
        MatchBytes(data, count, reason);
    }

    void MatchBounds(const RendererSceneBounds& bounds)
    {
        const bool empty = Flag();
        Require(empty == bounds.empty, "captured empty bounds");
        if (empty) return;
        float values[6];
        Read(values);
        Require(values[0] == bounds.minimum.x && values[1] == bounds.minimum.y && values[2] == bounds.minimum.z &&
            values[3] == bounds.maximum.x && values[4] == bounds.maximum.y && values[5] == bounds.maximum.z,
            "captured geometry envelope");
    }

    void MatchAttribute(RendererSceneVertexAttribute attribute, const RendererSceneBufferGroup& layout, ImportGeometryBufferView bytes)
    {
        if (!Flag()) return;
        const size_t offset = Integer(), size = Integer();
        const auto range = layout.attributes[uint32_t(attribute)];
        Require(offset <= range.size && size <= range.size - offset && range.offset <= bytes.vertices.count &&
            range.size <= bytes.vertices.count - size_t(range.offset), "captured attribute range");
        MatchBytes(bytes.vertices.data + range.offset + offset, size, "captured attribute bytes");
    }
}

void CompareImportGeometryReference(uvsr::ArrayView<const char> json, uvsr::ArrayView<const uint8_t> bytes,
    const uvsr::RendererSceneView& scene, const uvsr::ImportGeometry& geometry) noexcept
{
    if (!input)
    {
        // fixed test data from the independent control, described in import_geometry_fixture.md.
        const uint32_t endian = 1;
        static_assert(sizeof(float) == 4 && sizeof(double) == 8);
        Require(*reinterpret_cast<const unsigned char*>(&endian) == 1, "little-endian fixture");
        input = fopen("import_geometry_fixture.bin", "rb");
        Require(input != nullptr, "open reference");
        MatchBytes("UVIG0001", 8, "reference version");
        Require(Integer() == 9, "reference case count");
    }
    ++comparisons;
    Require(comparisons <= 9, "reference case coverage");
    MatchSpan(json.data, json.count, "original JSON input");
    MatchSpan(bytes.data, bytes.count, "original buffer input");
    const uint32_t nodeCount = Integer(), meshCount = Integer(), instanceCount = Integer();
    Require(nodeCount <= 128 && meshCount <= 64 && nodeCount == scene.nodes.count && meshCount == scene.meshes.count &&
        instanceCount == scene.instances.count, "captured canonical table counts");
    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        const auto& node = scene.nodes.data[i];
        const auto name = RendererSceneText(scene, node.name);
        MatchSpan(name.data, name.count, "captured node name");
        const uint32_t parent = Integer();
        const bool authored = Flag();
        Require(node.parentIndex == parent && node.hasLocalTransform == authored, "captured parent and authored transform flag");
        double world[12];
        Read(world);
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 3; ++column)
                Require(node.world.linear[row * 3 + column] == world[row * 3 + column], "captured current world linear values");
            Require(node.world.translation[row] == world[9 + row], "captured current world translation");
        }
    }
    for (uint32_t i = 0; i < instanceCount; ++i)
    {
        const auto& instance = scene.instances.data[i];
        const uint32_t node = Integer(), mesh = Integer(), jointCount = Integer();
        Require(instance.nodeIndex == node && instance.meshIndex == mesh, "captured instance registration order");
        Require(instance.joints.count == jointCount && instance.joints.first <= scene.joints.count &&
            jointCount <= scene.joints.count - instance.joints.first, "captured joint count and range");
        for (uint32_t j = 0; j < jointCount; ++j)
        {
            const auto& joint = scene.joints.data[instance.joints.first + j];
            Require(joint.nodeIndex == Integer(), "captured joint reference");
            MatchBytes(joint.inverseBind.values, 64, "captured inverse bind bytes");
        }
    }
    for (uint32_t i = 0; i < meshCount; ++i)
    {
        const auto& mesh = scene.meshes.data[i];
        const uint32_t vertexOffset = Integer(), indexOffset = Integer(), vertexCount = Integer(), indexCount = Integer(), primitiveCount = Integer();
        Require(mesh.vertexOffset == vertexOffset && mesh.indexOffset == indexOffset && mesh.vertexCount == vertexCount &&
            mesh.indexCount == indexCount && mesh.geometries.count == primitiveCount, "captured mesh offsets and counts");
        const uint32_t prototype = Integer();
        const bool isPrototype = Flag(), morph = Flag();
        Require(mesh.skinPrototypeIndex == prototype && mesh.isSkinPrototype == isPrototype && mesh.morphTargetAnimation == morph,
            "captured deformation flags");
        MatchBounds(mesh.objectBounds);
        Require(mesh.geometries.first <= scene.geometries.count && primitiveCount <= scene.geometries.count - mesh.geometries.first,
            "captured primitive range");
        for (uint32_t p = 0; p < primitiveCount; ++p)
        {
            const auto& primitive = scene.geometries.data[mesh.geometries.first + p];
            const uint32_t vertex = Integer(), index = Integer(), vertices = Integer(), indices = Integer(), type = Integer();
            Require(primitive.vertexOffsetInMesh == vertex && primitive.indexOffsetInMesh == index &&
                primitive.vertexCount == vertices && primitive.indexCount == indices && uint32_t(primitive.primitive) == type,
                "captured primitive layout");
            MatchBounds(primitive.objectBounds);
        }
        if (!Flag()) continue;
        Require(mesh.bufferGroupIndex < scene.bufferGroups.count, "captured buffer index");
        const auto data = geometry.Buffer(mesh.bufferGroupIndex);
        const auto& layout = scene.bufferGroups.data[mesh.bufferGroupIndex];
        if (isPrototype)
        {
            // the control excludes the old loader's uninitialized static joint/weight lanes.
            MatchAttribute(RendererSceneVertexAttribute::JointIndices, layout, data);
            MatchAttribute(RendererSceneVertexAttribute::JointWeights, layout, data);
        }
        if (!Flag()) continue;
        MatchSpan(data.indices.data, data.indices.count, "captured index bytes");
        MatchAttribute(RendererSceneVertexAttribute::Position, layout, data);
        MatchAttribute(RendererSceneVertexAttribute::Normal, layout, data);
        MatchAttribute(RendererSceneVertexAttribute::Tangent, layout, data);
        MatchAttribute(RendererSceneVertexAttribute::TexCoord0, layout, data);
        MatchAttribute(RendererSceneVertexAttribute::CurveRadius, layout, data);
    }
    printf("captured glTF comparison %u passed: %u nodes, %u meshes, %zu instances\n", comparisons, nodeCount, meshCount, scene.instances.count);
}

void FinishImportGeometryReference() noexcept
{
    Require(input && comparisons == 9, "all captured comparisons consumed");
    MatchBytes("UVIGEND1", 8, "complete reference trailer");
    const bool end = fgetc(input) == EOF && !ferror(input);
    const int closed = fclose(input);
    input = nullptr;
    Require(end && closed == 0, "reference has no trailing bytes and closes");
}
