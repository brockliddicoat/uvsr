#pragma once

#include "renderer_import_runtime_lights.h"

namespace uvsr::tests
{
    inline ImportRuntimeLightOptions RuntimeLightFixtureOptions() noexcept
    {
        ImportRuntimeLightOptions options;
        options.enabled = true;
        options.sun.name = {"sun_1", 5};
        options.sun.values.irradiance = 8;
        options.sun.values.angularSize = 0.2f;
        options.sun.transform.translation[0] = 2;
        options.sun.transform.translation[1] = 3;
        options.sun.transform.translation[2] = 4;
        options.sun.transform.rotation[1] = 1;
        options.sun.transform.rotation[3] = 0;
        options.flashlight.name = {"flashlight_1", 12};
        options.flashlight.kind = RendererSceneLightKind::Spot;
        options.flashlight.values.intensity = 12;
        options.flashlight.values.range = 50;
        options.flashlight.values.radius = 0.2f;
        options.flashlight.values.innerAngle = 7;
        options.flashlight.values.outerAngle = 22;
        options.flashlight.transform.translation[0] = -2;
        options.flashlight.transform.translation[1] = 4;
        options.flashlight.transform.translation[2] = 8;
        return options;
    }
}
