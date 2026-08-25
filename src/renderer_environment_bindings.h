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

    inline constexpr std::uint32_t
        ScreenSpaceCompositeDiffuseEnvironmentSlot = 7u;
    inline constexpr std::uint32_t
        ScreenSpaceCompositeSpecularEnvironmentSlot = 10u;
    inline constexpr std::uint32_t
        ScreenSpaceCompositeEnvironmentBrdfSlot = 11u;
    inline constexpr std::uint32_t
        ScreenSpaceCompositeSkyVisibilitySlot = 12u;

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

    template<class Resource>
    struct ScreenSpaceCompositeEnvironmentResources
    {
        Resource diffuseEnvironment{};
        Resource specularEnvironment{};
        Resource environmentBrdf{};
        Resource skyVisibility{};
    };

    template<class Resource>
    [[nodiscard]] constexpr
        ScreenSpaceCompositeEnvironmentResources<Resource>
        ResolveScreenSpaceCompositeEnvironmentResources(
            ScreenSpaceCompositeEnvironmentResources<Resource> active,
            Resource blackCubeArray,
            Resource blackTexture,
            Resource whiteTexture)
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
                : blackTexture,
            active.skyVisibility
                ? active.skyVisibility
                : whiteTexture
        };
    }

    template<class Resource>
    [[nodiscard]] constexpr std::array<
        RendererTextureBinding<Resource>, 4>
        MakeScreenSpaceCompositeEnvironmentBindings(
            const ScreenSpaceCompositeEnvironmentResources<Resource>&
                resources)
    {
        return {{
            { ScreenSpaceCompositeDiffuseEnvironmentSlot,
                resources.diffuseEnvironment },
            { ScreenSpaceCompositeSpecularEnvironmentSlot,
                resources.specularEnvironment },
            { ScreenSpaceCompositeEnvironmentBrdfSlot,
                resources.environmentBrdf },
            { ScreenSpaceCompositeSkyVisibilitySlot,
                resources.skyVisibility }
        }};
    }
}

#else

#define UVSR_PBR_DEFERRED_DIFFUSE_ENVIRONMENT_REGISTER t1
#define UVSR_PBR_DEFERRED_SPECULAR_ENVIRONMENT_REGISTER t2
#define UVSR_PBR_DEFERRED_ENVIRONMENT_BRDF_REGISTER t3

#define UVSR_SCREEN_SPACE_COMPOSITE_DIFFUSE_ENVIRONMENT_REGISTER t7
#define UVSR_SCREEN_SPACE_COMPOSITE_SPECULAR_ENVIRONMENT_REGISTER t10
#define UVSR_SCREEN_SPACE_COMPOSITE_ENVIRONMENT_BRDF_REGISTER t11
#define UVSR_SCREEN_SPACE_COMPOSITE_SKY_VISIBILITY_REGISTER t12

#endif

#endif
