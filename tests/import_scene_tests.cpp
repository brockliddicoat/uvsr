#include "renderer_import_scene.h"
#include "renderer_scene_light.h"
#include "import_runtime_light_fixture.h"
#include "scene_light_names.h"

#include "import_scene_fixture.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;
    constexpr uint32_t invalid = InvalidSceneIndex;
    size_t nativeFixtures = 0, rejectionCount = 0, keyComparisons = 0;

    void Require(bool condition, const char* reason)
    {
        if (condition) return;
        fprintf(stderr, "scene import failed: %s\n", reason);
        exit(1);
    }
    void Good(ImportResult result, const char* reason)
    {
        if (result) return;
        fprintf(stderr, "scene import failed: %s: %s, object %u, index %zu, parser %u\n",
            reason, ImportErrorText(result.error), unsigned(result.object), result.index, result.parserCode);
        exit(1);
    }
    void Bad(ImportResult result, ImportError error, const char* reason)
    {
        if (result.error != error)
        {
            fprintf(stderr, "scene rejection failed: %s: expected %s, got %s, object %u, index %zu, parser %u\n",
                reason, ImportErrorText(error), ImportErrorText(result.error), unsigned(result.object), result.index, result.parserCode);
            exit(1);
        }
        if (error != ImportError::OutOfMemory) ++rejectionCount;
    }
    struct Text
    {
        char* data = static_cast<char*>(malloc(65536));
        size_t count = 0;
        Text() { Require(data != nullptr, "fixture text storage"); data[0] = 0; }
        ~Text() { free(data); }
        void Append(const char* format, ...)
        {
            va_list args; va_start(args, format);
            const int size = vsnprintf(data + count, 65536 - count, format, args);
            va_end(args);
            Require(size >= 0 && size_t(size) < 65536 - count, "fixture text capacity");
            count += size_t(size);
        }
        ArrayView<const uint8_t> Bytes() const noexcept { return {reinterpret_cast<const uint8_t*>(data), count}; }
    };
    struct Fixture
    {
        Text json, views, accessors;
        uint8_t bytes[8192]{};
        size_t byteCount = 0;
        uint32_t accessorCount = 0;
        uint32_t Add(const void* input, size_t size, size_t count, const char* type, uint32_t component = 5126)
        {
            byteCount = (byteCount + 3) & ~size_t(3);
            Require(size <= sizeof(bytes) - byteCount, "fixture byte capacity");
            if (size) memcpy(bytes + byteCount, input, size);
            views.Append("%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu}", accessorCount ? "," : "", byteCount, size);
            accessors.Append("%s{\"bufferView\":%u,\"componentType\":%u,\"count\":%zu,\"type\":\"%s\"}",
                accessorCount ? "," : "", accessorCount, component, count, type);
            byteCount += size;
            return accessorCount++;
        }
        void Build(const char* nodes, const char* extra = "", const char* animations = nullptr,
            const char* scenes = "[{\"nodes\":[0]}]", bool glb = false)
        {
            json.Append("{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":%s,\"nodes\":%s%s", scenes, nodes, extra);
            if (byteCount) json.Append(",\"buffers\":[{%s\"byteLength\":%zu}],\"bufferViews\":[%s],\"accessors\":[%s]",
                glb ? "" : "\"uri\":\"fixture.bin\",", byteCount, views.data, accessors.data);
            if (animations) json.Append(",\"animations\":%s", animations);
            json.Append("}");
            if (glb)
            {
                Text container;
                const size_t jsonBytes = (json.count + 3) & ~size_t(3), binBytes = (byteCount + 3) & ~size_t(3);
                const size_t size = 28 + jsonBytes + binBytes;
                Require(byteCount && size < 65536, "GLB fixture capacity");
                const uint32_t header[]{0x46546c67, 2, uint32_t(size), uint32_t(jsonBytes), 0x4e4f534a};
                memcpy(container.data, header, sizeof(header));
                memcpy(container.data + 20, json.data, json.count);
                memset(container.data + 20 + json.count, ' ', jsonBytes - json.count);
                const uint32_t binHeader[]{uint32_t(binBytes), 0x004e4942};
                memcpy(container.data + 20 + jsonBytes, binHeader, sizeof(binHeader));
                memcpy(container.data + 28 + jsonBytes, bytes, byteCount);
                memset(container.data + 28 + jsonBytes + byteCount, 0, binBytes - byteCount);
                memcpy(json.data, container.data, size); json.count = size;
            }
        }
        ArrayView<const uint8_t> Bytes() const noexcept { return {bytes, byteCount}; }
    };
    ImportSceneOptions Options()
    {
        ImportSceneOptions options;
        options.generation = 31;
        options.modelName = {"fixture.gltf", 12};
        options.modelPath = options.modelName;
        return options;
    }
    void Parse(const Fixture& fixture, ImportDocument& document)
    {
        Good(document.Parse(fixture.json.Bytes()), "parse fixture");
        if (fixture.byteCount)
        {
            ImportBufferInfo info; Good(document.BufferInfo(0, info), "fixture buffer info");
            if (!info.resident) Good(document.SupplyBuffer(0, fixture.Bytes()), "supply fixture bytes");
        }
    }
    void Name(const RendererSceneView& scene, RendererSceneString value, const char* expected)
    {
        const auto text = RendererSceneText(scene, value);
        Require(text.count == strlen(expected) && (!text.count || memcmp(text.data, expected, text.count) == 0), "node name");
    }
    void CompareNative(const Fixture& fixture, const RendererSceneView& scene, uint32_t expectedModeDefects = 0)
    {
        keyComparisons += CompareImportSceneReference({fixture.json.data, fixture.json.count}, fixture.Bytes(), scene, expectedModeDefects);
        ++nativeFixtures;
        printf("captured scene fixture %zu: %zu nodes, %zu lights, %zu cameras, %zu animations, %zu samplers, %u classified mode defects\n",
            nativeFixtures, scene.nodes.count, scene.lights.count, scene.cameras.count, scene.animations.count, scene.samplers.count, expectedModeDefects);
    }
    void Convert(Fixture& fixture, RendererScene& scene, ImportGeometry& geometry)
    {
        ImportDocument document; Parse(fixture, document);
        Good(ConvertImportScene(document, Options(), scene, geometry), "convert scene fixture");
        document.Reset();
        Require(scene.IsPublished(), "scene survives parser destruction");
    }

    void LightsAndCameras()
    {
        Fixture fixture;
        const float triangle[]{0,0,0, 1,0,0, 0,1,0};
        fixture.Add(triangle, sizeof(triangle), 3, "VEC3");
        fixture.Build(R"([{"name":"occupied","mesh":0,"camera":0,"extensions":{"KHR_lights_punctual":{"light":0}},"translation":[2,3,4],"children":[1]},
            {"name":"authored child","camera":1,"extensions":{"KHR_lights_punctual":{"light":1}},"translation":[1,0,0]},
            {"camera":2,"extensions":{"KHR_lights_punctual":{"light":2}}}, {"camera":3}])",
            R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
            "cameras":[{"name":"perspective","type":"perspective","perspective":{"znear":0.1,"zfar":700,"yfov":0.9,"aspectRatio":1.6}},
            {"type":"perspective","perspective":{"znear":0.25,"yfov":1.1}},
            {"name":"","type":"orthographic","orthographic":{"znear":0,"zfar":50,"xmag":2,"ymag":3}},
            {"name":"unused","type":"perspective","perspective":{"znear":0.1,"yfov":1}}],
            "extensionsUsed":["KHR_lights_punctual"],"extensionsRequired":["KHR_lights_punctual"],
            "extensions":{"KHR_lights_punctual":{"lights":[{"type":"directional","intensity":3.5,"color":[0.1,0.3,0.7]},
            {"type":"point","intensity":70,"range":90,"color":[0.5,0.25,0.75]},
            {"name":"ignored source light name","type":"spot","intensity":25,"range":12,"spot":{"innerConeAngle":0.17,"outerConeAngle":0.63}}]}})",
            nullptr, R"([{"nodes":[0,2]}])");
        RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
        CompareNative(fixture, scene.View());
        Require(scene.View().nodes.count == 8 && scene.View().cameras.count == 3 && scene.View().lights.count == 3, "occupied node children and unselected definition");
    }
    void CameraNamesAndDefaults()
    {
        Fixture fixture;
        fixture.Build(R"([{"camera":0,"name":"kept"},{"camera":1},{"camera":2},{"camera":3,"name":"replaced"},
            {"extensions":{"KHR_lights_punctual":{"light":0}}},{"extensions":{"KHR_lights_punctual":{"light":1}}},
            {"extensions":{"KHR_lights_punctual":{"light":2}}}])",
            R"(,"cameras":[{"type":"perspective","perspective":{"znear":0.1,"yfov":1}},
            {"type":"perspective","perspective":{"znear":0.1,"yfov":1}},
            {"type":"perspective","perspective":{"znear":0.1,"yfov":1}},
            {"name":"","type":"perspective","perspective":{"znear":0.1,"yfov":1}}],
            "extensionsUsed":["KHR_lights_punctual"],"extensions":{"KHR_lights_punctual":{"lights":[
            {"type":"directional"},{"type":"point"},{"type":"spot","spot":{}}]}})", nullptr, R"([{"nodes":[0,1,2,3,4,5,6]}])");
        RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
        CompareNative(fixture, scene.View());
        const auto view = scene.View();
        Name(view, view.nodes.data[1].name, "kept"); Name(view, view.nodes.data[2].name, "Camera1");
        Name(view, view.nodes.data[3].name, "Camera2"); Name(view, view.nodes.data[4].name, "");
        Require(view.lights.data[2].values.innerAngle == 0 && view.lights.data[2].values.outerAngle == 45, "default cone degrees");
    }
    void AnimationValues()
    {
        Fixture fixture;
        const float times[]{0,2.5f}, translation[]{1,2,3,4,5,6}, rotation[]{0,0,0,1,0,0.6f,0,0.8f}, scale[]{1,1,1,2,3,4};
        const float cubic[]{-1,-2,-3, 1,2,3, 4,5,6, -7,-8,-9, 7,8,9, 10,11,12};
        fixture.Add(times, sizeof(times), 2, "SCALAR"); fixture.Add(translation, sizeof(translation), 2, "VEC3");
        fixture.Add(rotation, sizeof(rotation), 2, "VEC4"); fixture.Add(scale, sizeof(scale), 2, "VEC3");
        fixture.Add(cubic, sizeof(cubic), 6, "VEC3");
        fixture.Build(R"([{"name":"first","children":[1],"translation":[8,9,10]},{"name":"child"}])", "",
            R"([{"name":"walk","samplers":[{"input":0,"output":1},{"input":0,"output":2},{"input":0,"output":3,"interpolation":"STEP"},
            {"input":0,"output":4,"interpolation":"CUBICSPLINE"}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}},
            {"sampler":1,"target":{"node":0,"path":"rotation"}},{"sampler":2,"target":{"node":0,"path":"scale"}},
            {"sampler":3,"target":{"node":1,"path":"translation"}}]},
            {"name":"still","samplers":[{"input":0,"output":3}],"channels":[{"sampler":0,"target":{"node":1,"path":"scale"}}]}])");
        RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
        CompareNative(fixture, scene.View());
        const auto view = scene.View();
        Require(view.samplers.count == 5 && view.channels.count == 5 && view.keyframes.count == 10 &&
            view.samplers.data[1].interpolation == RendererSceneInterpolation::Slerp &&
            view.samplers.data[2].interpolation == RendererSceneInterpolation::Step &&
            view.samplers.data[3].interpolation == RendererSceneInterpolation::HermiteSpline, "interpolation map");
        Require(view.nodes.data[1].world.translation[0] == 8 && view.nodes.data[2].world.translation[0] == 8, "metadata does not restore playback");
        memset(fixture.json.data, 'x', fixture.json.count); memset(fixture.bytes, 0xcc, fixture.byteCount);
        Require(view.keyframes.data[6].inTangent.z == -3 && view.keyframes.data[7].outTangent.z == 12 &&
            view.keyframes.data[1].value.z == 6, "keyframes survive caller and parser destruction");
        Name(view, view.nodes.data[4].name, "walk");
    }
    void SharedAndReorderedSamplers()
    {
        const float times[]{0,1}, translation[]{1,2,3,4,5,6}, rotation[]{0,0,0,1,0,0,1,0};
        for (uint32_t reordered = 0; reordered < 2; ++reordered)
        {
            Fixture fixture;
            fixture.Add(times, sizeof(times), 2, "SCALAR"); fixture.Add(translation, sizeof(translation), 2, "VEC3");
            fixture.Add(rotation, sizeof(rotation), 2, "VEC4");
            fixture.Build(R"([{"name":"first"},{"name":"second"}])", "", reordered ?
                R"([{"samplers":[{"input":0,"output":2},{"input":0,"output":1}],"channels":[
                {"sampler":1,"target":{"node":0,"path":"translation"}},{"sampler":0,"target":{"node":0,"path":"rotation"}}]}])" :
                R"([{"samplers":[{"input":0,"output":1}],"channels":[
                {"sampler":0,"target":{"node":0,"path":"translation"}},{"sampler":0,"target":{"node":1,"path":"scale"}}]}])",
                R"([{"nodes":[0,1]}])");
            RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
            CompareNative(fixture, scene.View(), reordered ? 2 : 0);
            const auto view = scene.View();
            Require(view.channels.data[0].samplerIndex == 0 && view.channels.data[1].samplerIndex == reordered &&
                view.samplers.data[0].interpolation == RendererSceneInterpolation::Linear, "actual sampler references and sharing");
            if (reordered) Require(view.samplers.data[1].interpolation == RendererSceneInterpolation::Slerp, "rotation sampler follows channel reference");
        }
    }

    void SharedPlacements()
    {
        Fixture fixture;
        fixture.Build(R"([{"name":"left","camera":0,"extensions":{"KHR_lights_punctual":{"light":0}},"translation":[-4,0,0]},
            {"name":"right","camera":0,"extensions":{"KHR_lights_punctual":{"light":0}},"translation":[4,0,0]}])",
            R"(,"cameras":[{"type":"perspective","perspective":{"znear":0.1,"yfov":1}}],
            "extensionsUsed":["KHR_lights_punctual"],"extensions":{"KHR_lights_punctual":{"lights":[{"type":"point","intensity":7}]}})",
            nullptr, R"([{"nodes":[0,1]}])");
        const auto defect = ReadImportSceneDefectReference({fixture.json.data, fixture.json.count}, fixture.Bytes(), false);
        const uint32_t staleOwners = defect.staleOwners;
        Require(staleOwners == 2, "retained shared camera/light leaves point at the last placement");
        RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
        const auto view = scene.View();
        Require(view.nodes.count == 5 && view.cameras.count == 2 && view.lights.count == 2, "per-placement camera/light counts");
        for (uint32_t i = 0; i < 2; ++i)
        {
            const uint32_t cameraNode = 1 + i * 2, lightNode = 2 + i * 2;
            Require(view.cameras.data[i].nodeIndex == cameraNode && view.nodes.data[cameraNode].leafIndex == i &&
                view.lights.data[i].nodeIndex == lightNode && view.nodes.data[lightNode].leafIndex == i, "shared-source reciprocal links");
            Require(view.nodes.data[cameraNode].world.translation[0] == (i ? 4 : -4) &&
                view.nodes.data[lightNode].world.translation[0] == (i ? 4 : -4) && view.lights.data[i].values.intensity == 7, "independent placement transforms");
            Name(view, view.nodes.data[cameraNode].name, i ? "right" : "left");
        }
        printf("retained shared-leaf defect: %u stale owners; candidate has four reciprocal placements\n", staleOwners);
    }
    void SkinPlacements()
    {
        const float positions[]{0,0,0, 1,0,0, 0,1,0}, normals[]{0,0,1, 0,0,1, 0,0,1};
        const uint16_t joints[12]{};
        const float weights[]{1,0,0,0, 1,0,0,0, 1,0,0,0};
        const float bind[]{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        for (uint32_t placement = 1; placement <= 3; ++placement)
        {
            Fixture fixture;
            fixture.Add(positions, sizeof(positions), 3, "VEC3"); fixture.Add(normals, sizeof(normals), 3, "VEC3");
            fixture.Add(joints, sizeof(joints), 3, "VEC4", 5123); fixture.Add(weights, sizeof(weights), 3, "VEC4");
            fixture.Add(bind, sizeof(bind), 1, "MAT4");
            Text nodes;
            nodes.Append(R"([{"name":"skinned","mesh":0,"skin":0,"children":[1],"translation":[3,4,5]%s%s},{"name":"joint"}])",
                placement & 1 ? ",\"camera\":0" : "", placement & 2 ? ",\"extensions\":{\"KHR_lights_punctual\":{\"light\":0}}" : "");
            fixture.Build(nodes.data, R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"JOINTS_0":2,"WEIGHTS_0":3}}]}],
                "skins":[{"inverseBindMatrices":4,"joints":[1]}],
                "cameras":[{"name":"camera","type":"perspective","perspective":{"znear":0.1,"yfov":1}}],
                "extensionsUsed":["KHR_lights_punctual"],"extensions":{"KHR_lights_punctual":{"lights":[{"type":"point"}]}})");
            const auto defect = ReadImportSceneDefectReference({fixture.json.data, fixture.json.count}, fixture.Bytes(), true);
            const uint32_t oldCameras = defect.cameras, oldLights = defect.lights;
            Require(oldCameras == 0 && oldLights == (placement == 3 ? 1u : 0u), "retained deferred skin replaces the first auxiliary leaf");
            RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
            const auto view = scene.View();
            const uint32_t cameraCount = placement & 1 ? 1 : 0, lightCount = placement & 2 ? 1 : 0;
            Require(view.instances.count == 1 && view.instances.data[0].nodeIndex == 1 && view.cameras.count == cameraCount &&
                view.lights.count == lightCount && view.nodes.count == 3 + cameraCount + lightCount, "skin retains every co-located leaf");
            Require(view.nodes.data[1].leafKind == RendererSceneLeafKind::Instance && view.instances.data[0].joints.count == 1, "skin owns authored node");
            Name(view, view.nodes.data[1].name, "skinned");
            const uint32_t jointNode = 2 + cameraCount + lightCount;
            Require(view.joints.data[0].nodeIndex == jointNode && view.nodes.data[jointNode].parentIndex == 1, "skin joint target follows inserted leaves");
            for (uint32_t n = 2; n < jointNode; ++n)
                Require(view.nodes.data[n].parentIndex == 1 && !view.nodes.data[n].hasLocalTransform &&
                    view.nodes.data[n].world.translation[0] == 3 && view.nodes.data[n].world.translation[1] == 4 &&
                    view.nodes.data[n].world.translation[2] == 5 && view.nodes.data[n].nextSiblingIndex == n + 1, "identity auxiliary nodes precede authored joint");
            printf("retained skin leaf defect %u: %u cameras/%u lights; candidate %u/%u\n", placement, oldCameras, oldLights, cameraCount, lightCount);
        }
    }
    void IgnoredTracksAndUnusedSamplers()
    {
        const float times[]{0,1}, positions[]{0,0,0, 1,0,0, 0,1,0}, weights[]{0,1};
        {
            Fixture fixture;
            fixture.Add(times, sizeof(times), 2, "SCALAR"); fixture.Add(positions, sizeof(positions), 3, "VEC3");
            fixture.Add(weights, sizeof(weights), 2, "SCALAR");
            fixture.Build(R"([{"mesh":0},{"name":"outside"}])",
                R"(,"meshes":[{"weights":[0],"primitives":[{"attributes":{"POSITION":1},"targets":[{"POSITION":1}]}]}])",
                R"([{"name":"ignored tracks","samplers":[{"input":0,"output":2},{"input":0,"output":1},{"input":0,"output":1}],"channels":[
                {"sampler":0,"target":{"node":0,"path":"weights"}},{"sampler":1,"target":{"node":1,"path":"translation"}},
                {"sampler":2,"target":{"path":"translation"}}]},{"name":"empty","samplers":[],"channels":[]}])");
            RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
            CompareNative(fixture, scene.View());
            const auto view = scene.View();
            Require(view.animations.count == 2 && !view.channels.count && !view.samplers.count && !view.keyframes.count &&
                view.animations.data[0].duration == 0, "ignored tracks retain empty animation metadata without playback");
        }
        {
            Fixture fixture;
            const float translation[]{1,2,3,4,5,6};
            uint32_t nanBits = 0x7fc01234; float unused[2]; memcpy(&unused[0], &nanBits, 4); unused[1] = 1;
            fixture.Add(times, sizeof(times), 2, "SCALAR"); fixture.Add(translation, sizeof(translation), 2, "VEC3");
            fixture.Add(unused, sizeof(unused), 2, "SCALAR");
            fixture.Build(R"([{}])", "", R"([{"name":"unused samplers","samplers":[{"input":2,"output":2},{"input":0,"output":1},{"input":2,"output":2}],
                "channels":[{"sampler":1,"target":{"node":0,"path":"translation"}}]}])");
            RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
            const auto view = scene.View();
            Require(view.samplers.count == 1 && view.channels.count == 1 && view.keyframes.count == 2 &&
                view.channels.data[0].samplerIndex == 0 && view.keyframes.data[1].value.z == 6, "unused samplers are not decoded or published");
            // the old sampler loop would read channels[1] and channels[2].
            // this malformed control implementation is inspected, never executed.
            printf("unused sampler fixture: three source samplers, one channel, one owned canonical sampler\n");
        }
    }
    void RotationModes()
    {
        Fixture fixture;
        const float times[]{0,1}, rotations[]{0,0,0,1, 0,0,1,0};
        const float cubic[]{1,2,3,4, 0,0,0,1, 5,6,7,8, -1,-2,-3,-4, 0,0,1,0, -5,-6,-7,-8};
        fixture.Add(times, sizeof(times), 2, "SCALAR"); fixture.Add(rotations, sizeof(rotations), 2, "VEC4");
        fixture.Add(cubic, sizeof(cubic), 6, "VEC4");
        fixture.Build(R"([{"children":[1]},{}])", "", R"([{"samplers":[{"input":0,"output":1,"interpolation":"STEP"},
            {"input":0,"output":2,"interpolation":"CUBICSPLINE"}],"channels":[{"sampler":0,"target":{"node":0,"path":"rotation"}},
            {"sampler":1,"target":{"node":1,"path":"rotation"}}]}])");
        RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
        CompareNative(fixture, scene.View());
        Require(scene.View().keyframes.data[3].outTangent.w == -8, "cubic rotation keeps tangent w");
    }
    void RejectParse(const Fixture& fixture, const char* reason)
    {
        Fixture prior; prior.Build(R"([{"name":"prior"}])");
        ImportDocument document; Parse(prior, document);
        Bad(document.Parse(fixture.json.Bytes()), ImportError::InvalidData, reason);
        RendererScene scene; ImportGeometry geometry;
        Good(ConvertImportScene(document, Options(), scene, geometry), "failed parse retains prior document");
        Name(scene.View(), scene.View().nodes.data[1].name, "prior");
    }
    void RejectConversion(const Fixture& fixture, ImportError error, const char* reason)
    {
        ImportDocument document; Parse(fixture, document);
        RendererScene scene; ImportGeometry geometry;
        Bad(ConvertImportScene(document, Options(), scene, geometry), error, reason);
        Require(!scene.StorageBytes() && !scene.IsPublished() && !geometry.StorageBytes(), "failed conversion publishes no partial output");
    }
    void MalformedMetadata()
    {
        const char* badCameras[]{
            R"({"type":"perspective","name":7,"perspective":{"znear":0.1,"yfov":1}})",
            R"({"type":"perspective","perspective":{"znear":1e100,"yfov":1}})",
            R"({"type":"perspective","perspective":{"znear":0.1,"yfov":1e100}})",
            R"({"type":"perspective","perspective":{"znear":0.1,"yfov":1,"zfar":"bad"}})",
            R"({"type":"perspective","perspective":{"znear":0.1,"yfov":1,"aspectRatio":null}})",
            R"({"type":"orthographic","orthographic":{"znear":0,"zfar":5,"xmag":1e100,"ymag":2}})",
            R"({"type":"fisheye"})"};
        for (const auto* camera : badCameras)
        {
            Fixture fixture; Text extra; extra.Append(",\"cameras\":[%s]", camera);
            fixture.Build(R"([{"camera":0}])", extra.data); RejectParse(fixture, "invalid camera scalar, shape or type");
        }
        const char* badLights[]{
            R"({"type":"point","intensity":"bad"})", R"({"type":"point","intensity":1e100})",
            R"({"type":"point","range":null})", R"({"type":"point","range":1e100})",
            R"({"type":"directional","color":[1,1]})", R"({"type":"directional","color":[1,1,1e100]})",
            R"({"type":"spot","spot":{"innerConeAngle":"bad"}})", R"({"type":"spot","spot":{"outerConeAngle":1e100}})",
            R"({"type":"area"})"};
        for (const auto* light : badLights)
        {
            Fixture fixture; Text extra;
            extra.Append(",\"extensionsUsed\":[\"KHR_lights_punctual\"],\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[%s]}}", light);
            fixture.Build(R"([{"extensions":{"KHR_lights_punctual":{"light":0}}}])", extra.data);
            RejectParse(fixture, "invalid light scalar, shape or type");
        }
        const char* badNodes[]{R"([{"extensions":{"KHR_lights_punctual":7}}])",
            R"([{"extensions":{"KHR_lights_punctual":{}}}])", R"([{"extensions":{"KHR_lights_punctual":{"light":-1}}}])",
            R"([{"extensions":{"KHR_lights_punctual":{"light":"0"}}}])", R"([{"extensions":7}])",
            R"([{"camera":18446744073709551615}])", R"([{"extensions":{"KHR_lights_punctual":{"light":18446744073709551615}}}])"};
        for (const auto* nodes : badNodes)
        { Fixture fixture; fixture.Build(nodes); RejectParse(fixture, "invalid node light extension"); }
        const char* badAnimations[]{
            R"([{"samplers":[{"input":0,"output":0,"interpolation":7}],"channels":[]}])",
            R"([{"samplers":[{"input":0,"output":0,"interpolation":"CATMULLROM"}],"channels":[]}])",
            R"([{"samplers":[],"channels":[{"sampler":0,"target":{"node":0,"path":"other"}}]}])",
            R"([{"samplers":[],"channels":[{"sampler":0,"target":{"node":18446744073709551615,"path":"translation"}}]}])"};
        for (const auto* animation : badAnimations)
        { Fixture fixture; fixture.Build("[{}]", "", animation); RejectParse(fixture, "invalid animation mode or path"); }
    }
    void InvalidSceneValues()
    {
        const char* cameras[]{R"({"type":"perspective","perspective":{"znear":0,"yfov":1}})",
            R"({"type":"perspective","perspective":{"znear":0.1,"yfov":0}})",
            R"({"type":"perspective","perspective":{"znear":0.1,"yfov":3.2}})",
            R"({"type":"perspective","perspective":{"znear":0.1,"yfov":1,"zfar":0.1}})",
            R"({"type":"perspective","perspective":{"znear":0.1,"yfov":1,"aspectRatio":0}})",
            R"({"type":"orthographic","orthographic":{"znear":-1,"zfar":5,"xmag":1,"ymag":2}})",
            R"({"type":"orthographic","orthographic":{"znear":1,"zfar":1,"xmag":1,"ymag":2}})",
            R"({"type":"orthographic","orthographic":{"znear":0,"zfar":5,"xmag":0,"ymag":2}})"};
        for (const auto* camera : cameras)
        {
            Fixture fixture; Text extra; extra.Append(",\"cameras\":[%s]", camera);
            fixture.Build(R"([{"camera":0}])", extra.data);
            RejectConversion(fixture, ImportError::InvalidData, "invalid projection domain");
        }
        const char* lights[]{R"({"type":"directional","intensity":-1})", R"({"type":"point","color":[1,-1,1]})",
            R"({"type":"point","range":0})", R"({"type":"spot","spot":{"innerConeAngle":-1}})",
            R"({"type":"spot","spot":{"outerConeAngle":0}})", R"({"type":"spot","spot":{"outerConeAngle":2}})",
            R"({"type":"spot","spot":{"innerConeAngle":0.5,"outerConeAngle":0.5}})"};
        for (const auto* light : lights)
        {
            Fixture fixture; Text extra;
            extra.Append(",\"extensionsUsed\":[\"KHR_lights_punctual\"],\"extensions\":{\"KHR_lights_punctual\":{\"lights\":[%s]}}", light);
            fixture.Build(R"([{"extensions":{"KHR_lights_punctual":{"light":0}}}])", extra.data);
            RejectConversion(fixture, ImportError::InvalidData, "invalid light domain");
        }
        { Fixture fixture; fixture.Build(R"([{"camera":0}])"); RejectConversion(fixture, ImportError::InvalidIndex, "missing camera definition"); }
        { Fixture fixture; fixture.Build(R"([{"extensions":{"KHR_lights_punctual":{"light":0}}}])");
          RejectConversion(fixture, ImportError::InvalidIndex, "missing light definition"); }
    }
    void InvalidAnimationData()
    {
        const float times[]{0,1}, values[]{1,2,3,4,5,6};
        const char* animations[]{
            R"([{"samplers":[{"input":0,"output":1}],"channels":[{"sampler":2,"target":{"node":0,"path":"translation"}}]}])",
            R"([{"samplers":[{"input":0,"output":1}],"channels":[{"sampler":0,"target":{"node":4,"path":"translation"}}]}])",
            R"([{"samplers":[{"input":9,"output":1}],"channels":[]}])",
            R"([{"samplers":[{"input":0,"output":9}],"channels":[]}])"};
        for (const auto* animation : animations)
        {
            Fixture fixture; fixture.Add(times, sizeof(times), 2, "SCALAR"); fixture.Add(values, sizeof(values), 2, "VEC3");
            fixture.Build("[{}]", "", animation); RejectConversion(fixture, ImportError::InvalidIndex, "animation reference");
        }
        const char* shapes[]{"VEC2", "VEC3", "VEC4", "SCALAR"};
        for (uint32_t shape = 0; shape < 4; ++shape)
        {
            Fixture fixture; const float input[]{0,0,0,0,1,1,1,1}, output[]{1,2,3,4,5,6,7,8};
            const uint32_t lanes = shape == 0 ? 2 : shape == 1 ? 3 : shape == 2 ? 4 : 1;
            fixture.Add(input, size_t(lanes) * 8, 2, shape == 3 ? "SCALAR" : shapes[shape]);
            fixture.Add(output, shape == 3 ? 32 : 24, 2, shape == 3 ? "VEC4" : "VEC3");
            fixture.Build("[{}]", "", R"([{"samplers":[{"input":0,"output":1}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}])");
            RejectConversion(fixture, ImportError::InvalidAccessor, "animation input/output shape");
        }
        for (uint32_t kind = 0; kind < 7; ++kind)
        {
            Fixture fixture; float input[]{0,1}, output[]{1,2,3,4,5,6};
            uint32_t nanBits = 0x7fc01234;
            if (kind == 0) input[0] = -1;
            if (kind == 1) input[1] = input[0];
            if (kind == 2) { input[0] = 2; input[1] = 1; }
            if (kind == 3) memcpy(&input[1], &nanBits, 4);
            if (kind == 4) memcpy(&output[1], &nanBits, 4);
            fixture.Add(input, sizeof(input), 2, "SCALAR");
            fixture.Add(output, kind == 5 ? 12 : sizeof(output), kind == 5 ? 1 : 2, "VEC3");
            fixture.Build("[{}]", "", kind == 6 ?
                R"([{"samplers":[{"input":0,"output":1,"interpolation":"CUBICSPLINE"}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}])" :
                R"([{"samplers":[{"input":0,"output":1}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}])");
            RejectConversion(fixture, kind < 3 ? ImportError::InvalidData : kind < 5 ? ImportError::NonFiniteValue : ImportError::InvalidAccessor,
                "animation time ordering, nonfinite values or key count");
        }
        {
            Fixture fixture; fixture.Add(times, sizeof(times), 2, "SCALAR"); fixture.Add(values, sizeof(values), 2, "VEC3");
            fixture.Build("[{}]", "", R"([{"samplers":[{"input":0,"output":1}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}},
                {"sampler":0,"target":{"node":0,"path":"translation"}}]}])");
            RejectConversion(fixture, ImportError::InvalidData, "duplicate target attribute");
        }
        {
            Fixture fixture; fixture.Add(times, sizeof(times), 2, "SCALAR"); fixture.Add(values, sizeof(values), 2, "VEC3");
            fixture.Build("[{}]", "", R"([{"samplers":[{"input":0,"output":1}],"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}},
                {"sampler":0,"target":{"node":0,"path":"rotation"}}]}])");
            RejectConversion(fixture, ImportError::InvalidAccessor, "shared sampler cannot satisfy incompatible target shapes");
        }
    }
    void OwnedGlbAndFailureTransactions()
    {
        const float triangle[]{0,0,0,1,0,0,0,1,0}, times[]{0,1}, rotation[]{0,0,0,1,0,0,1,0}, translation[]{1,2,3,4,5,6};
        const auto build = [&](Fixture& fixture, bool glb)
        {
            fixture.Add(triangle, sizeof(triangle), 3, "VEC3"); fixture.Add(times, sizeof(times), 2, "SCALAR");
            fixture.Add(rotation, sizeof(rotation), 2, "VEC4"); fixture.Add(translation, sizeof(translation), 2, "VEC3");
            fixture.Build(R"([{"name":"mesh","mesh":0,"camera":0,"extensions":{"KHR_lights_punctual":{"light":0}},"children":[1]},{"name":"child"}])",
                R"(,"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],
                "cameras":[{"name":"view","type":"perspective","perspective":{"znear":0.1,"zfar":10,"yfov":1,"aspectRatio":2}}],
                "extensionsUsed":["KHR_lights_punctual"],"extensionsRequired":["KHR_lights_punctual"],
                "extensions":{"KHR_lights_punctual":{"lights":[{"type":"spot","intensity":2,"spot":{}}]}})",
                R"([{"name":"owned","samplers":[{"input":1,"output":2},{"input":1,"output":3}],"channels":[
                {"sampler":0,"target":{"node":0,"path":"rotation"}},{"sampler":1,"target":{"node":1,"path":"translation"}}]}])",
                R"([{"nodes":[0]}])", glb);
        };
        const auto check = [](const RendererScene& scene)
        {
            const auto view = scene.View();
            Require(view.nodes.count == 6 && view.cameras.count == 1 && view.lights.count == 1 && view.animations.count == 1 &&
                view.samplers.count == 2 && view.channels.count == 2 && view.keyframes.count == 4, "complete scene transaction counts");
            Require(view.cameras.data[0].nodeIndex == 2 && view.lights.data[0].nodeIndex == 3 && view.channels.data[1].nodeIndex == 4 &&
                view.keyframes.data[3].value.z == 6 && view.keyframes.data[1].value.z == 1, "complete scene transaction values and targets");
            Name(view, view.nodes.data[5].name, "owned");
        };
        {
            Fixture fixture; build(fixture, true);
            RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
            CompareNative(fixture, scene.View()); check(scene);
            memset(fixture.json.data, 0xee, fixture.json.count); memset(fixture.bytes, 0, fixture.byteCount);
            check(scene);
            RendererScene moved(static_cast<RendererScene&&>(scene)); ImportGeometry movedGeometry(static_cast<ImportGeometry&&>(geometry));
            Require(!scene.StorageBytes() && !geometry.StorageBytes(), "move clears source owners"); check(moved);
            moved = static_cast<RendererScene&&>(moved); movedGeometry = static_cast<ImportGeometry&&>(movedGeometry); check(moved);
            scene = static_cast<RendererScene&&>(moved); geometry = static_cast<ImportGeometry&&>(movedGeometry); check(scene);
            scene.Reset(); scene.Reset(); geometry.Reset(); geometry.Reset();
        }
        Fixture fixture; build(fixture, false);
        ImportDocument document; Parse(fixture, document);
        RendererScene scene; ImportGeometry geometry;
        Good(ConvertImportScene(document, Options(), scene, geometry), "measure scene capacities"); check(scene);
        const size_t scratch = geometry.ConversionScratchBytes(), storage = geometry.StorageBytes();
        const auto* nodeStorage = scene.View().nodes.data;
        const size_t sceneBytes = scene.StorageBytes();
        Bad(ConvertImportScene(document, Options(), scene, geometry), ImportError::InvalidState, "nonempty outputs remain unchanged");
        Require(scene.View().nodes.data == nodeStorage && scene.StorageBytes() == sceneBytes && geometry.StorageBytes() == storage, "nonempty owner identity preserved");
        check(scene); scene.Reset(); geometry.Reset();
        auto exact = Options(); exact.maxScratchBytes = scratch; exact.maxGeometryBytes = storage;
        Good(ConvertImportScene(document, exact, scene, geometry), "exact scene storage budgets"); check(scene); scene.Reset(); geometry.Reset();
        --exact.maxScratchBytes;
        Bad(ConvertImportScene(document, exact, scene, geometry), ImportError::Workspace, "one-byte scratch shortage");
        Require(!scene.StorageBytes() && !geometry.StorageBytes(), "scratch failure leaves clean outputs");
        exact.maxScratchBytes = scratch; --exact.maxGeometryBytes;
        Bad(ConvertImportScene(document, exact, scene, geometry), ImportError::Capacity, "one-byte geometry shortage");
        Require(!scene.StorageBytes() && !geometry.StorageBytes(), "geometry failure leaves clean outputs");
        ImportDocument missing;
        Good(missing.Parse(fixture.json.Bytes()), "parse unresolved animated scene");
        Bad(ConvertImportScene(missing, Options(), scene, geometry), ImportError::BufferUnavailable, "missing animated source buffer");
        Require(!scene.StorageBytes() && !geometry.StorageBytes(), "missing buffer leaves clean outputs");
        Good(missing.SupplyBuffer(0, fixture.Bytes()), "supply missing source buffer");
        Good(ConvertImportScene(missing, Options(), scene, geometry), "retry after missing buffer supply"); check(scene); scene.Reset(); geometry.Reset();
        size_t parseFaults = 0, conversionFaults = 0, sceneFaults = 0;
        for (int64_t ordinal = 0; ordinal < 40; ++ordinal)
        {
            SetImportAllocationFailureCountdown(ordinal);
            const auto result = document.Parse(fixture.json.Bytes());
            SetImportAllocationFailureCountdown(-1);
            if (result) { Good(document.SupplyBuffer(0, fixture.Bytes()), "supply successful replacement parse"); break; }
            Bad(result, ImportError::OutOfMemory, "scene metadata parse allocation fault"); ++parseFaults;
            Good(ConvertImportScene(document, Options(), scene, geometry), "failed parse retains supplied bytes and all metadata");
            check(scene); scene.Reset(); geometry.Reset();
        }
        Require(parseFaults >= 4 && parseFaults < 40, "all parse allocations tested");
        for (int64_t ordinal = 0; ordinal < 100; ++ordinal)
        {
            SetImportAllocationFailureCountdown(ordinal);
            const auto result = ConvertImportScene(document, Options(), scene, geometry);
            SetImportAllocationFailureCountdown(-1);
            if (result) { check(scene); scene.Reset(); geometry.Reset(); break; }
            Bad(result, ImportError::OutOfMemory, "scene conversion allocation fault"); ++conversionFaults;
            Require(!scene.StorageBytes() && !geometry.StorageBytes(), "conversion allocation failure leaves clean outputs");
            Good(ConvertImportScene(document, Options(), scene, geometry), "retry after conversion allocation failure");
            check(scene); scene.Reset(); geometry.Reset();
        }
        Require(conversionFaults > 15 && conversionFaults < 100, "all conversion allocations tested");
        for (uint32_t ordinal = 1; ordinal < 40; ++ordinal)
        {
            SetRendererSceneAllocationFailure(ordinal);
            const auto result = ConvertImportScene(document, Options(), scene, geometry);
            SetRendererSceneAllocationFailure(0);
            if (result) { check(scene); scene.Reset(); geometry.Reset(); break; }
            Bad(result, ImportError::OutOfMemory, "canonical scene allocation fault"); ++sceneFaults;
            Require(!scene.StorageBytes() && !geometry.StorageBytes(), "canonical allocation failure leaves clean outputs");
            Good(ConvertImportScene(document, Options(), scene, geometry), "retry after canonical allocation failure");
            check(scene); scene.Reset(); geometry.Reset();
        }
        Require(sceneFaults >= 15 && sceneFaults < 40, "all canonical record allocations tested");
        printf("scene allocation failures: parse %zu, conversion %zu, canonical %zu, each retried; scratch %zu, geometry %zu, scene %zu bytes\n",
            parseFaults, conversionFaults, sceneFaults, scratch, storage, sceneBytes);
    }
    void RuntimeLightNodes()
    {
        size_t cases = 0;
        for (uint32_t shape = 0; shape < 3; ++shape)
        for (uint32_t animationCount = 0; animationCount < 3; ++animationCount)
        for (uint32_t container = 0; container < 2; ++container)
        {
            const bool glb = container != 0;
            Fixture fixture;
            const float triangle[]{0,0,0, 1,0,0, 0,1,0}, times[]{0,1}, translation[]{0,0,0, 2,3,4};
            fixture.Add(triangle, sizeof(triangle), 3, "VEC3");
            fixture.Add(times, sizeof(times), 2, "SCALAR");
            fixture.Add(translation, sizeof(translation), 2, "VEC3");
            const char* nodes = shape ? R"([{"name":"SUN","mesh":0,"camera":0,"extensions":{"KHR_lights_punctual":{"light":0}},"translation":[2,3,4],"children":[1,2]},
                {"name":"Sun","extensions":{"KHR_lights_punctual":{"light":1}}},
                {"name":"flashlight_1","extensions":{"KHR_lights_punctual":{"light":0}}},
                {"name":"other sun","extensions":{"KHR_lights_punctual":{"light":2}}},
                {"name":"HDRI_SKY","extensions":{"KHR_lights_punctual":{"light":3}}}])" : "[]";
            Text extra;
            extra.Append(R"(,"meshes":[{"name":"triangle","primitives":[{"attributes":{"POSITION":0}}]}],
                "cameras":[{"name":"view","type":"perspective","perspective":{"znear":0.1,"yfov":1}}],
                "extensionsUsed":["KHR_lights_punctual"],"extensions":{"KHR_lights_punctual":{"lights":[
                {"type":"point","intensity":2},{"type":"%s","intensity":3.5,"color":[0.1,0.3,0.7]},
                {"type":"%s","intensity":7,"color":[0.5,0.25,0.75]},{"type":"spot","spot":{}}]}})",
                shape == 1 ? "directional" : "point", shape == 1 ? "directional" : "point");
            Text animations;
            animations.Append("[");
            for (uint32_t a = 0; a < animationCount; ++a)
                animations.Append(shape ? R"(%s{"name":"move%u","samplers":[{"input":1,"output":2}],"channels":[{"sampler":0,"target":{"node":1,"path":"translation"}}]})" :
                    R"(%s{"name":"empty%u","samplers":[],"channels":[]})", a ? "," : "", a);
            animations.Append("]");
            fixture.Build(nodes, extra.data, animationCount ? animations.data : nullptr, shape ? R"([{"nodes":[0,3,4]}])" : R"([{"nodes":[]}])", glb);
            ImportDocument document; Parse(fixture, document);
            RendererScene control, candidate; ImportGeometry controlGeometry, candidateGeometry;
            Good(ConvertImportScene(document, Options(), control, controlGeometry), "runtime node control");
            auto options = Options(); options.runtimeLights = tests::RuntimeLightFixtureOptions();
            ImportRuntimeLightIds ids{{123, 45}, {456, 78}};
            Good(ConvertImportScene(document, options, candidate, candidateGeometry, nullptr, &ids), "runtime node conversion");
            const auto before = control.View(), after = candidate.View();
            const uint32_t added = shape == 1 ? 1 : 2;
            Require(after.nodes.count == before.nodes.count + added && after.lights.count == before.lights.count + added &&
                after.cameras.count == before.cameras.count && after.animations.count == before.animations.count &&
                after.channels.count == before.channels.count && after.keyframes.count == before.keyframes.count,
                "direct runtime capacities retain all existing records");
            Require(ids.sun.generation == options.generation && ids.flashlight.generation == options.generation &&
                ids.sun.index == (shape == 1 ? 1u : uint32_t(before.lights.count)) && ids.flashlight.index == after.lights.count - 1,
                "direct runtime IDs use the first authored directional or appended fallback");
            const uint32_t firstRuntime = uint32_t(before.nodes.count);
            for (uint32_t n = 0; n < before.nodes.count; ++n)
            {
                const auto& a = before.nodes.data[n]; const auto& b = after.nodes.data[n];
                Require(before.preorder.data[n] == after.preorder.data[n] && a.parentIndex == b.parentIndex && a.leafKind == b.leafKind &&
                    a.leafIndex == b.leafIndex && a.hasLocalTransform == b.hasLocalTransform &&
                    !memcmp(&a.transform, &b.transform, sizeof(a.transform)) && !memcmp(&a.local, &b.local, sizeof(a.local)) &&
                    !memcmp(&a.world, &b.world, sizeof(a.world)) && !memcmp(&a.previousLocal, &b.previousLocal, sizeof(a.previousLocal)) &&
                    !memcmp(&a.previousWorld, &b.previousWorld, sizeof(a.previousWorld)), "direct existing IDs, local/world and previous state");
                const uint32_t expectedChild = n == before.root && a.firstChildIndex == invalid ? firstRuntime : a.firstChildIndex;
                const uint32_t expectedSibling = a.parentIndex == before.root && a.nextSiblingIndex == invalid ? firstRuntime : a.nextSiblingIndex;
                Require(b.firstChildIndex == expectedChild && b.nextSiblingIndex == expectedSibling, "authored root and animation tail links");
                auto expectedName = RendererSceneText(before, a.name);
                if (a.leafKind == RendererSceneLeafKind::Light) expectedName = NormalizeSceneLightName(expectedName);
                const auto actualName = RendererSceneText(after, b.name);
                Require(actualName.count == expectedName.count && (!actualName.count || !memcmp(actualName.data, expectedName.data, actualName.count)),
                    "normalize only the actual light node, including empty auxiliary names");
            }
            for (uint32_t l = 0; l < before.lights.count; ++l)
            {
                auto values = before.lights.data[l].values;
                if (l == ids.sun.index) { values.irradiance = 8; values.angularSize = 0.2f; }
                Require(before.lights.data[l].nodeIndex == after.lights.data[l].nodeIndex && before.lights.data[l].kind == after.lights.data[l].kind &&
                    !memcmp(&values, &after.lights.data[l].values, sizeof(values)), "only selected sun irradiance and angular size change");
            }
            for (uint32_t c = 0; c < before.channels.count; ++c)
                Require(before.channels.data[c].nodeIndex == after.channels.data[c].nodeIndex &&
                    before.channels.data[c].samplerIndex == after.channels.data[c].samplerIndex &&
                    before.channels.data[c].attribute == after.channels.data[c].attribute, "runtime insertion preserves animation references");
            Require(after.contentRevision == before.contentRevision && after.lightRevision == before.lightRevision &&
                after.transformRevision == before.transformRevision &&
                after.previousTransformRevision == before.previousTransformRevision + uint64_t(shape == 0) &&
                after.previousInstanceTransformRevision == before.previousInstanceTransformRevision,
                "runtime lights join the initial publication without edit revisions");
            if (shape)
            {
                Require(after.instances.data[0].nodeIndex == 1 && after.cameras.data[0].nodeIndex == 2 && after.lights.data[0].nodeIndex == 3 &&
                    after.nodes.data[1].firstChildIndex == 2 && after.nodes.data[2].nextSiblingIndex == 3 && after.nodes.data[3].nextSiblingIndex == 4,
                    "mesh camera light and authored-child ordering");
                Name(after, after.nodes.data[1].name, "SUN");
                Name(after, after.nodes.data[4].name, "sun_1");
            }
            const auto* sun = FindRendererSceneLight(after, ids.sun);
            const auto* flashlight = FindRendererSceneLight(after, ids.flashlight);
            Require(sun && flashlight && flashlight->nodeIndex == after.nodes.count - 1 &&
                after.nodes.data[flashlight->nodeIndex].parentIndex == after.root &&
                after.nodes.data[flashlight->nodeIndex].nextSiblingIndex == invalid, "flashlight appended despite authored duplicate");
            Name(after, after.nodes.data[flashlight->nodeIndex].name, "flashlight_1");
            if (added == 2)
            {
                const auto& node = after.nodes.data[sun->nodeIndex];
                Require(sun->nodeIndex == firstRuntime && node.parentIndex == after.root && node.nextSiblingIndex == flashlight->nodeIndex &&
                    node.world.translation[0] == 2 && node.world.translation[1] == 3 && node.world.translation[2] == 4 &&
                    node.world.linear[0] == -1 && node.world.linear[4] == 1 && node.world.linear[8] == -1, "fallback sun root-local pose");
            }
            for (size_t g = 0; g < controlGeometry.BufferCount(); ++g)
            {
                const auto a = controlGeometry.Buffer(g), b = candidateGeometry.Buffer(g);
                Require(a.indices.count == b.indices.count && a.vertices.count == b.vertices.count &&
                    (!a.indices.count || !memcmp(a.indices.data, b.indices.data, a.indices.count)) &&
                    (!a.vertices.count || !memcmp(a.vertices.data, b.vertices.data, a.vertices.count)), "runtime direct geometry bytes");
            }
            const size_t scratch = candidateGeometry.ConversionScratchBytes();
            candidate.Reset(); candidateGeometry.Reset();
            options.maxScratchBytes = scratch;
            Good(ConvertImportScene(document, options, candidate, candidateGeometry, nullptr, &ids), "exact runtime scratch");
            candidate.Reset(); candidateGeometry.Reset();
            --options.maxScratchBytes;
            const auto oldIds = ids;
            Bad(ConvertImportScene(document, options, candidate, candidateGeometry, nullptr, &ids), ImportError::Workspace, "one-byte runtime scratch shortage");
            Require(!candidate.StorageBytes() && !candidateGeometry.StorageBytes() && ids.sun == oldIds.sun && ids.flashlight == oldIds.flashlight,
                "direct failure preserves output IDs and owners");
            options.maxScratchBytes = SIZE_MAX;
            Good(ConvertImportScene(document, options, candidate, candidateGeometry, nullptr, &ids), "same-input runtime scratch retry");
            ++cases;
        }
        printf("runtime direct nodes: %zu glTF/GLB, root, sun, auxiliary leaf and animation-tail cases passed\n", cases);
    }
    void EmptyScenesAndRequiredBehavior()
    {
        for (uint32_t multiple = 0; multiple < 2; ++multiple)
        {
            Fixture fixture;
            fixture.Build("[]", "", multiple ? R"([{"name":"first","samplers":[],"channels":[]},{"samplers":[],"channels":[]}])" :
                R"([{"name":"first","samplers":[],"channels":[]}])", R"([{"nodes":[]}])");
            RendererScene scene; ImportGeometry geometry; Convert(fixture, scene, geometry);
            CompareNative(fixture, scene.View());
            Require(scene.View().nodes.count == (multiple ? 4u : 2u) && scene.View().animations.count == multiple + 1 &&
                scene.View().nodes.data[0].firstChildIndex == 1, "animation container attaches to an empty scene");
        }
        Fixture fixture;
        fixture.Build("[{}]", R"(,"extensionsUsed":["KHR_animation_pointer"],"extensionsRequired":["KHR_animation_pointer"])");
        ImportDocument document;
        Bad(document.Parse(fixture.json.Bytes()), ImportError::UnsupportedExtension, "required unsupported material animation pointers");
        Require(document.AccessorCount() == 0 && document.BufferCount() == 0, "unsupported required behavior leaves no document");
    }
}

int main()
{
    LightsAndCameras(); CameraNamesAndDefaults(); AnimationValues(); SharedAndReorderedSamplers();
    SharedPlacements(); SkinPlacements(); IgnoredTracksAndUnusedSamplers(); RotationModes();
    MalformedMetadata(); InvalidSceneValues(); InvalidAnimationData();
    OwnedGlbAndFailureTransactions();
    EmptyScenesAndRequiredBehavior();
    RuntimeLightNodes();
    printf("scene import passed: %zu captured fixtures, %zu keyframe comparisons, %zu explicit rejections\n",
        nativeFixtures, keyComparisons, rejectionCount);
    FinishImportSceneReference();
    return 0;
}
