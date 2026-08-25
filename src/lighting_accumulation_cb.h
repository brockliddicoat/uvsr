#ifndef UVSR_LIGHTING_ACCUMULATION_CB_H
#define UVSR_LIGHTING_ACCUMULATION_CB_H

struct LightingAccumulationConstants
{
    uint2 extent;
    uint resetHistory;
    uint padding;
};

#endif // UVSR_LIGHTING_ACCUMULATION_CB_H
