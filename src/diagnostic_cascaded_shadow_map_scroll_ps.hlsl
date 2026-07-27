#include "diagnostic_cascaded_shadow_map_cb.h"

cbuffer c_DiagnosticCsmScroll : register(b0)
{
    DiagnosticCsmScrollConstants g_Scroll;
};

Texture2DArray<float> t_SourceDepth : register(t0);

float main(float4 position : SV_Position) : SV_Depth
{
    const int2 destinationPixel = int2(position.xy);
    const int2 sourcePixel = destinationPixel + g_Scroll.sourceOffset;
    return t_SourceDepth.Load(int4(
        sourcePixel,
        int(g_Scroll.sourceArraySlice),
        0));
}
