#include "bend_screen_space_shadows_cb.h"

#define WAVE_SIZE 64
#include "../third_party/bend_sss/upstream/bend_sss_gpu.h"

cbuffer c_BendScreenSpaceShadows : register(b0)
{
    BendScreenSpaceShadowConstants g_Bend;
};

Texture2D<float> t_Depth : register(t0);
RWTexture2D<float> u_Visibility : register(u0);
SamplerState s_PointBorder : register(s0);

[numthreads(WAVE_SIZE, 1, 1)]
void main(int3 groupID : SV_GroupID, int3 groupThreadID : SV_GroupThreadID)
{
    DispatchParameters parameters;
    parameters.SetDefaults();

    parameters.SurfaceThickness = g_Bend.surfaceThickness;
    parameters.BilinearThreshold = g_Bend.bilinearThreshold;
    parameters.ShadowContrast = g_Bend.shadowContrast;
    parameters.IgnoreEdgePixels = g_Bend.ignoreEdgePixels != 0u;
    parameters.UsePrecisionOffset = g_Bend.usePrecisionOffset != 0u;
    parameters.BilinearSamplingOffsetMode =
        g_Bend.bilinearSamplingOffsetMode != 0u;
    parameters.DebugOutputEdgeMask = g_Bend.debugOutputEdgeMask != 0u;
    parameters.DebugOutputThreadIndex = g_Bend.debugOutputThreadIndex != 0u;
    parameters.DebugOutputWaveIndex = g_Bend.debugOutputWaveIndex != 0u;
    parameters.DepthBounds = g_Bend.depthBounds;
    parameters.UseEarlyOut = g_Bend.useEarlyOut != 0u;

    parameters.LightCoordinate = g_Bend.lightCoordinate;
    parameters.WaveOffset = g_Bend.waveOffset;
    parameters.FarDepthValue = g_Bend.farDepthValue;
    parameters.NearDepthValue = g_Bend.nearDepthValue;
    parameters.InvDepthTextureSize = g_Bend.invDepthTextureSize;
    parameters.DepthTexture = t_Depth;
    parameters.OutputTexture = u_Visibility;
    parameters.PointBorderSampler = s_PointBorder;

    WriteScreenSpaceShadow(parameters, groupID, groupThreadID.x);
}
