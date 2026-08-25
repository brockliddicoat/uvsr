#include "msaa_raster_topology.h"

#include <cstdlib>
#include <iostream>

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "MSAA raster topology test failed: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

int main()
{
    using namespace uvsr;

    for (std::uint32_t sampleCount : { 1u, 2u, 4u, 8u })
    {
        const MsaaRasterCandidates candidates =
            GetExactMsaaRasterCandidates(sampleCount);
        if (candidates.count != 1u || !candidates.values[0] ||
            candidates.values[0].presentationSampleCount != sampleCount ||
            candidates.values[0].rasterSampleCount != sampleCount ||
            candidates.values[0].linearResolutionScale != 1u ||
            candidates.values[0].TotalSampleCount() != sampleCount)
        {
            Fail("native topology changed");
        }
    }

    const MsaaRasterCandidates sixteen = GetExactMsaaRasterCandidates(16u);
    if (sixteen.count != 2u ||
        sixteen.values[0].presentationSampleCount != 16u ||
        sixteen.values[0].rasterSampleCount != 16u ||
        sixteen.values[0].linearResolutionScale != 1u ||
        sixteen.values[0].TotalSampleCount() != 16u ||
        sixteen.values[1].presentationSampleCount != 16u ||
        sixteen.values[1].rasterSampleCount != 4u ||
        sixteen.values[1].linearResolutionScale != 2u ||
        sixteen.values[1].TotalSampleCount() != 16u)
    {
        Fail("16x did not retain two exact sixteen-sample topologies");
    }

    if (GetExactMsaaRasterCandidates(0u).count != 0u ||
        GetExactMsaaRasterCandidates(3u).count != 0u ||
        GetExactMsaaRasterCandidates(32u).count != 0u)
    {
        Fail("unsupported sample count produced a topology");
    }

    const MsaaRenderExtent scaled = ScaleMsaaRenderExtent(1920u, 1080u, 2u);
    if (!scaled || scaled.width != 3840u || scaled.height != 2160u)
        Fail("composite render extent was not scaled exactly");
    if (ScaleMsaaRenderExtent(0u, 1080u, 2u) ||
        ScaleMsaaRenderExtent(8193u, 1080u, 2u) ||
        ScaleMsaaRenderExtent(1920u, 1080u, 0u))
    {
        Fail("invalid or oversized composite render extent was accepted");
    }

    return EXIT_SUCCESS;
}
