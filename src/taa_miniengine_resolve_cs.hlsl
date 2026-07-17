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
