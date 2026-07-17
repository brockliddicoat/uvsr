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
// Adapted for UVSR's NVRHI bindings, RGBA16F motion-validity channel,
// arbitrary output dimensions, and infinite reverse-Z device depth from
// Microsoft/DirectX-Graphics-Samples commit
// 357ade6ec6ff0d9dcadc48f35c7a28e37c0cdf7a.
//

#pragma pack_matrix(row_major)

static const uint kLdsPitch = 18;
static const uint kLdsRows = 10;
static const float kFarViewDepth = 65504.0f;

RWTexture2D<float4> OutTemporal : register(u0);
RWTexture2D<float> OutDepth : register(u1);

Texture2D<float4> VelocityBuffer : register(t0);
Texture2D<float3> InColor : register(t1);
Texture2D<float4> InTemporal : register(t2);
Texture2D<float> CurDepth : register(t3);
Texture2D<float> PreDepth : register(t4);

SamplerState LinearSampler : register(s0);

groupshared float ldsDepth[kLdsPitch * kLdsRows];
groupshared float ldsR[kLdsPitch * kLdsRows];
groupshared float ldsG[kLdsPitch * kLdsRows];
groupshared float ldsB[kLdsPitch * kLdsRows];

cbuffer CB1 : register(b1)
{
    float4x4 Projection;
    float2 RcpBufferDim;
    float TemporalBlendFactor;
    float RcpSpeedLimiter;
    float2 ViewportJitter;
    uint2 BufferDim;
    uint HistoryValid;
    float3 Padding;
}

void StoreRGB(uint ldsIdx, float3 RGB)
{
    ldsR[ldsIdx] = RGB.r;
    ldsG[ldsIdx] = RGB.g;
    ldsB[ldsIdx] = RGB.b;
}

float3 LoadRGB(uint ldsIdx)
{
    return float3(ldsR[ldsIdx], ldsG[ldsIdx], ldsB[ldsIdx]);
}

float2 STtoUV(float2 ST)
{
    return (ST + 0.5) * RcpBufferDim;
}

// These are MiniEngine's local, reversible blend-domain transforms, not a
// display tonemapper. Keeping them inside TAA preserves HDR highlight behavior
// while leaving UVSR's display pipeline entirely downstream.
float RGBToLuminance(float3 x)
{
    return dot(x, float3(0.212671, 0.715160, 0.072169));
}

float3 TM(float3 rgb)
{
    return rgb / (1 + RGBToLuminance(rgb));
}

float3 ITM(float3 rgb)
{
    return rgb / (1 - RGBToLuminance(rgb));
}

float3 ClipColor(
    float3 Color,
    float3 BoxMin,
    float3 BoxMax,
    float Dilation = 1.0)
{
    float3 BoxCenter = (BoxMax + BoxMin) * 0.5;
    float3 HalfDim = (BoxMax - BoxMin) * 0.5 * Dilation + 0.001;
    float3 Displacement = Color - BoxCenter;
    float3 Units = abs(Displacement / HalfDim);
    float MaxUnit = max(max(Units.x, Units.y), max(Units.z, 1.0));
    return BoxCenter + Displacement / MaxUnit;
}

void GetBBoxForPair(
    uint fillIdx,
    uint holeIdx,
    out float3 boxMin,
    out float3 boxMax)
{
    boxMin = boxMax = LoadRGB(fillIdx);
    float3 a = LoadRGB(fillIdx - kLdsPitch - 1);
    float3 b = LoadRGB(fillIdx - kLdsPitch + 1);
    boxMin = min(boxMin, min(a, b));
    boxMax = max(boxMax, max(a, b));
    a = LoadRGB(fillIdx + kLdsPitch - 1);
    b = LoadRGB(fillIdx + kLdsPitch + 1);
    boxMin = min(boxMin, min(a, b));
    boxMax = max(boxMax, max(a, b));
    a = LoadRGB(holeIdx);
    b = LoadRGB(holeIdx - fillIdx + holeIdx);
    boxMin = min(boxMin, min(a, b));
    boxMax = max(boxMax, max(a, b));
}

