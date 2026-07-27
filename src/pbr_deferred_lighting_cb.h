#ifndef UVSR_PBR_DEFERRED_LIGHTING_CB_H
#define UVSR_PBR_DEFERRED_LIGHTING_CB_H

#include <donut/shaders/deferred_lighting_cb.h>

struct PbrDeferredLightingConstants
{
    DeferredLightingConstants deferred;

    int separateIndirect;
    int writeSourceRadiance;
    int includeEmissiveSource;
    float emissiveSourceGain;

    int4 directionalVisibilityLightIndices;

    uint lightingDebugView;
    uint paddingDebug0;
    uint paddingDebug1;
    uint paddingDebug2;
};

#endif // UVSR_PBR_DEFERRED_LIGHTING_CB_H
