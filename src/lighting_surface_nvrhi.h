#pragma once

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>

namespace uvsr
{
    inline constexpr std::size_t LightingSurfaceTextureCount = 6u;

    // Borrowed, coherent G buffer surface for lighting consumers. All
    // attributes must describe the same raster sample or resolved receiver.
    struct LightingSurfaceView
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* diffuse = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* emissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;

        [[nodiscard]] bool HasRayTracingInputs() const noexcept
        {
            return depth && material && normals;
        }

        [[nodiscard]] bool HasCompleteGBuffer() const noexcept
        {
            return HasRayTracingInputs() && diffuse && emissive &&
                materialAmbientOcclusion;
        }


        [[nodiscard]] bool HasSameRayTracingInputs(
            const LightingSurfaceView& other) const noexcept
        {
            return depth == other.depth && material == other.material &&
                normals == other.normals;
        }
    };

    [[nodiscard]] inline constexpr std::array<nvrhi::ITexture*,
        LightingSurfaceTextureCount>
    GetLightingSurfaceTextures(const LightingSurfaceView& surface) noexcept
    {
        return {
            surface.depth,
            surface.diffuse,
            surface.material,
            surface.normals,
            surface.emissive,
            surface.materialAmbientOcclusion
        };
    }
}
