#pragma pack_matrix(row_major)

#include "image_based_lighting_background_cb.h"

cbuffer c_Background : register(b0)
{
    ImageBasedLightingBackgroundConstants g_Background;
};

TextureCube t_Radiance : register(t0);
SamplerState s_Radiance : register(s0);

void main(
    in float4 i_position : SV_Position,
    in float2 i_uv : UV,
    out float4 o_color : SV_Target0)
{
    float4 clipPosition;
    clipPosition.x = i_uv.x * 2.0f - 1.0f;
    clipPosition.y = 1.0f - i_uv.y * 2.0f;
    clipPosition.z = 0.5f;
    clipPosition.w = 1.0f;
    float4 translatedWorldPosition = mul(
        clipPosition,
        g_Background.matClipToTranslatedWorld);
    float3 direction = normalize(
        translatedWorldPosition.xyz /
        translatedWorldPosition.w);

    // This lookup intentionally has no coordinate flip. The CPU source
    // conversion, SH projection, GGX prefilter, receivers, and background all
    // use the same cube convention.
    float3 radiance = t_Radiance.SampleLevel(
        s_Radiance, direction, 0.0f).rgb *
        g_Background.radianceScale;
    if (any(!isfinite(radiance)))
        radiance = 0.0f;
    o_color = float4(
        min(max(radiance, 0.0f), 65504.0f),
        1.0f);
}
