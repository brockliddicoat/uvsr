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
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#pragma pack_matrix(row_major)

#include "procedural_sky_cb.h"
#include "procedural_sky_shared.h"

cbuffer c_ProceduralSky : register(b0)
{
    ProceduralSkyConstants g_Sky;
};

float SmoothSkyStep(float edge0, float edge1, float value)
{
    float t = saturate((value - edge0) / max(edge1 - edge0, 1e-5));
    return t * t * (3.0 - 2.0 * t);
}

uint HashSkyCell(int2 cell, uint seed)
{
    uint2 bits = (uint2)cell;
    uint value = bits.x * 0x8da6b343u;
    value ^= bits.y * 0xd8163841u;
    value ^= seed * 0xcb1ab31fu;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float HashSky01(int2 cell, uint seed)
{
    return float(HashSkyCell(cell, seed) & UVSR_SKY_STAR_HASH_MASK) *
        UVSR_SKY_STAR_HASH_SCALE;
}

float StarValueNoise(int2 starCell, float cellScale, uint seed)
{
    float2 samplePosition = ((float2)starCell + 0.5) /
        max(cellScale, 1.0);
    int2 lattice = (int2)floor(samplePosition);
    float2 blend = frac(samplePosition);
    blend = blend * blend * (3.0 - 2.0 * blend);

    float value00 = HashSky01(lattice, seed);
    float value10 = HashSky01(lattice + int2(1, 0), seed);
    float value01 = HashSky01(lattice + int2(0, 1), seed);
    float value11 = HashSky01(lattice + int2(1, 1), seed);
    return lerp(
        lerp(value00, value10, blend.x),
        lerp(value01, value11, blend.x),
        blend.y);
}

float GetStarClusterDensityMultiplier(int2 starCell)
{
    // A broad world-anchored octave forms loose star groups. A weaker detail
    // octave roughens their edges so the clusters do not read as smooth blobs.
    float broad = StarValueNoise(
        starCell,
        UVSR_SKY_STAR_CLUSTER_CELL_SCALE,
        UVSR_SKY_STAR_SEED_CLUSTER);
    float detail = StarValueNoise(
        starCell,
        UVSR_SKY_STAR_CLUSTER_DETAIL_SCALE,
        UVSR_SKY_STAR_SEED_CLUSTER_DETAIL);
    float clusterNoise = broad * 0.72 + detail * 0.28;
    float clusterAmount = SmoothSkyStep(
        UVSR_SKY_STAR_CLUSTER_LOW,
        UVSR_SKY_STAR_CLUSTER_HIGH,
        clusterNoise);
    return lerp(
        UVSR_SKY_STAR_CLUSTER_DENSITY_MIN,
        UVSR_SKY_STAR_CLUSTER_DENSITY_MAX,
        clusterAmount);
}

float GetStarDensityThreshold(float baseThreshold, int2 starCell)
{
    float baseProbability = 1.0 - saturate(baseThreshold);
    float clusteredProbability = saturate(
        baseProbability * GetStarClusterDensityMultiplier(starCell));
    return 1.0 - clusteredProbability;
}

float GetStarRadius(int2 starCell, out float sizeRandom)
{
    sizeRandom = HashSky01(starCell, UVSR_SKY_STAR_SEED_SIZE);
    float commonProfile = sizeRandom * sizeRandom;
    float commonRadius = lerp(
        UVSR_SKY_STAR_RADIUS_MIN,
        UVSR_SKY_STAR_RADIUS_COMMON_MAX,
        commonProfile);
    float largeAmount = SmoothSkyStep(
        UVSR_SKY_STAR_LARGE_START, 1.0, sizeRandom);
    return lerp(commonRadius, UVSR_SKY_STAR_RADIUS_MAX, largeAmount);
}

float3 EvaluateThreeBandAtmosphere(
    ProceduralSkyShaderParameters params,
    float3 direction)
{
    float upDot = clamp(dot(direction, params.directionUp), -1.0, 1.0);
    float normalizedElevation = asin(max(upDot, 0.0)) *
        (2.0 / 3.14159265358979323846);
    float shapedElevation = pow(
        normalizedElevation, max(params.elevationCurve, 0.05));
    float middleElevation = clamp(
        params.middleBandElevation, 0.01, 0.99);
    float horizonToMiddle = SmoothSkyStep(
        0.0, middleElevation, shapedElevation);
    float middleToZenith = SmoothSkyStep(
        middleElevation, 1.0, shapedElevation);

    float3 atmosphere = params.horizonColor * (1.0 - horizonToMiddle) +
        params.middleColor * horizonToMiddle * (1.0 - middleToZenith) +
        params.zenithColor * middleToZenith;

    if (upDot < 0.0)
    {
        float horizonDepth = max(sin(params.horizonSize), 1e-4);
        float groundWeight = SmoothSkyStep(
            0.0, horizonDepth, -upDot);
        atmosphere = lerp(
            params.horizonColor,
            params.groundColor,
            groundWeight);
    }

    // Directional horizon color is a separate low-energy lobe. A small
    // non-directional floor avoids a seam, while keeping the former warm tint
    // from washing gray across the complete lower band.
    float horizonHaze = exp2(-abs(upDot) * 10.0);
    float3 horizonDirection = direction - params.directionUp * upDot;
    float lightUpDot = dot(params.directionToLight, params.directionUp);
    float3 horizonLight = params.directionToLight -
        params.directionUp * lightUpDot;
    float horizonDirectionLength = length(horizonDirection);
    float horizonLightLength = length(horizonLight);
    float lightFacing = 0.0;
    if (horizonDirectionLength > 1e-4 && horizonLightLength > 1e-4)
    {
        lightFacing = pow(saturate(dot(
            horizonDirection / horizonDirectionLength,
            horizonLight / horizonLightLength)), 4.0);
    }
    float accentWeight = params.horizonAccentStrength * horizonHaze *
        (0.05 + 0.95 * lightFacing);
    atmosphere = lerp(
        atmosphere,
        params.horizonAccentColor,
        saturate(accentWeight));

    return atmosphere;
}

float MoonHash(float2 value)
{
    float3 bits = frac(float3(value.x, value.y, value.x) *
        float3(0.1031, 0.1030, 0.0973));
    bits += dot(bits, bits.yzx + 33.33);
    return frac((bits.x + bits.y) * bits.z);
}

float MoonValueNoise(float2 position)
{
    float2 cell = floor(position);
    float2 fraction = frac(position);
    fraction = fraction * fraction * (3.0 - 2.0 * fraction);

    float lower = lerp(
        MoonHash(cell),
        MoonHash(cell + float2(1.0, 0.0)),
        fraction.x);
    float upper = lerp(
        MoonHash(cell + float2(0.0, 1.0)),
        MoonHash(cell + float2(1.0, 1.0)),
        fraction.x);
    return lerp(lower, upper, fraction.y);
}

float MoonEllipse(
    float2 position,
    float2 center,
    float2 radii,
    float rotation)
{
    float sineRotation;
    float cosineRotation;
    sincos(rotation, sineRotation, cosineRotation);
    float2 offset = position - center;
    float2 local = float2(
        cosineRotation * offset.x + sineRotation * offset.y,
        -sineRotation * offset.x + cosineRotation * offset.y);
    local /= max(radii, float2(1e-4, 1e-4));
    return 1.0 - SmoothSkyStep(0.72, 1.0, length(local));
}

float MoonCraterRing(
    float2 position,
    float2 center,
    float radius,
    float width)
{
    float rimDistance = abs(length(position - center) - radius);
    return 1.0 - SmoothSkyStep(width, width * 2.0, rimDistance);
}

void BuildMoonTangentFrame(
    float3 directionToMoon,
    out float3 moonRight,
    out float3 moonUp)
{
    // Frisvad's continuous orthonormal-basis construction, adapted to UVSR's
    // Y-up world. Its only singularity is the nadir, where an above-horizon
    // moon cannot be visible; keep a finite fallback for authored edge cases.
    if (directionToMoon.y < -0.999999)
    {
        moonRight = float3(1.0, 0.0, 0.0);
        moonUp = float3(0.0, 0.0, 1.0);
        return;
    }

    float inverse = rcp(1.0 + directionToMoon.y);
    float crossTerm = -directionToMoon.x * directionToMoon.z * inverse;
    moonRight = normalize(float3(
        1.0 - directionToMoon.x * directionToMoon.x * inverse,
        -directionToMoon.x,
        crossTerm));
    moonUp = normalize(cross(directionToMoon, moonRight));
}

float3 EvaluateMoonSurface(
    float2 moonPosition,
    float moonRadiusPixels)
{
    // Broad fixed maria make the disk read as the Moon rather than generic
    // noise. Fine highland variation and crater rims fade out before they can
    // shimmer when the disk becomes too small on screen.
    float maria = 0.0;
    maria = max(maria, MoonEllipse(
        moonPosition, float2(-0.27, 0.25), float2(0.30, 0.23), -0.18));
    maria = max(maria, MoonEllipse(
        moonPosition, float2(-0.43, -0.02), float2(0.22, 0.38), 0.10));
    maria = max(maria, MoonEllipse(
        moonPosition, float2(0.16, 0.27), float2(0.18, 0.14), 0.10));
    maria = max(maria, MoonEllipse(
        moonPosition, float2(0.26, 0.05), float2(0.23, 0.16), -0.22));
    maria = max(maria, MoonEllipse(
        moonPosition, float2(-0.06, -0.25), float2(0.21, 0.14), 0.28));

    float detailFade = SmoothSkyStep(4.0, 10.0, moonRadiusPixels);
    float highlands = MoonValueNoise(moonPosition * 5.5 + 17.0);
    highlands += 0.5 * MoonValueNoise(moonPosition * 11.0 + 43.0);
    highlands *= 1.0 / 1.5;
    float surface = lerp(0.82, 1.02, highlands);
    surface *= 1.0 - 0.40 * maria;

    float craterA = MoonCraterRing(
        moonPosition, float2(-0.12, -0.08), 0.105, 0.016);
    float craterB = MoonCraterRing(
        moonPosition, float2(0.34, 0.34), 0.075, 0.013);
    surface += detailFade * (0.10 * craterA + 0.08 * craterB);

    float2 tychoOffset = moonPosition - float2(0.18, -0.42);
    float tychoDistance = length(tychoOffset);
    float tychoCore = 1.0 - SmoothSkyStep(0.025, 0.085, tychoDistance);
    float tychoAngle = atan2(tychoOffset.y, tychoOffset.x);
    float tychoRayPattern = pow(saturate(
        0.5 + 0.5 * cos(tychoAngle * 8.0)), 14.0);
    float tychoRays = (1.0 - SmoothSkyStep(
        0.08, 0.72, tychoDistance)) * tychoRayPattern;
    surface += detailFade * (0.16 * tychoCore + 0.05 * tychoRays);

    surface = lerp(0.84, surface, detailFade);
    float limbNormal = sqrt(saturate(
        1.0 - dot(moonPosition, moonPosition)));
    float limbShade = lerp(0.65, 1.0, pow(limbNormal, 0.45));

    // This warm-neutral tint belongs only to the visible lunar surface. The
    // directional moonlight that illuminates scene materials remains white.
    return saturate(surface * limbShade) * float3(1.0, 0.97, 0.91);
}

float3 EvaluateCelestialLight(
    ProceduralSkyShaderParameters params,
    float3 direction,
    float angularSizeOfPixel)
{
    float angleToLight = acos(clamp(
        dot(direction, params.directionToLight), -1.0, 1.0));
    float halfAngularSize = params.angularSizeOfLight * 0.5;
    float edgePixelScale = lerp(
        2.0, 0.75, saturate(params.moonSurfaceAmount));
    float edgeWidth = max(angularSizeOfPixel * edgePixelScale, 1e-5);
    float innerRadius = max(
        halfAngularSize * (1.0 - params.celestialSoftness) - edgeWidth,
        0.0);
    float lightDisk = 1.0 - smoothstep(
        innerRadius,
        halfAngularSize + edgeWidth,
        angleToLight);
    lightDisk = lightDisk * lightDisk * (3.0 - 2.0 * lightDisk);

    // Glow Size is an absolute outer radius. The halo therefore ends at the
    // same angle for the sun and moon instead of growing with the moon disk.
    float halo = 0.0;
    if (params.glowSize > halfAngularSize)
    {
        float haloSpan = max(
            params.glowSize - halfAngularSize,
            1e-5);
        float haloBase = saturate(
            (params.glowSize - angleToLight) / haloSpan);
        halo = params.glowIntensity *
            pow(haloBase, params.glowSharpness);
    }
    float3 diskSurface = float3(1.0, 1.0, 1.0);
    [branch]
    if (params.moonSurfaceAmount > 0.0 && lightDisk > 0.0)
    {
        // Keep lunar markings world-oriented and continuous as the authored
        // direction crosses the zenith; camera rotation cannot affect them.
        float3 moonRight;
        float3 moonUp;
        BuildMoonTangentFrame(
            params.directionToLight,
            moonRight,
            moonUp);
        float projectionRadius = max(sin(halfAngularSize), 1e-5);
        float2 moonPosition = float2(
            dot(direction, moonRight),
            dot(direction, moonUp)) / projectionRadius;
        float moonRadiusPixels = halfAngularSize /
            max(angularSizeOfPixel, 1e-5);
        diskSurface = lerp(
            diskSurface,
            EvaluateMoonSurface(moonPosition, moonRadiusPixels),
            saturate(params.moonSurfaceAmount));
    }

    float3 diskRadiance = lightDisk * diskSurface * params.lightColor;
    float3 haloRadiance = halo * params.lightColor;
    return max(diskRadiance, haloRadiance);
}

float3 EvaluateStarCell(
    ProceduralSkyShaderParameters params,
    float2 starGrid,
    int2 cell,
    float antialiasWidth)
{
    float selection = HashSky01(cell, UVSR_SKY_STAR_SEED);
    float densityThreshold = GetStarDensityThreshold(
        params.starDensityThreshold, cell);
    if (selection <= densityThreshold)
        return 0.0;

    float2 center = float2(
        HashSky01(cell, UVSR_SKY_STAR_SEED_OFFSET_X),
        HashSky01(cell, UVSR_SKY_STAR_SEED_OFFSET_Y));
    center = lerp(0.25, 0.75, center);
    float sizeRandom;
    float radius = GetStarRadius(cell, sizeRandom);
    float2 localOffset = starGrid - ((float2)cell + center);
    float starDisk = 1.0 - smoothstep(
        radius - antialiasWidth,
        radius + antialiasWidth,
        length(localOffset));
    float areaCoverage = saturate(
        (radius * radius) /
        (antialiasWidth * antialiasWidth));
    starDisk *= areaCoverage;

    float selectionRange = max(
        1.0 - densityThreshold,
        1e-5);
    float brightness = saturate(
        (selection - densityThreshold) / selectionRange);
    brightness *= brightness;
    // Most stars stay tiny and restrained. Only the upper tail of the size
    // distribution becomes both visibly larger and somewhat more energetic.
    brightness *= lerp(
        0.68, 1.45, SmoothSkyStep(0.65, 1.0, sizeRandom));

    float colorRandom = HashSky01(cell, UVSR_SKY_STAR_SEED_COLOR);
    float3 colorVariation = lerp(
        float3(1.12, 0.92, 0.80),
        float3(0.76, 0.92, 1.18),
        colorRandom);

    return colorVariation * (starDisk * brightness);
}

float3 EvaluateStars(
    ProceduralSkyShaderParameters params,
    float3 direction)
{
    // Octahedral upper-hemisphere coordinates avoid the polar singularity of
    // latitude/longitude grids. Cell identity depends only on world direction,
    // so camera translation and frame progression cannot move the stars.
    float invL1 = rcp(max(
        abs(direction.x) + abs(direction.y) + abs(direction.z),
        1e-5));
    float2 starUv = direction.xz * invL1 * 0.5 + 0.5;
    float2 starGrid = starUv * params.starCellScale;

    // Derivatives are evaluated before any content-dependent branch, keeping
    // the AA footprint well-defined for quads that straddle star-cell edges.
    float pixelFootprint = max(
        length(ddx(starGrid)), length(ddy(starGrid)));
    float antialiasWidth = clamp(pixelFootprint, 0.01, 0.40);

    float upDot = dot(direction, params.directionUp);
    if (params.starIntensity <= 0.0 || upDot <= 0.0)
        return 0.0;

    int2 baseCell = (int2)floor(starGrid);
    float2 cellFraction = frac(starGrid);
    int2 nearestNeighbor = int2(
        cellFraction.x < 0.5 ? -1 : 1,
        cellFraction.y < 0.5 ? -1 : 1);

    // The filtered disk can cross a cell edge. Evaluate the containing cell
    // and its three nearest neighbors so edge stars stay symmetric instead of
    // being clipped by whichever cell owns the current pixel.
    float3 stars = EvaluateStarCell(
        params, starGrid, baseCell, antialiasWidth);
    stars += EvaluateStarCell(
        params, starGrid,
        baseCell + int2(nearestNeighbor.x, 0), antialiasWidth);
    stars += EvaluateStarCell(
        params, starGrid,
        baseCell + int2(0, nearestNeighbor.y), antialiasWidth);
    stars += EvaluateStarCell(
        params, starGrid, baseCell + nearestNeighbor, antialiasWidth);

    float horizonFade = SmoothSkyStep(0.015, 0.24, upDot);
    float angleToLight = acos(clamp(
        dot(direction, params.directionToLight), -1.0, 1.0));
    float halfAngularSize = params.angularSizeOfLight * 0.5;
    float clearanceOuterRadius = max(
        params.glowSize, halfAngularSize);
    float clearanceInnerRadius = lerp(
        halfAngularSize, clearanceOuterRadius, 0.75);
    // The star void ends with the visible halo. A fixed dot-product range used
    // to suppress stars across a roughly 23-degree radius, which could look
    // like a second oversized lunar aura even after tightening the glow.
    float moonClearance = SmoothSkyStep(
        clearanceInnerRadius,
        clearanceOuterRadius,
        angleToLight);
    const float celestialRadiance = max(
        params.lightColor.x,
        max(params.lightColor.y, params.lightColor.z));
    // A disabled celestial light must not leave an otherwise unexplained hole
    // in the stars where its disk and halo used to be.
    moonClearance = lerp(
        1.0,
        moonClearance,
        step(1e-6, celestialRadiance));
    return params.starColor * stars *
        (horizonFade * moonClearance * params.starIntensity);
}

void main(
    in float4 i_position : SV_Position,
    in float2 i_uv : UV,
    out float4 o_color : SV_Target0)
{
    float4 clipPosition;
    clipPosition.x = i_uv.x * 2.0 - 1.0;
    clipPosition.y = 1.0 - i_uv.y * 2.0;
    clipPosition.z = 0.5;
    clipPosition.w = 1.0;
    float4 translatedWorldPosition = mul(
        clipPosition, g_Sky.matClipToTranslatedWorld);
    float3 direction = normalize(
        translatedWorldPosition.xyz / translatedWorldPosition.w);
    float angularSizeOfPixel = max(
        length(ddx(direction)), length(ddy(direction)));

    float3 radiance = EvaluateThreeBandAtmosphere(g_Sky.params, direction);
    radiance += EvaluateCelestialLight(
        g_Sky.params, direction, angularSizeOfPixel);
    radiance += EvaluateStars(g_Sky.params, direction);

    o_color = float4(radiance, 0.0);
}