float MinOf(float4 depths)
{
    return min(min(depths.x, depths.y), min(depths.z, depths.w));
}

int2 GetClosestPixel(uint Idx, out float ClosestDepth)
{
    float DepthO = ldsDepth[Idx];
    float DepthW = ldsDepth[Idx - 1];
    float DepthE = ldsDepth[Idx + 1];
    float DepthN = ldsDepth[Idx - kLdsPitch];
    float DepthS = ldsDepth[Idx + kLdsPitch];

    // MiniEngine selects min forward-linear depth. UVSR's reverse-Z mapping is
    // monotonic in the opposite direction, so max selects the same surface.
    ClosestDepth = max(DepthO, max(max(DepthW, DepthE), max(DepthN, DepthS)));

    if (DepthN == ClosestDepth)
        return int2(0, -1);
    else if (DepthS == ClosestDepth)
        return int2(0, +1);
    else if (DepthW == ClosestDepth)
        return int2(-1, 0);
    else if (DepthE == ClosestDepth)
        return int2(+1, 0);

    return int2(0, 0);
}

float LinearViewDepth(float deviceDepth)
{
    if (!(deviceDepth > 0.0f) || !isfinite(deviceDepth))
        return kFarViewDepth;

    float denominator =
        deviceDepth * Projection[2][3] - Projection[2][2];
    if (abs(denominator) <= 1e-8f || !isfinite(denominator))
        return kFarViewDepth;

    float viewZ =
        (Projection[3][2] - deviceDepth * Projection[3][3]) /
        denominator;
    return isfinite(viewZ)
        ? min(abs(viewZ), kFarViewDepth)
        : kFarViewDepth;
}

void ApplyTemporalBlend(
    uint2 ST,
    uint ldsIdx,
    float3 BoxMin,
    float3 BoxMax)
{
    if (any(ST >= BufferDim))
        return;

    float3 CurrentColor = LoadRGB(ldsIdx);

    float CompareDepth;
    int2 velocityOffset = GetClosestPixel(ldsIdx, CompareDepth);
    int2 velocityPixel = clamp(
        int2(ST) + velocityOffset,
        int2(0, 0),
        int2(BufferDim) - 1);

    // MiniEngine packs XYZ velocity. UVSR already produces the same
    // current-to-previous pixel XY and previous-minus-current device-depth Z,
    // plus alpha to distinguish valid zero motion from cleared background.
    float4 PackedVelocity = VelocityBuffer.Load(int3(velocityPixel, 0));
    float3 Velocity = PackedVelocity.xyz;
    bool ValidVelocity = PackedVelocity.a > 0.5f &&
        all(isfinite(PackedVelocity));

    CompareDepth += Velocity.z;

    // MiniEngine compares expected previous forward-linear depth against the
    // farthest of the bilinear footprint. Convert UVSR's reverse device depths
    // to view distance, preserving that exact inequality.
    float temporalDeviceDepth = MinOf(PreDepth.Gather(
        LinearSampler,
        STtoUV(float2(ST) + Velocity.xy + ViewportJitter)));
    float expectedPreviousDepth = LinearViewDepth(CompareDepth);
    float temporalDepth = LinearViewDepth(temporalDeviceDepth) + 1e-3f;

    // Fast-moving pixels cause motion blur and probably don't need TAA.
    float SpeedFactor =
        saturate(1.0 - length(Velocity.xy) * RcpSpeedLimiter);

    // Fetch temporal color. Its confidence weight is stored in alpha.
    float4 Temp = InTemporal.SampleLevel(
        LinearSampler,
        STtoUV(float2(ST) + Velocity.xy),
        0);
    float3 TemporalColor = Temp.rgb;
    float TemporalWeight = Temp.w;

    // Pixel colors are pre-multiplied by their weight to enable bilinear
    // filtering. Divide by weight to recover color.
    TemporalColor /= max(TemporalWeight, 1e-6);

    // Clip the temporal color to the current neighborhood's bounding box.
    // Increase the box for stationary pixels to avoid rejecting noisy
    // specular highlights.
    TemporalColor = ClipColor(
        TemporalColor,
        BoxMin,
        BoxMax,
        lerp(1.0, 4.0, SpeedFactor * SpeedFactor));

    // Update confidence based on speed, disocclusion, explicit history state,
    // and UVSR's motion-validity bit.
    TemporalWeight *= SpeedFactor *
        step(expectedPreviousDepth, temporalDepth) *
        float(HistoryValid != 0u && ValidVelocity);

    // Blend previous color with new color based on confidence. Confidence grows
    // until movement, disocclusion, or bounds breaks it.
    TemporalColor = ITM(lerp(
        TM(CurrentColor),
        TM(TemporalColor),
        TemporalWeight));

    // Update and quantize weight exactly as MiniEngine does.
    TemporalWeight = saturate(rcp(2.0 - TemporalWeight));
    TemporalWeight = f16tof32(f32tof16(TemporalWeight));

    OutTemporal[ST] = float4(TemporalColor, 1) * TemporalWeight;
    OutDepth[ST] = CurDepth[ST];
}

