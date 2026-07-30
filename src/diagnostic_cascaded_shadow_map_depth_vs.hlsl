#pragma pack_matrix(row_major)

#include <donut/shaders/depth_cb.h>
#include <donut/shaders/bindless.h>
#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/packing.hlsli>

#ifndef DIAGNOSTIC_CSM_PRECOMPUTED_DEPTH_AXIS
#define DIAGNOSTIC_CSM_PRECOMPUTED_DEPTH_AXIS 0
#endif

#ifndef DIAGNOSTIC_CSM_CONSERVATIVE_SATURATED_SLOPE
#define DIAGNOSTIC_CSM_CONSERVATIVE_SATURATED_SLOPE 0
#endif

#ifndef DIAGNOSTIC_CSM_ALGEBRAIC_SLOW_SLOPE
#define DIAGNOSTIC_CSM_ALGEBRAIC_SLOW_SLOPE 0
#endif

#ifndef DIAGNOSTIC_CSM_TRANSLATION_ONLY_CASTER_TRANSFORM
#define DIAGNOSTIC_CSM_TRANSLATION_ONLY_CASTER_TRANSFORM 0
#endif

DECLARE_CBUFFER(
    DepthPassConstants,
    g_Depth,
    DEPTH_BINDING_VIEW_CONSTANTS,
    DEPTH_SPACE_VIEW);

#ifdef TARGET_D3D11
ByteAddressBuffer t_Instances : REGISTER_SRV(
    DEPTH_BINDING_INSTANCE_BUFFER,
    DEPTH_SPACE_INPUT);
#else
StructuredBuffer<InstanceData> t_Instances : REGISTER_SRV(
    DEPTH_BINDING_INSTANCE_BUFFER,
    DEPTH_SPACE_INPUT);
#endif
ByteAddressBuffer t_Vertices : REGISTER_SRV(
    DEPTH_BINDING_VERTEX_BUFFER,
    DEPTH_SPACE_INPUT);

struct DiagnosticCsmDepthPushConstants
{
    uint startInstanceLocation;
    uint startVertexLocation;
    uint positionOffset;
    uint texCoordOffset;
    uint normalOffset;
    float constantDepthBias;
    float slopeDepthBias;
#if DIAGNOSTIC_CSM_PRECOMPUTED_DEPTH_AXIS
    float inverseDepthAxisLength;
#else
    float maximumSlopeDepthBias;
#endif
#if DIAGNOSTIC_CSM_TRANSLATION_ONLY_CASTER_TRANSFORM
    float3 translationOnlyWorldTranslation;
    uint casterTransformMode;
#endif
};

DECLARE_PUSH_CONSTANTS(
    DiagnosticCsmDepthPushConstants,
    g_Push,
    DEPTH_BINDING_PUSH_CONSTANTS,
    DEPTH_SPACE_INPUT);

bool TryTransformDiagnosticCsmNormalUeStyle(
    float3 objectNormal,
    float3x4 objectToWorld,
    out float3 worldNormal)
{
    // Donut's affineToColumnMajor packing becomes three row-major float4
    // rows in HLSL. The first three columns are therefore the transformed
    // local basis vectors.
    const float3 localXAxisWorld = float3(
        objectToWorld[0][0],
        objectToWorld[1][0],
        objectToWorld[2][0]);
    const float3 localYAxisWorld = float3(
        objectToWorld[0][1],
        objectToWorld[1][1],
        objectToWorld[2][1]);
    const float3 localZAxisWorld = float3(
        objectToWorld[0][2],
        objectToWorld[1][2],
        objectToWorld[2][2]);
    const float3 axisLengthSquared = float3(
        dot(localXAxisWorld, localXAxisWorld),
        dot(localYAxisWorld, localYAxisWorld),
        dot(localZAxisWorld, localZAxisWorld));

    const bool valid =
        all(isfinite(objectNormal)) &&
        all(isfinite(axisLengthSquared)) &&
        all(axisLengthSquared > 1e-12f);
    [branch]
    if (!valid)
    {
        worldNormal = 0.0f;
        return false;
    }

    // This matches UE's finite TRS treatment: remove per-axis scale before
    // rotating the local normal. It intentionally follows UE's basis-axis
    // normalization for shear instead of introducing a different
    // inverse-transpose convention in this reference path.
    const float3 inverseAxisLength = rsqrt(axisLengthSquared);
    worldNormal = mul(
        objectToWorld,
        float4(objectNormal * inverseAxisLength, 0.0f));
    const float worldNormalLengthSquared =
        dot(worldNormal, worldNormal);
    if (!all(isfinite(worldNormal)) ||
        !isfinite(worldNormalLengthSquared) ||
        !(worldNormalLengthSquared > 1e-12f))
    {
        worldNormal = 0.0f;
        return false;
    }

    return true;
}

