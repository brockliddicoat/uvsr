#pragma once

#include "renderer_import.h"
#include "renderer_scene.h"

namespace uvsr
{
    struct ImportRuntimeLightSpec
    {
        // borrowed through the synchronous import; copied into the final scene.
        ArrayView<const char> name;
        RendererSceneLightKind kind = RendererSceneLightKind::Directional;
        RendererSceneLightValues values;
        RendererSceneTransform transform; // local to the final scene root.
    };

    struct ImportRuntimeLightOptions
    {
        bool enabled = false;
        // the first live directional keeps its authored fields except irradiance
        // and angular size. the other sun fields apply only to an absent sun.
        ImportRuntimeLightSpec sun;
        ImportRuntimeLightSpec flashlight{{}, RendererSceneLightKind::Spot};
    };

    struct ImportRuntimeLightIds
    {
        RendererSceneHandle sun;
        RendererSceneHandle flashlight;
    };

    [[nodiscard]] inline ImportResult ValidateImportRuntimeLights(const ImportRuntimeLightOptions& options) noexcept
    {
        if (!options.enabled) return {};
        if (options.sun.kind != RendererSceneLightKind::Directional || options.flashlight.kind != RendererSceneLightKind::Spot)
            return {ImportError::InvalidInput, ImportObject::Light};
        const ArrayView<const char> names[]{options.sun.name, options.flashlight.name};
        for (const auto name : names)
        {
            if (!name.IsValid() || !name.count) return {ImportError::InvalidInput, ImportObject::Light};
            if (name.count >= InvalidSceneIndex) return {ImportError::Capacity, ImportObject::Light};
            for (size_t i = 0; i < name.count; ++i)
                if (!name.data[i]) return {ImportError::InvalidInput, ImportObject::Light};
        }
        // the scene's ordinary seal validates every applied value and transform.
        return {};
    }
}
