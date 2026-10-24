#pragma once

#include <stdint.h>

namespace uvsr
{
    enum class RendererMaterialDomain : uint8_t
    {
        Opaque,
        AlphaTested,
        AlphaBlended,
        Transmissive,
        TransmissiveAlphaTested,
        TransmissiveAlphaBlended,
        Count
    };

    enum class RendererMaterialRasterClass : uint8_t
    {
        Opaque,
        AlphaTested,
        Rejected
    };

    [[nodiscard]] constexpr RendererMaterialRasterClass
        ClassifyRendererMaterialDomain(RendererMaterialDomain domain) noexcept
    {
        switch (domain)
        {
        case RendererMaterialDomain::AlphaTested:
            return RendererMaterialRasterClass::AlphaTested;
        case RendererMaterialDomain::Opaque:
        case RendererMaterialDomain::AlphaBlended:
        case RendererMaterialDomain::Transmissive:
        case RendererMaterialDomain::TransmissiveAlphaTested:
        case RendererMaterialDomain::TransmissiveAlphaBlended:
            return RendererMaterialRasterClass::Opaque;
        default:
            return RendererMaterialRasterClass::Rejected;
        }
    }

}
