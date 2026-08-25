/*
 * Copyright (c) 2014-2024, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef UVSR_RENDERER_GPU_HELPERS_HLSLI
#define UVSR_RENDERER_GPU_HELPERS_HLSLI

#include "renderer_gpu_contract.h"

float4 ReconstructClipPosition(
    PlanarViewConstants view,
    float2 pixelPosition,
    float depth)
{
    float2 uv = (pixelPosition - view.viewportOrigin) * view.viewportSizeInv;
    return float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
}

float3 ReconstructViewPosition(
    PlanarViewConstants view,
    float2 pixelPosition,
    float depth)
{
    float4 position = mul(
        ReconstructClipPosition(view, pixelPosition, depth),
        view.matClipToView);
    return position.xyz / position.w;
}

float3 ReconstructWorldPosition(
    PlanarViewConstants view,
    float2 pixelPosition,
    float depth)
{
    float4 position = mul(
        ReconstructClipPosition(view, pixelPosition, depth),
        view.matClipToWorld);
    return position.xyz / position.w;
}

float3 GetIncidentVector(
    float4 cameraDirectionOrPosition,
    float3 surfacePosition)
{
    return cameraDirectionOrPosition.w > 0.0f
        ? normalize(surfacePosition - cameraDirectionOrPosition.xyz)
        : cameraDirectionOrPosition.xyz;
}

float3 GetMotionVector(
    float3 position,
    float3 previousWorldPosition,
    PlanarViewConstants view,
    PlanarViewConstants previousView)
{
    float4 previousClip = mul(
        float4(previousWorldPosition, 1.0f),
        previousView.matWorldToClip);
    if (previousClip.w <= 0.0f)
        return 0.0f;

    previousClip.xyz /= previousClip.w;
    float2 previousWindow = previousClip.xy * view.clipToWindowScale +
        view.clipToWindowBias;
    return float3(
        previousWindow - position.xy +
            (view.pixelOffset - previousView.pixelOffset),
        previousClip.z - position.z);
}

float Unpack_R8_SNORM(uint value)
{
    int signedValue = int(value << 24u) >> 24;
    return clamp(float(signedValue) / 127.0f, -1.0f, 1.0f);
}

float3 Unpack_RGB8_SNORM(uint value)
{
    return float3(
        Unpack_R8_SNORM(value),
        Unpack_R8_SNORM(value >> 8u),
        Unpack_R8_SNORM(value >> 16u));
}

float4 Unpack_RGBA8_SNORM(uint value)
{
    return float4(
        Unpack_R8_SNORM(value),
        Unpack_R8_SNORM(value >> 8u),
        Unpack_R8_SNORM(value >> 16u),
        Unpack_R8_SNORM(value >> 24u));
}

MaterialSample DefaultMaterialSample()
{
    MaterialSample result = (MaterialSample)0;
    result.opacity = 1.0f;
    result.occlusion = 1.0f;
    return result;
}

MaterialTextureSample DefaultMaterialTextures()
{
    MaterialTextureSample result;
    result.baseOrDiffuse = 1.0f;
    result.metalRoughOrSpecular = 1.0f;
    result.normal = float4(0.5f, 0.5f, 1.0f, 0.0f);
    result.emissive = 1.0f;
    result.occlusion = 1.0f;
    result.transmission = 1.0f;
    result.opacity = 1.0f;
    return result;
}

/*
 * The PBR workflow conversion below is derived from Khronos glTF examples.
 *
 * Copyright (c) 2016-2017 Gary Hsu
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

float GetPerceivedBrightness(float3 color)
{
    return sqrt(
        0.299f * color.r * color.r +
        0.587f * color.g * color.g +
        0.114f * color.b * color.b);
}

static const float UVSR_DIELECTRIC_SPECULAR = 0.04f;

float SolveMetalness(
    float diffuse,
    float specular,
    float oneMinusSpecularStrength)
{
    if (specular < UVSR_DIELECTRIC_SPECULAR)
        return 0.0f;
    float a = UVSR_DIELECTRIC_SPECULAR;
    float b = diffuse * oneMinusSpecularStrength /
        (1.0f - UVSR_DIELECTRIC_SPECULAR) + specular -
        2.0f * UVSR_DIELECTRIC_SPECULAR;
    float c = UVSR_DIELECTRIC_SPECULAR - specular;
    return clamp(
        (-b + sqrt(max(b * b - 4.0f * a * c, 0.0f))) / (2.0f * a),
        0.0f,
        1.0f);
}

void ConvertSpecularGlossToMetalRough(
    float3 diffuseColor,
    float3 specularColor,
    out float3 baseColor,
    out float metalness)
{
    const float epsilon = 1.0e-6f;
    float oneMinusSpecularStrength = 1.0f - max(
        specularColor.r,
        max(specularColor.g, specularColor.b));
    metalness = SolveMetalness(
        GetPerceivedBrightness(diffuseColor),
        GetPerceivedBrightness(specularColor),
        oneMinusSpecularStrength);
    float3 fromDiffuse = diffuseColor * (
        oneMinusSpecularStrength /
        (1.0f - UVSR_DIELECTRIC_SPECULAR) /
        max(1.0f - metalness, epsilon));
    float3 fromSpecular = specularColor -
        UVSR_DIELECTRIC_SPECULAR * (1.0f - metalness) /
        max(metalness, epsilon);
    baseColor = saturate(lerp(
        fromDiffuse,
        fromSpecular,
        metalness * metalness));
}

void ApplyNormalMap(
    inout MaterialSample result,
    float4 tangent,
    float4 normalTexture,
    float normalScale)
{
    float tangentLengthSquared = dot(tangent.xyz, tangent.xyz);
    if (tangentLengthSquared == 0.0f || tangent.w == 0.0f)
        return;
    normalTexture.xy = normalTexture.xy * 2.0f - 1.0f;
    normalTexture.xy *= normalScale;
    normalTexture.z = normalTexture.z <= 0.0f
        ? sqrt(saturate(
            1.0f - normalTexture.x * normalTexture.x -
            normalTexture.y * normalTexture.y))
        : abs(normalTexture.z * 2.0f - 1.0f);
    float normalLengthSquared = dot(normalTexture.xyz, normalTexture.xyz);
    if (normalLengthSquared == 0.0f)
        return;
    float3 localNormal = normalTexture.xyz / sqrt(normalLengthSquared);
    tangent.xyz *= rsqrt(tangentLengthSquared);
    float3 bitangent = cross(result.geometryNormal, tangent.xyz) * tangent.w;
    result.shadingNormal = normalize(
        tangent.xyz * localNormal.x +
        bitangent * localNormal.y +
        result.geometryNormal * localNormal.z);
}

MaterialSample EvaluateSceneMaterial(
    float3 normal,
    float4 tangent,
    MaterialConstants material,
    MaterialTextureSample textures)
{
    MaterialSample result = DefaultMaterialSample();
    result.geometryNormal = normalize(normal);
    result.shadingNormal = result.geometryNormal;

    if ((material.flags & MaterialFlags_UseSpecularGlossModel) != 0)
    {
        float3 diffuseColor = material.baseOrDiffuseColor *
            textures.baseOrDiffuse.rgb;
        float3 specularColor = material.specularColor *
            textures.metalRoughOrSpecular.rgb;
        result.roughness = 1.0f - textures.metalRoughOrSpecular.a *
            (1.0f - material.roughness);
        ConvertSpecularGlossToMetalRough(
            diffuseColor,
            specularColor,
            result.baseColor,
            result.metalness);
        result.hasMetalRoughParams = true;
        result.diffuseAlbedo = diffuseColor * (1.0f - max(
            specularColor.r,
            max(specularColor.g, specularColor.b)));
        result.specularF0 = specularColor;
    }
    else
    {
        result.baseColor = material.baseOrDiffuseColor *
            textures.baseOrDiffuse.rgb;
        result.roughness = material.roughness *
            textures.metalRoughOrSpecular.g;
        result.metalness = material.metalness * (
            (material.flags & MaterialFlags_MetalnessInRedChannel) != 0
                ? textures.metalRoughOrSpecular.r
                : textures.metalRoughOrSpecular.b);
        result.hasMetalRoughParams = true;
        result.diffuseAlbedo = lerp(
            result.baseColor * (1.0f - UVSR_DIELECTRIC_SPECULAR),
            0.0f,
            result.metalness);
        result.specularF0 = lerp(
            UVSR_DIELECTRIC_SPECULAR,
            result.baseColor,
            result.metalness);
    }

    result.occlusion = (material.flags &
        MaterialFlags_UseOcclusionTexture) != 0
        ? textures.occlusion.r
        : 1.0f;
    result.occlusion = lerp(
        1.0f,
        result.occlusion,
        material.occlusionStrength);
    result.opacity = material.opacity;
    if ((material.flags & MaterialFlags_UseOpacityTexture) != 0)
        result.opacity *= textures.opacity;
    else if ((material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0)
        result.opacity *= textures.baseOrDiffuse.a;
    result.opacity = saturate(result.opacity);
    result.transmission = material.transmissionFactor;
    if ((material.flags & MaterialFlags_UseTransmissionTexture) != 0)
        result.transmission *= textures.transmission.r;
    result.emissiveColor = material.emissiveColor;
    if ((material.flags & MaterialFlags_UseEmissiveTexture) != 0)
        result.emissiveColor *= textures.emissive.rgb;
    if ((material.flags & MaterialFlags_UseNormalTexture) != 0)
    {
        ApplyNormalMap(
            result,
            tangent,
            textures.normal,
            material.normalTextureScale);
    }
    return result;
}

MaterialTextureSample SampleMaterialTexturesAuto(
    MaterialConstants material,
    Texture2D baseOrDiffuseTexture,
    Texture2D metalRoughOrSpecularTexture,
    Texture2D normalTexture,
    Texture2D emissiveTexture,
    Texture2D occlusionTexture,
    Texture2D transmissionTexture,
    Texture2D opacityTexture,
    SamplerState materialSampler,
    float2 texCoord,
    float2 normalTexCoordScale)
{
    MaterialTextureSample result = DefaultMaterialTextures();
    if ((material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0)
        result.baseOrDiffuse = baseOrDiffuseTexture.Sample(
            materialSampler, texCoord);
    if ((material.flags &
            MaterialFlags_UseMetalRoughOrSpecularTexture) != 0)
    {
        result.metalRoughOrSpecular = metalRoughOrSpecularTexture.Sample(
            materialSampler, texCoord);
    }
    if ((material.flags & MaterialFlags_UseEmissiveTexture) != 0)
        result.emissive = emissiveTexture.Sample(materialSampler, texCoord);
    if ((material.flags & MaterialFlags_UseNormalTexture) != 0)
    {
        result.normal = normalTexture.Sample(
            materialSampler, texCoord * normalTexCoordScale);
    }
    if ((material.flags & MaterialFlags_UseOcclusionTexture) != 0)
        result.occlusion = occlusionTexture.Sample(materialSampler, texCoord);
    if ((material.flags & MaterialFlags_UseTransmissionTexture) != 0)
    {
        result.transmission = transmissionTexture.Sample(
            materialSampler, texCoord);
    }
    if ((material.flags & MaterialFlags_UseOpacityTexture) != 0)
        result.opacity = opacityTexture.Sample(materialSampler, texCoord).r;
    return result;
}

static const float2 UVSR_SHADOW_SAMPLE_POSITIONS[16] = {
    float2(-0.3935238f, 0.7530643f),
    float2(-0.3022015f, 0.297664f),
    float2(0.09813362f, 0.192451f),
    float2(-0.7593753f, 0.518795f),
    float2(0.2293134f, 0.7607011f),
    float2(0.6505286f, 0.6297367f),
    float2(0.5322764f, 0.2350069f),
    float2(0.8581018f, -0.01624052f),
    float2(-0.6928226f, 0.07119545f),
    float2(-0.3114384f, -0.3017288f),
    float2(0.2837671f, -0.179743f),
    float2(-0.3093514f, -0.749256f),
    float2(-0.7386893f, -0.5215692f),
    float2(0.3988827f, -0.617012f),
    float2(0.8114883f, -0.458026f),
    float2(0.08265103f, -0.8939569f)
};

float2 EvaluateShadowPoisson(
    Texture2DArray shadowMap,
    SamplerComparisonState shadowSampler,
    ShadowConstants shadow,
    float3 worldPosition,
    float2 sinCosRotation,
    float diskSizeTexels)
{
    float4 uvzw = mul(float4(worldPosition, 1.0f),
        shadow.matWorldToUvzwShadow);
    if (uvzw.w <= 0.0f)
        return 0.0f;
    float3 uvz = uvzw.xyz / uvzw.w;
    if (shadow.shadowFalloffDistance == 0.0f)
        uvz.z = min(uvz.z, 0.999999f);
    float2 fadeUv = saturate(
        abs(uvz.xy - shadow.shadowMapCenterUV) * shadow.shadowFadeScale +
        shadow.shadowFadeBias);
    float fade = fadeUv.x * fadeUv.y;
    if (shadow.shadowFalloffDistance > 0.0f)
        fade *= saturate((1.0f - uvz.z) * 10.0f);
    if (fade == 0.0f)
        return 0.0f;

    float visibility = 0.0f;
    [unroll]
    for (uint sampleIndex = 0u; sampleIndex < 16u; ++sampleIndex)
    {
        float2 offset = UVSR_SHADOW_SAMPLE_POSITIONS[sampleIndex];
        offset = float2(
            offset.x * sinCosRotation.x - offset.y * sinCosRotation.y,
            offset.x * sinCosRotation.y + offset.y * sinCosRotation.x);
        offset *= shadow.shadowMapSizeTexelsInv * diskSizeTexels;
        visibility += shadowMap.SampleCmpLevelZero(
            shadowSampler,
            float3(
                uvz.xy + offset,
                shadow.shadowMapArrayIndex),
            uvz.z);
    }
    visibility *= 1.0f / 16.0f;
    return float2(visibility * fade, fade);
}

#endif // UVSR_RENDERER_GPU_HELPERS_HLSLI
