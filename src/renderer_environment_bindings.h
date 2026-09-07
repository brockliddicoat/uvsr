#ifndef UVSR_RENDERER_ENVIRONMENT_BINDINGS_H
#define UVSR_RENDERER_ENVIRONMENT_BINDINGS_H

#ifdef __cplusplus

#include <array>
#include <cstdint>

namespace uvsr
{
    inline constexpr std::uint32_t PbrDeferredDiffuseEnvironmentSlot = 1u;
    inline constexpr std::uint32_t PbrDeferredSpecularEnvironmentSlot = 2u;
    inline constexpr std::uint32_t PbrDeferredEnvironmentBrdfSlot = 3u;

    template<class Resource>
    struct RendererTextureBinding
    {
        std::uint32_t slot;
        Resource resource;
    };

    template<class Resource>
    struct PbrDeferredEnvironmentResources
    {
        Resource diffuseEnvironment{};
        Resource specularEnvironment{};
        Resource environmentBrdf{};
    };

    template<class Resource>
    [[nodiscard]] constexpr PbrDeferredEnvironmentResources<Resource>
        ResolvePbrDeferredEnvironmentResources(
            PbrDeferredEnvironmentResources<Resource> active,
            Resource blackCubeArray,
            Resource blackTexture)
    {
        return {
            active.diffuseEnvironment
                ? active.diffuseEnvironment
                : blackCubeArray,
            active.specularEnvironment
                ? active.specularEnvironment
                : blackCubeArray,
            active.environmentBrdf
                ? active.environmentBrdf
                : blackTexture
        };
    }

    template<class Resource>
    [[nodiscard]] constexpr std::array<
        RendererTextureBinding<Resource>, 3>
        MakePbrDeferredEnvironmentBindings(
            const PbrDeferredEnvironmentResources<Resource>& resources)
    {
        return {{
            { PbrDeferredDiffuseEnvironmentSlot,
                resources.diffuseEnvironment },
            { PbrDeferredSpecularEnvironmentSlot,
                resources.specularEnvironment },
            { PbrDeferredEnvironmentBrdfSlot,
                resources.environmentBrdf }
        }};
    }

}

#else

#define UVSR_PBR_DEFERRED_DIFFUSE_ENVIRONMENT_REGISTER t1
#define UVSR_PBR_DEFERRED_SPECULAR_ENVIRONMENT_REGISTER t2
#define UVSR_PBR_DEFERRED_ENVIRONMENT_BRDF_REGISTER t3


#endif

#endif
