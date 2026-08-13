#ifndef UVSR_PATH_TRACING_CB_H
#define UVSR_PATH_TRACING_CB_H

#include <donut/shaders/light_cb.h>
#include <donut/shaders/view_cb.h>

#include "flashlight_shared.h"

#define UVSR_PATH_TRACING_FLAG_ACCUMULATE_SAMPLES (1u << 0u)
#define UVSR_PATH_TRACING_FLAG_REUSE_GI_CHECKPOINT (1u << 1u)
#define UVSR_PATH_TRACING_FLAG_REUSE_DIRECT (1u << 2u)
#define UVSR_PATH_TRACING_FLAG_REPLAY_PATH_SEEDS (1u << 3u)
#define UVSR_PATH_TRACING_FLAG_WRITE_STABLE_SIGNALS (1u << 4u)
#define UVSR_PATH_TRACING_FLAG_ANIMATE_HISTORY_RESET (1u << 5u)
#define UVSR_PATH_TRACING_FLAG_FILTER_FIREFLIES (1u << 6u)
#define UVSR_PATH_TRACING_FLAG_REVERSE_DEPTH (1u << 7u)
#define UVSR_PATH_TRACING_FLAG_SHOW_ENVIRONMENT_BACKGROUND (1u << 9u)
#define UVSR_PATH_TRACING_FLAG_REFRESH_DEBUG (1u << 10u)
#define UVSR_PATH_TRACING_FLAG_REPLICATE_PREVIEW (1u << 11u)

struct PathTracingConstants
{
    PlanarViewConstants view;
    PlanarViewConstants previousView;
    FlashlightBeamProfileBinding flashlight;

    float environmentScale;
    float rayBias;
    float maximumRayDistance;
    float fireflyThreshold;

    uint2 dispatchExtent;
    uint sampleSequencePhase;
    uint noisePattern;

    uint lightCount;
    uint maxBounces;
    uint russianRouletteStart;
    uint neeCandidateCount;

    uint stablePlaneCount;
    uint debugView;
    uint flags;
    uint schedulingSerialLow;

    uint schedulingSerialHigh;
    uint previousViewValid;
    uint schedulingPadding1;
    uint schedulingPadding2;

    uint4 rayMaterialLimits;

    uint2 schedulingGrid;
    uint2 schedulingPhase;

    uint accumulationAveraging;
    uint accumulationScheduling;
    uint accumulationEffectiveHistory;
    uint accumulationMinimumSamples;

    float accumulationTargetRelativeError;
    float accumulationMinimumUpdateRate;
    uint2 accumulationPadding;
};

#endif // UVSR_PATH_TRACING_CB_H
