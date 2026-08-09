#ifndef UVSR_PBR_DEFERRED_LIGHTING_CB_H
#define UVSR_PBR_DEFERRED_LIGHTING_CB_H

#include <donut/shaders/deferred_lighting_cb.h>
#include "flashlight_shared.h"
#include "sky_visibility_application.h"

#define UVSR_DIRECT_VISIBILITY_SCALAR_R8 0u
#define UVSR_DIRECT_VISIBILITY_RGB_RGBA16F 1u

struct PbrDeferredLightingConstants
{
    DeferredLightingConstants deferred;

    int separateIndirect;
    uint lightingDebugView;
    uint visibilityDebugView;
    uint skyVisibilityApplication;

    int2 directVisibilityLightIndices;
    uint2 directVisibilityEncodings;

    int flashlightLightIndex;
    uint3 flashlightPadding;

    FlashlightBeamProfile flashlightBeamProfile;
};

#endif // UVSR_PBR_DEFERRED_LIGHTING_CB_H
