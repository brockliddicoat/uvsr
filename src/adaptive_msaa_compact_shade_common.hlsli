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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Copyright (c) 2017-2025 The Forge Interactive Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */

// Exact provenance:
// - The visibility-only resource flow and descriptor-once MSAA classification
//   are adapted from The Forge Visibility_Buffer sample at commit
//   a00d6d301b9fd01a1edc99f7c4e38996ce1bee0b (Apache-2.0), especially
//   MsaaEdgeDetect.frag.fsl and VisibilityBufferShade.frag.fsl.
// - UVSR replaces Forge's transient filtered-primitive key with its exact
//   generation-bearing uint2 SurfaceKey, reconstructs Donut geometry and
//   material inputs, and validates every indirection before use.
// - Material evaluation, transforms, motion vectors, G-buffer packing, and
//   PBR semantics are reproduced from UVSR/Donut at UVSR base commit
//   578496047c4753077e36f39a68df8c2c323cee58 and pinned Donut commit
//   bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 (MIT).
// Required third-party notices are also maintained in UVSR's central notices.

#ifndef UVSR_ADAPTIVE_MSAA_COMPACT_SHADE_COMMON_HLSLI
#define UVSR_ADAPTIVE_MSAA_COMPACT_SHADE_COMMON_HLSLI

#pragma pack_matrix(row_major)

#include <donut/shaders/bindless.h>
#include <donut/shaders/binding_helpers.hlsli>
#include <donut/shaders/gbuffer.hlsli>
#include <donut/shaders/gbuffer_cb.h>
#include <donut/shaders/motion_vectors.hlsli>
#include <donut/shaders/packing.hlsli>
#include "adaptive_msaa_raster_cb.h"
#define ENABLE_METAL_ROUGH_RECONSTRUCTION 1
#include <donut/shaders/scene_material.hlsli>
#include "pbr_gbuffer.hlsli"

#ifndef ADAPTIVE_MSAA_COMPACT_SAMPLES
#error ADAPTIVE_MSAA_COMPACT_SAMPLES must be a static 2, 4, 8, or 16.
#endif

#if ADAPTIVE_MSAA_COMPACT_SAMPLES != 2 && \
    ADAPTIVE_MSAA_COMPACT_SAMPLES != 4 && \
    ADAPTIVE_MSAA_COMPACT_SAMPLES != 8 && \
    ADAPTIVE_MSAA_COMPACT_SAMPLES != 16
#error Unsupported compact visibility MSAA sample count.
#endif

#ifndef WHITE_WORLD
#define WHITE_WORLD 0
#endif

static const uint CompactInvalidWord = 0xffffffffu;
static const uint CompactClearedVisibilityWord =
    ADAPTIVE_MSAA_CLEARED_VISIBILITY_WORD;
static const uint CompactGenerationShift = 20u;
static const uint CompactSlotMask = 0x000fffffu;
static const uint CompactGenerationMask = 0x00000fffu;
static const uint CompactMinimumGeneration = 1u;
static const uint CompactMaximumGeneration = 0x00000ffeu;
static const uint CompactInstanceValid = 1u << 0u;
static const uint CompactInstanceDeformationSupported = 1u << 1u;
static const uint CompactInstanceMaterialSupported = 1u << 2u;
static const uint CompactGeometrySkinned = 1u << 0u;
static const uint CompactGeometryAlphaTested = 1u << 1u;
static const uint CompactGeometryDoubleSided = 1u << 2u;
static const uint CompactGeometryPayloadInvalid = 0u;
static const uint CompactGeometryPayloadReconstruct = 1u;
static const uint CompactGeometryPayloadFullGBufferFallback = 2u;

static const uint CompactReasonNone = 0u;
static const uint CompactReasonInvalidSentinel = 1u;
static const uint CompactReasonInvalidHandleEncoding = 2u;
static const uint CompactReasonSlotOutOfRange = 3u;
static const uint CompactReasonGenerationMismatch = 4u;
static const uint CompactReasonTopologyEpochMismatch = 5u;
static const uint CompactReasonLodRevisionMismatch = 6u;
static const uint CompactReasonDeformationRevisionMismatch = 7u;
static const uint CompactReasonMaterialRevisionMismatch = 8u;
static const uint CompactReasonCapacityOverflow = 9u;
static const uint CompactReasonPrimitiveOverflow = 10u;
static const uint CompactReasonTransientMappingFailure = 14u;
static const uint CompactReasonUnsupportedDeformation = 15u;
static const uint CompactReasonUnsupportedMaterial = 16u;
static const uint CompactReasonUnsafeSharing = 17u;
static const uint CompactReasonReconstructionFailure = 18u;
static const uint CompactReasonFormatUnsupported = 19u;
static const uint CompactReasonWorkListOverflow = 21u;
static const uint CompactReasonTextureContentRevisionMismatch = 22u;
static const uint CompactReasonTextureQualificationMismatch = 23u;

static const uint CompactVisibilityClassBackground =
    ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND;
static const uint CompactVisibilityClassReconstruct =
    ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT;
static const uint CompactVisibilityClassLocalFallbackBit =
    ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_FALLBACK_BIT;
static const uint CompactVisibilityClassFallbackBit =
    ADAPTIVE_MSAA_VISIBILITY_CLASS_FALLBACK_BIT;
static const uint CompactVisibilityClassReasonMask =
    ADAPTIVE_MSAA_VISIBILITY_CLASS_REASON_MASK;

static const float CompactRelativeDepthTolerance = 1.0f / 4096.0f;
static const float CompactPositionFootprintFraction = 1.0f / 8.0f;
static const int CompactRasterSubpixelScale = 256;
static const int CompactRasterPixelCenter = 128;

struct CompactGeometryInstanceGpuRecord
{
    uint generation;
    uint geometryIndex;
    uint instanceIndex;
    uint geometryRecordIndex;

    uint stableInstanceHandleLow;
    uint stableInstanceHandleHigh;
    uint topologyEpochLow;
    uint topologyEpochHigh;

    uint topologyRevision;
    uint lodRevision;
    uint deformationRevision;
    uint materialRevision;

    uint primitiveCount;
    uint flags;
    uint payloadClass;
    uint payloadFallbackReason;
};

struct CompactGeometryGpuRecord
{
    uint indexBufferDescriptor;
    uint vertexBufferDescriptor;
    uint indexOffset;
    uint positionOffset;

    uint previousPositionOffset;
    uint texCoordOffset;
    uint normalOffset;
    uint tangentOffset;

    uint indexCount;
    uint vertexCount;
    uint materialIndex;
    uint flags;
};

struct CompactSceneGpuConstants
{
    uint topologyEpochLow;
    uint topologyEpochHigh;
    uint topologyRevision;
    uint lodRevision;

    uint geometryInstanceRecordCount;
    uint geometryRecordCount;
    uint materialRecordCount;
    uint instanceHandleCount;

    uint instanceBufferDescriptor;
    uint materialBufferDescriptor;
    uint rawBufferDescriptorCount;
    uint textureDescriptorCount;

    uint deformationRevision;
    uint materialRevision;
    uint descriptorCount;
    uint reserved0;
};

cbuffer c_CompactShade : register(b1)
{
    GBufferFillConstants g_CompactGBuffer;
    CompactSceneGpuConstants g_CompactScene;
    uint2 g_CompactSnapshotBuildSerial;
    uint g_CompactSampleCount;
    uint g_CompactFrontCounterClockwise;
    uint g_CompactPreparationQueueCapacity;
    uint g_CompactReserved0;
    uint g_CompactReserved1;
    uint g_CompactReserved2;
};

Texture2DMS<float, ADAPTIVE_MSAA_COMPACT_SAMPLES>
    t_CompactDepth : register(t8);
Texture2DMS<uint2, ADAPTIVE_MSAA_COMPACT_SAMPLES>
    t_CompactSurfaceOwnership : register(t21);
StructuredBuffer<CompactGeometryInstanceGpuRecord>
    t_CompactGeometryInstances : register(t22);
StructuredBuffer<CompactGeometryGpuRecord>
    t_CompactGeometry : register(t23);
StructuredBuffer<uint> t_CompactInstanceHandles : register(t24);
Texture2DMS<uint, ADAPTIVE_MSAA_COMPACT_SAMPLES>
    t_CompactVisibilityClass : register(t26);
Texture2DMS<float4, ADAPTIVE_MSAA_COMPACT_SAMPLES>
    t_CompactFallbackDiffuse : register(t27);
Texture2DMS<float4, ADAPTIVE_MSAA_COMPACT_SAMPLES>
    t_CompactFallbackMaterial : register(t28);
Texture2DMS<float4, ADAPTIVE_MSAA_COMPACT_SAMPLES>
    t_CompactFallbackNormals : register(t29);
Texture2DMS<float4, ADAPTIVE_MSAA_COMPACT_SAMPLES>
    t_CompactFallbackEmissive : register(t30);
Texture2DMS<float, ADAPTIVE_MSAA_COMPACT_SAMPLES>
    t_CompactFallbackAmbientOcclusion : register(t31);
Texture2DMS<float4, ADAPTIVE_MSAA_COMPACT_SAMPLES>
    t_CompactFallbackMotion : register(t32);