[numthreads(8, 8, 1)]
void main(
    uint3 DTid : SV_DispatchThreadID,
    uint GI : SV_GroupIndex,
    uint3 GTid : SV_GroupThreadID,
    uint3 Gid : SV_GroupID)
{
    const uint ldsHalfPitch = kLdsPitch / 2;

    // Prefetch a 16x8 tile of pixels (8x8 colors) including a one-pixel border.
    // MiniEngine assumes fixed native dimensions. Sampling through the same
    // clamp sampler makes its gather path safe for UVSR's arbitrary windows.
    for (uint i = GI; i < 45; i += 64)
    {
        uint X = (i % ldsHalfPitch) * 2;
        uint Y = (i / ldsHalfPitch) * 2;
        uint TopLeftIdx = X + Y * kLdsPitch;
        int2 TopLeftST =
            int2(Gid.xy * uint2(8, 8)) - 1 + int2(X / 2, Y);
        float2 UV = RcpBufferDim *
            (float2(TopLeftST) * float2(2, 1) + float2(2, 1));

        float4 Depths = CurDepth.Gather(LinearSampler, UV);
        ldsDepth[TopLeftIdx + 0] = Depths.w;
        ldsDepth[TopLeftIdx + 1] = Depths.z;
        ldsDepth[TopLeftIdx + kLdsPitch] = Depths.x;
        ldsDepth[TopLeftIdx + 1 + kLdsPitch] = Depths.y;

        float4 R4 = InColor.GatherRed(LinearSampler, UV);
        float4 G4 = InColor.GatherGreen(LinearSampler, UV);
        float4 B4 = InColor.GatherBlue(LinearSampler, UV);
        StoreRGB(TopLeftIdx, float3(R4.w, G4.w, B4.w));
        StoreRGB(
            TopLeftIdx + 1,
            float3(R4.z, G4.z, B4.z));
        StoreRGB(
            TopLeftIdx + kLdsPitch,
            float3(R4.x, G4.x, B4.x));
        StoreRGB(
            TopLeftIdx + 1 + kLdsPitch,
            float3(R4.y, G4.y, B4.y));
    }

    GroupMemoryBarrierWithGroupSync();

    uint Idx0 = GTid.x * 2 + GTid.y * kLdsPitch + kLdsPitch + 1;
    uint Idx1 = Idx0 + 1;

    float3 BoxMin;
    float3 BoxMax;
    GetBBoxForPair(Idx0, Idx1, BoxMin, BoxMax);

    uint2 ST0 = DTid.xy * uint2(2, 1);
    ApplyTemporalBlend(ST0, Idx0, BoxMin, BoxMax);

    uint2 ST1 = ST0 + uint2(1, 0);
    ApplyTemporalBlend(ST1, Idx1, BoxMin, BoxMax);
}
