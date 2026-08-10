//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Author:  James Stanard
//
// Adapted from Microsoft DirectX Graphics Samples for UVSR's RGBA16F scene
// target and arbitrary output dimensions. Distributed under
// legal/licenses/Microsoft-DirectX-Graphics-Samples-MIT.txt.
//

Texture2D<float4> TemporalColor : register(t0);
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

    float4 Color = TemporalColor[DTid.xy];
    OutColor[DTid.xy] = float4(
        Color.rgb / max(Color.w, 1e-6),
        1.0);
}
