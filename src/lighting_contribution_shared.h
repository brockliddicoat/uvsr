#ifndef UVSR_LIGHTING_CONTRIBUTION_SHARED_H
#define UVSR_LIGHTING_CONTRIBUTION_SHARED_H

// Single source of truth for contribution-source bit positions shared by C++
// scene state, CPU reference tests, and HLSL lighting gates. Keep these as
// integer macros because this file is compiled by both language front ends;
// consumers convert them to their own typed constants. The UVSR prefix keeps
// the cross-language macro surface narrow and collision-resistant.
#define UVSR_LIGHTING_SOURCE_DIRECT (1u << 0u)
#define UVSR_LIGHTING_SOURCE_EMISSIVE (1u << 1u)
#define UVSR_LIGHTING_SOURCE_ENVIRONMENT (1u << 2u)
#define UVSR_LIGHTING_SOURCE_INDIRECT_DIFFUSE (1u << 3u)
#define UVSR_LIGHTING_SOURCE_INDIRECT_SPECULAR (1u << 4u)
#define UVSR_LIGHTING_SOURCE_ALL 0x1fu

#endif // UVSR_LIGHTING_CONTRIBUTION_SHARED_H
