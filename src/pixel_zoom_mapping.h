#pragma once

#ifdef __cplusplus
namespace uvsr
{
    [[nodiscard]] constexpr
#endif
int ResolvePixelZoomSourceCoordinate(int sourceSize, int panelSize, int zoomFactor, int panelPixel)
{
    const int factor = zoomFactor > 0 ? zoomFactor : 1;
    const int delta = panelPixel - (panelSize - factor) / 2;
    // Signed division truncates toward zero. Negative remainders need one lower texel.
    const int offset = delta / factor - (delta % factor < 0 ? 1 : 0);
    const int maximum = sourceSize > 0 ? sourceSize - 1 : 0;
    const int coordinate = sourceSize / 2 + offset;
    return coordinate < 0 ? 0 : coordinate > maximum ? maximum : coordinate;
}
#ifdef __cplusplus
}
#endif
