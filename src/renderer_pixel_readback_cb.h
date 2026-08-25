#ifndef UVSR_RENDERER_PIXEL_READBACK_CB_H
#define UVSR_RENDERER_PIXEL_READBACK_CB_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>
#define UVSR_READBACK_INT std::int32_t
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
namespace uvsr
{
    struct RendererReadbackUint4
    {
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t z;
        std::uint32_t w;
    };

    template<class Map, class Unmap>
    [[nodiscard]] std::optional<RendererReadbackUint4>
        ReadRendererUint4(Map&& map, Unmap&& unmap)
    {
        const void* data = std::forward<Map>(map)();
        if (!data)
            return std::nullopt;
        RendererReadbackUint4 values{};
        std::memcpy(&values, data, sizeof(values));
        std::forward<Unmap>(unmap)();
        return values;
    }
}

static_assert(sizeof(RendererPixelReadbackConstants) == 16u);
static_assert(alignof(RendererPixelReadbackConstants) == 4u);
static_assert(offsetof(RendererPixelReadbackConstants, pixelX) == 0u);
static_assert(offsetof(RendererPixelReadbackConstants, pixelY) == 4u);
static_assert(offsetof(RendererPixelReadbackConstants, padding0) == 8u);
static_assert(offsetof(RendererPixelReadbackConstants, padding1) == 12u);
static_assert(std::is_trivial_v<RendererPixelReadbackConstants>);
static_assert(std::is_standard_layout_v<RendererPixelReadbackConstants>);
static_assert(std::is_trivially_copyable_v<RendererPixelReadbackConstants>);
static_assert(sizeof(uvsr::RendererReadbackUint4) == 16u);
static_assert(alignof(uvsr::RendererReadbackUint4) == 4u);
static_assert(offsetof(uvsr::RendererReadbackUint4, x) == 0u);
static_assert(offsetof(uvsr::RendererReadbackUint4, y) == 4u);
static_assert(offsetof(uvsr::RendererReadbackUint4, z) == 8u);
static_assert(offsetof(uvsr::RendererReadbackUint4, w) == 12u);
static_assert(std::is_trivial_v<uvsr::RendererReadbackUint4>);
static_assert(std::is_standard_layout_v<uvsr::RendererReadbackUint4>);
static_assert(std::is_trivially_copyable_v<uvsr::RendererReadbackUint4>);
#endif

#undef UVSR_READBACK_INT

#endif