float4 GetBiasedShadowPositionFromWorld(
    float3 worldPosition,
    float3 worldNormal,
    bool hasNormal)
{
    float4 position = mul(
        float4(worldPosition, 1.0f),
        g_Depth.matWorldToClip);

    // UE clamps whole-scene directional casters to the shadow near plane.
    // Without this, triangles crossing the configured light-depth boundary are
    // clipped and can leave cut silhouettes in the cascade.
    position.z = max(position.z, 0.0f);

    // UE vertex factories provide normals. Fail conservatively to its maximum
    // clamped slope when a Donut buffer lacks one or contains a degenerate
    // value, instead of silently dropping the slope term.
#if DIAGNOSTIC_CSM_PRECOMPUTED_DEPTH_AXIS
    const float maximumSlopeDepthBias = 1.0f;
    float slope = maximumSlopeDepthBias;
#else
    float slope = g_Push.maximumSlopeDepthBias;
#endif
    if (hasNormal)
    {
        const float3 worldDepthAxis = float3(
            g_Depth.matWorldToClip[0][2],
            g_Depth.matWorldToClip[1][2],
            g_Depth.matWorldToClip[2][2]);
        const float normalLengthSquared = dot(worldNormal, worldNormal);
#if DIAGNOSTIC_CSM_PRECOMPUTED_DEPTH_AXIS && DIAGNOSTIC_CSM_CONSERVATIVE_SATURATED_SLOPE && DIAGNOSTIC_CSM_ALGEBRAIC_SLOW_SLOPE
        const float projectedNumerator =
            dot(worldNormal, worldDepthAxis) *
            g_Push.inverseDepthAxisLength;
        const float projectedNumeratorSquared =
            projectedNumerator * projectedNumerator;
        // One float step below the exact NoL-squared saturation boundary.
        // The reference clamped slope is already one throughout this range.
        const float saturatedNoLSquaredThreshold = 0.49999997f;
        const float saturationLimit =
            saturatedNoLSquaredThreshold *
            normalLengthSquared;
        const bool optimizedSlopeInputsValid =
            isfinite(g_Push.inverseDepthAxisLength) &&
            isfinite(projectedNumeratorSquared) &&
            isfinite(saturationLimit) &&
            normalLengthSquared > 1e-12f &&
            g_Push.inverseDepthAxisLength > 0.0f;
        [branch]
        if (optimizedSlopeInputsValid &&
            projectedNumeratorSquared <= saturationLimit)
        {
            slope = maximumSlopeDepthBias;
        }
        else if (optimizedSlopeInputsValid)
        {
            const float projectedMagnitude =
                abs(projectedNumerator);
            slope = projectedMagnitude > 0.0f
                ? sqrt(max(
                    normalLengthSquared -
                        projectedNumeratorSquared,
                    0.0f)) / projectedMagnitude
                : maximumSlopeDepthBias;
            slope = clamp(
                slope, 0.0f, maximumSlopeDepthBias);
        }
        else
        {
            if (normalLengthSquared > 1e-12f &&
                g_Push.inverseDepthAxisLength > 0.0f)
            {
                const float noL = saturate(abs(
                    dot(worldNormal, worldDepthAxis) *
                    rsqrt(normalLengthSquared) *
                    g_Push.inverseDepthAxisLength));
                slope = noL > 1e-6f
                    ? sqrt(saturate(1.0f - noL * noL)) / noL
                    : maximumSlopeDepthBias;
                slope = clamp(
                    slope, 0.0f, maximumSlopeDepthBias);
            }
        }
#elif DIAGNOSTIC_CSM_PRECOMPUTED_DEPTH_AXIS && DIAGNOSTIC_CSM_CONSERVATIVE_SATURATED_SLOPE
        const float projectedNumerator =
            dot(worldNormal, worldDepthAxis) *
            g_Push.inverseDepthAxisLength;
        const float projectedNumeratorSquared =
            projectedNumerator * projectedNumerator;
        // One float step below the exact NoL-squared saturation boundary.
        // The reference clamped slope is already one throughout this range.
        const float saturatedNoLSquaredThreshold = 0.49999997f;
        const float saturationLimit =
            saturatedNoLSquaredThreshold *
            normalLengthSquared;
        const bool saturatedFastPathInputsValid =
            isfinite(g_Push.inverseDepthAxisLength) &&
            isfinite(projectedNumeratorSquared) &&
            isfinite(saturationLimit) &&
            normalLengthSquared > 1e-12f &&
            g_Push.inverseDepthAxisLength > 0.0f;
        [branch]
        if (saturatedFastPathInputsValid &&
            projectedNumeratorSquared <= saturationLimit)
        {
            slope = maximumSlopeDepthBias;
        }
        else
        {
            if (normalLengthSquared > 1e-12f &&
                g_Push.inverseDepthAxisLength > 0.0f)
            {
                const float noL = saturate(abs(
                    dot(worldNormal, worldDepthAxis) *
                    rsqrt(normalLengthSquared) *
                    g_Push.inverseDepthAxisLength));
                slope = noL > 1e-6f
                    ? sqrt(saturate(1.0f - noL * noL)) / noL
                    : maximumSlopeDepthBias;
                slope = clamp(
                    slope, 0.0f, maximumSlopeDepthBias);
            }
        }
#elif DIAGNOSTIC_CSM_PRECOMPUTED_DEPTH_AXIS && DIAGNOSTIC_CSM_ALGEBRAIC_SLOW_SLOPE
        const float projectedNumerator =
            dot(worldNormal, worldDepthAxis) *
            g_Push.inverseDepthAxisLength;
        const float projectedNumeratorSquared =
            projectedNumerator * projectedNumerator;
        const bool algebraicSlopeInputsValid =
            isfinite(g_Push.inverseDepthAxisLength) &&
            isfinite(projectedNumeratorSquared) &&
            isfinite(normalLengthSquared) &&
            normalLengthSquared > 1e-12f &&
            g_Push.inverseDepthAxisLength > 0.0f;
        [branch]
        if (algebraicSlopeInputsValid)
        {
            const float projectedMagnitude =
                abs(projectedNumerator);
            slope = projectedMagnitude > 0.0f
                ? sqrt(max(
                    normalLengthSquared -
                        projectedNumeratorSquared,
                    0.0f)) / projectedMagnitude
                : maximumSlopeDepthBias;
            slope = clamp(
                slope, 0.0f, maximumSlopeDepthBias);
        }
        else
        {
            if (normalLengthSquared > 1e-12f &&
                g_Push.inverseDepthAxisLength > 0.0f)
            {
                const float noL = saturate(abs(
                    dot(worldNormal, worldDepthAxis) *
                    rsqrt(normalLengthSquared) *
                    g_Push.inverseDepthAxisLength));
                slope = noL > 1e-6f
                    ? sqrt(saturate(1.0f - noL * noL)) / noL
                    : maximumSlopeDepthBias;
                slope = clamp(
                    slope, 0.0f, maximumSlopeDepthBias);
            }
        }
#elif DIAGNOSTIC_CSM_PRECOMPUTED_DEPTH_AXIS
        if (normalLengthSquared > 1e-12f &&
            g_Push.inverseDepthAxisLength > 0.0f)
        {
            const float noL = saturate(abs(
                dot(worldNormal, worldDepthAxis) *
                rsqrt(normalLengthSquared) *
                g_Push.inverseDepthAxisLength));
            slope = noL > 1e-6f
                ? sqrt(saturate(1.0f - noL * noL)) / noL
                : maximumSlopeDepthBias;
            slope = clamp(
                slope, 0.0f, maximumSlopeDepthBias);
        }
#else
        const float axisLengthSquared = dot(worldDepthAxis, worldDepthAxis);
        if (normalLengthSquared > 1e-12f && axisLengthSquared > 1e-12f)
        {
            const float noL = saturate(abs(dot(
                worldNormal * rsqrt(normalLengthSquared),
                worldDepthAxis * rsqrt(axisLengthSquared))));
            slope = noL > 1e-6f
                ? sqrt(saturate(1.0f - noL * noL)) / noL
                : g_Push.maximumSlopeDepthBias;
            slope = clamp(
                slope, 0.0f, g_Push.maximumSlopeDepthBias);
        }
#endif
    }

    const float normalizedBias = g_Push.constantDepthBias +
        g_Push.slopeDepthBias * slope;
    position.z += max(normalizedBias, 0.0f) * position.w;
    return position;
}

