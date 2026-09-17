#include "renderer_scene_material_mode.h"
#include "renderer_scene_encoding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
#error material mode tests require exception-disabled compilation
#endif

namespace
{
    using namespace uvsr;
    constexpr uint32_t MaterialCount = 30;
    void Require(bool value, const char* reason)
    {
        if (value) return;
        fprintf(stderr, "material mode check failed: %s\n", reason); exit(1);
    }
    void Encode(const RendererSceneView& view, MaterialConstants (&output)[MaterialCount])
    {
        const int32_t slots[]{0, 1};
        Require(view.materials.count == MaterialCount, "material fixture size");
        for (uint32_t index = 0; index < MaterialCount; ++index)
            Require(EncodeRendererSceneMaterial(view.materials.data[index].values, int32_t(view.materials.data[index].selectionId),
                {slots, 2}, 2, output[index]).Succeeded(), "material GPU encoding");
    }
}

void CheckRendererSceneMaterialModes()
{
    using namespace uvsr;
    using Slot = RendererSceneMaterialTextureSlot;
    RendererScene scene;
    RendererSceneCounts counts; counts.nodes = 1; counts.materials = MaterialCount; counts.textures = 2;
    Require(scene.Prepare(counts).Succeeded(), "material fixture preparation");
    for (uint32_t domain = 0; domain < 6; ++domain) for (uint32_t coverage = 0; coverage < 5; ++coverage)
    {
        const uint32_t index = domain * 5 + coverage;
        RendererSceneMaterial material; material.selectionId = 90000 + index;
        auto& original = material.originalValues;
        original.domain = RendererMaterialDomain(domain);
        original.baseOrDiffuseColor = {-0.25f, 0.4f, 1.25f};
        original.specularColor = {0.2f, 0.3f, 0.4f}; original.useSpecularGlossModel = (coverage & 1) != 0;
        original.emissiveColor = {-0.5f, 0.25f, 0.5f}; original.emissiveIntensity = 4;
        original.metalness = 1.5f; original.roughness = -0.3f; original.opacity = 0.35f;
        original.alphaCutoff = (coverage & 1) ? 0.f : 1.f;
        original.transmissionFactor = 0.6f; original.normalTextureScale = 0.8f;
        original.normalTextureTransformScale = {2, 3}; original.occlusionStrength = 0.4f;
        original.enableSubsurfaceScattering = original.enableHair = original.doubleSided = true;
        for (auto& texture : original.textures) texture = 0;
        original.textures[uint32_t(Slot::Opacity)] = 1;
        if (coverage == 1 || coverage == 3) original.textures[uint32_t(Slot::Opacity)] = InvalidSceneIndex;
        if (coverage == 3) original.textures[uint32_t(Slot::BaseOrDiffuse)] = InvalidSceneIndex;
        if (coverage == 2 || coverage == 4) original.enableOpacityTexture = false;
        if (coverage == 4) original.enableBaseOrDiffuseTexture = false;
        material.values = original;
        Require(scene.Write(index, material).Succeeded(), "authored material fixture");
    }
    uint8_t workspace[64]{};
    Require(scene.SealWorkspaceBytes() <= sizeof(workspace) && scene.Seal(0, {workspace, sizeof(workspace)}).Succeeded() &&
        scene.Publish(373).Succeeded(), "material fixture publication");
    Require(ApplyRendererSceneMaterialMode(scene, WhiteWorldMode::Off).Succeeded(), "initial PBR normalization");
    MaterialConstants originalGpu[MaterialCount]{}; Encode(scene.View(), originalGpu);
    const bool alphaDomains[]{false, true, true, false, true, true};
    const bool baseCoverage[]{false, true, true, false, false};
    const bool separateCoverage[]{true, false, false, false, false};
    uint32_t comparisons = 0;
    const WhiteWorldMode modes[]{WhiteWorldMode::On, WhiteWorldMode::PreserveDetail, WhiteWorldMode::PreserveLighting, WhiteWorldMode::Off};
    for (const auto mode : modes)
    {
        Require(ApplyRendererSceneMaterialMode(scene, mode).Succeeded(), "retained mode transaction");
        const auto view = scene.View();
        for (uint32_t index = 0; index < MaterialCount; ++index)
        {
            const uint32_t domain = index / 5, coverage = index % 5;
            const auto& record = view.materials.data[index]; const auto& value = record.values;
            Require(record.selectionId == 90000 + index && record.originalValues.domain == RendererMaterialDomain(domain) &&
                record.originalValues.baseOrDiffuseColor.x == -0.25f && record.originalValues.emissiveIntensity == 4 &&
                value.normalTextureScale == 0.8f && value.normalTextureTransformScale.x == 2 && value.normalTextureTransformScale.y == 3 &&
                value.occlusionStrength == 0.4f && value.doubleSided, "original identity and unaffected fields remain exact");
            if (mode == WhiteWorldMode::Off)
            {
                Require(value.domain == RendererMaterialDomain(domain) && value.baseOrDiffuseColor.x == 0 &&
                    value.baseOrDiffuseColor.y == 0.4f && value.baseOrDiffuseColor.z == 1 && value.metalness == 1 && value.roughness == 0 &&
                    value.emissiveColor.x == 0 && value.emissiveColor.y == 1 && value.emissiveColor.z == 2 && value.emissiveIntensity == 1 &&
                    value.useSpecularGlossModel == ((coverage & 1) != 0) && value.enableSubsurfaceScattering && value.enableHair,
                    "off restores authored features with retained PBR normalization");
                const float expectedSpecular = (coverage & 1) ? 0.2f : 0.2f * 0.2f;
                Require(value.specularColor.x == expectedSpecular, "off preserves specular-gloss and dielectric F0 semantics");
            }
            else
            {
                const bool alpha = alphaDomains[domain];
                Require(value.domain == (alpha ? RendererMaterialDomain::AlphaTested : RendererMaterialDomain::Opaque) &&
                    value.baseOrDiffuseColor.x == 1 && value.baseOrDiffuseColor.y == 1 && value.baseOrDiffuseColor.z == 1 &&
                    value.opacity == (alpha ? 0.35f : 1.f) && value.alphaCutoff == (alpha ? ((coverage & 1) ? 0.01f : 0.99f) : 0.5f) &&
                    value.enableBaseOrDiffuseTexture == (alpha && baseCoverage[coverage]) &&
                    value.enableOpacityTexture == (alpha && separateCoverage[coverage]), "all alpha domains retain the correct coverage source");
                Require(!value.useSpecularGlossModel && value.metalness == 0 && value.roughness == 0.72f && value.transmissionFactor == 0 &&
                    !value.enableMetalRoughOrSpecularTexture && !value.enableTransmissionTexture && !value.enableSubsurfaceScattering && !value.enableHair &&
                    value.enableNormalTexture == (mode == WhiteWorldMode::PreserveDetail) &&
                    value.enableOcclusionTexture == (mode == WhiteWorldMode::PreserveDetail) &&
                    value.enableEmissiveTexture == (mode == WhiteWorldMode::PreserveLighting) &&
                    value.emissiveColor.y == (mode == WhiteWorldMode::PreserveLighting ? 1.f : 0.f) &&
                    value.emissiveColor.z == (mode == WhiteWorldMode::PreserveLighting ? 2.f : 0.f) && value.emissiveIntensity == 1,
                    "white modes preserve only their selected detail or lighting features");
            }
            ++comparisons;
        }
        Require(!ApplyRendererSceneMaterialMode(scene, mode).changed, "repeated mode keeps revisions stable");
    }
    MaterialConstants restored[MaterialCount]{}; Encode(scene.View(), restored);
    Require(!memcmp(originalGpu, restored, sizeof(restored)), "all mode round trips restore exact GPU material bytes");
    auto edited = scene.View().materials.data[0].values; edited.roughness = 0.91f;
    Require(scene.SetMaterial({scene.View().generation, 0}, edited).changed &&
        ApplyRendererSceneMaterialMode(scene, WhiteWorldMode::On).changed, "material edit followed by mode change");
    MaterialConstants beforeFailure[MaterialCount]{}; Encode(scene.View(), beforeFailure);
    const auto before = scene.View();
    SetRendererSceneMaterialModeAllocationFailure(true);
    const auto failed = ApplyRendererSceneMaterialMode(scene, WhiteWorldMode::Off);
    SetRendererSceneMaterialModeAllocationFailure(false);
    Encode(scene.View(), restored);
    Require(failed.error == RendererSceneError::Allocation && scene.View().materialRevision == before.materialRevision &&
        scene.View().contentRevision == before.contentRevision && !memcmp(beforeFailure, restored, sizeof(restored)),
        "failed batch preserves every material and revision");
    Require(ApplyRendererSceneMaterialMode(scene, WhiteWorldMode(-1)).error == RendererSceneError::Value &&
        ApplyRendererSceneMaterialMode(scene, WhiteWorldMode(4)).error == RendererSceneError::Value &&
        ApplyRendererSceneMaterialMode(scene, WhiteWorldMode::Off).changed, "invalid modes reject and allocation retry succeeds");
    Encode(scene.View(), restored);
    Require(!memcmp(originalGpu, restored, sizeof(restored)), "mode restoration uses authored originals after an edit and failed command");
    RendererScene empty;
    Require(ApplyRendererSceneMaterialMode(empty, WhiteWorldMode::On).error == RendererSceneError::InvalidState, "unpublished scene rejection");
    RendererSceneCounts emptyCounts; emptyCounts.nodes = 1;
    Require(empty.Prepare(emptyCounts).Succeeded() && empty.Seal(0, {workspace, sizeof(workspace)}).Succeeded() && empty.Publish(374).Succeeded(),
        "empty material fixture");
    SetRendererSceneMaterialModeAllocationFailure(true);
    const auto emptyResult = ApplyRendererSceneMaterialMode(empty, WhiteWorldMode::On);
    SetRendererSceneMaterialModeAllocationFailure(false);
    Require(emptyResult.Succeeded() && !emptyResult.changed, "empty material command requires no allocation");
    printf("canonical material modes: %u domain/coverage/mode comparisons, exact GPU restoration and atomic allocation retry passed\n", comparisons);
}
