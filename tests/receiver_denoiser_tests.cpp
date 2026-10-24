#include "denoising_contract.h"
#include "denoising_settings.h"
#include "directional_shadow_settings.h"
#include "gpu_capabilities.h"
#include "lighting_surface.h"
#include "ray_traced_sky_visibility_result.h"
#include "ray_traced_sky_visibility_settings.h"
#include "renderer_geometry_passes.h"
#include "renderer_receiver_texture_contract.h"
#include "world_space_representation_contract.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <numeric>
#include <vector>

namespace
{
    using namespace uvsr;

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    nvrhi::TextureDesc Texture(uint32_t samples, nvrhi::Format format)
    {
        nvrhi::TextureDesc texture;
        texture.width = 640; texture.height = 360; texture.sampleCount = samples;
        texture.dimension = samples == 1 ? nvrhi::TextureDimension::Texture2D : nvrhi::TextureDimension::Texture2DMS;
        texture.format = format;
        texture.isUAV = samples == 1;
        return texture;
    }

    void Damage(nvrhi::TextureDesc& texture, unsigned defect)
    {
        switch (defect)
        {
        case 0: ++texture.arraySize; break;
        case 1: ++texture.mipLevels; break;
        case 2: texture.dimension = nvrhi::TextureDimension::TextureCube; break;
        case 3: --texture.width; break;
        case 4: texture.sampleCount = 3; break;
        case 5: ++texture.sampleQuality; break;
        case 6: texture.format = nvrhi::Format::UNKNOWN; break;
        case 7: texture.isUAV = false; break;
        }
    }

    void CheckTopology()
    {
        Require(!SupportsRequiredShaderModel(0x64) && SupportsRequiredShaderModel(0x65) &&
            SupportsRequiredShaderModel(0x66) && !SupportsRequiredFeatureLevel(0xa100) &&
            SupportsRequiredFeatureLevel(0xb000) && SupportsRequiredFeatureLevel(0xc200) &&
            !SupportsBindlessResourceTables(1) && SupportsBindlessResourceTables(2) &&
            !SupportsOptionalRayQueryRendering(1, 11) && !SupportsOptionalRayQueryRendering(2, 10) &&
            SupportsOptionalRayQueryRendering(2, 11), "required GPU capability boundary changed");
    }



    void CheckReceivers()
    {
        const auto expected = Texture(1, nvrhi::Format::D32);
        Require(AreRendererReceiverTextureDescriptorsCompatible(expected, expected, expected),
            "single-sample receiver was rejected");
        for (size_t slot = 0; slot < 3; ++slot)
        for (unsigned defect = 0; defect < 6; ++defect)
        {
            std::array receivers{ expected, expected, expected };
            Damage(receivers[slot], defect);
            Require(!AreRendererReceiverTextureDescriptorsCompatible(receivers[0], receivers[1], receivers[2]),
                "receiver mismatch was accepted");
        }
        for (uint32_t samples : { 0u, 2u, 4u, 8u, 16u })
            Require(!IsRendererReceiverTextureDescriptorSupported(Texture(samples, nvrhi::Format::D32)),
                "non-single-sample receiver was accepted");
        std::max_align_t token{};
        auto* texture = reinterpret_cast<nvrhi::ITexture*>(&token);
        LightingSurfaceView surface{ texture, texture, texture, texture, texture, texture, nullptr };
        Require(surface.HasCompleteGBuffer() && !surface.HasDenoisingInputs(),
            "motion ceased to be an explicit denoising input");
        Require(bool(RayTracedSkyVisibilityResult{ texture, nullptr, true }) &&
            !bool(RayTracedSkyVisibilityResult{ texture, nullptr, false }) &&
            !bool(RayTracedSkyVisibilityResult{ nullptr, texture, true }),
            "sky availability lost its dispatch or signal boundary");
    }

