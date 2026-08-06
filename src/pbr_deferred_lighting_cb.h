#ifndef UVSR_PBR_DEFERRED_LIGHTING_CB_H
#define UVSR_PBR_DEFERRED_LIGHTING_CB_H

#include <donut/shaders/deferred_lighting_cb.h>
#include "sky_visibility_application.h"

#define UVSR_DIRECTIONAL_VISIBILITY_SCALAR_R8 0u
#define UVSR_DIRECTIONAL_VISIBILITY_RGB_RGBA16F 1u

struct PbrDeferredLightingConstants
{
    DeferredLightingConstants deferred;

    int separateIndirect;
    uint lightingDebugView;
    uint visibilityDebugView;
    uint skyVisibilityApplication;

    int2 directionalVisibilityLightIndices;
    uint2 directionalVisibilityEncodings;
};

#endif // UVSR_PBR_DEFERRED_LIGHTING_CB_H