Texture2D<float4> g_CompactTextures[] : register(t0, space6);
ByteAddressBuffer g_CompactRawBuffers[] : register(t0, space7);
SamplerState s_CompactMaterial : register(s0);

struct CompactInterpolation
{
    float3 affineBarycentrics;
    float3 perspectiveBarycentrics;
    float deviceDepth;
    uint valid;
};

struct CompactTriangle
{
    uint3 indices;
    uint previousPositionValid;
    float3 objectPosition[3];
    float3 previousObjectPosition[3];
    float2 texCoord[3];
    float3 worldPosition[3];
    float3 previousWorldPosition[3];
    float3 worldNormal[3];
    float4 worldTangent[3];
    float4 clipPosition[3];
    float2 windowPosition[3];
    int2 rasterPosition[3];
};

struct CompactSurface
{
    uint valid;
    uint reason;
    uint2 key;
    uint frontFace;
    float3 hitBarycentrics;
    float deviceDepth;
    float centerDeviceDepth;
    float3 triangleWorldPosition;
    float3 lightingWorldPosition;
    float linearDepth;
    float pixelFootprint;
    float4 channel0;
    float4 channel1;
    float4 channel2;
    float4 channel3;
    float ambientOcclusion;
    float4 motion;
};

bool CompactSameSurface(uint2 first, uint2 second)
{
    const uint generation =
        (first.x >> CompactGenerationShift) & CompactGenerationMask;
    return all(first == second) &&
        first.x != CompactInvalidWord &&
        first.y != CompactInvalidWord &&
        generation >= CompactMinimumGeneration &&
        generation <= CompactMaximumGeneration;
}

bool CompactIsClearedVisibilityKey(uint2 key)
{
    return all(key == CompactClearedVisibilityWord);
}

bool CompactFinite(float value)
{
    return isfinite(value);
}

bool CompactFinite2(float2 value)
{
    return all(isfinite(value));
}

bool CompactFinite3(float3 value)
{
    return all(isfinite(value));
}

bool CompactFinite4(float4 value)
{
    return all(isfinite(value));
}

bool CompactBoundedFloat(float value, float maximumAbsoluteValue)
{
    return CompactFinite(value) &&
        abs(value) <= maximumAbsoluteValue;
}

bool CompactBoundedFloat2(float2 value, float maximumAbsoluteValue)
{
    return CompactFinite2(value) &&
        all(abs(value) <= maximumAbsoluteValue);
}

bool CompactBoundedFloat3(float3 value, float maximumAbsoluteValue)
{
    return CompactFinite3(value) &&
        all(abs(value) <= maximumAbsoluteValue);
}

bool CompactConditionedClipVertex(
    float4 clipPosition,
    out float2 windowPosition)
{
    windowPosition = 0.0f;
    if (!CompactFinite4(clipPosition) ||
        clipPosition.w < AdaptiveMsaaCompactMinimumClipW ||
        clipPosition.w > AdaptiveMsaaCompactMaximumClipW)
    {
        return false;
    }

    const float3 ndc = clipPosition.xyz / clipPosition.w;
    if (!CompactFinite3(ndc) ||
        abs(ndc.z) > AdaptiveMsaaCompactMaximumAbsoluteNdcZ)
    {
        return false;
    }
    precise float2 invariantWindowPosition = ndc.xy *
            g_CompactGBuffer.view.clipToWindowScale +
        g_CompactGBuffer.view.clipToWindowBias;
    windowPosition = invariantWindowPosition;
    return CompactFinite2(windowPosition) &&
        all(windowPosition >=
            AdaptiveMsaaCompactMinimumWindowCoordinate) &&
        all(windowPosition <=
            AdaptiveMsaaCompactMaximumWindowCoordinate);
}

bool CompactGetRuntimeSamplePosition(
    uint sampleIndex,
    out float2 sampleOffset);

bool CompactSnapRasterPosition(
    float2 windowPosition,
    out int2 rasterPosition,
    out float2 snappedWindowPosition)
{
    rasterPosition = 0;
    snappedWindowPosition = 0.0f;
    if (!CompactFinite2(windowPosition))
        return false;
    if (any(windowPosition <
            AdaptiveMsaaCompactMinimumWindowCoordinate) ||
        any(windowPosition >
            AdaptiveMsaaCompactMaximumWindowCoordinate))
    {
        return false;
    }

    const float2 scaled =
        windowPosition * float(CompactRasterSubpixelScale);
    const float2 snapped = round(scaled);
    // D3D12_VIEWPORT_BOUNDS_MIN/MAX are -32768 and 32767. Constraining the
    // snapped 16.8 values to that domain keeps subtraction exact in int32 and
    // the largest determinant within 49 signed bits.
    if (!CompactFinite2(scaled) || !CompactFinite2(snapped) ||
        any(snapped < -8388608.0f) ||
        any(snapped > 8388352.0f))
    {
        return false;
    }

    rasterPosition = int2(snapped);
    snappedWindowPosition = float2(rasterPosition) /
        float(CompactRasterSubpixelScale);
    return true;
}

int64_t CompactOrientRaster(int2 first, int2 second, int2 samplePoint)
{
    return (int64_t(second.x) - int64_t(first.x)) *
            (int64_t(samplePoint.y) - int64_t(first.y)) -
        (int64_t(second.y) - int64_t(first.y)) *
            (int64_t(samplePoint.x) - int64_t(first.x));
}

bool CompactRasterEdgeIncludesSample(
    int2 first,
    int2 second,
    int2 samplePoint,
    int windingSign)
{
    const int64_t rawEdge =
        CompactOrientRaster(first, second, samplePoint);
    const int64_t normalizedEdge = windingSign > 0
        ? rawEdge
        : -rawEdge;
    const int64_t dx = int64_t(windingSign) *
        (int64_t(second.x) - int64_t(first.x));
    const int64_t dy = int64_t(windingSign) *
        (int64_t(second.y) - int64_t(first.y));
    // Render-target Y grows down. After winding normalization, directed top
    // or left edges are inclusive and their opposite shared edges are not.
    const bool inclusive = dy < 0 || (dy == 0 && dx > 0);
    return normalizedEdge > 0 ||
        (normalizedEdge == 0 && inclusive);
}

bool CompactRasterSampleCovered(
    CompactTriangle surfaceTriangle,
    int2 pixelPosition,
    uint sampleIndex,
    out int64_t signedArea)
{
    signedArea = CompactOrientRaster(
        surfaceTriangle.rasterPosition[0],
        surfaceTriangle.rasterPosition[1],
        surfaceTriangle.rasterPosition[2]);
    if (signedArea == 0)
        return false;

    float2 sampleOffset;
    if (!CompactGetRuntimeSamplePosition(sampleIndex, sampleOffset))
        return false;
    const float2 fixedOffsetFloat = round(
        sampleOffset * float(CompactRasterSubpixelScale));
    if (!CompactFinite2(fixedOffsetFloat) ||
        any(sampleOffset * float(CompactRasterSubpixelScale) !=
            fixedOffsetFloat))
    {
        return false;
    }
    const int2 samplePoint = pixelPosition * CompactRasterSubpixelScale +
        CompactRasterPixelCenter + int2(fixedOffsetFloat);
    const int windingSign = signedArea > 0 ? 1 : -1;
    return CompactRasterEdgeIncludesSample(
            surfaceTriangle.rasterPosition[0],
            surfaceTriangle.rasterPosition[1],
            samplePoint,
            windingSign) &&
        CompactRasterEdgeIncludesSample(
            surfaceTriangle.rasterPosition[1],
            surfaceTriangle.rasterPosition[2],
            samplePoint,
            windingSign) &&
        CompactRasterEdgeIncludesSample(
            surfaceTriangle.rasterPosition[2],
            surfaceTriangle.rasterPosition[0],
            samplePoint,
            windingSign);
}

bool CompactGetPrimitiveRasterCoverage(
    CompactTriangle surfaceTriangle,
    int2 pixelPosition,
    out uint coverageMask,
    out int64_t signedArea)
{
    coverageMask = 0u;
    signedArea = 0;
    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        ++sampleIndex)
    {
        int64_t sampleArea;
        if (CompactRasterSampleCovered(
                surfaceTriangle,
                pixelPosition,
                sampleIndex,
                sampleArea))
        {
            coverageMask |= 1u << sampleIndex;
        }
        if (sampleIndex == 0u)
            signedArea = sampleArea;
    }
    return signedArea != 0;
}

// This analytic 16.8 coverage mirror is production reconstruction metadata,
// never identity. CPU preflight rejects every clip-plane touch/crossing and
// proves stable original-vertex snapping, while GetSamplePosition is required
// by Direct3D to return the bound sample pattern on its exact 1/16-pixel grid
// (which is exactly representable at 16.8). Final ownership still comes only
// from the multisampled key written after fixed-function depth testing.

bool CompactElementFits(
    uint byteOffset,
    uint elementIndex,
    uint elementStride,
    uint bufferByteSize)
{
    if (elementStride == 0u || byteOffset > bufferByteSize ||
        elementStride > bufferByteSize - byteOffset)
    {
        return false;
    }
    return elementIndex <=
        (bufferByteSize - byteOffset - elementStride) / elementStride;
}

