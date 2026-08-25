#ifndef UVSR_PBR_DEFERRED_LIGHTING_CB_H
#define UVSR_PBR_DEFERRED_LIGHTING_CB_H

#include "renderer_gpu_contract.h"
#include "flashlight_shared.h"
#include "pbr_lighting_debug_contract.h"
#include "sky_visibility_application.h"

struct PbrDeferredLightingConstants
{
    DeferredLightingConstants deferred;

    int separateIndirect;
    UVSR_GPU_UINT lightingDebugView;
    UVSR_GPU_UINT visibilityDebugView;
    UVSR_GPU_UINT skyVisibilityApplication;

    UVSR_GPU_INT2 directVisibilityLightIndices;
    int flashlightLightIndex;
    UVSR_GPU_UINT flashlightPadding;

    FlashlightBeamProfile flashlightBeamProfile;
};

#endif // UVSR_PBR_DEFERRED_LIGHTING_CB_H
