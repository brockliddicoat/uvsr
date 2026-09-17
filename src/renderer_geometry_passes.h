#pragma once

#include <cstdint>

namespace uvsr
{
    enum class RendererGeometryOutput : std::uint8_t
    {
        Pbr,
        MaterialId
    };

    struct RendererGeometryInitializationContract
    {
        bool device = false;
        bool shaderFactory = false;
        bool fallbackTexture = false;
        bool vertexShader = false;
        bool pixelShader = false;
        bool alphaTestedPixelShader = false;
        bool materialLayout = false;
        bool viewLayout = false;
        bool inputLayout = false;
        bool viewConstantBuffer = false;
        bool materialSampler = false;
        bool viewBindingSet = false;

        [[nodiscard]] constexpr bool IsComplete() const noexcept
        {
            return device && shaderFactory && fallbackTexture &&
                vertexShader && pixelShader && alphaTestedPixelShader &&
                materialLayout && viewLayout && inputLayout &&
                viewConstantBuffer && materialSampler && viewBindingSet;
        }
    };

    struct RendererGeometryPassDescription
    {
        RendererGeometryOutput output = RendererGeometryOutput::Pbr;
        bool whiteWorld = false;
    };

}
