#ifndef UVSR_PBR_DEFERRED_LIGHTING_CB_H
#define UVSR_PBR_DEFERRED_LIGHTING_CB_H

#include <donut/shaders/deferred_lighting_cb.h>

struct PbrDeferredLightingConstants
{
    DeferredLightingConstants deferred;

    int separateIndirect;
    uint lightingDebugView;
    int directionalVisibilityLightIndex;
    uint visibilityDebugView;
};

#endif // UVSR_PBR_DEFERRED_LIGHTING_CB_H
