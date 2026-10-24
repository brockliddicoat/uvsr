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
    if (surface.x < 0) { u_Output[p] = packIllumination(center); return; }
    float variance = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
    [unroll] for (int x = -1; x <= 1; ++x)
    {
        int2 tap = clamp(int2(p) + int2(x, y), 0, int2(g.extent) - 1);
        variance += unpackIllumination(t_Input[tap]).a * (x == 0 ? 0.5 : 0.25) * (y == 0 ? 0.5 : 0.25);
    }
    float phiIllum = g.phiColor * sqrt(max(0, 1e-10 + variance));
    float phiDepth = max(surface.y, 1e-8) * max(g.sampleStride.x, g.sampleStride.y) * g.stepSize;
    float3 normal = unpackNormal(surface.zw);
    float sum = 1;
    float4 illumination = center;
    const float kernel[3] = {1, 2.0 / 3.0, 1.0 / 6.0};
    [unroll] for (int y = -2; y <= 2; ++y)
    [unroll] for (int x = -2; x <= 2; ++x)
    {
        int2 tap = int2(p) + int2(x, y) * int(g.stepSize);
        if (!inside(tap) || (x == 0 && y == 0)) continue;
        float4 sample = unpackIllumination(t_Input[tap]), guide = t_Surface[tap];
        float w = computeWeight(surface.x, guide.x, phiDepth * length(float2(x, y)),
            normal, unpackNormal(guide.zw), g.phiNormal, luminance(center.rgb), luminance(sample.rgb), phiIllum);
        w *= kernel[abs(x)] * kernel[abs(y)];
        sum += w;
        illumination += float4(w.xxx, w * w) * sample;
    }
    u_Output[p] = packIllumination(illumination / float4(sum.xxx, sum * sum));
}
