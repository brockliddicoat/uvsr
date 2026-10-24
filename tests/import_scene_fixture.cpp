#include "import_scene_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;
    FILE* reference = nullptr;
    uint32_t referenceCount = 0;
    void Require(bool value, const char* reason) noexcept
    {
        if (value) return;
        fprintf(stderr, "captured scene reference %u failed: %s\n", referenceCount, reason); exit(1);
    }
    void Read(void* data, size_t size) noexcept
    { Require(fread(data, 1, size, reference) == size, "complete reference field"); }
    uint32_t U32() noexcept { uint32_t value; Read(&value, 4); return value; }
    float F32() noexcept { float value; Read(&value, 4); return value; }
    double F64() noexcept { double value; Read(&value, 8); return value; }
    bool Flag() noexcept { const auto value = U32(); Require(value <= 1, "reference boolean"); return value != 0; }
    void MatchBytes(const void* data, size_t count, const char* reason) noexcept
    {
        uint8_t expected[256];
        const auto* actual = static_cast<const uint8_t*>(data);
        while (count)
        {
            const size_t size = count < sizeof(expected) ? count : sizeof(expected);
            Read(expected, size); Require(!memcmp(actual, expected, size), reason);
            actual += size; count -= size;
        }
    }
    void MatchSpan(const void* data, size_t count, const char* reason) noexcept
    { Require(U32() == count, reason); MatchBytes(data, count, reason); }
    void Input(ArrayView<const char> json, ArrayView<const uint8_t> bytes, uint32_t tag) noexcept
    {
        if (!reference)
        {
            const uint32_t endian = 1;
            static_assert(sizeof(float) == 4 && sizeof(double) == 8);
            Require(*reinterpret_cast<const uint8_t*>(&endian) == 1, "little-endian fixture");
            reference = fopen("import_scene_fixture.bin", "rb"); Require(reference != nullptr, "open reference");
            MatchBytes("UVIS0001", 8, "reference version"); Require(U32() == 14, "reference count");
        }
        Require(referenceCount < 14 && U32() == tag && U32() == ++referenceCount, "reference case order");
        MatchSpan(json.data, json.count, "original scene input"); MatchSpan(bytes.data, bytes.count, "original buffer input");
    }
}