bool CompactDescriptorPartitionValid()
{
    return g_CompactScene.textureDescriptorCount <=
            g_CompactScene.descriptorCount &&
        g_CompactScene.rawBufferDescriptorCount ==
            g_CompactScene.descriptorCount -
                g_CompactScene.textureDescriptorCount;
}

bool CompactRawBufferDescriptorValid(uint descriptor)
{
    return CompactDescriptorPartitionValid() &&
        descriptor != CompactInvalidWord &&
        descriptor >= g_CompactScene.textureDescriptorCount &&
        descriptor < g_CompactScene.descriptorCount;
}

bool CompactTextureIndexValid(int descriptor)
{
    return CompactDescriptorPartitionValid() &&
        descriptor >= 0 &&
        uint(descriptor) < g_CompactScene.textureDescriptorCount;
}

uint CompactValidateKey(uint2 key, out uint slot, out uint generation)
{
    slot = 0u;
    generation = 0u;
    if (key.x == CompactInvalidWord || key.y == CompactInvalidWord)
        return CompactReasonInvalidSentinel;
    slot = key.x & CompactSlotMask;
    generation = (key.x >> CompactGenerationShift) &
        CompactGenerationMask;
    if (generation < CompactMinimumGeneration ||
        generation > CompactMaximumGeneration)
    {
        return CompactReasonInvalidHandleEncoding;
    }
    if (slot >= g_CompactScene.geometryInstanceRecordCount)
        return CompactReasonSlotOutOfRange;
    return CompactReasonNone;
}

bool CompactDecodeVisibilityClass(
    uint visibilityClass,
    out bool reconstruct,
    out bool fallback,
    out bool localFallback,
    out uint fallbackReason)
{
    reconstruct = visibilityClass == CompactVisibilityClassReconstruct;
    fallback = (visibilityClass & CompactVisibilityClassFallbackBit) != 0u;
    localFallback = fallback &&
        (visibilityClass & CompactVisibilityClassLocalFallbackBit) != 0u;
    fallbackReason = visibilityClass & CompactVisibilityClassReasonMask;
    const bool localReasonAllowed =
        fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_DEFORMATION ||
        fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_UNSUPPORTED_MATERIAL ||
        fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_RECONSTRUCTION_FAILURE ||
        fallbackReason ==
            ADAPTIVE_MSAA_VISIBILITY_CLASS_LOCAL_REASON_TEXTURE_QUALIFICATION_MISMATCH;
    return reconstruct ||
        (visibilityClass <= 0xffu && fallback &&
            fallbackReason != CompactReasonNone &&
            fallbackReason <=
                ADAPTIVE_MSAA_VISIBILITY_CLASS_MAX_REASON &&
            (!localFallback || localReasonAllowed));
}

// Fallback G-buffer samples still carry the same generation-bearing identity.
// Validate that ownership and every explicit revision without following a
// reconstruction-only geometry/material descriptor.
uint CompactValidateFallbackOwnershipKey(
    uint2 key,
    uint expectedFallbackReason,
    bool localFallback)
{
    uint geometryInstanceCount;
    uint geometryInstanceStride;
    t_CompactGeometryInstances.GetDimensions(
        geometryInstanceCount, geometryInstanceStride);
    uint handleCount;
    uint handleStride;
    t_CompactInstanceHandles.GetDimensions(handleCount, handleStride);
    if (geometryInstanceStride != 64u || handleStride != 4u ||
        geometryInstanceCount !=
            g_CompactScene.geometryInstanceRecordCount ||
        handleCount != g_CompactScene.instanceHandleCount)
    {
        return CompactReasonFormatUnsupported;
    }

    uint slot;
    uint generation;
    uint reason = CompactValidateKey(key, slot, generation);
    if (reason != CompactReasonNone)
        return reason;

    CompactGeometryInstanceGpuRecord geometryInstance =
        t_CompactGeometryInstances[slot];
    if (geometryInstance.generation != generation)
        return CompactReasonGenerationMismatch;
    if ((geometryInstance.flags & CompactInstanceValid) == 0u ||
        (geometryInstance.stableInstanceHandleLow == 0u &&
            geometryInstance.stableInstanceHandleHigh == 0u))
    {
        return CompactReasonTransientMappingFailure;
    }
    const bool localReasonAllowed =
        expectedFallbackReason == CompactReasonUnsupportedDeformation ||
        expectedFallbackReason == CompactReasonUnsupportedMaterial ||
        expectedFallbackReason == CompactReasonReconstructionFailure ||
        expectedFallbackReason == CompactReasonTextureQualificationMismatch;
    const bool payloadMatches = localFallback
        ? localReasonAllowed &&
            geometryInstance.payloadClass ==
                CompactGeometryPayloadReconstruct &&
            geometryInstance.payloadFallbackReason == CompactReasonNone
        : geometryInstance.payloadClass ==
                CompactGeometryPayloadFullGBufferFallback &&
            geometryInstance.payloadFallbackReason ==
                expectedFallbackReason;
    if (!payloadMatches || expectedFallbackReason == CompactReasonNone ||
        expectedFallbackReason >
            ADAPTIVE_MSAA_VISIBILITY_CLASS_MAX_REASON)
    {
        return CompactReasonTransientMappingFailure;
    }
    if (geometryInstance.topologyEpochLow !=
            g_CompactScene.topologyEpochLow ||
        geometryInstance.topologyEpochHigh !=
            g_CompactScene.topologyEpochHigh ||
        geometryInstance.topologyRevision !=
            g_CompactScene.topologyRevision)
    {
        return CompactReasonTopologyEpochMismatch;
    }
    if (geometryInstance.lodRevision != g_CompactScene.lodRevision)
        return CompactReasonLodRevisionMismatch;
    if (geometryInstance.deformationRevision !=
            g_CompactScene.deformationRevision)
    {
        return CompactReasonDeformationRevisionMismatch;
    }
    if (geometryInstance.materialRevision !=
            g_CompactScene.materialRevision)
    {
        return CompactReasonMaterialRevisionMismatch;
    }
    if (key.y >= geometryInstance.primitiveCount)
        return CompactReasonPrimitiveOverflow;
    if (!CompactRawBufferDescriptorValid(
            g_CompactScene.instanceBufferDescriptor))
    {
        return CompactReasonTransientMappingFailure;
    }

    const uint instanceDescriptor = NonUniformResourceIndex(
        g_CompactScene.instanceBufferDescriptor);
    uint instanceBytes;
    g_CompactRawBuffers[instanceDescriptor].GetDimensions(instanceBytes);
    if (!CompactElementFits(
            0u,
            geometryInstance.instanceIndex,
            c_SizeOfInstanceData,
            instanceBytes))
    {
        return CompactReasonReconstructionFailure;
    }
    InstanceData instance = LoadInstanceData(
        g_CompactRawBuffers[instanceDescriptor],
        geometryInstance.instanceIndex * c_SizeOfInstanceData);
    if (geometryInstance.geometryIndex >= instance.numGeometries ||
        instance.firstGeometryInstanceIndex >
            CompactInvalidWord - geometryInstance.geometryIndex)
    {
        return CompactReasonTransientMappingFailure;
    }
    const uint handleIndex = instance.firstGeometryInstanceIndex +
        geometryInstance.geometryIndex;
    if (handleIndex >= handleCount ||
        t_CompactInstanceHandles[handleIndex] != key.x)
    {
        return CompactReasonTransientMappingFailure;
    }
    return CompactReasonNone;
}

bool CompactValidDeviceDepth(float deviceDepth)
{
    // The immutable CPU proof rejects any primitive whose fixed-function D32
    // interpolation/storage interval can reach the reverse-Z clear value.
    // A final key paired with zero therefore proves transaction drift.
    return CompactFinite(deviceDepth) &&
        deviceDepth > 0.0f && deviceDepth <= 1.0f;
}

bool CompactValidFallbackDeviceDepth(float deviceDepth)
{
    // Exact key/class identity distinguishes a covered far-plane sample from
    // reverse-Z clear background, so the full-G-buffer path may retain zero.
    return CompactFinite(deviceDepth) &&
        deviceDepth >= 0.0f && deviceDepth <= 1.0f;
}

