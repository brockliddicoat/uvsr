#ifndef UVSR_RENDERER_PIXEL_READBACK_CB_H
#define UVSR_RENDERER_PIXEL_READBACK_CB_H

#ifdef __cplusplus
#include <stddef.h>
#include <stdint.h>
#define UVSR_READBACK_INT int32_t
#else
#define UVSR_READBACK_INT int
#endif

struct RendererPixelReadbackConstants
{
    UVSR_READBACK_INT pixelX;
    UVSR_READBACK_INT pixelY;
    UVSR_READBACK_INT padding0;
    UVSR_READBACK_INT padding1;
};

#ifdef __cplusplus
static_assert(sizeof(RendererPixelReadbackConstants) == 16u);
static_assert(alignof(RendererPixelReadbackConstants) == 4u);
static_assert(offsetof(RendererPixelReadbackConstants, pixelX) == 0u);
static_assert(offsetof(RendererPixelReadbackConstants, pixelY) == 4u);
static_assert(offsetof(RendererPixelReadbackConstants, padding0) == 8u);
static_assert(offsetof(RendererPixelReadbackConstants, padding1) == 12u);
#endif

#undef UVSR_READBACK_INT

#endif