uint32_t CompareImportSceneReference(uvsr::ArrayView<const char> json, uvsr::ArrayView<const uint8_t> bytes,
    const uvsr::RendererSceneView& scene, uint32_t expectedModeDefects) noexcept
{
    Input(json, bytes, 1);
    const uint32_t nodeCount = U32(), lightCount = U32(), cameraCount = U32(), animationCount = U32();
    Require(nodeCount <= 128 && nodeCount == scene.nodes.count && lightCount == scene.lights.count &&
        cameraCount == scene.cameras.count && animationCount == scene.animations.count, "captured table counts");
    uint32_t lights = 0, cameras = 0, animations = 0, samplerCount = 0, modeDefects = 0, keyComparisons = 0;
    for (uint32_t i = 0; i < nodeCount; ++i)
    {
        const auto& node = scene.nodes.data[i];
        const auto name = RendererSceneText(scene, node.name);
        MatchSpan(name.data, name.count, "node name");
        Require(node.parentIndex == U32() && node.hasLocalTransform == Flag(), "node parent and local transform presence");
        for (uint32_t r = 0; r < 3; ++r)
        {
            Require(node.world.translation[r] == F64(), "current world translation");
            for (uint32_t c = 0; c < 3; ++c) Require(node.world.linear[r * 3 + c] == F64(), "current world linear values");
        }
        const uint32_t leaf = U32();
        if (leaf == 1)
        {
            Require(lights < scene.lights.count && node.leafKind == RendererSceneLeafKind::Light && node.leafIndex == lights, "light placement order");
            const auto& record = scene.lights.data[lights++];
            Require(record.nodeIndex == i, "reciprocal light ownership");
            const auto& value = record.values;
            Require(value.color.x == F32() && value.color.y == F32() && value.color.z == F32(), "linear light color");
            Require(uint32_t(record.kind) == U32(), "light kind");
            if (record.kind == RendererSceneLightKind::Directional)
                Require(value.irradiance == F32() && value.angularSize == F32(), "directional irradiance and angular size");
            else if (record.kind == RendererSceneLightKind::Spot)
                Require(value.intensity == F32() && value.radius == F32() && value.range == F32() &&
                    value.innerAngle == F32() && value.outerAngle == F32(), "spot intensity radius range and degree cones");
            else if (record.kind == RendererSceneLightKind::Point)
                Require(value.intensity == F32() && value.radius == F32() && value.range == F32(), "point intensity radius and range");
            else Require(false, "recognized light");
        }
        else if (leaf == 2)
        {
            Require(cameras < scene.cameras.count && node.leafKind == RendererSceneLeafKind::Camera && node.leafIndex == cameras, "camera placement order");
            const auto& record = scene.cameras.data[cameras++];
            Require(record.nodeIndex == i && uint32_t(record.kind) == U32(), "reciprocal camera ownership and kind");
            if (record.kind == RendererSceneCameraKind::Perspective)
                Require(record.nearPlane == F32() && record.verticalFov == F32() && record.hasFarPlane == Flag() &&
                    record.farPlane == F32() && record.hasAspectRatio == Flag() && record.aspectRatio == F32(), "perspective optional fields");
            else if (record.kind == RendererSceneCameraKind::Orthographic)
                Require(record.nearPlane == F32() && record.farPlane == F32() && record.xMagnitude == F32() && record.yMagnitude == F32(), "orthographic extents");
            else Require(false, "recognized camera");
        }
        else if (leaf == 3)
        {
            Require(animations < scene.animations.count && node.leafKind == RendererSceneLeafKind::Animation && node.leafIndex == animations, "animation placement order");
            const auto& record = scene.animations.data[animations++];
            Require(record.nodeIndex == i && record.channels.count == U32() && record.duration == F32(), "animation channels and duration");
            Require(record.channels.first <= scene.channels.count && record.channels.count <= scene.channels.count - record.channels.first, "channel range");
            for (uint32_t c = 0; c < record.channels.count; ++c)
            {
                const auto& channel = scene.channels.data[record.channels.first + c];
                Require(channel.nodeIndex == U32() && uint32_t(channel.attribute) == U32() && channel.materialIndex == InvalidSceneIndex, "animation target identity and attribute");
                const uint32_t index = U32();
                const bool added = Flag();
                if (added)
                {
                    Require(index == samplerCount && samplerCount < 64 && samplerCount < scene.samplers.count, "sampler comparison capacity and first-use order");
                    const auto& sampler = scene.samplers.data[samplerCount++];
                    if (uint32_t(sampler.interpolation) != U32()) ++modeDefects;
                    Require(sampler.keyframes.count == U32(), "sampler key count");
                    Require(sampler.keyframes.first <= scene.keyframes.count && sampler.keyframes.count <= scene.keyframes.count - sampler.keyframes.first, "keyframe range");
                    for (uint32_t k = 0; k < sampler.keyframes.count; ++k)
                    {
                        const auto& key = scene.keyframes.data[sampler.keyframes.first + k];
                        Require(key.time == F32(), "key time");
                        MatchBytes(&key.value, sizeof(key.value), "keyframe value bytes");
                        MatchBytes(&key.inTangent, sizeof(key.inTangent), "keyframe input tangent bytes");
                        MatchBytes(&key.outTangent, sizeof(key.outTangent), "keyframe output tangent bytes");
                        ++keyComparisons;
                    }
                }
                Require(index < samplerCount && channel.samplerIndex == index, "sampler sharing and first-use order");
            }
        }
        else Require(leaf == 0, "reference leaf tag");
    }
    Require(lights == lightCount && cameras == cameraCount && animations == animationCount && samplerCount == U32() &&
        samplerCount == scene.samplers.count && modeDefects == expectedModeDefects, "captured table counts and classified mode defects");
    return keyComparisons;
}

ImportSceneDefectReference ReadImportSceneDefectReference(uvsr::ArrayView<const char> json,
    uvsr::ArrayView<const uint8_t> bytes, bool skin) noexcept
{
    Input(json, bytes, skin ? 3 : 2);
    return {U32(), U32(), U32()};
}

void FinishImportSceneReference() noexcept
{
    Require(reference && referenceCount == 14, "all scene references consumed");
    MatchBytes("UVISEND1", 8, "reference footer");
    Require(fgetc(reference) == EOF && !ferror(reference), "exact reference length");
    Require(fclose(reference) == 0, "close reference"); reference = nullptr;
}