CompactInterpolation CompactInterpolateTriangle(
    CompactTriangle surfaceTriangle,
    float2 evaluationPosition)
{
    CompactInterpolation result = (CompactInterpolation)0;
    const float2 p0 = surfaceTriangle.windowPosition[0];
    const float2 p1 = surfaceTriangle.windowPosition[1];
    const float2 p2 = surfaceTriangle.windowPosition[2];
    const float denominator =
        (p1.y - p2.y) * (p0.x - p2.x) +
        (p2.x - p1.x) * (p0.y - p2.y);
    if (!CompactFinite(denominator) ||
        abs(denominator) <=
            AdaptiveMsaaCompactMinimumProjectedTriangleArea)
        return result;

    result.affineBarycentrics.x =
        ((p1.y - p2.y) * (evaluationPosition.x - p2.x) +
         (p2.x - p1.x) * (evaluationPosition.y - p2.y)) /
        denominator;
    result.affineBarycentrics.y =
        ((p2.y - p0.y) * (evaluationPosition.x - p2.x) +
         (p0.x - p2.x) * (evaluationPosition.y - p2.y)) /
        denominator;
    result.affineBarycentrics.z = 1.0f -
        result.affineBarycentrics.x - result.affineBarycentrics.y;
    if (!CompactBoundedFloat3(
            result.affineBarycentrics,
            AdaptiveMsaaCompactMaximumAbsoluteAffineBarycentric))
    {
        return result;
    }

    float3 reciprocalW = float3(
        rcp(surfaceTriangle.clipPosition[0].w),
        rcp(surfaceTriangle.clipPosition[1].w),
        rcp(surfaceTriangle.clipPosition[2].w));
    const float perspectiveDenominator = dot(
        result.affineBarycentrics,
        reciprocalW);
    if (!CompactFinite3(reciprocalW) ||
        !CompactFinite(perspectiveDenominator) ||
        perspectiveDenominator <
            AdaptiveMsaaCompactMinimumPerspectiveDenominator)
    {
        return result;
    }
    result.perspectiveBarycentrics =
        result.affineBarycentrics * reciprocalW /
        perspectiveDenominator;
    result.deviceDepth = dot(
        result.affineBarycentrics,
        float3(
            surfaceTriangle.clipPosition[0].z /
                surfaceTriangle.clipPosition[0].w,
            surfaceTriangle.clipPosition[1].z /
                surfaceTriangle.clipPosition[1].w,
            surfaceTriangle.clipPosition[2].z /
                surfaceTriangle.clipPosition[2].w));
    result.valid = CompactBoundedFloat3(
            result.perspectiveBarycentrics,
            AdaptiveMsaaCompactMaximumAbsolutePerspectiveBarycentric) &&
        CompactFinite(result.deviceDepth);
    return result;
}

float2 CompactInterpolateFloat2(
    float2 values[3],
    float3 perspectiveBarycentrics)
{
    return values[0] * perspectiveBarycentrics.x +
        values[1] * perspectiveBarycentrics.y +
        values[2] * perspectiveBarycentrics.z;
}

float3 CompactInterpolateFloat3(
    float3 values[3],
    float3 perspectiveBarycentrics)
{
    return values[0] * perspectiveBarycentrics.x +
        values[1] * perspectiveBarycentrics.y +
        values[2] * perspectiveBarycentrics.z;
}

float4 CompactInterpolateFloat4(
    float4 values[3],
    float3 perspectiveBarycentrics)
{
    return values[0] * perspectiveBarycentrics.x +
        values[1] * perspectiveBarycentrics.y +
        values[2] * perspectiveBarycentrics.z;
}

bool CompactGetRuntimeSamplePosition(
    uint sampleIndex,
    out float2 sampleOffset)
{
    sampleOffset = 0.0f;
    if (sampleIndex >= ADAPTIVE_MSAA_COMPACT_SAMPLES ||
        g_CompactSampleCount != ADAPTIVE_MSAA_COMPACT_SAMPLES)
    {
        return false;
    }
    // This query is intentionally made on the exact bound depth resource.
    // Quality level zero may expose a vendor pattern; hard-coded standard
    // tables are therefore test fixtures only and never runtime authority.
    sampleOffset = t_CompactDepth.GetSamplePosition(sampleIndex);
    return CompactFinite2(sampleOffset) &&
        all(sampleOffset >= -0.5f) &&
        all(sampleOffset < 0.5f);
}

uint CompactFirstCoveredSample(uint sameSurfaceMask)
{
    const uint fullMask =
        (1u << ADAPTIVE_MSAA_COMPACT_SAMPLES) - 1u;
    sameSurfaceMask &= fullMask;
    if (sameSurfaceMask == 0u || sameSurfaceMask == fullMask)
        return CompactInvalidWord;
    [unroll]
    for (uint sampleIndex = 0u;
        sampleIndex < ADAPTIVE_MSAA_COMPACT_SAMPLES;
        ++sampleIndex)
    {
        if ((sameSurfaceMask & (1u << sampleIndex)) != 0u)
            return sampleIndex;
    }
    return CompactInvalidWord;
}

