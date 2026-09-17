#include "import_material_fixture.h"
#include "renderer_import_image.h"
#include "renderer_scene_encoding.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;
    FILE* reference = nullptr;
    uint32_t fixtureCount = 0;

    void Require(bool value, const char* reason) noexcept
    {
        if (value) return;
        fprintf(stderr, "captured material fixture %u failed: %s\n", fixtureCount, reason);
        exit(1);
    }
    void Read(void* data, size_t count) noexcept
    { Require(fread(data, 1, count, reference) == count, "complete reference field"); }
    uint32_t U32() noexcept { uint32_t value; Read(&value, 4); return value; }
    bool Flag() noexcept { const uint32_t value = U32(); Require(value <= 1, "reference boolean"); return value != 0; }
    void Scalar(float& value) noexcept { Read(&value, 4); }
    void Scalar(bool& value) noexcept { value = Flag(); }
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
    void MatchText(const RendererSceneView& scene, RendererSceneString value) noexcept
    { const auto text = RendererSceneText(scene, value); MatchSpan(text.data, text.count, "owned material text"); }
    void MatchPath(ArrayView<const char> path, bool embedded, bool reportTail) noexcept
    {
        char expected[4096];
        const size_t rawCount = U32();
        Require(rawCount <= sizeof(expected), "reference path capacity"); Read(expected, rawCount);
        const auto* terminator = embedded ? nullptr : static_cast<const char*>(memchr(expected, 0, rawCount));
        const size_t count = terminator ? size_t(terminator - expected) : rawCount;
        Require(path.count == count && (!count || !memcmp(path.data, expected, count)), "owned texture path");
        if (reportTail && rawCount != count)
            printf("retained path tail excluded: native %zu bytes, filename %zu bytes\n", rawCount, count);
    }
    void ReadValues(RendererSceneMaterialValues& source) noexcept
    {
        source.domain = static_cast<RendererMaterialDomain>(U32());
#define UVSR_SAME(field) Scalar(source.field)
#define UVSR_COLOR(field) Scalar(source.field.x); Scalar(source.field.y); Scalar(source.field.z)
        UVSR_COLOR(baseOrDiffuseColor); UVSR_COLOR(specularColor); UVSR_COLOR(emissiveColor);
        UVSR_SAME(emissiveIntensity); UVSR_SAME(metalness); UVSR_SAME(roughness); UVSR_SAME(opacity);
        UVSR_SAME(alphaCutoff); UVSR_SAME(transmissionFactor); UVSR_SAME(normalTextureScale); UVSR_SAME(occlusionStrength);
        UVSR_SAME(useSpecularGlossModel); UVSR_SAME(enableSubsurfaceScattering); UVSR_SAME(enableHair); UVSR_SAME(doubleSided);
        UVSR_COLOR(subsurface.transmissionColor); UVSR_COLOR(subsurface.scatteringColor);
        UVSR_SAME(subsurface.scale); UVSR_SAME(subsurface.anisotropy);
        UVSR_COLOR(hair.baseColor); UVSR_COLOR(hair.diffuseReflectionTint);
        UVSR_SAME(hair.melanin); UVSR_SAME(hair.melaninRedness); UVSR_SAME(hair.longitudinalRoughness);
        UVSR_SAME(hair.azimuthalRoughness); UVSR_SAME(hair.diffuseReflectionWeight); UVSR_SAME(hair.ior); UVSR_SAME(hair.cuticleAngle);
        UVSR_SAME(enableBaseOrDiffuseTexture); UVSR_SAME(enableMetalRoughOrSpecularTexture);
        UVSR_SAME(enableNormalTexture); UVSR_SAME(enableEmissiveTexture); UVSR_SAME(enableOcclusionTexture);
        UVSR_SAME(enableTransmissionTexture); UVSR_SAME(enableOpacityTexture); UVSR_SAME(metalnessInRedChannel);
        Scalar(source.normalTextureTransformScale.x); Scalar(source.normalTextureTransformScale.y);
#undef UVSR_COLOR
#undef UVSR_SAME

    }
    void CompareValues(const RendererSceneMaterialValues& value, const RendererSceneMaterialValues& source, MaterialConstants control, uint32_t selection, size_t textureCount)
    {
#define UVSR_SAME(field) Require(value.field == source.field, #field)
#define UVSR_COLOR(field) Require(value.field.x == source.field.x && value.field.y == source.field.y && value.field.z == source.field.z, #field)
        Require(uint32_t(value.domain) == uint32_t(source.domain), "material domain");
        UVSR_COLOR(baseOrDiffuseColor); UVSR_COLOR(specularColor); UVSR_COLOR(emissiveColor);
        UVSR_SAME(emissiveIntensity); UVSR_SAME(metalness); UVSR_SAME(roughness); UVSR_SAME(opacity);
        UVSR_SAME(alphaCutoff); UVSR_SAME(transmissionFactor); UVSR_SAME(normalTextureScale); UVSR_SAME(occlusionStrength);
        UVSR_SAME(useSpecularGlossModel); UVSR_SAME(enableSubsurfaceScattering); UVSR_SAME(enableHair); UVSR_SAME(doubleSided);
        UVSR_COLOR(subsurface.transmissionColor); UVSR_COLOR(subsurface.scatteringColor);
        UVSR_SAME(subsurface.scale); UVSR_SAME(subsurface.anisotropy);
        UVSR_COLOR(hair.baseColor); UVSR_COLOR(hair.diffuseReflectionTint);
        UVSR_SAME(hair.melanin); UVSR_SAME(hair.melaninRedness); UVSR_SAME(hair.longitudinalRoughness);
        UVSR_SAME(hair.azimuthalRoughness); UVSR_SAME(hair.diffuseReflectionWeight); UVSR_SAME(hair.ior); UVSR_SAME(hair.cuticleAngle);
        UVSR_SAME(enableBaseOrDiffuseTexture); UVSR_SAME(enableMetalRoughOrSpecularTexture);
        UVSR_SAME(enableNormalTexture); UVSR_SAME(enableEmissiveTexture); UVSR_SAME(enableOcclusionTexture);
        UVSR_SAME(enableTransmissionTexture); UVSR_SAME(enableOpacityTexture); UVSR_SAME(metalnessInRedChannel);
        Require(value.normalTextureTransformScale.x == source.normalTextureTransformScale.x &&
            value.normalTextureTransformScale.y == source.normalTextureTransformScale.y, "normal transform scale");
#undef UVSR_COLOR
#undef UVSR_SAME
        MaterialConstants candidate{};
        // native IDs are assigned by a pointer-keyed unordered map. compare the
        // same material in the candidate's import-local picking namespace.
        Require(control.materialID == 0, "captured picking normalization");
        control.materialID = int32_t(selection);
        int32_t descriptors[64];
        for (auto& descriptor : descriptors) descriptor = -1;
        Require(textureCount <= 64, "descriptor fixture capacity");
        Require(EncodeRendererSceneMaterial(value, int32_t(selection), {descriptors, textureCount}, 0, candidate).Succeeded(), "material encoding");
        Require(memcmp(&control, &candidate, sizeof(control)) == 0, "captured material constant bytes for raster and ray tables");
    }

}

