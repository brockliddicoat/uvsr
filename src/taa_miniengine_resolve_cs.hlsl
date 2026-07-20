//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard
//
// Adapted only for UVSR's RGBA16F scene target and arbitrary output
// dimensions from Microsoft/DirectX-Graphics-Samples commit
// 357ade6ec6ff0d9dcadc48f35c7a28e37c0cdf7a.
//

#include "taa_miniengine_options_shared.h"
#include "taa_miniengine_debug.hlsli"

#ifndef TAA_DEBUG_VIEW
#error TAA_DEBUG_VIEW must be a compile-time shader define
#endif

Texture2D<float4> TemporalColor : register(t0);
Texture2D<float2> DebugValues : register(t1);
RWTexture2D<float4> OutColor : register(u0);

cbuffer InlineConstants : register(b0)
{
    float2 UnusedSharpenWeights;
    uint2 BufferDim;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= BufferDim))
        return;

#if TAA_DEBUG_VIEW == UVSR_TAA_DEBUG_STABLE_INTERIOR
    float value = DebugValues[DTid.xy].x;
    OutColor[DTid.xy] = float4(
        MiniEngineTaaDebugHeatmap(value),
        1.0);
#elif TAA_DEBUG_VIEW == UVSR_TAA_DEBUG_FINAL_HISTORY_WEIGHT
    float storedConfidence = TemporalColor[DTid.xy].w;
    float baseHistoryWeight = saturate(
        2.0 - rcp(max(storedConfidence, 1e-6)));
    float stableInteriorScore = DebugValues[DTid.xy].x;
    float reductionTarget = min(
        baseHistoryWeight,
        UVSR_TAA_STABLE_INTERIOR_FLOOR);
    float value = lerp(
        baseHistoryWeight,
        reductionTarget,
        stableInteriorScore);
    OutColor[DTid.xy] = float4(
        MiniEngineTaaDebugHeatmap(value),
        1.0);
#elif TAA_DEBUG_VIEW == UVSR_TAA_DEBUG_SAMPLE_RESURRECTION
    // Packed developer diagnostic:
    //   0.00        ineligible
    //   0.25        eligible, no candidate accepted
    //   0.50..0.70  first older frame, contribution in the offset
    //   0.75..0.95  second older frame, contribution in the offset
    float value = DebugValues[DTid.xy].y;
    float3 debugColor = 0.0;
    if (value > 0.0 && value < 0.5)
    {
        debugColor = float3(1.0, 0.0, 0.0);
    }
    else if (value >= 0.5 && value < 0.75)
    {
        float contribution = saturate((value - 0.5) / 0.2);
        debugColor = float3(
            0.0,
            0.25 + 0.75 * contribution,
            0.0);
    }
    else if (value >= 0.75)
    {
        float contribution = saturate((value - 0.75) / 0.2);
        debugColor = float3(
            0.0,
            0.0,
            0.25 + 0.75 * contribution);
    }
    OutColor[DTid.xy] = float4(debugColor, 1.0);
#else
    float4 Color = TemporalColor[DTid.xy];
    OutColor[DTid.xy] = float4(
        Color.rgb / max(Color.w, 1e-6),
        1.0);
#endif
}
