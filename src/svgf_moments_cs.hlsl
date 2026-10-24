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

[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID)
{
    if (!inside(p)) return;
    float4 center = unpackIllumination(t_Input[p]), surface = t_Surface[p];
    float history = t_Moments[p].z;
    if (history >= 4 || surface.x < 0) { u_Output[p] = packIllumination(center); return; }
    float3 normal = unpackNormal(surface.zw);
    float phiDepth = max(surface.y, 1e-8) * max(g.sampleStride.x, g.sampleStride.y) * 3;
    float sum = 0;
    float3 illumination = 0;
    float2 moments = 0;
    [loop] for (int y = -3; y <= 3; ++y)
    [loop] for (int x = -3; x <= 3; ++x)
    {
        int2 tap = int2(p) + int2(x, y);
        if (!inside(tap)) continue;
        if (t_Moments[tap].z < 1) continue;
        float4 sample = unpackIllumination(t_Input[tap]), guide = t_Surface[tap];
        float w = computeWeight(surface.x, guide.x, phiDepth * length(float2(x, y)),
            normal, unpackNormal(guide.zw), g.phiNormal,
            history > 0 ? luminance(center.rgb) : luminance(sample.rgb), luminance(sample.rgb), g.phiColor);
        sum += w;
        illumination += w * sample.rgb;
        moments += w * t_Moments[tap].xy;
    }
    sum = max(sum, 1e-6);
    moments /= sum;
    float variance = max(0, moments.y - moments.x * moments.x) * 4 / max(history, 1);
    u_Output[p] = packIllumination(float4(illumination / sum, variance));
}
