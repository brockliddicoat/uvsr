#ifndef UVSR_BENT_AO_APPLICATION_HLSLI
#define UVSR_BENT_AO_APPLICATION_HLSLI

// Bent AO application is compiled only for the enabled feature variants.
// 0 = diffuse IBL, 1 = specular IBL occlusion, 2 = both.
#ifndef BENT_AO_APPLICATION
#define BENT_AO_APPLICATION 0
#endif

#include "bent_ao_application_shared.h"

#endif // UVSR_BENT_AO_APPLICATION_HLSLI
