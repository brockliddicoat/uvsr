//
// Static quality mapping copied from the pinned official SMAA source.
//

#ifndef SMAA_QUALITY_PRESET
#error SMAA_QUALITY_PRESET must be a compile-time shader define
#endif

#if SMAA_QUALITY_PRESET == 0
#define SMAA_PRESET_LOW 1
#elif SMAA_QUALITY_PRESET == 1
#define SMAA_PRESET_MEDIUM 1
#elif SMAA_QUALITY_PRESET == 2
#define SMAA_PRESET_HIGH 1
#elif SMAA_QUALITY_PRESET == 3
#define SMAA_PRESET_ULTRA 1
#else
#error Unsupported SMAA_QUALITY_PRESET value
#endif
