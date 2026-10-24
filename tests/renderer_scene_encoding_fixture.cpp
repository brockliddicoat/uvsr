#include "renderer_scene_encoding_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    FILE* reference = nullptr;
    uint32_t counts[6]{};
    void Require(bool value, const char* reason) noexcept
    {
        if (value) return;
        fprintf(stderr, "captured scene encoding reference failed: %s\n", reason); exit(1);
    }
    void Read(void* data, size_t size) noexcept
    { Require(fread(data, 1, size, reference) == size, "complete reference field"); }
    uint32_t U32() noexcept { uint32_t value; Read(&value, 4); return value; }
    double F64() noexcept { double value; Read(&value, 8); return value; }
    void Tag(uint32_t tag) noexcept
    {
        if (!reference)
        {
            const uint32_t endian = 1;
            static_assert(sizeof(double) == 8 && sizeof(float) == 4);
            Require(*reinterpret_cast<const uint8_t*>(&endian) == 1, "little-endian fixture");
            reference = fopen("renderer_scene_encoding_fixture.bin", "rb");
            Require(reference != nullptr, "open reference");
            char magic[8]; Read(magic, sizeof(magic));
            Require(!memcmp(magic, "UVSE0001", sizeof(magic)), "reference version");
        }
        Require(U32() == tag, "reference case order"); ++counts[tag - 1];
    }
    void Transform(uvsr::RendererSceneTransform& value) noexcept
    {
        for (double& lane : value.translation) lane = F64();
        for (double& lane : value.rotation) lane = F64();
        for (double& lane : value.scaling) lane = F64();
    }
    void Bytes(void* data, size_t size) noexcept
    { Require(U32() == size, "exact ABI size"); Read(data, size); }
}

void ReadMaterialEncodingReference(uint32_t domain, uint32_t mask, uint32_t extra, MaterialConstants& expected) noexcept
{
    Tag(1);
    Require(U32() == domain && U32() == mask && U32() == extra, "material domain and feature inputs");
    Bytes(&expected, sizeof(expected));
}
void ReadLightSetupReference(uint32_t seed, uvsr::RendererSceneTransform& root, uvsr::RendererSceneTransform& parent) noexcept
{ Tag(2); Require(U32() == seed, "light setup seed"); Transform(root); Transform(parent); }
void ReadLightFrameReference(uint32_t index, uvsr::RendererSceneLightFrame& frame, LightConstants& expected) noexcept
{
    Tag(3); Require(U32() == index, "light kind order");
    for (double& lane : frame.position) lane = F64();
    for (double& lane : frame.direction) lane = F64();
    Bytes(&expected, sizeof(expected));
}
uvsr::RendererSceneTransform ReadLightTransformReference() noexcept
{ Tag(4); uvsr::RendererSceneTransform result; Transform(result); return result; }
bool ReadLightCutoffReference() noexcept
{ Tag(5); const auto value = U32(); Require(value <= 1, "finite cutoff result"); return value != 0; }
void ReadPointLightReference(LightConstants& expected) noexcept
{ Tag(6); Bytes(&expected, sizeof(expected)); }
void FinishSceneEncodingReference() noexcept
{
    Require(reference != nullptr && U32() == 7, "end marker");
    constexpr uint32_t expected[]{3072, 45, 1056, 485, 8, 1};
    for (uint32_t i = 0; i < 6; ++i)
        Require(counts[i] == expected[i] && U32() == expected[i], "complete original comparison counts");
    Require(fgetc(reference) == EOF && !ferror(reference) && fclose(reference) == 0, "exact fixture end");
    reference = nullptr;
}
