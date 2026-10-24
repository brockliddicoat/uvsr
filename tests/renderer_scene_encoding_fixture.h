#pragma once

#include "renderer_scene_light.h"
#include "renderer_gpu_contract.h"

// fixed Donut results and setup values. see renderer_scene_encoding_fixture.md.
void ReadMaterialEncodingReference(uint32_t domain, uint32_t mask, uint32_t extra, MaterialConstants& expected) noexcept;
void ReadLightSetupReference(uint32_t seed, uvsr::RendererSceneTransform& root, uvsr::RendererSceneTransform& parent) noexcept;
void ReadLightFrameReference(uint32_t index, uvsr::RendererSceneLightFrame& frame, LightConstants& expected) noexcept;
uvsr::RendererSceneTransform ReadLightTransformReference() noexcept;
bool ReadLightCutoffReference() noexcept;
void ReadPointLightReference(LightConstants& expected) noexcept;
void FinishSceneEncodingReference() noexcept;
