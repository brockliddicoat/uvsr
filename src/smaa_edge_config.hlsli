// The four shipping quality presets are the only static SMAA edge PSOs.
// Thresholds remain source-faithful: Low 0.15, Medium/High 0.10, Ultra 0.05.

#ifndef SMAA_QUALITY_PRESET
#error SMAA_QUALITY_PRESET must be a compile-time shader define
#endif

#if SMAA_QUALITY_PRESET == 0
#define SMAA_THRESHOLD 0.15
#elif SMAA_QUALITY_PRESET == 1 || SMAA_QUALITY_PRESET == 2
#define SMAA_THRESHOLD 0.1
#elif SMAA_QUALITY_PRESET == 3
#define SMAA_THRESHOLD 0.05
#else
#error Unsupported SMAA_QUALITY_PRESET value
#endif