uint CompactValidateTextureBindings(MaterialConstants material)
{
    if ((material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0 &&
        !CompactTextureIndexValid(material.baseOrDiffuseTextureIndex))
        return CompactReasonTransientMappingFailure;
    if ((material.flags & MaterialFlags_UseMetalRoughOrSpecularTexture) != 0 &&
        !CompactTextureIndexValid(material.metalRoughOrSpecularTextureIndex))
        return CompactReasonTransientMappingFailure;
    if ((material.flags & MaterialFlags_UseEmissiveTexture) != 0 &&
        !CompactTextureIndexValid(material.emissiveTextureIndex))
        return CompactReasonTransientMappingFailure;
    if ((material.flags & MaterialFlags_UseNormalTexture) != 0 &&
        (!CompactTextureIndexValid(material.normalTextureIndex) ||
            !CompactBoundedFloat(
                material.normalTextureScale,
                AdaptiveMsaaCompactMaximumAbsoluteNormalMapScale) ||
            !CompactBoundedFloat2(
                material.normalTextureTransformScale,
                AdaptiveMsaaCompactMaximumAbsoluteNormalTextureUvScale)))
    {
        return CompactTextureIndexValid(material.normalTextureIndex)
            ? CompactReasonUnsupportedMaterial
            : CompactReasonTransientMappingFailure;
    }
    if ((material.flags & MaterialFlags_UseOcclusionTexture) != 0 &&
        !CompactTextureIndexValid(material.occlusionTextureIndex))
        return CompactReasonTransientMappingFailure;
    if ((material.flags & MaterialFlags_UseTransmissionTexture) != 0 &&
        !CompactTextureIndexValid(material.transmissionTextureIndex))
        return CompactReasonTransientMappingFailure;
    if ((material.flags & MaterialFlags_UseOpacityTexture) != 0 &&
        !CompactTextureIndexValid(material.opacityTextureIndex))
        return CompactReasonTransientMappingFailure;
    return CompactReasonNone;
}

uint CompactSampleMaterialTexturesGrad(
    MaterialConstants material,
    float2 texCoord,
    float2 texCoordDx,
    float2 texCoordDy,
    out MaterialTextureSample values)
{
    values = DefaultMaterialTextures();
    if (!CompactBoundedFloat2(
            texCoord,
            AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient) ||
        !CompactBoundedFloat2(
            texCoordDx,
            AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient) ||
        !CompactBoundedFloat2(
            texCoordDy,
            AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient))
    {
        return CompactReasonReconstructionFailure;
    }
    if ((material.flags & MaterialFlags_UseBaseOrDiffuseTexture) != 0)
    {
        const uint index = NonUniformResourceIndex(
            uint(material.baseOrDiffuseTextureIndex));
        values.baseOrDiffuse = g_CompactTextures[index].SampleGrad(
            s_CompactMaterial, texCoord, texCoordDx, texCoordDy);
    }
    if ((material.flags & MaterialFlags_UseMetalRoughOrSpecularTexture) != 0)
    {
        const uint index = NonUniformResourceIndex(
            uint(material.metalRoughOrSpecularTextureIndex));
        values.metalRoughOrSpecular = g_CompactTextures[index].SampleGrad(
            s_CompactMaterial, texCoord, texCoordDx, texCoordDy);
    }
    if ((material.flags & MaterialFlags_UseEmissiveTexture) != 0)
    {
        const uint index = NonUniformResourceIndex(
            uint(material.emissiveTextureIndex));
        values.emissive = g_CompactTextures[index].SampleGrad(
            s_CompactMaterial, texCoord, texCoordDx, texCoordDy);
    }
    if ((material.flags & MaterialFlags_UseNormalTexture) != 0)
    {
        const uint index = NonUniformResourceIndex(
            uint(material.normalTextureIndex));
        const float2 scale = material.normalTextureTransformScale;
        const float2 scaledTexCoord = texCoord * scale;
        const float2 scaledTexCoordDx = texCoordDx * scale;
        const float2 scaledTexCoordDy = texCoordDy * scale;
        if (!CompactBoundedFloat2(
                scaledTexCoord,
                AdaptiveMsaaCompactMaximumAbsoluteTextureSampleOperand) ||
            !CompactBoundedFloat2(
                scaledTexCoordDx,
                AdaptiveMsaaCompactMaximumAbsoluteTextureSampleOperand) ||
            !CompactBoundedFloat2(
                scaledTexCoordDy,
                AdaptiveMsaaCompactMaximumAbsoluteTextureSampleOperand))
        {
            return CompactReasonUnsupportedMaterial;
        }
        values.normal = g_CompactTextures[index].SampleGrad(
            s_CompactMaterial,
            scaledTexCoord,
            scaledTexCoordDx,
            scaledTexCoordDy);
    }
    if ((material.flags & MaterialFlags_UseOcclusionTexture) != 0)
    {
        const uint index = NonUniformResourceIndex(
            uint(material.occlusionTextureIndex));
        values.occlusion = g_CompactTextures[index].SampleGrad(
            s_CompactMaterial, texCoord, texCoordDx, texCoordDy);
    }
    if ((material.flags & MaterialFlags_UseTransmissionTexture) != 0)
    {
        const uint index = NonUniformResourceIndex(
            uint(material.transmissionTextureIndex));
        values.transmission = g_CompactTextures[index].SampleGrad(
            s_CompactMaterial, texCoord, texCoordDx, texCoordDy);
    }
    if ((material.flags & MaterialFlags_UseOpacityTexture) != 0)
    {
        const uint index = NonUniformResourceIndex(
            uint(material.opacityTextureIndex));
        values.opacity = g_CompactTextures[index].SampleGrad(
            s_CompactMaterial, texCoord, texCoordDx, texCoordDy).r;
    }
    return CompactReasonNone;
}

uint CompactLoadTriangle(
    uint2 key,
    out CompactTriangle surfaceTriangle,
    out MaterialConstants material,
    out CompactGeometryGpuRecord geometry)
{
    surfaceTriangle = (CompactTriangle)0;
    material = (MaterialConstants)0;
    geometry = (CompactGeometryGpuRecord)0;

    uint geometryInstanceCount;
    uint geometryInstanceStride;
    t_CompactGeometryInstances.GetDimensions(
        geometryInstanceCount,
        geometryInstanceStride);
    uint geometryCount;
    uint geometryStride;
    t_CompactGeometry.GetDimensions(geometryCount, geometryStride);
    uint handleCount;
    uint handleStride;
    t_CompactInstanceHandles.GetDimensions(handleCount, handleStride);
    if (geometryInstanceStride != 64u || geometryStride != 48u ||
        handleStride != 4u ||
        geometryInstanceCount != g_CompactScene.geometryInstanceRecordCount ||
        geometryCount != g_CompactScene.geometryRecordCount ||
        handleCount != g_CompactScene.instanceHandleCount)
    {
        return CompactReasonFormatUnsupported;
    }

    uint slot;
    uint generation;
    uint reason = CompactValidateKey(key, slot, generation);
    if (reason != CompactReasonNone)
        return reason;

    CompactGeometryInstanceGpuRecord geometryInstance =
        t_CompactGeometryInstances[slot];
    if (geometryInstance.generation != generation)
        return CompactReasonGenerationMismatch;
    if ((geometryInstance.flags & CompactInstanceValid) == 0u)
        return CompactReasonTransientMappingFailure;
    if (geometryInstance.stableInstanceHandleLow == 0u &&
        geometryInstance.stableInstanceHandleHigh == 0u)
        return CompactReasonTransientMappingFailure;
    if ((geometryInstance.flags &
            CompactInstanceDeformationSupported) == 0u)
        return CompactReasonUnsupportedDeformation;
    if ((geometryInstance.flags & CompactInstanceMaterialSupported) == 0u)
        return CompactReasonUnsupportedMaterial;
    if (geometryInstance.payloadClass !=
            CompactGeometryPayloadReconstruct ||
        geometryInstance.payloadFallbackReason != CompactReasonNone)
    {
        return CompactReasonTransientMappingFailure;
    }
    if (geometryInstance.topologyEpochLow !=
            g_CompactScene.topologyEpochLow ||
        geometryInstance.topologyEpochHigh !=
            g_CompactScene.topologyEpochHigh ||
        geometryInstance.topologyRevision !=
            g_CompactScene.topologyRevision)
        return CompactReasonTopologyEpochMismatch;
    if (geometryInstance.lodRevision != g_CompactScene.lodRevision)
        return CompactReasonLodRevisionMismatch;
    if (geometryInstance.deformationRevision !=
            g_CompactScene.deformationRevision)
        return CompactReasonDeformationRevisionMismatch;
    if (geometryInstance.materialRevision !=
            g_CompactScene.materialRevision)
        return CompactReasonMaterialRevisionMismatch;
    if (key.y >= geometryInstance.primitiveCount)
        return CompactReasonPrimitiveOverflow;
    if (geometryInstance.geometryRecordIndex >= geometryCount)
        return CompactReasonTransientMappingFailure;

    geometry = t_CompactGeometry[
        geometryInstance.geometryRecordIndex];
    if (!CompactRawBufferDescriptorValid(
            geometry.indexBufferDescriptor) ||
        !CompactRawBufferDescriptorValid(
            geometry.vertexBufferDescriptor) ||
        !CompactRawBufferDescriptorValid(
            g_CompactScene.instanceBufferDescriptor) ||
        !CompactRawBufferDescriptorValid(
            g_CompactScene.materialBufferDescriptor))
    {
        return CompactReasonTransientMappingFailure;
    }
    if (geometry.materialIndex >= g_CompactScene.materialRecordCount)
        return CompactReasonTransientMappingFailure;
    if (geometry.indexCount == 0u ||
        geometry.indexCount % 3u != 0u ||
        geometry.indexCount / 3u != geometryInstance.primitiveCount ||
        geometry.vertexCount == 0u)
    {
        return CompactReasonReconstructionFailure;
    }
    if (key.y >= geometry.indexCount / 3u)
    {
        return CompactReasonPrimitiveOverflow;
    }

    const uint instanceDescriptor = NonUniformResourceIndex(
        g_CompactScene.instanceBufferDescriptor);
    const uint materialDescriptor = NonUniformResourceIndex(
        g_CompactScene.materialBufferDescriptor);
    const uint indexDescriptor = NonUniformResourceIndex(
        geometry.indexBufferDescriptor);
    const uint vertexDescriptor = NonUniformResourceIndex(
        geometry.vertexBufferDescriptor);
    uint instanceBytes;
    uint materialBytes;
    uint indexBytes;
    uint vertexBytes;
    g_CompactRawBuffers[instanceDescriptor].GetDimensions(instanceBytes);
    g_CompactRawBuffers[materialDescriptor].GetDimensions(materialBytes);
    g_CompactRawBuffers[indexDescriptor].GetDimensions(indexBytes);
    g_CompactRawBuffers[vertexDescriptor].GetDimensions(vertexBytes);

    if (!CompactElementFits(
            0u, geometryInstance.instanceIndex,
            c_SizeOfInstanceData, instanceBytes) ||
        !CompactElementFits(
            0u, geometry.materialIndex,
            c_SizeOfMaterialConstants, materialBytes) ||
        !CompactElementFits(
            geometry.indexOffset, key.y,
            c_SizeOfTriangleIndices, indexBytes))
    {
        return CompactReasonReconstructionFailure;
    }

    InstanceData instance = LoadInstanceData(
        g_CompactRawBuffers[instanceDescriptor],
        geometryInstance.instanceIndex * c_SizeOfInstanceData);
    if (geometryInstance.geometryIndex >= instance.numGeometries ||
        instance.firstGeometryInstanceIndex >
            CompactInvalidWord - geometryInstance.geometryIndex)
    {
        return CompactReasonTransientMappingFailure;
    }
    const uint handleIndex = instance.firstGeometryInstanceIndex +
        geometryInstance.geometryIndex;
    if (handleIndex >= handleCount ||
        t_CompactInstanceHandles[handleIndex] != key.x)
    {
        return CompactReasonTransientMappingFailure;
    }

    material = LoadMaterialConstants(
        g_CompactRawBuffers[materialDescriptor],
        geometry.materialIndex * c_SizeOfMaterialConstants);
    reason = CompactValidateTextureBindings(material);
    if (reason != CompactReasonNone)
        return reason;
    const bool geometryAlpha =
        (geometry.flags & CompactGeometryAlphaTested) != 0u;
    const bool materialAlpha =
        material.domain == MaterialDomain_AlphaTested;
    // Alpha coverage was finalized by the visibility raster pixel shader
    // using the ordinary Donut material bindings and implicit derivatives.
    // Compute validates the exact domain pairing and may sample opacity to
    // reconstruct the reference G-buffer value, but never uses reconstructed
    // opacity to re-discard or re-decide coverage for a surviving final key.
    if ((material.domain != MaterialDomain_Opaque && !materialAlpha) ||
        geometryAlpha != materialAlpha ||
        (((geometry.flags & CompactGeometryDoubleSided) != 0u) !=
            ((material.flags & MaterialFlags_DoubleSided) != 0)))
    {
        return CompactReasonUnsupportedMaterial;
    }
    if ((geometry.flags & CompactGeometrySkinned) != 0u &&
        geometry.previousPositionOffset == geometry.positionOffset)
    {
        return CompactReasonUnsupportedDeformation;
    }

    const uint indexAddress = geometry.indexOffset +
        key.y * c_SizeOfTriangleIndices;
    surfaceTriangle.indices =
        g_CompactRawBuffers[indexDescriptor].Load3(
        indexAddress);
    if (any(surfaceTriangle.indices >= geometry.vertexCount))
        return CompactReasonReconstructionFailure;
    surfaceTriangle.previousPositionValid = 1u;

    [unroll]
    for (uint vertex = 0u; vertex < 3u; ++vertex)
    {
        const uint index = surfaceTriangle.indices[vertex];
        if (!CompactElementFits(
                geometry.positionOffset, index,
                c_SizeOfPosition, vertexBytes) ||
            !CompactElementFits(
                geometry.texCoordOffset, index,
                c_SizeOfTexcoord, vertexBytes) ||
            !CompactElementFits(
                geometry.normalOffset, index,
                c_SizeOfNormal, vertexBytes) ||
            !CompactElementFits(
                geometry.tangentOffset, index,
                c_SizeOfNormal, vertexBytes))
        {
            return CompactReasonReconstructionFailure;
        }
        surfaceTriangle.objectPosition[vertex] = asfloat(
            g_CompactRawBuffers[vertexDescriptor].Load3(
                geometry.positionOffset + index * c_SizeOfPosition));
        const bool previousPositionFits = CompactElementFits(
            geometry.previousPositionOffset,
            index,
            c_SizeOfPosition,
            vertexBytes);
        if (previousPositionFits)
        {
            surfaceTriangle.previousObjectPosition[vertex] = asfloat(
                g_CompactRawBuffers[vertexDescriptor].Load3(
                    geometry.previousPositionOffset +
                    index * c_SizeOfPosition));
        }
        else
        {
            surfaceTriangle.previousObjectPosition[vertex] = 0.0f;
            surfaceTriangle.previousPositionValid = 0u;
        }
        surfaceTriangle.texCoord[vertex] = asfloat(
            g_CompactRawBuffers[vertexDescriptor].Load2(
                geometry.texCoordOffset + index * c_SizeOfTexcoord));
        const float3 objectNormal = Unpack_RGB8_SNORM(
            g_CompactRawBuffers[vertexDescriptor].Load(
                geometry.normalOffset + index * c_SizeOfNormal));
        const float4 objectTangent = Unpack_RGBA8_SNORM(
            g_CompactRawBuffers[vertexDescriptor].Load(
                geometry.tangentOffset + index * c_SizeOfNormal));

        precise float3 invariantWorldPosition;
        precise float4 invariantClipPosition;
        AdaptiveMsaaTransformPosition(
            surfaceTriangle.objectPosition[vertex],
            instance.transform,
            g_CompactGBuffer.view.matWorldToClip,
            invariantWorldPosition,
            invariantClipPosition);
        surfaceTriangle.worldPosition[vertex] =
            invariantWorldPosition;
        if (surfaceTriangle.previousPositionValid != 0u)
        {
            surfaceTriangle.previousWorldPosition[vertex] = mul(
                instance.prevTransform,
                float4(
                    surfaceTriangle.previousObjectPosition[vertex],
                    1.0f));
            if (!CompactFinite3(
                    surfaceTriangle.previousWorldPosition[vertex]))
            {
                surfaceTriangle.previousWorldPosition[vertex] = 0.0f;
                surfaceTriangle.previousPositionValid = 0u;
            }
        }
        surfaceTriangle.worldNormal[vertex] = mul(
            instance.transform,
            float4(objectNormal, 0.0f));
        surfaceTriangle.worldTangent[vertex].xyz = mul(
            instance.transform,
            float4(objectTangent.xyz, 0.0f));
        surfaceTriangle.worldTangent[vertex].w = objectTangent.w;
        surfaceTriangle.clipPosition[vertex] =
            invariantClipPosition;
        if (!CompactFinite2(surfaceTriangle.texCoord[vertex]) ||
            !CompactFinite3(surfaceTriangle.worldPosition[vertex]) ||
            !CompactFinite3(surfaceTriangle.worldNormal[vertex]) ||
            !CompactFinite4(surfaceTriangle.worldTangent[vertex]) ||
            !CompactFinite4(surfaceTriangle.clipPosition[vertex]))
        {
            return CompactReasonReconstructionFailure;
        }
        // Command-free CPU preflight proves the same complete transform,
        // divide, and viewport predicate over the immutable position bound.
        // The immutable per-triangle CPU proof additionally rejects every
        // canonical clip-plane touch/crossing, snap tie, post-snap
        // degeneracy, and orientation ambiguity. This runtime mirror must
        // therefore be unreachable for a covered key unless the transaction
        // identity changed after preflight.
        float2 unsnappedWindowPosition;
        if (!CompactConditionedClipVertex(
                surfaceTriangle.clipPosition[vertex],
                unsnappedWindowPosition))
        {
            return CompactReasonReconstructionFailure;
        }
        float2 snappedWindowPosition;
        if (!CompactSnapRasterPosition(
                unsnappedWindowPosition,
                surfaceTriangle.rasterPosition[vertex],
                snappedWindowPosition))
        {
            return CompactReasonReconstructionFailure;
        }
        // Direct3D establishes the interpolation plane after n.8 X/Y snap.
        // Keep the exact raster position for coverage and its floating-point
        // image for every affine/perspective attribute reconstruction.
        surfaceTriangle.windowPosition[vertex] =
            snappedWindowPosition;
    }

    const float2 continuousEdge01 =
        surfaceTriangle.windowPosition[1] -
        surfaceTriangle.windowPosition[0];
    const float2 continuousEdge02 =
        surfaceTriangle.windowPosition[2] -
        surfaceTriangle.windowPosition[0];
    const float continuousArea =
        continuousEdge01.x * continuousEdge02.y -
        continuousEdge01.y * continuousEdge02.x;
    const int64_t snappedArea = CompactOrientRaster(
        surfaceTriangle.rasterPosition[0],
        surfaceTriangle.rasterPosition[1],
        surfaceTriangle.rasterPosition[2]);
    const uint64_t absoluteSnappedArea = snappedArea < 0
        ? uint64_t(-snappedArea)
        : uint64_t(snappedArea);
    if (!CompactFinite(continuousArea) ||
        abs(continuousArea) <=
            AdaptiveMsaaCompactMinimumProjectedTriangleArea ||
        absoluteSnappedArea <
            AdaptiveMsaaCompactMinimumSnappedTriangleArea ||
        ((continuousArea > 0.0f) != (snappedArea > 0)))
    {
        return CompactReasonReconstructionFailure;
    }
    return CompactReasonNone;
}

uint CompactGetAnalyticCoarseGradients(
    CompactTriangle surfaceTriangle,
    int2 pixelPosition,
    out float2 texCoord,
    out float2 texCoordDx,
    out float2 texCoordDy,
    out CompactInterpolation centerInterpolation)
{
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    centerInterpolation = CompactInterpolateTriangle(
        surfaceTriangle, pixelCenter);
    if (centerInterpolation.valid == 0u)
        return CompactReasonReconstructionFailure;
    texCoord = CompactInterpolateFloat2(
        surfaceTriangle.texCoord,
        centerInterpolation.perspectiveBarycentrics);

    // Equal clip W reduces the raster interpolator to one affine UV plane.
    // Derive that plane's constant gradients from edge-relative UVs so a
    // large absolute coordinate cannot lose the much smaller one-pixel delta
    // when two independently reconstructed helper-lane values are subtracted.
    // Bit equality keeps every genuinely perspective primitive on the exact
    // finite-difference path below.
    const uint clipW0 = asuint(surfaceTriangle.clipPosition[0].w);
    const bool equalClipW =
        clipW0 == asuint(surfaceTriangle.clipPosition[1].w) &&
        clipW0 == asuint(surfaceTriangle.clipPosition[2].w);
    if (equalClipW)
    {
        const float2 p0 = surfaceTriangle.windowPosition[0];
        const float2 p1 = surfaceTriangle.windowPosition[1];
        const float2 p2 = surfaceTriangle.windowPosition[2];
        precise float inverseDenominator = rcp(
            (p1.y - p2.y) * (p0.x - p2.x) +
            (p2.x - p1.x) * (p0.y - p2.y));
        precise float2 affineBarycentricDx = float2(
            p1.y - p2.y,
            p2.y - p0.y) * inverseDenominator;
        precise float2 affineBarycentricDy = float2(
            p2.x - p1.x,
            p0.x - p2.x) * inverseDenominator;
        precise float2 texCoordEdge0 =
            surfaceTriangle.texCoord[0] - surfaceTriangle.texCoord[2];
        precise float2 texCoordEdge1 =
            surfaceTriangle.texCoord[1] - surfaceTriangle.texCoord[2];
        precise float2 affineTexCoordDx =
            texCoordEdge0 * affineBarycentricDx.x +
            texCoordEdge1 * affineBarycentricDx.y;
        precise float2 affineTexCoordDy =
            texCoordEdge0 * affineBarycentricDy.x +
            texCoordEdge1 * affineBarycentricDy.y;
        texCoordDx = affineTexCoordDx;
        texCoordDy = affineTexCoordDy;
    }
    else
    {
        // This is the target-GPU candidate for the unchanged G-buffer's
        // implicit texture gradients, not a portable proof of Sample parity.
        // D3D leaves the coarse derivative row/column and quad alignment to
        // the hardware implementation. Reconstruct finite 2x2-aligned
        // helper-lane deltas for the perspective case.
        // production enablement remains gated by the real full-G-buffer comparison
        // matrix for the selected adapter and driver.
        const int2 quadOrigin = pixelPosition & ~1;
        const float2 topLeftPosition = float2(quadOrigin) + 0.5f;
        const CompactInterpolation topLeftInterpolation =
            CompactInterpolateTriangle(surfaceTriangle, topLeftPosition);
        const CompactInterpolation topRightInterpolation =
            CompactInterpolateTriangle(
                surfaceTriangle, topLeftPosition + float2(1.0f, 0.0f));
        const CompactInterpolation bottomLeftInterpolation =
            CompactInterpolateTriangle(
                surfaceTriangle, topLeftPosition + float2(0.0f, 1.0f));
        if (topLeftInterpolation.valid == 0u ||
            topRightInterpolation.valid == 0u ||
            bottomLeftInterpolation.valid == 0u)
        {
            return CompactReasonReconstructionFailure;
        }
        const float2 topLeftTexCoord = CompactInterpolateFloat2(
            surfaceTriangle.texCoord,
            topLeftInterpolation.perspectiveBarycentrics);
        const float2 topRightTexCoord = CompactInterpolateFloat2(
            surfaceTriangle.texCoord,
            topRightInterpolation.perspectiveBarycentrics);
        const float2 bottomLeftTexCoord = CompactInterpolateFloat2(
            surfaceTriangle.texCoord,
            bottomLeftInterpolation.perspectiveBarycentrics);
        texCoordDx = topRightTexCoord - topLeftTexCoord;
        texCoordDy = bottomLeftTexCoord - topLeftTexCoord;
    }
    return CompactBoundedFloat2(
            texCoord,
            AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient) &&
        CompactBoundedFloat2(
            texCoordDx,
            AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient) &&
        CompactBoundedFloat2(
            texCoordDy,
            AdaptiveMsaaCompactMaximumAbsoluteTexCoordOrGradient)
        ? CompactReasonNone
        : CompactReasonReconstructionFailure;
}

uint CompactGetFeatureMask(MaterialConstants material)
{
    uint featureMask = 0u;
    if ((material.flags & MaterialFlags_SubsurfaceScattering) != 0)
        featureMask |= PbrFeature_Translucency | PbrFeature_Scattering;
    if (material.transmissionFactor > 0.0f)
        featureMask |= PbrFeature_Refraction;
    if ((material.flags & MaterialFlags_DoubleSided) != 0)
        featureMask |= PbrFeature_DoubleSided;
    return featureMask;
}

float3 CompactGetViewDirection(float3 surfacePosition)
{
    return g_CompactGBuffer.view.cameraDirectionOrPosition.w > 0.0f
        ? g_CompactGBuffer.view.cameraDirectionOrPosition.xyz -
            surfacePosition
        : -g_CompactGBuffer.view.cameraDirectionOrPosition.xyz;
}

bool CompactIsMirroredView()
{
    // Match the exact raster-state bit captured from IView::IsMirrored().
    // Re-deriving it from a float determinant can disagree near singular
    // transforms and invert double-sided normals.
    return g_CompactFrontCounterClockwise != 0u;
}

float CompactLinearViewDepth(float3 worldPosition)
{
    const float4 viewPosition = mul(
        float4(worldPosition, 1.0f),
        g_CompactGBuffer.view.matWorldToView);
    return abs(viewPosition.z / viewPosition.w);
}

float CompactPixelFootprint(
    int2 pixelPosition,
    float deviceDepth,
    float3 worldPosition)
{
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    const float3 worldRight = ReconstructWorldPosition(
        g_CompactGBuffer.view,
        pixelCenter + float2(1.0f, 0.0f),
        deviceDepth);
    const float3 worldDown = ReconstructWorldPosition(
        g_CompactGBuffer.view,
        pixelCenter + float2(0.0f, 1.0f),
        deviceDepth);
    return min(
        length(worldRight - worldPosition),
        length(worldDown - worldPosition));
}

CompactSurface CompactReconstructSurfaceWithTriangle(
    int2 pixelPosition,
    uint sampleIndex,
    uint sameSurfaceMask,
    uint2 key,
    float deviceDepth,
    out CompactTriangle surfaceTriangle)
{
    CompactSurface result = (CompactSurface)0;
    surfaceTriangle = (CompactTriangle)0;
    result.reason = CompactReasonReconstructionFailure;
    result.key = key;
    result.deviceDepth = deviceDepth;
    if (!CompactValidDeviceDepth(deviceDepth))
        return result;

    MaterialConstants material;
    CompactGeometryGpuRecord geometry;
    result.reason = CompactLoadTriangle(
        key, surfaceTriangle, material, geometry);
    if (result.reason != CompactReasonNone)
        return result;

    const uint fullMask =
        (1u << ADAPTIVE_MSAA_COMPACT_SAMPLES) - 1u;
    sameSurfaceMask &= fullMask;
    uint primitiveCoverageMask;
    int64_t primitiveSignedArea;
    if (!CompactGetPrimitiveRasterCoverage(
            surfaceTriangle,
            pixelPosition,
            primitiveCoverageMask,
            primitiveSignedArea))
    {
        return result;
    }
    primitiveCoverageMask &= fullMask;
    if (sampleIndex >= ADAPTIVE_MSAA_COMPACT_SAMPLES ||
        sameSurfaceMask == 0u || primitiveCoverageMask == 0u ||
        (sameSurfaceMask & (1u << sampleIndex)) == 0u ||
        (sameSurfaceMask & ~primitiveCoverageMask) != 0u)
    {
        return result;
    }

    float2 sampleOffset;
    if (!CompactGetRuntimeSamplePosition(sampleIndex, sampleOffset))
        return result;
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    CompactInterpolation hit = CompactInterpolateTriangle(
        surfaceTriangle, pixelCenter + sampleOffset);
    if (hit.valid == 0u)
        return result;
    // SurfaceKeys and depth are outputs of the same freshly cleared compact
    // raster pass. The exact final-key mask plus immutable key/epoch validation
    // establishes ownership after hardware clipping and depth testing without
    // a format-agnostic depth epsilon. The loaded DSV value remains
    // authoritative for reverse-Z lighting.
    result.hitBarycentrics = hit.perspectiveBarycentrics;

    float2 texCoord;
    float2 texCoordDx;
    float2 texCoordDy;
    CompactInterpolation center;
    result.reason = CompactGetAnalyticCoarseGradients(
        surfaceTriangle,
        pixelPosition,
        texCoord,
        texCoordDx,
        texCoordDy,
        center);
    if (result.reason != CompactReasonNone)
        return result;

    // SurfaceKey/depth survivors remain the only ownership identity. The
    // analytic mask is reconstruction metadata now that preflight rejects
    // clipping and proves the exact original-vertex snap. Reference centroid
    // interpolation is chosen from original primitive coverage before later
    // depth/stencil survivors are removed: full primitive coverage uses pixel
    // center, otherwise the lowest originally covered sample is authoritative
    // even when that sample was subsequently occluded.
    const uint centroidSample = primitiveCoverageMask == fullMask
        ? CompactInvalidWord
        : CompactFirstCoveredSample(primitiveCoverageMask);
    CompactInterpolation centroidInterpolation = center;
    if (centroidSample != CompactInvalidWord)
    {
        float2 centroidOffset;
        if (!CompactGetRuntimeSamplePosition(
                centroidSample, centroidOffset))
            return result;
        centroidInterpolation = CompactInterpolateTriangle(
            surfaceTriangle,
            pixelCenter + centroidOffset);
        if (centroidInterpolation.valid == 0u)
            return result;
    }

    float3 worldPosition = CompactInterpolateFloat3(
        surfaceTriangle.worldPosition,
        center.perspectiveBarycentrics);
    float3 previousWorldPosition = 0.0f;
    if (surfaceTriangle.previousPositionValid != 0u)
    {
        previousWorldPosition = CompactInterpolateFloat3(
            surfaceTriangle.previousWorldPosition,
            center.perspectiveBarycentrics);
        if (!CompactFinite3(previousWorldPosition))
        {
            previousWorldPosition = 0.0f;
            surfaceTriangle.previousPositionValid = 0u;
        }
    }
    float3 normal = CompactInterpolateFloat3(
        surfaceTriangle.worldNormal,
        centroidInterpolation.perspectiveBarycentrics);
    float4 tangent = CompactInterpolateFloat4(
        surfaceTriangle.worldTangent,
        centroidInterpolation.perspectiveBarycentrics);
    if (!CompactFinite3(worldPosition) ||
        !CompactFinite3(normal) || !CompactFinite4(tangent))
        return result;

    MaterialTextureSample textures;
    result.reason = CompactSampleMaterialTexturesGrad(
        material,
        texCoord,
        texCoordDx,
        texCoordDy,
        textures);
    if (result.reason != CompactReasonNone)
        return result;
#if WHITE_WORLD
    textures.baseOrDiffuse.rgb = 1.0f;
#endif
    MaterialSample surface = EvaluateSceneMaterial(
        normal, tangent, material, textures);
    const int64_t signedRasterArea = primitiveSignedArea;
    const uint64_t absoluteRasterArea = signedRasterArea < 0
        ? uint64_t(-signedRasterArea)
        : uint64_t(signedRasterArea);
    if (absoluteRasterArea <
        AdaptiveMsaaCompactMinimumSnappedTriangleArea)
        return result;
    result.frontFace = CompactIsMirroredView()
        ? signedRasterArea < 0
        : signedRasterArea > 0;
    const bool doubleSided =
        (material.flags & MaterialFlags_DoubleSided) != 0;
    if (ShouldFlipPbrSurfaceNormals(
            doubleSided,
            result.frontFace != 0u,
            surface.geometryNormal,
            CompactGetViewDirection(worldPosition)))
    {
        surface.shadingNormal = -surface.shadingNormal;
        surface.geometryNormal = -surface.geometryNormal;
    }

    const float dielectricF0 =
        (material.flags & MaterialFlags_UseSpecularGlossModel) == 0 &&
        material.specularColor.r > 0.0f
        ? saturate(material.specularColor.r)
        : IorToF0(1.5f);
    PbrGBufferData pbrData = (PbrGBufferData)0;
    pbrData.material.baseColor = surface.baseColor;
    pbrData.material.metalness = surface.metalness;
    pbrData.material.perceptualRoughness = surface.roughness;
    pbrData.material.dielectricF0 = dielectricF0;
    pbrData.material.emissive = surface.emissiveColor;
    pbrData.material.opacity = surface.opacity;
    pbrData.material.featureMask = CompactGetFeatureMask(material);
    pbrData.shadingNormal = surface.shadingNormal;
    pbrData.geometricNormal = surface.geometryNormal;
    pbrData.ambientOcclusion = surface.occlusion;
    EncodePbrGBuffer(
        pbrData,
        result.channel0,
        result.channel1,
        result.channel2,
        result.channel3,
        result.ambientOcclusion);

    result.centerDeviceDepth = center.deviceDepth;
    result.motion = 0.0f;
    if (surfaceTriangle.previousPositionValid != 0u)
    {
        const float4 previousClip = mul(
            float4(previousWorldPosition, 1.0f),
            g_CompactGBuffer.viewPrev.matWorldToClip);
        if (previousClip.w > 0.0f && CompactFinite(previousClip.w))
        {
            result.motion = float4(GetMotionVector(
                float3(pixelCenter, center.deviceDepth),
                previousWorldPosition,
                g_CompactGBuffer.view,
                g_CompactGBuffer.viewPrev), 1.0f);
            if (!CompactFinite4(result.motion))
                result.motion = 0.0f;
        }
    }

    // Reference deferred lighting reconstructs XY at pixel center and Z from
    // the exact per-sample DSV value. The true sample-position triangle hit
    // remains available above for identity and reconstruction validation but
    // must not silently redefine the established lighting convention.
    result.triangleWorldPosition = worldPosition;
    result.lightingWorldPosition = ReconstructWorldPosition(
        g_CompactGBuffer.view,
        pixelCenter,
        deviceDepth);
    result.linearDepth = CompactLinearViewDepth(
        result.lightingWorldPosition);
    result.pixelFootprint = CompactPixelFootprint(
        pixelPosition,
        deviceDepth,
        result.lightingWorldPosition);
    if (!CompactFinite3(result.hitBarycentrics) ||
        !CompactFinite(result.centerDeviceDepth) ||
        !CompactFinite3(result.triangleWorldPosition) ||
        !CompactFinite3(result.lightingWorldPosition) ||
        !CompactFinite(result.linearDepth) ||
        !CompactFinite(result.pixelFootprint) ||
        result.pixelFootprint < 0.0f ||
        !CompactFinite4(result.channel0) ||
        !CompactFinite4(result.channel1) ||
        !CompactFinite4(result.channel2) ||
        !CompactFinite4(result.channel3) ||
        !CompactFinite(result.ambientOcclusion) ||
        !CompactFinite4(result.motion))
    {
        return result;
    }

    result.valid = 1u;
    result.reason = CompactReasonNone;
    return result;
}

CompactSurface CompactReconstructSurface(
    int2 pixelPosition,
    uint sampleIndex,
    uint sameSurfaceMask,
    uint2 key,
    float deviceDepth)
{
    CompactTriangle ignoredTriangle;
    return CompactReconstructSurfaceWithTriangle(
        pixelPosition,
        sampleIndex,
        sameSurfaceMask,
        key,
        deviceDepth,
        ignoredTriangle);
}

uint CompactValidateTriangleHit(
    int2 pixelPosition,
    uint sampleIndex,
    float deviceDepth,
    CompactTriangle surfaceTriangle,
    out float3 hitBarycentrics)
{
    hitBarycentrics = 0.0f;
    if (!CompactValidDeviceDepth(deviceDepth))
        return CompactReasonReconstructionFailure;
    float2 sampleOffset;
    if (!CompactGetRuntimeSamplePosition(sampleIndex, sampleOffset))
        return CompactReasonReconstructionFailure;
    CompactInterpolation hit = CompactInterpolateTriangle(
        surfaceTriangle,
        float2(pixelPosition) + 0.5f + sampleOffset);
    if (hit.valid == 0u)
        return CompactReasonReconstructionFailure;
    hitBarycentrics = hit.perspectiveBarycentrics;
    return CompactFinite3(hitBarycentrics)
        ? CompactReasonNone
        : CompactReasonReconstructionFailure;
}

uint CompactValidateSurfaceHit(
    int2 pixelPosition,
    uint sampleIndex,
    uint2 key,
    float deviceDepth,
    out float3 hitBarycentrics)
{
    hitBarycentrics = 0.0f;
    if (!CompactValidDeviceDepth(deviceDepth))
        return CompactReasonReconstructionFailure;
    CompactTriangle surfaceTriangle;
    MaterialConstants material;
    CompactGeometryGpuRecord geometry;
    uint reason = CompactLoadTriangle(
        key, surfaceTriangle, material, geometry);
    if (reason != CompactReasonNone)
        return reason;
    return CompactValidateTriangleHit(
        pixelPosition,
        sampleIndex,
        deviceDepth,
        surfaceTriangle,
        hitBarycentrics);
}

CompactSurface CompactDeriveSampleFromSharedTriangle(
    int2 pixelPosition,
    uint sampleIndex,
    uint2 key,
    float deviceDepth,
    CompactSurface sharedSurface,
    CompactTriangle sharedTriangle)
{
    CompactSurface result = sharedSurface;
    result.valid = 0u;
    result.reason = CompactReasonReconstructionFailure;
    if (sharedSurface.valid == 0u ||
        !CompactSameSurface(sharedSurface.key, key))
        return result;
    result.reason = CompactValidateTriangleHit(
        pixelPosition,
        sampleIndex,
        deviceDepth,
        sharedTriangle,
        result.hitBarycentrics);
    if (result.reason != CompactReasonNone)
        return result;
    result.key = key;
    result.deviceDepth = deviceDepth;
    const float2 pixelCenter = float2(pixelPosition) + 0.5f;
    result.lightingWorldPosition = ReconstructWorldPosition(
        g_CompactGBuffer.view,
        pixelCenter,
        deviceDepth);
    result.linearDepth = CompactLinearViewDepth(
        result.lightingWorldPosition);
    result.pixelFootprint = CompactPixelFootprint(
        pixelPosition,
        deviceDepth,
        result.lightingWorldPosition);
    if (!CompactFinite3(result.lightingWorldPosition) ||
        !CompactFinite(result.linearDepth) ||
        !CompactFinite(result.pixelFootprint) ||
        result.pixelFootprint < 0.0f)
    {
        result.reason = CompactReasonReconstructionFailure;
        return result;
    }
    result.valid = 1u;
    result.reason = CompactReasonNone;
    return result;
}

bool CompactEqualFloatBits(float first, float second)
{
    return asuint(first) == asuint(second);
}

bool CompactEqualFloat3Bits(float3 first, float3 second)
{
    return all(asuint(first) == asuint(second));
}

bool CompactEqualFloat4Bits(float4 first, float4 second)
{
    return all(asuint(first) == asuint(second));
}

bool CompactCanShareShading(
    CompactSurface first,
    CompactSurface second)
{
    if (first.valid == 0u || second.valid == 0u ||
        !CompactSameSurface(first.key, second.key) ||
        first.frontFace != second.frontFace ||
        !CompactEqualFloat4Bits(first.channel0, second.channel0) ||
        !CompactEqualFloat4Bits(first.channel1, second.channel1) ||
        !CompactEqualFloat4Bits(first.channel2, second.channel2) ||
        !CompactEqualFloat4Bits(first.channel3, second.channel3) ||
        !CompactEqualFloatBits(
            first.ambientOcclusion,
            second.ambientOcclusion))
    {
        return false;
    }
    const float depthDifference = abs(
        first.linearDepth - second.linearDepth);
    const float depthScale = max(
        abs(first.linearDepth), abs(second.linearDepth));
    if ((depthScale == 0.0f && depthDifference != 0.0f) ||
        (depthScale != 0.0f && depthDifference / depthScale >
            CompactRelativeDepthTolerance))
    {
        return false;
    }
    const float positionSpan = length(
        first.lightingWorldPosition - second.lightingWorldPosition);
    const float smallerFootprint = min(
        first.pixelFootprint, second.pixelFootprint);
    return positionSpan <=
        CompactPositionFootprintFraction * smallerFootprint;
}

bool CompactCanShareCompleteRadiance(
    CompactSurface first,
    CompactSurface second)
{
    return CompactEqualFloatBits(
            first.deviceDepth,
            second.deviceDepth) &&
        CompactEqualFloat3Bits(
            first.lightingWorldPosition,
            second.lightingWorldPosition);
}

#endif // UVSR_ADAPTIVE_MSAA_COMPACT_SHADE_COMMON_HLSLI