void BeginImportMaterialReference(uvsr::ArrayView<const char> json, uvsr::ArrayView<const uint8_t> bytes,
    uvsr::ArrayView<const ImportMaterialFixtureFile> files, const char* model) noexcept
{
    if (!reference)
    {
        const uint32_t endian = 1;
        Require(*reinterpret_cast<const uint8_t*>(&endian) == 1 && sizeof(float) == 4, "little-endian fixture");
        reference = fopen("import_material_fixture.bin", "rb"); Require(reference != nullptr, "open reference");
        MatchBytes("UVIM0001", 8, "reference version");
        Require(U32() == 14 && U32() == sizeof(MaterialConstants), "reference shape");
    }
    Require(fixtureCount < 14 && U32() == ++fixtureCount, "reference case order");
    MatchSpan(json.data, json.count, "original material input"); MatchSpan(bytes.data, bytes.count, "original geometry input");
    MatchSpan(model, strlen(model), "original model path");
    Require(U32() == files.count, "original fixture file count");
    for (size_t i = 0; i < files.count; ++i)
    {
        const auto& file = files.data[i];
        MatchSpan(file.path, strlen(file.path), "original fixture file path");
        MatchSpan(file.bytes.data, file.bytes.count, "original fixture file bytes");
    }
}

size_t CompareImportMaterialReference(const uvsr::RendererSceneView& view, uvsr::ImportTextures& imageOwner,
    void* fileContext, ReadImportMaterialFixtureFile read) noexcept
{
    const uint32_t count = U32();
    Require(count == view.materials.count && count <= 64, "exact material count and fixture capacity");
    Require(view.textures.count <= 64 && view.textures.count == imageOwner.TextureCount(), "fixture texture map capacity");
    bool found[64]{};
    uint32_t textureMap[64];
    for (auto& index : textureMap) index = InvalidSceneIndex;
    for (uint32_t i = 0; i < count; ++i)
    {
        const int32_t sourceIndex = int32_t(U32());
        size_t index = 0;
        while (index < view.materials.count && view.materials.data[index].materialIndexInModel != sourceIndex) ++index;
        Require(index < view.materials.count && !found[index], "one-to-one source material identity"); found[index] = true;
        const auto& material = view.materials.data[index];
        Require(material.selectionId == index && FindRendererSceneMaterialSelection(view, material.selectionId).index == index,
            "candidate selection identity resolves to its source material");
        MatchText(view, material.name); MatchText(view, material.modelFileName);
        RendererSceneMaterialValues expected;
        ReadValues(expected);
        MaterialConstants constants{}; Read(&constants, sizeof(constants));
        CompareValues(material.values, expected, constants, material.selectionId, view.textures.count);
        CompareValues(material.originalValues, expected, constants, material.selectionId, view.textures.count);
        for (uint32_t slot = 0; slot < 7; ++slot)
        {
            const uint32_t expectedTexture = U32(), texture = material.values.textures[slot];
            Require((texture == InvalidSceneIndex) == (expectedTexture == InvalidSceneIndex), "texture slot presence");
            if (texture == InvalidSceneIndex) continue;
            Require(texture < view.textures.count && expectedTexture < 64, "texture reference range");
            if (textureMap[expectedTexture] != InvalidSceneIndex)
                Require(textureMap[expectedTexture] == texture, "shared texture identity");
            else
            {
                for (const auto used : textureMap) Require(used != texture, "distinct texture identity");
                textureMap[expectedTexture] = texture;
            }
        }
    }
    Require(U32() == view.geometries.count, "geometry material reference count");
    for (size_t p = 0; p < view.geometries.count; ++p)
    {
        Require(view.geometries.data[p].materialIndex < view.materials.count, "geometry material reference range");
        Require(view.materials.data[view.geometries.data[p].materialIndex].materialIndexInModel == int32_t(U32()), "first-use material reference");
    }
    ImportTextures moved(static_cast<ImportTextures&&>(imageOwner));
    Require(!imageOwner.StorageBytes() && moved.TextureCount() == view.textures.count, "texture owner move");
    Require(U32() == view.textures.count, "exact texture count");
    for (size_t i = 0; i < view.textures.count; ++i)
    {
        const size_t index = textureMap[i];
        Require(index < view.textures.count, "all canonical textures have consumers");
        const auto texture = moved.Texture(index);
        const bool embedded = texture.imageIndex != InvalidSceneIndex && moved.Image(texture.imageIndex).embedded;
        MatchPath(RendererSceneText(view, view.textures.data[index].path), embedded, true);
        MatchText(view, view.textures.data[index].mimeType);
        const bool primary = Flag();
        Require(primary == (texture.imageIndex != InvalidSceneIndex), "primary image presence");
        if (primary)
        {
            auto image = moved.Image(texture.imageIndex);
            Require(texture.forceSRGB == Flag(), "first-use image color space");
            const auto format = static_cast<ImportImageFormat>(U32());
            const uint32_t width = U32(), height = U32(), bits = U32(), alpha = U32();
            const bool present = Flag();
            if (!image.embedded) image.bytes = read(fileContext, image.path);
            ImportDecodedImage decoded;
            ImportImageDecodeOptions options; options.forceSRGB = texture.forceSRGB;
            const auto result = decoded.Decode(image, options);
            Require(bool(result) == present && bool(decoded.Bytes().count) == present, "decoded data presence");
            if (present)
            {
                const auto info = decoded.Info();
                Require(info.format == format && info.width == width && info.height == height &&
                    info.originalBitsPerPixel == bits && uint32_t(info.alpha) == alpha, "decoded texture interpretation");
                Require(U32() == info.arraySize, "decoded array count");
                size_t subresource = 0;
                for (uint32_t a = 0; a < info.arraySize; ++a)
                {
                    Require(U32() == info.mipLevels, "decoded mip count");
                    for (uint32_t m = 0; m < info.mipLevels; ++m)
                    {
                        Require(subresource < decoded.Subresources().count, "decoded subresource range");
                        const auto& layout = decoded.Subresources().data[subresource++];
                        Require(U32() == layout.rowPitch && U32() == layout.size, "decoded subresource layout");
                        const auto pixels = decoded.Bytes();
                        Require(layout.offset <= pixels.count && layout.size <= pixels.count - layout.offset, "decoded pixel range");
                        MatchBytes(pixels.data + layout.offset, layout.size, "decoded pixel bytes");
                    }
                }
                Require(subresource == decoded.Subresources().count, "complete decoded subresources");
            }
            else
            {
                // the removed loader used a 1x1 descriptor for a missing encoded file.
                Require(format == ImportImageFormat::Unknown && width == 1 && height == 1 && !bits && !alpha &&
                    !image.bytes.count && !decoded.StorageBytes(), "captured missing-file descriptor");
            }
        }
        Require(U32() == texture.swizzles.count, "swizzle merge count");
        for (size_t s = 0; s < texture.swizzles.count; ++s)
        {
            const auto& swizzle = texture.swizzles.data[s];
            const auto image = moved.Image(swizzle.imageIndex);
            Require(image.embedded == Flag(), "swizzle source kind");
            MatchPath(image.path, image.embedded, false);
            if (image.embedded)
            {
                const size_t expectedSize = U32();
                Require(expectedSize >= image.bytes.count && expectedSize - image.bytes.count <= 1, "owned swizzle image length");
                MatchBytes(image.bytes.data, image.bytes.count, "owned swizzle image bytes");
                if (expectedSize != image.bytes.count) { uint8_t padding; Read(&padding, 1); }
            }
            Require(U32() == swizzle.channelCount, "swizzle channels");
            for (uint32_t c = 0; c < swizzle.channelCount; ++c)
                Require(int32_t(U32()) == swizzle.channels[c], "swizzle channel value");
        }
    }
    printf("captured material fixture passed: %zu materials, %zu geometries, %zu textures, %zu image sources\n",
        view.materials.count, view.geometries.count, view.textures.count, moved.ImageCount());
    return 2 * count;
}

void FinishImportMaterialReference() noexcept
{
    Require(reference && fixtureCount == 14, "all material fixtures consumed");
    MatchBytes("UVIMEND1", 8, "reference footer");
    Require(fgetc(reference) == EOF && !ferror(reference), "exact reference length");
    Require(fclose(reference) == 0, "close reference"); reference = nullptr;
}
