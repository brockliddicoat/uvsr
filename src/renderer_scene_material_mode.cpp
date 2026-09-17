#include "renderer_scene_material_mode.h"
#include "pbr_material.h"
#include <new>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error scene material mode requires exception-disabled compilation
#endif

namespace uvsr
{
#if defined(UVSR_BUILD_TESTING)
    namespace { bool allocationFailure = false; }
    void SetRendererSceneMaterialModeAllocationFailure(bool fail) noexcept { allocationFailure = fail; }
#endif

    static RendererSceneMaterialValues ResolveRendererSceneMaterialMode(
        const RendererSceneMaterialValues& original, WhiteWorldMode mode) noexcept
    {
        auto material = original;
        if (mode != WhiteWorldMode::Off)
        {
            const bool alpha = original.domain == RendererMaterialDomain::AlphaTested ||
                original.domain == RendererMaterialDomain::AlphaBlended ||
                original.domain == RendererMaterialDomain::TransmissiveAlphaTested ||
                original.domain == RendererMaterialDomain::TransmissiveAlphaBlended;
            const bool separateOpacity = alpha && original.enableOpacityTexture &&
                original.textures[uint32_t(RendererSceneMaterialTextureSlot::Opacity)] != InvalidSceneIndex;
            const bool baseAlpha = alpha && !separateOpacity && original.enableBaseOrDiffuseTexture &&
                original.textures[uint32_t(RendererSceneMaterialTextureSlot::BaseOrDiffuse)] != InvalidSceneIndex;
            const bool detail = mode == WhiteWorldMode::PreserveDetail;
            const bool lighting = mode == WhiteWorldMode::PreserveLighting;
            // coverage stays tied to its original texture. the retained white
            // shader permutation replaces sampled RGB before shading.
            material.domain = alpha ? RendererMaterialDomain::AlphaTested : RendererMaterialDomain::Opaque;
            material.useSpecularGlossModel = false;
            material.baseOrDiffuseColor = {1, 1, 1};
            material.specularColor = {0.04f, 0.04f, 0.04f};
            material.emissiveColor = lighting ? original.emissiveColor : gpu_contract::Float3{};
            material.emissiveIntensity = lighting ? original.emissiveIntensity : 1.f;
            material.metalness = 0;
            material.roughness = 0.72f;
            material.opacity = alpha ? original.opacity : 1.f;
            material.alphaCutoff = alpha ? (original.alphaCutoff < 0.01f ? 0.01f :
                (original.alphaCutoff > 0.99f ? 0.99f : original.alphaCutoff)) : 0.5f;
            material.transmissionFactor = 0;
            material.enableBaseOrDiffuseTexture = baseAlpha;
            material.enableMetalRoughOrSpecularTexture = false;
            material.enableEmissiveTexture = lighting && original.enableEmissiveTexture;
            material.enableTransmissionTexture = false;
            material.enableOpacityTexture = separateOpacity;
            material.enableNormalTexture = detail && original.enableNormalTexture;
            material.enableOcclusionTexture = detail && original.enableOcclusionTexture;
            material.enableSubsurfaceScattering = false;
            material.enableHair = false;
        }
        PbrImportedMaterialValues normalized;
        normalized.baseColor = material.baseOrDiffuseColor;
        normalized.metalness = material.metalness;
        normalized.perceptualRoughness = material.roughness;
        const float intensity = material.emissiveIntensity < 0.f ? 0.f : material.emissiveIntensity;
        normalized.emissive = {material.emissiveColor.x * intensity, material.emissiveColor.y * intensity, material.emissiveColor.z * intensity};
        normalized.opacity = material.opacity;
        ValidatePbrImportedMaterial(normalized);
        material.baseOrDiffuseColor = normalized.baseColor;
        material.metalness = normalized.metalness;
        material.roughness = normalized.perceptualRoughness;
        material.emissiveColor = normalized.emissive;
        material.emissiveIntensity = 1;
        material.opacity = normalized.opacity;
        if (!material.useSpecularGlossModel)
        {
            const float f0 = PbrIorToF0(normalized.ior);
            material.specularColor = {f0, f0, f0};
        }
        return material;
    }

    RendererSceneResult ApplyRendererSceneMaterialMode(RendererScene& scene, WhiteWorldMode mode) noexcept
    {
        if (mode < WhiteWorldMode::Off || mode > WhiteWorldMode::PreserveLighting) return {RendererSceneError::Value};
        const auto view = scene.View();
        if (!scene.IsPublished()) return {RendererSceneError::InvalidState};
        if (view.materials.count > size_t(PTRDIFF_MAX) / sizeof(RendererSceneMaterialValues)) return {RendererSceneError::Capacity};
        RendererSceneMaterialValues* batch = nullptr;
        if (view.materials.count)
        {
#if defined(UVSR_BUILD_TESTING)
            if (!allocationFailure)
#endif
                batch = new (std::nothrow) RendererSceneMaterialValues[view.materials.count];
            if (!batch) return {RendererSceneError::Allocation};
        }
        for (size_t index = 0; index < view.materials.count; ++index)
            batch[index] = ResolveRendererSceneMaterialMode(view.materials.data[index].originalValues, mode);
        const auto result = scene.SetMaterials(view.generation, {batch, view.materials.count});
        delete[] batch;
        return result;
    }
}
