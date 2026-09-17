#pragma once

#include "renderer_import_description.h"
#include "renderer_scene_records.h"

namespace uvsr
{
    enum DescriptionTransform : uint8_t
    { DescriptionTranslation = 1, DescriptionRotation = 2, DescriptionScaling = 4 };

    struct DescriptionNode
    {
        RendererSceneTransform transform;
        RendererSceneString name;
        RendererSceneString parentPath;
        uint32_t parent = InvalidSceneIndex;
        uint32_t subtreeEnd = 0;
        uint32_t model = InvalidSceneIndex;
        uint32_t leaf = InvalidSceneIndex;
        RendererSceneLeafKind leafKind = RendererSceneLeafKind::None;
        uint8_t transformFlags = 0;
        bool hasParent = false;
    };

    struct DescriptionAnimation
    {
        RendererSceneString name;
        RendererSceneRange channels;
    };

    struct DescriptionChannel
    {
        RendererSceneAnimationSampler sampler;
        RendererSceneRange targets;
        RendererSceneString property;
        RendererSceneAnimationAttribute attribute = RendererSceneAnimationAttribute::Undefined;
    };

    struct DescriptionCounts
    {
        uint32_t models = 0, nodes = 0, lights = 0, cameras = 0;
        uint32_t animations = 0, channels = 0, targets = 0, keyframes = 0, stringBytes = 0;
    };

    // all tables have exactly the validated counts. this loading-only owner
    // contains no parser object, graph ownership chain or model resource copy.
    struct ImportDescriptionState
    {
        DescriptionCounts counts;
        RendererSceneString* models = nullptr;
        DescriptionNode* nodes = nullptr;
        RendererSceneLight* lights = nullptr;
        RendererSceneCamera* cameras = nullptr;
        DescriptionAnimation* animations = nullptr;
        DescriptionChannel* channels = nullptr;
        RendererSceneString* targets = nullptr;
        RendererSceneKeyframe* keyframes = nullptr;
        char* strings = nullptr;
        uint32_t ignoredLeafTypes = 0;
        uint32_t unknownInterpolationModes = 0;
        size_t storageBytes = 0, scratchBytes = 0;
        ~ImportDescriptionState() noexcept;
    };

    struct ImportDescriptionAccess
    {
        static const ImportDescriptionState* State(const ImportSceneDescription& description) noexcept
        { return description.m_State; }
    };
}
