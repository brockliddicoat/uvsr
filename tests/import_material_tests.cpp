#include "renderer_import_scene.h"
#include "renderer_scene_encoding.h"

#include "import_material_fixture.h"
#include <filesystem>
#include <string>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stb_image_write.h>
#include <Windows.h>

namespace
{
    using namespace uvsr;
    size_t comparisons = 0, rejections = 0;
    constexpr float positions[9]{0, 0, 0, 1, 0, 0, 0, 1, 0};
    ArrayView<const uint8_t> DefaultBytes() noexcept
    { return {reinterpret_cast<const uint8_t*>(positions), sizeof(positions)}; }

    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "material import failed: %s\n", reason);
        exit(1);
    }
    void Good(ImportResult result, const char* reason)
    {
        if (result) return;
        fprintf(stderr, "material import failed: %s: %s, object %u, index %zu, parser %u\n",
            reason, ImportErrorText(result.error), unsigned(result.object), result.index, result.parserCode);
        exit(1);
    }
    struct Text
    {
        char* data = static_cast<char*>(malloc(131072));
        size_t count = 0;
        Text() { Require(data != nullptr, "text storage"); data[0] = 0; }
        ~Text() { free(data); }
        void Append(const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            const int size = vsnprintf(data + count, 131072 - count, format, args);
            va_end(args);
            Require(size >= 0 && size_t(size) < 131072 - count, "text capacity");
            count += size_t(size);
        }
    };
    ImportSceneOptions Options()
    {
        ImportSceneOptions options;
        options.generation = 42;
        options.modelName = {"fixture", 7};
        options.modelPath = {"fixture.gltf", 12};
        return options;
    }
    using NamedFile = ImportMaterialFixtureFile;
    struct EncodedImage
    {
        uint8_t data[4096]{};
        size_t count = 0;
        ArrayView<const uint8_t> Bytes() const noexcept { return {data, count}; }
        static void Write(void* context, void* bytes, int size)
        {
            auto& image = *static_cast<EncodedImage*>(context);
            Require(size >= 0 && size_t(size) <= sizeof(image.data) - image.count, "encoded image capacity");
            memcpy(image.data + image.count, bytes, size_t(size));
            image.count += size_t(size);
        }
        void Png(uint8_t red = 31)
        {
            const uint8_t pixels[]{red, 61, 127, 0, 255, 13, 29, 64, 7, 211, 83, 128, 91, 151, 253, 255};
            count = 0;
            Require(stbi_write_png_to_func(Write, this, 2, 2, 4, pixels, 8) != 0, "encode PNG fixture");
        }
        void Dds()
        {
            memset(data, 0, sizeof(data));
            const auto put = [this](size_t offset, uint32_t value) { memcpy(data + offset, &value, 4); };
            put(0, 0x20534444); put(4, 124); put(8, 0x100f); put(12, 2); put(16, 2); put(20, 8); put(28, 1);
            put(76, 32); put(80, 0x41); put(88, 32); put(92, 0xff); put(96, 0xff00); put(100, 0xff0000);
            put(104, 0xff000000); put(108, 0x1000);
            const uint8_t pixels[]{151, 32, 44, 0, 61, 72, 83, 128, 94, 105, 116, 255, 127, 138, 149, 255};
            memcpy(data + 128, pixels, sizeof(pixels));
            count = 144;
        }
        void Base64(Text& text) const
        {
            constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            for (size_t i = 0; i < count; i += 3)
            {
                const uint32_t word = uint32_t(data[i]) << 16 | (i + 1 < count ? uint32_t(data[i + 1]) << 8 : 0) |
                    (i + 2 < count ? uint32_t(data[i + 2]) : 0);
                text.Append("%c%c%c%c", alphabet[word >> 18], alphabet[word >> 12 & 63],
                    i + 1 < count ? alphabet[word >> 6 & 63] : '=', i + 2 < count ? alphabet[word & 63] : '=');
            }
        }
    };
    struct MaterialFiles
    {
        ArrayView<const NamedFile> images;
        const char* model;
        static std::string FileName(const std::filesystem::path& path)
        { return std::string(path.generic_string().c_str()); }
        bool FileExists(const std::filesystem::path& path) const
        {
            const auto name = FileName(path);
            if (name == model || path.filename() == "fixture.bin") return true;
            for (size_t i = 0; i < images.count; ++i) if (name == images.data[i].path) return true;
            return false;
        }
        static bool Exists(void* context, ArrayView<const char> path) noexcept
        { return static_cast<MaterialFiles*>(context)->FileExists(std::string(path.data, path.count)); }
        static ArrayView<const uint8_t> Read(void* context, ArrayView<const char> path)
        {
            const auto& files = *static_cast<const MaterialFiles*>(context);
            const auto name = FileName(std::string(path.data, path.count));
            for (size_t i = 0; i < files.images.count; ++i)
                if (name == files.images.data[i].path) return files.images.data[i].bytes;
            return {};
        }
    };
    void Fixture(Text& json, const char* materials, ArrayView<const int32_t> order, const char* extra = "",
        size_t byteCount = sizeof(positions), const char* extraViews = "")
    {
        json.Append(R"({"asset":{"version":"2.0"},"buffers":[{"uri":"fixture.bin","byteLength":%zu}],"bufferViews":[{"buffer":0,"byteLength":36}%s],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"materials":%s,%s"meshes":[{"primitives":[)", byteCount, extraViews, materials, extra);
        for (size_t i = 0; i < order.count; ++i)
        {
            json.Append("%s{\"attributes\":{\"POSITION\":0}", i ? "," : "");
            if (order.data[i] >= 0) json.Append(",\"material\":%d", order.data[i]);
            json.Append("}");
        }
        json.Append(R"(]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})");
    }
    void Parse(const Text& json, ImportDocument& document, ArrayView<const uint8_t> bytes = {})
    {
        Good(document.Parse({reinterpret_cast<const uint8_t*>(json.data), json.count}), "parse material fixture");
        ImportBufferInfo info;
        Good(document.BufferInfo(0, info), "material geometry buffer");
        if (!info.resident) Good(document.SupplyBuffer(0, bytes.count ? bytes : DefaultBytes()), "supply material geometry");
    }
    void CompareFixture(Text& json, ArrayView<const NamedFile> namedFiles = {}, const char* model = "fixture.gltf", ArrayView<const uint8_t> bytes = {})
    {
        if (!bytes.count) bytes = DefaultBytes();
        BeginImportMaterialReference({json.data, json.count}, bytes, namedFiles, model);
        MaterialFiles files{namedFiles, model};
        ImportDocument document;
        Parse(json, document, bytes);
        RendererScene scene;
        ImportGeometry geometry;
        ImportTextures imageOwner;
        auto options = Options();
        options.modelPath = {model, strlen(model)};
        options.fileExists = MaterialFiles::Exists;
        options.fileContext = &files;
        Good(ConvertImportScene(document, options, scene, geometry, &imageOwner), "convert materials");
        document.Reset();
        memset(json.data, 0xcd, json.count);
        comparisons += CompareImportMaterialReference(scene.View(), imageOwner, &files, MaterialFiles::Read);
    }
    void DomainsAndValues()
    {
        for (unsigned specular = 0; specular < 2; ++specular)
        {
            Text materials;
            materials.Append("[");
            const char* modes[]{"OPAQUE", "MASK", "BLEND"};
            for (unsigned i = 0; i < 6; ++i)
            {
                materials.Append("%s{\"name\":\"authored-%u\",\"alphaMode\":\"%s\",\"alphaCutoff\":0.234,\"doubleSided\":%s,\"emissiveFactor\":[0.37,0.23,0.91],",
                    i ? "," : "", i, modes[i % 3], i % 2 ? "true" : "false");
                materials.Append(R"("pbrMetallicRoughness":{"baseColorFactor":[0.13,0.29,0.61,0.42],"metallicFactor":0.73,"roughnessFactor":0.26},"extensions":{)");
                bool comma = false;
                if (specular)
                {
                    materials.Append(R"("KHR_materials_pbrSpecularGlossiness":{"diffuseFactor":[0.53,0.24,0.18,0.31],"specularFactor":[0.11,0.35,0.87],"glossinessFactor":0.72})");
                    comma = true;
                }
                if (i >= 3)
                { materials.Append("%s\"KHR_materials_transmission\":{\"transmissionFactor\":%s}", comma ? "," : "", i == 3 ? "0" : "0.61"); comma = true; }
                if (i % 2) materials.Append("%s\"KHR_materials_emissive_strength\":{\"emissiveStrength\":3.71}", comma ? "," : "");
                materials.Append("}}");
            }
            materials.Append(",{}, {\"pbrMetallicRoughness\":{}}, {\"name\":\"unused\"}]");
            const int32_t order[]{4, 1, 4, 5, 0, 2, 3, 6, 7, -1};
            Text json;
            Fixture(json, materials.data, {order, 10});
            CompareFixture(json);
        }
    }
    void CustomMaterials()
    {
        const int32_t order[]{0, 1, 2, 3};
        Text json;
        Fixture(json, R"([{"extensions":{"NV_materials_subsurface":{"transmissionColor":[0.1,0.2,0.3],"scatteringColor":[0.4,0.5,0.6],"scale":7.2,"anisotropy":-0.4}}},{"extensions":{"NV_materials_hair":{"baseColor":[0.27,0.31,0.42],"melanin":0.71,"melaninRedness":0.19,"longitudinalRoughness":0.33,"azimuthalRoughness":0.57,"ior":1.8,"cuticleAngle":4.2,"diffuseReflectionWeight":0.43,"diffuseReflectionTint":[0.51,0.67,0.88]}}},{"extensions":{"NV_materials_subsurface":{}}},{"extensions":{"NV_materials_hair":{}}}])",
            {order, 4}, R"("extensionsRequired":["NV_materials_subsurface","NV_materials_hair","KHR_mesh_quantization"],)");
        CompareFixture(json);
        Text escaped;
        const int32_t one[]{0};
        Fixture(escaped, R"([{"extensions":{"NV_materials_subsurface":{"scale":3}}}])", {one, 1},
            R"("extensions\u0052equired":["NV_materials_subsurface"],"extras":{"extensionsRequired":"nested"},)");
        CompareFixture(escaped);
    }
    void ExternalTextures()
    {
        EncodedImage png;
        png.Png();
        const NamedFile files[]{{"shared.png", png.Bytes()}, {"srgb.png", png.Bytes()}, {"normal.png", png.Bytes()}};
        const int32_t order[]{2, 1, 2};
        Text json;
        Fixture(json, R"([{"name":"unused first request","normalTexture":{"index":0}},
            {"alphaMode":"MASK","pbrMetallicRoughness":{"baseColorTexture":{"index":1,"texCoord":1,"extensions":{"KHR_texture_transform":{"offset":[0.2,0.3],"rotation":0.1,"scale":[3,4]}}},"metallicRoughnessTexture":{"index":3}},
            "emissiveTexture":{"index":2},"normalTexture":{"index":4,"scale":2.7,"texCoord":1,"extensions":{"KHR_texture_transform":{"scale":[0.7,1.3],"offset":[0.1,0.2],"rotation":0.9}}},"occlusionTexture":{"index":1,"strength":0.61}},
            {"extensions":{"KHR_materials_pbrSpecularGlossiness":{"diffuseTexture":{"index":2},"specularGlossinessTexture":{"index":3}},"KHR_materials_transmission":{"transmissionFactor":0.8,"transmissionTexture":{"index":1}}}}])",
            {order, 3}, R"("images":[{"uri":"shared.png"},{"uri":"shared.png"},{"uri":"srgb.png"},{"uri":"normal.png"}],
            "textures":[{"source":0,"sampler":0},{"source":1,"sampler":1},{"source":2,"sampler":0},{"source":2,"sampler":1},{"source":3}],
            "samplers":[{"magFilter":9728,"minFilter":9984,"wrapS":33071,"wrapT":33648},{"magFilter":9729,"minFilter":9987,"wrapS":10497,"wrapT":10497}],)");
        CompareFixture(json, {files, 3});
        Text required;
        const int32_t one[]{0};
        Fixture(required, R"([{"normalTexture":{"index":0,"scale":0.4,"extensions":{"KHR_texture_transform":{"scale":[2,3]}}}}])", {one, 1},
            R"("extensionsRequired":["KHR_texture_transform"],"textures":[{"source":0}],"images":[{"uri":"normal.png"}],)");
        CompareFixture(required, {files, 3});
    }
    void DdsSelection()
    {
        EncodedImage png, dds;
        png.Png(); dds.Dds();
        const NamedFile files[]{{"assets/auto.dds", dds.Bytes()}, {"assets/explicit.dds", dds.Bytes()},
            {"assets/plain.png", png.Bytes()}, {"assets/fallback.png", png.Bytes()}};
        const int32_t order[]{0};
        Text json;
        Fixture(json, R"([{"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":2}},"normalTexture":{"index":1},"emissiveTexture":{"index":3}}])",
            {order, 1}, R"("images":[{"uri":"auto.png"},{"uri":"explicit.dds"},{"uri":"plain.png"},{"uri":"fallback.png"},{"uri":"missing.dds"}],
            "textures":[{"source":0},{"source":0,"extensions":{"MSFT_texture_dds":{"source":1}}},{"source":2},{"source":3,"extensions":{"MSFT_texture_dds":{"source":4}}}],
            "extensionsRequired":["MSFT_texture_dds"],)");
        CompareFixture(json, {files, 4}, "assets/scene.gltf");
    }
    void TexturePaths()
    {
        wchar_t directory[MAX_PATH]{}, file[MAX_PATH]{};
        const DWORD length = GetTempPathW(MAX_PATH, directory);
        Require(length > 0 && length < MAX_PATH && GetTempFileNameW(directory, L"uvm", 0, file), "native path probe file");
        HANDLE handle = CreateFileW(file, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD written = 0;
        const uint8_t content[]{19, 37, 61, 83};
        const bool wrote = handle != INVALID_HANDLE_VALUE && WriteFile(handle, content, sizeof(content), &written, nullptr) && written == sizeof(content);
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        std::wstring retained(file);
        retained.push_back(L'\0'); retained.append(L"stale");
        const DWORD attributes = GetFileAttributesW(retained.c_str());
        const bool exists = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
        HANDLE input = CreateFileW(retained.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        uint8_t read[sizeof(content)]{};
        DWORD received = 0;
        LARGE_INTEGER size{};
        const bool matched = input != INVALID_HANDLE_VALUE && GetFileSizeEx(input, &size) && size.QuadPart == sizeof(content) &&
            ReadFile(input, read, sizeof(read), &received, nullptr) && received == sizeof(content) && !memcmp(read, content, sizeof(content));
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        const bool removed = DeleteFileW(file) != 0;
        Require(wrote && exists && matched && removed, "Windows consumes the filename before its first null");
        EncodedImage png, dds;
        png.Png(); dds.Dds();
        const NamedFile files[]{{"C:/assets/images/a b.png", png.Bytes()}, {"C:/assets/../shared.png", png.Bytes()},
            {"C:/assets/.hidden.dds", dds.Bytes()}, {"C:/assets/nested//upper.DDS", dds.Bytes()}};
        const int32_t one[]{0};
        Text json;
        Fixture(json, R"([{"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1}},"normalTexture":{"index":2},"occlusionTexture":{"index":3}}])", {one, 1},
            R"("images":[{"uri":"images/a%20b.png"},{"uri":"../shared.png"},{"uri":".hidden"},{"uri":"nested//upper.DDS"}],"textures":[{"source":0},{"source":1},{"source":2},{"source":3}],)");
        CompareFixture(json, {files, 4}, "C:/assets/scene.gltf");
    }
    void EmbeddedTextures()
    {
        EncodedImage first, second;
        first.Png(); second.Png(197);
        Text a, b, extra;
        first.Base64(a); second.Base64(b);
        extra.Append(R"("images":[{"name":"same name","uri":"data:image/png;base64,%s","mimeType":"image/png"},{"name":"same name","uri":"data:image/png;base64,%s"},
            {"name":"","uri":"data:image/png;base64,%s"},{"uri":"data:image/png;base64,%s"}],"textures":[{"source":0},{"source":1},{"source":0},{"source":2},{"source":3}],)", a.data, b.data, a.data, b.data);
        Text json;
        const int32_t order[]{0, 1};
        Fixture(json, R"([{"pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicRoughnessTexture":{"index":1}},"normalTexture":{"index":2}},
            {"emissiveTexture":{"index":3},"occlusionTexture":{"index":4}}])", {order, 2}, extra.data);
        CompareFixture(json, {}, "assets/inline.gltf");
        for (unsigned glb = 0; glb < 2; ++glb)
        {
            uint8_t bytes[4096];
            memcpy(bytes, positions, sizeof(positions));
            memcpy(bytes + sizeof(positions), first.data, first.count);
            const size_t byteCount = sizeof(positions) + first.count;
            Text views, bufferJson;
            views.Append(",{\"buffer\":0,\"byteOffset\":36,\"byteLength\":%zu}", first.count);
            Fixture(bufferJson, R"([{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}},"extensions":{"NV_materials_subsurface":{"scale":2}}}])", {order, 1},
                R"("images":[{"bufferView":1,"mimeType":"image/png"}],"textures":[{"source":0}],"extensionsRequired":["NV_materials_subsurface"],)", byteCount, views.data);
            if (glb)
            {
                constexpr char uri[] = "\"uri\":\"fixture.bin\",";
                char* found = strstr(bufferJson.data, uri);
                Require(found != nullptr, "GLB fixture buffer");
                memmove(found, found + sizeof(uri) - 1, bufferJson.count - size_t(found - bufferJson.data) - sizeof(uri) + 2);
                bufferJson.count -= sizeof(uri) - 1;
                const size_t jsonPadded = (bufferJson.count + 3) & ~size_t(3);
                const size_t binaryPadded = (byteCount + 3) & ~size_t(3);
                Text container;
                container.count = 28 + jsonPadded + binaryPadded;
                memset(container.data, 0, container.count);
                const auto put = [&container](size_t offset, uint32_t value) { memcpy(container.data + offset, &value, 4); };
                put(0, 0x46546c67); put(4, 2); put(8, uint32_t(container.count));
                put(12, uint32_t(jsonPadded)); put(16, 0x4e4f534a);
                memset(container.data + 20, ' ', jsonPadded);
                memcpy(container.data + 20, bufferJson.data, bufferJson.count);
                put(20 + jsonPadded, uint32_t(binaryPadded)); put(24 + jsonPadded, 0x004e4942);
                memcpy(container.data + 28 + jsonPadded, bytes, byteCount);
                CompareFixture(container, {}, "assets/inline.glb", {bytes, byteCount});
            }
            else CompareFixture(bufferJson, {}, "assets/inline.gltf", {bytes, byteCount});
        }
    }
    void SwizzleSemantics()
    {
        EncodedImage png;
        png.Png();
        const NamedFile files[]{{"packed.png", png.Bytes()}, {"channel.png", png.Bytes()}, {"second.png", png.Bytes()}};
        const int32_t order[]{0, 1, 2};
        Text json;
        Fixture(json, R"([{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},{"normalTexture":{"index":1}},{"emissiveTexture":{"index":2}}])", {order, 3},
            R"("images":[{"uri":"packed.png"},{"uri":"channel.png"},{"uri":"channel.png"},{"uri":"second.png"}],
            "textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":1,"channels":[0,1,2,-1]},{"source":2,"channels":[3]}]}}},
            {"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":2,"channels":[9]},{"source":3,"channels":[]},{"source":3,"channels":[2,1]}]}}},
            {"extensions":{"NV_texture_swizzle":{"options":[{"source":1,"channels":[3,2,1,0]}]}}}],)");
        CompareFixture(json, {files, 3});
        Text required;
        Fixture(required, R"([{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}])", {order, 1},
            R"("extensionsRequired":["NV_texture_swizzle"],"images":[{"uri":"packed.png"},{"uri":"channel.png"}],"textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":1,"channels":[3,2,1,0]}]}}}],)");
        CompareFixture(required, {files, 3});
        EncodedImage second;
        second.Png(227);
        Text a, b, extra, embedded;
        png.Base64(a); second.Base64(b);
        extra.Append(R"("images":[{"uri":"packed.png"},{"name":"same name","uri":"data:image/png;base64,%s"},{"name":"same name","uri":"data:image/png;base64,%s"}],
            "textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":1,"channels":[0]},{"source":2,"channels":[1]}]}}},
            {"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":1,"channels":[3]}]}}}],)", a.data, b.data);
        Fixture(embedded, R"([{"normalTexture":{"index":0}},{"emissiveTexture":{"index":1}}])", {order, 2}, extra.data);
        CompareFixture(embedded, {files, 3});
    }
    void MetadataFailures()
    {
        struct Case { const char* tail; ImportError error; } cases[]{
            {R"("extensionsRequired":["NV_materials_hair","UVSR_unknown"])", ImportError::UnsupportedExtension},
            {R"("extensionsRequired":["NV_materials_hair","KHR_materials_ior"])", ImportError::UnsupportedExtension},
            {R"("extensionsRequired":["NV_materials_hair"],"extensionsRequired":[])", ImportError::InvalidData},
            {R"("materials":[{"pbrMetallicRoughness":7}])", ImportError::InvalidData},
            {R"("materials":[{"pbrMetallicRoughness":{"baseColorFactor":[1,2,3,4,5]}}])", ImportError::InvalidData},
            {R"("materials":[{"pbrMetallicRoughness":{"metallicFactor":1e100}}])", ImportError::InvalidData},
            {R"("materials":[{"extensions":{"KHR_materials_pbrSpecularGlossiness":{"glossinessFactor":"bad"}}}])", ImportError::InvalidData},
            {R"("materials":[{"extensions":{"KHR_materials_transmission":{"transmissionFactor":[]}}}])", ImportError::InvalidData},
            {R"("materials":[{"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":null}}}])", ImportError::InvalidData},
            {R"("materials":[{"extensions":{"NV_materials_subsurface":{"transmissionColor":[1,2]}}}])", ImportError::InvalidData},
            {R"("materials":[{"extensions":{"NV_materials_hair":{"cuticleAngle":1e90}}}])", ImportError::InvalidData},
            {R"("textures":[{"source":0}],"images":[{"uri":"a.png"}],"materials":[{"normalTexture":{"index":1}}])", ImportError::InvalidIndex},
            {R"("textures":[{"source":0}],"images":[{"uri":"a.png"}],"materials":[{"normalTexture":{"index":0,"extensions":{"KHR_texture_transform":{"scale":[1]}}}}])", ImportError::InvalidData},
            {R"("extensionsRequired":["KHR_texture_transform"],"textures":[{"source":0}],"images":[{"uri":"a.png"}],"materials":[{"normalTexture":{"index":0,"extensions":{"KHR_texture_transform":{"rotation":0.1}}}}])", ImportError::UnsupportedExtension},
            {R"("extensionsRequired":["KHR_texture_transform"],"textures":[{"source":0}],"images":[{"uri":"a.png"}],"materials":[{"normalTexture":{"index":0,"extensions":{"KHR_texture_transform":{"texCoord":1}}}}])", ImportError::UnsupportedExtension},
            {R"("extensionsRequired":["NV_materials_subsurface"],"textures":[{"source":0}],"images":[{"uri":"a.png"}],"materials":[{"extensions":{"NV_materials_subsurface":{"transmissionColorTexture":{"index":0}}}}])", ImportError::UnsupportedExtension},
            {R"("textures":[{"extensions":{"MSFT_texture_dds":{"source":2}}}],"images":[{"uri":"a.dds"}])", ImportError::InvalidIndex},
            {R"("textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":0,"channels":[0,1,2,3,4]}]}}}],"images":[{"uri":"a.png"}])", ImportError::InvalidData},
            {R"("textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":0,"channels":[-2]}]}}}],"images":[{"uri":"a.png"}])", ImportError::InvalidData},
            {R"("textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"channels":[0]}]}}}],"images":[{"uri":"a.png"}])", ImportError::InvalidData},
            {R"("textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":0,"channels":[2147483648]}]}}}],"images":[{"uri":"a.png"}])", ImportError::InvalidData},
            {R"("textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":0,"channels":[0.5]}]}}}],"images":[{"uri":"a.png"}])", ImportError::InvalidData},
            {R"("images":[{"name":null,"uri":"a.png"}])", ImportError::InvalidData},
            {R"("images":[{"mimeType":false,"uri":"a.png"}])", ImportError::InvalidData},
            {R"("images":[{"uri":"a%00.png"}])", ImportError::InvalidUri},
            {R"("images":[{"uri":"a%2.png"}])", ImportError::InvalidUri},
            {R"("images":[{"uri":"data:image/png;base64,"}])", ImportError::InvalidUri},
            {R"("images":[{"uri":"data:image/png;base64,==AA"}])", ImportError::InvalidUri},
            {R"("samplers":[{"magFilter":9984}])", ImportError::InvalidData},
            {R"("samplers":[{"minFilter":0}])", ImportError::InvalidData},
            {R"("samplers":[{"wrapS":65536}])", ImportError::InvalidData},
            {R"("samplers":[{"wrapT":null}])", ImportError::InvalidData}
        };
        ImportDocument document;
        constexpr char retained[] = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"retained.bin","byteLength":4}]})";
        Good(document.Parse({reinterpret_cast<const uint8_t*>(retained), sizeof(retained) - 1}), "retained document");
        for (const auto& test : cases)
        {
            Text json;
            json.Append("{\"asset\":{\"version\":\"2.0\"},%s}", test.tail);
            const auto result = document.Parse({reinterpret_cast<const uint8_t*>(json.data), json.count});
            if (result.error != test.error)
                fprintf(stderr, "rejection %zu expected %s, got %s: %s\n", rejections, ImportErrorText(test.error), ImportErrorText(result.error), test.tail);
            Require(result.error == test.error && document.BufferCount() == 1, "metadata failure preserves document");
            ++rejections;
        }
    }
    void AllocationFailures()
    {
        Text json, base64, extra;
        EncodedImage png;
        png.Png(); png.Base64(base64);
        const int32_t order[]{1, 0};
        extra.Append(R"("textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"options":[{"source":1,"channels":[0,1,2,-1]}]}}}],"images":[{"mimeType":"image/png","uri":"data:image/png;base64,%s"},{"uri":"a.png"}],)", base64.data);
        Fixture(json, R"([{"emissiveTexture":{"index":0},"extensions":{"NV_materials_subsurface":{"scale":2}}},{"pbrMetallicRoughness":{"roughnessFactor":0.3,"baseColorTexture":{"index":0}}}])", {order, 2}, extra.data);
        MaterialFiles files{{}, "fixture.gltf"};
        auto options = Options();
        options.fileExists = MaterialFiles::Exists; options.fileContext = &files;
        size_t parseFaults = 0;
        for (int64_t ordinal = 0; ordinal < 32; ++ordinal)
        {
            ImportDocument document;
            SetImportAllocationFailureCountdown(ordinal);
            const auto result = document.Parse({reinterpret_cast<const uint8_t*>(json.data), json.count});
            SetImportAllocationFailureCountdown(-1);
            if (result) break;
            Require(result.error == ImportError::OutOfMemory && document.BufferCount() == 0, "metadata allocation failure");
            Parse(json, document);
            ++parseFaults;
        }
        Require(parseFaults >= 6 && parseFaults < 32, "all metadata allocations covered");
        ImportDocument document;
        Parse(json, document);
        size_t conversionFaults = 0;
        for (int64_t ordinal = 0; ordinal < 64; ++ordinal)
        {
            RendererScene scene;
            ImportGeometry geometry;
            ImportTextures textures;
            SetImportAllocationFailureCountdown(ordinal);
            const auto result = ConvertImportScene(document, options, scene, geometry, &textures);
            SetImportAllocationFailureCountdown(-1);
            if (result) break;
            Require(result.error == ImportError::OutOfMemory && !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes(), "conversion failure preserves empty outputs");
            Good(ConvertImportScene(document, options, scene, geometry, &textures), "retry material conversion");
            ++conversionFaults;
        }
        Require(conversionFaults >= 32 && conversionFaults < 64, "all image/conversion allocations covered");
        size_t sceneFaults = 0;
        for (int64_t ordinal = 0; ordinal < 32; ++ordinal)
        {
            RendererScene scene; ImportGeometry geometry; ImportTextures textures;
            SetRendererSceneAllocationFailure(uint32_t(ordinal + 1));
            const auto result = ConvertImportScene(document, options, scene, geometry, &textures);
            SetRendererSceneAllocationFailure(0);
            if (result) break;
            Require(result.error == ImportError::OutOfMemory && !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes(), "canonical failure releases geometry and image candidates");
            Good(ConvertImportScene(document, options, scene, geometry, &textures), "retry canonical material allocation");
            ++sceneFaults;
        }
        Require(sceneFaults > 0 && sceneFaults < 32, "all canonical material allocations covered");
        RendererScene scene; ImportGeometry geometry; ImportTextures textures;
        Good(ConvertImportScene(document, options, scene, geometry, &textures), "measure image capacity");
        options.maxScratchBytes = geometry.ConversionScratchBytes();
        options.maxGeometryBytes = geometry.StorageBytes(); options.maxImageBytes = textures.StorageBytes();
        scene.Reset(); geometry.Reset(); textures.Reset();
        Good(ConvertImportScene(document, options, scene, geometry, &textures), "exact image and scratch capacity");
        RendererScene spareScene; ImportGeometry spareGeometry;
        const auto retainedImage = textures.Image(0);
        Require(ConvertImportScene(document, options, spareScene, spareGeometry, &textures).error == ImportError::InvalidState &&
            textures.Image(0).bytes.data == retainedImage.bytes.data && !spareScene.StorageBytes() && !spareGeometry.StorageBytes(), "nonempty image output preserved");
        scene.Reset(); geometry.Reset(); textures.Reset();
        --options.maxImageBytes;
        Require(ConvertImportScene(document, options, scene, geometry, &textures).error == ImportError::Capacity &&
            !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes(), "one byte short image capacity");
        ++options.maxImageBytes; --options.maxScratchBytes;
        Require(ConvertImportScene(document, options, scene, geometry, &textures).error == ImportError::Workspace &&
            !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes(), "one byte short image planning workspace");
        ++options.maxScratchBytes;
        Good(ConvertImportScene(document, options, scene, geometry, &textures), "retry exact image capacity");
        document.Reset(); memset(json.data, 0xcd, json.count);
        const auto encoded = textures.Image(textures.Texture(0).imageIndex);
        Require(encoded.bytes.count == png.count && memcmp(encoded.bytes.data, png.data, png.count) == 0, "exact copied PNG after parser/input destruction");
        ImportTextures moved;
        moved = static_cast<ImportTextures&&>(textures);
        Require(!textures.ImageCount() && !textures.StorageBytes() && moved.ImageCount() == 2, "move assignment retains image ownership");
        moved = static_cast<ImportTextures&&>(moved);
        Require(moved.TextureCount() == 1, "texture self move");
        moved.Reset(); moved.Reset();
        Require(!moved.StorageBytes() && !moved.Image(SIZE_MAX).bytes.count && moved.Texture(SIZE_MAX).imageIndex == InvalidSceneIndex, "image reset and invalid views");
        printf("material allocation failures: parse %zu, conversion %zu, canonical %zu, with retry and exact capacity\n", parseFaults, conversionFaults, sceneFaults);
    }
    void TextureFailures()
    {
        const int32_t one[]{0};
        Text json;
        Fixture(json, R"([{"normalTexture":{"index":0}}])", {one, 1}, R"("textures":[{"source":0}],"images":[{"uri":"a.png"}],)");
        ImportDocument document;
        Parse(json, document);
        RendererScene scene; ImportGeometry geometry; ImportTextures textures;
        MaterialFiles files{{}, "fixture.gltf"};
        auto options = Options();
        const auto rejected = [&](ImportResult result, ImportError expected, const char* reason)
        {
            if (result.error != expected) fprintf(stderr, "%s: expected %s, got %s\n", reason, ImportErrorText(expected), ImportErrorText(result.error));
            Require(result.error == expected && !scene.StorageBytes() && !geometry.StorageBytes() && !textures.StorageBytes(), reason);
            ++rejections;
        };
        rejected(ConvertImportScene(document, options, scene, geometry, &textures), ImportError::InvalidInput, "file existence callback required");
        options.fileExists = MaterialFiles::Exists; options.fileContext = &files;
        rejected(ConvertImportScene(document, options, scene, geometry), ImportError::InvalidOutput, "image owner required");
        options.modelPath = {};
        rejected(ConvertImportScene(document, options, scene, geometry, &textures), ImportError::InvalidInput, "file image needs model path");
        options.modelPath = {nullptr, 1};
        rejected(ConvertImportScene(document, options, scene, geometry, &textures), ImportError::InvalidInput, "invalid path borrow");
        options.modelPath = {"fixture.gltf", 12};
        struct Case { const char* extra; ImportError error; } cases[]{
            {R"("textures":[{"source":1}],"images":[{"uri":"a.png"}],)", ImportError::InvalidIndex},
            {R"("textures":[{"source":0,"sampler":1}],"samplers":[{}],"images":[{"uri":"a.png"}],)", ImportError::InvalidIndex},
            {R"("textures":[{"source":0}],"images":[{"uri":"https://example.invalid/a.png"}],)", ImportError::UnsupportedData},
            {R"("textures":[{"source":0}],"images":[{"bufferView":2,"mimeType":"image/png"}],)", ImportError::InvalidIndex},
            {R"("textures":[{"extensions":{"NV_texture_swizzle":{"options":[{"source":0,"channels":[0]}]}}}],"images":[{"uri":"a.png"}],"extensionsRequired":["NV_texture_swizzle"],)", ImportError::UnsupportedExtension}
        };
        for (const auto& test : cases)
        {
            Text candidate;
            Fixture(candidate, R"([{"normalTexture":{"index":0}}])", {one, 1}, test.extra);
            Parse(candidate, document);
            rejected(ConvertImportScene(document, options, scene, geometry, &textures), test.error, "invalid texture conversion");
        }
        Text range;
        Fixture(range, R"([{"normalTexture":{"index":0}}])", {one, 1}, R"("textures":[{"source":0}],"images":[{"bufferView":1,"mimeType":"image/png"}],)", 36,
            R"(,{"buffer":0,"byteOffset":34,"byteLength":8})");
        rejected(document.Parse({reinterpret_cast<const uint8_t*>(range.data), range.count}), ImportError::InvalidRange, "invalid image buffer range at parse boundary");
        constexpr char missing[] = R"({"asset":{"version":"2.0"},"buffers":[{"uri":"missing.bin","byteLength":4}],"bufferViews":[{"buffer":0,"byteLength":4}],"images":[{"bufferView":0,"mimeType":"image/png"}],"textures":[{"source":0}],"materials":[{"normalTexture":{"index":0}}]})";
        Good(document.Parse({reinterpret_cast<const uint8_t*>(missing), sizeof(missing) - 1}), "parse missing image buffer");
        rejected(ConvertImportScene(document, options, scene, geometry, &textures), ImportError::BufferUnavailable, "missing image buffer");
    }
    void LargeMetadata()
    {
        EncodedImage png;
        png.Png();
        Text base64, materials, extra, json;
        png.Base64(base64);
        char padding[3073]; memset(padding, 'x', sizeof(padding) - 1); padding[sizeof(padding) - 1] = 0;
        materials.Append(R"([{"emissiveTexture":{"index":0},"extensions":{"NV_materials_hair":{"ignored":"%s","melanin":0.37}}}])", padding);
        extra.Append(R"("textures":[{"source":0,"extensions":{"NV_texture_swizzle":{"ignored":"%s","options":[{"source":0,"channels":[0]}]}}}],"images":[{"uri":"data:image/png;base64,%s"}],"extensionsRequired":["NV_materials_hair","NV_texture_swizzle"],)", padding, base64.data);
        const int32_t one[]{0};
        Fixture(json, materials.data, {one, 1}, extra.data);
        ImportDocument document; Parse(json, document);
        RendererScene scene; ImportGeometry geometry; ImportTextures textures;
        Good(ConvertImportScene(document, Options(), scene, geometry, &textures), "large custom metadata has checked input-sized capacity");
        Require(scene.View().materials.data[0].values.hair.melanin == 0.37f && textures.Texture(0).swizzles.count == 1,
            "custom extension values beyond legacy cutoffs are retained");
    }
}

int main()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    DomainsAndValues();
    CustomMaterials();
    ExternalTextures();
    DdsSelection();
    TexturePaths();
    EmbeddedTextures();
    SwizzleSemantics();
    MetadataFailures();
    TextureFailures();
    LargeMetadata();
    AllocationFailures();
    printf("material import passed: %zu captured value/encoding comparisons, %zu explicit metadata/conversion rejections\n", comparisons, rejections);
    FinishImportMaterialReference();
    return 0;
}
