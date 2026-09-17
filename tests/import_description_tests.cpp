#include "renderer_import_description.h"
#include "import/renderer_import_description_private.h"

#include "import_description_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <utility>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace
{
    using namespace uvsr;
    using namespace uvsr::test_fixture;
    size_t referenceComparisons = 0, rejected = 0, allocationFailures = 0;
    const char* fileName = "C:/development/assets/room/room.scene.json";

    void Require(bool condition, const char* message)
    {
        if (!condition) { fprintf(stderr, "description: %s\n", message); exit(1); }
    }
    void Good(ImportResult result, const char* message)
    {
        if (!result)
        {
            fprintf(stderr, "description: %s: %s, object %u, index %zu, parser %u\n", message,
                ImportErrorText(result.error), unsigned(result.object), result.index, result.parserCode);
            exit(1);
        }
    }
    ArrayView<const uint8_t> Bytes(const std::string& text) { return {reinterpret_cast<const uint8_t*>(text.data()), text.size()}; }
    ArrayView<const char> Text(const char* text) { return {text, strlen(text)}; }
    std::string Text(const ImportDescriptionState& state, RendererSceneString text)
    {
        if (!text.length) return {};
        Require(text.offset <= state.counts.stringBytes && text.length < state.counts.stringBytes - text.offset, "text range");
        Require(state.strings[text.offset + text.length] == 0, "text terminator");
        return {state.strings + text.offset, text.length};
    }
    const ImportDescriptionState& State(const ImportSceneDescription& description)
    {
        const auto* state = ImportDescriptionAccess::State(description);
        Require(state != nullptr, "missing state");
        return *state;
    }
    void CompareTransform(const DescriptionNode& candidate, unsigned index)
    {
        const auto& expected = DescriptionTransforms[index];
        Require(candidate.transformFlags == expected.flags, "authored transform flags");
        Require(memcmp(candidate.transform.translation, expected.translation, sizeof(expected.translation)) == 0 &&
            memcmp(candidate.transform.scaling, expected.scaling, sizeof(expected.scaling)) == 0 &&
            memcmp(candidate.transform.rotation, expected.rotation, sizeof(expected.rotation)) == 0, "captured transform values");
        ++referenceComparisons;
    }

    void CompareLeaf(const ImportDescriptionState& state, const DescriptionNode& candidate, unsigned index)
    {
        if (index < 2 || index == 12) return;
        if (index < 8)
        {
            Require(candidate.leafKind == RendererSceneLeafKind::Light && candidate.leaf < state.counts.lights, "captured light leaf");
            const auto& light = state.lights[candidate.leaf];
            const auto& other = DescriptionLights[index - 2];
            Require(light.kind == other.kind && memcmp(&light.values.color, other.color, sizeof(other.color)) == 0, "captured light color and kind");
            if (light.kind == RendererSceneLightKind::Directional)
            {
                Require(light.values.irradiance == other.irradiance && light.values.angularSize == other.angularSize, "captured directional fields");
            }
            else if (light.kind == RendererSceneLightKind::Point)
            {
                Require(light.values.intensity == other.intensity && light.values.radius == other.radius && light.values.range == other.range, "captured point fields");
            }
            else
            {
                Require(light.values.intensity == other.intensity && light.values.radius == other.radius && light.values.range == other.range &&
                    light.values.innerAngle == other.innerAngle && light.values.outerAngle == other.outerAngle, "captured spot fields");
            }
        }
        else
        {
            Require(candidate.leafKind == RendererSceneLeafKind::Camera && candidate.leaf < state.counts.cameras, "captured camera leaf");
            const auto& camera = state.cameras[candidate.leaf];
            const auto& other = DescriptionCameras[index - 8];
            Require(camera.kind == other.kind, "captured camera kind");
            if (camera.kind == RendererSceneCameraKind::Perspective)
            {
                Require(camera.nearPlane == other.nearPlane && camera.verticalFov == other.verticalFov &&
                    camera.hasFarPlane == other.hasFarPlane && camera.hasAspectRatio == other.hasAspectRatio &&
                    (!camera.hasFarPlane || camera.farPlane == other.farPlane) && (!camera.hasAspectRatio || camera.aspectRatio == other.aspectRatio), "captured perspective fields");
            }
            else
            {
                Require(camera.nearPlane == other.nearPlane && camera.farPlane == other.farPlane && camera.xMagnitude == other.xMagnitude && camera.yMagnitude == other.yMagnitude, "captured orthographic fields");
            }
        }
        ++referenceComparisons;
    }

    std::string RichFixture()
    {
        return R"({"displayName":"room", "initialCamera":{"position":[1,2,3]},
            "models":["parts/first.gltf","parts/../second.glb","part%20literal.glb","D:/shared/model.gltf"],
            "graph":[
                {"name":"first","model":0.0,"translation":[1.25,-2,3],"euler":[0.12,-0.64,1.9],"scaling":[-1,2,0.5]},
                {"name":"scalar","model":1,"translation":3,"rotation":2,"euler":"ignored","scaling":-0.5},
                {"type":"DirectionalLight","color":[0.25,0.5,0.75],"irradiance":2.75,"angularSize":0.75},
                {"type":"DirectionalLight"},
                {"type":"PointLight","color":0.5,"intensity":4,"radius":0.2,"range":50},
                {"type":"PointLight"},
                {"type":"SpotLight","intensity":3,"radius":0.1,"range":22,"innerAngle":12,"outerAngle":30},
                {"type":"SpotLight"},
                {"type":"PerspectiveCamera","zNear":0.02,"zFar":500,"aspectRatio":1.8,"verticalFov":0.7},
                {"type":"PerspectiveCamera","zNear":null,"zFar":null,"aspectRatio":null},
                {"type":"OrthographicCamera","xMag":3,"yMag":4,"zNear":-5,"zFar":10},
                {"type":"OrthographicCamera"},
                {"name":"[]{}\"quoted","parent":"/first","children":[{"name":"child","children":[{"name":"last"}]}]},
                {"name":"unknown","type":"UnknownType"}],
            "animations":[{"name":"tracks","channels":[
                {"attribute":"translation","mode":"linear","targets":["/first","/scalar"],"data":[{"time":-1,"value":2},{"time":0,"value":[1,2,3]},{"time":0,"value":[4,5,6,7,99]}]},
                {"attribute":"roughness","mode":"hermite","target":"material:surface","data":[{"time":0,"value":0.1,"inTangent":[1],"outTangent":[2,3]},{"time":2,"value":[0.5]}]},
                {"attribute":"rotation","mode":"slerp","target":"/first","targets":false,"data":[{"time":0,"value":[0,0,0,1]}]},
                {"attribute":"scaling","mode":"catmull-rom","target":"/first","data":[{"time":0},{"time":1}]},
                {"attribute":"intensity","mode":"unknown","data":[]},
                {"attribute":"translation","target":"/first","data":[]} ]},{"name":"empty"}]})";
    }

    void Rich()
    {
        auto json = RichFixture();
        ImportSceneDescription description;
        Good(description.Parse(Bytes(json), Text(fileName)), "rich parse");
        memset(json.data(), 0xa5, json.size());
        const auto& state = State(description);
        Require(state.counts.models == 4 && state.counts.nodes == 16 && state.counts.lights == 6 && state.counts.cameras == 4 &&
            state.counts.animations == 2 && state.counts.channels == 6 && state.counts.targets == 6 && state.counts.keyframes == 8, "rich counts");
        const char* paths[]{"C:/development/assets/room/parts/first.gltf", "C:/development/assets/room/parts/../second.glb",
            "C:/development/assets/room/part%20literal.glb", "D:/shared/model.gltf"};
        for (size_t i = 0; i < 4; ++i)
        {
            const auto path = description.ModelPath(i);
            Require(std::string(path.data, path.count) == paths[i], "literal descriptor path");
        }
        Require(!description.ModelPath(4).data, "invalid model path");
        for (uint32_t i = 0; i < 13; ++i)
        {
            CompareTransform(state.nodes[i], i);
            CompareLeaf(state, state.nodes[i], i);
        }
        Require(state.nodes[12].subtreeEnd == 15 && state.nodes[13].parent == 12 && state.nodes[13].subtreeEnd == 15 &&
            state.nodes[14].parent == 13 && state.nodes[14].subtreeEnd == 15 && state.nodes[15].parent == InvalidSceneIndex, "flat preorder subtree ranges");
        Require(Text(state, state.nodes[12].name) == "[]{}\"quoted" && Text(state, state.nodes[12].parentPath) == "/first", "escaped string or custom parent");
        Require(state.ignoredLeafTypes == 1 && state.unknownInterpolationModes == 1, "ignored metadata counts");
        Require(state.nodes[15].leafKind == RendererSceneLeafKind::None && state.nodes[15].leaf == InvalidSceneIndex, "unknown type changed leaf");
        const RendererSceneInterpolation modes[]{RendererSceneInterpolation::Linear, RendererSceneInterpolation::HermiteSpline,
            RendererSceneInterpolation::Slerp, RendererSceneInterpolation::CatmullRomSpline, RendererSceneInterpolation::Step, RendererSceneInterpolation::Step};
        unsigned referenceKey = 0;
        for (uint32_t i = 0; i < 6; ++i)
        {
            const auto& channel = state.channels[i];
            Require(channel.sampler.interpolation == modes[i], "channel mode");
            Require(channel.sampler.keyframes.count == DescriptionKeyCounts[i], "key count");
            for (uint32_t k = 0; k < DescriptionKeyCounts[i]; ++k)
            {
                const auto& key = state.keyframes[channel.sampler.keyframes.first + k];
                const auto& expected = DescriptionKeys[referenceKey++];
                Require(key.time == expected.time, "key time");
                Require(memcmp(&key.value, expected.value, sizeof(expected.value)) == 0 &&
                    memcmp(&key.inTangent, expected.inTangent, sizeof(expected.inTangent)) == 0 &&
                    memcmp(&key.outTangent, expected.outTangent, sizeof(expected.outTangent)) == 0, "retained key vector fields");
                ++referenceComparisons;
            }
        }
        Require(referenceKey == sizeof(DescriptionKeys) / sizeof(DescriptionKeys[0]), "captured key coverage");
        Require(Text(state, state.targets[state.channels[1].targets.first]) == "material:surface" &&
            Text(state, state.channels[1].property) == "roughness", "material target identity");
        const auto* address = &state;
        ImportSceneDescription moved(std::move(description));
        Require(!description.StorageBytes() && &State(moved) == address, "move constructor");
        ImportSceneDescription assigned;
        assigned = std::move(moved);
        Require(!moved.StorageBytes() && &State(assigned) == address, "move assignment");
        auto& alias = assigned;
        assigned = std::move(alias);
        Require(&State(assigned) == address, "self move");
        assigned.Reset(); assigned.Reset();
        Require(!assigned.StorageBytes() && !assigned.ModelCount(), "reset");
    }

    void Rejects()
    {
        const char* invalid[]{"", "[]", "{", R"({"models":false})", R"({"models":[3]})", R"({"models":[""]})",
            R"({"models":["bad\u0000path"]})", R"({"models":[],"models":[]})", R"({"graph":{}})", R"({"graph":[0]})",
            R"({"graph":[{"name":false}]})", R"({"graph":[{"name":"a","name":"b"}]})", R"({"graph":[{"parent":3}]})",
            R"({"graph":[{"model":0}]})", R"({"models":["a.gltf"],"graph":[{"model":-1}]})", R"({"models":["a.gltf"],"graph":[{"model":0.5}]})",
            R"({"models":["a.gltf"],"graph":[{"model":true}]})", R"({"graph":[{"translation":"bad"}]})", R"({"graph":[{"translation":[1,2]}]})",
            R"({"graph":[{"rotation":[0,0,1]}]})", R"({"graph":[{"euler":[0,1,2,3]}]})", R"({"graph":[{"scaling":[1,false,3]}]})",
            R"({"graph":[{"children":{}}]})", R"({"graph":[{"type":false}]})", R"({"graph":[{"type":"PointLight","color":[1,2]}]})",
            R"({"graph":[{"type":"DirectionalLight","irradiance":1e40}]})", R"({"graph":[{"type":"SpotLight","range":"bad"}]})",
            R"({"graph":[{"type":"PerspectiveCamera","zFar":1e40}]})", R"({"graph":[{"type":"OrthographicCamera","xMag":false}]})",
            R"({"animations":false})", R"({"animations":[false]})", R"({"animations":[{"channels":false}]})", R"({"animations":[{"channels":[{}]}]})",
            R"({"animations":[{"channels":[{"attribute":"translation","mode":3}]}]})", R"({"animations":[{"channels":[{"attribute":"translation","data":[{}]}]}]})",
            R"({"animations":[{"channels":[{"attribute":"translation","data":[{"time":1},{"time":0}]}]}]})",
            R"({"animations":[{"channels":[{"attribute":"translation","data":[{"time":0,"value":[1,"bad"]}]}]}]})",
            R"({"animations":[{"channels":[{"attribute":"translation","target":false}]}]})",
            R"({"animations":[{"channels":[{"attribute":"translation","targets":[3]}]}]})",
            R"({"animations":[{"channels":[{"attribute":"translation","data":[{"time":0,"time":1}]}]}]})"};
        ImportSceneDescription description;
        const std::string valid = R"({"models":["keep.gltf"]})";
        Good(description.Parse(Bytes(valid), Text(fileName)), "retained owner");
        const auto* state = &State(description);
        for (const char* input : invalid)
        {
            const auto result = description.Parse(Bytes(input), Text(fileName));
            if (result) { fprintf(stderr, "accepted malformed description: %s\n", input); Require(false, "malformed input accepted"); }
            Require(&State(description) == state, "malformed parse changed owner");
            ++rejected;
        }
        const char nul[]{'a', 0, 'b'};
        for (const auto path : {ArrayView<const char>{}, ArrayView<const char>{nullptr, 1}, ArrayView<const char>{nul, 3}, ArrayView<const char>{"a", SIZE_MAX}})
        {
            Require(!description.Parse(Bytes(valid), path) && &State(description) == state, "invalid path changed owner");
            ++rejected;
        }
    }

    void Failures()
    {
        const auto json = RichFixture();
        ImportSceneDescription description;
        Good(description.Parse(Bytes(json), Text(fileName)), "budget baseline");
        const size_t storage = description.StorageBytes(), scratch = description.ScratchBytes();
        const auto* old = &State(description);
        Require(description.Parse(Bytes(json), Text(fileName), {storage - 1, scratch}).error == ImportError::Capacity && &State(description) == old, "short storage budget");
        Require(description.Parse(Bytes(json), Text(fileName), {storage, scratch - 1}).error == ImportError::Workspace && &State(description) == old, "short scratch budget");
        rejected += 2;
        Good(description.Parse(Bytes(json), Text(fileName), {storage, scratch}), "exact budgets");
        for (int64_t count = 0; count < 64; ++count)
        {
            old = &State(description);
            SetImportAllocationFailureCountdown(count);
            const auto result = description.Parse(Bytes(json), Text(fileName));
            SetImportAllocationFailureCountdown(-1);
            if (result) break;
            Require(result.error == ImportError::OutOfMemory && &State(description) == old, "allocation failure changed owner");
            ++allocationFailures;
            Good(description.Parse(Bytes(json), Text(fileName)), "allocation retry");
            Require(description.StorageBytes() == storage && description.ScratchBytes() == scratch && description.ModelCount() == 4, "allocation retry content");
            Require(count != 63, "allocation retry limit");
        }
        Require(allocationFailures == 11, "allocation fault coverage");
        const std::string empty = "{}";
        Good(description.Parse(Bytes(empty), Text(fileName)), "empty description");
        Require(description.ModelCount() == 0 && State(description).counts.nodes == 0, "empty description retains old records");
        printf("description allocation failures: %zu, exact storage %zu, scratch %zu bytes\n", allocationFailures, storage, scratch);
    }

    void DeepWide()
    {
#if defined(_WIN32)
        ULONG_PTR low = 0, high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        Require(high > low && high - low <= 65536, "actual stack budget");
        printf("description hierarchy stack: %zu bytes\n", size_t(high - low));
#endif
        constexpr uint32_t depth = 400, width = 100000;
        std::string deep = "{\"graph\":[";
        for (uint32_t i = 0; i < depth; ++i) deep += i + 1 == depth ? "{\"name\":\"x\"}" : "{\"name\":\"x\",\"children\":[";
        for (uint32_t i = 1; i < depth; ++i) deep += "]}";
        deep += "]}";
        ImportSceneDescription description;
        Good(description.Parse(Bytes(deep), Text(fileName)), "deep description");
        const auto& state = State(description);
        Require(state.counts.nodes == depth, "deep count");
        for (uint32_t i = 0; i < depth; ++i)
            Require(state.nodes[i].parent == (i ? i - 1 : InvalidSceneIndex) && state.nodes[i].subtreeEnd == depth, "deep flat links");
        std::string wide = "{\"graph\":[";
        for (uint32_t i = 0; i < width; ++i) wide += i ? ",{\"name\":\"x\"}" : "{\"name\":\"x\"}";
        wide += "]}";
        Good(description.Parse(Bytes(wide), Text(fileName)), "wide description");
        const auto& wideState = State(description);
        Require(wideState.counts.nodes == width, "wide count");
        for (uint32_t i = 0; i < width; ++i)
            Require(wideState.nodes[i].parent == InvalidSceneIndex && wideState.nodes[i].subtreeEnd == i + 1, "wide flat links");
        std::string excessive = "{\"graph\":[";
        for (uint32_t i = 0; i < 1100; ++i) excessive += "{\"children\":[";
        excessive += "{}";
        for (uint32_t i = 0; i < 1100; ++i) excessive += "]}";
        excessive += "]}";
        Require(description.Parse(Bytes(excessive), Text(fileName)).error == ImportError::InvalidJson && &State(description) == &wideState, "vendor depth limit changed owner");
        ++rejected;
        printf("description hierarchy: depth %u, width %u, excessive JSON depth rejected\n", depth, width);
    }
}

int main()
{
    Rich();
    Rejects();
    Failures();
#if defined(_WIN32)
    HANDLE thread = CreateThread(nullptr, 65536, [](void*) -> DWORD { DeepWide(); return 0; }, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    Require(thread && WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0, "bounded-stack fixture");
    DWORD result = 1;
    Require(GetExitCodeThread(thread, &result) && result == 0, "bounded-stack result");
    Require(CloseHandle(thread) != 0, "bounded-stack handle");
#else
    DeepWide();
#endif
    printf("description: %zu captured field comparisons, %zu explicit rejections, %zu allocation failures with retry\n", referenceComparisons, rejected, allocationFailures);
    return 0;
}
