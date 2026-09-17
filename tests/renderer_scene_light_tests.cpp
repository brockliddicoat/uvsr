#include "renderer_scene_light.h"
#include "renderer_scene_encoding.h"
#include "renderer_scene_encoding_fixture.h"
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    using namespace uvsr;

    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "canonical light check failed: %s\n", reason);
        exit(1);
    }

    struct Fixture
    {
        RendererScene scene;
        uint64_t generation;
        RendererSceneHandle Light(uint32_t index) const { return {generation, index}; }
        RendererSceneHandle Node(uint32_t index) const { return {generation, index + 2}; }
        RendererSceneHandle Parent() const { return {generation, 1}; }

        explicit Fixture(uint32_t seed) : generation(uint64_t(seed) + 17)
        {
            RendererSceneNode nodes[5];
            ReadLightSetupReference(seed, nodes[0].transform, nodes[1].transform);
            nodes[0].hasLocalTransform = nodes[1].hasLocalTransform = seed != 0;
            nodes[0].firstChildIndex = 1;
            nodes[1].parentIndex = 0; nodes[1].firstChildIndex = 2;
            const char names[] = "sun_1authored lampflashlight";
            RendererSceneCounts counts;
            counts.nodes = 5; counts.lights = 3; counts.stringBytes = sizeof(names) - 1;
            Require(scene.Prepare(counts).Succeeded(), "light fixture storage");
            for (uint32_t index = 0; index < 3; ++index)
            {
                auto& node = nodes[index + 2];
                node.parentIndex = 1;
                node.nextSiblingIndex = index < 2 ? index + 3 : InvalidSceneIndex;
                node.leafKind = RendererSceneLeafKind::Light; node.leafIndex = index;
                node.hasLocalTransform = true;
                node.transform.translation[0] = .23 * index;
                node.transform.translation[1] = -.37 * index;
                node.transform.translation[2] = .41 * index;
                node.name = index == 0 ? RendererSceneString{0, 5} : index == 1 ? RendererSceneString{5, 13} : RendererSceneString{18, 10};
                RendererSceneLight light;
                light.nodeIndex = index + 2; light.kind = RendererSceneLightKind(index);
                Require(scene.Write(index, light).Succeeded(), "light fixture record");
            }
            for (uint32_t index = 0; index < 5; ++index)
                Require(scene.Write(index, nodes[index]).Succeeded(), "light fixture node");
            Require(scene.WriteStrings(0, {names, sizeof(names) - 1}).Succeeded(), "light fixture names");
            const size_t workspaceBytes = scene.SealWorkspaceBytes();
            auto* workspace = static_cast<uint8_t*>(malloc(workspaceBytes));
            Require(workspace != nullptr && scene.Seal(0, {workspace, workspaceBytes}).Succeeded(), "light fixture seal");
            free(workspace);
            Require(scene.Publish(generation).Succeeded(), "light fixture publication");
        }
    };

    void SameTransform(const RendererSceneTransform& actual, const RendererSceneTransform& values)
    {
        for (uint32_t lane = 0; lane < 3; ++lane)
            Require(actual.translation[lane] == values.translation[lane] && actual.scaling[lane] == values.scaling[lane],
                "exact captured translation/scaling");
        for (uint32_t lane = 0; lane < 4; ++lane)
            Require(actual.rotation[lane] == values.rotation[lane], "exact captured quaternion");
    }

    void SameFrameAndBytes(Fixture& fixture, uint32_t index)
    {
        const auto handle = fixture.Light(index);
        RendererSceneLightFrame actual;
        Require(GetRendererSceneLightFrame(fixture.scene.View(), handle, actual).Succeeded(), "canonical light world frame");
        RendererSceneLightFrame expectedFrame;
        LightConstants expected;
        ReadLightFrameReference(index, expectedFrame, expected);
        for (uint32_t lane = 0; lane < 3; ++lane)
            Require(actual.position[lane] == expectedFrame.position[lane] && actual.direction[lane] == expectedFrame.direction[lane], "exact captured world frame");
        LightConstants encoded;
        memset(&encoded, 0xcd, sizeof(encoded));
        Require(EncodeRendererSceneLight(fixture.scene.View(), handle, encoded).Succeeded() &&
            memcmp(&encoded, &expected, sizeof(encoded)) == 0, "all initialized captured light ABI bytes");
    }

    void PoseAndEncoding()
    {
        uint32_t poses = 0, encodings = 0;
        for (uint32_t seed = 0; seed < 32; ++seed)
        {
            Fixture fixture(seed);
            for (uint32_t index = 0; index < 3; ++index)
            {
                const auto handle = fixture.Light(index);
                const auto node = fixture.Node(index);
                SameFrameAndBytes(fixture, index); ++encodings;
                for (uint32_t valuesCase = 0; valuesCase < 5; ++valuesCase)
                {
                    RendererSceneLightValues values;
                    values.color = {.17f, .43f, .79f};
                    values.irradiance = 7.7f; values.intensity = 13.1f; values.radius = .3f;
                    constexpr float angular[]{-1, 0, .2f, 90, 120};
                    values.angularSize = angular[valuesCase];
                    values.range = valuesCase < 2 ? float(valuesCase) - 1 : 17.3f;
                    values.innerAngle = 31.7f; values.outerAngle = 47.9f;
                    Require(fixture.scene.SetLight(handle, values).Succeeded(), "canonical light values");
                    SameFrameAndBytes(fixture, index); ++encodings;
                }
                for (uint32_t mode = 0; mode < 5; ++mode)
                {
                    RendererSceneLightPose pose;
                    pose.setPosition = mode != 1;
                    pose.setDirection = mode != 0;
                    pose.setRight = mode >= 3;
                    pose.position[0] = 7; pose.position[1] = 11; pose.position[2] = 13;
                    pose.direction[0] = .1234567890123; pose.direction[1] = -.91; pose.direction[2] = .27;
                    pose.right[0] = .13; pose.right[1] = .97; pose.right[2] = -.23;
                    if (mode == 4)
                        for (uint32_t lane = 0; lane < 3; ++lane) pose.right[lane] = 0;
                    const auto before = fixture.scene.View();
                    const auto result = SetRendererSceneLightPose(fixture.scene, handle, pose);
                    Require(result.Succeeded(), "canonical parented pose");
                    SameTransform(fixture.scene.View().nodes.data[node.index].transform, ReadLightTransformReference());
                    Require(fixture.scene.View().instanceTransformRevision == before.instanceTransformRevision &&
                        fixture.scene.View().previousTransformRevision == before.previousTransformRevision,
                        "light command does not invalidate instance GPU data or settle previous transforms");
                    SameFrameAndBytes(fixture, index); ++encodings; ++poses;
                    const auto revision = fixture.scene.View().contentRevision;
                    Require(!SetRendererSceneLightPose(fixture.scene, handle, pose).changed &&
                        fixture.scene.View().contentRevision == revision, "identical pose is an exact no-op");
                }
            }
        }
        printf("canonical light oracle: %u parented poses, %u exact world/ABI comparisons, signed scale, roll and no-op revisions passed\n",
            poses, encodings);
    }

    void LoadingPose()
    {
        Fixture fixture(0);
        RendererSceneLightPose pose;
        pose.direction[0] = .1; pose.direction[1] = -.9; pose.direction[2] = .1;
        pose.setDirection = true;
        RendererSceneTransform output;
        Require(ResolveRendererSceneLightTransform({}, {}, pose, output).Succeeded(), "loading sun pose");
        SameTransform(output, ReadLightTransformReference());
        const auto retained = output;
        pose.direction[0] = pose.direction[1] = pose.direction[2] = 0;
        Require(!ResolveRendererSceneLightTransform({}, {}, pose, output).Succeeded() &&
            memcmp(&output, &retained, sizeof(output)) == 0, "loading pose failure preserves output");
        puts("loading light pose: exact captured fallback sun and unchanged rejected output passed");
    }

    void Ordering()
    {
        RendererSceneLight lights[21];
        RendererSceneLightRange range;
        range.scene.generation = 11;
        range.scene.lights = {lights, 21};
        constexpr uint32_t flashlightIndices[]{0, 5, 20};
        for (uint32_t flashlight : flashlightIndices)
        {
            range.flashlight = {11, flashlight};
            for (uint32_t enabled = 0; enabled < 2; ++enabled)
            {
                range.includeFlashlight = enabled != 0;
                Require(range.Count() == 20 + enabled && !range.At(range.Count()), "exact submitted count and checked ordinal");
                bool visited[21]{};
                uint32_t expected = 0;
                for (uint32_t ordinal = 0; ordinal < range.Count(); ++ordinal)
                {
                    const auto handle = range.At(ordinal);
                    Require(handle.generation == 11 && handle.index < 21 && !visited[handle.index] && range.Ordinal(handle) == ordinal,
                        "unique generation-qualified ordinal and inverse selection");
                    visited[handle.index] = true;
                    if (enabled && ordinal == 0) Require(handle.index == flashlight, "flashlight leads editable and enabled submission");
                    else
                    {
                        if (expected == flashlight) ++expected;
                        Require(handle.index == expected++, "remaining imported preorder");
                    }
                    if (!enabled && ordinal < 16) Require(handle.index != flashlight, "inactive flashlight does not use a deferred slot");
                }
                Require(visited[flashlight] == bool(enabled), "disabled flashlight excluded exactly once");
                Require(enabled || range.Ordinal(range.flashlight) == InvalidSceneIndex, "excluded light cannot be selected from submitted order");
            }
        }
        range.flashlight.generation = 12;
        Require(range.Count() == 21 && range.At(0).index == 0, "stale flashlight does not hide an unrelated light");
        range.scene.lights = {lights, size_t(UINT32_MAX) + 1};
        Require(!range.Count() && !range.At(0), "oversized ordinal view rejected");
        range.scene.lights = {nullptr, 1};
        Require(!range.Count(), "malformed borrow rejected");
        range.scene = {};
        Require(!range.Count() && !range.At(0), "empty scene range");
        puts("canonical light order: 21 lights, first/middle/last flashlight, active and inactive 16-light prefixes passed");
    }

    void RejectedInputs()
    {
        Fixture fixture(3);
        const auto handle = fixture.Light(0);
        const auto node = fixture.Node(0);
        RendererSceneLightPose valid;
        valid.setPosition = valid.setDirection = true;
        valid.position[0] = 1; valid.position[1] = 2; valid.position[2] = 3;
        valid.direction[2] = -1;
        for (uint32_t invalid = 0; invalid < 9; ++invalid)
        {
            auto target = handle;
            auto pose = valid;
            switch (invalid)
            {
            case 0: ++target.generation; break;
            case 1: target.index = 3; break;
            case 2: pose.position[0] = NAN; break;
            case 3: pose.direction[2] = 0; break;
            case 4: pose.direction[2] = INFINITY; break;
            case 5: pose.setRight = true; pose.right[0] = NAN; break;
            case 6: pose.setRight = true; pose.setDirection = false; break;
            case 7: pose.position[0] = DBL_MAX; break;
            case 8:
            {
                const auto parent = fixture.Parent();
                auto transform = fixture.scene.View().nodes.data[parent.index].transform;
                transform.scaling[0] = 0;
                Require(fixture.scene.SetTransform(parent, transform).Succeeded(), "singular parent fixture");
                break;
            }
            }
            const auto before = fixture.scene.View();
            const auto oldNode = before.nodes.data[node.index];
            Require(!SetRendererSceneLightPose(fixture.scene, target, pose).Succeeded(), "invalid pose rejected");
            Require(fixture.scene.View().contentRevision == before.contentRevision &&
                memcmp(&oldNode, fixture.scene.View().nodes.data + node.index, sizeof(oldNode)) == 0, "invalid pose preserves scene transaction");
        }
        LightConstants encoded;
        memset(&encoded, 0x73, sizeof(encoded));
        const auto old = encoded;
        Require(!EncodeRendererSceneLight(fixture.scene.View(), {handle.generation + 1, handle.index}, encoded).Succeeded() &&
            memcmp(&encoded, &old, sizeof(old)) == 0, "stale light preserves encoded output");
        auto view = fixture.scene.View();
        RendererSceneLight malformed[3];
        for (uint32_t index = 0; index < 3; ++index) malformed[index] = view.lights.data[index];
        view.lights = {malformed, 3};
        malformed[handle.index].values.color.x = NAN;
        Require(!EncodeRendererSceneLight(view, handle, encoded).Succeeded() && memcmp(&encoded, &old, sizeof(old)) == 0,
            "malformed values preserve encoded output");
        malformed[handle.index].values.color.x = 1;
        malformed[handle.index].nodeIndex = UINT32_MAX;
        RendererSceneLightFrame frame{{7, 11, 13}, {2, 3, 5}};
        const auto oldFrame = frame;
        Require(!GetRendererSceneLightFrame(view, handle, frame).Succeeded() && memcmp(&frame, &oldFrame, sizeof(frame)) == 0,
            "malformed node preserves queried frame");
        fixture.scene.Reset();
        Require(!SetRendererSceneLightPose(fixture.scene, handle, valid).Succeeded(), "retired generation rejects commands");
        puts("canonical light failures: nine atomic pose rejections, stale generation, malformed values/node and retirement passed");
    }

    void SingularFrames()
    {
        constexpr double scales[]{0, double(.5e-6f), double(1e-6f), double(2e-6f)};
        for (double scale : scales)
            for (uint32_t directionOnly = 0; directionOnly < 2; ++directionOnly)
            {
                Fixture fixture(0);
                const auto parent = fixture.Parent();
                auto transform = fixture.scene.View().nodes.data[parent.index].transform;
                transform.scaling[0] = scale;
                Require(fixture.scene.SetTransform(parent, transform).Succeeded(), "pivot-scale parent");
                RendererSceneLightPose pose;
                pose.setPosition = !directionOnly;
                pose.setDirection = directionOnly != 0;
                pose.position[0] = 3; pose.position[1] = 5; pose.position[2] = 7;
                pose.direction[2] = -1;
                const auto light = fixture.Light(0);
                const auto node = fixture.Node(0);
                const auto before = fixture.scene.View();
                const auto oldNode = before.nodes.data[node.index];
                const auto result = SetRendererSceneLightPose(fixture.scene, light, pose);
                const bool finite = ReadLightCutoffReference();
                Require(result.Succeeded() == finite, "same retained inverse cutoff for separate position and direction commands");
                if (finite) SameTransform(fixture.scene.View().nodes.data[node.index].transform, ReadLightTransformReference());
                else Require(before.contentRevision == fixture.scene.View().contentRevision &&
                    memcmp(&oldNode, fixture.scene.View().nodes.data + node.index, sizeof(oldNode)) == 0,
                    "singular command preserves all canonical node fields");
            }
        for (uint32_t index = 0; index < 3; ++index)
        {
            Fixture fixture(0);
            const auto light = fixture.Light(index);
            const auto node = fixture.Node(index);
            auto transform = fixture.scene.View().nodes.data[node.index].transform;
            for (double& scale : transform.scaling) scale = 0;
            Require(fixture.scene.SetTransform(node, transform).Succeeded(), "zero-scale light node");
            LightConstants encoded;
            memset(&encoded, 0x5e, sizeof(encoded));
            const auto old = encoded;
            const auto result = EncodeRendererSceneLight(fixture.scene.View(), light, encoded);
            if (index == 1)
            {
                LightConstants expected;
                ReadPointLightReference(expected);
                Require(result.Succeeded() && memcmp(&encoded, &expected, sizeof(encoded)) == 0,
                    "zero-scale point preserves its captured direction-independent bytes");
            }
            else Require(!result.Succeeded() && memcmp(&encoded, &old, sizeof(encoded)) == 0,
                "zero-scale directional/spot reject undefined directions atomically");
        }
        puts("canonical light singular cases: eight separate pose cutoff cases and all three zero-scale light kinds passed");
    }

}

void CheckRendererSceneLights()
{
    PoseAndEncoding();
    LoadingPose();
    Ordering();
    RejectedInputs();
    SingularFrames();
}