float4 GetBiasedShadowPosition(
    uint vertexId,
    uint instanceId)
{
    instanceId += g_Push.startInstanceLocation;
    vertexId += g_Push.startVertexLocation;

    const float3 objectPosition = asfloat(t_Vertices.Load3(
        g_Push.positionOffset + vertexId * c_SizeOfPosition));
    const bool hasNormal = g_Push.normalOffset != ~0u;
    float3 objectNormal = 0.0f;
    if (hasNormal)
    {
        const uint packedNormal = t_Vertices.Load(
            g_Push.normalOffset + vertexId * c_SizeOfNormal);
        objectNormal = Unpack_RGB8_SNORM(packedNormal);
    }

#if DIAGNOSTIC_CSM_TRANSLATION_ONLY_CASTER_TRANSFORM
    const bool useTranslationOnlyTransform =
        g_Push.casterTransformMode == 1u;
    float3 worldPosition;
    float3 worldNormal = 0.0f;
    bool worldNormalValid = hasNormal;
    [branch]
    if (useTranslationOnlyTransform)
    {
        worldPosition =
            objectPosition + g_Push.translationOnlyWorldTranslation;
        worldNormal = objectNormal;
    }
    else
    {
#ifdef TARGET_D3D11
        const InstanceData instance = LoadInstanceData(
            t_Instances, instanceId * c_SizeOfInstanceData);
#else
        const InstanceData instance = t_Instances[instanceId];
#endif
        worldPosition = mul(
            instance.transform,
            float4(objectPosition, 1.0f));
        if (hasNormal)
        {
            worldNormalValid =
                TryTransformDiagnosticCsmNormalUeStyle(
                    objectNormal,
                    instance.transform,
                    worldNormal);
        }
    }
#else
#ifdef TARGET_D3D11
    const InstanceData instance = LoadInstanceData(
        t_Instances, instanceId * c_SizeOfInstanceData);
#else
    const InstanceData instance = t_Instances[instanceId];
#endif
    const float3 worldPosition = mul(
        instance.transform,
        float4(objectPosition, 1.0f));
    float3 worldNormal = 0.0f;
    bool worldNormalValid = hasNormal;
    if (hasNormal)
    {
        worldNormalValid =
            TryTransformDiagnosticCsmNormalUeStyle(
                objectNormal,
                instance.transform,
                worldNormal);
    }
#endif
    return GetBiasedShadowPositionFromWorld(
        worldPosition, worldNormal, worldNormalValid);
}