    void CheckVisibilityDomains()
    {
        DirectionalShadowSettings directional;
        Require(directional.enabled && IsDirectionalShadowSettingsValid(directional), "directional visibility default changed");
        directional.rayBias = DirectionalShadowMaximumRayBias + .001f;
        Require(!IsDirectionalShadowSettingsValid(directional), "invalid directional bias was accepted");
        RayTracedSkyVisibilitySettings sky;
        Require(IsRayTracedSkyVisibilityConfigurationSupported(sky) && HasRayTracedSkyVisibilityConsumer(sky),
            "default sky visibility is unavailable");
        for (int exponent = -1; exponent <= 7; ++exponent)
        {
            sky.sampleRateLog2 = exponent;
            const bool valid = exponent >= 0 && exponent <= 6;
            Require(IsRayTracedSkyVisibilitySampleRateSupported(exponent) == valid &&
                IsRayTracedSkyVisibilityConfigurationSupported(sky) == valid &&
                ResolveRayTracedSkyVisibilitySampleCount(exponent) == (valid ? 1u << unsigned(exponent) : 1u),
                "sky sample domain changed");
        }
        sky = {};
        sky.applyToDiffuseIbl = sky.applyToSpecularIbl = false;
        Require(!HasRayTracedSkyVisibilityConsumer(sky) && IsRayTracedSkyVisibilityConfigurationSupported(sky),
            "idle sky visibility became invalid");
        for (float bias : { -.001f, RayTracedSkyVisibilityMaximumRayBias + .001f,
                std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
        {
            sky.rayBias = bias;
            Require(!IsRayTracedSkyVisibilityConfigurationSupported(sky), "invalid sky bias was accepted");
        }
        for (float bias : { 0.f, RayTracedSkyVisibilityMaximumRayBias })
        {
            sky.rayBias = bias;
            Require(IsRayTracedSkyVisibilityConfigurationSupported(sky), "sky bias endpoint was rejected");
        }
        sky = {};
        sky.maxDistance = RayVisibilityMaxDistance::Count;
        Require(!IsRayTracedSkyVisibilityConfigurationSupported(sky) &&
            ResolveRayVisibilityMaxDistance(RayVisibilityMaxDistance::Maximum, 7) == 14 &&
            ResolveRayVisibilityMaxDistance(RayVisibilityMaxDistance::Meters2, 1000) == 2, "sky distance domain changed");
        sky = {};
        sky.noise.custom.pattern = NoisePattern::Count;
        Require(!IsRayTracedSkyVisibilityConfigurationSupported(sky), "invalid sky noise pattern was accepted");
        sky = {};
        sky.noise.custom.resolution = static_cast<NoiseResolution>(1024);
        Require(!IsRayTracedSkyVisibilityConfigurationSupported(sky), "invalid sky noise resolution was accepted");

        using Domain = RendererMaterialDomain;
        for (Domain domain : { Domain::Opaque, Domain::AlphaTested, Domain::AlphaBlended, Domain::Transmissive })
        {
            Require(IsRayVisibilityMaterialDomainSupported(domain, Domain::Opaque, Domain::AlphaTested) ==
                    (domain == Domain::Opaque || domain == Domain::AlphaTested) &&
                IsRayVisibilityMaterialDomainOpaque(domain, Domain::Opaque) == (domain == Domain::Opaque),
                "ray material-domain acceptance changed");
        }
        uint32_t offset = 0;
        Require(TryResolveRayVisibilityGeometryMapOffset(0, offset) && offset == 0 &&
            TryResolveRayVisibilityGeometryMapOffset(MaximumRayVisibilityGeometryMapOffset, offset) &&
            offset == MaximumRayVisibilityGeometryMapOffset &&
            !TryResolveRayVisibilityGeometryMapOffset(uint64_t(MaximumRayVisibilityGeometryMapOffset) + 1, offset),
            "ray geometry identity overflow was accepted");
    }

    void CheckDenoisers()
    {
        using Method = DenoisingMethodChoice;
        for (unsigned effect = 0; effect < 5; ++effect)
        for (unsigned method = 0; method < 4; ++method)
            Require(SupportsDenoisingMethod(static_cast<DenoisingEffect>(effect), static_cast<Method>(method)) ==
                (method < 2), "denoiser method domain changed");
        Require(IsDenoisingMethodRuntimeAvailable(Method::Raw, false), "Raw became unavailable");
        Require(IsDenoisingMethodRuntimeAvailable(Method::Svgf, true) &&
            !IsDenoisingMethodRuntimeAvailable(Method::Svgf, false), "denoiser availability is incorrect");
        Require(!IsDenoisingMethodRuntimeAvailable(static_cast<Method>(255), true), "corrupt method was accepted");
        DenoisingSettings settings;
        settings.global.method = Method::Svgf;
        settings.ambientOcclusion.method = Method::Raw;
        Require(&ResolveDenoisingSettings(settings, settings.ambientOcclusion, false) == &settings.global &&
            &ResolveDenoisingSettings(settings, settings.ambientOcclusion, true) == &settings.ambientOcclusion &&
            settings.diffuseGi.method == Method::Svgf, "global inheritance changed a dormant effect override");
        DenoisingSignalSettings invalid;
        invalid.alpha = std::numeric_limits<float>::quiet_NaN();
        invalid.iterations = 200;
        invalid.method = static_cast<Method>(255);
        invalid = SanitizeDenoisingSettings(DenoisingEffect::DiffuseGi, invalid);
        Require(invalid.alpha == 0.05f && invalid.iterations == 7u &&
            invalid.method == Method::Svgf, "invalid denoising values escaped sanitization");


    }

}

void CheckReceiversAndDenoisers()
{
    CheckTopology();
    CheckReceivers();
    CheckVisibilityDomains();
    CheckDenoisers();
}
