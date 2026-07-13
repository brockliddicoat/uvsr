#ifndef UVSR_RECONSTRUCTIVE_TEMPORAL_AA_CB_H
#define UVSR_RECONSTRUCTIVE_TEMPORAL_AA_CB_H

#include <donut/shaders/view_cb.h>

// These values are part of the CPU/shader ABI. Keep them stable: the UI and
// capture tooling use the numeric values to select a visualization without
// rebuilding shader permutations.
#define RTAA_DEBUG_FINAL_OUTPUT                         0
#define RTAA_DEBUG_CURRENT_JITTER                       1
#define RTAA_DEBUG_MOTION_VECTORS                       2
#define RTAA_DEBUG_DILATED_MOTION_VECTORS               3
#define RTAA_DEBUG_VELOCITY_SOURCE_PIXEL                4
#define RTAA_DEBUG_VELOCITY_CONFIDENCE                  5
#define RTAA_DEBUG_REPROJECTED_HISTORY                  6
#define RTAA_DEBUG_PREVIOUS_HISTORY_UV                  7
#define RTAA_DEBUG_CURRENT_DEPTH                        8
#define RTAA_DEBUG_REPROJECTED_PREVIOUS_DEPTH           9
#define RTAA_DEBUG_DEPTH_CONFIDENCE                    10
#define RTAA_DEBUG_NORMAL_CONFIDENCE                   11
#define RTAA_DEBUG_MATERIAL_MATCH                      12
#define RTAA_DEBUG_OBJECT_MATCH                        13
#define RTAA_DEBUG_COMBINED_HISTORY_CONFIDENCE         14
#define RTAA_DEBUG_EXPLICIT_REACTIVE_MASK              15
#define RTAA_DEBUG_AUTOMATIC_REACTIVE_MASK             16
#define RTAA_DEBUG_FINAL_REACTIVE_VALUE                17
#define RTAA_DEBUG_THIN_GEOMETRY_CLASSIFICATION        18
#define RTAA_DEBUG_THIN_GEOMETRY_ACCUMULATED_COVERAGE 19
#define RTAA_DEBUG_VARIANCE                            20
#define RTAA_DEBUG_CLIPPING_BOUNDS                     21
#define RTAA_DEBUG_UNCLIPPED_HISTORY                   22
#define RTAA_DEBUG_CLIPPED_HISTORY                     23
#define RTAA_DEBUG_CURRENT_FRAME_WEIGHT                24
#define RTAA_DEBUG_HISTORY_SAMPLE_COUNT                25
#define RTAA_DEBUG_SPATIAL_FALLBACK_CONTRIBUTION       26
#define RTAA_DEBUG_RESURRECTION_ELIGIBILITY            27
#define RTAA_DEBUG_RESURRECTION_SOURCE                 28
#define RTAA_DEBUG_SHARPENING_CONTRIBUTION             29
#define RTAA_DEBUG_REJECTION_REASONS                   30
#define RTAA_DEBUG_FINAL_NRA_RTAA_OUTPUT               31

// Shared by RTAA_Prepare and RTAA_Resolve. Every parameter row is exactly
// sixteen bytes. Besides satisfying D3D constant-buffer packing, the explicit
// rows make accidental CPU/HLSL ABI drift obvious during review.
struct ReconstructiveTemporalAAConstants
{
    // currentView includes this frame's projection offset. The history views
    // preserve the matrices and jitter that were active when each history was
    // generated. Their NoOffset matrices remain available through the embedded
    // PlanarViewConstants for de-jittered motion and persistent reprojection.
    PlanarViewConstants currentView;
    PlanarViewConstants immediateHistoryView;
    PlanarViewConstants persistentHistoryView0;
    PlanarViewConstants persistentHistoryView1;

    float2 resolution;
    float2 invResolution;

    // Pixel units, centered around zero. UV conversion must multiply by
    // invResolution. The renderer's G-buffer motion is already de-jittered, so
    // Resolve never adds this delta to previous = current + motion.
    float2 currentJitter;
    float2 previousJitter;

    // Frame-rate-independent weights are evaluated on the CPU. The shaders
    // only interpolate these analytical responses; they never evaluate exp().
    float stableCurrentWeight;
    float movingCurrentWeight;
    float reactiveCurrentWeight;
    float thinCoverageCurrentWeight;

    float motionWeightStartPixels;
    float motionWeightEndPixels;
    float paddingMotion0;
    float paddingMotion1;

    float depthThresholdAbsolute;
    float depthThresholdRelative;
    float normalRejectCosine;
    float normalAcceptCosine;

    float automaticReactiveStrength;
    float automaticReactiveLumaThreshold;
    float automaticReactiveChromaThreshold;
    float paddingReactive;

    float thinDepthThreshold;
    float thinContrastThreshold;
    float thinMaxRelaxation;
    float paddingThin;

    float varianceSigma;
    float varianceLumaScale;
    float varianceChromaScale;
    float thinClipExpansion;

    float spatialDepthWeight;
    float spatialNormalWeight;
    float spatialLumaWeight;
    float paddingSpatial;

    float resurrectionMaxWeight;
    float resurrectionMatchThreshold;
    float paddingResurrection0;
    float paddingResurrection1;

    uint maxHistorySamples;
    uint maxMovingHistorySamples;
    uint spatialFallbackRadius;
    uint debugMode;

    uint historyValid;
    uint reverseZ;
    uint enableVelocityDilation;
    uint enableMaterialValidation;

    uint enableObjectValidation;
    uint enableExplicitReactive;
    uint enableAutomaticReactive;
    uint enableThinGeometry;

    uint enableThinDiffusion;
    uint enableSpatialFallback;
    uint enableResurrection;
    uint persistentValidMask;

    uint writeDebug;
    uint paddingFlags0;
    uint paddingFlags1;
    uint paddingFlags2;
};

#endif // UVSR_RECONSTRUCTIVE_TEMPORAL_AA_CB_H
