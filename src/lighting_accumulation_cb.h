#ifndef UVSR_LIGHTING_ACCUMULATION_CB_H
#define UVSR_LIGHTING_ACCUMULATION_CB_H

struct LightingAccumulationConstants
{
    uint2 extent;
    uint schedulingSerialLow;
    uint schedulingSerialHigh;

    uint resetHistory;
    uint accumulateSamples;
    uint averaging;
    uint scheduling;

    uint effectiveHistory;
    uint minimumSamples;
    float targetRelativeError;
    float minimumUpdateRate;
};

#endif // UVSR_LIGHTING_ACCUMULATION_CB_H
