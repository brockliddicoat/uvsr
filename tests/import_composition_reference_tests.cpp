#include "renderer_import_composition.h"
#include "renderer_import_path.h"
#include "renderer_import_load.h"
#include "../cmake/RequireNoCppExceptions.h"

#include <Windows.h>
#include <stb_image_write.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
    using namespace uvsr;
    constexpr uint32_t invalid = InvalidSceneIndex;
    const char* currentCase = "initialization";
    size_t comparedScenes = 0, comparedNodes = 0, comparedChannels = 0, comparedTextures = 0, comparedPalettes = 0;
    size_t loadedScenes = 0, loadedTextures = 0;

    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "captured composition [%s]: %s\n", currentCase, reason);
        exit(1);
    }
    void Good(ImportResult result, const char* reason)
    {
        if (result) return;
        fprintf(stderr, "captured composition [%s]: %s: %s, object %u, index %zu\n",
            currentCase, reason, ImportErrorText(result.error), unsigned(result.object), result.index);
        exit(1);
    }
    ArrayView<const char> Text(const std::string& value) { return {value.data(), value.size()}; }
    ArrayView<const uint8_t> Bytes(const std::string& value)
    { return {reinterpret_cast<const uint8_t*>(value.data()), value.size()}; }
    std::string String(ArrayView<const char> value)
    { return value.count ? std::string(value.data, value.count) : std::string{}; }
    FILE* reference = nullptr;
    std::string physicalRoot;
    uint32_t referenceInputs = 0, referenceGraphs = 0, referencePalettes = 0;
    void Read(void* data, size_t count)
    {
        if (!reference)
        {
            const uint32_t endian = 1;
            Require(*reinterpret_cast<const uint8_t*>(&endian) == 1, "little-endian reference");
            reference = fopen("import_composition_fixture.bin", "rb");
            char magic[8];
            Require(reference && fread(magic, 1, 8, reference) == 8 && !memcmp(magic, "UVIC0001", 8), "reference version");
        }
        Require(!count || fread(data, 1, count, reference) == count, "complete reference field");
    }
    uint32_t U32() { uint32_t value; Read(&value, 4); return value; }
    float F32() { float value; Read(&value, 4); return value; }
    double F64() { double value; Read(&value, 8); return value; }
    bool Flag() { const auto value = U32(); Require(value <= 1, "reference boolean"); return value != 0; }
    void Match(const void* data, size_t count, const char* reason)
    {
        const auto* actual = static_cast<const uint8_t*>(data);
        uint8_t expected[256];
        while (count)
        {
            const auto size = count < sizeof(expected) ? count : sizeof(expected);
            Read(expected, size); Require(!memcmp(actual, expected, size), reason);
            actual += size; count -= size;
        }
    }
    void MatchSpan(const void* data, size_t count, const char* reason)
    { Require(U32() == count, reason); Match(data, count, reason); }
    std::string NormalizePhysical(std::string value)
    {
        if (!physicalRoot.empty() && value.compare(0, physicalRoot.size(), physicalRoot) == 0 &&
            (value.size() == physicalRoot.size() || value[physicalRoot.size()] == '/'))
            value.replace(0, physicalRoot.size(), "$physical");
        return value;
    }
    std::string ReferenceText()
    {
        const auto count = U32(); Require(count <= 4096, "bounded reference text");
        std::string value(count, '\0'); Read(value.data(), value.size()); return value;
    }
    void SameText(ArrayView<const char> actual, const std::string& expected, const char* reason)
    { Require(NormalizePhysical(String(actual)) == expected, reason); }
    void MatchText(ArrayView<const char> actual, const char* reason)
    { SameText(actual, ReferenceText(), reason); }
    void ReferenceInput(const std::string& path, const void* bytes, size_t count)
    {
        Require(U32() == 1, "reference input order"); MatchText(Text(path), "original fixture path");
        MatchSpan(bytes, count, "original fixture bytes"); ++referenceInputs;
    }
    void PartialReference(uint32_t expected)
    { Require(U32() == 4 && U32() == expected, "captured unavailable-source errors"); }
    void FinishReference()
    {
        Require(U32() == 0 && U32() == referenceInputs && U32() == referenceGraphs && U32() == referencePalettes,
            "complete reference sequence");
        Require(U32() == 13 && U32() == 7, "captured native relative-query and model errors");
        Require(fgetc(reference) == EOF && !ferror(reference) && fclose(reference) == 0, "exact reference end");
        reference = nullptr;
    }

    // these containers construct fixed test input. file payloads use the same
    // explicit owner and platform reader as the production loading transaction.
    struct Files
    {
        struct Entry { std::string path; std::vector<uint8_t> bytes; };
        struct Blob
        {
            ImportFileData bytes;
            bool present = false;
            explicit operator bool() const { return present; }
            const Blob* operator->() const { return this; }
            const void* data() const { return bytes.Bytes().data; }
            size_t size() const { return bytes.Bytes().count; }
        };
        std::vector<Entry> entries;
        bool physical = false;
        explicit Files(bool physicalFiles = false) : physical(physicalFiles) {}
        void Add(const std::string& path, const void* data, size_t count)
        {
            ReferenceInput(path, data, count);
            const auto* bytes = static_cast<const uint8_t*>(data);
            entries.push_back({path, std::vector<uint8_t>(bytes, bytes + count)});
        }
        void Add(const std::string& path, const std::string& data) { Add(path, data.data(), data.size()); }
        ImportResult ReadFile(ArrayView<const char> path, size_t limit, ImportFileData& output, ImportCancellation cancellation) const noexcept
        {
            if (physical)
            {
                const auto source = NativeImportFileSource();
                return source.read(source.context, path, limit, output, cancellation);
            }
            if (cancellation.IsRequested()) return {ImportError::Canceled};
            for (const auto& entry : entries)
            {
                if (entry.path != String(path)) continue;
                if (entry.bytes.size() > limit) return {ImportError::Capacity};
                ImportFileData candidate;
                const auto result = candidate.Allocate(entry.bytes.size());
                if (!result) return result;
                if (!entry.bytes.empty()) memcpy(candidate.WritableBytes().data, entry.bytes.data(), entry.bytes.size());
                output = static_cast<ImportFileData&&>(candidate); return {};
            }
            return {ImportError::FileUnavailable};
        }
        bool fileExists(const std::string& path) const
        {
            if (physical)
            {
                const auto source = NativeImportFileSource(); bool present = false;
                Good(source.exists(source.context, Text(path), present), "physical fixture existence"); return present;
            }
            for (const auto& entry : entries) if (entry.path == path) return true;
            return false;
        }
        Blob readFile(const std::string& path) const
        {
            Blob result;
            const auto status = ReadFile(Text(path), SIZE_MAX, result.bytes, {});
            if (status.error == ImportError::FileUnavailable) return result;
            Good(status, "read fixture file"); result.present = true; return result;
        }
    };
    bool Exists(void* context, ArrayView<const char> path) noexcept
    { return static_cast<Files*>(context)->fileExists(String(path)); }
    ImportResult ReadLoadFile(void* context, ArrayView<const char> path, size_t limit,
        ImportFileData& output, ImportCancellation cancellation) noexcept
    { return static_cast<Files*>(context)->ReadFile(path, limit, output, cancellation); }
    ImportResult ExistsLoadFile(void* context, ArrayView<const char> path, bool& present) noexcept
    { present = Exists(context, path); return {}; }
    ImportFileSource LoadFiles(Files& files) { return {ReadLoadFile, ExistsLoadFile, &files}; }

    std::string Resolve(ArrayView<const char> containingFile, ArrayView<const char> pathReference)
    {
        size_t count = 0;
        Good(MeasureImportPath(containingFile, pathReference, count), "measure input path");
        std::vector<char> buffer(count + 1);
        Good(ResolveImportPath(containingFile, pathReference, {buffer.data(), buffer.size()}, count), "resolve input path");
        return {buffer.data(), count};
    }
    ImportModel LoadModel(Files& files, const std::string& path)
    {
        const auto input = files.readFile(path);
        Require(bool(input), "model file exists");
        std::vector<uint8_t> caller(static_cast<const uint8_t*>(input->data()), static_cast<const uint8_t*>(input->data()) + input->size());
        ImportDocument document;
        Good(document.Parse({caller.data(), caller.size()}), "parse model");
        memset(caller.data(), 0xa5, caller.size());
        for (size_t i = 0; i < document.BufferCount(); ++i)
        {
            ImportBufferInfo info;
            Good(document.BufferInfo(i, info), "model buffer info");
            if (info.resident) continue;
            const auto bytes = files.readFile(Resolve(Text(path), info.uri));
            Require(bool(bytes), "external model buffer exists");
            std::vector<uint8_t> supplied(static_cast<const uint8_t*>(bytes->data()), static_cast<const uint8_t*>(bytes->data()) + bytes->size());
            Good(document.SupplyBuffer(i, {supplied.data(), supplied.size()}), "supply external model buffer");
            memset(supplied.data(), 0xcc, supplied.size());
        }
        ImportSceneOptions options;
        const std::string name = std::filesystem::path(path).filename().generic_string();
        options.generation = 91; options.modelName = Text(name); options.modelPath = Text(path);
        options.fileExists = Exists; options.fileContext = &files;
        ImportModel output;
        Good(ConvertImportScene(document, options, output.scene, output.geometry, &output.textures), "convert model");
        document.Reset();
        return output;
    }

    void CompareTextures(const RendererSceneView& scene, const ImportTextures& owner, Files& files,
        const ImportDecodedImages* loaded)
    {
        Require(U32() == scene.textures.count, "captured texture count");
        Require((loaded ? loaded->Images().count : owner.TextureCount()) == scene.textures.count, "canonical texture count");
        for (size_t t = 0; t < scene.textures.count; ++t)
        {
            const auto path = ReferenceText(), mime = ReferenceText();
            const bool srgb = Flag();
            const auto format = Flag() ? ImportImageFormat::SRGBA8_UNORM : ImportImageFormat::RGBA8_UNORM;
            ImportDecodedImage decoded;
            bool embedded = false;
            if (!loaded)
            {
                const auto texture = owner.Texture(t);
                Require(texture.imageIndex != invalid && texture.forceSRGB == srgb, "first request controls cross-model color space");
                auto image = owner.Image(texture.imageIndex); embedded = image.embedded;
                Files::Blob encoded;
                if (!embedded)
                {
                    encoded = files.readFile(String(image.path)); Require(bool(encoded), "external image exists");
                    image.bytes = encoded.bytes.Bytes();
                }
                Good(decoded.Decode(image, {texture.forceSRGB}), "composed image decode");
            }
            SameText(RendererSceneText(scene, scene.textures.data[t].path), embedded ? path : std::string(path.c_str()), "composed texture path");
            SameText(RendererSceneText(scene, scene.textures.data[t].mimeType), mime, "composed texture MIME");
            const auto& image = loaded ? loaded->Images().data[t] : decoded;
            const auto info = image.Info(); const auto bytes = image.Bytes(); const auto layouts = image.Subresources();
            Require(info.format == format && info.width == U32() && info.height == U32() && info.depth == U32() &&
                info.arraySize == U32() && info.mipLevels == U32() && info.originalBitsPerPixel == U32() && uint32_t(info.alpha) == U32(),
                "decoded image interpretation");
            Require(info.width == 2 && info.height == 2 && info.arraySize <= 4 && info.mipLevels <= 4 &&
                layouts.count == size_t(info.arraySize) * info.mipLevels, "positive fixture image layout");
            if (loaded)
                Require(scene.textures.data[t].alpha == info.alpha && scene.textures.data[t].originalBitsPerPixel == info.originalBitsPerPixel,
                    "loaded canonical image metadata");
            for (size_t l = 0; l < layouts.count; ++l)
            {
                const auto& layout = layouts.data[l];
                Require(layout.offset <= bytes.count && layout.size <= bytes.count - layout.offset && layout.rowPitch == U32(), "decoded subresource bounds and pitch");
                MatchSpan(bytes.data + layout.offset, layout.size, "decoded subresource size and pixel bytes");
            }
            if (loaded) { ++loadedTextures; continue; }
            const auto texture = owner.Texture(t);
            Require(texture.swizzles.count == U32(), "cross-model swizzle count");
            for (size_t s = 0; s < texture.swizzles.count; ++s)
            {
                const auto& candidate = texture.swizzles.data[s];
                const auto source = owner.Image(candidate.imageIndex);
                Require(source.embedded == Flag(), "swizzle source kind");
                const auto sourcePath = ReferenceText();
                SameText(source.path, source.embedded ? sourcePath : std::string(sourcePath.c_str()), "swizzle source identity");
                Require(candidate.channelCount == U32() && candidate.channelCount <= 4, "swizzle channel count");
                for (uint32_t c = 0; c < candidate.channelCount; ++c) Require(uint32_t(candidate.channels[c]) == U32(), "swizzle channel value");
                if (source.embedded)
                {
                    const auto count = U32();
                    Require(count >= source.bytes.count && count - source.bytes.count <= 1, "owned swizzle byte count");
                    Match(source.bytes.data, source.bytes.count, "owned cross-model swizzle bytes");
                    if (count > source.bytes.count) { uint8_t padding; Read(&padding, 1); }
                }
            }
            ++comparedTextures;
        }
    }

    void Compare(const ImportModel& output, Files& files, bool record = true, const ImportDecodedImages* decoded = nullptr)
    {
        const size_t previousChannels = comparedChannels, previousTextures = comparedTextures;
        const auto scene = output.scene.View();
        Require(scene.nodes.count <= 256 && scene.meshes.count <= 64 && scene.materials.count <= 64 &&
            scene.textures.count <= 64 && scene.samplers.count <= 64, "comparison capacities");
        Require(U32() == 2, "reference graph order"); MatchText(Text(std::string(currentCase)), "reference case");
        Require(record == Flag() && bool(decoded) == Flag(), "reference comparison mode");
        const auto nodeCount = U32();
        Require(nodeCount == scene.nodes.count && U32() == scene.instances.count && U32() == scene.meshes.count && U32() == scene.materials.count,
            "complete node and registered owner counts");
        Require(scene.preorder.count == scene.nodes.count, "complete canonical traversal");
        MatchSpan(scene.preorder.data, scene.preorder.count * sizeof(uint32_t), "original identity-binding selectors");
        bool nodes[256]{}, meshes[64]{}, materials[64]{}, textures[64]{}, samplers[64]{};
        for (size_t p = 0; p < scene.preorder.count; ++p)
        {
            const auto index = scene.preorder.data[p];
            Require(index < nodeCount && !nodes[index], "unique canonical traversal index"); nodes[index] = true;
        }
        for (size_t i = 0; i < scene.instances.count; ++i)
        {
            const auto& instance = scene.instances.data[i];
            Require(instance.nodeIndex == U32() && instance.meshIndex == U32() && instance.meshIndex < scene.meshes.count, "instance order and shared mesh identity");
            meshes[instance.meshIndex] = true;
            const auto& mesh = scene.meshes.data[instance.meshIndex];
            Require(mesh.skinPrototypeIndex == U32(), "skin prototype identity");
            if (mesh.skinPrototypeIndex != invalid)
            { Require(mesh.skinPrototypeIndex < scene.meshes.count, "prototype capacity"); meshes[mesh.skinPrototypeIndex] = true; }
            Require(instance.joints.count == U32() && instance.joints.first <= scene.joints.count &&
                instance.joints.count <= scene.joints.count - instance.joints.first, "instance joint count and range");
            for (size_t j = 0; j < instance.joints.count; ++j)
            {
                const auto& joint = scene.joints.data[instance.joints.first + j];
                Require(joint.nodeIndex == U32(), "clone joint remapping"); Match(joint.inverseBind.values, 64, "inverse bind bytes");
            }
        }
        for (uint32_t m = 0; m < scene.meshes.count; ++m)
        {
            Require(meshes[m], "every canonical mesh has a captured source owner");
            const auto& mesh = scene.meshes.data[m];
            Require(mesh.geometries.count == U32() && mesh.vertexOffset == U32() && mesh.indexOffset == U32() &&
                mesh.vertexCount == U32() && mesh.indexCount == U32(), "composed mesh layout");
            Require(mesh.geometries.first <= scene.geometries.count && mesh.geometries.count <= scene.geometries.count - mesh.geometries.first, "mesh geometry range");
            for (uint32_t p = 0; p < mesh.geometries.count; ++p)
            {
                const auto material = scene.geometries.data[mesh.geometries.first + p].materialIndex;
                Require(material == U32() && material < scene.materials.count, "shared material identity"); materials[material] = true;
            }
            const bool prototype = Flag(); Require(prototype == (mesh.skinPrototypeIndex != invalid), "mesh prototype presence");
            if (!prototype)
            {
                const auto bytes = output.geometry.Buffer(mesh.bufferGroupIndex);
                Require(mesh.bufferGroupIndex < scene.bufferGroups.count, "mesh buffer group");
                const auto range = scene.bufferGroups.data[mesh.bufferGroupIndex].attributes[uint32_t(RendererSceneVertexAttribute::Position)];
                const auto count = U32();
                Require(range.size >= count && range.offset <= bytes.vertices.count && range.size <= bytes.vertices.count - range.offset, "composed packed position range");
                Match(bytes.vertices.data + range.offset, count, "composed packed position bytes");
                MatchSpan(bytes.indices.data, bytes.indices.count, "composed index bytes");
            }
        }
        for (uint32_t m = 0; m < scene.materials.count; ++m)
        {
            Require(materials[m], "every material retains its source owner identity");
            const auto& material = scene.materials.data[m];
            MatchText(RendererSceneText(scene, material.name), "material name"); MatchText(RendererSceneText(scene, material.modelFileName), "material source file");
            Require(uint32_t(material.materialIndexInModel) == U32() && material.selectionId == m &&
                material.values.roughness == F32() && material.values.metalness == F32(), "material source index and values");
            for (const auto index : material.values.textures)
            {
                Require(index == U32(), "material texture presence and shared identity");
                if (index != invalid) { Require(index < scene.textures.count && index < 64, "texture capacity"); textures[index] = true; }
            }
        }
        for (uint32_t t = 0; t < scene.textures.count; ++t) Require(textures[t], "every texture has a consumer");
        CompareTextures(scene, output.textures, files, decoded);
        size_t lights = 0, cameras = 0, animations = 0;
        for (uint32_t n = 0; n < nodeCount; ++n)
        {
            const auto& node = scene.nodes.data[n]; MatchText(RendererSceneText(scene, node.name), "node name");
            Require(node.parentIndex == U32() && node.firstChildIndex == U32() && node.nextSiblingIndex == U32() && node.hasLocalTransform == Flag(), "node links and local transform presence");
            for (uint32_t r = 0; r < 3; ++r)
            {
                Require(node.transform.translation[r] == F64() && node.transform.scaling[r] == F64(), "inherited and overridden TRS components");
                Require(node.world.translation[r] == F64(), "composed world translation");
                for (uint32_t c = 0; c < 3; ++c) Require(node.world.linear[r * 3 + c] == F64(), "composed world linear values");
            }
            Require(!memcmp(&node.world, &node.previousWorld, sizeof(node.world)), "initial previous world");
            const auto leaf = U32();
            if (leaf == 1)
            {
                ++lights; Require(node.leafKind == RendererSceneLeafKind::Light && node.leafIndex < scene.lights.count, "light placement");
                const auto& value = scene.lights.data[node.leafIndex];
                Require(value.nodeIndex == n && value.nodeIndex == U32() && value.values.color.x == F32() &&
                    value.values.color.y == F32() && value.values.color.z == F32() && uint32_t(value.kind) == U32(), "light owner color and kind");
                if (value.kind == RendererSceneLightKind::Directional)
                    Require(value.values.irradiance == F32() && value.values.angularSize == F32(), "directional fields");
                else if (value.kind == RendererSceneLightKind::Spot)
                    Require(value.values.intensity == F32() && value.values.radius == F32() && value.values.range == F32() &&
                        value.values.innerAngle == F32() && value.values.outerAngle == F32(), "spot fields");
                else if (value.kind == RendererSceneLightKind::Point)
                    Require(value.values.intensity == F32() && value.values.radius == F32() && value.values.range == F32(), "point fields");
                else Require(false, "known light kind");
            }
            else if (leaf == 2)
            {
                ++cameras; Require(node.leafKind == RendererSceneLeafKind::Camera && node.leafIndex < scene.cameras.count, "camera placement");
                const auto& value = scene.cameras.data[node.leafIndex];
                Require(value.nodeIndex == n && value.nodeIndex == U32() && uint32_t(value.kind) == U32(), "camera owner and kind");
                if (value.kind == RendererSceneCameraKind::Perspective)
                    Require(value.nearPlane == F32() && value.verticalFov == F32() && value.hasFarPlane == Flag() && (!value.hasFarPlane || value.farPlane == F32()) &&
                        value.hasAspectRatio == Flag() && (!value.hasAspectRatio || value.aspectRatio == F32()), "perspective fields");
                else if (value.kind == RendererSceneCameraKind::Orthographic)
                    Require(value.nearPlane == F32() && value.farPlane == F32() && value.xMagnitude == F32() && value.yMagnitude == F32(), "orthographic fields");
                else Require(false, "known camera kind");
            }
            else if (leaf == 3)
            {
                ++animations; Require(node.leafKind == RendererSceneLeafKind::Animation && node.leafIndex < scene.animations.count, "animation placement");
                const auto& value = scene.animations.data[node.leafIndex];
                Require(value.nodeIndex == n && value.nodeIndex == U32() && value.channels.count == U32() && value.duration == F32(), "animation count and duration");
                Require(value.channels.first <= scene.channels.count && value.channels.count <= scene.channels.count - value.channels.first, "animation channel range");
                for (uint32_t c = 0; c < value.channels.count; ++c)
                {
                    const auto& channel = scene.channels.data[value.channels.first + c];
                    Require(channel.nodeIndex == U32() && channel.materialIndex == U32() && uint32_t(channel.attribute) == U32(), "resolved animation target identity and attribute");
                    MatchText(RendererSceneText(scene, channel.property), "animation property");
                    Require(channel.samplerIndex == U32() && channel.samplerIndex < scene.samplers.count, "sampler sharing across clones and targets");
                    samplers[channel.samplerIndex] = true;
                    const auto& sampler = scene.samplers.data[channel.samplerIndex];
                    Require(uint32_t(sampler.interpolation) == U32() && sampler.keyframes.count == U32(), "sampler mode and count");
                    Require(sampler.keyframes.first <= scene.keyframes.count && sampler.keyframes.count <= scene.keyframes.count - sampler.keyframes.first, "sampler key range");
                    for (uint32_t k = 0; k < sampler.keyframes.count; ++k)
                    {
                        const auto& key = scene.keyframes.data[sampler.keyframes.first + k]; Require(key.time == F32(), "keyframe time");
                        Match(&key.value, 16, "keyframe value bytes"); Match(&key.inTangent, 16, "keyframe input tangent bytes"); Match(&key.outTangent, 16, "keyframe output tangent bytes");
                    }
                    ++comparedChannels;
                }
            }
            else if (leaf == 4) Require(node.leafKind == RendererSceneLeafKind::Instance, "mesh leaf kind");
            else Require((leaf == 0 || leaf == 5) && node.leafKind == RendererSceneLeafKind::None, "empty leaf or joint marker");
        }
        Require(lights == scene.lights.count && cameras == scene.cameras.count && animations == scene.animations.count && U32() == scene.samplers.count, "complete leaf and sampler counts");
        for (uint32_t s = 0; s < scene.samplers.count; ++s) Require(samplers[s], "no orphan samplers");
        ++referenceGraphs;
        if (!record) { comparedChannels = previousChannels; comparedTextures = previousTextures; return; }
        ++comparedScenes; comparedNodes += nodeCount;
        printf("captured composition %zu [%s]: %zu nodes, %zu instances, %zu materials, %zu channels\n",
            comparedScenes, currentCase, size_t(nodeCount), scene.instances.count, scene.materials.count, scene.channels.count);
    }
    void ComparePalettes(const ImportModel& output)
    {
        const auto view = output.scene.View();
        for (size_t i = 0; i < view.instances.count; ++i)
        {
            const auto& instance = view.instances.data[i]; if (!instance.joints.count) continue;
            const auto source = output.geometry.Buffer(view.meshes.data[instance.meshIndex].bufferGroupIndex);
            const size_t size = source.jointMatrices.count * sizeof(gpu_contract::Float4x4);
            Require(source.skinInstanceIndex == i && source.jointMatrices.count == instance.joints.count && size && size <= 4096, "composed palette ownership");
            Require(U32() == 3, "reference palette order"); MatchText(Text(std::string(currentCase)), "palette case");
            Require(U32() == i && U32() == instance.joints.count, "captured palette instance and joint count");
            MatchSpan(source.jointMatrices.data, size, "composed final-pose palette equals captured GPU joint-buffer bytes");
            ++comparedPalettes; ++referencePalettes;
        }
    }

    ImportModel Run(Files& files,
        const char* label, const std::string& path)
    {
        currentCase = label;
        ImportModel output;
        if (std::filesystem::path(path).extension() == ".gltf" || std::filesystem::path(path).extension() == ".glb")
            output = LoadModel(files, path);
        else
        {
            auto blob = files.readFile(path);
            Require(bool(blob), "description file exists");
            std::vector<uint8_t> caller(static_cast<const uint8_t*>(blob->data()), static_cast<const uint8_t*>(blob->data()) + blob->size());
            ImportSceneDescription description;
            Good(description.Parse({caller.data(), caller.size()}, Text(path)), "parse application description");
            memset(caller.data(), 0xfe, caller.size());
            std::vector<ImportModel> models(description.ModelCount());
            for (size_t m = 0; m < models.size(); ++m) models[m] = LoadModel(files, String(description.ModelPath(m)));
            ImportCompositionOptions options; options.generation = 93;
            Good(ComposeImportScene(description, {models.data(), models.size()}, options, output.scene, output.geometry, output.textures), "compose application scene");
            description.Reset();
            for (const auto& model : models)
                Require(!model.scene.StorageBytes() && !model.geometry.StorageBytes() && !model.textures.StorageBytes(), "input owners consumed");
        }
        Compare(output, files);
        ImportLoadedScene loaded;
        ImportSceneLoadOptions loadOptions; loadOptions.generation = 95;
        Good(LoadImportScene(LoadFiles(files), Text(path), loadOptions, loaded), "complete CPU loading transaction");
        Require(loaded.progress.state == ImportLoadState::Ready && !loaded.progress.objectsUnavailable, "positive loading transaction ready");
        ImportModel loadedModel;
        loadedModel.scene = static_cast<RendererScene&&>(loaded.scene);
        loadedModel.geometry = static_cast<ImportGeometry&&>(loaded.geometry);
        Compare(loadedModel, files, false, &loaded.images); ++loadedScenes;
        ComparePalettes(output);
        return output;
    }

    std::string Model(const char* material = "M", const char* extra = "", const char* buffer = "positions.bin")
    {
        return std::string(R"({"asset":{"version":"2.0"},"scene":0,"buffers":[{"uri":")") + buffer +
            R"(","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"type":"VEC3","count":3}],"materials":[{"name":")" + material +
            R"("}],"meshes":[{"name":"triangle","primitives":[{"attributes":{"POSITION":0},"material":0}]}],"nodes":[{"name":"mesh","mesh":0,"children":[1]},{"name":"joint","translation":[0,1,0]}],"scenes":[{"nodes":[0]}])" + extra + "}";
    }
    void Replace(std::string& value, const std::string& before, const std::string& after)
    {
        const size_t index = value.find(before);
        Require(index != std::string::npos, "fixture replacement exists");
        value.replace(index, before.size(), after);
    }
    std::string TexturedModel(const std::string& materials, const std::string& extra)
    {
        auto result = Model("M", extra.c_str());
        Replace(result, R"("materials":[{"name":"M"}])", "\"materials\":" + materials);
        return result;
    }
    std::vector<uint8_t> Png()
    {
        const uint8_t pixels[]{31,61,127,0, 255,13,29,64, 7,211,83,128, 91,151,253,255};
        std::vector<uint8_t> bytes;
        const auto write = [](void* context, void* input, int count)
        {
            Require(count >= 0, "PNG write count");
            auto& output = *static_cast<std::vector<uint8_t>*>(context);
            const auto* first = static_cast<const uint8_t*>(input);
            output.insert(output.end(), first, first + count);
        };
        Require(stbi_write_png_to_func(write, &bytes, 2, 2, 4, pixels, 8) != 0, "PNG fixture encoding");
        return bytes;
    }
    std::vector<uint8_t> Dds()
    {
        std::vector<uint8_t> bytes(144, 0);
        const auto put = [&bytes](size_t index, uint32_t value) { memcpy(bytes.data() + index, &value, 4); };
        put(0, 0x20534444); put(4, 124); put(8, 0x100f); put(12, 2); put(16, 2); put(20, 8); put(28, 1);
        put(76, 32); put(80, 0x41); put(88, 32); put(92, 0xff); put(96, 0xff00); put(100, 0xff0000);
        put(104, 0xff000000); put(108, 0x1000);
        const uint8_t pixels[]{151,32,44,0, 61,72,83,128, 94,105,116,255, 127,138,149,255};
        memcpy(bytes.data() + 128, pixels, sizeof(pixels));
        return bytes;
    }
    std::string Base64(const std::vector<uint8_t>& bytes)
    {
        constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        for (size_t i = 0; i < bytes.size(); i += 3)
        {
            const uint32_t word = uint32_t(bytes[i]) << 16 | (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0) |
                (i + 2 < bytes.size() ? uint32_t(bytes[i + 2]) : 0);
            result += alphabet[(word >> 18) & 63]; result += alphabet[(word >> 12) & 63];
            result += i + 1 < bytes.size() ? alphabet[(word >> 6) & 63] : '=';
            result += i + 2 < bytes.size() ? alphabet[word & 63] : '=';
        }
        return result;
    }
    void SharedImages()
    {
        Files files;
        const float positions[]{0,0,0, 1,0,0, 0,1,0};
        files.Add("C:/images/positions.bin", positions, sizeof(positions));
        const auto png = Png();
        for (const char* name : {"shared.png", "unused.png", "channel.png", "new.png", "foo.png"})
            files.Add(std::string("C:/images/") + name, png.data(), png.size());
        files.Add("C:/images/unused.gltf", TexturedModel(
            R"([{"name":"unused first","normalTexture":{"index":0}},{"name":"unused unique","normalTexture":{"index":1}}])",
            R"(,"images":[{"uri":"shared.png"},{"uri":"unused.png"},{"uri":"channel.png"}],"textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":2,"channels":[0]},{"source":2,"channels":[1]}]}}},{"source":1}])"));
        files.Add("C:/images/selected.gltf", TexturedModel(
            R"([{"name":"selected","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])",
            R"(,"images":[{"uri":"shared.png"},{"uri":"shared.png"},{"uri":"channel.png"},{"uri":"new.png"}],"textures":[{"source":1,"extensions":{"NV_texture_swizzle":{"options":[{"source":2,"channels":[3]},{"source":3,"channels":[2,1]}]}}}])"));
        files.Add("C:/images/shared.scene.json", R"({"models":["unused.gltf","selected.gltf"],"graph":[{"name":"selected","model":1},{"name":"clone","model":1}]})");
        auto shared = Run(files, "unused first requests and swizzle batches", "C:/images/shared.scene.json");
        Require(shared.textures.TextureCount() == 1 && shared.textures.TextureRequestCount() == 2 &&
            !shared.textures.Texture(0).forceSRGB && shared.textures.Texture(0).swizzles.count == 3, "unused request retention and merged texture semantics");
        const auto dds = Dds();
        files.Add("C:/images/foo.dds", dds.data(), dds.size());
        files.Add("C:/images/auto.gltf", TexturedModel(R"([{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])",
            R"(,"images":[{"uri":"foo.png"}],"textures":[{"source":0}])"));
        files.Add("C:/images/explicit.gltf", TexturedModel(R"([{"normalTexture":{"index":0}}])",
            R"(,"images":[{"uri":"foo.png"},{"uri":"foo.dds"}],"textures":[{"source":0,"extensions":{"MSFT_texture_dds":{"source":1}}}])"));
        files.Add("C:/images/dds.scene.json", R"({"models":["auto.gltf","explicit.gltf"],"graph":[{"name":"first","model":0},{"name":"second","model":1}]})");
        auto selectedDds = Run(files, "cross-model automatic and explicit DDS", "C:/images/dds.scene.json");
        Require(selectedDds.textures.TextureCount() == 1 && selectedDds.textures.TextureRequestCount() == 1 &&
            String(selectedDds.textures.Image(selectedDds.textures.Texture(0).imageIndex).path) == "C:/images/foo.dds", "DDS alias selection");
        const std::string embedded = TexturedModel(R"([{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])",
            ",\"images\":[{\"name\":\"same\",\"uri\":\"data:image/png;base64," + Base64(png) + "\"}],\"textures\":[{\"source\":0}]");
        files.Add("C:/images/a/model.gltf", embedded); files.Add("C:/images/b/model.gltf", embedded);
        files.Add("C:/images/a/positions.bin", positions, sizeof(positions)); files.Add("C:/images/b/positions.bin", positions, sizeof(positions));
        files.Add("C:/images/embedded.scene.json", R"({"models":["a/model.gltf","b/model.gltf"],"graph":[{"name":"A","model":0},{"name":"B","model":1},{"name":"C","model":0}]})");
        auto inlineImages = Run(files, "same-named embedded images in separate models", "C:/images/embedded.scene.json");
        Require(inlineImages.textures.TextureCount() == 2 && inlineImages.textures.ImageCount() == 2 &&
            inlineImages.textures.Texture(0).imageIndex != inlineImages.textures.Texture(1).imageIndex &&
            String(inlineImages.textures.Image(0).path) == String(inlineImages.textures.Image(1).path), "embedded image identity is independent of its display name");
        files.entries.clear();
        for (size_t i = 0; i < inlineImages.textures.ImageCount(); ++i)
            Require(inlineImages.textures.Image(i).bytes.count == png.size() &&
                memcmp(inlineImages.textures.Image(i).bytes.data, png.data(), png.size()) == 0, "embedded bytes survive fixture filesystem destruction");
    }

    std::vector<uint8_t> Glb(std::string json, const std::vector<uint8_t>& binary)
    {
        while (json.size() % 4) json += ' ';
        const size_t binSize = (binary.size() + 3) & ~size_t(3);
        std::vector<uint8_t> result(28 + json.size() + binSize, 0);
        Require(result.size() < UINT32_MAX, "GLB fixture capacity");
        const uint32_t header[]{0x46546c67, 2, uint32_t(result.size()), uint32_t(json.size()), 0x4e4f534a};
        const uint32_t chunk[]{uint32_t(binSize), 0x004e4942};
        memcpy(result.data(), header, sizeof(header)); memcpy(result.data() + 20, json.data(), json.size());
        memcpy(result.data() + 20 + json.size(), chunk, sizeof(chunk));
        memcpy(result.data() + 28 + json.size(), binary.data(), binary.size());
        return result;
    }
    struct PhysicalFiles
    {
        std::filesystem::path root;
        std::vector<std::filesystem::path> directories, files;
        Files nativeFiles{true};
        PhysicalFiles()
        {
            char directory[MAX_PATH + 1]{}, unique[MAX_PATH + 1]{};
            const DWORD length = GetTempPathA(MAX_PATH, directory);
            Require(length && length < MAX_PATH && GetTempFileNameA(directory, "uvc", 0, unique), "unique physical fixture path");
            Require(DeleteFileA(unique) && CreateDirectoryA(unique, nullptr), "create owned physical fixture root");
            root = unique; physicalRoot = root.generic_string(); directories.push_back(root);
        }
        void Directory(const std::string& relative)
        {
            const auto path = root / relative;
            Require(CreateDirectoryA(path.string().c_str(), nullptr) != 0, "create owned fixture directory");
            directories.push_back(path);
        }
        void Write(const std::string& relative, const void* data, size_t count)
        {
            const auto path = root / relative;
            Require(!nativeFiles.fileExists(path.generic_string()) && count <= UINT32_MAX, "new physical fixture file");
            HANDLE file = CreateFileA(path.string().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            DWORD written = 0;
            const bool complete = file != INVALID_HANDLE_VALUE && WriteFile(file, data, DWORD(count), &written, nullptr) && written == count;
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            Require(complete, "create physical fixture file");
            ReferenceInput(path.generic_string(), data, count); files.push_back(path);
        }
        void Write(const std::string& relative, const std::string& data) { Write(relative, data.data(), data.size()); }
        ~PhysicalFiles()
        {
            // delete only files created above, then their now-empty directories.
            for (const auto& file : files) Require(DeleteFileA(file.string().c_str()) != 0, "remove owned fixture file");
            for (size_t i = directories.size(); i; --i)
                Require(RemoveDirectoryA(directories[i - 1].string().c_str()) != 0, "remove empty owned fixture directory");
        }
    };
    void PhysicalLayouts()
    {
        currentCase = "physical layout setup";
        PhysicalFiles files;
        const float positions[]{0,0,0, 1,0,0, 0,1,0};
        const auto png = Png();
        std::string gltf = TexturedModel(R"([{"name":"external","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])",
            R"(,"images":[{"uri":"../../textures/tile%20a.png"}],"textures":[{"source":0}])");
        Replace(gltf, "positions.bin", "../../buffers/pos%20a.bin");
        std::string binaryJson = TexturedModel(R"([{"name":"embedded","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])",
            R"(,"images":[{"name":"buffer image","bufferView":1,"mimeType":"image/png"}],"textures":[{"source":0}])");
        const auto* positionBytes = reinterpret_cast<const uint8_t*>(positions);
        std::vector<uint8_t> binary(positionBytes, positionBytes + sizeof(positions));
        binary.insert(binary.end(), png.begin(), png.end());
        Replace(binaryJson, R"("buffers":[{"uri":"positions.bin","byteLength":36}])", "\"buffers\":[{\"byteLength\":" + std::to_string(binary.size()) + "}]");
        Replace(binaryJson, R"("bufferViews":[{"buffer":0,"byteLength":36}])",
            "\"bufferViews\":[{\"buffer\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":" + std::to_string(png.size()) + "}]");
        const auto glb = Glb(binaryJson, binary);
        const std::string description = R"({"models":["models/model.gltf","models/embedded.glb"],"graph":[{"name":"A","model":0},{"name":"B","model":1,"translation":[3,2,1]},{"name":"C","model":0,"parent":"/B","euler":[0.2,0.3,0.4]}]})";
        for (const char* layout : {"development", "package"})
        {
            const std::string base = std::string(layout) + "/";
            files.Directory(layout); files.Directory(base + "assets"); files.Directory(base + "assets/scenes");
            files.Directory(base + "assets/scenes/models"); files.Directory(base + "assets/buffers"); files.Directory(base + "assets/textures");
            files.Write(base + "assets/buffers/pos a.bin", positions, sizeof(positions));
            files.Write(base + "assets/textures/tile a.png", png.data(), png.size());
            files.Write(base + "assets/scenes/models/model.gltf", gltf);
            files.Write(base + "assets/scenes/models/embedded.glb", glb.data(), glb.size());
            files.Write(base + "assets/scenes/main.scene.json", description);
            const std::string sceneRoot = (files.root / (base + "assets/scenes")).generic_string();
            Run(files.nativeFiles, layout, sceneRoot + "/models/model.gltf");
            Run(files.nativeFiles, layout, sceneRoot + "/models/embedded.glb");
            auto composed = Run(files.nativeFiles, layout, sceneRoot + "/main.scene.json");
            Require(composed.scene.View().instances.count == 3 && composed.textures.TextureCount() == 2, "physical composed glTF/GLB result");
        }
        printf("physical development/package composition: 6 loads, encoded URI buffers/images and GLB buffer images\n");
    }
    void Basic()
    {
        Files files;
        const float positions[]{0,0,0, 1,0,0, 0,1,0};
        files.Add("C:/scenes/positions.bin", positions, sizeof(positions));
        files.Add("C:/scenes/model.gltf", Model());
        files.Add("C:/scenes/unused.gltf", Model("unused"));
        files.Add("C:/scenes/empty.scene.json", "{}");
        files.Add("C:/scenes/placement.scene.json", R"({"models":["model.gltf","unused.gltf"],"graph":[{"name":"A","model":0,"translation":[2,3,4],"scaling":[2,1,1],"type":"PerspectiveCamera","verticalFov":0.75,"children":[{"name":"lamp","type":"PointLight","intensity":4}]},{"name":"B","model":0}],"animations":[{"name":"mixed","channels":[{"attribute":"translation","mode":"linear","targets":["/A/mesh","material:M","/absent"],"data":[{"time":0,"value":[1,2,3]},{"time":2,"value":4}]}]},{"name":"empty","channels":[{"attribute":"radius","target":"/missing"}]}]})");
        files.Add("C:/scenes/parents.scene.json", R"({"graph":[{"name":"skipped","parent":"/future","children":[{"name":"also skipped"}]},{"name":"future","children":[{"name":""},{"name":"."}]},{"name":"backward","parent":"/future"},{"name":"relative","parent":"future"},{"name":"dup"},{"name":"dup"},{"name":"first duplicate","parent":"/dup"},{"name":"empty child","parent":"/future/"},{"name":"dot child","parent":"/future/."}]})");
        files.Add("C:/scenes/order.scene.json", R"({"models":["model.gltf","unused.gltf"],"graph":[{"name":"parent","children":[{"name":"first","model":1,"translation":[1,2,3],"euler":[0.1,0.2,0.3],"children":[{"name":"attached","model":0,"parent":"/","type":"OrthographicCamera","zNear":0.3,"zFar":40,"xMag":3,"yMag":2}]},{"name":"second","model":1,"translation":[4,5,6]}]},{"name":"third","model":1,"scaling":[2,3,4]},{"name":"fourth","model":0,"type":"SpotLight","color":[0.3,0.4,0.5],"intensity":8,"innerAngle":10,"outerAngle":40}]})");
        Run(files, "direct glTF", "C:/scenes/model.gltf");
        Run(files, "empty application", "C:/scenes/empty.scene.json");
        Run(files, "first template and mixed targets", "C:/scenes/placement.scene.json");
        Run(files, "parent resolution and skipped subtrees", "C:/scenes/parents.scene.json");
        Run(files, "registration and override ordering", "C:/scenes/order.scene.json");
    }

    Files SkinFiles()
    {
        struct Input
        {
            float positions[9]{0,0,0, 1,0,0, 0,1,0};
            uint16_t joints[12]{};
            float weights[12]{};
            float inverse[16]{};
            float times[2]{0,1};
            float translations[6]{1,0,0, 2,0,0};
        } input;
        static_assert(sizeof(Input) == 204 && offsetof(Input, inverse) == 108 && offsetof(Input, times) == 172);
        for (uint32_t i = 0; i < 3; ++i) input.weights[i * 4] = 1;
        for (uint32_t i = 0; i < 16; i += 5) input.inverse[i] = 1;
        const std::string json = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"rig.bin","byteLength":204}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":48},{"buffer":0,"byteOffset":108,"byteLength":64},{"buffer":0,"byteOffset":172,"byteLength":8},{"buffer":0,"byteOffset":180,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":1,"componentType":5123,"count":3,"type":"VEC4"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},{"bufferView":3,"componentType":5126,"count":1,"type":"MAT4"},{"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR"},{"bufferView":5,"componentType":5126,"count":2,"type":"VEC3"}],"meshes":[{"name":"rig","primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2}}]}],"skins":[{"joints":[1],"inverseBindMatrices":3}],"nodes":[{"name":"skin","mesh":0,"skin":0},{"name":"joint","translation":[1,0,0]}],"scenes":[{"nodes":[0,1]}],"animations":[{"name":"move","samplers":[{"input":4,"output":5}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]}]})";
        Files files;
        std::string selectedScene = json;
        Replace(selectedScene, R"("asset":{"version":"2.0"})", R"("asset":{"version":"2.0"},"scene":0)");
        files.Add("C:/skins/rig.gltf", selectedScene); files.Add("C:/skins/rig.bin", &input, sizeof(input));
        const auto png = Png();
        files.Add("C:/skins/attached.gltf", TexturedModel(R"([{"name":"M","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])",
            ",\"images\":[{\"uri\":\"data:image/png;base64," + Base64(png) + "\"}],\"textures\":[{\"source\":0,\"extensions\":{\"NV_texture_swizzle\":{\"options\":[{\"source\":0,\"channels\":[3]}]}}}]"));
        files.Add("C:/skins/positions.bin", input.positions, sizeof(input.positions));
        files.Add("C:/skins/main.scene.json", R"({"models":["rig.gltf","attached.gltf"],"graph":[{"name":"A","model":0,"translation":[2,3,4],"children":[{"name":"attached","model":1,"parent":"/A/joint"}]},{"name":"B","model":0,"translation":[10,5,6],"euler":[0.2,0.3,0.4],"scaling":[1.3,0.8,1.1]}],"animations":[{"name":"application","channels":[{"attribute":"translation","mode":"hermite","targets":["/A/joint","/B/joint"],"data":[{"time":0,"value":[1,0,0],"inTangent":[0.1,0.2,0.3]},{"time":2,"value":[2,0,0],"outTangent":[0.3,0.2,0.1]}]},{"attribute":"roughness","mode":"catmull-rom","target":"material:M","data":[{"time":0,"value":0.3},{"time":2,"value":0.7}]}]}]})");
        return files;
    }
    void Skins()
    {
        auto files = SkinFiles();
        Run(files, "direct animated skin", "C:/skins/rig.gltf");
        const auto result = Run(files, "skin joint and animation clone remapping", "C:/skins/main.scene.json");
        Require(result.scene.View().instances.count == 4 && result.scene.View().joints.count == 2 &&
            result.scene.View().animations.count == 3 && result.scene.View().channels.count == 5 &&
            result.scene.View().samplers.count == 3 && result.textures.TextureCount() == 1, "composed skin, attached texture and animation ownership");
    }
    void PartialLoads()
    {
        currentCase = "complete loading with unavailable model subtrees";
        Files files;
        const float positions[]{0,0,0, 1,0,0, 0,1,0};
        files.Add("C:/partial/positions.bin", positions, sizeof(positions));
        files.Add("C:/partial/valid.gltf", Model());
        files.Add("C:/partial/broken.gltf", "invalid json");
        files.Add("C:/partial/buffer.gltf", Model("M", "", "missing.bin"));
        files.Add("C:/partial/main.json", R"({"models":["missing.gltf","valid.gltf","broken.gltf","buffer.gltf","unreferenced.gltf"],"graph":[{"name":"missing","model":0,"children":[{"name":"skip valid child","model":1}]},{"name":"valid","model":1},{"name":"broken","model":2,"children":[{"name":"skip child"}]},{"name":"buffer","model":3},{"name":"missing twice","model":0},{"name":"camera","type":"PerspectiveCamera","verticalFov":0.75}]})");
        PartialReference(4);
        struct Issues
        {
            uint32_t count = 0, mask = 0;
            static void Report(void* context, uint32_t index, ArrayView<const char> path, ImportLoadOperation, ImportResult result) noexcept
            {
                auto& self = *static_cast<Issues*>(context);
                Require(index < 5 && path.count && !result && !(self.mask & (1u << index)), "single unavailable event per source slot");
                ++self.count; self.mask |= 1u << index;
            }
        } issues;
        ImportSceneLoadOptions options; options.generation = 96;
        ImportLoadCallbacks callbacks; callbacks.context = &issues; callbacks.modelUnavailable = Issues::Report;
        ImportLoadedScene loaded;
        Good(LoadImportScene(LoadFiles(files), Text(std::string("C:/partial/main.json")), options, loaded, callbacks), "candidate partial load");
        Require(issues.count == 4 && issues.mask == 29 && loaded.progress.objectsTotal == 5 && loaded.progress.objectsCompleted == 5 &&
            loaded.progress.objectsUnavailable == 4 && loaded.composition.skippedModelSubtrees == 4, "partial source ordinals and subtree counts");
        ImportModel loadedModel;
        loadedModel.scene = static_cast<RendererScene&&>(loaded.scene);
        loadedModel.geometry = static_cast<ImportGeometry&&>(loaded.geometry);
        Compare(loadedModel, files, true, &loaded.images); ++loadedScenes;
        for (const char* path : {"C:/partial/missing.gltf", "C:/partial/broken.gltf", "C:/partial/buffer.gltf"})
        {
            ImportLoadedScene candidate;
            Require(!LoadImportScene(LoadFiles(files), Text(std::string(path)), options, candidate) && !candidate.scene.StorageBytes(),
                "candidate direct model failure is transactional");
        }
        PartialReference(7);
    }
    std::string Quoted(const std::string& value)
    {
        std::string output = "\"";
        for (const char character : value)
        {
            if (character == '"' || character == '\\') output += '\\';
            output += character;
        }
        return output + '"';
    }
    void NodePaths()
    {
        const char* paths[]{"/", "\\", "/a", "\\a", "/a/", "/a//", "/a/.", "/a/x/..", "/a/./..", "/a/x/../", "/a//x",
            "", "a", "./a", "C:/a", "//server/a", "//?/a", "///a", "\\\\server\\a"};
        for (const char* path : paths)
        {
            Files files;
            const std::string json = std::string(R"({"graph":[{"name":"a","children":[{"name":""},{"name":"."},{"name":"x"}]},{"name":"placed","parent":)") +
                Quoted(path) + R"(,"children":[{"name":"child"}]}],"animations":[{"name":"target","channels":[{"attribute":"translation","mode":"linear","target":)" +
                Quoted(path) + R"(,"targets":["/a"],"data":[{"time":0,"value":[1,2,3]}]}]}]})";
            files.Add("C:/paths/main.scene.json", json);
            Run(files, path, "C:/paths/main.scene.json");
        }
    }

    struct InputDigest
    {
        uint64_t value = UINT64_C(14695981039346656037);
        void Add(const void* memory, size_t count)
        {
            const auto* bytes = static_cast<const uint8_t*>(memory);
            for (size_t i = 0; i < count; ++i) { value ^= bytes[i]; value *= UINT64_C(1099511628211); }
        }
        template<class T> void Scalar(const T& input) { Add(&input, sizeof(input)); }
        template<class T> void View(ArrayView<const T> input)
        {
            Scalar(input.data); Scalar(input.count);
            Require(input.IsValid() && input.count <= SIZE_MAX / sizeof(T), "input digest range");
            Add(input.data, input.count * sizeof(T));
        }
        explicit InputDigest(const ImportModel& model)
        {
            const auto scene = model.scene.View();
            View(scene.nodes); View(scene.meshes); View(scene.geometries); View(scene.instances); View(scene.materials); View(scene.textures);
            View(scene.bufferGroups); View(scene.morphRanges); View(scene.joints); View(scene.lights); View(scene.cameras);
            View(scene.animations); View(scene.channels); View(scene.samplers); View(scene.keyframes); View(scene.strings); View(scene.preorder);
            Scalar(scene.root); Scalar(scene.generation); Scalar(scene.contentRevision); Scalar(scene.transformRevision);
            Scalar(model.scene.StorageBytes()); Scalar(model.geometry.StorageBytes()); Scalar(model.textures.StorageBytes());
            Scalar(model.geometry.ConversionScratchBytes()); Scalar(model.geometry.BufferCount());
            for (size_t b = 0; b < model.geometry.BufferCount(); ++b)
            {
                const auto buffer = model.geometry.Buffer(b);
                View(buffer.indices); View(buffer.vertices); View(buffer.morphs); View(buffer.jointMatrices);
                Scalar(buffer.indexOwner); Scalar(buffer.skinInstanceIndex);
            }
            Scalar(model.textures.ImageCount()); Scalar(model.textures.TextureCount()); Scalar(model.textures.TextureRequestCount());
            for (size_t i = 0; i < model.textures.ImageCount(); ++i)
            {
                const auto image = model.textures.Image(i);
                View(image.path); View(image.mimeType); View(image.bytes); Scalar(image.embedded);
            }
            for (size_t t = 0; t < model.textures.TextureRequestCount(); ++t)
            {
                const auto request = model.textures.TextureRequest(t);
                Scalar(request.imageIndex); Scalar(request.requestIndex); Scalar(request.forceSRGB); View(request.swizzles);
            }
            for (size_t t = 0; t < model.textures.TextureCount(); ++t) Scalar(model.textures.Texture(t).requestIndex);
        }
    };
    void CompositionFailures()
    {
        currentCase = "skin and texture composition failures";
        auto files = SkinFiles();
        const auto blob = files.readFile("C:/skins/main.scene.json");
        ImportSceneDescription description;
        Good(description.Parse({static_cast<const uint8_t*>(blob->data()), blob->size()}, Text(std::string("C:/skins/main.scene.json"))), "failure-test description");
        const auto modelsForRetry = [&]()
        {
            std::vector<ImportModel> models(description.ModelCount());
            for (size_t m = 0; m < models.size(); ++m) models[m] = LoadModel(files, String(description.ModelPath(m)));
            return models;
        };
        ImportCompositionOptions options; options.generation = 101;
        size_t scratchBytes = 0, geometryBytes = 0, imageBytes = 0;
        {
            auto models = modelsForRetry();
            ImportModel result;
            ImportCompositionStats stats;
            Good(ComposeImportScene(description, {models.data(), models.size()}, options, result.scene, result.geometry, result.textures, &stats), "failure-test reference output");
            Compare(result, files, false);
            scratchBytes = stats.peakScratchBytes; geometryBytes = result.geometry.StorageBytes(); imageBytes = result.textures.StorageBytes();
        }
        Require(scratchBytes && geometryBytes && imageBytes, "nonempty combined owner budgets");
        size_t failures[2]{};
        for (uint32_t kind = 0; kind < 2; ++kind)
        {
            bool reachedSuccess = false;
            for (uint32_t fault = kind ? 1 : 0; fault < 256; ++fault)
            {
                auto models = modelsForRetry();
                std::vector<uint64_t> before;
                for (const auto& model : models) before.push_back(InputDigest(model).value);
                ImportModel result;
                ImportCompositionStats stats; stats.peakScratchBytes = 765; stats.ignoredAnimationTargets = 123;
                if (kind) SetRendererSceneAllocationFailure(fault); else SetImportAllocationFailureCountdown(fault);
                const auto status = ComposeImportScene(description, {models.data(), models.size()}, options, result.scene, result.geometry, result.textures, &stats);
                SetRendererSceneAllocationFailure(0); SetImportAllocationFailureCountdown(-1);
                if (status) { Compare(result, files, false); reachedSuccess = true; break; }
                Require(status.error == ImportError::OutOfMemory, "explicit composition allocation failure");
                ++failures[kind];
                for (size_t m = 0; m < models.size(); ++m)
                    Require(models[m].scene.IsPublished() && InputDigest(models[m]).value == before[m], "failed composition preserves every input array, byte range and pointer");
                Require(!result.scene.StorageBytes() && !result.geometry.StorageBytes() && !result.textures.StorageBytes() &&
                    stats.peakScratchBytes == 765 && stats.ignoredAnimationTargets == 123, "failed composition preserves all outputs and stats");
                Good(ComposeImportScene(description, {models.data(), models.size()}, options, result.scene, result.geometry, result.textures), "same-input combined owner retry");
                Compare(result, files, false);
            }
            Require(reachedSuccess, "combined owner failure sweep reached success");
        }
        for (uint32_t limit = 0; limit < 4; ++limit)
        {
            auto models = modelsForRetry();
            std::vector<uint64_t> before;
            for (const auto& model : models) before.push_back(InputDigest(model).value);
            auto bounded = options;
            bounded.maxScratchBytes = scratchBytes; bounded.maxGeometryBytes = geometryBytes; bounded.maxImageBytes = imageBytes;
            if (limit == 1) --bounded.maxScratchBytes;
            if (limit == 2) --bounded.maxGeometryBytes;
            if (limit == 3) --bounded.maxImageBytes;
            ImportModel result;
            const auto status = ComposeImportScene(description, {models.data(), models.size()}, bounded, result.scene, result.geometry, result.textures);
            if (limit)
            {
                Require(status.error == (limit == 1 ? ImportError::Workspace : ImportError::Capacity), "one-byte-short combined owner budget");
                for (size_t m = 0; m < models.size(); ++m) Require(InputDigest(models[m]).value == before[m], "budget failure preserves every input owner");
                Require(!result.scene.StorageBytes() && !result.geometry.StorageBytes() && !result.textures.StorageBytes(), "budget failure preserves output owners");
                Good(ComposeImportScene(description, {models.data(), models.size()}, options, result.scene, result.geometry, result.textures), "budget retry");
            }
            else Good(status, "exact combined owner budgets");
            Compare(result, files, false);
        }
        printf("composed skin and texture allocation failures: import %zu, canonical %zu, all exact-input retries match captured values; budgets %zu scratch, %zu geometry, %zu image bytes\n",
            failures[0], failures[1], scratchBytes, geometryBytes, imageBytes);
    }
    void PercentAliasDefect()
    {
        currentCase = "retained percent-alias cache defect";
        Files files;
        const auto png = Png();
        const float positions[]{0,0,0, 1,0,0, 0,1,0};
        files.Add("C:/aliases/tile a.png", png.data(), png.size());
        files.Add("C:/aliases/positions.bin", positions, sizeof(positions));
        files.Add("C:/aliases/linear.gltf", TexturedModel(R"([{"name":"linear","normalTexture":{"index":0}}])",
            R"(,"images":[{"uri":"tile%20a.png"}],"textures":[{"source":0}])"));
        files.Add("C:/aliases/color.gltf", TexturedModel(R"([{"name":"color","pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])",
            R"(,"images":[{"uri":"tile%20%61.png"}],"textures":[{"source":0}])"));
        const std::string json = R"({"models":["linear.gltf","color.gltf"],"graph":[{"name":"A","model":0},{"name":"B","model":1}]})";
        files.Add("C:/aliases/main.scene.json", json);
        ImportSceneDescription description;
        Good(description.Parse(Bytes(json), Text(std::string("C:/aliases/main.scene.json"))), "percent-alias description");
        ImportModel models[]{LoadModel(files, "C:/aliases/linear.gltf"), LoadModel(files, "C:/aliases/color.gltf")};
        ImportModel output;
        ImportCompositionOptions options; options.generation = 103;
        Good(ComposeImportScene(description, models, options, output.scene, output.geometry, output.textures), "percent-alias composition");
        description.Reset();
        Require(output.textures.TextureCount() == 1 && output.textures.TextureRequestCount() == 1 &&
            !output.textures.Texture(0).forceSRGB && String(output.textures.Image(output.textures.Texture(0).imageIndex).path) == "C:/aliases/tile a.png",
            "candidate owns one decoded path with the first logical request's color space");
        const auto view = output.scene.View();
        Require(view.materials.count == 2 && view.instances.count == 2 && view.textures.count == 1 &&
            view.materials.data[0].values.textures[uint32_t(RendererSceneMaterialTextureSlot::Normal)] == 0 &&
            view.materials.data[1].values.textures[uint32_t(RendererSceneMaterialTextureSlot::BaseOrDiffuse)] == 0,
            "both material roles retain the same canonical texture reference");
        SameText(RendererSceneText(view, view.textures.data[0].path), "C:/aliases/tile a.png", "canonical texture uses the decoded filename");
        Require(U32() == 5, "classified percent-alias control");
        const auto linear = ReferenceText(), color = ReferenceText();
        Require(linear != color && std::string(linear.c_str()) == std::string(color.c_str()) &&
            linear.size() > strlen(linear.c_str()) && color.size() > strlen(color.c_str()), "captured old cache distinguishes stale URI tails");
        Require(!Flag() && Flag() && Flag() && Flag() && Flag(), "captured malformed keys change mixed-role color-space sharing");
        printf("retained percent-alias defect: %zu/%zu-byte native keys, one %zu-byte filename; old linear/sRGB requests stay separate, candidate first request is linear\n",
            linear.size(), color.size(), strlen(linear.c_str()));
    }

}

void RunImportCompositionReferenceTests()
{
    Basic();
    SharedImages();
    PhysicalLayouts();
    Skins();
    PartialLoads();
    NodePaths();
    CompositionFailures();
    PercentAliasDefect();
    FinishReference();
    printf("captured Scene::Load composition: %zu scenes, %zu nodes, %zu channels, %zu textures, %zu GPU palettes matched\n",
        comparedScenes, comparedNodes, comparedChannels, comparedTextures, comparedPalettes);
    printf("complete CPU loading compared with captured Scene::Load: %zu scenes, %zu decoded textures, 7 recorded native model errors\n",
        loadedScenes, loadedTextures);
}