float4 GetInputAssemblerBiasedShadowPosition(
    float3 objectPosition,
    float3 objectNormal,
    float3x4 instanceTransform)
{
    const float3 worldPosition = mul(
        instanceTransform,
        float4(objectPosition, 1.0f));
    float3 worldNormal = 0.0f;
    const bool worldNormalValid =
        TryTransformDiagnosticCsmNormalUeStyle(
            objectNormal,
            instanceTransform,
            worldNormal);
    return GetBiasedShadowPositionFromWorld(
        worldPosition, worldNormal, worldNormalValid);
}

// Opaque depth rendering has no pixel shader and therefore no interpolants.
void main(
    in uint vertexId : SV_VertexID,
    in uint instanceId : SV_InstanceID,
    out float4 position : SV_Position)
{
    position = GetBiasedShadowPosition(vertexId, instanceId);
}

// Alpha-tested casters share the identical biased position path and retain
// only the texture coordinate required by Donut's existing alpha test.
void alpha_tested(
    in uint vertexId : SV_VertexID,
    in uint instanceId : SV_InstanceID,
    out float4 position : SV_Position,
    out float2 texCoord : TEXCOORD)
{
    position = GetBiasedShadowPosition(vertexId, instanceId);
    vertexId += g_Push.startVertexLocation;
    texCoord = asfloat(t_Vertices.Load2(
        g_Push.texCoordOffset + vertexId * c_SizeOfTexcoord));
}

// The experimental IA path is restricted to rigid, non-translation casters
// with complete position, UV, normal, and instance buffers. It shares the
// exact projection, near-plane clamp, reverse-Z convention, and shader bias
// calculation with the manual-fetch reference path.
void main_input_assembler(
    in float3 objectPosition : POSITION,
    in float2 texCoord : TEXCOORD,
    in float4 objectNormal : NORMAL,
    in float3x4 instanceTransform : TRANSFORM,
    out float4 position : SV_Position)
{
    position = GetInputAssemblerBiasedShadowPosition(
        objectPosition,
        objectNormal.xyz,
        instanceTransform);
}

void alpha_tested_input_assembler(
    in float3 objectPosition : POSITION,
    in float2 inputTexCoord : TEXCOORD,
    in float4 objectNormal : NORMAL,
    in float3x4 instanceTransform : TRANSFORM,
    out float4 position : SV_Position,
    out float2 texCoord : TEXCOORD)
{
    position = GetInputAssemblerBiasedShadowPosition(
        objectPosition,
        objectNormal.xyz,
        instanceTransform);
    texCoord = inputTexCoord;
}
