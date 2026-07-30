#ifndef UVSR_SCREEN_SPACE_INDIRECT_COMPOSITE_SHARED_H
#define UVSR_SCREEN_SPACE_INDIRECT_COMPOSITE_SHARED_H

// This is the complete additive contract shared by the production HLSL
// composite and the dependency-free CPU reference test. Environment diffuse
// receives screen-space ambient visibility; traced diffuse GI is already a
// separate transport result and is not shadowed a second time. Environment
// specular is routed through the production composite separately so it can use
// the same visibility field without changing this additive diffuse contract.
#ifdef __cplusplus
#define UVSR_INDIRECT_COMPOSITE_INLINE constexpr
#define UVSR_INDIRECT_COMPOSITE_VALUE float
#else
#define UVSR_INDIRECT_COMPOSITE_INLINE
#define UVSR_INDIRECT_COMPOSITE_VALUE float3
#endif

UVSR_INDIRECT_COMPOSITE_INLINE UVSR_INDIRECT_COMPOSITE_VALUE
    ComposeScreenSpaceIndirectLighting(
        UVSR_INDIRECT_COMPOSITE_VALUE baseLighting,
        UVSR_INDIRECT_COMPOSITE_VALUE environmentDiffuse,
        float ambientVisibility,
        UVSR_INDIRECT_COMPOSITE_VALUE screenSpaceGi)
{
    return baseLighting +
        environmentDiffuse * ambientVisibility +
        screenSpaceGi;
}

#undef UVSR_INDIRECT_COMPOSITE_VALUE
#undef UVSR_INDIRECT_COMPOSITE_INLINE

#endif // UVSR_SCREEN_SPACE_INDIRECT_COMPOSITE_SHARED_H
