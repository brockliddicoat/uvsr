#pragma once

#include "renderer_scene.h"

namespace uvsr
{
    struct RendererSceneLightDirection { double x = 0, y = 0, z = 0; };
    struct RendererSceneLightAngles { float azimuth = 0, elevation = 0; };

    [[nodiscard]] RendererSceneLightAngles GetRendererSceneLightAngles(
        RendererSceneLightDirection direction, bool directional) noexcept;
    [[nodiscard]] RendererSceneLightDirection MakeRendererSceneLightDirection(
        float azimuthDegrees, float elevationDegrees, bool directional) noexcept;

    struct RendererSceneLightPose
    {
        double position[3]{};
        double direction[3]{};
        double right[3]{};
        bool setPosition = false;
        bool setDirection = false;
        bool setRight = false;
    };

    struct RendererSceneLightFrame
    {
        double position[3]{};
        double direction[3]{};
    };

    [[nodiscard]] inline const RendererSceneLight* FindRendererSceneLight(
        const RendererSceneView& scene, RendererSceneHandle handle) noexcept
    {
        return handle && handle.generation == scene.generation && scene.lights.IsValid() && handle.index < scene.lights.count
            ? scene.lights.data + handle.index : nullptr;
    }

    [[nodiscard]] RendererSceneResult GetRendererSceneLightFrame(const RendererSceneView& scene,
        RendererSceneHandle light, RendererSceneLightFrame& output) noexcept;
    // shared loading/editing math. failure leaves output untouched; callers own
    // validation of the source transform and publication of the result.
    [[nodiscard]] RendererSceneResult ResolveRendererSceneLightTransform(const RendererSceneTransform& current,
        const RendererSceneAffine& parent, const RendererSceneLightPose& pose,
        RendererSceneTransform& output) noexcept;
    [[nodiscard]] RendererSceneResult SetRendererSceneLightPose(RendererScene& scene,
        RendererSceneHandle light, const RendererSceneLightPose& pose) noexcept;

    // frame/UI borrow of the canonical scene. editable order always includes the
    // flashlight; submission excludes it while inactive, before any light cap.
    struct RendererSceneLightRange
    {
        RendererSceneView scene;
        RendererSceneHandle flashlight;
        bool includeFlashlight = true;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] uint32_t Count() const noexcept;
        [[nodiscard]] RendererSceneHandle At(uint32_t ordinal) const noexcept;
        [[nodiscard]] uint32_t Ordinal(RendererSceneHandle light) const noexcept;
    };
}
