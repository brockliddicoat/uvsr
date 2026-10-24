/***************************************************************************
 # Copyright (c) 2015-23, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#include "svgf_common.hlsli"

bool validTap(int2 p, float4 center, float4 correspondence)
{
    if (!inside(p)) return false;
    float4 previous = t_PreviousSurface[p];
    if (previous.x < 0 || t_Moments[p].z < 1) return false;
    // expected depth is expressed in the previous camera, unlike a direct
    // comparison of current and previous view Z when the camera translates.
    if (abs(previous.x - correspondence.z) / (center.y + 1e-2) > 10) return false;
    return distance(unpackNormal(center.zw), unpackNormal(previous.zw)) /
        (correspondence.w + 1e-2) <= 16;
}

[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID)
{
    if (!inside(p)) return;
    float3 illumination = g.scalar != 0 ? t_Input[p].xxx : t_Input[p].rgb;
    bool sampleValid = all(isfinite(illumination)) && (g.material == 0 || t_Input[p].a > 0);
    uint2 full = surfacePixel(p);
    if (g.material != 0)
        illumination = (illumination - t_Emission[full].rgb) / max(t_Albedo[full].rgb, 0.001);
    sampleValid = sampleValid && all(isfinite(illumination));
    if (!sampleValid) illumination = 0;
    float4 surface = t_Surface[p], correspondence = t_Reprojection[p];
    float3 previousIllumination = 0;
    float3 previousMoments = 0;
    float sum = 0;
    if (g.reset == 0 && surface.x >= 0 && correspondence.z >= 0)
    {
        int2 base = int2(floor(correspondence.xy));
        float2 f = frac(correspondence.xy);
        [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x)
        {
            int2 tap = base + int2(x, y);
            if (!validTap(tap, surface, correspondence)) continue;
            float w = (x == 0 ? 1 - f.x : f.x) * (y == 0 ? 1 - f.y : f.y);
            previousIllumination += w * unpackIllumination(t_Feedback[tap]).rgb;
            previousMoments += w * t_Moments[tap].xyz;
            sum += w;
        }
        if (sum < 0.01)
        {
            previousIllumination = 0;
            previousMoments = 0;
            sum = 0;
            int2 nearest = int2(floor(correspondence.xy + 0.5));
            [unroll] for (int y = -1; y <= 1; ++y)
            [unroll] for (int x = -1; x <= 1; ++x)
            {
                int2 tap = nearest + int2(x, y);
                if (!validTap(tap, surface, correspondence)) continue;
                previousIllumination += unpackIllumination(t_Feedback[tap]).rgb;
                previousMoments += t_Moments[tap].xyz;
                sum += 1;
            }
        }
    }
    bool success = sum >= 0.01;
    previousIllumination = success ? previousIllumination / sum : 0;
    previousMoments = success ? previousMoments / sum : 0;
    // a failed path attempt is missing evidence, not a black observation.
    if (!sampleValid)
    {
        u_Output[p] = packIllumination(float4(previousIllumination, max(0, previousMoments.y - previousMoments.x * previousMoments.x)));
        u_Second[p] = float4(previousMoments, 0);
        return;
    }
    float history = min(32.0, success ? previousMoments.z + 1 : 1);
    float alpha = success ? max(g.alpha, 1 / history) : 1;
    float momentsAlpha = success ? max(g.momentsAlpha, 1 / history) : 1;
    float l = luminance(illumination);
    float2 moments = lerp(previousMoments.xy, float2(l, l * l), momentsAlpha);
    u_Output[p] = packIllumination(float4(lerp(previousIllumination, illumination, alpha), max(0, moments.y - moments.x * moments.x)));
    // history length follows the same accepted taps as radiance and moments.
    u_Second[p] = float4(moments, history, 0);
}
