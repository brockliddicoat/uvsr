/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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

#include "renderer_light_probe_contract.h"

static const float UVSR_LIGHT_PROBE_PI = 3.14159265358979323846f;

struct LightProbeGeometryInput
{
    float4 position : SV_Position;
    float2 uv : UV;
};

struct LightProbeGeometryOutput
{
    float4 position : SV_Position;
    float2 uv : UV;
    uint arrayIndex : SV_RenderTargetArrayIndex;
};

[maxvertexcount(3)]
[instance(6)]
void cubemap_gs(
    triangle LightProbeGeometryInput input[3],
    uint instanceId : SV_GSInstanceID,
    inout TriangleStream<LightProbeGeometryOutput> output)
{
    LightProbeGeometryOutput vertex;
    vertex.arrayIndex = instanceId;

    [unroll]
    for (uint index = 0u; index < 3u; ++index)
    {
        vertex.position = input[index].position;
        vertex.uv = input[index].uv;
        output.Append(vertex);
    }
}

float LightProbeRadicalInverse(uint value)
{
    value = ((value & 0x55555555u) << 1u) |
        ((value & 0xaaaaaaaau) >> 1u);
    value = ((value & 0x33333333u) << 2u) |
        ((value & 0xccccccccu) >> 2u);
    value = ((value & 0x0f0f0f0fu) << 4u) |
        ((value & 0xf0f0f0f0u) >> 4u);
    value = ((value & 0x00ff00ffu) << 8u) |
        ((value & 0xff00ff00u) >> 8u);
    value = (value << 16u) | (value >> 16u);
    return float(value) * 2.3283064365386963e-10f;
}

float2 LightProbeHammersley(uint index, uint sampleCount)
{
    return float2(
        float(index) / float(sampleCount),
        LightProbeRadicalInverse(index));
}

float3 LightProbeCubeDirection(float2 uv, uint face)
{
    float3 direction = float3(uv.x * 2.f - 1.f, 1.f - uv.y * 2.f, 1.f);
    switch (face)
    {
    case 0u: direction = float3(direction.z, direction.y, -direction.x); break;
    case 1u: direction = float3(-direction.z, direction.y, direction.x); break;
    case 2u: direction = float3(direction.x, direction.z, -direction.y); break;
    case 3u: direction = float3(direction.x, -direction.z, direction.y); break;
    case 4u: break;
    case 5u: direction = float3(-direction.x, direction.y, -direction.z); break;
    }
    return normalize(direction);
}

void LightProbeOrthonormalBasis(
    float3 normal,
    out float3 tangent,
    out float3 bitangent)
{
    const float signValue = normal.z >= 0.f ? 1.f : -1.f;
    const float a = -1.f / (signValue + normal.z);
    const float b = normal.x * normal.y * a;
    tangent = float3(
        1.f + signValue * normal.x * normal.x * a,
        signValue * b,
        -signValue * normal.x);
    bitangent = float3(
        b,
        signValue + normal.y * normal.y * a,
        -normal.y);
}

float3 LightProbeImportanceSampleGgx(float2 random, float roughness)
{
    const float alpha = roughness * roughness;
    const float phi = 2.f * UVSR_LIGHT_PROBE_PI * random.x;
    const float cosine = sqrt(
        (1.f - random.y) /
        (1.f + (alpha * alpha - 1.f) * random.y));
    const float sine = sqrt(1.f - cosine * cosine);
    return float3(sine * cos(phi), sine * sin(phi), cosine);
}

float LightProbeSmithGOverNdotV(
    float roughness,
    float normalDotView,
    float normalDotLight)
{
    const float alpha = roughness * roughness;
    const float alphaSquared = alpha * alpha;
    const float viewTerm = normalDotView * sqrt(
        alphaSquared +
        (1.f - alphaSquared) * normalDotLight * normalDotLight);
    const float lightTerm = normalDotLight * sqrt(
        alphaSquared +
        (1.f - alphaSquared) * normalDotView * normalDotView);
    return 2.f * normalDotLight / (viewTerm + lightTerm);
}

cbuffer c_LightProbe : register(b0)
{
    LightProbeProcessingConstants g_LightProbe;
};

TextureCube<float4> t_EnvironmentMap : register(t0);
SamplerState s_EnvironmentMapSampler : register(s0);

void mip_ps(
    in LightProbeGeometryOutput input,
    out float4 color : SV_Target0)
{
    const float3 direction = LightProbeCubeDirection(
        input.uv, input.arrayIndex);
    color = t_EnvironmentMap.SampleLevel(
        s_EnvironmentMapSampler, direction, 0.f);
}

void specular_probe_ps(
    in LightProbeGeometryOutput input,
    out float4 color : SV_Target0)
{
    const float3 normal = LightProbeCubeDirection(
        input.uv, input.arrayIndex);
    float3 tangent;
    float3 bitangent;
    LightProbeOrthonormalBasis(normal, tangent, bitangent);

    float4 totalRadiance = 0.f;
    float totalWeight = 0.f;
    for (uint index = 0u; index < g_LightProbe.sampleCount; ++index)
    {
        const float3 localHalfVector = LightProbeImportanceSampleGgx(
            LightProbeHammersley(index, g_LightProbe.sampleCount),
            g_LightProbe.roughness);
        const float3 halfVector =
            localHalfVector.x * tangent +
            localHalfVector.y * bitangent +
            localHalfVector.z * normal;
        const float3 lightDirection = reflect(-normal, halfVector);
        const float normalDotLight = saturate(
            dot(normal, lightDirection));
        if (normalDotLight > 0.f)
        {
            totalRadiance += t_EnvironmentMap.SampleLevel(
                s_EnvironmentMapSampler,
                lightDirection,
                g_LightProbe.lodBias) * normalDotLight;
            totalWeight += normalDotLight;
        }
    }
    color = totalRadiance / totalWeight;
}

void environment_brdf_ps(
    in float4 position : SV_Position,
    in float2 uv : UV,
    out float4 color : SV_Target0)
{
    const float normalDotView = uv.x;
    const float roughness = uv.y;
    const float3 viewDirection = float3(
        sqrt(1.f - normalDotView * normalDotView),
        0.f,
        normalDotView);

    float scale = 0.f;
    float bias = 0.f;
    static const uint sampleCount = 1024u;
    for (uint index = 0u; index < sampleCount; ++index)
    {
        const float3 halfVector = LightProbeImportanceSampleGgx(
            LightProbeHammersley(index, sampleCount), roughness);
        const float3 lightDirection =
            2.f * dot(viewDirection, halfVector) * halfVector - viewDirection;
        const float normalDotLight = saturate(lightDirection.z);
        const float normalDotHalf = saturate(halfVector.z);
        const float viewDotHalf = saturate(
            dot(viewDirection, halfVector));
        if (normalDotLight > 0.f)
        {
            const float geometry = LightProbeSmithGOverNdotV(
                roughness, normalDotView, normalDotLight);
            const float visibility = geometry * viewDotHalf / normalDotHalf;
            const float fresnel = pow(1.f - viewDotHalf, 5.f);
            scale += (1.f - fresnel) * visibility;
            bias += fresnel * visibility;
        }
    }
    color = float4(float2(scale, bias) / float(sampleCount), 0.f, 0.f);
}
