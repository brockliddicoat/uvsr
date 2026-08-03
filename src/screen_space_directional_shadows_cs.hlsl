#include "screen_space_directional_shadows_cb.h"

#define WAVE_SIZE 64
#include "../third_party/bend_sss/upstream/bend_sss_gpu.h"

cbuffer Constants : register(b0)
{
    ScreenSpaceDirectionalShadowConstants g_Shadow;
};

Texture2D<float> t_Depth : register(t0);
RWTexture2D<float> u_Visibility : register(u0);
SamplerState s_PointBorder : register(s0);

[numthreads(WAVE_SIZE, 1, 1)]
void main(int3 groupID : SV_GroupID, int3 groupThreadID : SV_GroupThreadID)
{
    DispatchParameters parameters;
    parameters.SetDefaults();

    parameters.SurfaceThickness = g_Shadow.surfaceThickness;
    parameters.BilinearThreshold = g_Shadow.bilinearThreshold;
    parameters.ShadowContrast = g_Shadow.shadowContrast;
    parameters.IgnoreEdgePixels = g_Shadow.ignoreEdgePixels != 0u;
    parameters.UsePrecisionOffset = g_Shadow.usePrecisionOffset != 0u;
    parameters.BilinearSamplingOffsetMode =
        g_Shadow.bilinearSamplingOffsetMode != 0u;
    parameters.DebugOutputEdgeMask = false;
    parameters.DebugOutputThreadIndex =
        g_Shadow.debugOutputThreadIndex != 0u;
    parameters.DebugOutputWaveIndex =
        g_Shadow.debugOutputWaveIndex != 0u;
    parameters.DepthBounds = g_Shadow.depthBounds;
    parameters.UseEarlyOut = g_Shadow.useEarlyOut != 0u;

    parameters.LightCoordinate = g_Shadow.lightCoordinate;
    parameters.WaveOffset = g_Shadow.waveOffset;
    parameters.FarDepthValue = g_Shadow.farDepthValue;
    parameters.NearDepthValue = g_Shadow.nearDepthValue;
    parameters.InvDepthTextureSize = g_Shadow.invDepthTextureSize;
    parameters.DepthTexture = t_Depth;
    parameters.OutputTexture = u_Visibility;
    parameters.PointBorderSampler = s_PointBorder;

    WriteScreenSpaceShadow(parameters, groupID, groupThreadID.x);
}
