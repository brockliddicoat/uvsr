#include "import/renderer_import_private.h"

#include <fastgltf/core.hpp>
#include <simdjson.h>
#include <float.h>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace uvsr
{
    namespace
    {
        using Object = simdjson::dom::object;
        using Array = simdjson::dom::array;
        constexpr auto success = simdjson::SUCCESS;
        constexpr auto missing = simdjson::NO_SUCH_FIELD;

        ImportResult Failure(ImportError error, ImportObject object, size_t index = SIZE_MAX) noexcept
        { return {error, object, index}; }

        template<class T> bool Allocate(T*& output, size_t count) noexcept
        {
            if (!count) return true;
            output = static_cast<T*>(ImportAllocate(count * sizeof(T)));
            if (!output) return false;
            for (size_t i = 0; i < count; ++i) new (&output[i]) T{};
            return true;
        }

        bool Child(const Object& object, const char* key, Object& output, bool& present) noexcept
        {
            const auto error = object[key].get_object().get(output);
            present = error != missing;
            return error == missing || error == success;
        }

        bool Number(const Object& object, const char* key, float& output) noexcept
        {
            double value = 0;
            const auto error = object[key].get_double().get(value);
            if (error == missing) return true;
            if (error || !isfinite(value) || value < -FLT_MAX || value > FLT_MAX) return false;
            output = float(value);
            return true;
        }

        bool Vector(const Object& object, const char* key, float* output, size_t count) noexcept
        {
            Array values;
            const auto error = object[key].get_array().get(values);
            if (error == missing) return true;
            if (error || values.size() != count) return false;
            size_t i = 0;
            for (auto item : values)
            {
                double value = 0;
                if (item.get_double().get(value) || !isfinite(value) || value < -FLT_MAX || value > FLT_MAX) return false;
                output[i++] = float(value);
            }
            return true;
        }

        bool Vector3(const Object& object, const char* key, gpu_contract::Float3& output) noexcept
        {
            float values[3]{output.x, output.y, output.z};
            if (!Vector(object, key, values, 3)) return false;
            output = {values[0], values[1], values[2]};
            return true;
        }

        bool Index(const Object& object, const char* key, uint64_t& output, bool required = false) noexcept
        {
            const auto error = object[key].get_uint64().get(output);
            return error == success || (!required && error == missing);
        }

        // only a valid, already parsed JSON object reaches this scan. recognize
        // escaped ASCII keys too; never rewrite a nested key or string value.
        bool RequiredKey(ArrayView<const uint8_t> json, size_t& offset, size_t& length) noexcept
        {
            size_t depth = 0;
            bool found = false;
            for (size_t i = 0; i < json.count; ++i)
            {
                const uint8_t c = json.data[i];
                if (c == '{' || c == '[') { ++depth; continue; }
                if (c == '}' || c == ']') { --depth; continue; }
                if (c != '"') continue;
                const size_t first = i++;
                size_t matched = 0;
                bool same = true;
                constexpr char key[] = "extensionsRequired";
                while (i < json.count && json.data[i] != '"')
                {
                    uint32_t value = json.data[i++];
                    if (value == '\\')
                    {
                        if (i >= json.count) return false;
                        value = json.data[i++];
                        if (value == 'u')
                        {
                            if (json.count - i < 4) return false;
                            value = 0;
                            for (uint32_t h = 0; h < 4; ++h)
                            {
                                const uint8_t digit = json.data[i++];
                                const uint32_t nibble = digit <= '9' ? digit - '0' : (digit | 32) - 'a' + 10;
                                value = value * 16 + nibble;
                            }
                        }
                        else if (value != '"' && value != '\\' && value != '/') value = 0;
                    }
                    if (matched >= sizeof(key) - 1 || value != uint8_t(key[matched])) same = false;
                    ++matched;
                }
                if (i == json.count) return false;
                size_t next = i + 1;
                while (next < json.count && (json.data[next] == ' ' || json.data[next] == '\r' ||
                    json.data[next] == '\n' || json.data[next] == '\t')) ++next;
                if (depth == 1 && same && matched == sizeof(key) - 1 && next < json.count && json.data[next] == ':')
                {
                    if (found) return false;
                    offset = first;
                    length = i - first + 1;
                    found = true;
                }
            }
            return found;
        }

        ImportResult RequiredExtensions(const Object& root, ArrayView<const uint8_t> json, ImportMetadata& metadata) noexcept
        {
            Array names;
            const auto error = root["extensionsRequired"].get_array().get(names);
            if (error == missing) return {};
            if (error) return Failure(ImportError::InvalidData, ImportObject::Document);
            size_t count = 0;
            for (auto field : root) if (field.key == "extensionsRequired") ++count;
            if (count != 1) return Failure(ImportError::InvalidData, ImportObject::Document);
            for (auto item : names)
            {
                std::string_view name;
                if (item.get_string().get(name)) return Failure(ImportError::InvalidData, ImportObject::Document);
                if (name == "NV_materials_subsurface") { metadata.requiredSubsurface = true; continue; }
                if (name == "NV_materials_hair") { metadata.requiredHair = true; continue; }
                if (name == "NV_texture_swizzle") { metadata.requiredSwizzle = true; continue; }
                bool supported = false;
                for (const auto& extension : fastgltf::extensionStrings)
                    if (extension.first == name && (uint64_t(extension.second) & ImportParserExtensions()))
                    { metadata.requiredExtensions |= uint64_t(extension.second); supported = true; break; }
                if (!supported) return Failure(ImportError::UnsupportedExtension, ImportObject::Document);
            }
            if ((metadata.requiredSubsurface || metadata.requiredHair || metadata.requiredSwizzle) &&
                !RequiredKey(json, metadata.requiredKeyOffset, metadata.requiredKeyLength))
                return Failure(ImportError::InvalidData, ImportObject::Document);
            return {};
        }

        ImportResult TextureInfo(const Object& parent, const char* key, bool normal,
            const ImportMetadata& metadata, ImportMaterialMetadata& material, size_t materialIndex) noexcept
        {
            Object info;
            bool present = false;
            const auto invalid = Failure(ImportError::InvalidData, ImportObject::Material, materialIndex);
            if (!Child(parent, key, info, present)) return invalid;
            if (!present) return {};
            uint64_t texture = 0, texCoord = 0;
            if (!Index(info, "index", texture, true) || !Index(info, "texCoord", texCoord)) return invalid;
            if (texture >= metadata.textureCount) return Failure(ImportError::InvalidIndex, ImportObject::Texture, size_t(texture));
            if (texCoord) material.flags |= ImportMaterialIgnoredTexCoord;
            float scalar = 1;
            if (!Number(info, "scale", scalar) || !Number(info, "strength", scalar)) return invalid;
            Object extensions;
            if (!Child(info, "extensions", extensions, present)) return invalid;
            if (!present) return {};
            Object transform;
            if (!Child(extensions, "KHR_texture_transform", transform, present)) return invalid;
            if (!present) return {};
            float scale[2]{1, 1}, offset[2]{}, rotation = 0;
            uint64_t transformedCoord = texCoord;
            if (!Vector(transform, "scale", scale, 2) || !Vector(transform, "offset", offset, 2) ||
                !Number(transform, "rotation", rotation) || !Index(transform, "texCoord", transformedCoord)) return invalid;
            const bool ignored = !normal || offset[0] != 0 || offset[1] != 0 || rotation != 0 || transformedCoord != 0;
            if (ignored)
            {
                material.flags |= ImportMaterialIgnoredTransform;
                if (metadata.requiredExtensions & uint64_t(fastgltf::Extensions::KHR_texture_transform))
                    return Failure(ImportError::UnsupportedExtension, ImportObject::Material, materialIndex);
            }
            return {};
        }

#define UVSR_METADATA_TRY(expression) do { const ImportResult result = (expression); if (!result) return result; } while (false)

        ImportResult MaterialMetadata(const Object& source, ImportMetadata& metadata, size_t index) noexcept
        {
            auto& material = metadata.materials[index];
            const auto invalid = Failure(ImportError::InvalidData, ImportObject::Material, index);
            float vector[4]{}, scalar = 0;
            if (!Vector(source, "emissiveFactor", vector, 3) || !Number(source, "alphaCutoff", scalar)) return invalid;
            Object object;
            bool present = false;
            if (!Child(source, "pbrMetallicRoughness", object, present)) return invalid;
            if (present)
            {
                material.flags |= ImportMaterialPbr;
                if (!Vector(object, "baseColorFactor", vector, 4) || !Number(object, "metallicFactor", scalar) ||
                    !Number(object, "roughnessFactor", scalar)) return invalid;
                UVSR_METADATA_TRY(TextureInfo(object, "baseColorTexture", false, metadata, material, index));
                UVSR_METADATA_TRY(TextureInfo(object, "metallicRoughnessTexture", false, metadata, material, index));
            }
            UVSR_METADATA_TRY(TextureInfo(source, "normalTexture", true, metadata, material, index));
            UVSR_METADATA_TRY(TextureInfo(source, "occlusionTexture", false, metadata, material, index));
            UVSR_METADATA_TRY(TextureInfo(source, "emissiveTexture", false, metadata, material, index));
            Object extensions;
            if (!Child(source, "extensions", extensions, present)) return invalid;
            if (!present) return {};
            if (!Child(extensions, "KHR_materials_pbrSpecularGlossiness", object, present)) return invalid;
            if (present)
            {
                if (!Vector(object, "diffuseFactor", vector, 4) || !Vector(object, "specularFactor", vector, 3) ||
                    !Number(object, "glossinessFactor", scalar)) return invalid;
                UVSR_METADATA_TRY(TextureInfo(object, "diffuseTexture", false, metadata, material, index));
                UVSR_METADATA_TRY(TextureInfo(object, "specularGlossinessTexture", false, metadata, material, index));
            }
            if (!Child(extensions, "KHR_materials_transmission", object, present)) return invalid;
            if (present)
            {
                if (!Number(object, "transmissionFactor", scalar)) return invalid;
                UVSR_METADATA_TRY(TextureInfo(object, "transmissionTexture", false, metadata, material, index));
            }
            if (!Child(extensions, "KHR_materials_emissive_strength", object, present)) return invalid;
            if (present)
            {
                material.flags |= ImportMaterialEmissiveStrength;
                if (!Number(object, "emissiveStrength", scalar)) return invalid;
            }
            if (!Child(extensions, "NV_materials_subsurface", object, present)) return invalid;
            if (present)
            {
                material.flags |= ImportMaterialSubsurface;
                auto& s = material.subsurface;
                s = {{0, 0, 0}, {0, 0, 0}, 0, 0};
                if (!Vector3(object, "transmissionColor", s.transmissionColor) || !Vector3(object, "scatteringColor", s.scatteringColor) ||
                    !Number(object, "scale", s.scale) || !Number(object, "anisotropy", s.anisotropy)) return invalid;
                UVSR_METADATA_TRY(TextureInfo(object, "transmissionColorTexture", false, metadata, material, index));
                simdjson::dom::element ignoredTexture;
                if (metadata.requiredSubsurface && object["transmissionColorTexture"].get(ignoredTexture) == success)
                    return Failure(ImportError::UnsupportedExtension, ImportObject::Material, index);
            }
            if (!Child(extensions, "NV_materials_hair", object, present)) return invalid;
            if (present)
            {
                material.flags |= ImportMaterialHair;
                auto& h = material.hair;
                h = {{0, 0, 0}, 0, 0, 0, 0, 0, {0, 0, 0}, 0, 0};
                if (!Vector3(object, "baseColor", h.baseColor) || !Number(object, "melanin", h.melanin) ||
                    !Number(object, "melaninRedness", h.melaninRedness) || !Number(object, "longitudinalRoughness", h.longitudinalRoughness) ||
                    !Number(object, "azimuthalRoughness", h.azimuthalRoughness) || !Number(object, "diffuseReflectionWeight", h.diffuseReflectionWeight) ||
                    !Vector3(object, "diffuseReflectionTint", h.diffuseReflectionTint) || !Number(object, "ior", h.ior) ||
                    !Number(object, "cuticleAngle", h.cuticleAngle)) return invalid;
            }
            return {};
        }

        ImportResult SwizzleOptions(const Object& texture, Array& options, bool& present) noexcept
        {
            Object extensions, swizzle;
            if (!Child(texture, "extensions", extensions, present)) return Failure(ImportError::InvalidData, ImportObject::Texture);
            if (!present) return {};
            if (!Child(extensions, "NV_texture_swizzle", swizzle, present)) return Failure(ImportError::InvalidData, ImportObject::Texture);
            if (!present) return {};
            const auto error = swizzle["options"].get_array().get(options);
            present = error != missing;
            if (error != success && error != missing) return Failure(ImportError::InvalidData, ImportObject::Texture);
            return {};
        }
    }

    uint64_t ImportParserExtensions() noexcept
    {
        using E = fastgltf::Extensions;
        return uint64_t(E::KHR_materials_pbrSpecularGlossiness | E::KHR_materials_transmission |
            E::KHR_materials_emissive_strength | E::KHR_texture_transform | E::MSFT_texture_dds |
            E::KHR_mesh_quantization | E::KHR_lights_punctual);
    }

    ImportMetadata::~ImportMetadata() noexcept
    { free(nodeFlags); free(cameraNames); free(materials); free(images); free(imageMimeTypes); free(textureSwizzles); free(swizzles); }

    void ImportMetadata::TakeFrom(ImportMetadata& source) noexcept
    {
        if (this == &source) return;
        free(nodeFlags); free(cameraNames); free(materials); free(images); free(imageMimeTypes); free(textureSwizzles); free(swizzles);
        nodeFlags = source.nodeFlags; source.nodeFlags = nullptr;
        nodeCount = source.nodeCount;
        cameraNames = source.cameraNames; source.cameraNames = nullptr;
        cameraCount = source.cameraCount;
        materials = source.materials; source.materials = nullptr;
        materialCount = source.materialCount;
        images = source.images; source.images = nullptr;
        imageCount = source.imageCount;
        imageMimeTypes = source.imageMimeTypes; source.imageMimeTypes = nullptr;
        imageMimeBytes = source.imageMimeBytes;
        textureSwizzles = source.textureSwizzles; source.textureSwizzles = nullptr;
        textureCount = source.textureCount;
        swizzles = source.swizzles; source.swizzles = nullptr;
        swizzleCount = source.swizzleCount;
        requiredExtensions = source.requiredExtensions;
        requiredSubsurface = source.requiredSubsurface;
        requiredHair = source.requiredHair;
        requiredSwizzle = source.requiredSwizzle;
        requiredKeyOffset = source.requiredKeyOffset;
        requiredKeyLength = source.requiredKeyLength;
    }

    ImportResult ReadImportSceneMetadata(const Object& root, ImportMetadata& metadata) noexcept
    {
        Array cameras;
        auto error = root["cameras"].get_array().get(cameras);
        if (error != success && error != missing) return Failure(ImportError::InvalidData, ImportObject::Camera);
        if (error == success)
        {
            metadata.cameraCount = cameras.size();
            if (metadata.cameraCount > PTRDIFF_MAX) return Failure(ImportError::Overflow, ImportObject::Camera);
            if (!Allocate(metadata.cameraNames, metadata.cameraCount)) return Failure(ImportError::OutOfMemory, ImportObject::Camera);
            size_t index = 0;
            for (auto item : cameras)
            {
                Object camera, projection;
                std::string_view type, name;
                if (item.get_object().get(camera) || camera["type"].get_string().get(type))
                    return Failure(ImportError::InvalidData, ImportObject::Camera, index);
                error = camera["name"].get_string().get(name);
                if (error != success && error != missing) return Failure(ImportError::InvalidData, ImportObject::Camera, index);
                metadata.cameraNames[index] = error == success;
                const bool perspective = type == "perspective";
                if ((!perspective && type != "orthographic") || camera[perspective ? "perspective" : "orthographic"].get_object().get(projection))
                    return Failure(ImportError::InvalidData, ImportObject::Camera, index);
                const char* fields[]{"znear", "zfar", "yfov", "aspectRatio", "xmag", "ymag"};
                for (const auto* field : fields)
                {
                    float value = 0;
                    if (!Number(projection, field, value)) return Failure(ImportError::InvalidData, ImportObject::Camera, index);
                }
                ++index;
            }
        }
        Object extensions, punctual;
        bool present = false;
        if (!Child(root, "extensions", extensions, present)) return Failure(ImportError::InvalidData, ImportObject::Document);
        if (present)
        {
            if (!Child(extensions, "KHR_lights_punctual", punctual, present)) return Failure(ImportError::InvalidData, ImportObject::Light);
            if (present)
            {
                Array lights;
                if (punctual["lights"].get_array().get(lights)) return Failure(ImportError::InvalidData, ImportObject::Light);
                size_t index = 0;
                for (auto item : lights)
                {
                    Object light, spot;
                    std::string_view type;
                    float values[3]{};
                    if (item.get_object().get(light) || light["type"].get_string().get(type) ||
                        (type != "directional" && type != "point" && type != "spot") ||
                        !Number(light, "intensity", values[0]) || !Number(light, "range", values[0]) ||
                        !Vector(light, "color", values, 3) || !Child(light, "spot", spot, present))
                        return Failure(ImportError::InvalidData, ImportObject::Light, index);
                    if (present && (!Number(spot, "innerConeAngle", values[0]) || !Number(spot, "outerConeAngle", values[0])))
                        return Failure(ImportError::InvalidData, ImportObject::Light, index);
                    ++index;
                }
            }
        }
        Array animations;
        error = root["animations"].get_array().get(animations);
        if (error != success && error != missing) return Failure(ImportError::InvalidData, ImportObject::Animation);
        if (error == success)
        {
            size_t index = 0;
            for (auto item : animations)
            {
                Object animation;
                Array channels, samplers;
                if (item.get_object().get(animation) || animation["channels"].get_array().get(channels) ||
                    animation["samplers"].get_array().get(samplers)) return Failure(ImportError::InvalidData, ImportObject::Animation, index);
                for (auto samplerItem : samplers)
                {
                    Object sampler;
                    std::string_view mode;
                    if (samplerItem.get_object().get(sampler)) return Failure(ImportError::InvalidData, ImportObject::Animation, index);
                    error = sampler["interpolation"].get_string().get(mode);
                    // the pinned parser treats a non-string as the default mode.
                    if (error != missing && (error || (mode != "STEP" && mode != "LINEAR" && mode != "CUBICSPLINE")))
                        return Failure(ImportError::InvalidData, ImportObject::Animation, index);
                }
                for (auto channelItem : channels)
                {
                    std::string_view path;
                    uint64_t node = 0;
                    error = channelItem["target"]["node"].get_uint64().get(node);
                    if (error != missing && (error != success || node >= UINT32_MAX))
                        return Failure(ImportError::InvalidData, ImportObject::Animation, index);
                    if (channelItem["target"]["path"].get_string().get(path) ||
                        (path != "translation" && path != "rotation" && path != "scale" && path != "weights"))
                        return Failure(ImportError::InvalidData, ImportObject::Animation, index);
                }
                ++index;
            }
        }
        return {};
    }

    ImportResult ReadImportMaterialMetadata(const Object& root, ArrayView<const uint8_t> json, ImportMetadata& metadata) noexcept
    {
        UVSR_METADATA_TRY(RequiredExtensions(root, json, metadata));
        Array samplers;
        const auto samplerError = root["samplers"].get_array().get(samplers);
        if (samplerError != success && samplerError != missing) return Failure(ImportError::InvalidData, ImportObject::Document);
        if (samplerError == success)
        {
            size_t index = 0;
            for (auto item : samplers)
            {
                Object sampler;
                if (item.get_object().get(sampler)) return Failure(ImportError::InvalidData, ImportObject::Document, index);
                const char* keys[]{"magFilter", "minFilter", "wrapS", "wrapT"};
                for (size_t key = 0; key < 4; ++key)
                {
                    uint64_t value = 0;
                    const auto error = sampler[keys[key]].get_uint64().get(value);
                    if (error == missing) continue;
                    const bool valid = key < 2 ? value == 9728 || value == 9729 ||
                        (key == 1 && value >= 9984 && value <= 9987) : value == 33071 || value == 33648 || value == 10497;
                    if (error || !valid) return Failure(ImportError::InvalidData, ImportObject::Document, index);
                }
                ++index;
            }
        }
        Array images, textures, materials;
        auto error = root["images"].get_array().get(images);
        if (error != success && error != missing) return Failure(ImportError::InvalidData, ImportObject::Image);
        const size_t imageCount = error == success ? images.size() : 0;
        metadata.imageCount = imageCount;
        error = root["textures"].get_array().get(textures);
        if (error != success && error != missing) return Failure(ImportError::InvalidData, ImportObject::Texture);
        metadata.textureCount = error == success ? textures.size() : 0;
        error = root["materials"].get_array().get(materials);
        if (error != success && error != missing) return Failure(ImportError::InvalidData, ImportObject::Material);
        metadata.materialCount = error == success ? materials.size() : 0;
        if (metadata.materialCount >= INT32_MAX || metadata.materialCount > size_t(PTRDIFF_MAX) / sizeof(ImportMaterialMetadata) ||
            metadata.textureCount >= UINT32_MAX || metadata.textureCount > size_t(PTRDIFF_MAX) / sizeof(RendererSceneRange) ||
            imageCount >= UINT32_MAX || imageCount > size_t(PTRDIFF_MAX) / sizeof(ImportImageMetadata))
            return Failure(ImportError::Capacity, ImportObject::Document);
        if (!Allocate(metadata.materials, metadata.materialCount) || !Allocate(metadata.textureSwizzles, metadata.textureCount) ||
            !Allocate(metadata.images, imageCount))
            return Failure(ImportError::OutOfMemory, ImportObject::Document);
        if (imageCount)
        {
            size_t index = 0;
            for (auto item : images)
            {
                std::string_view name, mime;
                const auto nameError = item["name"].get_string().get(name);
                const auto mimeError = item["mimeType"].get_string().get(mime);
                if ((nameError != success && nameError != missing) || (mimeError != success && mimeError != missing))
                    return Failure(ImportError::InvalidData, ImportObject::Image, index);
                auto& image = metadata.images[index++];
                image.named = nameError == success;
                if (mimeError == success)
                {
                    if (mime.size() > UINT32_MAX - metadata.imageMimeBytes) return Failure(ImportError::Capacity, ImportObject::Image);
                    image.mimeType = {uint32_t(metadata.imageMimeBytes), uint32_t(mime.size())};
                    metadata.imageMimeBytes += mime.size();
                }
            }
            if (metadata.imageMimeBytes > PTRDIFF_MAX) return Failure(ImportError::Overflow, ImportObject::Image);
            if (!Allocate(metadata.imageMimeTypes, metadata.imageMimeBytes)) return Failure(ImportError::OutOfMemory, ImportObject::Image);
            index = 0;
            for (auto item : images)
            {
                const auto range = metadata.images[index++].mimeType;
                if (!range.length) continue;
                std::string_view mime;
                if (item["mimeType"].get_string().get(mime)) return Failure(ImportError::InvalidState, ImportObject::Image);
                memcpy(metadata.imageMimeTypes + range.offset, mime.data(), mime.size());
            }
        }
        if (metadata.textureCount)
        {
            size_t index = 0;
            for (auto item : textures)
            {
                Object texture;
                if (item.get_object().get(texture)) return Failure(ImportError::InvalidData, ImportObject::Texture, index);
                Object extensions, dds;
                bool present = false;
                if (!Child(texture, "extensions", extensions, present)) return Failure(ImportError::InvalidData, ImportObject::Texture, index);
                if (present)
                {
                    if (!Child(extensions, "MSFT_texture_dds", dds, present)) return Failure(ImportError::InvalidData, ImportObject::Texture, index);
                    uint64_t image = 0;
                    if (present && (!Index(dds, "source", image, true) || image >= imageCount))
                        return Failure(ImportError::InvalidIndex, ImportObject::Image, size_t(image));
                }
                Array options;
                UVSR_METADATA_TRY(SwizzleOptions(texture, options, present));
                const size_t count = present ? options.size() : 0;
                if (count > UINT32_MAX - metadata.swizzleCount) return Failure(ImportError::Capacity, ImportObject::Texture, index);
                metadata.textureSwizzles[index++] = {uint32_t(metadata.swizzleCount), uint32_t(count)};
                metadata.swizzleCount += count;
            }
            if (metadata.swizzleCount > size_t(PTRDIFF_MAX) / sizeof(ImportSwizzleMetadata))
                return Failure(ImportError::Overflow, ImportObject::Texture);
            if (!Allocate(metadata.swizzles, metadata.swizzleCount)) return Failure(ImportError::OutOfMemory, ImportObject::Texture);
            size_t swizzleIndex = 0;
            for (auto item : textures)
            {
                Object texture;
                if (item.get_object().get(texture)) return Failure(ImportError::InvalidData, ImportObject::Texture);
                Array options;
                bool present = false;
                UVSR_METADATA_TRY(SwizzleOptions(texture, options, present));
                if (!present) continue;
                for (auto option : options)
                {
                    Object object;
                    uint64_t image = 0;
                    Array channels;
                    if (option.get_object().get(object) || !Index(object, "source", image, true) || image >= imageCount ||
                        object["channels"].get_array().get(channels) || channels.size() > 4)
                        return Failure(ImportError::InvalidData, ImportObject::Texture, swizzleIndex);
                    auto& swizzle = metadata.swizzles[swizzleIndex++];
                    swizzle.image = uint32_t(image);
                    for (auto channel : channels)
                    {
                        int64_t value = 0;
                        if (channel.get_int64().get(value) || value < -1 || value > INT32_MAX)
                            return Failure(ImportError::InvalidData, ImportObject::Texture, swizzleIndex - 1);
                        swizzle.channels[swizzle.channelCount++] = int32_t(value);
                    }
                }
            }
        }
        if (metadata.materialCount)
        {
            size_t index = 0;
            for (auto item : materials)
            {
                Object material;
                if (item.get_object().get(material)) return Failure(ImportError::InvalidData, ImportObject::Material, index);
                UVSR_METADATA_TRY(MaterialMetadata(material, metadata, index++));
            }
        }
        return {};
    }

#undef UVSR_METADATA_TRY
}
