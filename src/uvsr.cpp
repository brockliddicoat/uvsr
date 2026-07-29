/*
* Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#include <string>
#include <vector>
#include <array>
#include <deque>
#include <memory>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cfloat>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <utility>
#include <Windows.h>
#include <bcrypt.h>
#include <GLFW/glfw3.h>

#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/core/string_utils.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/Scene.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/View.h>
#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/ForwardShadingPass.h>
#include <donut/render/GBuffer.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/PixelReadbackPass.h>
#include <donut/app/ApplicationBase.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/app/Camera.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_renderer.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>

#include "pbr_material.h"
#include "pbr_deferred_lighting_pass.h"
#include "image_based_lighting_background_pass.h"
#include "image_based_lighting_environment.h"
#include "image_based_lighting_shared.h"
#include "bend_screen_space_shadows.h"
#include "diagnostic_cascaded_shadow_map.h"
#include "diagnostic_csm_benchmark.h"
#include "gpu_performance_monitor.h"
#include "gpu_crash_diagnostics.h"
#include "camera_collision.h"
#include "camera_controllers.h"
#include "command_line_options.h"
#include "experiment_title.h"
#include "scene_catalog.h"
#include "screen_space_visibility.h"
#include "sponza_camera_preset.h"
#include "sparse_virtual_shadow_map.h"
#include "svsm_motion_benchmark.h"
#include "taa_miniengine.h"
#include "world_material_view.h"

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

static bool g_RestartRequested = false;
static int g_RestartAdapterIndex = -1;
static GLFWkeyfun g_BenchmarkForwardKeyCallback = nullptr;

void BenchmarkWindowKeyCallback(
    GLFWwindow* window,
    int key,
    int scancode,
    int action,
    int mods)
{
    // DeviceManager normally handles Alt+Enter before application input.
    // Suppress that transition in benchmark mode so monitor-native fullscreen
    // cannot silently replace the 1920x1080 reference frame.
    if (key == GLFW_KEY_ENTER && action == GLFW_PRESS && (mods & GLFW_MOD_ALT))
        return;

    if (g_BenchmarkForwardKeyCallback)
        g_BenchmarkForwardKeyCallback(window, key, scancode, action, mods);
}

struct GpuAdapterChoice
{
    int adapterIndex = -1;
    std::string name;
    uint64_t dedicatedVideoMemory = 0;
};

constexpr float UiBackgroundBlurPixels = 4.f;

struct UiBackdropRect
{
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    float rounding = 0.f;
    bool visible = false;
};

static bool HasSvsmCasterRelevantDirtyState(
    const SceneGraphNode* root,
    std::vector<const SceneGraphNode*>& scratch)
{
    if (!root)
        return true;

    using DirtyFlags = SceneGraphNode::DirtyFlags;
    const SceneContentFlags CasterContent =
        SceneContentFlags::OpaqueMeshes |
        SceneContentFlags::AlphaTestedMeshes;
    auto hasDirtyFlag = [](
        DirtyFlags value,
        DirtyFlags flag) {
        return (value & flag) != DirtyFlags::None;
    };

    scratch.clear();
    scratch.push_back(root);
    while (!scratch.empty())
    {
        const SceneGraphNode* node = scratch.back();
        scratch.pop_back();
        if (!node)
            return true;

        const DirtyFlags dirty = node->GetDirtyFlags();
        if (dirty == DirtyFlags::None)
            continue;

        const bool containsCasterContent =
            ((node->GetLeafContentFlags() |
                node->GetSubgraphContentFlags()) &
                CasterContent) != SceneContentFlags::None;
        const SvsmCasterDirtyNodeDecision decision =
            GetSvsmCasterDirtyNodeDecision(
                hasDirtyFlag(dirty, DirtyFlags::LocalTransform),
                hasDirtyFlag(dirty, DirtyFlags::Leaf),
                hasDirtyFlag(dirty, DirtyFlags::SubgraphStructure),
                hasDirtyFlag(dirty, DirtyFlags::SubgraphTransforms),
                hasDirtyFlag(dirty, DirtyFlags::SubgraphContentUpdate),
                containsCasterContent);
        if (decision.casterStateChanged)
            return true;
        if (!decision.inspectChildren)
            continue;

        for (size_t childIndex = 0u;
            childIndex < node->GetNumChildren();
            ++childIndex)
        {
            const SceneGraphNode* child =
                node->GetChild(childIndex);
            if (!child)
                return true;
            if (child->GetDirtyFlags() != DirtyFlags::None)
                scratch.push_back(child);
        }
    }
    return false;
}

class PbrGBufferFillPass final : public GBufferFillPass
{
public:
    PbrGBufferFillPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        bool whiteWorld)
        : GBufferFillPass(device, commonPasses)
        , m_WhiteWorld(whiteWorld)
    {
    }

protected:
    nvrhi::ShaderHandle CreatePixelShader(
        ShaderFactory& shaderFactory,
        const CreateParameters& params,
        bool alphaTested) override
    {
        std::vector<ShaderMacro> macros;
        macros.emplace_back("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0");
        macros.emplace_back("ALPHA_TESTED", alphaTested ? "1" : "0");
        macros.emplace_back("WHITE_WORLD", m_WhiteWorld ? "1" : "0");
        return shaderFactory.CreateShader(
            "uvsr/pbr_gbuffer_ps.hlsl", "main", &macros, nvrhi::ShaderType::Pixel);
    }

private:
    bool m_WhiteWorld = false;
};

class PbrForwardShadingPass final : public ForwardShadingPass
{
public:
    PbrForwardShadingPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        bool whiteWorld)
        : ForwardShadingPass(device, commonPasses)
        , m_WhiteWorld(whiteWorld)
    {
    }

protected:
    nvrhi::ShaderHandle CreatePixelShader(
        ShaderFactory& shaderFactory,
        const CreateParameters&,
        bool transmissiveMaterial) override
    {
        std::vector<ShaderMacro> macros;
        macros.emplace_back("TRANSMISSIVE_MATERIAL", transmissiveMaterial ? "1" : "0");
        macros.emplace_back("WHITE_WORLD", m_WhiteWorld ? "1" : "0");
        return shaderFactory.CreateShader(
            "uvsr/pbr_forward_ps.hlsl", "main", &macros, nvrhi::ShaderType::Pixel);
    }

private:
    bool m_WhiteWorld = false;
};

static bool CopyBmpToClipboard(const std::filesystem::path& fileName)
{
    HANDLE file = CreateFileW(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    const DWORD fileSize = GetFileSize(file, nullptr);
    if (fileSize <= sizeof(BITMAPFILEHEADER))
    {
        CloseHandle(file);
        return false;
    }

    BITMAPFILEHEADER header{};
    DWORD bytesRead = 0;
    const bool validHeader = ReadFile(file, &header, sizeof(header), &bytesRead, nullptr)
        && bytesRead == sizeof(header) && header.bfType == 0x4D42;
    if (!validHeader)
    {
        CloseHandle(file);
        return false;
    }

    const SIZE_T dibSize = fileSize - sizeof(BITMAPFILEHEADER);
    HGLOBAL dibMemory = GlobalAlloc(GMEM_MOVEABLE, dibSize);
    void* dibData = dibMemory ? GlobalLock(dibMemory) : nullptr;
    const bool readSucceeded = dibData
        && ReadFile(file, dibData, DWORD(dibSize), &bytesRead, nullptr)
        && bytesRead == dibSize;
    if (dibData)
        GlobalUnlock(dibMemory);
    CloseHandle(file);

    if (!readSucceeded)
    {
        if (dibMemory)
            GlobalFree(dibMemory);
        return false;
    }

    bool clipboardOpened = false;
    for (int attempt = 0; attempt < 5 && !clipboardOpened; ++attempt)
    {
        clipboardOpened = OpenClipboard(nullptr) != FALSE;
        if (!clipboardOpened)
            Sleep(10);
    }

    if (!clipboardOpened)
    {
        GlobalFree(dibMemory);
        return false;
    }

    EmptyClipboard();
    const bool copied = SetClipboardData(CF_DIB, dibMemory) != nullptr;
    CloseClipboard();
    if (!copied)
        GlobalFree(dibMemory);
    return copied;
}

static bool RestartCurrentProcess()
{
    std::wstring commandLine = GetCommandLineW();
    if (g_RestartAdapterIndex >= 0)
    {
        // ProcessCommandLine applies options from left to right, so appending
        // the requested adapter also replaces an older -adapter option carried
        // by a previous renderer restart without rewriting unrelated arguments.
        commandLine += L" -adapter ";
        commandLine += std::to_wstring(g_RestartAdapterIndex);
    }

    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created)
    {
        log::error("Failed to restart UVSR (Win32 error %lu)", GetLastError());
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

class RenderTargets : public GBufferRenderTargets
{
public:
    nvrhi::TextureHandle HdrColor;
    nvrhi::TextureHandle BaseLighting;
    nvrhi::TextureHandle DirectDiffuseRadiance;
    nvrhi::TextureHandle LdrColor;
    nvrhi::TextureHandle MaterialIDs;
    nvrhi::TextureHandle MaterialAmbientOcclusion;
    bool PbrEnabled = true;
    bool VisibilityResourcesEnabled = false;
    bool VisibilitySourceRadianceEnabled = false;
    bool MotionVectorsEnabled = false;

    nvrhi::HeapHandle Heap;

    std::shared_ptr<FramebufferFactory> ForwardFramebuffer;
    std::shared_ptr<FramebufferFactory> LdrFramebuffer;
    std::shared_ptr<FramebufferFactory> MaterialIDFramebuffer;
    
    void Init(
        nvrhi::IDevice* device,
        dm::uint2 size,
        dm::uint sampleCount,
        bool enableMotionVectors,
        bool useReverseProjection,
        bool enablePbr,
        bool enableVisibilityResources,
        bool enableVisibilitySourceRadiance)
    {
        GBufferRenderTargets::Init(device, size, sampleCount, enableMotionVectors, useReverseProjection);
        PbrEnabled = enablePbr;
        VisibilityResourcesEnabled = enableVisibilityResources;
        VisibilitySourceRadianceEnabled =
            enableVisibilityResources && enableVisibilitySourceRadiance;
        MotionVectorsEnabled = enableMotionVectors;

        if (enablePbr)
        {
            // Repack the existing 32-bit GBufferSpecular allocation as linear
            // material data. This changes no per-pixel bandwidth.
            nvrhi::TextureDesc materialDesc = GBufferSpecular->getDesc();
            materialDesc.format = nvrhi::Format::RGBA8_UNORM;
            materialDesc.debugName = "PbrGBufferMaterial";
            GBufferSpecular = device->createTexture(materialDesc);

            nvrhi::TextureDesc materialAmbientOcclusionDesc = materialDesc;
            materialAmbientOcclusionDesc.format = nvrhi::Format::R8_UNORM;
            materialAmbientOcclusionDesc.clearValue = nvrhi::Color(1.f);
            materialAmbientOcclusionDesc.debugName = "PbrMaterialAmbientOcclusion";
            MaterialAmbientOcclusion = device->createTexture(materialAmbientOcclusionDesc);

            if (enableMotionVectors)
            {
                nvrhi::TextureDesc motionDesc = MotionVectors->getDesc();
                motionDesc.format = nvrhi::Format::RGBA16_FLOAT;
                motionDesc.debugName = "PbrGBufferMotionVectorsWithDepth";
                MotionVectors = device->createTexture(motionDesc);
            }

            GBufferFramebuffer = std::make_shared<FramebufferFactory>(device);
            GBufferFramebuffer->RenderTargets = {
                GBufferDiffuse,
                GBufferSpecular,
                GBufferNormals,
                GBufferEmissive,
                MaterialAmbientOcclusion };
            if (enableMotionVectors)
                GBufferFramebuffer->RenderTargets.push_back(MotionVectors);
            GBufferFramebuffer->DepthTarget = Depth;
        }
        
        nvrhi::TextureDesc desc;
        desc.width = size.x;
        desc.height = size.y;
        desc.isRenderTarget = true;
        desc.useClearValue = true;
        desc.clearValue = nvrhi::Color(1.f);
        desc.sampleCount = sampleCount;
        desc.dimension = sampleCount > 1 ? nvrhi::TextureDimension::Texture2DMS : nvrhi::TextureDimension::Texture2D;
        desc.keepInitialState = true;
        desc.isVirtual = device->queryFeatureSupport(nvrhi::Feature::VirtualResources);

        desc.clearValue = nvrhi::Color(0.f);
        desc.isTypeless = false;
        desc.isUAV = sampleCount == 1;
        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.initialState = nvrhi::ResourceStates::RenderTarget;
        desc.debugName = "HdrColor";
        HdrColor = device->createTexture(desc);

        if (enableVisibilityResources)
        {
            desc.debugName = "ScreenSpaceVisibility/BaseLighting";
            BaseLighting = device->createTexture(desc);

            if (VisibilitySourceRadianceEnabled)
            {
                desc.debugName = "ScreenSpaceVisibility/DirectDiffuseRadiance";
                DirectDiffuseRadiance = device->createTexture(desc);
            }
        }

        // Picking is deliberately kept out of the every-frame G-buffer. The
        // failed NRA-RTAA v1 needed stable surface IDs every frame; now a
        // compact target plus the existing on-demand material-ID pass avoids
        // an otherwise permanent MRT write and restores the original cost.
        desc.format = nvrhi::Format::RG16_UINT;
        desc.isUAV = false;
        desc.debugName = "MaterialIDs";
        MaterialIDs = device->createTexture(desc);

        // The render targets below this point are non-MSAA
        desc.sampleCount = 1;
        desc.dimension = nvrhi::TextureDimension::Texture2D;

        desc.format = nvrhi::Format::SRGBA8_UNORM;
        desc.isUAV = false;
        desc.debugName = "LdrColor";
        LdrColor = device->createTexture(desc);

        if (desc.isVirtual)
        {
            uint64_t heapSize = 0;
            std::vector<nvrhi::ITexture*> textures = { HdrColor, MaterialIDs, LdrColor };
            if (BaseLighting)
                textures.push_back(BaseLighting);
            if (DirectDiffuseRadiance)
                textures.push_back(DirectDiffuseRadiance);

            for (auto texture : textures)
            {
                nvrhi::MemoryRequirements memReq = device->getTextureMemoryRequirements(texture);
                heapSize = nvrhi::align(heapSize, memReq.alignment);
                heapSize += memReq.size;
            }

            nvrhi::HeapDesc heapDesc;
            heapDesc.type = nvrhi::HeapType::DeviceLocal;
            heapDesc.capacity = heapSize;
            heapDesc.debugName = "RenderTargetHeap";

            Heap = device->createHeap(heapDesc);

            uint64_t offset = 0;
            for (auto texture : textures)
            {
                nvrhi::MemoryRequirements memReq = device->getTextureMemoryRequirements(texture);
                offset = nvrhi::align(offset, memReq.alignment);

                device->bindTextureMemory(texture, Heap, offset);

                offset += memReq.size;
            }
        }
        
        ForwardFramebuffer = std::make_shared<FramebufferFactory>(device);
        ForwardFramebuffer->RenderTargets = { HdrColor };
        ForwardFramebuffer->DepthTarget = Depth;

        LdrFramebuffer = std::make_shared<FramebufferFactory>(device);
        LdrFramebuffer->RenderTargets = { LdrColor };

        MaterialIDFramebuffer = std::make_shared<FramebufferFactory>(device);
        MaterialIDFramebuffer->RenderTargets = { MaterialIDs };
        MaterialIDFramebuffer->DepthTarget = Depth;
    }

    [[nodiscard]] bool IsUpdateRequired(
        uint2 size,
        uint sampleCount,
        bool enablePbr,
        bool enableVisibilityResources,
        bool enableVisibilitySourceRadiance,
        bool enableMotionVectors) const
    {
        if (any(m_Size != size) || m_SampleCount != sampleCount ||
            PbrEnabled != enablePbr ||
            VisibilityResourcesEnabled != enableVisibilityResources ||
            VisibilitySourceRadianceEnabled !=
                (enableVisibilityResources && enableVisibilitySourceRadiance) ||
            MotionVectorsEnabled != enableMotionVectors)
            return true;

        return false;
    }

    void Clear(nvrhi::ICommandList* commandList) override
    {
        GBufferRenderTargets::Clear(commandList);
        if (MaterialAmbientOcclusion)
        {
            commandList->clearTextureFloat(
                MaterialAmbientOcclusion, nvrhi::AllSubresources, nvrhi::Color(1.f));
        }

        commandList->clearTextureFloat(HdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        if (BaseLighting)
            commandList->clearTextureFloat(BaseLighting, nvrhi::AllSubresources, nvrhi::Color(0.f));
        if (DirectDiffuseRadiance)
            commandList->clearTextureFloat(
                DirectDiffuseRadiance, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(LdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
    }
};

enum class WhiteWorldMode
{
    Off,
    On,
    PreserveDetail,
    PreserveLighting
};

enum class PbrLightingDebugView : uint32_t
{
    None,
    ShadingNormal,
    GeometricNormal,
    NormalDifference,
    DiffuseEnvironment,
    CardinalEnvironment,
    PrefilteredSpecularEnvironment,
    EnvironmentBrdf,
    FinalSpecularEnvironment,
    CombinedEnvironment,
    SpecularOcclusion,
    EnvironmentMip
};

static void ApplyPbrMaterialParameters(Material& material, float ior = 1.5f)
{
    PbrMaterialParameters parameters;
    parameters.baseColor = material.baseOrDiffuseColor;
    parameters.metalness = material.metalness;
    parameters.perceptualRoughness = material.roughness;
    parameters.ior = ior;
    parameters.emissive = material.emissiveColor * std::max(material.emissiveIntensity, 0.f);
    parameters.opacity = material.opacity;
    if (material.enableSubsurfaceScattering)
    {
        parameters.featureMask |= uint8_t(PbrMaterialFeature::Translucency);
        parameters.featureMask |= uint8_t(PbrMaterialFeature::Scattering);
    }
    if (material.transmissionFactor > 0.f)
        parameters.featureMask |= uint8_t(PbrMaterialFeature::Refraction);

    ValidatePbrMaterialParameters(parameters);
    material.baseOrDiffuseColor = parameters.baseColor;
    material.metalness = parameters.metalness;
    material.roughness = parameters.perceptualRoughness;
    material.emissiveColor = parameters.emissive;
    material.emissiveIntensity = 1.f;
    material.opacity = parameters.opacity;

    // Donut does not consume specularColor in its metallic-roughness workflow,
    // so UVSR uses that existing uploaded field for the dielectric F0 scalar.
    if (!material.useSpecularGlossModel)
        material.specularColor = float3(PbrIorToF0(parameters.ior));

    material.dirty = true;
}

enum class RendererMode
{
    Deferred,
    Forward,
    ForwardTonemapperless
};

class AgxToneMappingPass
{
private:
    nvrhi::DeviceHandle m_Device;
    nvrhi::ShaderHandle m_PixelShader;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::GraphicsPipelineHandle m_Pipeline;
    nvrhi::ITexture* m_BoundSource = nullptr;
    std::shared_ptr<FramebufferFactory> m_FramebufferFactory;

public:
    AgxToneMappingPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        const std::shared_ptr<FramebufferFactory>& framebufferFactory)
        : m_Device(device)
        , m_FramebufferFactory(framebufferFactory)
    {
        m_PixelShader = shaderFactory->CreateShader(
            "uvsr/agx_tonemapping_ps.hlsl", "main", nullptr, nvrhi::ShaderType::Pixel);

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::Texture_SRV(0)
        };
        m_BindingLayout = device->createBindingLayout(layoutDesc);

        nvrhi::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
        pipelineDesc.VS = commonPasses->m_FullscreenVS;
        pipelineDesc.PS = m_PixelShader;
        pipelineDesc.bindingLayouts = { m_BindingLayout };
        pipelineDesc.renderState.rasterState.setCullNone();
        pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
        pipelineDesc.renderState.depthStencilState.stencilEnable = false;
        m_Pipeline = device->createGraphicsPipeline(
            pipelineDesc, framebufferFactory->GetFramebufferInfo());
    }

    void Render(
        nvrhi::ICommandList* commandList,
        const ICompositeView& compositeView,
        nvrhi::ITexture* sourceTexture)
    {
        if (!m_BindingSet || m_BoundSource != sourceTexture)
        {
            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture)
            };
            m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
            m_BoundSource = sourceTexture;
        }

        commandList->beginMarker("AgX Tone Mapping");
        for (uint32_t viewIndex = 0;
            viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR);
            ++viewIndex)
        {
            const IView* view = compositeView.GetChildView(ViewType::PLANAR, viewIndex);
            nvrhi::GraphicsState state;
            state.pipeline = m_Pipeline;
            state.framebuffer = m_FramebufferFactory->GetFramebuffer(*view);
            state.bindings = { m_BindingSet };
            state.viewport = view->GetViewportState();
            commandList->setGraphicsState(state);

            nvrhi::DrawArguments arguments;
            arguments.instanceCount = 1;
            arguments.vertexCount = 4;
            commandList->draw(arguments);
        }
        commandList->endMarker();
    }
};

struct UIData
{
    bool                                ShowUI = true;
    std::array<UiBackdropRect, 2>        BackdropRects;
    std::vector<GpuAdapterChoice>       GpuAdapterChoices;
    int                                 ActiveGpuAdapterIndex = -1;
    bool                                EnablePbr = true;
    RendererMode                        RenderMode = RendererMode::Deferred;
    bool                                EnableMiniEngineTaa = true;
    bool                                EnableMiniEngineTaaSharpen = true;
    float                               MiniEngineTaaSharpness =
        MiniEngineTaaDefaultSharpness;
    BendScreenSpaceShadowSettings       BendScreenSpaceShadows;
    SparseVirtualShadowMapSettings      SparseVirtualShadowMaps;
    DiagnosticCascadedShadowMapSettings DiagnosticCascadedShadowMaps;
    ScreenSpaceVisibilitySettings       ScreenSpaceVisibility;
    bool                                ShaderReloadRequested = false;
    bool                                ShowEnvironmentBackground = true;
    bool                                EnableDiffuseIbl = true;
    float                               DiffuseIblStrength = 1.f;
    bool                                EnableSpecularIbl = true;
    float                               SpecularIblStrength = 1.f;
    WhiteWorldMode                      WhiteWorld = WhiteWorldMode::Off;
    ImageBasedLightingSource            EnvironmentSource =
        ImageBasedLightingSource::Kloppenheim03Day;
    float                               EnvironmentExposureStops =
        GetImageBasedLightingSourceInfo(
            EnvironmentSource).defaultExposureStops;
    PbrLightingDebugView                LightingDebugView =
        PbrLightingDebugView::None;
    CameraMode                          Camera = CameraMode::ThirdPerson;
    std::shared_ptr<Material>           SelectedMaterial;
    std::shared_ptr<SceneGraphNode>     SelectedNode;
    bool                                ShowMaterialEditor = false;
    bool                                CopyScreenshotToClipboard = false;

    [[nodiscard]] bool UsesDeferredShading() const
    {
        return RenderMode == RendererMode::Deferred;
    }

    [[nodiscard]] bool HasMiniEngineTaaVisibilityConflict() const
    {
        // These visibility histories do not yet receive TAA's subpixel jitter delta.
        return HasActiveScreenSpaceVisibilityConsumer() &&
            (ScreenSpaceVisibility.reconstruction.temporalEnabled ||
                ScreenSpaceVisibility.sampling.adaptiveSparseSamplingEnabled);
    }

    [[nodiscard]] bool HasActiveScreenSpaceVisibilityConsumer() const
    {
        return HasActiveScreenSpaceLightingConsumer(
            ScreenSpaceVisibility.enabled,
            ScreenSpaceVisibility.HasActiveAmbientOcclusion(),
            ScreenSpaceVisibility.HasActiveIndirectDiffuse(),
            IsImageBasedLightingLobeActive(
                EnableDiffuseIbl,
                DiffuseIblStrength),
            IsImageBasedLightingLobeActive(
                EnableSpecularIbl,
                SpecularIblStrength));
    }

    [[nodiscard]] bool UsesMiniEngineTaa() const
    {
        return IsMiniEngineTaaAvailable(
            EnableMiniEngineTaa,
            EnablePbr,
            UsesDeferredShading(),
            ScreenSpaceVisibility.reconstruction.temporalEnabled,
            ScreenSpaceVisibility.sampling.adaptiveSparseSamplingEnabled);
    }

    [[nodiscard]] bool UsesTonemapper() const
    {
        return RenderMode != RendererMode::ForwardTonemapperless;
    }
};

class UvsrSceneViewer : public ApplicationBase
{
private:
    typedef ApplicationBase Super;

    std::shared_ptr<RootFileSystem>     m_RootFs;
    std::shared_ptr<NativeFileSystem>   m_NativeFs;
    std::vector<SceneCatalogEntry>      m_SceneCatalog;
    std::string                         m_CurrentSceneName;
    std::filesystem::path               m_SceneDir;
    std::shared_ptr<Scene>				m_Scene;
	std::vector<std::pair<std::shared_ptr<Material>, Material>> m_OriginalMaterials;
	std::shared_ptr<ShaderFactory>      m_ShaderFactory;
    std::shared_ptr<DirectionalLight>   m_SunLight;
    std::shared_ptr<InstancedOpaqueDrawStrategy> m_OpaqueDrawStrategy;
    std::unique_ptr<RenderTargets>      m_RenderTargets;
    std::shared_ptr<ForwardShadingPass>  m_ForwardPass;
    std::shared_ptr<GBufferFillPass>     m_GBufferPass;
    std::shared_ptr<DeferredLightingPass> m_DeferredLightingPass;
    std::unique_ptr<PbrDeferredLightingPass> m_PbrDeferredLightingPass;
    std::unique_ptr<ImageBasedLightingEnvironment>
                                        m_ImageBasedLightingEnvironment;
    std::unique_ptr<ImageBasedLightingBackgroundPass>
                                        m_ImageBasedLightingBackgroundPass;
    std::unique_ptr<AgxToneMappingPass> m_AgxToneMappingPass;
    std::unique_ptr<BendScreenSpaceShadowPass>
                                        m_BendScreenSpaceShadowPass;
    std::unique_ptr<SparseVirtualShadowMapPass>
                                        m_SparseVirtualShadowMapPass;
    std::unique_ptr<DiagnosticCascadedShadowMapPass>
                                        m_DiagnosticCascadedShadowMapPass;
    std::unique_ptr<ScreenSpaceVisibilityPass> m_ScreenSpaceVisibilityPass;
    std::unique_ptr<MiniEngineTemporalAAPass> m_MiniEngineTemporalAAPass;
    std::unique_ptr<MaterialIDPass>     m_MaterialIDPass;
    std::unique_ptr<PixelReadbackPass>  m_PixelReadbackPass;

    std::shared_ptr<IView>              m_View;
    std::shared_ptr<PlanarView>         m_PreviousView;
    
    nvrhi::CommandListHandle            m_CommandList;
    UvsrFirstPersonCamera               m_FirstPersonCamera{ true };
    UvsrThirdPersonCamera               m_ThirdPersonCamera;
    UvsrFirstPersonCamera               m_PivotCamera{ false };
    StaticViewCamera                    m_StaticCamera;
    CameraCollisionWorld                m_CameraCollisionWorld;
    BindingCache                        m_BindingCache;
    
    float                               m_CameraVerticalFov = 60.f;
    float                               m_SceneDiagonal = 100.f;
    float                               m_CameraCollisionRadius = 0.1f;
    uint2                               m_PickPosition = 0u;
    bool                                m_Pick = false;
    bool                                m_BenchmarkCameraRequested = false;
    bool                                m_BenchmarkCameraActive = false;
    bool                                m_DiagnosticCsmBenchmarkRequested =
        false;
    DiagnosticCascadedShadowMapSettings m_DiagnosticCsmBenchmarkSettings;
    const Scene*                        m_DiagnosticCsmBenchmarkScene =
        nullptr;
    const DirectionalLight*             m_DiagnosticCsmBenchmarkLight =
        nullptr;
    double3                             m_DiagnosticCsmBenchmarkLightDirection =
        0.0;
    bool                                m_DiagnosticCsmBenchmarkStateArmed =
        false;
    static constexpr uint32_t           DiagnosticCsmBenchmarkWarmupFrames =
        256u;
    static constexpr double             DiagnosticCsmBenchmarkWarmupSeconds =
        2.0;
    static constexpr double
                                        DiagnosticCsmBenchmarkFreshTelemetryAgeMilliseconds =
        100.0;
    static constexpr uint64_t
                                        DiagnosticCsmBenchmarkFreshTelemetryGenerations =
        2u;
    static constexpr uint32_t           DiagnosticCsmBenchmarkMeasurementFrames =
        1024u;
    struct DiagnosticCsmBenchmarkSample
    {
        uint64_t sourceFrame = 0u;
        uint64_t applicationFrame = 0u;
        uint32_t backBufferIndex = 0u;
        float frameIntervalMilliseconds = 0.f;
        bool issuedFrameContextAvailable = false;
        DiagnosticCsmTimings timings;
        DiagnosticCsmStats stats;
        GpuPerformanceMetrics gpuMetrics;
        GpuTimingNormalizationEstimate normalized;
    };
    struct DiagnosticCsmIssuedFrameContext
    {
        uint64_t sourceFrame = 0u;
        uint64_t applicationFrame = 0u;
        uint32_t backBufferIndex = 0u;
        float frameIntervalMilliseconds = 0.f;
        GpuPerformanceMetrics gpuMetrics;
    };
    bool                                m_DiagnosticCsmRecordRequested =
        false;
    bool                                m_DiagnosticCsmRecordStarted =
        false;
    bool                                m_DiagnosticCsmRecordFinished =
        false;
    std::string                         m_DiagnosticCsmRecordRunId;
    std::string                         m_DiagnosticCsmExecutableSha256;
    std::string                         m_DiagnosticCsmTimingConfiguration;
    std::string                         m_DiagnosticCsmTimingConfigurationId;
    std::chrono::steady_clock::time_point
                                        m_DiagnosticCsmRecordStartTime;
    bool                                m_DiagnosticCsmLastSourceFrameValid =
        false;
    uint64_t                            m_DiagnosticCsmLastSourceFrame =
        0u;
    uint32_t                            m_DiagnosticCsmRecordWarmupFrames =
        0u;
    uint64_t                            m_DiagnosticCsmWarmupInitialTelemetryGeneration =
        0u;
    uint64_t                            m_DiagnosticCsmMeasurementStartTelemetryGeneration =
        0u;
    double                              m_DiagnosticCsmWarmupElapsedMilliseconds =
        0.0;
    bool                                m_DiagnosticCsmRecordWarmupComplete =
        false;
    bool                                m_DiagnosticCsmLastSubmissionTimeValid =
        false;
    std::chrono::steady_clock::time_point
                                        m_DiagnosticCsmLastSubmissionTime;
    std::deque<DiagnosticCsmIssuedFrameContext>
                                        m_DiagnosticCsmIssuedFrameContexts;
    std::vector<DiagnosticCsmBenchmarkSample>
                                        m_DiagnosticCsmBenchmarkSamples;
    bool                                m_SvsmMotionBenchmarkAutostartPending =
        false;
    bool                                m_SvsmMotionBenchmarkWriteResultFile =
        false;
    bool                                m_DredDiagnosticsActive = false;
    bool                                m_SvsmMotionDiagnosticConfiguration =
        false;
    std::filesystem::path               m_SvsmMotionMeasurementReadyPath;
    bool                                m_SvsmMotionMeasurementGateReady =
        false;
    bool                                m_SvsmMotionMeasurementGateComplete =
        false;
    bool                                m_SvsmMotionMeasurementGateContaminated =
        false;
    bool                                m_SvsmMotionMeasurementGateWaitingForCompletion =
        false;
    bool                                m_SvsmMotionMeasurementGateStageWritten =
        false;
    uint64_t                            m_SvsmMotionMeasurementBenchmarkEndUnixMilliseconds =
        0u;
    std::string                         m_SvsmMotionMeasurementReadyContents;
    std::string                         m_SvsmMotionMeasurementGateStatus =
        "not configured";
    std::string                         m_SvsmMotionExecutableSha256;
    SparseVirtualShadowMapSettings      m_SvsmMotionAutostartTargetSettings;
    SvsmMotionAutostartStage            m_SvsmMotionAutostartStage =
        SvsmMotionAutostartStage::Baseline;
    uint32_t                            m_SvsmMotionAutostartStageFrames = 0u;
    uint32_t                            m_SvsmMotionAutostartStableFrames = 0u;
    bool                                m_SvsmMotionAutostartStageWritten =
        false;
    bool                                m_SvsmMotionBenchmarkPreviousBenchmarkCameraActive =
        false;
    SvsmMotionBenchmarkKind             m_SvsmMotionBenchmarkKind =
        SvsmMotionBenchmarkKind::Camera;
    bool                                m_SvsmMotionBenchmarkActive = false;
    bool                                m_SvsmMotionBenchmarkFramePrepared =
        false;
    bool                                m_SvsmMotionBenchmarkStarted = false;
    bool                                m_SvsmMotionBenchmarkDraining = false;
    uint64_t                            m_SvsmMotionBenchmarkFrame = 0u;
    uint64_t                            m_SvsmMotionBenchmarkPreparedFrame = 0u;
    uint64_t                            m_SvsmMotionBenchmarkCurrentTimingTag =
        0u;
    uint32_t                            m_SvsmMotionBenchmarkPreparationFrames =
        0u;
    uint32_t                            m_SvsmMotionBenchmarkDrainFrames = 0u;
    CameraMode                          m_SvsmMotionBenchmarkPreviousCamera =
        CameraMode::ThirdPerson;
    SponzaCameraLocation                m_SvsmMotionBenchmarkPreviousLocation =
        SponzaCameraLocation::Free;
    float                               m_SvsmMotionBenchmarkPreviousFov = 60.f;
    float                               m_SvsmMotionBenchmarkPreviousZoom = 10.f;
    float3                              m_SvsmMotionBenchmarkPreviousPosition =
        0.f;
    float3                              m_SvsmMotionBenchmarkPreviousDirection =
        float3(0.f, 0.f, -1.f);
    float3                              m_SvsmMotionBenchmarkPreviousUp =
        float3(0.f, 1.f, 0.f);
    float3                              m_SvsmMotionBenchmarkPreviousRight =
        float3(1.f, 0.f, 0.f);
    int                                 m_SvsmMotionBenchmarkPreviousWidth = 0;
    int                                 m_SvsmMotionBenchmarkPreviousHeight = 0;
    SparseVirtualShadowMapSettings      m_SvsmMotionBenchmarkStartSettings;
    bool                                m_SvsmMotionBenchmarkPreviousTaaEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkPreviousTaaSharpenEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkPreviousBendEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkPreviousCsmEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkPreviousScreenSpaceVisibilityEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkStartTaaEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkStartUsesTaa = false;
    bool                                m_SvsmMotionBenchmarkStartTaaSharpenEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkStartBendEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkStartCsmEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkStartScreenSpaceVisibilityEnabled =
        false;
    WhiteWorldMode                      m_SvsmMotionBenchmarkStartWhiteWorld =
        WhiteWorldMode::Off;
    bool                                m_SvsmMotionBenchmarkStartEnvironmentBackgroundEnabled =
        false;
    const Scene*                        m_SvsmMotionBenchmarkStartScene =
        nullptr;
    const DirectionalLight*             m_SvsmMotionBenchmarkStartLight =
        nullptr;
    std::shared_ptr<SceneGraphNode>      m_SvsmMotionBenchmarkStartLightNode;
    double3                             m_SvsmMotionBenchmarkStartLightDirection =
        0.0;
    double3                             m_SvsmMotionBenchmarkExpectedLightDirection =
        0.0;
    double3                             m_SvsmMotionBenchmarkSunRotationAxis =
        double3(1.0, 0.0, 0.0);
    double3                             m_SvsmMotionBenchmarkStartLightTranslation =
        0.0;
    double3                             m_SvsmMotionBenchmarkStartLightScaling =
        1.0;
    dquat                               m_SvsmMotionBenchmarkStartLightRotation =
        dquat::identity();
    double3                             m_SvsmMotionBenchmarkExpectedLightTranslation =
        0.0;
    double3                             m_SvsmMotionBenchmarkExpectedLightScaling =
        1.0;
    dquat                               m_SvsmMotionBenchmarkExpectedLightRotation =
        dquat::identity();
    SparseVirtualShadowMapPass*         m_SvsmMotionBenchmarkTimingPass =
        nullptr;
    std::vector<bool>                   m_SvsmMotionBenchmarkSeenGpuFrames;
    bool                                m_SvsmMotionBenchmarkDuplicateGpuTag =
        false;
    bool                                m_SvsmMotionBenchmarkInvalidGpuTag =
        false;
    bool                                m_SvsmMotionBenchmarkInvalidGpuTiming =
        false;
    bool                                m_SvsmMotionBenchmarkInvalidCpuTiming =
        false;
    bool                                m_SvsmMotionBenchmarkDetailedTimingObserved =
        false;
    bool                                m_SvsmMotionBenchmarkBatchedSupported =
        false;
    bool                                m_SvsmMotionBenchmarkBatchedActive =
        false;
    bool                                m_SvsmMotionBenchmarkPacketSortingActive =
        false;
    bool                                m_SvsmMotionBenchmarkLevelSkipActive =
        false;
    bool                                m_SvsmMotionBenchmarkPacketCullingActive =
        false;
    bool                                m_SvsmMotionBenchmarkHierarchyActive =
        false;
    bool                                m_SvsmMotionBenchmarkHierarchyUnavailable =
        false;
    bool                                m_SvsmMotionBenchmarkReceiverMaskActive =
        false;
    bool                                m_SvsmMotionBenchmarkReceiverMaskUnavailable =
        false;
    bool                                m_SvsmMotionBenchmarkScatterRasterActive =
        false;
    bool                                m_SvsmMotionBenchmarkPacketCullingUnavailable =
        false;
    bool                                m_SvsmMotionBenchmarkRequestedPathInactive =
        false;
    SvsmMotionBenchmarkPathObservation  m_SvsmMotionBenchmarkStaticHierarchyObservation;
    SvsmMotionBenchmarkPathObservation  m_SvsmMotionBenchmarkPairedDepthObservation;
    SvsmMotionBenchmarkPathObservation  m_SvsmMotionBenchmarkDeferredMergeObservation;
    struct SvsmMotionBenchmarkCpuTiming
    {
        uint64_t sourceTag = 0u;
        float sceneValidationMilliseconds = 0.f;
        float clipmapUpdateMilliseconds = 0.f;
        float packetCullingMilliseconds = 0.f;
        float totalMilliseconds = 0.f;
    };
    std::vector<SparseVirtualShadowMapGpuTiming>
                                        m_SvsmMotionBenchmarkGpuTimings;
    std::vector<SvsmMotionBenchmarkCpuTiming>
                                        m_SvsmMotionBenchmarkCpuTimings;
    std::vector<float>                  m_SvsmMotionBenchmarkGpuSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkMarkSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkAllocationSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkClearingSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkPacketGpuSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkRenderSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkFilterSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkSceneValidationCpuSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkClipmapUpdateCpuSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkPacketCpuSamples;
    std::vector<float>                  m_SvsmMotionBenchmarkCpuSamples;
    std::string                         m_SvsmMotionBenchmarkStatus =
        "Ready to test the current sparse virtual shadow map configuration.";
    bool                                m_SponzaCameraLocationsAvailable = false;
    uint64_t                            m_SvsmSceneStateRevision = 1u;
    uint64_t                            m_SvsmCasterStateRevision = 1u;
    bool                                m_SvsmSceneStateRevisionReliable = true;
    std::vector<const SceneGraphNode*>  m_SvsmDirtyNodeScratch;
    SponzaCameraLocation                m_SponzaCameraLocation =
        SponzaCameraLocation::SimplifiedApproximation;

    UIData&                             m_ui;

public:

    UvsrSceneViewer(
        DeviceManager* deviceManager,
        UIData& ui,
        const std::string& sceneName,
        bool benchmarkCameraRequested,
        bool diagnosticCsmBenchmarkRequested,
        bool diagnosticCsmRecordRequested,
        bool svsmMotionBenchmarkRequested,
        bool svsmSunMotionBenchmarkRequested,
        bool dredDiagnosticsActive,
        bool svsmMotionDiagnosticConfiguration,
        const std::filesystem::path& svsmMotionMeasurementReadyPath)
        : Super(deviceManager)
        , m_BindingCache(deviceManager->GetDevice())
        , m_BenchmarkCameraRequested(benchmarkCameraRequested)
        , m_DiagnosticCsmBenchmarkRequested(
            diagnosticCsmBenchmarkRequested)
        , m_DiagnosticCsmBenchmarkSettings(
            ui.DiagnosticCascadedShadowMaps)
        , m_DiagnosticCsmRecordRequested(
            diagnosticCsmRecordRequested)
        , m_SvsmMotionBenchmarkAutostartPending(
            svsmMotionBenchmarkRequested)
        , m_SvsmMotionBenchmarkWriteResultFile(
            svsmMotionBenchmarkRequested)
        , m_SvsmMotionBenchmarkKind(
            svsmSunMotionBenchmarkRequested
                ? SvsmMotionBenchmarkKind::SunSlow
                : SvsmMotionBenchmarkKind::Camera)
        , m_DredDiagnosticsActive(dredDiagnosticsActive)
        , m_SvsmMotionDiagnosticConfiguration(
            svsmMotionDiagnosticConfiguration)
        , m_SvsmMotionMeasurementReadyPath(
            svsmMotionMeasurementReadyPath)
        , m_SvsmMotionAutostartTargetSettings(
            ui.SparseVirtualShadowMaps)
        , m_ui(ui)
    {
        if (m_DiagnosticCsmRecordRequested)
        {
            const auto unixMicroseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            m_DiagnosticCsmRecordRunId =
                std::to_string(unixMicroseconds) + "-" +
                std::to_string(GetCurrentProcessId());
            m_DiagnosticCsmExecutableSha256 =
                ComputeFileSha256(GetCurrentExecutablePath());
            m_DiagnosticCsmTimingConfiguration =
                BuildDiagnosticCsmTimingConfigurationIdentity(
                    m_DiagnosticCsmBenchmarkSettings);
            m_DiagnosticCsmTimingConfigurationId =
                BuildDiagnosticCsmTimingConfigurationId(
                    m_DiagnosticCsmBenchmarkSettings);
            m_DiagnosticCsmBenchmarkSamples.reserve(
                DiagnosticCsmBenchmarkMeasurementFrames);
        }
        if (svsmMotionBenchmarkRequested)
        {
            m_SvsmMotionExecutableSha256 =
                ComputeFileSha256(GetCurrentExecutablePath());
            // Present several complete scene frames before the first SVSM page
            // pool allocation or dispatch. Pass shaders and pipelines already
            // exist, while texture/mesh uploads stay out of the page-pool cold
            // start submission.
            m_ui.SparseVirtualShadowMaps.enabled = false;
            m_SvsmMotionBenchmarkStatus =
                "Staging complete baseline frames before SVSM warmup...";
        }

        m_RootFs = std::make_shared<RootFileSystem>();

        std::filesystem::path mediaDir = app::GetDirectoryWithExecutable().parent_path() / "media";
        std::filesystem::path frameworkShaderDir = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderDir = app::GetDirectoryWithExecutable() / "shaders/uvsr" / app::GetShaderTypeName(GetDevice()->getGraphicsAPI());

        m_RootFs->mount("/media", mediaDir);
        m_RootFs->mount("/shaders/donut", frameworkShaderDir);
        m_RootFs->mount("/shaders/uvsr", appShaderDir);

        m_NativeFs = std::make_shared<NativeFileSystem>();

        m_SceneDir = mediaDir / "glTF-Sample-Assets/Models/";
        m_SceneCatalog = BuildSceneCatalog(
            *m_NativeFs,
            m_SceneDir,
            FindScenes(*m_NativeFs, m_SceneDir));

        if (sceneName.empty() && m_SceneCatalog.empty())
        {
            log::fatal("No scene descriptor or model found in media folder '%s'\n"
                "Please make sure that folder contains valid scene files.",
                m_SceneDir.generic_string().c_str());
        }
        
        m_TextureCache = std::make_shared<TextureCache>(GetDevice(), m_NativeFs, nullptr);

        m_ShaderFactory = std::make_shared<ShaderFactory>(GetDevice(), m_RootFs, "/shaders");
        m_CommonPasses = std::make_shared<CommonRenderPasses>(GetDevice(), m_ShaderFactory);
        m_ImageBasedLightingEnvironment =
            std::make_unique<ImageBasedLightingEnvironment>(
                GetDevice(),
                m_ShaderFactory,
                m_CommonPasses,
                mediaDir / "environments");

        m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>();


        m_CommandList = GetDevice()->createCommandList();

        SetAsynchronousLoadingEnabled(true);

        if (sceneName.empty())
        {
            // Use an exact catalog path rather than a substring preference:
            // the standardized Decorated and Plain Sponza descriptors share
            // their architecture components, while the complete scene is
            // UVSR's stable default.
            const std::string defaultScene = (m_SceneDir
                / "intel_sponza/intel_pbr_sponza.scene.json").lexically_normal().generic_string();
            if (const SceneCatalogEntry* entry = FindSceneCatalogEntry(m_SceneCatalog, defaultScene))
                SetCurrentSceneName(entry->FileName);
            else
            {
                log::warning(
                    "Default PBR Sponza Decorated descriptor '%s' was not found; loading '%s' instead.",
                    defaultScene.c_str(),
                    m_SceneCatalog.front().FileName.c_str());
                SetCurrentSceneName(m_SceneCatalog.front().FileName);
            }
        }
        else
            SetCurrentSceneName(sceneName);

    }

    ~UvsrSceneViewer() override
    {
        if (m_DiagnosticCsmRecordRequested &&
            m_DiagnosticCsmRecordStarted &&
            !m_DiagnosticCsmRecordFinished)
        {
            WriteDiagnosticCsmBenchmarkFailure(
                "application closed before benchmark recording completed");
        }
    }

	std::shared_ptr<vfs::IFileSystem> GetRootFs() const
    {
		return m_RootFs;
	}

    BaseCamera& GetActiveCamera() const
    {
        switch (m_ui.Camera)
        {
        case CameraMode::FirstPerson: return (BaseCamera&)m_FirstPersonCamera;
        case CameraMode::ThirdPerson: return (BaseCamera&)m_ThirdPersonCamera;
        case CameraMode::Static: return (BaseCamera&)m_StaticCamera;
        case CameraMode::Pivot: return (BaseCamera&)m_PivotCamera;
        default: return (BaseCamera&)m_FirstPersonCamera;
        }
    }

    void SetCameraMode(CameraMode mode)
    {
        if (mode != CameraMode::ThirdPerson && mode != CameraMode::Static)
            return;

        if (m_BenchmarkCameraActive && mode != CameraMode::Static)
            return;

        if (mode == m_ui.Camera)
            return;

        const BaseCamera& source = GetActiveCamera();
        const float3 position = source.GetPosition();
        const float3 direction = source.GetDir();
        const float3 up = source.GetUp();

        switch (mode)
        {
        case CameraMode::FirstPerson:
            m_FirstPersonCamera.LookTo(position, direction, up);
            break;

        case CameraMode::ThirdPerson:
            m_ThirdPersonCamera.LookTo(position, direction, up);
            m_ThirdPersonCamera.CancelPendingMotion();
            break;

        case CameraMode::Static:
            m_StaticCamera.LookTo(position, direction, up);
            break;

        case CameraMode::Pivot:
            m_PivotCamera.LookTo(position, direction, up);
            break;
        }

        m_ui.Camera = mode;
    }

    [[nodiscard]] bool IsBenchmarkCameraActive() const
    {
        return m_BenchmarkCameraActive;
    }

    [[nodiscard]] bool HasSponzaCameraLocations() const
    {
        return m_SponzaCameraLocationsAvailable;
    }

    [[nodiscard]] static const char* GetSvsmMotionBenchmarkKindName(
        SvsmMotionBenchmarkKind kind)
    {
        return kind == SvsmMotionBenchmarkKind::SunSlow
            ? "sunSlow"
            : "camera";
    }

    [[nodiscard]] static const char* GetSvsmMotionBenchmarkPhaseName(
        SvsmMotionBenchmarkPhase phase)
    {
        switch (phase)
        {
        case SvsmMotionBenchmarkPhase::Warm: return "warm";
        case SvsmMotionBenchmarkPhase::Baseline: return "baseline";
        case SvsmMotionBenchmarkPhase::Forward: return "forward";
        case SvsmMotionBenchmarkPhase::Recovery: return "recovery";
        case SvsmMotionBenchmarkPhase::Reverse: return "reverse";
        case SvsmMotionBenchmarkPhase::FinalRecovery:
            return "finalRecovery";
        case SvsmMotionBenchmarkPhase::Complete: return "complete";
        }
        return "unknown";
    }

    [[nodiscard]] static bool IsExactSvsmMotionBenchmarkVector(
        const double3& left,
        const double3& right)
    {
        return left.x == right.x &&
            left.y == right.y &&
            left.z == right.z;
    }

    [[nodiscard]] static bool IsExactSvsmMotionBenchmarkQuaternion(
        const dquat& left,
        const dquat& right)
    {
        return left.w == right.w &&
            left.x == right.x &&
            left.y == right.y &&
            left.z == right.z;
    }

    [[nodiscard]] static bool IsValidSvsmMotionBenchmarkDirection(
        const double3& direction)
    {
        return std::isfinite(direction.x) &&
            std::isfinite(direction.y) &&
            std::isfinite(direction.z) &&
            dot(direction, direction) > 1e-20;
    }

    [[nodiscard]] bool IsSvsmMotionBenchmarkLightStateExpected() const
    {
        if (!m_SunLight ||
            m_SunLight.get() != m_SvsmMotionBenchmarkStartLight ||
            !m_SvsmMotionBenchmarkStartLightNode ||
            m_SunLight->GetNodeSharedPtr() !=
                m_SvsmMotionBenchmarkStartLightNode)
        {
            return false;
        }

        const SceneGraphNode& node =
            *m_SvsmMotionBenchmarkStartLightNode;
        if (!IsExactSvsmMotionBenchmarkVector(
                node.GetTranslation(),
                m_SvsmMotionBenchmarkExpectedLightTranslation) ||
            !IsExactSvsmMotionBenchmarkVector(
                node.GetScaling(),
                m_SvsmMotionBenchmarkExpectedLightScaling) ||
            !IsExactSvsmMotionBenchmarkQuaternion(
                node.GetRotation(),
                m_SvsmMotionBenchmarkExpectedLightRotation))
        {
            return false;
        }

        const double3 actual = m_SunLight->GetDirection();
        const double3 expected =
            m_SvsmMotionBenchmarkExpectedLightDirection;
        constexpr double DirectionTolerance = 1e-9;
        return std::abs(actual.x - expected.x) <= DirectionTolerance &&
            std::abs(actual.y - expected.y) <= DirectionTolerance &&
            std::abs(actual.z - expected.z) <= DirectionTolerance;
    }

    [[nodiscard]] bool CanStartSvsmMotionBenchmark() const
    {
        return IsSceneLoaded() &&
            m_SponzaCameraLocationsAvailable &&
            m_ui.EnablePbr &&
            m_ui.UsesDeferredShading() &&
            m_ui.WhiteWorld == WhiteWorldMode::Off &&
            m_ui.SparseVirtualShadowMaps.enabled &&
            m_SparseVirtualShadowMapPass &&
            m_SunLight &&
            m_SunLight->GetNode() &&
            IsValidSvsmMotionBenchmarkDirection(
                m_SunLight->GetDirection()) &&
            !m_SvsmMotionBenchmarkActive;
    }

    [[nodiscard]] bool CanStageSvsmMotionBenchmark() const
    {
        return IsSceneLoaded() &&
            m_SponzaCameraLocationsAvailable &&
            m_ui.EnablePbr &&
            m_ui.UsesDeferredShading() &&
            m_ui.WhiteWorld == WhiteWorldMode::Off &&
            m_SparseVirtualShadowMapPass &&
            m_SunLight &&
            m_SunLight->GetNode() &&
            IsValidSvsmMotionBenchmarkDirection(
                m_SunLight->GetDirection()) &&
            !m_SvsmMotionBenchmarkActive;
    }

    [[nodiscard]] bool IsSvsmMotionBenchmarkRunning() const
    {
        return m_SvsmMotionBenchmarkAutostartPending ||
            m_SvsmMotionBenchmarkActive;
    }

    [[nodiscard]] std::string GetSvsmMotionBenchmarkStatus() const
    {
        if (!m_SvsmMotionBenchmarkActive)
            return m_SvsmMotionBenchmarkStatus;

        if (!m_SvsmMotionBenchmarkStarted)
        {
            std::ostringstream status;
            status << m_SvsmMotionBenchmarkStatus << " ("
                << m_SvsmMotionBenchmarkPreparationFrames << " / "
                << SvsmMotionBenchmarkPreparationFrameLimit << ")";
            return status.str();
        }

        if (m_SvsmMotionBenchmarkDraining)
        {
            if (m_SvsmMotionMeasurementGateWaitingForCompletion)
            {
                return "External measurement gate: " +
                    m_SvsmMotionMeasurementGateStatus;
            }
            const uint64_t outstanding = m_SvsmMotionBenchmarkTimingPass
                ? m_SvsmMotionBenchmarkTimingPass->GetTimingAccounting()
                    .outstanding
                : 0u;
            std::ostringstream status;
            status << "Draining "
                << (m_SvsmMotionBenchmarkStartSettings.
                        detailedGpuTimingEnabled
                    ? "detailed"
                    : "total-only")
                << " GPU timings: " << outstanding
                << " outstanding, frame "
                << m_SvsmMotionBenchmarkDrainFrames << " / "
                << SvsmMotionBenchmarkDrainFrameLimit;
            return status.str();
        }

        const SvsmMotionBenchmarkPhase phase =
            GetSvsmMotionBenchmarkPhase(
                m_SvsmMotionBenchmarkKind,
                m_SvsmMotionBenchmarkFrame);

        std::ostringstream status;
        status << GetSvsmMotionBenchmarkPhaseName(phase)
            << " " << GetSvsmMotionBenchmarkKindName(
                m_SvsmMotionBenchmarkKind)
            << ": frame "
            << m_SvsmMotionBenchmarkFrame << " / "
            << GetSvsmMotionBenchmarkEndFrame(
                m_SvsmMotionBenchmarkKind) << ", "
            << std::fixed << std::setprecision(1)
            << GetSvsmMotionBenchmarkAngleDegrees(
                m_SvsmMotionBenchmarkKind,
                m_SvsmMotionBenchmarkFrame)
            << " degrees, "
            << (m_SvsmMotionBenchmarkStartSettings.
                    detailedGpuTimingEnabled
                ? "detailed"
                : "total-only")
            << " GPU timing";
        return status.str();
    }

    bool StartSvsmMotionBenchmark(
        SvsmMotionBenchmarkKind kind =
            SvsmMotionBenchmarkKind::Camera)
    {
        if (!CanStartSvsmMotionBenchmark())
            return false;

        // A manual start during a pending automated run owns the single
        // benchmark lifecycle and must prevent a second autostart afterward.
        m_SvsmMotionBenchmarkAutostartPending = false;
        m_SvsmMotionBenchmarkKind = kind;
        if (m_SvsmMotionExecutableSha256.empty())
        {
            m_SvsmMotionExecutableSha256 =
                ComputeFileSha256(GetCurrentExecutablePath());
        }

        const SponzaCameraPreset& preset =
            GetDefaultSponzaCameraPreset();
        const BaseCamera& previousCamera = GetActiveCamera();
        m_SvsmMotionBenchmarkPreviousBenchmarkCameraActive =
            m_BenchmarkCameraActive;
        m_SvsmMotionBenchmarkPreviousCamera = m_ui.Camera;
        m_SvsmMotionBenchmarkPreviousLocation =
            m_SponzaCameraLocation;
        m_SvsmMotionBenchmarkPreviousFov = m_CameraVerticalFov;
        m_SvsmMotionBenchmarkPreviousZoom =
            m_ThirdPersonCamera.GetReferenceZoomDistance();
        m_SvsmMotionBenchmarkPreviousPosition =
            previousCamera.GetPosition();
        m_SvsmMotionBenchmarkPreviousDirection =
            previousCamera.GetDir();
        m_SvsmMotionBenchmarkPreviousUp =
            previousCamera.GetUp();
        m_SvsmMotionBenchmarkPreviousRight = normalize(cross(
            m_SvsmMotionBenchmarkPreviousDirection,
            m_SvsmMotionBenchmarkPreviousUp));
        m_SvsmMotionBenchmarkPreviousTaaEnabled =
            m_ui.EnableMiniEngineTaa;
        m_SvsmMotionBenchmarkPreviousTaaSharpenEnabled =
            m_ui.EnableMiniEngineTaaSharpen;
        m_SvsmMotionBenchmarkPreviousBendEnabled =
            m_ui.BendScreenSpaceShadows.enabled;
        m_SvsmMotionBenchmarkPreviousCsmEnabled =
            m_ui.DiagnosticCascadedShadowMaps.enabled;
        m_SvsmMotionBenchmarkPreviousScreenSpaceVisibilityEnabled =
            m_ui.ScreenSpaceVisibility.enabled;

        // Keep the benchmark lane independent from other visibility
        // producers and temporal consumers. The previous values are restored
        // after either completion or an abort.
        m_ui.EnableMiniEngineTaa = false;
        m_ui.EnableMiniEngineTaaSharpen = false;
        m_ui.BendScreenSpaceShadows.enabled = false;
        m_ui.DiagnosticCascadedShadowMaps.enabled = false;
        m_ui.ScreenSpaceVisibility.enabled = false;

        m_SvsmMotionBenchmarkStartSettings =
            m_ui.SparseVirtualShadowMaps;
        m_SvsmMotionBenchmarkStartTaaEnabled =
            m_ui.EnableMiniEngineTaa;
        m_SvsmMotionBenchmarkStartUsesTaa = m_ui.UsesMiniEngineTaa();
        m_SvsmMotionBenchmarkStartTaaSharpenEnabled =
            m_ui.EnableMiniEngineTaaSharpen;
        m_SvsmMotionBenchmarkStartBendEnabled =
            m_ui.BendScreenSpaceShadows.enabled;
        m_SvsmMotionBenchmarkStartCsmEnabled =
            m_ui.DiagnosticCascadedShadowMaps.enabled;
        m_SvsmMotionBenchmarkStartScreenSpaceVisibilityEnabled =
            m_ui.ScreenSpaceVisibility.enabled;
        m_SvsmMotionBenchmarkStartWhiteWorld = m_ui.WhiteWorld;
        m_SvsmMotionBenchmarkStartEnvironmentBackgroundEnabled =
            m_ui.ShowEnvironmentBackground;
        m_SvsmMotionBenchmarkStartScene = m_Scene.get();
        m_SvsmMotionBenchmarkStartLight = m_SunLight.get();
        m_SvsmMotionBenchmarkStartLightNode =
            m_SunLight->GetNodeSharedPtr();
        if (!m_SvsmMotionBenchmarkStartLightNode)
        {
            m_ui.EnableMiniEngineTaa =
                m_SvsmMotionBenchmarkPreviousTaaEnabled;
            m_ui.EnableMiniEngineTaaSharpen =
                m_SvsmMotionBenchmarkPreviousTaaSharpenEnabled;
            m_ui.BendScreenSpaceShadows.enabled =
                m_SvsmMotionBenchmarkPreviousBendEnabled;
            m_ui.DiagnosticCascadedShadowMaps.enabled =
                m_SvsmMotionBenchmarkPreviousCsmEnabled;
            m_ui.ScreenSpaceVisibility.enabled =
                m_SvsmMotionBenchmarkPreviousScreenSpaceVisibilityEnabled;
            return false;
        }
        m_SvsmMotionBenchmarkStartLightDirection =
            normalize(m_SunLight->GetDirection());
        m_SvsmMotionBenchmarkExpectedLightDirection =
            m_SvsmMotionBenchmarkStartLightDirection;
        m_SvsmMotionBenchmarkStartLightTranslation =
            m_SvsmMotionBenchmarkStartLightNode->GetTranslation();
        m_SvsmMotionBenchmarkStartLightRotation =
            m_SvsmMotionBenchmarkStartLightNode->GetRotation();
        m_SvsmMotionBenchmarkStartLightScaling =
            m_SvsmMotionBenchmarkStartLightNode->GetScaling();
        m_SvsmMotionBenchmarkExpectedLightTranslation =
            m_SvsmMotionBenchmarkStartLightTranslation;
        m_SvsmMotionBenchmarkExpectedLightRotation =
            m_SvsmMotionBenchmarkStartLightRotation;
        m_SvsmMotionBenchmarkExpectedLightScaling =
            m_SvsmMotionBenchmarkStartLightScaling;
        const double3 worldUp(0.0, 1.0, 0.0);
        const double3 fallbackForward(0.0, 0.0, 1.0);
        const double3 axisReference =
            std::abs(dot(
                m_SvsmMotionBenchmarkStartLightDirection,
                worldUp)) < 0.999
                ? worldUp
                : fallbackForward;
        m_SvsmMotionBenchmarkSunRotationAxis = normalize(cross(
            m_SvsmMotionBenchmarkStartLightDirection,
            axisReference));
        GetDeviceManager()->GetWindowDimensions(
            m_SvsmMotionBenchmarkPreviousWidth,
            m_SvsmMotionBenchmarkPreviousHeight);
        glfwSetWindowSize(
            GetDeviceManager()->GetWindow(),
            int(preset.ReferenceWidth),
            int(preset.ReferenceHeight));

        ApplySponzaCameraPreset(preset);
        m_SponzaCameraLocation =
            SponzaCameraLocation::SimplifiedApproximation;
        m_BenchmarkCameraActive = true;
        m_ui.Camera = CameraMode::Static;
        m_SvsmMotionBenchmarkFrame = 0u;
        m_SvsmMotionBenchmarkPreparedFrame = 0u;
        m_SvsmMotionBenchmarkCurrentTimingTag = 0u;
        m_SvsmMotionBenchmarkFramePrepared = false;
        m_SvsmMotionBenchmarkStarted = false;
        m_SvsmMotionBenchmarkDraining = false;
        m_SvsmMotionBenchmarkPreparationFrames = 0u;
        m_SvsmMotionBenchmarkDrainFrames = 0u;
        m_SvsmMotionMeasurementGateComplete = false;
        m_SvsmMotionMeasurementGateContaminated = false;
        m_SvsmMotionMeasurementGateWaitingForCompletion = false;
        m_SvsmMotionMeasurementBenchmarkEndUnixMilliseconds = 0u;
        m_SvsmMotionMeasurementGateStatus =
            m_SvsmMotionMeasurementGateReady
                ? "ready"
                : "not configured";
        m_SvsmMotionBenchmarkTimingPass = nullptr;
        const uint32_t measurementFrameCount =
            GetSvsmMotionBenchmarkMeasurementFrameCount(
                m_SvsmMotionBenchmarkKind);
        m_SvsmMotionBenchmarkSeenGpuFrames.assign(
            measurementFrameCount,
            false);
        m_SvsmMotionBenchmarkDuplicateGpuTag = false;
        m_SvsmMotionBenchmarkInvalidGpuTag = false;
        m_SvsmMotionBenchmarkInvalidGpuTiming = false;
        m_SvsmMotionBenchmarkInvalidCpuTiming = false;
        m_SvsmMotionBenchmarkDetailedTimingObserved = false;
        m_SvsmMotionBenchmarkBatchedSupported = false;
        m_SvsmMotionBenchmarkBatchedActive = false;
        m_SvsmMotionBenchmarkPacketSortingActive = false;
        m_SvsmMotionBenchmarkLevelSkipActive = false;
        m_SvsmMotionBenchmarkPacketCullingActive = false;
        m_SvsmMotionBenchmarkHierarchyActive = false;
        m_SvsmMotionBenchmarkHierarchyUnavailable = false;
        m_SvsmMotionBenchmarkReceiverMaskActive = false;
        m_SvsmMotionBenchmarkReceiverMaskUnavailable = false;
        m_SvsmMotionBenchmarkScatterRasterActive = false;
        m_SvsmMotionBenchmarkPacketCullingUnavailable = false;
        m_SvsmMotionBenchmarkRequestedPathInactive = false;
        m_SvsmMotionBenchmarkStaticHierarchyObservation = {};
        m_SvsmMotionBenchmarkPairedDepthObservation = {};
        m_SvsmMotionBenchmarkDeferredMergeObservation = {};
        m_SvsmMotionBenchmarkGpuTimings.clear();
        m_SvsmMotionBenchmarkCpuTimings.clear();
        m_SvsmMotionBenchmarkGpuSamples.clear();
        m_SvsmMotionBenchmarkMarkSamples.clear();
        m_SvsmMotionBenchmarkAllocationSamples.clear();
        m_SvsmMotionBenchmarkClearingSamples.clear();
        m_SvsmMotionBenchmarkPacketGpuSamples.clear();
        m_SvsmMotionBenchmarkRenderSamples.clear();
        m_SvsmMotionBenchmarkFilterSamples.clear();
        m_SvsmMotionBenchmarkSceneValidationCpuSamples.clear();
        m_SvsmMotionBenchmarkClipmapUpdateCpuSamples.clear();
        m_SvsmMotionBenchmarkPacketCpuSamples.clear();
        m_SvsmMotionBenchmarkCpuSamples.clear();
        // Keep evidence collection itself from introducing heap-allocation
        // spikes into the measured sequence.
        m_SvsmMotionBenchmarkGpuTimings.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkCpuTimings.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkGpuSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkMarkSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkAllocationSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkClearingSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkPacketGpuSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkRenderSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkFilterSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkSceneValidationCpuSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkClipmapUpdateCpuSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkPacketCpuSamples.reserve(
            measurementFrameCount);
        m_SvsmMotionBenchmarkCpuSamples.reserve(
            measurementFrameCount);
        // UI- and command-line-started runs share one durable latest-result
        // artifact so a spike observed interactively has the same evidence as
        // an automated run.
        m_SvsmMotionBenchmarkWriteResultFile = true;
        m_SvsmMotionBenchmarkActive = true;
        m_SvsmMotionBenchmarkStatus =
            std::string("Preparing isolated ") +
            GetSvsmMotionBenchmarkKindName(m_SvsmMotionBenchmarkKind) +
            " Benchmark Position 1 at 1920 x 1080 with " +
            (m_SvsmMotionBenchmarkStartSettings.detailedGpuTimingEnabled
                ? "detailed"
                : "total-only") +
            " GPU timing...";
        WriteSvsmMotionBenchmarkResultFile(
            "state=running\nstatus=" +
            m_SvsmMotionBenchmarkStatus + "\n");
        if (m_SvsmMotionBenchmarkKind ==
            SvsmMotionBenchmarkKind::SunSlow)
        {
            log::info(
                "SVSM SunSlow benchmark started with %s GPU timing: 120 warm frames, 60 baseline frames, a 45-degree sun sweep at exactly 0.1 degrees per rendered frame, 164 recovery frames, the exact reverse sweep, and 164 final recovery frames",
                m_SvsmMotionBenchmarkStartSettings.detailedGpuTimingEnabled
                    ? "detailed"
                    : "total-only");
        }
        else
        {
            log::info(
                "SVSM camera motion benchmark started with %s GPU timing: 180 warm frames, a 45-degree right turn at 0.1 degrees per rendered frame without pacing, a 100-frame hold, and the same return",
                m_SvsmMotionBenchmarkStartSettings.detailedGpuTimingEnabled
                    ? "detailed"
                    : "total-only");
        }
        return true;
    }

    bool StartSvsmSunMotionBenchmark()
    {
        return StartSvsmMotionBenchmark(
            SvsmMotionBenchmarkKind::SunSlow);
    }

    [[nodiscard]] SponzaCameraLocation GetSponzaCameraLocation() const
    {
        return m_SponzaCameraLocation;
    }

    void ApplySponzaCameraPreset(const SponzaCameraPreset& preset)
    {
        m_CameraVerticalFov = preset.VerticalFovDegrees;
        const float zoomReferenceDistance =
            m_ThirdPersonCamera.GetReferenceZoomDistance();
        m_ThirdPersonCamera.ResetZoomReferenceDistance(zoomReferenceDistance);
        m_ThirdPersonCamera.SetExactPose(
            preset.Position,
            preset.Direction,
            preset.Up,
            preset.Right);
        m_FirstPersonCamera.SetExactPose(
            preset.Position,
            preset.Direction,
            preset.Up,
            preset.Right);
        m_PivotCamera.SetExactPose(
            preset.Position,
            preset.Direction,
            preset.Up,
            preset.Right);
        m_StaticCamera.SetExactPose(
            preset.Position,
            preset.Direction,
            preset.Up,
            preset.Right);

        m_PreviousView.reset();
        if (m_ScreenSpaceVisibilityPass)
            m_ScreenSpaceVisibilityPass->ResetHistory();
        if (m_MiniEngineTemporalAAPass)
            m_MiniEngineTemporalAAPass->ResetHistory();
    }

    void SetSponzaCameraLocation(SponzaCameraLocation location)
    {
        if (!m_SponzaCameraLocationsAvailable || m_BenchmarkCameraActive)
            return;

        if (location == SponzaCameraLocation::Free)
        {
            m_SponzaCameraLocation = location;
            log::info("Camera location is now Piloted");
            return;
        }

        const SponzaCameraPreset* preset = FindSponzaCameraPreset(location);
        if (!preset)
            return;

        ApplySponzaCameraPreset(*preset);
        m_SponzaCameraLocation = location;
        log::info(
            "Applied camera location '%s' (%s)",
            preset->Label,
            preset->Id);
    }

    void UpdateSponzaCameraLocationTracking()
    {
        if (!m_SponzaCameraLocationsAvailable ||
            m_SponzaCameraLocation == SponzaCameraLocation::Free)
        {
            return;
        }

        const SponzaCameraPreset* preset =
            FindSponzaCameraPreset(m_SponzaCameraLocation);
        if (preset && !IsSponzaCameraAtPreset(
            *preset,
            m_ThirdPersonCamera.GetPosition(),
            m_ThirdPersonCamera.GetDir(),
            m_ThirdPersonCamera.GetUp()))
        {
            m_SponzaCameraLocation = SponzaCameraLocation::Free;
            log::info("Camera location is now Piloted");
        }
    }

    void RestoreSvsmMotionBenchmarkState()
    {
        m_SvsmMotionBenchmarkActive = false;
        m_SvsmMotionBenchmarkStarted = false;
        m_SvsmMotionBenchmarkDraining = false;
        m_SvsmMotionBenchmarkFramePrepared = false;
        m_SvsmMotionBenchmarkCurrentTimingTag = 0u;
        m_BenchmarkCameraActive =
            m_SvsmMotionBenchmarkPreviousBenchmarkCameraActive;
        if (m_SparseVirtualShadowMapPass)
            m_SparseVirtualShadowMapPass->ResetTimingAccounting();

        if (m_SvsmMotionBenchmarkStartLightNode)
        {
            // The retained node is the object the benchmark actually
            // modified. Restore it even if the scene or primary-light pointer
            // was replaced while an abort was being detected.
            m_SvsmMotionBenchmarkStartLightNode->SetTransform(
                &m_SvsmMotionBenchmarkStartLightTranslation,
                &m_SvsmMotionBenchmarkStartLightRotation,
                &m_SvsmMotionBenchmarkStartLightScaling);
        }
        m_ui.EnableMiniEngineTaa =
            m_SvsmMotionBenchmarkPreviousTaaEnabled;
        m_ui.EnableMiniEngineTaaSharpen =
            m_SvsmMotionBenchmarkPreviousTaaSharpenEnabled;
        m_ui.BendScreenSpaceShadows.enabled =
            m_SvsmMotionBenchmarkPreviousBendEnabled;
        m_ui.DiagnosticCascadedShadowMaps.enabled =
            m_SvsmMotionBenchmarkPreviousCsmEnabled;
        m_ui.ScreenSpaceVisibility.enabled =
            m_SvsmMotionBenchmarkPreviousScreenSpaceVisibilityEnabled;

        m_CameraVerticalFov = m_SvsmMotionBenchmarkPreviousFov;
        m_ThirdPersonCamera.ResetZoomReferenceDistance(
            m_SvsmMotionBenchmarkPreviousZoom);
        m_ThirdPersonCamera.SetExactPose(
            m_SvsmMotionBenchmarkPreviousPosition,
            m_SvsmMotionBenchmarkPreviousDirection,
            m_SvsmMotionBenchmarkPreviousUp,
            m_SvsmMotionBenchmarkPreviousRight);
        m_FirstPersonCamera.SetExactPose(
            m_SvsmMotionBenchmarkPreviousPosition,
            m_SvsmMotionBenchmarkPreviousDirection,
            m_SvsmMotionBenchmarkPreviousUp,
            m_SvsmMotionBenchmarkPreviousRight);
        m_PivotCamera.SetExactPose(
            m_SvsmMotionBenchmarkPreviousPosition,
            m_SvsmMotionBenchmarkPreviousDirection,
            m_SvsmMotionBenchmarkPreviousUp,
            m_SvsmMotionBenchmarkPreviousRight);
        m_StaticCamera.SetExactPose(
            m_SvsmMotionBenchmarkPreviousPosition,
            m_SvsmMotionBenchmarkPreviousDirection,
            m_SvsmMotionBenchmarkPreviousUp,
            m_SvsmMotionBenchmarkPreviousRight);
        m_ui.Camera = m_SvsmMotionBenchmarkPreviousCamera;
        m_SponzaCameraLocation =
            m_SvsmMotionBenchmarkPreviousLocation;
        m_PreviousView.reset();
        if (m_ScreenSpaceVisibilityPass)
            m_ScreenSpaceVisibilityPass->ResetHistory();
        if (m_MiniEngineTemporalAAPass)
            m_MiniEngineTemporalAAPass->ResetHistory();

        if (m_SvsmMotionBenchmarkPreviousWidth > 0 &&
            m_SvsmMotionBenchmarkPreviousHeight > 0)
        {
            glfwSetWindowSize(
                GetDeviceManager()->GetWindow(),
                m_SvsmMotionBenchmarkPreviousWidth,
                m_SvsmMotionBenchmarkPreviousHeight);
        }
        m_SvsmMotionBenchmarkTimingPass = nullptr;
        m_SvsmMotionBenchmarkStartLightNode.reset();
    }

    [[nodiscard]] static uint64_t GetUnixMilliseconds()
    {
        return uint64_t(std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

    [[nodiscard]] static std::filesystem::path
    GetCurrentExecutablePath()
    {
        std::array<wchar_t, 32768u> pathBuffer{};
        const DWORD length = GetModuleFileNameW(
            nullptr,
            pathBuffer.data(),
            DWORD(pathBuffer.size()));
        if (length == 0u || length >= pathBuffer.size())
            return {};

        std::error_code error;
        const std::filesystem::path canonical =
            std::filesystem::weakly_canonical(
                std::filesystem::path(
                    std::wstring(pathBuffer.data(), length)),
                error);
        return error ? std::filesystem::path{} : canonical;
    }

    [[nodiscard]] static std::string ComputeFileSha256(
        const std::filesystem::path& path)
    {
        if (path.empty())
            return {};

        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<UCHAR> hashObject;
        std::vector<UCHAR> digest;
        auto cleanup = [&]() {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0u);
        };

        NTSTATUS status = BCryptOpenAlgorithmProvider(
            &algorithm,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0u);
        if (!BCRYPT_SUCCESS(status))
            return {};

        ULONG objectBytes = 0u;
        ULONG digestBytes = 0u;
        ULONG returnedBytes = 0u;
        status = BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectBytes),
            sizeof(objectBytes),
            &returnedBytes,
            0u);
        if (!BCRYPT_SUCCESS(status) || objectBytes == 0u)
        {
            cleanup();
            return {};
        }
        status = BCryptGetProperty(
            algorithm,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&digestBytes),
            sizeof(digestBytes),
            &returnedBytes,
            0u);
        if (!BCRYPT_SUCCESS(status) || digestBytes != 32u)
        {
            cleanup();
            return {};
        }

        hashObject.resize(objectBytes);
        digest.resize(digestBytes);
        status = BCryptCreateHash(
            algorithm,
            &hash,
            hashObject.data(),
            ULONG(hashObject.size()),
            nullptr,
            0u,
            0u);
        if (!BCRYPT_SUCCESS(status))
        {
            cleanup();
            return {};
        }

        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            cleanup();
            return {};
        }
        std::vector<char> buffer(1024u * 1024u);
        while (input)
        {
            input.read(buffer.data(), std::streamsize(buffer.size()));
            const std::streamsize bytesRead = input.gcount();
            if (bytesRead <= 0)
                break;
            status = BCryptHashData(
                hash,
                reinterpret_cast<PUCHAR>(buffer.data()),
                ULONG(bytesRead),
                0u);
            if (!BCRYPT_SUCCESS(status))
            {
                cleanup();
                return {};
            }
        }
        if (!input.eof() && input.fail())
        {
            cleanup();
            return {};
        }

        status = BCryptFinishHash(
            hash,
            digest.data(),
            ULONG(digest.size()),
            0u);
        if (!BCRYPT_SUCCESS(status))
        {
            cleanup();
            return {};
        }
        cleanup();

        constexpr char HexDigits[] = "0123456789ABCDEF";
        std::string result;
        result.resize(digest.size() * 2u);
        for (size_t index = 0u; index < digest.size(); ++index)
        {
            result[index * 2u] = HexDigits[digest[index] >> 4u];
            result[index * 2u + 1u] =
                HexDigits[digest[index] & 0x0fu];
        }
        return result;
    }

    [[nodiscard]] static bool IsCurrentExecutablePath(
        std::string_view markerPath)
    {
        if (markerPath.empty())
            return false;

        std::error_code markerError;
        const std::filesystem::path canonicalMarker =
            std::filesystem::weakly_canonical(
                std::filesystem::path(std::string(markerPath)),
                markerError);
        const std::filesystem::path canonicalExecutable =
            GetCurrentExecutablePath();
        if (markerError || canonicalExecutable.empty())
            return false;
        return _wcsicmp(
            canonicalMarker.native().c_str(),
            canonicalExecutable.native().c_str()) == 0;
    }

    [[nodiscard]] bool PollSvsmMotionMeasurementCompletion()
    {
        if (m_SvsmMotionMeasurementReadyPath.empty())
            return true;

        m_SvsmMotionMeasurementGateWaitingForCompletion = true;
        std::ifstream markerFile(
            m_SvsmMotionMeasurementReadyPath,
            std::ios::binary);
        const std::string markerContents{
            std::istreambuf_iterator<char>(markerFile),
            std::istreambuf_iterator<char>()};
        const SvsmMotionMeasurementMarker ready =
            ParseSvsmMotionMeasurementMarker(
                m_SvsmMotionMeasurementReadyContents);
        const SvsmMotionMeasurementMarker terminal =
            ParseSvsmMotionMeasurementMarker(markerContents);
        const uint64_t now = GetUnixMilliseconds();
        // The monitor's final hardware sample and atomic marker replacement can
        // take several seconds on systems with slow sensor providers. Keep the
        // wait bounded while avoiding a false invalid result after a clean
        // declared measurement window.
        constexpr uint64_t CompletionGraceMilliseconds = 30000u;
        const uint64_t completionTimeout =
            ready.measurementDeadlineUnixMilliseconds <=
                    std::numeric_limits<uint64_t>::max() -
                        CompletionGraceMilliseconds
                ? ready.measurementDeadlineUnixMilliseconds +
                    CompletionGraceMilliseconds
                : std::numeric_limits<uint64_t>::max();

        if (terminal.state ==
            SvsmMotionMeasurementMarkerState::Ready)
        {
            if (!IsSameSvsmMotionMeasurementRun(ready, terminal))
            {
                m_SvsmMotionMeasurementGateStatus =
                    "ready marker identity changed";
                return true;
            }
            if (now <= completionTimeout)
            {
                m_SvsmMotionMeasurementGateStatus =
                    "waiting for the external measurement window";
                return false;
            }
            m_SvsmMotionMeasurementGateStatus =
                "external measurement completion timed out";
            return true;
        }

        if (terminal.state ==
            SvsmMotionMeasurementMarkerState::Complete)
        {
            m_SvsmMotionMeasurementGateComplete =
                IsSvsmMotionMeasurementMarkerCleanCompletion(
                    ready,
                    terminal,
                    m_SvsmMotionMeasurementBenchmarkEndUnixMilliseconds);
            m_SvsmMotionMeasurementGateStatus =
                m_SvsmMotionMeasurementGateComplete
                    ? "complete"
                    : "complete marker did not cover this benchmark";
            return true;
        }

        if (terminal.state ==
            SvsmMotionMeasurementMarkerState::Contaminated)
        {
            m_SvsmMotionMeasurementGateContaminated =
                terminal.completionTimingValid &&
                IsSameSvsmMotionMeasurementRun(ready, terminal);
            m_SvsmMotionMeasurementGateStatus =
                m_SvsmMotionMeasurementGateContaminated
                    ? "contaminated"
                    : "contaminated marker identity was invalid";
            return true;
        }

        if (now <= completionTimeout)
        {
            m_SvsmMotionMeasurementGateStatus =
                "waiting for a valid terminal measurement marker";
            return false;
        }
        m_SvsmMotionMeasurementGateStatus =
            "terminal measurement marker was missing or invalid";
        return true;
    }

    void WriteSvsmMotionBenchmarkResultFile(
        const std::string& contents) const
    {
        if (!m_SvsmMotionBenchmarkWriteResultFile)
            return;

        static constexpr const char* ResultPath =
            "outputs/svsm-motion-benchmark-latest.txt";
        if (!WriteBenchmarkArtifactAtomically(ResultPath, contents))
        {
            log::warning(
                "SVSM motion benchmark could not atomically publish '%s'",
                ResultPath);
        }
    }

    void WriteSvsmMotionAutostartStage(const char* stage)
    {
        std::ostringstream result;
        result << "state=staging\n"
            << "stage=" << stage << "\n"
            << "benchmarkKind="
            << GetSvsmMotionBenchmarkKindName(
                m_SvsmMotionBenchmarkKind) << "\n"
            << "executableSha256="
            << (m_SvsmMotionExecutableSha256.empty()
                    ? "unavailable"
                    : m_SvsmMotionExecutableSha256) << "\n"
            << "configurationId="
            << BuildSvsmMotionBenchmarkConfigurationId(
                m_SvsmMotionAutostartTargetSettings) << "\n"
            << "configuration="
            << BuildSvsmMotionBenchmarkConfigurationIdentity(
                m_SvsmMotionAutostartTargetSettings) << "\n"
            << "measurementGateConfigured="
            << (!m_SvsmMotionMeasurementReadyPath.empty() ? 1u : 0u)
            << "\n"
            << "measurementGateReady="
            << (m_SvsmMotionMeasurementGateReady ? 1u : 0u) << "\n"
            << "measurementGateComplete="
            << (m_SvsmMotionMeasurementGateComplete ? 1u : 0u) << "\n"
            << "measurementGateContaminated="
            << (m_SvsmMotionMeasurementGateContaminated ? 1u : 0u)
            << "\n"
            << "measurementGateStatus="
            << m_SvsmMotionMeasurementGateStatus << "\n"
            << "dredDiagnostics="
            << (m_DredDiagnosticsActive ? 1u : 0u) << "\n"
            << "diagnosticConfiguration="
            << (m_SvsmMotionDiagnosticConfiguration ? 1u : 0u) << "\n"
            << "gpuTimingMode="
            << (m_SvsmMotionAutostartTargetSettings.
                    detailedGpuTimingEnabled
                ? "detailed"
                : "totalOnly")
            << "\n"
            << "taaEnabled="
            << (m_ui.EnableMiniEngineTaa ? 1u : 0u) << "\n"
            << "bendEnabled="
            << (m_ui.BendScreenSpaceShadows.enabled ? 1u : 0u)
            << "\n"
            << "diagnosticCsmEnabled="
            << (m_ui.DiagnosticCascadedShadowMaps.enabled ? 1u : 0u)
            << "\n"
            << "screenSpaceVisibilityEnabled="
            << (m_ui.ScreenSpaceVisibility.enabled ? 1u : 0u)
            << "\n"
            << "physicalPages="
            << m_SvsmMotionAutostartTargetSettings.physicalPageCount
            << "\n"
            << "pageRenderBudget="
            << m_SvsmMotionAutostartTargetSettings.pageRenderBudget
            << "\n"
            << "deterministicFinePageSelectorConfigured="
            << (ShouldUseSvsmDeterministicFinePageBudget(
                    m_SvsmMotionAutostartTargetSettings.pageRenderBudget,
                    m_SvsmMotionAutostartTargetSettings.physicalPageCount,
                    m_SvsmMotionAutostartTargetSettings
                        .coarsestPageRenderBudgetEnabled)
                    ? 1u
                    : 0u) << "\n"
            << "precomposedClipmapTransformsConfigured="
            << (m_SvsmMotionAutostartTargetSettings
                    .precomposedClipmapTransformsEnabled
                    ? 1u
                    : 0u) << "\n"
            << "staticDepthHierarchyCullingConfigured="
            << (m_SvsmMotionAutostartTargetSettings
                    .staticDepthHierarchyCullingEnabled
                    ? 1u
                    : 0u) << "\n"
            << "staticDepthHierarchyBias="
            << m_SvsmMotionAutostartTargetSettings
                .staticDepthHierarchyBias << "\n"
            << "pairedStaticDynamicDepthConfigured="
            << (m_SvsmMotionAutostartTargetSettings
                    .pairedStaticDynamicDepthEnabled
                    ? 1u
                    : 0u) << "\n"
            << "deferredStaticDepthMergeConfigured="
            << (m_SvsmMotionAutostartTargetSettings
                    .deferredStaticDepthMergeEnabled
                    ? 1u
                    : 0u) << "\n"
            << "gpuGatedDrawSubmission="
            << (m_SvsmMotionAutostartTargetSettings.
                    gpuGatedDrawSubmission ? 1u : 0u) << "\n"
            << "batchedDrawSubmission="
            << (m_SvsmMotionAutostartTargetSettings.
                    batchedDrawSubmissionEnabled ? 1u : 0u) << "\n"
            << "packetStateSorting="
            << (m_SvsmMotionAutostartTargetSettings.
                    packetStateSortingEnabled ? 1u : 0u) << "\n"
            << "levelEmptyWorkSkip="
            << (m_SvsmMotionAutostartTargetSettings.
                    levelEmptyWorkSkipEnabled ? 1u : 0u) << "\n"
            << "packetPageCulling="
            << (m_SvsmMotionAutostartTargetSettings.
                    packetPageCullingEnabled ? 1u : 0u) << "\n"
            << "hierarchicalScheduledPageMask="
            << (m_SvsmMotionAutostartTargetSettings.
                    hierarchicalScheduledPageMaskEnabled
                    ? 1u
                    : 0u) << "\n"
            << "receiverPageMaskCulling="
            << (m_SvsmMotionAutostartTargetSettings.
                    receiverPageMaskCullingEnabled
                    ? 1u
                    : 0u) << "\n"
            << "scatterRaster="
            << (m_SvsmMotionAutostartTargetSettings.
                    dirtyPageScatterRasterEnabled ? 1u : 0u) << "\n";
        WriteSvsmMotionBenchmarkResultFile(result.str());
        log::info("SVSM motion autostart stage: %s", stage);
    }

    void AbortSvsmMotionBenchmark(const char* reason)
    {
        m_SvsmMotionBenchmarkStatus =
            std::string("Aborted: ") + reason;
        std::ostringstream result;
        result << "state=aborted\n"
            << "evidenceValid=0\n"
            << "status=" << m_SvsmMotionBenchmarkStatus << "\n"
            << "configurationId="
            << BuildSvsmMotionBenchmarkConfigurationId(
                m_SvsmMotionBenchmarkStartSettings) << "\n"
            << "configuration="
            << BuildSvsmMotionBenchmarkConfigurationIdentity(
                m_SvsmMotionBenchmarkStartSettings) << "\n";
        WriteSvsmMotionBenchmarkResultFile(result.str());
        log::warning(
            "SVSM motion benchmark aborted: %s",
            reason);
        RestoreSvsmMotionBenchmarkState();
    }

    void CollectCompletedSvsmMotionBenchmarkTimings()
    {
        if (!m_SvsmMotionBenchmarkTimingPass ||
            m_SvsmMotionBenchmarkTimingPass !=
                m_SparseVirtualShadowMapPass.get())
        {
            return;
        }

        SparseVirtualShadowMapGpuTiming timing;
        while (m_SvsmMotionBenchmarkTimingPass->PopCompletedTiming(timing))
        {
            if (!IsSvsmMotionBenchmarkMeasurementFrame(
                    m_SvsmMotionBenchmarkKind,
                    timing.sourceTag))
            {
                m_SvsmMotionBenchmarkInvalidGpuTag = true;
                continue;
            }
            const size_t index = size_t(
                timing.sourceTag -
                GetSvsmMotionBenchmarkWarmFrameCount(
                    m_SvsmMotionBenchmarkKind));
            if (index >= m_SvsmMotionBenchmarkSeenGpuFrames.size())
            {
                m_SvsmMotionBenchmarkInvalidGpuTag = true;
                continue;
            }
            if (!IsValidSvsmMotionBenchmarkGpuTiming(
                    timing.pageMarkingMilliseconds,
                    timing.allocationMilliseconds,
                    timing.clearingMilliseconds,
                    timing.packetPageCullingMilliseconds,
                    timing.pageRenderingMilliseconds,
                    timing.filteringMilliseconds,
                    timing.totalMilliseconds,
                    timing.detailedGpuTimingEnabled,
                    m_SvsmMotionBenchmarkStartSettings.
                        detailedGpuTimingEnabled))
            {
                m_SvsmMotionBenchmarkInvalidGpuTiming = true;
                continue;
            }
            if (m_SvsmMotionBenchmarkSeenGpuFrames[index])
            {
                m_SvsmMotionBenchmarkDuplicateGpuTag = true;
                continue;
            }
            m_SvsmMotionBenchmarkSeenGpuFrames[index] = true;
            m_SvsmMotionBenchmarkDetailedTimingObserved |=
                timing.detailedGpuTimingEnabled;
            m_SvsmMotionBenchmarkGpuTimings.push_back(timing);
            m_SvsmMotionBenchmarkGpuSamples.push_back(
                timing.totalMilliseconds);
            m_SvsmMotionBenchmarkMarkSamples.push_back(
                timing.pageMarkingMilliseconds);
            m_SvsmMotionBenchmarkAllocationSamples.push_back(
                timing.allocationMilliseconds);
            m_SvsmMotionBenchmarkClearingSamples.push_back(
                timing.clearingMilliseconds);
            m_SvsmMotionBenchmarkPacketGpuSamples.push_back(
                timing.packetPageCullingMilliseconds);
            m_SvsmMotionBenchmarkRenderSamples.push_back(
                timing.pageRenderingMilliseconds);
            m_SvsmMotionBenchmarkFilterSamples.push_back(
                timing.filteringMilliseconds);
        }
    }

    void FinishSvsmMotionBenchmark()
    {
        auto summarizePhase = [this](
                                  SvsmMotionBenchmarkPhase phase) {
            std::vector<float> values;
            values.reserve(m_SvsmMotionBenchmarkGpuTimings.size());
            for (const SparseVirtualShadowMapGpuTiming& timing :
                m_SvsmMotionBenchmarkGpuTimings)
            {
                if (GetSvsmMotionBenchmarkPhase(
                        m_SvsmMotionBenchmarkKind,
                        timing.sourceTag) == phase)
                {
                    values.push_back(timing.totalMilliseconds);
                }
            }
            return SummarizeSvsmMotionBenchmarkSamples(std::move(values));
        };

        const SparseVirtualShadowMapTimingAccounting accounting =
            m_SvsmMotionBenchmarkTimingPass
            ? m_SvsmMotionBenchmarkTimingPass->GetTimingAccounting()
            : SparseVirtualShadowMapTimingAccounting{};
        const bool diagnosticConfiguration =
            m_SvsmMotionDiagnosticConfiguration ||
            !IsSvsmMotionBenchmarkAcceptanceConfiguration(
                m_SvsmMotionBenchmarkStartSettings);
        const bool detailedGpuTiming =
            m_SvsmMotionBenchmarkStartSettings.detailedGpuTimingEnabled ||
            m_SvsmMotionBenchmarkDetailedTimingObserved;
        const bool environmentValid =
            IsSvsmMotionBenchmarkEnvironmentValid(
                m_DredDiagnosticsActive,
                diagnosticConfiguration);
        const bool measurementGateValid =
            !m_SvsmMotionMeasurementReadyPath.empty() &&
            m_SvsmMotionMeasurementGateReady &&
            m_SvsmMotionMeasurementGateComplete &&
            !m_SvsmMotionMeasurementGateContaminated;
        const bool executableIdentityValid =
            m_SvsmMotionExecutableSha256.size() == 64u;
        const bool evidenceValid = executableIdentityValid &&
            environmentValid &&
            measurementGateValid &&
            !m_SvsmMotionBenchmarkDetailedTimingObserved &&
            IsSvsmMotionBenchmarkEvidenceValidForFrameCount(
                GetSvsmMotionBenchmarkMeasurementFrameCount(
                    m_SvsmMotionBenchmarkKind),
                m_SvsmMotionBenchmarkGpuSamples.size(),
                m_SvsmMotionBenchmarkCpuSamples.size(),
                accounting.issued,
                accounting.dropped,
                accounting.retired,
                accounting.outstanding,
                m_SvsmMotionBenchmarkDuplicateGpuTag,
                m_SvsmMotionBenchmarkInvalidGpuTag,
                m_SvsmMotionBenchmarkInvalidGpuTiming,
                m_SvsmMotionBenchmarkInvalidCpuTiming);
        const SvsmMotionBenchmarkTimingSummary gpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkGpuSamples);
        const SvsmMotionBenchmarkTimingSummary baselineSummary =
            summarizePhase(SvsmMotionBenchmarkPhase::Baseline);
        const SvsmMotionBenchmarkTimingSummary forwardSummary =
            summarizePhase(SvsmMotionBenchmarkPhase::Forward);
        const SvsmMotionBenchmarkTimingSummary recoverySummary =
            summarizePhase(SvsmMotionBenchmarkPhase::Recovery);
        const SvsmMotionBenchmarkTimingSummary reverseSummary =
            summarizePhase(SvsmMotionBenchmarkPhase::Reverse);
        const SvsmMotionBenchmarkTimingSummary finalRecoverySummary =
            summarizePhase(SvsmMotionBenchmarkPhase::FinalRecovery);
        const float gpuMedian = gpuSummary.median;
        const float gpuWorst = gpuSummary.maximum;
        const SvsmMotionBenchmarkTimingSummary markingSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkMarkSamples);
        const SvsmMotionBenchmarkTimingSummary allocationSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkAllocationSamples);
        const SvsmMotionBenchmarkTimingSummary clearingSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkClearingSamples);
        const SvsmMotionBenchmarkTimingSummary packetGpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkPacketGpuSamples);
        const SvsmMotionBenchmarkTimingSummary renderingSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkRenderSamples);
        const SvsmMotionBenchmarkTimingSummary filteringSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkFilterSamples);
        const SvsmMotionBenchmarkTimingSummary sceneValidationCpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkSceneValidationCpuSamples);
        const SvsmMotionBenchmarkTimingSummary clipmapUpdateCpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkClipmapUpdateCpuSamples);
        const SvsmMotionBenchmarkTimingSummary packetCpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkPacketCpuSamples);
        const SvsmMotionBenchmarkTimingSummary cpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkCpuSamples);
        const float sceneValidationCpuMedian =
            sceneValidationCpuSummary.median;
        const float clipmapUpdateCpuMedian =
            clipmapUpdateCpuSummary.median;
        const float packetCpuMedian = packetCpuSummary.median;
        const float cpuMedian = cpuSummary.median;
        const float cpuWorst = cpuSummary.maximum;
        const bool batchedDrawRequested =
            m_SvsmMotionBenchmarkStartSettings.gpuGatedDrawSubmission &&
            m_SvsmMotionBenchmarkStartSettings
                .batchedDrawSubmissionEnabled;
        const bool packetSortingRequested =
            batchedDrawRequested &&
            m_SvsmMotionBenchmarkStartSettings
                .packetStateSortingEnabled;
        const bool levelSkipRequested =
            batchedDrawRequested &&
            m_SvsmMotionBenchmarkStartSettings
                .levelEmptyWorkSkipEnabled;
        const bool packetCullingRequested =
            m_SvsmMotionBenchmarkStartSettings.gpuGatedDrawSubmission &&
            m_SvsmMotionBenchmarkStartSettings
                .packetPageCullingEnabled;
        const bool scatterRasterRequested =
            packetCullingRequested &&
            m_SvsmMotionBenchmarkStartSettings
                .dirtyPageScatterRasterEnabled;
        const bool hierarchyRequested =
            IsSvsmMotionBenchmarkHierarchyRequested(
                packetCullingRequested,
                m_SvsmMotionBenchmarkStartSettings
                    .hierarchicalScheduledPageMaskEnabled);
        const bool requestedPathActive =
            !m_SvsmMotionBenchmarkRequestedPathInactive &&
            (!batchedDrawRequested ||
                m_SvsmMotionBenchmarkBatchedActive) &&
            (!packetSortingRequested ||
                m_SvsmMotionBenchmarkPacketSortingActive) &&
            (!levelSkipRequested ||
                m_SvsmMotionBenchmarkLevelSkipActive) &&
            (!packetCullingRequested ||
                m_SvsmMotionBenchmarkPacketCullingActive) &&
            (!hierarchyRequested ||
                m_SvsmMotionBenchmarkHierarchyActive) &&
            (!scatterRasterRequested ||
                m_SvsmMotionBenchmarkScatterRasterActive) &&
            !m_SvsmMotionBenchmarkPacketCullingUnavailable &&
            !m_SvsmMotionBenchmarkHierarchyUnavailable;
        const std::size_t framesOverMedianTarget =
            CountSvsmMotionBenchmarkSamplesAbove(
                m_SvsmMotionBenchmarkGpuSamples,
                SvsmMotionBenchmarkMedianTargetMilliseconds);
        const std::size_t framesOverSpikeCeiling =
            CountSvsmMotionBenchmarkSamplesAbove(
                m_SvsmMotionBenchmarkGpuSamples,
                SvsmMotionBenchmarkSpikeCeilingMilliseconds);
        const std::size_t framesOverOneMillisecond =
            CountSvsmMotionBenchmarkSamplesAbove(
                m_SvsmMotionBenchmarkGpuSamples,
                1.f);
        const bool medianTargetMet = evidenceValid &&
            requestedPathActive &&
            gpuMedian <= SvsmMotionBenchmarkMedianTargetMilliseconds;
        const bool spikeTargetMet = evidenceValid &&
            requestedPathActive &&
            gpuWorst <= SvsmMotionBenchmarkSpikeCeilingMilliseconds;
        const bool gpuTargetMet = IsSvsmMotionBenchmarkGpuTargetMet(
            evidenceValid,
            requestedPathActive,
            gpuSummary);
        const bool configuredScatterSafetyBounded =
            IsSvsmDirtyPageScatterSafetyBounded(
                m_SvsmMotionBenchmarkStartSettings
                    .dirtyPageScatterAmplificationGuardEnabled,
                m_SvsmMotionBenchmarkStartSettings
                    .coarsestPageRenderBudgetEnabled,
                m_SvsmMotionBenchmarkStartSettings.pageRenderBudget,
                m_SvsmMotionBenchmarkStartSettings
                    .dirtyPageScatterMaximumAmplification);

        std::vector<SparseVirtualShadowMapGpuTiming> slowestTimings =
            m_SvsmMotionBenchmarkGpuTimings;
        std::sort(
            slowestTimings.begin(),
            slowestTimings.end(),
            [](const SparseVirtualShadowMapGpuTiming& left,
               const SparseVirtualShadowMapGpuTiming& right) {
                if (left.totalMilliseconds != right.totalMilliseconds)
                    return left.totalMilliseconds > right.totalMilliseconds;
                return left.sourceTag < right.sourceTag;
            });
        constexpr size_t SlowFrameCount = 10u;
        if (slowestTimings.size() > SlowFrameCount)
            slowestTimings.resize(SlowFrameCount);

        std::ostringstream status;
        status << (evidenceValid
                ? "Complete: "
                : "Complete (invalid evidence): ")
            << m_SvsmMotionBenchmarkGpuSamples.size()
            << " " << GetSvsmMotionBenchmarkKindName(
                m_SvsmMotionBenchmarkKind)
            << " samples, GPU median "
            << std::fixed << std::setprecision(3)
            << gpuMedian
            << " ms / p95 "
            << gpuSummary.p95
            << " ms / p99 "
            << gpuSummary.p99
            << " ms / worst "
            << gpuWorst
            << " ms; "
            << (detailedGpuTiming ? "detailed" : "total-only")
            << " GPU timing; CPU stage medians validate "
            << sceneValidationCpuMedian
            << " / views "
            << clipmapUpdateCpuMedian
            << " / packets "
            << packetCpuMedian
            << " / all "
            << cpuMedian
            << " ms (all worst "
            << cpuWorst
            << " ms); timing "
            << accounting.issued << " issued / "
            << accounting.retired << " retired / "
            << accounting.dropped << " dropped / "
            << accounting.outstanding << " outstanding; requested path "
            << (requestedPathActive ? "active; " : "inactive; ")
            << "0.400 ms median target "
            << (medianTargetMet ? "met" : "not met")
            << "; 0.700 ms spike ceiling "
            << (spikeTargetMet ? "met." : "not met.");
        m_SvsmMotionBenchmarkStatus = status.str();

        std::ostringstream result;
        result << "state=complete\n"
            << "evidenceValid=" << (evidenceValid ? 1 : 0) << "\n"
            << "environmentValid=" <<
                (environmentValid ? 1 : 0) << "\n"
            << "measurementGateConfigured="
            << (!m_SvsmMotionMeasurementReadyPath.empty() ? 1u : 0u)
            << "\n"
            << "measurementGateReady="
            << (m_SvsmMotionMeasurementGateReady ? 1u : 0u) << "\n"
            << "measurementGateComplete="
            << (m_SvsmMotionMeasurementGateComplete ? 1u : 0u) << "\n"
            << "measurementGateContaminated="
            << (m_SvsmMotionMeasurementGateContaminated ? 1u : 0u)
            << "\n"
            << "measurementGateStatus="
            << m_SvsmMotionMeasurementGateStatus << "\n"
            << "measurementGateValid="
            << (measurementGateValid ? 1u : 0u) << "\n"
            << "executableIdentityValid="
            << (executableIdentityValid ? 1u : 0u) << "\n"
            << "dredDiagnostics=" <<
                (m_DredDiagnosticsActive ? 1 : 0) << "\n"
            << "diagnosticConfiguration=" <<
                (diagnosticConfiguration ? 1 : 0) << "\n"
            << "requestedPathActive=" <<
                (requestedPathActive ? 1 : 0) << "\n"
            << "gpuTimingMode="
            << (detailedGpuTiming ? "detailed" : "totalOnly") << "\n"
            << "stageTimingsAvailable="
            << (m_SvsmMotionBenchmarkDetailedTimingObserved ? 1u : 0u)
            << "\n"
            << "detailedGpuTimingObserved=" <<
                (m_SvsmMotionBenchmarkDetailedTimingObserved ? 1 : 0)
                << "\n"
            << "requestedPathInactiveObserved=" <<
                (m_SvsmMotionBenchmarkRequestedPathInactive ? 1 : 0)
                << "\n"
            << "targetMet=" << (gpuTargetMet ? 1 : 0) << "\n"
            << "medianTargetMet=" << (medianTargetMet ? 1 : 0) << "\n"
            << "spikeTargetMet=" << (spikeTargetMet ? 1 : 0) << "\n"
            << "renderer=" << GetDeviceManager()->GetRendererString() << "\n"
            << "scene=" << m_CurrentSceneName << "\n"
            << "commit=" << UVSR_GIT_COMMIT << "\n"
            << "executableSha256="
            << (m_SvsmMotionExecutableSha256.empty()
                    ? "unavailable"
                    : m_SvsmMotionExecutableSha256) << "\n"
            << "configurationId="
            << BuildSvsmMotionBenchmarkConfigurationId(
                m_SvsmMotionBenchmarkStartSettings) << "\n"
            << "configuration="
            << BuildSvsmMotionBenchmarkConfigurationIdentity(
                m_SvsmMotionBenchmarkStartSettings) << "\n"
            << "invalidGpuTiming="
            << (m_SvsmMotionBenchmarkInvalidGpuTiming ? 1u : 0u)
            << "\n"
            << "invalidCpuTiming="
            << (m_SvsmMotionBenchmarkInvalidCpuTiming ? 1u : 0u)
            << "\n"
            << "width=1920\n"
            << "height=1080\n"
            << "benchmarkKind="
            << GetSvsmMotionBenchmarkKindName(
                m_SvsmMotionBenchmarkKind) << "\n"
            << "motionStepTenthDegreeTicks=1\n"
            << "motionStepDegrees=0.1\n"
            << "cameraStepDegrees="
            << (m_SvsmMotionBenchmarkKind ==
                    SvsmMotionBenchmarkKind::Camera
                    ? 0.1f
                    : 0.f) << "\n"
            << "sunStepDegrees="
            << (m_SvsmMotionBenchmarkKind ==
                    SvsmMotionBenchmarkKind::SunSlow
                    ? 0.1f
                    : 0.f) << "\n"
            << "warmFrames="
            << GetSvsmMotionBenchmarkWarmFrameCount(
                m_SvsmMotionBenchmarkKind) << "\n"
            << "measurementFrames="
            << GetSvsmMotionBenchmarkMeasurementFrameCount(
                m_SvsmMotionBenchmarkKind) << "\n"
            << "sampleCount=" << m_SvsmMotionBenchmarkGpuSamples.size()
            << "\n"
            << "gpuMedianMs=" << std::fixed << std::setprecision(6)
            << gpuMedian << "\n"
            << "gpuP95Ms=" << gpuSummary.p95 << "\n"
            << "gpuP99Ms=" << gpuSummary.p99 << "\n"
            << "gpuWorstMs=" << gpuWorst << "\n"
            << "framesOver0_4Ms=" << framesOverMedianTarget << "\n"
            << "framesOver0_7Ms=" << framesOverSpikeCeiling << "\n"
            << "framesOver1_0Ms=" << framesOverOneMillisecond << "\n"
            << "timingIssued=" << accounting.issued << "\n"
            << "timingRetired=" << accounting.retired << "\n"
            << "timingDropped=" << accounting.dropped << "\n"
            << "timingOutstanding=" << accounting.outstanding << "\n"
            << "preset=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.preset) << "\n"
            << "mode=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.mode) << "\n"
            << "firstClipmapExtent=" <<
                m_SvsmMotionBenchmarkStartSettings.firstClipmapExtent << "\n"
            << "maximumLightDepth=" <<
                m_SvsmMotionBenchmarkStartSettings.maximumLightDepth << "\n"
            << "physicalPageCount=" <<
                m_SvsmMotionBenchmarkStartSettings.physicalPageCount << "\n"
            << "pageRenderBudget=" <<
                m_SvsmMotionBenchmarkStartSettings.pageRenderBudget << "\n"
            << "coarsestPageRenderBudget=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .coarsestPageRenderBudgetEnabled ? 1u : 0u) << "\n"
            << "deterministicFinePageSelectorConfigured=" << (
                ShouldUseSvsmDeterministicFinePageBudget(
                    m_SvsmMotionBenchmarkStartSettings.pageRenderBudget,
                    m_SvsmMotionBenchmarkStartSettings.physicalPageCount,
                    m_SvsmMotionBenchmarkStartSettings
                        .coarsestPageRenderBudgetEnabled)
                        ? 1u
                        : 0u) << "\n"
            << "markingMode=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.markingMode) << "\n"
            << "perPixelMarkingDedupe=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .perPixelMarkingDedupeEnabled ? 1u : 0u) << "\n"
            << "filterMode=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.filterMode) << "\n"
            << "filterKernel=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.filterKernel) << "\n"
            << "poissonOrdering=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.poissonOrdering) << "\n"
            << "tapCount=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.tapCount) << "\n"
            << "resolutionBias=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.resolutionBias) << "\n"
            << "debugView=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.debugView) << "\n"
            << "precomposedClipmapTransformsConfigured=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .precomposedClipmapTransformsEnabled
                        ? 1u
                        : 0u) << "\n"
            << "caching=" << (
                m_SvsmMotionBenchmarkStartSettings.cachingEnabled
                    ? 1u : 0u) << "\n"
            << "lightDepthOriginGuardBand=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .lightDepthOriginGuardBandEnabled ? 1u : 0u) << "\n"
            << "lightDepthOriginGuardBandFraction=" <<
                m_SvsmMotionBenchmarkStartSettings
                    .lightDepthOriginGuardBandFraction << "\n"
            << "staticPageRequestReuse=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .staticPageRequestReuseEnabled ? 1u : 0u) << "\n"
            << "allocationBudgetSaturationEarlyOut=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .allocationBudgetSaturationEarlyOutEnabled
                        ? 1u : 0u) << "\n"
            << "allocationBudgetSaturationEarlyOutActive=" << (
                IsSvsmAllocationBudgetSaturationEarlyOutActive(
                    m_SvsmMotionBenchmarkStartSettings.mode,
                    m_SvsmMotionBenchmarkStartSettings
                        .allocationBudgetSaturationEarlyOutEnabled,
                    m_SvsmMotionBenchmarkStartSettings
                        .pageRenderBudget) ? 1u : 0u) << "\n"
            << "finiteBudgetStaticDrain=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .finiteBudgetStaticDrainEnabled ? 1u : 0u) << "\n"
            << "staticVisibilityCaching=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .staticVisibilityCachingEnabled ? 1u : 0u) << "\n"
            << "sceneStateCaching=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .sceneStateCachingEnabled ? 1u : 0u) << "\n"
            << "casterOnlySceneRevision=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .casterOnlySceneRevisionEnabled ? 1u : 0u) << "\n"
            << "renderPacketCaching=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .renderPacketCachingEnabled ? 1u : 0u) << "\n"
            << "persistentCasterSourceCaching=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .persistentCasterSourceCachingEnabled
                        ? 1u : 0u) << "\n"
            << "sharedClipmapPacketBuilder=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .sharedClipmapPacketBuilderEnabled
                        ? 1u : 0u) << "\n"
            << "opaqueRasterSpecialization=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .opaqueRasterSpecializationEnabled
                        ? 1u : 0u) << "\n"
            << "leanAlphaTestedBindings=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .leanAlphaTestedBindingsEnabled
                        ? 1u : 0u) << "\n"
            << "staticDepthHierarchyCullingConfigured=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .staticDepthHierarchyCullingEnabled
                        ? 1u
                        : 0u) << "\n"
            << "staticDepthHierarchyBias=" <<
                m_SvsmMotionBenchmarkStartSettings
                    .staticDepthHierarchyBias << "\n"
            << "pairedStaticDynamicDepthConfigured=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .pairedStaticDynamicDepthEnabled
                        ? 1u : 0u) << "\n"
            << "pairedStaticDynamicDepth=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .pairedStaticDynamicDepthEnabled
                        ? 1u : 0u) << "\n"
            << "deferredStaticDepthMergeConfigured=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .deferredStaticDepthMergeEnabled
                        ? 1u : 0u) << "\n"
            << "deferredStaticDepthMerge=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .deferredStaticDepthMergeEnabled
                        ? 1u : 0u) << "\n"
            << "movingLightUncachedPolicy=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .movingLightUncachedEnabled
                        ? 1u : 0u) << "\n"
            << "retainPhysicalMappingsOnContentInvalidation=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .retainPhysicalMappingsOnContentInvalidationEnabled
                        ? 1u : 0u) << "\n"
            << "movingLightLodBias=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .movingLightLodBiasEnabled
                        ? 1u : 0u) << "\n"
            << "movingLightResolutionBias=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings
                    .movingLightResolutionBias) << "\n"
            << "movingLightLodRecoveryFrames=" <<
                m_SvsmMotionBenchmarkStartSettings
                    .movingLightLodRecoveryFrames << "\n"
            << "receiverDistanceMipClamp=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .receiverDistanceMipClampEnabled
                        ? 1u : 0u) << "\n"
            << "receiverDistanceMipClampStartScale=" <<
                m_SvsmMotionBenchmarkStartSettings
                    .receiverDistanceMipClampStartScale << "\n"
            << "receiverDistanceMipClampMaximumLevel=" <<
                m_SvsmMotionBenchmarkStartSettings
                    .receiverDistanceMipClampMaximumLevel << "\n"
            << "movingLightContinuousReceiverBias=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .movingLightContinuousReceiverBiasEnabled
                        ? 1u : 0u) << "\n"
            << "localizedInvalidation=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .localizedInvalidationEnabled
                        ? 1u : 0u) << "\n"
            << "tightLocalizedInvalidationBounds=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .tightLocalizedInvalidationBoundsEnabled
                        ? 1u : 0u) << "\n"
            << "adaptiveStaticCasterCache=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .adaptiveCasterCacheClassificationEnabled
                        ? 1u : 0u) << "\n"
            << "defaultObjectInvalidationMode=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings
                    .defaultObjectInvalidationMode) << "\n"
            << "gpuGatedDrawSubmission=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .gpuGatedDrawSubmission ? 1u : 0u) << "\n"
            << "batchedDrawSubmission=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .batchedDrawSubmissionEnabled ? 1u : 0u) << "\n"
            << "packetStateSorting=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .packetStateSortingEnabled ? 1u : 0u) << "\n"
            << "levelEmptyWorkSkip=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .levelEmptyWorkSkipEnabled ? 1u : 0u) << "\n"
            << "packetPageCulling=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .packetPageCullingEnabled ? 1u : 0u) << "\n"
            << "hierarchicalScheduledPageMask=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .hierarchicalScheduledPageMaskEnabled
                    ? 1u
                    : 0u) << "\n"
            << "receiverPageMaskCulling=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .receiverPageMaskCullingEnabled
                    ? 1u
                    : 0u) << "\n"
            << "dirtyPageScatterRaster=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .dirtyPageScatterRasterEnabled ? 1u : 0u) << "\n"
            << "dirtyPageScatterSafetyBounded=" << (
                configuredScatterSafetyBounded ? 1u : 0u) << "\n"
            << "scatterAlphaTestEarlyReject=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .scatterAlphaTestEarlyRejectEnabled ? 1u : 0u) << "\n"
            << "scatterAlphaTestEarlyRejectActive=" << (
                configuredScatterSafetyBounded &&
                IsSvsmDirtyPageScatterOptimizationActive(
                    m_SvsmMotionBenchmarkStartSettings.mode,
                    m_SvsmMotionBenchmarkStartSettings
                        .gpuGatedDrawSubmission,
                    m_SvsmMotionBenchmarkStartSettings
                        .packetPageCullingEnabled,
                    m_SvsmMotionBenchmarkStartSettings
                        .dirtyPageScatterRasterEnabled,
                    m_SvsmMotionBenchmarkStartSettings
                        .scatterAlphaTestEarlyRejectEnabled)
                        ? 1u : 0u) << "\n"
            << "dirtyPageScatterAmplificationGuard=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .dirtyPageScatterAmplificationGuardEnabled
                        ? 1u : 0u) << "\n"
            << "dirtyPageScatterAmplificationGuardActive=" << (
                configuredScatterSafetyBounded &&
                IsSvsmDirtyPageScatterOptimizationActive(
                    m_SvsmMotionBenchmarkStartSettings.mode,
                    m_SvsmMotionBenchmarkStartSettings
                        .gpuGatedDrawSubmission,
                    m_SvsmMotionBenchmarkStartSettings
                        .packetPageCullingEnabled,
                    m_SvsmMotionBenchmarkStartSettings
                        .dirtyPageScatterRasterEnabled,
                    m_SvsmMotionBenchmarkStartSettings
                        .dirtyPageScatterAmplificationGuardEnabled)
                        ? 1u : 0u) << "\n"
            << "dirtyPageScatterMaximumAmplification=" <<
                m_SvsmMotionBenchmarkStartSettings
                    .dirtyPageScatterMaximumAmplification << "\n"
            << "packetRectangleDirectScan=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .packetRectangleDirectScanEnabled ? 1u : 0u) << "\n"
            << "recentPageEvictionGrace=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .recentPageEvictionGraceEnabled ? 1u : 0u) << "\n"
            << "pageTranslationCaching=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .pageTranslationCachingEnabled ? 1u : 0u) << "\n"
            << "detailedGpuTimingConfigured=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .detailedGpuTimingEnabled ? 1u : 0u) << "\n"
            << "adaptiveFiltering=" << (
                m_SvsmMotionBenchmarkStartSettings.adaptiveFiltering
                    ? 1u : 0u) << "\n"
            << "taaEnabled=" << (
                m_SvsmMotionBenchmarkStartTaaEnabled ? 1u : 0u) << "\n"
            << "taaActive=" << (
                m_SvsmMotionBenchmarkStartUsesTaa ? 1u : 0u) << "\n"
            << "taaSharpenEnabled=" << (
                m_SvsmMotionBenchmarkStartTaaSharpenEnabled
                    ? 1u : 0u) << "\n"
            << "bendEnabled=" << (
                m_SvsmMotionBenchmarkStartBendEnabled ? 1u : 0u)
                << "\n"
            << "diagnosticCsmEnabled=" << (
                m_SvsmMotionBenchmarkStartCsmEnabled ? 1u : 0u)
                << "\n"
            << "screenSpaceVisibilityEnabled=" << (
                m_SvsmMotionBenchmarkStartScreenSpaceVisibilityEnabled
                    ? 1u : 0u) << "\n"
            << "whiteWorldMode=" << uint32_t(
                m_SvsmMotionBenchmarkStartWhiteWorld) << "\n"
            << "environmentBackgroundEnabled=" << (
                m_SvsmMotionBenchmarkStartEnvironmentBackgroundEnabled
                    ? 1u : 0u) << "\n"
            << "startLightDirectionX="
            << m_SvsmMotionBenchmarkStartLightDirection.x << "\n"
            << "startLightDirectionY="
            << m_SvsmMotionBenchmarkStartLightDirection.y << "\n"
            << "startLightDirectionZ="
            << m_SvsmMotionBenchmarkStartLightDirection.z << "\n"
            << "sunRotationAxisX="
            << m_SvsmMotionBenchmarkSunRotationAxis.x << "\n"
            << "sunRotationAxisY="
            << m_SvsmMotionBenchmarkSunRotationAxis.y << "\n"
            << "sunRotationAxisZ="
            << m_SvsmMotionBenchmarkSunRotationAxis.z << "\n"
            << "lightPointerStable=1\n"
            << "lightNodePointerStable=1\n"
            << "staticDepthHierarchyCullingRequestedObserved=" << (
                m_SvsmMotionBenchmarkStaticHierarchyObservation
                    .requestedObserved ? 1u : 0u) << "\n"
            << "staticDepthHierarchyCullingActiveObserved=" << (
                m_SvsmMotionBenchmarkStaticHierarchyObservation
                    .activeObserved ? 1u : 0u) << "\n"
            << "staticDepthHierarchyCullingInactiveObserved=" << (
                m_SvsmMotionBenchmarkStaticHierarchyObservation
                    .inactiveObserved ? 1u : 0u) << "\n"
            << "staticDepthHierarchyCullingUnavailableObserved=" << (
                m_SvsmMotionBenchmarkStaticHierarchyObservation
                    .unavailableObserved ? 1u : 0u) << "\n"
            << "pairedStaticDynamicDepthRequestedObserved=" << (
                m_SvsmMotionBenchmarkPairedDepthObservation
                    .requestedObserved ? 1u : 0u) << "\n"
            << "pairedStaticDynamicDepthActiveObserved=" << (
                m_SvsmMotionBenchmarkPairedDepthObservation
                    .activeObserved ? 1u : 0u) << "\n"
            << "pairedStaticDynamicDepthInactiveObserved=" << (
                m_SvsmMotionBenchmarkPairedDepthObservation
                    .inactiveObserved ? 1u : 0u) << "\n"
            << "deferredStaticDepthMergeRequestedObserved=" << (
                m_SvsmMotionBenchmarkDeferredMergeObservation
                    .requestedObserved ? 1u : 0u) << "\n"
            << "deferredStaticDepthMergeActiveObserved=" << (
                m_SvsmMotionBenchmarkDeferredMergeObservation
                    .activeObserved ? 1u : 0u) << "\n"
            << "deferredStaticDepthMergeInactiveObserved=" << (
                m_SvsmMotionBenchmarkDeferredMergeObservation
                    .inactiveObserved ? 1u : 0u) << "\n"
            << "deferredStaticDepthMergeUnavailableObserved=" << (
                m_SvsmMotionBenchmarkDeferredMergeObservation
                    .unavailableObserved ? 1u : 0u) << "\n"
            << "batchedDrawSupported=" << (
                m_SvsmMotionBenchmarkBatchedSupported ? 1u : 0u) << "\n"
            << "batchedDrawActive=" << (
                m_SvsmMotionBenchmarkBatchedActive ? 1u : 0u) << "\n"
            << "packetStateSortingActive=" << (
                m_SvsmMotionBenchmarkPacketSortingActive ? 1u : 0u) << "\n"
            << "levelEmptyWorkSkipActive=" << (
                m_SvsmMotionBenchmarkLevelSkipActive ? 1u : 0u) << "\n"
            << "packetPageCullingActive=" << (
                m_SvsmMotionBenchmarkPacketCullingActive ? 1u : 0u) << "\n"
            << "hierarchicalScheduledPageMaskActive=" << (
                m_SvsmMotionBenchmarkHierarchyActive ? 1u : 0u) << "\n"
            << "hierarchicalScheduledPageMaskUnavailable=" << (
                m_SvsmMotionBenchmarkHierarchyUnavailable ? 1u : 0u) << "\n"
            << "receiverPageMaskCullingActive=" << (
                m_SvsmMotionBenchmarkReceiverMaskActive ? 1u : 0u) << "\n"
            << "receiverPageMaskCullingUnavailable=" << (
                m_SvsmMotionBenchmarkReceiverMaskUnavailable ? 1u : 0u)
                << "\n"
            << "dirtyPageScatterRasterActive=" << (
                m_SvsmMotionBenchmarkScatterRasterActive ? 1u : 0u) << "\n"
            << "packetPageCullingUnavailable=" << (
                m_SvsmMotionBenchmarkPacketCullingUnavailable ? 1u : 0u) << "\n"
            << "packetPageMemoryBytes=" << (
                m_SvsmMotionBenchmarkTimingPass
                    ? m_SvsmMotionBenchmarkTimingPass->GetTimings()
                        .packetPageMetadataBytes +
                        m_SvsmMotionBenchmarkTimingPass->GetTimings()
                            .packetPageListBytes
                    : 0u) << "\n"
            << "receiverPageMaskMemoryBytes=" << (
                m_SvsmMotionBenchmarkTimingPass
                    ? m_SvsmMotionBenchmarkTimingPass->GetTimings()
                        .receiverPageMaskBytes
                    : 0u) << "\n"
            << "status=" << m_SvsmMotionBenchmarkStatus << "\n";
        auto writeTimingSummary = [&result](
                                      const char* name,
                                      const SvsmMotionBenchmarkTimingSummary&
                                          summary) {
            result << name << "SampleCount=" << summary.sampleCount << "\n"
                << name << "MedianMs=" << summary.median << "\n"
                << name << "P95Ms=" << summary.p95 << "\n"
                << name << "P99Ms=" << summary.p99 << "\n"
                << name << "WorstMs=" << summary.maximum << "\n";
        };
        writeTimingSummary("marking", markingSummary);
        writeTimingSummary("allocation", allocationSummary);
        writeTimingSummary("clearing", clearingSummary);
        writeTimingSummary("packetGpu", packetGpuSummary);
        writeTimingSummary("rendering", renderingSummary);
        writeTimingSummary("filtering", filteringSummary);
        writeTimingSummary("sceneValidationCpu", sceneValidationCpuSummary);
        writeTimingSummary("clipmapUpdateCpu", clipmapUpdateCpuSummary);
        writeTimingSummary("packetCpu", packetCpuSummary);
        writeTimingSummary("cpu", cpuSummary);
        writeTimingSummary("baseline", baselineSummary);
        writeTimingSummary("forward", forwardSummary);
        writeTimingSummary("recovery", recoverySummary);
        writeTimingSummary("reverse", reverseSummary);
        writeTimingSummary("finalRecovery", finalRecoverySummary);
        if (m_SvsmMotionBenchmarkKind ==
            SvsmMotionBenchmarkKind::Camera)
        {
            // Preserve the legacy field names consumed by existing camera
            // motion-result analysis.
            writeTimingSummary("turnRight", forwardSummary);
            writeTimingSummary("holdRight", recoverySummary);
            writeTimingSummary("turnBack", reverseSummary);
        }
        result << "slowFrameCount=" << slowestTimings.size() << "\n";
        for (size_t index = 0u; index < slowestTimings.size(); ++index)
        {
            const SparseVirtualShadowMapGpuTiming& timing =
                slowestTimings[index];
            const SvsmMotionBenchmarkPhase phase =
                GetSvsmMotionBenchmarkPhase(
                    m_SvsmMotionBenchmarkKind,
                    timing.sourceTag);
            const float gpuStageSum = SumSvsmMotionBenchmarkGpuStages(
                timing.pageMarkingMilliseconds,
                timing.allocationMilliseconds,
                timing.clearingMilliseconds,
                timing.packetPageCullingMilliseconds,
                timing.pageRenderingMilliseconds,
                timing.filteringMilliseconds);
            const auto cpuTiming = std::find_if(
                m_SvsmMotionBenchmarkCpuTimings.begin(),
                m_SvsmMotionBenchmarkCpuTimings.end(),
                [&timing](const SvsmMotionBenchmarkCpuTiming& candidate) {
                    return candidate.sourceTag == timing.sourceTag;
                });
            const bool cpuTimingAvailable =
                cpuTiming != m_SvsmMotionBenchmarkCpuTimings.end();
            const SvsmMotionBenchmarkCpuTiming cpuTimingValue =
                cpuTimingAvailable
                    ? *cpuTiming
                    : SvsmMotionBenchmarkCpuTiming{};
            result << "slowFrame" << index << "Tag=" << timing.sourceTag << "\n"
                << "slowFrame" << index << "Segment="
                << GetSvsmMotionBenchmarkPhaseName(phase) << "\n"
                << "slowFrame" << index << "AngleDegrees="
                << GetSvsmMotionBenchmarkAngleDegrees(
                    m_SvsmMotionBenchmarkKind,
                    timing.sourceTag) << "\n"
                << "slowFrame" << index << "TotalMs="
                << timing.totalMilliseconds << "\n"
                << "slowFrame" << index << "MarkingMs="
                << timing.pageMarkingMilliseconds << "\n"
                << "slowFrame" << index << "AllocationMs="
                << timing.allocationMilliseconds << "\n"
                << "slowFrame" << index << "ClearingMs="
                << timing.clearingMilliseconds << "\n"
                << "slowFrame" << index << "PacketCullingMs="
                << timing.packetPageCullingMilliseconds << "\n"
                << "slowFrame" << index << "RenderingMs="
                << timing.pageRenderingMilliseconds << "\n"
                << "slowFrame" << index << "FilteringMs="
                << timing.filteringMilliseconds << "\n"
                << "slowFrame" << index << "GpuStageSumMs="
                << gpuStageSum << "\n"
                << "slowFrame" << index << "GpuUnattributedMs="
                << timing.totalMilliseconds - gpuStageSum << "\n"
                << "slowFrame" << index << "CpuTimingAvailable="
                << (cpuTimingAvailable ? 1u : 0u) << "\n"
                << "slowFrame" << index << "SceneValidationCpuMs="
                << cpuTimingValue.sceneValidationMilliseconds << "\n"
                << "slowFrame" << index << "ClipmapUpdateCpuMs="
                << cpuTimingValue.clipmapUpdateMilliseconds << "\n"
                << "slowFrame" << index << "PacketCullingCpuMs="
                << cpuTimingValue.packetCullingMilliseconds << "\n"
                << "slowFrame" << index << "TotalCpuMs="
                << cpuTimingValue.totalMilliseconds << "\n";
        }
        WriteSvsmMotionBenchmarkResultFile(result.str());
        log::info(
            "SVSM motion benchmark completed after %u motion frames and %u drain frames: %s",
            GetSvsmMotionBenchmarkEndFrame(
                m_SvsmMotionBenchmarkKind),
            m_SvsmMotionBenchmarkDrainFrames,
            m_SvsmMotionBenchmarkStatus.c_str());
        RestoreSvsmMotionBenchmarkState();
    }

    void UpdateSvsmMotionBenchmark()
    {
        m_SvsmMotionBenchmarkCurrentTimingTag = 0u;
        if (m_SvsmMotionBenchmarkAutostartPending &&
            !m_SvsmMotionBenchmarkActive)
        {
            if (!m_SvsmMotionMeasurementReadyPath.empty() &&
                !m_SvsmMotionMeasurementGateReady)
            {
                // A unique marker lets the external thermal monitor establish
                // its exact renderer identity before any benchmark stage runs.
                // Keep the measured producer off while the monitor qualifies.
                m_ui.SparseVirtualShadowMaps.enabled = false;
                std::ifstream marker(
                    m_SvsmMotionMeasurementReadyPath,
                    std::ios::binary);
                std::string markerContents{
                    std::istreambuf_iterator<char>(marker),
                    std::istreambuf_iterator<char>()};
                const SvsmMotionMeasurementMarker parsedMarker =
                    ParseSvsmMotionMeasurementMarker(markerContents);
                const uint64_t now = GetUnixMilliseconds();
                const bool readyForThisRenderer =
                    parsedMarker.state ==
                        SvsmMotionMeasurementMarkerState::Ready &&
                    parsedMarker.identityValid &&
                    parsedMarker.timingValid &&
                    parsedMarker.rendererProcessId ==
                        uint64_t(GetCurrentProcessId()) &&
                    IsCurrentExecutablePath(parsedMarker.rendererPath) &&
                    now >= parsedMarker.measurementStartUnixMilliseconds &&
                    now <= parsedMarker.measurementDeadlineUnixMilliseconds;
                if (readyForThisRenderer)
                {
                    m_SvsmMotionMeasurementReadyContents =
                        std::move(markerContents);
                    m_SvsmMotionMeasurementGateReady = true;
                    m_SvsmMotionMeasurementGateStatus = "ready";
                }
                else if (!markerContents.empty() &&
                    parsedMarker.state !=
                        SvsmMotionMeasurementMarkerState::Invalid)
                {
                    m_SvsmMotionBenchmarkAutostartPending = false;
                    m_SvsmMotionMeasurementGateStatus =
                        "marker did not identify this renderer and time window";
                    m_SvsmMotionBenchmarkStatus =
                        "Aborted: the external measurement marker was not valid for this renderer and time window.";
                    WriteSvsmMotionBenchmarkResultFile(
                        "state=aborted\nstatus=" +
                        m_SvsmMotionBenchmarkStatus + "\n");
                    log::warning(
                        "%s",
                        m_SvsmMotionBenchmarkStatus.c_str());
                    return;
                }
                if (!m_SvsmMotionMeasurementGateReady)
                {
                    if (!m_SvsmMotionMeasurementGateStageWritten)
                    {
                        WriteSvsmMotionAutostartStage(
                            "measurement-gate-wait");
                        m_SvsmMotionMeasurementGateStageWritten = true;
                    }
                    m_SvsmMotionBenchmarkStatus =
                        "Waiting for the external thermal measurement gate...";
                    return;
                }
                m_SvsmMotionBenchmarkStatus =
                    "External thermal gate ready; staging baseline frames...";
            }
            if (IsSceneLoaded() && !m_SponzaCameraLocationsAvailable)
            {
                m_SvsmMotionBenchmarkAutostartPending = false;
                m_SvsmMotionBenchmarkStatus =
                    "Aborted: the requested scene has no standardized Sponza camera.";
                WriteSvsmMotionBenchmarkResultFile(
                    "state=aborted\nstatus=" +
                    m_SvsmMotionBenchmarkStatus + "\n");
                log::warning("%s", m_SvsmMotionBenchmarkStatus.c_str());
            }
            else if (CanStageSvsmMotionBenchmark())
            {
                if (m_SvsmMotionAutostartStage ==
                    SvsmMotionAutostartStage::Baseline)
                {
                    // Staging is a locked harness state, not a UI suggestion.
                    // Keep SVSM disabled for every counted baseline render
                    // even if an input event attempted to toggle it.
                    m_ui.SparseVirtualShadowMaps.enabled = false;
                }
                if (!m_SvsmMotionAutostartStageWritten)
                {
                    WriteSvsmMotionAutostartStage("baseline-off");
                    m_SvsmMotionAutostartStageWritten = true;
                }

                const SparseVirtualShadowMapTimings& timings =
                    m_SparseVirtualShadowMapPass->GetTimings();
                const SvsmMotionAutostartDecision decision =
                    AdvanceSvsmMotionAutostart(
                        m_SvsmMotionAutostartStage,
                        m_SvsmMotionAutostartStageFrames,
                        m_SvsmMotionAutostartStableFrames,
                        timings.active,
                        timings.staticPageDrainActive);
                m_SvsmMotionAutostartStage = decision.stage;
                m_SvsmMotionAutostartStageFrames =
                    decision.stageFrames;
                m_SvsmMotionAutostartStableFrames =
                    decision.stableFrames;

                if (decision.timedOut)
                {
                    m_SvsmMotionBenchmarkAutostartPending = false;
                    m_SvsmMotionBenchmarkStatus =
                        "Aborted: staged SVSM warmup did not drain.";
                    WriteSvsmMotionBenchmarkResultFile(
                        "state=aborted\nstatus=" +
                        m_SvsmMotionBenchmarkStatus + "\n");
                    log::warning(
                        "%s",
                        m_SvsmMotionBenchmarkStatus.c_str());
                    return;
                }
                if (decision.enableSvsm)
                {
                    // Flush the durable stage marker before the first SVSM
                    // resource allocation or GPU command is recorded.
                    WriteSvsmMotionAutostartStage("svsm-warmup");
                    m_ui.SparseVirtualShadowMaps =
                        m_SvsmMotionAutostartTargetSettings;
                    m_SvsmMotionBenchmarkStatus =
                        "Warming SVSM until the static page drain is inactive...";
                }
                if (decision.startBenchmark)
                {
                    WriteSvsmMotionAutostartStage("measurement-ready");
                    m_SvsmMotionBenchmarkAutostartPending = false;
                    StartSvsmMotionBenchmark(
                        m_SvsmMotionBenchmarkKind);
                }
            }
        }
        if (!m_SvsmMotionBenchmarkActive)
            return;

        const SponzaCameraPreset& preset =
            GetDefaultSponzaCameraPreset();

        if (!IsSceneLoaded() ||
            m_Scene.get() != m_SvsmMotionBenchmarkStartScene)
        {
            AbortSvsmMotionBenchmark("the scene changed or became unavailable");
            return;
        }
        if (!m_ui.EnablePbr || !m_ui.UsesDeferredShading())
        {
            AbortSvsmMotionBenchmark("deferred PBR rendering was disabled");
            return;
        }
        if (m_ui.Camera != CameraMode::Static ||
            m_CameraVerticalFov != preset.VerticalFovDegrees)
        {
            AbortSvsmMotionBenchmark("the benchmark camera configuration changed");
            return;
        }
        if (!m_ui.SparseVirtualShadowMaps.enabled ||
            !IsSameSvsmConfiguration(
                m_ui.SparseVirtualShadowMaps,
                m_SvsmMotionBenchmarkStartSettings))
        {
            AbortSvsmMotionBenchmark("the SVSM configuration changed");
            return;
        }
        if (m_ui.EnableMiniEngineTaa !=
                m_SvsmMotionBenchmarkStartTaaEnabled ||
            m_ui.UsesMiniEngineTaa() !=
                m_SvsmMotionBenchmarkStartUsesTaa ||
            m_ui.EnableMiniEngineTaaSharpen !=
                m_SvsmMotionBenchmarkStartTaaSharpenEnabled ||
            m_ui.BendScreenSpaceShadows.enabled !=
                m_SvsmMotionBenchmarkStartBendEnabled ||
            m_ui.DiagnosticCascadedShadowMaps.enabled !=
                m_SvsmMotionBenchmarkStartCsmEnabled ||
            m_ui.ScreenSpaceVisibility.enabled !=
                m_SvsmMotionBenchmarkStartScreenSpaceVisibilityEnabled ||
            m_ui.WhiteWorld !=
                m_SvsmMotionBenchmarkStartWhiteWorld ||
            m_ui.ShowEnvironmentBackground !=
                m_SvsmMotionBenchmarkStartEnvironmentBackgroundEnabled)
        {
            AbortSvsmMotionBenchmark(
                "the locked benchmark isolation state changed");
            return;
        }
        if (!IsSvsmMotionBenchmarkLightStateExpected())
        {
            AbortSvsmMotionBenchmark(
                "the producing directional light or its node transform changed outside the benchmark");
            return;
        }
        if (!m_SparseVirtualShadowMapPass)
        {
            AbortSvsmMotionBenchmark("the SVSM pass became unavailable");
            return;
        }
        if (m_SvsmMotionBenchmarkTimingPass &&
            m_SvsmMotionBenchmarkTimingPass !=
                m_SparseVirtualShadowMapPass.get())
        {
            AbortSvsmMotionBenchmark("SVSM resources were recreated during measurement");
            return;
        }

        int width = 0;
        int height = 0;
        GetDeviceManager()->GetWindowDimensions(width, height);
        if (width != int(preset.ReferenceWidth) ||
            height != int(preset.ReferenceHeight))
        {
            if (m_SvsmMotionBenchmarkStarted)
            {
                AbortSvsmMotionBenchmark("the benchmark window size changed");
                return;
            }
            ++m_SvsmMotionBenchmarkPreparationFrames;
            if (m_SvsmMotionBenchmarkPreparationFrames >=
                SvsmMotionBenchmarkPreparationFrameLimit)
            {
                AbortSvsmMotionBenchmark("1920 x 1080 preparation timed out");
            }
            return;
        }
        m_SvsmMotionBenchmarkStarted = true;

        CollectCompletedSvsmMotionBenchmarkTimings();

        if (m_SvsmMotionBenchmarkFramePrepared)
        {
            if (IsSvsmMotionBenchmarkMeasurementFrame(
                    m_SvsmMotionBenchmarkKind,
                    m_SvsmMotionBenchmarkPreparedFrame))
            {
                const SparseVirtualShadowMapTimings& timings =
                    m_SparseVirtualShadowMapPass->GetTimings();
                if (timings.active)
                {
                    ObserveSvsmMotionBenchmarkPath(
                        m_SvsmMotionBenchmarkStaticHierarchyObservation,
                        timings.staticDepthHierarchyCullingRequested,
                        timings.staticDepthHierarchyCullingActive,
                        timings.staticDepthHierarchyCullingUnavailable);
                    ObserveSvsmMotionBenchmarkPath(
                        m_SvsmMotionBenchmarkPairedDepthObservation,
                        m_SvsmMotionBenchmarkStartSettings
                            .pairedStaticDynamicDepthEnabled,
                        timings.effectivePairedStaticDynamicDepth,
                        false);
                    ObserveSvsmMotionBenchmarkPath(
                        m_SvsmMotionBenchmarkDeferredMergeObservation,
                        timings.deferredStaticDepthMergeRequested,
                        timings.deferredStaticDepthMergeActive,
                        timings.deferredStaticDepthMergeUnavailable);
                    const bool batchedDrawRequested =
                        m_SvsmMotionBenchmarkStartSettings
                            .gpuGatedDrawSubmission &&
                        m_SvsmMotionBenchmarkStartSettings
                            .batchedDrawSubmissionEnabled;
                    const bool packetSortingRequested =
                        batchedDrawRequested &&
                        m_SvsmMotionBenchmarkStartSettings
                            .packetStateSortingEnabled;
                    const bool levelSkipRequested =
                        batchedDrawRequested &&
                        m_SvsmMotionBenchmarkStartSettings
                            .levelEmptyWorkSkipEnabled;
                    const bool packetCullingRequested =
                        m_SvsmMotionBenchmarkStartSettings
                            .gpuGatedDrawSubmission &&
                        m_SvsmMotionBenchmarkStartSettings
                            .packetPageCullingEnabled;
                    const bool scatterRasterRequested =
                        packetCullingRequested &&
                        m_SvsmMotionBenchmarkStartSettings
                            .dirtyPageScatterRasterEnabled;
                    const bool hierarchyRequested =
                        IsSvsmMotionBenchmarkHierarchyRequested(
                            packetCullingRequested,
                            m_SvsmMotionBenchmarkStartSettings
                                .hierarchicalScheduledPageMaskEnabled);
                    const bool packetCullingPathSatisfied =
                        IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
                            packetCullingRequested,
                            timings.packetPageCullingActive,
                            timings.packetPageCullingUnavailable,
                            timings.staticPageRequestReuseActive,
                            timings.staticPageDrainActive);
                    const bool hierarchyPathSatisfied =
                        IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
                            hierarchyRequested,
                            timings.hierarchicalScheduledPageMaskActive,
                            timings.
                                hierarchicalScheduledPageMaskUnavailable ||
                                timings.packetPageCullingUnavailable,
                            timings.staticPageRequestReuseActive,
                            timings.staticPageDrainActive);
                    const bool scatterPathSatisfied =
                        IsSvsmMotionBenchmarkPageMaintenancePathSatisfied(
                            scatterRasterRequested,
                            timings.dirtyPageScatterRasterActive,
                            timings.packetPageCullingUnavailable,
                            timings.staticPageRequestReuseActive,
                            timings.staticPageDrainActive);
                    const bool frameRequestedPathActive =
                        (!batchedDrawRequested ||
                            timings.batchedDrawActive) &&
                        (!packetSortingRequested ||
                            timings.packetStateSortingActive) &&
                        (!levelSkipRequested ||
                            timings.levelEmptyWorkSkipActive) &&
                        packetCullingPathSatisfied &&
                        hierarchyPathSatisfied &&
                        scatterPathSatisfied;
                    m_SvsmMotionBenchmarkRequestedPathInactive |=
                        !frameRequestedPathActive;
                    m_SvsmMotionBenchmarkBatchedSupported |=
                        timings.batchedDrawSupported;
                    m_SvsmMotionBenchmarkBatchedActive |=
                        timings.batchedDrawActive;
                    m_SvsmMotionBenchmarkPacketSortingActive |=
                        timings.packetStateSortingActive;
                    m_SvsmMotionBenchmarkLevelSkipActive |=
                        timings.levelEmptyWorkSkipActive;
                    m_SvsmMotionBenchmarkPacketCullingActive |=
                        timings.packetPageCullingActive;
                    m_SvsmMotionBenchmarkHierarchyActive |=
                        timings.hierarchicalScheduledPageMaskActive;
                    m_SvsmMotionBenchmarkHierarchyUnavailable |=
                        timings.
                            hierarchicalScheduledPageMaskUnavailable;
                    m_SvsmMotionBenchmarkReceiverMaskActive |=
                        timings.receiverPageMaskCullingActive;
                    m_SvsmMotionBenchmarkReceiverMaskUnavailable |=
                        timings.receiverPageMaskCullingUnavailable;
                    m_SvsmMotionBenchmarkScatterRasterActive |=
                        timings.dirtyPageScatterRasterActive;
                    m_SvsmMotionBenchmarkPacketCullingUnavailable |=
                        timings.packetPageCullingUnavailable;
                    if (!IsValidSvsmMotionBenchmarkCpuTiming(
                            timings.sceneValidationCpuMilliseconds,
                            timings.clipmapUpdateCpuMilliseconds,
                            timings.cullingCpuMilliseconds,
                            timings.totalCpuMilliseconds))
                    {
                        m_SvsmMotionBenchmarkInvalidCpuTiming = true;
                    }
                    else
                    {
                        m_SvsmMotionBenchmarkCpuTimings.push_back({
                            m_SvsmMotionBenchmarkPreparedFrame,
                            timings.sceneValidationCpuMilliseconds,
                            timings.clipmapUpdateCpuMilliseconds,
                            timings.cullingCpuMilliseconds,
                            timings.totalCpuMilliseconds
                        });
                        m_SvsmMotionBenchmarkSceneValidationCpuSamples.
                            push_back(
                                timings.sceneValidationCpuMilliseconds);
                        m_SvsmMotionBenchmarkClipmapUpdateCpuSamples.push_back(
                            timings.clipmapUpdateCpuMilliseconds);
                        m_SvsmMotionBenchmarkPacketCpuSamples.push_back(
                            timings.cullingCpuMilliseconds);
                        m_SvsmMotionBenchmarkCpuSamples.push_back(
                            timings.totalCpuMilliseconds);
                    }
                }
            }
            m_SvsmMotionBenchmarkFramePrepared = false;
        }

        if (m_SvsmMotionBenchmarkFrame >=
            GetSvsmMotionBenchmarkEndFrame(
                m_SvsmMotionBenchmarkKind))
        {
            m_SvsmMotionBenchmarkDraining = true;
            // UpdateSvsmMotionBenchmark clears this at entry already. Keep the
            // drain invariant explicit so a future control-flow refactor
            // cannot issue duplicate queries with the final source tag.
            m_SvsmMotionBenchmarkCurrentTimingTag = 0u;
            const SparseVirtualShadowMapTimingAccounting& accounting =
                m_SvsmMotionBenchmarkTimingPass->GetTimingAccounting();
            if (accounting.outstanding == 0u)
            {
                if (m_SvsmMotionMeasurementBenchmarkEndUnixMilliseconds ==
                    0u)
                {
                    m_SvsmMotionMeasurementBenchmarkEndUnixMilliseconds =
                        GetUnixMilliseconds();
                }
                if (PollSvsmMotionMeasurementCompletion())
                    FinishSvsmMotionBenchmark();
                return;
            }
            ++m_SvsmMotionBenchmarkDrainFrames;
            if (m_SvsmMotionBenchmarkDrainFrames >=
                SvsmMotionBenchmarkDrainFrameLimit)
            {
                AbortSvsmMotionBenchmark("GPU timing drain timed out");
                return;
            }
            m_StaticCamera.SetExactPose(
                preset.Position,
                preset.Direction,
                preset.Up,
                preset.Right);
            return;
        }

        if (m_SvsmMotionBenchmarkFrame ==
            GetSvsmMotionBenchmarkWarmFrameCount(
                m_SvsmMotionBenchmarkKind))
        {
            m_SvsmMotionBenchmarkTimingPass =
                m_SparseVirtualShadowMapPass.get();
            m_SvsmMotionBenchmarkTimingPass->ResetTimingAccounting();
        }

        const uint64_t sourceFrame = m_SvsmMotionBenchmarkFrame;
        const int32_t motionTenthDegreeTicks =
            GetSvsmMotionBenchmarkTenthDegreeTicks(
                m_SvsmMotionBenchmarkKind,
                sourceFrame);
        const float angleDegrees =
            float(motionTenthDegreeTicks) * 0.1f;
        if (m_SvsmMotionBenchmarkKind ==
            SvsmMotionBenchmarkKind::SunSlow)
        {
            m_StaticCamera.SetExactPose(
                preset.Position,
                preset.Direction,
                preset.Up,
                preset.Right);
            if (IsSvsmMotionBenchmarkDirectionUpdateFrame(
                    m_SvsmMotionBenchmarkKind,
                    sourceFrame))
            {
                const daffine3 sunTurn = rotation(
                    m_SvsmMotionBenchmarkSunRotationAxis,
                    radians(
                        double(motionTenthDegreeTicks) * 0.1));
                const double3 direction = normalize(
                    sunTurn.transformVector(
                        m_SvsmMotionBenchmarkStartLightDirection));
                // Use the original directional-light object and derive every
                // angle from an integer tenth-degree tick. There is no
                // cumulative integration or wall-clock dependency.
                m_SunLight->SetDirection(direction);
                m_SvsmMotionBenchmarkExpectedLightDirection = direction;
                m_SvsmMotionBenchmarkExpectedLightTranslation =
                    m_SvsmMotionBenchmarkStartLightNode->GetTranslation();
                m_SvsmMotionBenchmarkExpectedLightRotation =
                    m_SvsmMotionBenchmarkStartLightNode->GetRotation();
                m_SvsmMotionBenchmarkExpectedLightScaling =
                    m_SvsmMotionBenchmarkStartLightNode->GetScaling();
            }
        }
        else
        {
            const affine3 turn = rotation(
                preset.Up,
                -radians(angleDegrees));
            m_StaticCamera.SetExactPose(
                preset.Position,
                normalize(turn.transformVector(preset.Direction)),
                normalize(turn.transformVector(preset.Up)),
                normalize(turn.transformVector(preset.Right)));
        }
        m_SvsmMotionBenchmarkPreparedFrame = sourceFrame;
        if (IsSvsmMotionBenchmarkMeasurementFrame(
                m_SvsmMotionBenchmarkKind,
                sourceFrame))
        {
            m_SvsmMotionBenchmarkCurrentTimingTag = sourceFrame;
        }
        m_SvsmMotionBenchmarkFramePrepared = true;
        ++m_SvsmMotionBenchmarkFrame;
    }

    const std::vector<SceneCatalogEntry>& GetAvailableScenes() const
    {
        return m_SceneCatalog;
    }

    std::filesystem::path const& GetSceneDir() const
    {
        return m_SceneDir;
    }

    std::string GetCurrentSceneName() const
    {
        return m_CurrentSceneName;
    }

    std::string GetCurrentSceneDisplayName() const
    {
        if (const SceneCatalogEntry* entry = FindSceneCatalogEntry(m_SceneCatalog, m_CurrentSceneName))
            return entry->DisplayName;

        // Explicit command-line paths are allowed even when they are not in
        // the picker. Preserve the old in-tree relative-path presentation for
        // those scenes and show an external path verbatim.
        return MakeSceneDisplayName(m_SceneDir, m_CurrentSceneName);
    }

    void SetCurrentSceneName(const std::string& sceneName)
    {
        const SceneCatalogEntry* catalogEntry = FindSceneCatalogEntry(m_SceneCatalog, sceneName);
        const std::string resolvedSceneName = catalogEntry ? catalogEntry->FileName : sceneName;
        if (m_CurrentSceneName == resolvedSceneName)
            return;

		m_CurrentSceneName = resolvedSceneName;

		BeginLoadingScene(m_NativeFs, m_CurrentSceneName);
    }

    void ResetAllRendererSettings()
    {
        // Restore modes through their public setters first so material shader
        // permutations cannot retain state from the old setup.
        SetWhiteWorldMode(WhiteWorldMode::Off);

        m_ui.EnablePbr = true;
        m_ui.RenderMode = RendererMode::Deferred;
        m_ui.EnableMiniEngineTaa = true;
        m_ui.EnableMiniEngineTaaSharpen = true;
        m_ui.MiniEngineTaaSharpness = MiniEngineTaaDefaultSharpness;
        m_ui.BendScreenSpaceShadows =
            BendScreenSpaceShadowSettings{};
        m_ui.DiagnosticCascadedShadowMaps =
            DiagnosticCascadedShadowMapSettings{};
        m_ui.ScreenSpaceVisibility = ScreenSpaceVisibilitySettings{};
        m_ui.ShowEnvironmentBackground = true;
        m_ui.EnableDiffuseIbl = true;
        m_ui.DiffuseIblStrength = 1.f;
        m_ui.EnableSpecularIbl = true;
        m_ui.SpecularIblStrength = 1.f;
        m_ui.EnvironmentSource =
            ImageBasedLightingSource::Kloppenheim03Day;
        m_ui.EnvironmentExposureStops =
            GetImageBasedLightingSourceInfo(
                m_ui.EnvironmentSource).defaultExposureStops;
        m_ui.LightingDebugView = PbrLightingDebugView::None;

        // This also recreates any passes/resources that were absent because PBR,
        // deferred rendering, or white-world permutations had been disabled
        // before the reset.
        m_ui.ShaderReloadRequested = true;
        log::info("All renderer settings restored to factory defaults");
    }

    void SynchronizeCameraInput()
    {
        GLFWwindow* window = GetDeviceManager()->GetWindow();
        if (!window)
            return;

        const bool windowFocused = glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
        const bool imguiAvailable = ImGui::GetCurrentContext() != nullptr;
        const bool keyboardCaptured = imguiAvailable && ImGui::GetIO().WantCaptureKeyboard;
        const bool mouseCaptured = imguiAvailable && ImGui::GetIO().WantCaptureMouse;

        // Donut's cameras intentionally keep their own key/button latches, but
        // an ImGui popup or a native focus transition can consume the matching
        // release callback. Polling GLFW once per animated frame reconciles the
        // latches with physical state after focus returns. Inactive controllers
        // are explicitly released so switching modes cannot revive stale input.
        static constexpr int CameraKeys[] = {
            GLFW_KEY_Q,
            GLFW_KEY_E,
            GLFW_KEY_A,
            GLFW_KEY_D,
            GLFW_KEY_W,
            GLFW_KEY_S,
            GLFW_KEY_LEFT,
            GLFW_KEY_RIGHT,
            GLFW_KEY_UP,
            GLFW_KEY_DOWN,
            GLFW_KEY_Z,
            GLFW_KEY_C,
            GLFW_KEY_LEFT_SHIFT,
            GLFW_KEY_RIGHT_SHIFT,
            GLFW_KEY_LEFT_CONTROL,
            GLFW_KEY_RIGHT_CONTROL,
            GLFW_KEY_LEFT_ALT
        };

        const bool firstPersonActive = m_ui.Camera == CameraMode::FirstPerson;
        const bool thirdPersonActive = m_ui.Camera == CameraMode::ThirdPerson;
        const bool pivotActive = m_ui.Camera == CameraMode::Pivot;
        const bool allowKeyboard = windowFocused && !keyboardCaptured;
        for (int key : CameraKeys)
        {
            // GLFW polling is used only to clear stale latches. Synthesizing a
            // press here would turn a key held while closing UI into a new
            // camera action even though the camera never received its press.
            const bool physicallyPressed = allowKeyboard &&
                glfwGetKey(window, key) == GLFW_PRESS;
            if (!firstPersonActive || !physicallyPressed)
                m_FirstPersonCamera.KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
            if (!thirdPersonActive || !physicallyPressed)
                m_ThirdPersonCamera.KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
            if (!pivotActive || !physicallyPressed)
                m_PivotCamera.KeyboardUpdate(key, 0, GLFW_RELEASE, 0);
        }

        // ImGui consumes mouse-position callbacks while its windows are active.
        // Polling the current position into both cameras prevents the inactive
        // third-person camera from seeing one giant stale delta after a mode
        // switch. Match DeviceManager's display-scale conversion exactly.
        double cursorX = 0.0;
        double cursorY = 0.0;
        glfwGetCursorPos(window, &cursorX, &cursorY);
        if (!GetDeviceManager()->GetDeviceParams().supportExplicitDisplayScaling)
        {
            float dpiScaleX = 1.f;
            float dpiScaleY = 1.f;
            GetDeviceManager()->GetDPIScaleInfo(dpiScaleX, dpiScaleY);
            cursorX /= dpiScaleX;
            cursorY /= dpiScaleY;
        }
        m_FirstPersonCamera.MousePosUpdate(cursorX, cursorY);
        m_ThirdPersonCamera.MousePosUpdate(cursorX, cursorY);
        m_PivotCamera.MousePosUpdate(cursorX, cursorY);

        static constexpr int CameraMouseButtons[] = {
            GLFW_MOUSE_BUTTON_LEFT,
            GLFW_MOUSE_BUTTON_MIDDLE,
            GLFW_MOUSE_BUTTON_RIGHT
        };

        const bool allowMouse = windowFocused && !mouseCaptured;
        for (int button : CameraMouseButtons)
        {
            const bool physicallyPressed = allowMouse &&
                glfwGetMouseButton(window, button) == GLFW_PRESS;
            if (!firstPersonActive || !physicallyPressed)
                m_FirstPersonCamera.MouseButtonUpdate(button, GLFW_RELEASE, 0);
            if (!thirdPersonActive || !physicallyPressed)
                m_ThirdPersonCamera.MouseButtonUpdate(button, GLFW_RELEASE, 0);
            if (!pivotActive || !physicallyPressed)
                m_PivotCamera.MouseButtonUpdate(button, GLFW_RELEASE, 0);
        }
    }

    void BuildCameraCollisionWorld()
    {
        std::vector<CameraCollisionWorld::Triangle> triangles;
        const auto& instances = m_Scene->GetSceneGraph()->GetMeshInstances();

        size_t triangleCapacity = 0;
        for (const auto& instance : instances)
        {
            if (!instance)
                continue;

            std::shared_ptr<MeshInfo> mesh = instance->GetMesh();
            if (const auto skinnedInstance = std::dynamic_pointer_cast<SkinnedMeshInstance>(instance))
                mesh = skinnedInstance->GetPrototypeMesh();

            if (!mesh)
                continue;

            for (const auto& geometry : mesh->geometries)
            {
                if (geometry && geometry->type == MeshGeometryPrimitiveType::Triangles)
                    triangleCapacity += geometry->numIndices / 3;
            }
        }
        triangles.reserve(triangleCapacity);

        for (const auto& instance : instances)
        {
            if (!instance || !instance->GetNode())
                continue;

            std::shared_ptr<MeshInfo> mesh = instance->GetMesh();
            if (const auto skinnedInstance = std::dynamic_pointer_cast<SkinnedMeshInstance>(instance))
                mesh = skinnedInstance->GetPrototypeMesh();

            if (!mesh || !mesh->buffers || mesh->buffers->indexData.empty() ||
                mesh->buffers->positionData.empty())
            {
                continue;
            }

            const auto& indices = mesh->buffers->indexData;
            const auto& positions = mesh->buffers->positionData;
            const affine3 localToWorld = instance->GetNode()->GetLocalToWorldTransformFloat();

            for (const auto& geometry : mesh->geometries)
            {
                if (!geometry || geometry->type != MeshGeometryPrimitiveType::Triangles)
                    continue;

                const size_t firstIndex = size_t(mesh->indexOffset) + geometry->indexOffsetInMesh;
                const size_t firstVertex = size_t(mesh->vertexOffset) + geometry->vertexOffsetInMesh;
                if (firstIndex + geometry->numIndices > indices.size())
                {
                    log::warning("Skipping camera collision geometry with an invalid index range");
                    continue;
                }

                for (uint32_t index = 0; index + 2 < geometry->numIndices; index += 3)
                {
                    const size_t vertex0 = firstVertex + indices[firstIndex + index];
                    const size_t vertex1 = firstVertex + indices[firstIndex + index + 1];
                    const size_t vertex2 = firstVertex + indices[firstIndex + index + 2];
                    if (vertex0 >= positions.size() || vertex1 >= positions.size() ||
                        vertex2 >= positions.size())
                    {
                        continue;
                    }

                    triangles.push_back({
                        localToWorld.transformPoint(positions[vertex0]),
                        localToWorld.transformPoint(positions[vertex1]),
                        localToWorld.transformPoint(positions[vertex2])
                    });
                }
            }
        }

        const auto buildStart = std::chrono::high_resolution_clock::now();
        m_CameraCollisionWorld.Build(std::move(triangles));
        const auto buildDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - buildStart).count();
        log::info(
            "Camera collision: %zu triangles, %.3f-unit radius, built in %lld ms",
            m_CameraCollisionWorld.GetTriangleCount(),
            m_CameraCollisionRadius,
            static_cast<long long>(buildDuration));
    }

    virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
		if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		{
            m_ui.ShowUI = !m_ui.ShowUI;
            return true;	
		}

        GetActiveCamera().KeyboardUpdate(key, scancode, action, mods);
        return true;
    }

    virtual bool MousePosUpdate(double xpos, double ypos) override
    {
        // Keep all interactive controllers synchronized while inactive. A
        // later press can then begin from the current cursor position instead
        // of applying all motion accumulated since the last mode switch.
        m_FirstPersonCamera.MousePosUpdate(xpos, ypos);
        m_ThirdPersonCamera.MousePosUpdate(xpos, ypos);
        m_PivotCamera.MousePosUpdate(xpos, ypos);

        m_PickPosition = uint2(static_cast<uint>(xpos), static_cast<uint>(ypos));

        return true;
    }

    virtual bool MouseButtonUpdate(int button, int action, int mods) override
    {
        GetActiveCamera().MouseButtonUpdate(button, action, mods);
        
        if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_2)
            m_Pick = true;

        return true;
    }

    virtual bool MouseScrollUpdate(double xoffset, double yoffset) override
    {
        GetActiveCamera().MouseScrollUpdate(xoffset, yoffset);

        return true;
    }

    virtual void Animate(float fElapsedTimeSeconds) override
    {
        SynchronizeCameraInput();

        switch (m_ui.Camera)
        {
        case CameraMode::ThirdPerson:
        {
            // Freelook combines mouse/arrow look, W/S dolly, and A/D strafe.
            // It moves the eye directly with no orbit target or pivot state.
            const float3 start = m_ThirdPersonCamera.GetPosition();
            m_ThirdPersonCamera.Animate(fElapsedTimeSeconds);

            const float3 desiredPosition = m_ThirdPersonCamera.GetPosition();
            const float3 resolvedPosition = m_CameraCollisionWorld.MoveSphere(
                start, desiredPosition, m_CameraCollisionRadius);
            if (lengthSquared(resolvedPosition - desiredPosition) > 1e-12f)
            {
                // The correction becomes the free-look camera's next origin;
                // its look direction and dolly sensitivity stay unchanged.
                m_ThirdPersonCamera.ApplyCollisionPosition(resolvedPosition);
            }
            UpdateSponzaCameraLocationTracking();
            break;
        }

        case CameraMode::Pivot:
            m_PivotCamera.Animate(fElapsedTimeSeconds);
            break;

        case CameraMode::Static:
            break;

        case CameraMode::FirstPerson:
        {
            const float3 start = m_FirstPersonCamera.GetPosition();
            m_FirstPersonCamera.Animate(fElapsedTimeSeconds);

            const float3 desiredPosition = m_FirstPersonCamera.GetPosition();
            const float3 resolvedPosition = m_CameraCollisionWorld.MoveSphere(
                start, desiredPosition, m_CameraCollisionRadius);
            if (lengthSquared(resolvedPosition - desiredPosition) > 1e-12f)
            {
                m_FirstPersonCamera.LookTo(
                    resolvedPosition,
                    m_FirstPersonCamera.GetDir(),
                    m_FirstPersonCamera.GetUp());
            }
            break;
        }
        }
    }


    virtual void SceneUnloading() override
    {
        if (m_ForwardPass) m_ForwardPass->ResetBindingCache();
        if (m_DeferredLightingPass) m_DeferredLightingPass->ResetBindingCache();
        if (m_PbrDeferredLightingPass) m_PbrDeferredLightingPass->ResetBindingCache();
        if (m_ScreenSpaceVisibilityPass)
        {
            m_ScreenSpaceVisibilityPass->ResetBindingCache();
            m_ScreenSpaceVisibilityPass->ResetHistory();
        }
        if (m_MiniEngineTemporalAAPass)
            m_MiniEngineTemporalAAPass->ResetHistory();
        if (m_GBufferPass) m_GBufferPass->ResetBindingCache();
        if (m_DiagnosticCascadedShadowMapPass)
            m_DiagnosticCascadedShadowMapPass->ResetSceneState();
        m_BindingCache.Clear();
        m_SunLight.reset();
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_OriginalMaterials.clear();
        m_PreviousView.reset();
        m_CameraCollisionWorld.Clear();

    }

    virtual bool LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName) override
    {
        using namespace std::chrono;

        std::unique_ptr<engine::Scene> scene = std::make_unique<engine::Scene>(GetDevice(),
            *m_ShaderFactory, fs, m_TextureCache, nullptr, nullptr);

        auto startTime = high_resolution_clock::now();

        if (scene->Load(fileName))
        {
            m_Scene = std::move(scene);

            auto endTime = high_resolution_clock::now();
            auto duration = duration_cast<milliseconds>(endTime - startTime).count();
            log::info("Scene loading time: %llu ms", duration);

            return true;
        }
        
        return false;
    }
    
    virtual void SceneLoaded() override
    {
        Super::SceneLoaded();

        // Refresh transforms before extracting collision triangles. Donut frees
        // importer CPU arrays while FinishedLoading uploads mesh buffers, so the
        // first-party collision copy must be built between these two steps.
        m_Scene->RefreshSceneGraph(GetFrameIndex());
        const box3 loadedSceneBounds = m_Scene->GetSceneGraph()->GetRootNode()->GetGlobalBoundingBox();
        m_SceneDiagonal = std::max(length(loadedSceneBounds.diagonal()), 100.f);
        m_CameraCollisionRadius = std::max(0.1f, m_SceneDiagonal * 0.0005f);
        BuildCameraCollisionWorld();

        m_Scene->FinishedLoading(GetFrameIndex());

        m_OriginalMaterials.clear();
        for (const auto& material : m_Scene->GetSceneGraph()->GetMaterials())
            m_OriginalMaterials.emplace_back(material, *material);
        SetWhiteWorldMode(m_ui.WhiteWorld);

        for (auto light : m_Scene->GetSceneGraph()->GetLights())
        {
            if (light->GetLightType() == LightType_Directional)
            {
                m_SunLight = std::static_pointer_cast<DirectionalLight>(light);
                if (m_SunLight->irradiance <= 0.f)
                    m_SunLight->irradiance = 1.f;
                break;
            }
        }

        if (!m_SunLight)
        {
            m_SunLight = std::make_shared<DirectionalLight>();
            m_SunLight->angularSize = 0.53f;
            m_SunLight->irradiance = 1.f;

            auto node = std::make_shared<SceneGraphNode>();
            node->SetLeaf(m_SunLight);
            m_SunLight->SetDirection(dm::double3(0.1, -0.9, 0.1));
            m_SunLight->SetName("Sun");
            m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);
        }

        const SponzaCameraPreset* sceneDefaultCamera = FindStandardSponzaCameraPreset(
            *m_NativeFs,
            m_CurrentSceneName);
        m_SponzaCameraLocationsAvailable = sceneDefaultCamera != nullptr;
        if (m_SponzaCameraLocationsAvailable)
        {
            m_SponzaCameraLocation = ResolveSponzaCameraLocation(
                m_SponzaCameraLocation,
                m_BenchmarkCameraRequested);
        }
        const SponzaCameraPreset* sponzaCamera = m_SponzaCameraLocationsAvailable
            ? FindSponzaCameraPreset(m_SponzaCameraLocation)
            : nullptr;
        if (sponzaCamera)
            m_CameraVerticalFov = sponzaCamera->VerticalFovDegrees;

        std::shared_ptr<SceneGraphNode> cameraTarget = m_Scene->GetSceneGraph()->GetRootNode();
        // Prefer the compact asteroid core when present so the initial view
        // includes the full rocky platform instead of tightly framing only the
        // temple. Older Jungle Ruins exports retain the pyramid marker fallback.
        float cameraDistanceScale = 1.f;
        if (auto asteroid = FindDescendantByName(cameraTarget, "UVSR_AsteroidCore"))
        {
            cameraTarget = asteroid;
            cameraDistanceScale = 1.45f;
        }
        else if (auto pyramid = FindDescendantByName(cameraTarget, "Pyramid_EmitterShell"))
            cameraTarget = pyramid;
        PointThirdPersonCameraAt(cameraTarget, cameraDistanceScale, true);

        if (sponzaCamera)
        {
            ApplySponzaCameraPreset(*sponzaCamera);
            log::info(
                "Applied standardized camera location '%s' (%s) to '%s' at %u x %u and %.1f degrees vertical FOV",
                sponzaCamera->Label,
                sponzaCamera->Id,
                m_CurrentSceneName.c_str(),
                sponzaCamera->ReferenceWidth,
                sponzaCamera->ReferenceHeight,
                sponzaCamera->VerticalFovDegrees);
        }

        m_ui.Camera = CameraMode::ThirdPerson;

        if (!sponzaCamera)
        {
            const float3 initialPosition = m_ThirdPersonCamera.GetPosition();
            const float3 initialDirection = m_ThirdPersonCamera.GetDir();
            const float3 initialUp = m_ThirdPersonCamera.GetUp();
            m_FirstPersonCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_PivotCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_StaticCamera.LookTo(initialPosition, initialDirection, initialUp);
        }

        m_BenchmarkCameraActive = m_BenchmarkCameraRequested && sponzaCamera;
        if (m_BenchmarkCameraRequested)
        {
            if (m_BenchmarkCameraActive)
            {
                m_ui.Camera = CameraMode::Static;
                log::info(
                    "Benchmark camera '%s' is active in Locked mode",
                    sponzaCamera->Id);
            }
            else
            {
                log::warning(
                    "--benchmark-camera applies only to the two standardized PBR Sponza scenes; using the normal scene camera");
            }
        }

        if (m_DiagnosticCsmBenchmarkRequested)
        {
            const bool firstAcceptedScene =
                m_DiagnosticCsmBenchmarkScene == nullptr;
            const bool validScene =
                m_BenchmarkCameraActive &&
                (firstAcceptedScene ||
                    m_DiagnosticCsmBenchmarkScene == m_Scene.get()) &&
                m_SunLight;
            if (!validScene)
            {
                log::error(
                    "Diagnostic CSM benchmark aborted because the standardized scene, camera, or directional light changed");
                glfwSetWindowShouldClose(
                    GetDeviceManager()->GetWindow(),
                    GLFW_TRUE);
            }
            else if (firstAcceptedScene)
            {
                m_DiagnosticCsmBenchmarkScene = m_Scene.get();
                m_DiagnosticCsmBenchmarkLight = m_SunLight.get();
                m_DiagnosticCsmBenchmarkLightDirection =
                    m_SunLight->GetDirection();
            }
        }

    }

    void SetWhiteWorldMode(WhiteWorldMode mode)
    {
        const bool modeChanged = m_ui.WhiteWorld != mode;
        const bool shaderModeChanged = (m_ui.WhiteWorld == WhiteWorldMode::Off) !=
            (mode == WhiteWorldMode::Off);
        m_ui.WhiteWorld = mode;

        if (modeChanged && m_ScreenSpaceVisibilityPass)
            m_ScreenSpaceVisibilityPass->ResetHistory();
        if (modeChanged && m_MiniEngineTemporalAAPass)
            m_MiniEngineTemporalAAPass->ResetHistory();

        const bool enabled = mode != WhiteWorldMode::Off;
        const bool preserveDetailMaps = mode == WhiteWorldMode::PreserveDetail;
        const bool preserveLighting = mode == WhiteWorldMode::PreserveLighting;

        if (!m_Scene)
            return;

        for (auto& [material, original] : m_OriginalMaterials)
        {
            *material = original;

            if (enabled)
            {
                const bool originalUsesAlpha =
                    original.domain == MaterialDomain::AlphaTested ||
                    original.domain == MaterialDomain::AlphaBlended ||
                    original.domain == MaterialDomain::TransmissiveAlphaTested ||
                    original.domain == MaterialDomain::TransmissiveAlphaBlended;
                const bool hasSeparateOpacity = originalUsesAlpha &&
                    original.enableOpacityTexture && original.opacityTexture;
                const bool hasBaseAlpha = originalUsesAlpha && !hasSeparateOpacity &&
                    original.enableBaseOrDiffuseTexture && original.baseOrDiffuseTexture;

                // Preserve the coverage source but normalize all alpha domains
                // to depth-writing alpha test. WHITE_WORLD shader permutations
                // replace sampled RGB with white before material evaluation.
                material->domain = originalUsesAlpha
                    ? MaterialDomain::AlphaTested
                    : MaterialDomain::Opaque;
                material->useSpecularGlossModel = false;
                material->baseOrDiffuseColor = dm::float3(1.f);
                material->specularColor = dm::float3(0.04f);
                material->emissiveColor = preserveLighting
                    ? original.emissiveColor
                    : dm::float3(0.f);
                material->emissiveIntensity = preserveLighting
                    ? original.emissiveIntensity
                    : 1.f;
                material->metalness = 0.f;
                material->roughness = 0.72f;
                material->opacity = originalUsesAlpha ? original.opacity : 1.f;
                material->alphaCutoff = originalUsesAlpha
                    ? std::clamp(original.alphaCutoff, 0.01f, 0.99f)
                    : 0.5f;
                material->transmissionFactor = 0.f;
                material->enableBaseOrDiffuseTexture = hasBaseAlpha;
                material->enableMetalRoughOrSpecularTexture = false;
                material->enableEmissiveTexture =
                    preserveLighting && original.enableEmissiveTexture;
                material->enableTransmissionTexture = false;
                material->enableOpacityTexture = hasSeparateOpacity;
                material->enableNormalTexture = preserveDetailMaps && original.enableNormalTexture;
                material->enableOcclusionTexture =
                    preserveDetailMaps && original.enableOcclusionTexture;
                material->enableSubsurfaceScattering = false;
                material->enableHair = false;
            }

            ApplyPbrMaterialParameters(*material);
        }

        m_Scene->GetSceneGraph()->GetRootNode()->InvalidateContent();
        if (shaderModeChanged)
            m_ui.ShaderReloadRequested = true;
    }

    static std::shared_ptr<SceneGraphNode> FindDescendantByName(
        const std::shared_ptr<SceneGraphNode>& node,
        const std::string& name)
    {
        if (!node || node->GetName() == name)
            return node;

        for (size_t childIndex = 0; childIndex < node->GetNumChildren(); ++childIndex)
        {
            SceneGraphNode* child = node->GetChild(childIndex);
            if (!child)
                continue;

            if (auto found = FindDescendantByName(child->shared_from_this(), name))
                return found;
        }

        return nullptr;
    }

    void PointThirdPersonCameraAt(
        const std::shared_ptr<SceneGraphNode>& node,
        float distanceScale = 1.f,
        bool resetOrientation = false)
    {
        if (!node)
            return;

        dm::box3 bounds = node->GetGlobalBoundingBox();
        if (bounds.isempty()
            || !all(dm::isfinite(bounds.m_mins))
            || !all(dm::isfinite(bounds.m_maxs)))
            return;

        float radius = length(bounds.diagonal()) * 0.5f;
        float distance = radius * distanceScale / sinf(dm::radians(m_CameraVerticalFov * 0.5f));
        if (!std::isfinite(distance) || distance <= 0.f)
            return;

        if (resetOrientation)
        {
            // Reuse Donut's established orbit framing math only to calculate
            // the initial eye pose. Runtime Freelook is a free-moving camera
            // and retains no pivot or orbit state from this temporary object.
            ThirdPersonCamera framingCamera;
            framingCamera.SetRotation(dm::radians(135.f), dm::radians(20.f));
            framingCamera.SetTargetPosition(bounds.center());
            framingCamera.SetDistance(distance);
            framingCamera.Animate(0.f);
            m_ThirdPersonCamera.LookTo(
                framingCamera.GetPosition(),
                framingCamera.GetDir(),
                framingCamera.GetUp());
        }
        else
        {
            const float3 direction = m_ThirdPersonCamera.GetDir();
            const float3 up = m_ThirdPersonCamera.GetUp();
            m_ThirdPersonCamera.LookTo(
                bounds.center() - direction * distance,
                direction,
                up);
        }
        m_ThirdPersonCamera.ResetZoomReferenceDistance(distance);
    }

    std::shared_ptr<TextureCache> GetTextureCache()
    {
        return m_TextureCache;
    }

    std::shared_ptr<Scene> GetScene()
    {
        return m_Scene;
    }

    bool SetupView()
    {
        float2 renderTargetSize = float2(m_RenderTargets->GetSize());

        std::shared_ptr<PlanarView> planarView = std::dynamic_pointer_cast<PlanarView, IView>(m_View);

        float verticalFov = dm::radians(m_CameraVerticalFov);
        // Keep the near plane proportional to scene scale for stable depth.
        const float sceneScaleNear = std::max(0.1f, m_SceneDiagonal * 0.0005f);
        const dm::affine3 viewMatrix = GetActiveCamera().GetWorldToViewMatrix();

        bool topologyChanged = false;

        if (!planarView)
        {
            m_View = planarView = std::make_shared<PlanarView>();
            topologyChanged = true;
        }

        float4x4 projection = perspProjD3DStyleReverse(verticalFov, renderTargetSize.x / renderTargetSize.y, sceneScaleNear);

        planarView->SetViewport(nvrhi::Viewport(
            renderTargetSize.x,
            renderTargetSize.y));
        const MiniEngineTaaJitterSample jitter =
            m_ui.UsesMiniEngineTaa()
            ? GetMiniEngineTaaJitter(uint64_t(GetFrameIndex()))
            : MiniEngineTaaJitterSample{ 0.f, 0.f };
        planarView->SetPixelOffset(float2(jitter.x, jitter.y));

        planarView->SetMatrices(viewMatrix, projection);
        planarView->UpdateCache();

        return topologyChanged;
    }

    void CaptureCurrentViewForMotionVectors()
    {
        const auto currentView = std::dynamic_pointer_cast<PlanarView>(m_View);
        if (!currentView)
        {
            m_PreviousView.reset();
            return;
        }

        if (!m_PreviousView)
            m_PreviousView = std::make_shared<PlanarView>();
        m_PreviousView->SetViewport(currentView->GetViewport());
        m_PreviousView->SetPixelOffset(currentView->GetPixelOffset());
        m_PreviousView->SetMatrices(
            currentView->GetViewMatrix(),
            currentView->GetProjectionMatrix(false));
        m_PreviousView->UpdateCache();
    }

    void CreateRenderPasses()
    {
        m_MiniEngineTemporalAAPass.reset();

        ForwardShadingPass::CreateParameters ForwardParams;
        ForwardParams.trackLiveness = false;
        if (m_ui.EnablePbr)
            m_ForwardPass = std::make_shared<PbrForwardShadingPass>(
                GetDevice(), m_CommonPasses, m_ui.WhiteWorld != WhiteWorldMode::Off);
        else
            m_ForwardPass = std::make_shared<ForwardShadingPass>(GetDevice(), m_CommonPasses);
        m_ForwardPass->Init(*m_ShaderFactory, ForwardParams);
        
        GBufferFillPass::CreateParameters GBufferParams;
        GBufferParams.enableMotionVectors = m_RenderTargets->MotionVectorsEnabled;
        if (m_ui.EnablePbr)
            m_GBufferPass = std::make_shared<PbrGBufferFillPass>(
                GetDevice(), m_CommonPasses, m_ui.WhiteWorld != WhiteWorldMode::Off);
        else
            m_GBufferPass = std::make_shared<GBufferFillPass>(GetDevice(), m_CommonPasses);
        m_GBufferPass->Init(*m_ShaderFactory, GBufferParams);

        GBufferParams.enableMotionVectors = false;
        m_MaterialIDPass = std::make_unique<MaterialIDPass>(GetDevice(), m_CommonPasses);
        m_MaterialIDPass->Init(*m_ShaderFactory, GBufferParams);

        m_PixelReadbackPass = std::make_unique<PixelReadbackPass>(GetDevice(), m_ShaderFactory, m_RenderTargets->MaterialIDs, nvrhi::Format::RGBA32_UINT);

        if (m_ui.EnablePbr)
        {
            m_DeferredLightingPass.reset();
            m_PbrDeferredLightingPass = std::make_unique<PbrDeferredLightingPass>(
                GetDevice(), m_CommonPasses);
            m_PbrDeferredLightingPass->Init(m_ShaderFactory);
            m_BendScreenSpaceShadowPass =
                std::make_unique<BendScreenSpaceShadowPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_CommonPasses);
            m_SparseVirtualShadowMapPass =
                std::make_unique<SparseVirtualShadowMapPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_CommonPasses);
            if (!m_DiagnosticCascadedShadowMapPass)
            {
                m_DiagnosticCascadedShadowMapPass =
                    std::make_unique<DiagnosticCascadedShadowMapPass>(
                        GetDevice(),
                        m_ShaderFactory,
                        m_CommonPasses);
            }
            m_ScreenSpaceVisibilityPass = std::make_unique<ScreenSpaceVisibilityPass>(
                GetDevice(),
                m_ShaderFactory,
                m_CommonPasses,
                app::GetDirectoryWithExecutable().parent_path() /
                    "media/noise/visibility_filter_adapted_gauss1_ema035_r8.bin");
        }
        else
        {
            m_PbrDeferredLightingPass.reset();
            m_BendScreenSpaceShadowPass.reset();
            m_SparseVirtualShadowMapPass.reset();
            m_DiagnosticCascadedShadowMapPass.reset();
            m_ScreenSpaceVisibilityPass.reset();
            m_DeferredLightingPass = std::make_shared<DeferredLightingPass>(GetDevice(), m_CommonPasses);
            m_DeferredLightingPass->Init(m_ShaderFactory);
        }

        if (m_ui.UsesMiniEngineTaa())
        {
            m_MiniEngineTemporalAAPass =
                std::make_unique<MiniEngineTemporalAAPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_RenderTargets->HdrColor,
                    m_RenderTargets->Depth,
                    m_RenderTargets->MotionVectors);
        }

        m_ImageBasedLightingBackgroundPass =
            m_ImageBasedLightingEnvironment
                ? std::make_unique<ImageBasedLightingBackgroundPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_CommonPasses,
                    m_RenderTargets->ForwardFramebuffer,
                    *m_View,
                    m_ImageBasedLightingEnvironment->
                        GetRadianceTextureResource())
                : nullptr;
        
        m_AgxToneMappingPass = std::make_unique<AgxToneMappingPass>(
            GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer);

    }

    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override
    {
        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->open();
#ifdef _WIN32
        NameD3d12CommandList(
            m_CommandList,
            L"UVSR Splash Command List");
        SetD3d12DredMarker(m_CommandList, L"UVSR Splash Start");
#endif
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
    }

    static bool IsSameDiagnosticCsmBenchmarkWork(
        const DiagnosticCsmStats& left,
        const DiagnosticCsmStats& right)
    {
        return HasSameDiagnosticCsmBenchmarkWorkIdentity(left, right);
    }

    [[nodiscard]] std::filesystem::path
        GetDiagnosticCsmBenchmarkArtifactPath(
            const char* extension) const
    {
        return app::GetDirectoryWithExecutable()
            .parent_path()
            .parent_path() /
            "outputs" /
            (std::string("diagnostic-csm-benchmark-latest.") +
                extension);
    }

    [[nodiscard]] bool WriteBenchmarkArtifactAtomically(
        const std::filesystem::path& targetPath,
        const std::string& contents) const
    {
        std::error_code directoryError;
        std::filesystem::create_directories(
            targetPath.parent_path(),
            directoryError);
        if (directoryError)
        {
            log::error(
                "Benchmark artifact writer could not create '%s' (%s)",
                targetPath.parent_path().string().c_str(),
                directoryError.message().c_str());
            return false;
        }

        std::filesystem::path temporaryPath = targetPath;
        temporaryPath += "." +
            std::to_string(GetCurrentProcessId()) + ".tmp";
        {
            std::ofstream output(
                temporaryPath,
                std::ios::binary | std::ios::trunc);
            if (!output)
            {
                log::error(
                    "Benchmark artifact writer could not open temporary artifact '%s'",
                    temporaryPath.string().c_str());
                return false;
            }
            output.write(
                contents.data(),
                std::streamsize(contents.size()));
            output.flush();
            if (!output.good())
            {
                log::error(
                    "Benchmark artifact writer could not finish temporary artifact '%s'",
                    temporaryPath.string().c_str());
                output.close();
                std::error_code removeError;
                std::filesystem::remove(
                    temporaryPath,
                    removeError);
                return false;
            }
        }

        if (!MoveFileExW(
                temporaryPath.c_str(),
                targetPath.c_str(),
                MOVEFILE_REPLACE_EXISTING |
                    MOVEFILE_WRITE_THROUGH))
        {
            log::error(
                "Benchmark artifact writer could not publish '%s' (Windows error %lu)",
                targetPath.string().c_str(),
                GetLastError());
            std::error_code removeError;
            std::filesystem::remove(temporaryPath, removeError);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool WriteDiagnosticCsmBenchmarkState(
        const char* state,
        const char* reason) const
    {
        std::ostringstream result;
        result << "state=" << state << "\n"
            << "runId=" << m_DiagnosticCsmRecordRunId << "\n"
            << "official=0\n"
            << "rawAuthoritative=0\n"
            << "evidenceValid=0\n"
            << "normalizationAdvisory=1\n"
            << "executableSha256="
            << (m_DiagnosticCsmExecutableSha256.empty()
                    ? "unavailable"
                    : m_DiagnosticCsmExecutableSha256) << "\n"
            << "timingConfigurationId="
            << (m_DiagnosticCsmTimingConfigurationId.empty()
                    ? "unavailable"
                    : m_DiagnosticCsmTimingConfigurationId) << "\n"
            << "timingConfiguration="
            << (m_DiagnosticCsmTimingConfiguration.empty()
                    ? "unavailable"
                    : m_DiagnosticCsmTimingConfiguration) << "\n";
        if (reason && reason[0] != '\0')
            result << "reason=" << reason << "\n";
        return WriteBenchmarkArtifactAtomically(
            GetDiagnosticCsmBenchmarkArtifactPath("txt"),
            result.str());
    }

    [[nodiscard]] bool StartDiagnosticCsmBenchmarkRecording()
    {
        if (m_DiagnosticCsmRecordStarted)
            return true;

        m_DiagnosticCsmRecordStarted = true;
        if (m_DiagnosticCsmExecutableSha256.size() != 64u ||
            m_DiagnosticCsmTimingConfiguration.empty() ||
            m_DiagnosticCsmTimingConfigurationId.size() != 16u)
        {
            log::error(
                "Diagnostic CSM benchmark could not establish immutable executable and timing-configuration identity");
            WriteDiagnosticCsmBenchmarkFailure(
                "immutable executable or timing-configuration identity was unavailable");
            return false;
        }
        m_DiagnosticCsmRecordStartTime =
            std::chrono::steady_clock::now();
        if (WriteDiagnosticCsmBenchmarkState("running", nullptr))
            return true;

        log::error(
            "Diagnostic CSM benchmark could not publish its running state");
        return false;
    }

    void WriteDiagnosticCsmBenchmarkFailure(const char* reason) const
    {
        if (!WriteDiagnosticCsmBenchmarkState("aborted", reason))
        {
            log::error(
                "Diagnostic CSM benchmark could not publish its aborted state: %s",
                reason);
        }
    }

    [[nodiscard]] bool FinishDiagnosticCsmBenchmarkRecording()
    {
        std::vector<float> rawGpuSamples;
        std::vector<float> normalizedGpuSamples;
        std::vector<float> workIndexSamples;
        std::vector<float> clockCapacitySamples;
        std::vector<float> utilizedTFlopsSamples;
        std::vector<float> utilizationPercentSamples;
        std::vector<float> memoryBandwidthSamples;
        std::vector<float> telemetryAgeMillisecondsSamples;
        std::vector<float> totalCpuSamples;
        std::vector<float> setupCpuSamples;
        std::vector<float> cullingCpuSamples;
        std::vector<float> recordingCpuSamples;
        std::vector<float> timingAgeSamples;
        std::vector<float> cullingGpuSamples;
        std::vector<float> clearUpdateSamples;
        std::vector<float> rasterSamples;
        std::vector<float> samplingSamples;
        const size_t sampleCount =
            m_DiagnosticCsmBenchmarkSamples.size();
        if (sampleCount == 0u)
        {
            WriteDiagnosticCsmBenchmarkFailure(
                "no GPU timing samples were recorded");
            return false;
        }
        const bool detailedGpuTimings =
            m_DiagnosticCsmBenchmarkSamples.front()
                .timings.detailedGpuTimingEnabled;
        const char* rendererName =
            GetDeviceManager()->GetRendererString();
        const std::string_view rendererNameView =
            rendererName ? std::string_view(rendererName) :
                std::string_view();
        const GpuTimingNormalizationCalibration* calibration =
            FindGpuTimingNormalizationCalibration(rendererNameView);
        for (std::vector<float>* samples : {
                &rawGpuSamples,
                &normalizedGpuSamples,
                &workIndexSamples,
                &clockCapacitySamples,
                &utilizedTFlopsSamples,
                &utilizationPercentSamples,
                &memoryBandwidthSamples,
                &telemetryAgeMillisecondsSamples,
                &totalCpuSamples,
                &setupCpuSamples,
                &cullingCpuSamples,
                &recordingCpuSamples,
                &timingAgeSamples,
                &cullingGpuSamples,
                &clearUpdateSamples,
                &rasterSamples,
                &samplingSamples })
        {
            samples->reserve(sampleCount);
        }

        std::array<uint32_t, 5u> gradeCounts{};
        size_t utilizationValidCount = 0u;
        uint64_t minimumTelemetryGeneration =
            std::numeric_limits<uint64_t>::max();
        uint64_t maximumTelemetryGeneration = 0u;
        for (const DiagnosticCsmBenchmarkSample& sample :
            m_DiagnosticCsmBenchmarkSamples)
        {
            rawGpuSamples.push_back(sample.timings.totalMilliseconds);
            if (sample.normalized.valid)
            {
                normalizedGpuSamples.push_back(float(
                    sample.normalized.estimatedMilliseconds));
                workIndexSamples.push_back(float(
                    sample.normalized.workIndexMillisecondsTFlops));
                clockCapacitySamples.push_back(float(
                    sample.normalized.currentClockCapacityTFlops));
                if (sample.normalized.utilizationValid)
                {
                    utilizedTFlopsSamples.push_back(float(
                        sample.normalized.utilizedTFlops));
                    utilizationPercentSamples.push_back(float(
                        sample.gpuMetrics.gpuUtilization * 100.0));
                    ++utilizationValidCount;
                }
                memoryBandwidthSamples.push_back(float(
                    sample.gpuMetrics.memoryBandwidthGBps));
                telemetryAgeMillisecondsSamples.push_back(float(
                    sample.gpuMetrics.telemetryAgeMilliseconds));
                minimumTelemetryGeneration = std::min(
                    minimumTelemetryGeneration,
                    sample.gpuMetrics.telemetryGeneration);
                maximumTelemetryGeneration = std::max(
                    maximumTelemetryGeneration,
                    sample.gpuMetrics.telemetryGeneration);

                const size_t gradeIndex =
                    size_t(sample.normalized.grade);
                if (gradeIndex < gradeCounts.size())
                    ++gradeCounts[gradeIndex];
            }
            totalCpuSamples.push_back(
                sample.timings.totalCpuMilliseconds);
            setupCpuSamples.push_back(
                sample.timings.setupCpuMilliseconds);
            cullingCpuSamples.push_back(
                sample.timings.cullingCpuMilliseconds);
            recordingCpuSamples.push_back(
                sample.timings.recordingCpuMilliseconds);
            timingAgeSamples.push_back(float(
                sample.timings.gpuTimingAgeFrames));
            if (detailedGpuTimings)
            {
                cullingGpuSamples.push_back(
                    sample.timings.cullingGpuMilliseconds);
                clearUpdateSamples.push_back(
                    sample.timings.clearUpdateMilliseconds);
                rasterSamples.push_back(
                    sample.timings.rasterMilliseconds);
                samplingSamples.push_back(
                    sample.timings.samplingMilliseconds);
            }
        }

        const SvsmMotionBenchmarkTimingSummary rawGpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(rawGpuSamples);
        SvsmMotionBenchmarkTimingSummary normalizedGpuSummary{};
        SvsmMotionBenchmarkTimingSummary workIndexSummary{};
        SvsmMotionBenchmarkTimingSummary clockCapacitySummary{};
        SvsmMotionBenchmarkTimingSummary utilizedTFlopsSummary{};
        SvsmMotionBenchmarkTimingSummary utilizationSummary{};
        SvsmMotionBenchmarkTimingSummary memoryBandwidthSummary{};
        SvsmMotionBenchmarkTimingSummary telemetryAgeMillisecondsSummary{};
        if (!normalizedGpuSamples.empty())
        {
            normalizedGpuSummary =
                SummarizeSvsmMotionBenchmarkSamples(
                    normalizedGpuSamples);
            workIndexSummary =
                SummarizeSvsmMotionBenchmarkSamples(
                    workIndexSamples);
            clockCapacitySummary =
                SummarizeSvsmMotionBenchmarkSamples(
                    clockCapacitySamples);
            if (!utilizedTFlopsSamples.empty())
            {
                utilizedTFlopsSummary =
                    SummarizeSvsmMotionBenchmarkSamples(
                        utilizedTFlopsSamples);
                utilizationSummary =
                    SummarizeSvsmMotionBenchmarkSamples(
                        utilizationPercentSamples);
            }
            memoryBandwidthSummary =
                SummarizeSvsmMotionBenchmarkSamples(
                    memoryBandwidthSamples);
            telemetryAgeMillisecondsSummary =
                SummarizeSvsmMotionBenchmarkSamples(
                    telemetryAgeMillisecondsSamples);
        }
        const GpuTimingNormalizationRunGrade normalizationRunGrade =
            SummarizeGpuTimingNormalizationRunGrade(
                sampleCount,
                gradeCounts);
        const bool normalizedSummaryPublishable =
            normalizationRunGrade.validFraction >= 0.95;
        const SvsmMotionBenchmarkTimingSummary totalCpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(totalCpuSamples);
        const SvsmMotionBenchmarkTimingSummary setupCpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(setupCpuSamples);
        const SvsmMotionBenchmarkTimingSummary cullingCpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(cullingCpuSamples);
        const SvsmMotionBenchmarkTimingSummary recordingCpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(recordingCpuSamples);
        const SvsmMotionBenchmarkTimingSummary timingAgeSummary =
            SummarizeSvsmMotionBenchmarkSamples(timingAgeSamples);
        SvsmMotionBenchmarkTimingSummary cullingGpuSummary{};
        SvsmMotionBenchmarkTimingSummary clearUpdateSummary{};
        SvsmMotionBenchmarkTimingSummary rasterSummary{};
        SvsmMotionBenchmarkTimingSummary samplingSummary{};
        if (detailedGpuTimings)
        {
            cullingGpuSummary =
                SummarizeSvsmMotionBenchmarkSamples(
                    cullingGpuSamples);
            clearUpdateSummary =
                SummarizeSvsmMotionBenchmarkSamples(
                    clearUpdateSamples);
            rasterSummary =
                SummarizeSvsmMotionBenchmarkSamples(rasterSamples);
            samplingSummary =
                SummarizeSvsmMotionBenchmarkSamples(samplingSamples);
        }

        const DiagnosticCsmBenchmarkSample& first =
            m_DiagnosticCsmBenchmarkSamples.front();
        const DiagnosticCsmBenchmarkSample& last =
            m_DiagnosticCsmBenchmarkSamples.back();
        const DiagnosticCsmStats& stats = first.stats;
        const bool expectedDetailedGpuTimings =
            m_DiagnosticCsmBenchmarkSettings.detailedGpuTimingEnabled;
        bool timingsValid = true;
        bool detailedGpuTimingModeUniform = true;
        bool workIdentityStable = true;
        bool issuedFrameContextsValid = true;
        uint64_t missingSourceFrames = 0u;
        uint64_t sourceFrameGapEvents = 0u;
        uint64_t maximumSourceFrameGap = 0u;
        const size_t missingIssuedFrameContexts = size_t(std::count_if(
            m_DiagnosticCsmBenchmarkSamples.begin(),
            m_DiagnosticCsmBenchmarkSamples.end(),
            [](const DiagnosticCsmBenchmarkSample& sample) {
                return !sample.issuedFrameContextAvailable;
            }));
        for (const DiagnosticCsmBenchmarkSample& sample :
            m_DiagnosticCsmBenchmarkSamples)
        {
            timingsValid &=
                IsValidDiagnosticCsmBenchmarkTiming(sample.timings);
            detailedGpuTimingModeUniform &=
                sample.timings.detailedGpuTimingEnabled ==
                    detailedGpuTimings;
            workIdentityStable &=
                IsSameDiagnosticCsmBenchmarkWork(stats, sample.stats);
            issuedFrameContextsValid &=
                IsValidDiagnosticCsmBenchmarkIssuedFrameContext(
                    sample.issuedFrameContextAvailable,
                    sample.frameIntervalMilliseconds);
        }
        bool sourceFrameArithmeticValid = true;
        bool sourceFramesStrictlyConsecutive = true;
        for (size_t index = 1u; index < sampleCount; ++index)
        {
            const uint64_t previousSourceFrame =
                m_DiagnosticCsmBenchmarkSamples[index - 1u].sourceFrame;
            const uint64_t currentSourceFrame =
                m_DiagnosticCsmBenchmarkSamples[index].sourceFrame;
            if (!AreDiagnosticCsmBenchmarkSourceFramesConsecutive(
                    previousSourceFrame,
                    currentSourceFrame))
            {
                sourceFramesStrictlyConsecutive = false;
            }
            if (currentSourceFrame <= previousSourceFrame)
            {
                sourceFrameArithmeticValid = false;
                ++sourceFrameGapEvents;
                continue;
            }
            const uint64_t gap =
                currentSourceFrame - previousSourceFrame;
            maximumSourceFrameGap =
                std::max(maximumSourceFrameGap, gap);
            if (gap > 1u)
            {
                ++sourceFrameGapEvents;
                missingSourceFrames += gap - 1u;
            }
        }
        const std::string expectedTimingConfiguration =
            BuildDiagnosticCsmTimingConfigurationIdentity(
                m_DiagnosticCsmBenchmarkSettings);
        const std::string expectedTimingConfigurationId =
            BuildDiagnosticCsmTimingConfigurationId(
                m_DiagnosticCsmBenchmarkSettings);
        DiagnosticCsmBenchmarkEvidence benchmarkEvidence;
        benchmarkEvidence.expectedSampleCount =
            DiagnosticCsmBenchmarkMeasurementFrames;
        benchmarkEvidence.sampleCount = sampleCount;
        benchmarkEvidence.missingIssuedFrameContexts =
            missingIssuedFrameContexts;
        benchmarkEvidence.sourceFrameGapEvents = sourceFrameGapEvents;
        benchmarkEvidence.missingSourceFrames = missingSourceFrames;
        benchmarkEvidence.maximumSourceFrameGap = maximumSourceFrameGap;
        benchmarkEvidence.executableIdentityValid =
            m_DiagnosticCsmExecutableSha256.size() == 64u;
        benchmarkEvidence.timingConfigurationIdentityValid =
            m_DiagnosticCsmTimingConfiguration ==
                expectedTimingConfiguration &&
            m_DiagnosticCsmTimingConfigurationId ==
                expectedTimingConfigurationId;
        benchmarkEvidence.timingsValid = timingsValid;
        benchmarkEvidence.detailedGpuTimingModeUniform =
            detailedGpuTimingModeUniform;
        benchmarkEvidence.detailedGpuTimingModeMatchesConfiguration =
            detailedGpuTimings == expectedDetailedGpuTimings;
        benchmarkEvidence.workIdentityStable = workIdentityStable;
        benchmarkEvidence.sourceFrameArithmeticValid =
            sourceFrameArithmeticValid;
        benchmarkEvidence.sourceFramesStrictlyConsecutive =
            sourceFramesStrictlyConsecutive;
        benchmarkEvidence.issuedFrameContextsValid =
            issuedFrameContextsValid;
        const bool rawAuthoritative =
            IsDiagnosticCsmBenchmarkRawAuthoritative(
                "complete",
                benchmarkEvidence);
        if (!rawAuthoritative)
        {
            WriteDiagnosticCsmBenchmarkFailure(
                "raw timing evidence was incomplete or internally inconsistent");
            return false;
        }

        std::ostringstream result;
        result << std::fixed << std::setprecision(6)
            << "state=complete\n"
            << "runId=" << m_DiagnosticCsmRecordRunId << "\n"
            << "official=0\n"
            << "rawAuthoritative=" << (rawAuthoritative ? 1u : 0u)
            << "\n"
            << "evidenceValid=" << (rawAuthoritative ? 1u : 0u)
            << "\n"
            << "normalizationAdvisory=1\n"
            << "sameAdapterOnly=1\n"
            << "normalizationVersion=calibrated-issue-frame-clock-capacity-v2\n"
            << "normalizationFormula=rawGpuMs*currentClockCapacityTFlops/42.5\n"
            << "normalizationCalibrationId="
            << (calibration ? calibration->id : "unavailable") << "\n"
            << "normalizationCalibrationAdapter="
            << (calibration
                    ? calibration->exactAdapterName
                    : std::string_view("unavailable")) << "\n"
            << "normalizationReferenceClockCapacityTFlops="
            << (calibration
                    ? calibration->referenceClockCapacityTFlops
                    : 0.0) << "\n"
            << "normalizationReferenceMemoryBandwidthGBps="
            << (calibration
                    ? calibration->referenceMemoryBandwidthGBps
                    : 0.0) << "\n"
            << "normalizationCoveragePercent="
            << normalizationRunGrade.validFraction * 100.0 << "\n"
            << "normalizationRunGrade="
            << GetGpuTimingNormalizationGradeLabel(
                normalizationRunGrade.grade) << "\n"
            << "renderer=" << rendererNameView
            << "\n"
            << "scene=" << m_CurrentSceneName << "\n"
            << "commit=" << UVSR_GIT_COMMIT << "\n"
            << "executableSha256="
            << m_DiagnosticCsmExecutableSha256 << "\n"
            << "timingConfigurationId="
            << m_DiagnosticCsmTimingConfigurationId << "\n"
            << "timingConfiguration="
            << m_DiagnosticCsmTimingConfiguration << "\n"
            << "profile="
            << uint32_t(m_DiagnosticCsmBenchmarkSettings.profile)
            << "\n"
            << "width=" << stats.outputWidth << "\n"
            << "height=" << stats.outputHeight << "\n"
            << "sampleCount=" << sampleCount << "\n"
            << "normalizedSampleCount="
            << normalizedGpuSamples.size() << "\n"
            << "warmupSourceFrames="
            << DiagnosticCsmBenchmarkWarmupFrames << "\n"
            << "warmupMinimumSeconds="
            << DiagnosticCsmBenchmarkWarmupSeconds << "\n"
            << "warmupElapsedMilliseconds="
            << m_DiagnosticCsmWarmupElapsedMilliseconds << "\n"
            << "warmupInitialTelemetryGeneration="
            << m_DiagnosticCsmWarmupInitialTelemetryGeneration << "\n"
            << "measurementStartTelemetryGeneration="
            << m_DiagnosticCsmMeasurementStartTelemetryGeneration
            << "\n"
            << "firstSourceFrame=" << first.sourceFrame << "\n"
            << "lastSourceFrame=" << last.sourceFrame << "\n"
            << "sourceFrameGapEvents=" << sourceFrameGapEvents
            << "\n"
            << "missingSourceFrames=" << missingSourceFrames << "\n"
            << "maximumSourceFrameGap=" << maximumSourceFrameGap
            << "\n"
            << "missingIssuedFrameContexts="
            << missingIssuedFrameContexts << "\n"
            << "gpuTimingAgeMedianFrames=" << timingAgeSummary.median
            << "\n"
            << "gpuTimingAgeMaximumFrames=" << timingAgeSummary.maximum
            << "\n"
            << "gpuTelemetrySemantics=issue-frame cached approximately 500 ms and paired by GPU timing source frame\n"
            << "gpuTimingMode="
            << (detailedGpuTimings
                    ? "detailed"
                    : "totalOnly") << "\n"
            << "stageTimingsAvailable="
            << (detailedGpuTimings ? 1u : 0u) << "\n"
            << "rawGpuMedianMs=" << rawGpuSummary.median << "\n"
            << "rawGpuP95Ms=" << rawGpuSummary.p95 << "\n"
            << "rawGpuP99Ms=" << rawGpuSummary.p99 << "\n"
            << "rawGpuWorstMs=" << rawGpuSummary.maximum << "\n";
        if (!normalizedGpuSamples.empty())
        {
            result
                << "normalizedValidSubsetMedianMs="
                << normalizedGpuSummary.median << "\n"
                << "normalizedValidSubsetP95Ms="
                << normalizedGpuSummary.p95 << "\n"
                << "normalizedValidSubsetP99Ms="
                << normalizedGpuSummary.p99 << "\n"
                << "telemetryAgeMedianMilliseconds="
                << telemetryAgeMillisecondsSummary.median << "\n"
                << "telemetryAgeMaximumMilliseconds="
                << telemetryAgeMillisecondsSummary.maximum << "\n"
                << "telemetryGenerationMinimum="
                << minimumTelemetryGeneration << "\n"
                << "telemetryGenerationMaximum="
                << maximumTelemetryGeneration << "\n";
        }
        else
        {
            result
                << "normalizedValidSubsetMedianMs=unavailable\n"
                << "normalizedValidSubsetP95Ms=unavailable\n"
                << "normalizedValidSubsetP99Ms=unavailable\n"
                << "telemetryAgeMedianMilliseconds=unavailable\n"
                << "telemetryAgeMaximumMilliseconds=unavailable\n"
                << "telemetryGenerationMinimum=unavailable\n"
                << "telemetryGenerationMaximum=unavailable\n";
        }
        if (normalizedSummaryPublishable)
        {
            result
                << "normalizedGpuMedianMs="
                << normalizedGpuSummary.median << "\n"
                << "normalizedGpuP95Ms="
                << normalizedGpuSummary.p95 << "\n"
                << "normalizedGpuP99Ms="
                << normalizedGpuSummary.p99 << "\n"
                << "normalizedGpuWorstMs="
                << normalizedGpuSummary.maximum << "\n"
                << "workIndexMedianMsTFlops="
                << workIndexSummary.median << "\n"
                << "clockCapacityMedianTFlops="
                << clockCapacitySummary.median << "\n"
                << "clockCapacityMinimumTFlops="
                << *std::min_element(
                    clockCapacitySamples.begin(),
                    clockCapacitySamples.end()) << "\n"
                << "clockCapacityMaximumTFlops="
                << clockCapacitySummary.maximum << "\n"
                << "memoryBandwidthMedianGBps="
                << memoryBandwidthSummary.median << "\n"
                << "memoryBandwidthMinimumGBps="
                << *std::min_element(
                    memoryBandwidthSamples.begin(),
                    memoryBandwidthSamples.end()) << "\n"
                << "memoryBandwidthMaximumGBps="
                << memoryBandwidthSummary.maximum << "\n";
        }
        else
        {
            result
                << "normalizedGpuMedianMs=unavailable\n"
                << "normalizedGpuP95Ms=unavailable\n"
                << "normalizedGpuP99Ms=unavailable\n"
                << "normalizedGpuWorstMs=unavailable\n"
                << "workIndexMedianMsTFlops=unavailable\n"
                << "clockCapacityMedianTFlops=unavailable\n"
                << "clockCapacityMinimumTFlops=unavailable\n"
                << "clockCapacityMaximumTFlops=unavailable\n"
                << "memoryBandwidthMedianGBps=unavailable\n"
                << "memoryBandwidthMinimumGBps=unavailable\n"
                << "memoryBandwidthMaximumGBps=unavailable\n";
        }
        result
            << "gpuUtilizationValidCount="
            << utilizationValidCount << "\n";
        if (!utilizationPercentSamples.empty())
        {
            result
                << "utilizedMedianTFlops="
                << utilizedTFlopsSummary.median << "\n"
                << "gpuUtilizationMedianPercent="
                << utilizationSummary.median << "\n";
        }
        else
        {
            result
                << "utilizedMedianTFlops=unavailable\n"
                << "gpuUtilizationMedianPercent=unavailable\n";
        }
        result
            << "gradeACount="
            << gradeCounts[size_t(GpuTimingNormalizationGrade::A)]
            << "\n"
            << "gradeBCount="
            << gradeCounts[size_t(GpuTimingNormalizationGrade::B)]
            << "\n"
            << "gradeCCount="
            << gradeCounts[size_t(GpuTimingNormalizationGrade::C)]
            << "\n"
            << "directionalCount="
            << gradeCounts[
                size_t(GpuTimingNormalizationGrade::Directional)]
            << "\n"
            << "totalCpuMedianMs=" << totalCpuSummary.median << "\n"
            << "totalCpuP95Ms=" << totalCpuSummary.p95 << "\n"
            << "setupCpuMedianMs=" << setupCpuSummary.median << "\n"
            << "cullingCpuMedianMs=" << cullingCpuSummary.median
            << "\n"
            << "recordingCpuMedianMs=" << recordingCpuSummary.median
            << "\n";
        if (detailedGpuTimings)
        {
            result
                << "cullingGpuMedianMs=" << cullingGpuSummary.median
                << "\n"
                << "clearUpdateMedianMs="
                << clearUpdateSummary.median << "\n"
                << "rasterMedianMs=" << rasterSummary.median << "\n"
                << "samplingMedianMs=" << samplingSummary.median
                << "\n";
        }
        else
        {
            result
                << "cullingGpuMedianMs=unavailable\n"
                << "clearUpdateMedianMs=unavailable\n"
                << "rasterMedianMs=unavailable\n"
                << "samplingMedianMs=unavailable\n";
        }
        result
            << "cascadeCount=" << stats.cascadeCount << "\n"
            << "shadowMapResolution=" << stats.shadowMapResolution
            << "\n"
            << "depthBitsPerTexel=" << stats.depthBitsPerTexel << "\n"
            << "filterSampleCount=" << stats.filterSampleCount << "\n"
            << "filterComparisonCount=" << stats.filterComparisonCount
            << "\n"
            << "coarseCasterProjectionPairs="
            << stats.coarseCasterProjectionPairs << "\n"
            << "radiusCulledCasterProjectionPairs="
            << stats.radiusCulledCasterProjectionPairs << "\n"
            << "accuratelyCulledCasterProjectionPairs="
            << stats.accuratelyCulledCasterProjectionPairs << "\n"
            << "candidateCasterProjectionPairs="
            << stats.candidateCasterProjectionPairs << "\n"
            << "renderedCasterProjectionPairs="
            << stats.renderedCasterProjectionPairs << "\n"
            << "manualCasterProjectionPairs="
            << stats.manualCasterProjectionPairs << "\n"
            << "inputAssemblerCasterProjectionPairs="
            << stats.inputAssemblerCasterProjectionPairs << "\n"
            << "submissionStatsAvailable="
            << (stats.submissionStatsAvailable ? 1u : 0u) << "\n";
        if (stats.submissionStatsAvailable)
        {
            result
                << "submittedDrawCalls=" << stats.submittedDrawCalls
                << "\n"
                << "submittedTriangles=" << stats.submittedTriangles
                << "\n"
                << "submittedTranslationOnlyDrawCalls="
                << stats.submittedTranslationOnlyDrawCalls << "\n"
                << "submittedTranslationOnlyTriangles="
                << stats.submittedTranslationOnlyTriangles << "\n";
        }
        else
        {
            result
                << "submittedDrawCalls=unavailable\n"
                << "submittedTriangles=unavailable\n"
                << "submittedTranslationOnlyDrawCalls=unavailable\n"
                << "submittedTranslationOnlyTriangles=unavailable\n";
        }
        result
            << "translationOnlyCasterTransformRequested="
            << (stats.translationOnlyCasterTransformRequested ? 1u : 0u)
            << "\n"
            << "translationOnlyCasterTransformEnabled="
            << (stats.translationOnlyCasterTransformEnabled ? 1u : 0u)
            << "\n"
            << "inputAssemblerCasterFetchRequested="
            << (stats.inputAssemblerCasterFetchRequested ? 1u : 0u)
            << "\n"
            << "inputAssemblerCasterFetchEnabled="
            << (stats.inputAssemblerCasterFetchEnabled ? 1u : 0u)
            << "\n"
            << "opaqueDepthStateMergingEnabled="
            << (stats.opaqueDepthStateMergingEnabled ? 1u : 0u)
            << "\n"
            << "positionOnlyOpaqueEnabled="
            << (stats.positionOnlyOpaqueEnabled ? 1u : 0u) << "\n"
            << "cachedShadowDrawListsRequested="
            << (stats.cachedShadowDrawListsRequested ? 1u : 0u)
            << "\n"
            << "cachedShadowDrawListsActive="
            << (stats.cachedShadowDrawListsActive ? 1u : 0u)
            << "\n"
            << "cachedShadowDrawListHits="
            << stats.cachedShadowDrawListHits << "\n"
            << "cachedShadowDrawListMisses="
            << stats.cachedShadowDrawListMisses << "\n"
            << "cachedShadowDrawListEntries="
            << stats.cachedShadowDrawListEntries << "\n"
            << "cachedShadowDrawListCasterProjectionPairs="
            << stats.cachedShadowDrawListCasterProjectionPairs << "\n"
            << "batchedFullRedrawClearActive="
            << (stats.batchedFullRedrawClearActive ? 1u : 0u)
            << "\n"
            << "receiverRasterScissorEnabled="
            << (stats.receiverRasterScissorEnabled ? 1u : 0u)
            << "\n"
            << "wholeMapReuseEnabled="
            << (m_DiagnosticCsmBenchmarkSettings.wholeMapReuseEnabled
                    ? 1u
                    : 0u) << "\n"
            << "wholeCascadeReuseEnabled="
            << (m_DiagnosticCsmBenchmarkSettings
                    .wholeCascadeReuseEnabled ? 1u : 0u) << "\n"
            << "dirtyRectanglesEnabled="
            << (m_DiagnosticCsmBenchmarkSettings
                    .dirtyRectanglesEnabled ? 1u : 0u) << "\n"
            << "scrollingEnabled="
            << (m_DiagnosticCsmBenchmarkSettings.scrollingEnabled
                    ? 1u
                    : 0u) << "\n"
            << "receiverRasterScissoredCascades="
            << stats.receiverRasterScissoredCascades << "\n"
            << "logicalTexels=" << stats.logicalTexels << "\n"
            << "updatedTexels=" << stats.updatedTexels << "\n"
            << "clearedTexels=" << stats.clearedTexels << "\n";

        std::ostringstream samples;
        samples << "runId,executableSha256,timingConfigurationId,"
            "timingConfiguration,sourceFrame,sourceFrameGap,gpuTimingAgeFrames,"
            "issuedFrameContextAvailable,applicationFrame,backBufferIndex,"
            "frameIntervalMs,rawGpuMs,normalizationValid,normalizedGpuMs,"
            "clockCapacityTFlops,utilizedTFlops,gpuUtilizationValid,"
            "gpuUtilizationPercent,memoryBandwidthGBps,memoryBandwidthRatio,"
            "telemetryGeneration,telemetryAgeMs,grade,totalCpuMs,setupCpuMs,cullingCpuMs,"
            "recordingCpuMs,stageTimingsAvailable,cullingGpuMs,"
            "clearUpdateMs,rasterMs,samplingMs\n";
        samples << std::fixed << std::setprecision(6);
        for (size_t index = 0u; index < sampleCount; ++index)
        {
            const DiagnosticCsmBenchmarkSample& sample =
                m_DiagnosticCsmBenchmarkSamples[index];
            const uint64_t sourceFrameGap = index == 0u
                ? 0u
                : sample.sourceFrame -
                    m_DiagnosticCsmBenchmarkSamples[index - 1u]
                        .sourceFrame;
            samples << m_DiagnosticCsmRecordRunId << ","
                << m_DiagnosticCsmExecutableSha256 << ","
                << m_DiagnosticCsmTimingConfigurationId << ","
                << m_DiagnosticCsmTimingConfiguration << ","
                << sample.sourceFrame << ","
                << sourceFrameGap << ","
                << sample.timings.gpuTimingAgeFrames << ","
                << (sample.issuedFrameContextAvailable ? 1u : 0u)
                << ",";
            if (sample.issuedFrameContextAvailable)
            {
                samples << sample.applicationFrame << ","
                    << sample.backBufferIndex << ","
                    << sample.frameIntervalMilliseconds << ",";
            }
            else
            {
                samples << ",,,";
            }
            samples
                << sample.timings.totalMilliseconds << ","
                << (sample.normalized.valid ? 1u : 0u) << ",";
            if (sample.normalized.valid)
                samples << sample.normalized.estimatedMilliseconds;
            samples << ",";
            if (sample.gpuMetrics.valid &&
                std::isfinite(sample.gpuMetrics.gpuGFlops) &&
                sample.gpuMetrics.gpuGFlops > 0.0)
            {
                samples << sample.gpuMetrics.gpuGFlops / 1000.0;
            }
            samples << ",";
            if (sample.normalized.valid &&
                sample.normalized.utilizationValid)
            {
                samples << sample.normalized.utilizedTFlops;
            }
            samples << ","
                << (sample.gpuMetrics.gpuUtilizationValid ? 1u : 0u)
                << ",";
            if (sample.gpuMetrics.gpuUtilizationValid &&
                std::isfinite(sample.gpuMetrics.gpuUtilization))
            {
                samples << sample.gpuMetrics.gpuUtilization * 100.0;
            }
            samples << ",";
            if (std::isfinite(sample.gpuMetrics.memoryBandwidthGBps) &&
                sample.gpuMetrics.memoryBandwidthGBps > 0.0)
            {
                samples << sample.gpuMetrics.memoryBandwidthGBps;
            }
            samples << ",";
            if (sample.normalized.valid)
                samples << sample.normalized.memoryBandwidthRatio;
            samples << ","
                << sample.gpuMetrics.telemetryGeneration << ",";
            if (std::isfinite(
                    sample.gpuMetrics.telemetryAgeMilliseconds) &&
                sample.gpuMetrics.telemetryAgeMilliseconds >= 0.0)
            {
                samples
                    << sample.gpuMetrics.telemetryAgeMilliseconds;
            }
            samples << ","
                << GetGpuTimingNormalizationGradeLabel(
                    sample.normalized.grade) << ",";
            samples << sample.timings.totalCpuMilliseconds << ","
                << sample.timings.setupCpuMilliseconds << ","
                << sample.timings.cullingCpuMilliseconds << ","
                << sample.timings.recordingCpuMilliseconds << ","
                << (detailedGpuTimings ? 1u : 0u) << ",";
            if (detailedGpuTimings)
            {
                samples
                    << sample.timings.cullingGpuMilliseconds << ","
                    << sample.timings.clearUpdateMilliseconds << ","
                    << sample.timings.rasterMilliseconds << ","
                    << sample.timings.samplingMilliseconds << "\n";
            }
            else
            {
                samples << ",,,\n";
            }
        }

        const std::filesystem::path csvPath =
            GetDiagnosticCsmBenchmarkArtifactPath("csv");
        const std::filesystem::path textPath =
            GetDiagnosticCsmBenchmarkArtifactPath("txt");
        if (!WriteBenchmarkArtifactAtomically(
                csvPath,
                samples.str()))
        {
            WriteDiagnosticCsmBenchmarkFailure(
                "could not publish the buffered CSV artifact");
            return false;
        }
        if (!WriteBenchmarkArtifactAtomically(
                textPath,
                result.str()))
        {
            WriteDiagnosticCsmBenchmarkFailure(
                "could not publish the buffered text artifact");
            return false;
        }

        if (normalizedSummaryPublishable)
        {
            log::info(
                "Diagnostic CSM benchmark recorded %zu unique source frames: raw median %.3f ms, advisory normalized median %.3f ms at %.1f TFLOPS, p95 %.3f ms, p99 %.3f ms, coverage %.1f%%, grade %s",
                sampleCount,
                rawGpuSummary.median,
                normalizedGpuSummary.median,
                GpuTimingNormalizationReferenceTFlops,
                normalizedGpuSummary.p95,
                normalizedGpuSummary.p99,
                normalizationRunGrade.validFraction * 100.0,
                GetGpuTimingNormalizationGradeLabel(
                    normalizationRunGrade.grade));
        }
        else
        {
            log::info(
                "Diagnostic CSM benchmark recorded %zu unique source frames: raw median %.3f ms; advisory normalization coverage %.1f%% was insufficient for a run summary",
                sampleCount,
                rawGpuSummary.median,
                normalizationRunGrade.validFraction * 100.0);
        }
        return true;
    }

    void UpdateDiagnosticCsmBenchmarkRecording()
    {
        if (!m_DiagnosticCsmRecordRequested ||
            m_DiagnosticCsmRecordFinished ||
            !m_DiagnosticCsmBenchmarkStateArmed ||
            !m_DiagnosticCascadedShadowMapPass)
        {
            return;
        }

        if (m_DiagnosticCsmRecordStarted &&
            std::chrono::steady_clock::now() -
                m_DiagnosticCsmRecordStartTime >
                std::chrono::seconds(90))
        {
            m_DiagnosticCsmRecordFinished = true;
            WriteDiagnosticCsmBenchmarkFailure(
                "benchmark recording timed out after 90 seconds");
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return;
        }

        const DiagnosticCsmTimings& timings =
            m_DiagnosticCascadedShadowMapPass->GetTimings();
        if (!timings.active)
        {
            return;
        }

        const auto submissionTime = std::chrono::steady_clock::now();
        const char* rendererName =
            GetDeviceManager()->GetRendererString();
        const GpuPerformanceMetrics issueGpuMetrics =
            QueryGpuPerformanceMetrics(rendererName);
        const float frameIntervalMilliseconds =
            m_DiagnosticCsmLastSubmissionTimeValid
                ? std::chrono::duration<float, std::milli>(
                    submissionTime -
                    m_DiagnosticCsmLastSubmissionTime).count()
                : 0.f;
        m_DiagnosticCsmLastSubmissionTime = submissionTime;
        m_DiagnosticCsmLastSubmissionTimeValid = true;
        if (timings.gpuTimingSource ==
                DiagnosticCsmGpuTimingSource::TimerQuery &&
            timings.gpuTimingAgeFrames <=
                std::numeric_limits<uint64_t>::max() -
                    timings.gpuTimingSourceFrame)
        {
            const uint64_t issuedSourceFrame =
                timings.gpuTimingSourceFrame +
                timings.gpuTimingAgeFrames;
            DiagnosticCsmIssuedFrameContext context;
            context.sourceFrame = issuedSourceFrame;
            context.applicationFrame = uint64_t(GetFrameIndex());
            context.backBufferIndex =
                GetDeviceManager()->GetCurrentBackBufferIndex();
            context.frameIntervalMilliseconds =
                frameIntervalMilliseconds;
            context.gpuMetrics = issueGpuMetrics;
            if (m_DiagnosticCsmIssuedFrameContexts.empty() ||
                m_DiagnosticCsmIssuedFrameContexts.back().sourceFrame <
                    issuedSourceFrame)
            {
                m_DiagnosticCsmIssuedFrameContexts.push_back(context);
            }
            else if (
                m_DiagnosticCsmIssuedFrameContexts.back().sourceFrame ==
                    issuedSourceFrame)
            {
                m_DiagnosticCsmIssuedFrameContexts.back() = context;
            }
            while (m_DiagnosticCsmIssuedFrameContexts.size() > 64u)
                m_DiagnosticCsmIssuedFrameContexts.pop_front();
        }

        const DiagnosticCsmTimingFrameOrder sourceFrameOrder =
            ClassifyDiagnosticCsmTimingSourceFrame(
                m_DiagnosticCsmLastSourceFrame,
                m_DiagnosticCsmLastSourceFrameValid,
                timings);
        if (sourceFrameOrder ==
                DiagnosticCsmTimingFrameOrder::Unavailable ||
            sourceFrameOrder ==
                DiagnosticCsmTimingFrameOrder::Duplicate)
        {
            return;
        }
        if (sourceFrameOrder ==
            DiagnosticCsmTimingFrameOrder::OutOfOrder)
        {
            m_DiagnosticCsmRecordFinished = true;
            WriteDiagnosticCsmBenchmarkFailure(
                "GPU timing source frames moved backward");
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return;
        }
        if (!IsValidDiagnosticCsmBenchmarkTiming(timings))
        {
            m_DiagnosticCsmRecordFinished = true;
            WriteDiagnosticCsmBenchmarkFailure(
                "GPU or CPU benchmark timing was non-finite, non-positive, or unavailable");
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return;
        }
        m_DiagnosticCsmLastSourceFrame =
            timings.gpuTimingSourceFrame;
        m_DiagnosticCsmLastSourceFrameValid = true;

        if (!m_DiagnosticCsmRecordWarmupComplete)
        {
            if (m_DiagnosticCsmRecordWarmupFrames <
                DiagnosticCsmBenchmarkWarmupFrames)
            {
                ++m_DiagnosticCsmRecordWarmupFrames;
            }

            m_DiagnosticCsmWarmupElapsedMilliseconds =
                std::chrono::duration<double, std::milli>(
                    submissionTime -
                    m_DiagnosticCsmRecordStartTime).count();
            if (m_DiagnosticCsmWarmupInitialTelemetryGeneration == 0u &&
                issueGpuMetrics.telemetryGeneration != 0u)
            {
                m_DiagnosticCsmWarmupInitialTelemetryGeneration =
                    issueGpuMetrics.telemetryGeneration;
            }

            const bool minimumFramesElapsed =
                m_DiagnosticCsmRecordWarmupFrames >=
                    DiagnosticCsmBenchmarkWarmupFrames;
            const bool minimumWallTimeElapsed =
                m_DiagnosticCsmWarmupElapsedMilliseconds >=
                    DiagnosticCsmBenchmarkWarmupSeconds * 1000.0;
            const bool calibratedAdapter =
                FindGpuTimingNormalizationCalibration(
                    rendererName ? rendererName : "") !=
                    nullptr;
            const bool telemetryGenerationAdvanced =
                m_DiagnosticCsmWarmupInitialTelemetryGeneration != 0u &&
                issueGpuMetrics.telemetryGeneration >=
                    m_DiagnosticCsmWarmupInitialTelemetryGeneration &&
                issueGpuMetrics.telemetryGeneration -
                    m_DiagnosticCsmWarmupInitialTelemetryGeneration >=
                    DiagnosticCsmBenchmarkFreshTelemetryGenerations;
            const bool freshTelemetry =
                issueGpuMetrics.valid &&
                issueGpuMetrics.gpuUtilizationValid &&
                std::isfinite(
                    issueGpuMetrics.telemetryAgeMilliseconds) &&
                issueGpuMetrics.telemetryAgeMilliseconds >= 0.0 &&
                issueGpuMetrics.telemetryAgeMilliseconds <=
                    DiagnosticCsmBenchmarkFreshTelemetryAgeMilliseconds &&
                telemetryGenerationAdvanced;
            if (!minimumFramesElapsed ||
                !minimumWallTimeElapsed ||
                (calibratedAdapter && !freshTelemetry))
            {
                return;
            }

            m_DiagnosticCsmRecordWarmupComplete = true;
            m_DiagnosticCsmMeasurementStartTelemetryGeneration =
                issueGpuMetrics.telemetryGeneration;
            return;
        }

        DiagnosticCsmBenchmarkSample sample;
        sample.sourceFrame = timings.gpuTimingSourceFrame;
        sample.timings = timings;
        const auto issuedContext = std::find_if(
            m_DiagnosticCsmIssuedFrameContexts.rbegin(),
            m_DiagnosticCsmIssuedFrameContexts.rend(),
            [&sample](const DiagnosticCsmIssuedFrameContext& context) {
                return context.sourceFrame == sample.sourceFrame;
            });
        if (issuedContext !=
            m_DiagnosticCsmIssuedFrameContexts.rend())
        {
            sample.applicationFrame = issuedContext->applicationFrame;
            sample.backBufferIndex = issuedContext->backBufferIndex;
            sample.frameIntervalMilliseconds =
                issuedContext->frameIntervalMilliseconds;
            sample.gpuMetrics = issuedContext->gpuMetrics;
            sample.issuedFrameContextAvailable = true;
        }
        sample.stats =
            m_DiagnosticCascadedShadowMapPass->GetStats();
        sample.normalized = NormalizeGpuTimingMilliseconds(
            sample.timings.totalMilliseconds,
            sample.gpuMetrics,
            rendererName ? rendererName : "");
        if (!m_DiagnosticCsmBenchmarkSamples.empty() &&
            !IsSameDiagnosticCsmBenchmarkWork(
                m_DiagnosticCsmBenchmarkSamples.front().stats,
                sample.stats))
        {
            m_DiagnosticCsmRecordFinished = true;
            WriteDiagnosticCsmBenchmarkFailure(
                "CSM work identity changed during measurement");
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return;
        }

        m_DiagnosticCsmBenchmarkSamples.push_back(std::move(sample));
        if (m_DiagnosticCsmBenchmarkSamples.size() <
            DiagnosticCsmBenchmarkMeasurementFrames)
        {
            return;
        }

        const bool artifactsPublished =
            FinishDiagnosticCsmBenchmarkRecording();
        m_DiagnosticCsmRecordFinished = true;
        if (!artifactsPublished)
        {
            log::error(
                "Diagnostic CSM benchmark completed sampling but could not publish a complete result");
        }
        glfwSetWindowShouldClose(
            GetDeviceManager()->GetWindow(),
            GLFW_TRUE);
    }

    bool ValidateDiagnosticCsmBenchmarkState(
        int windowWidth,
        int windowHeight,
        bool shadowStateDirty)
    {
        if (!m_DiagnosticCsmBenchmarkRequested)
            return true;

        const SponzaCameraPreset& preset =
            GetDefaultSponzaCameraPreset();
        bool valid =
            m_DiagnosticCsmBenchmarkScene == m_Scene.get() &&
            m_DiagnosticCsmBenchmarkLight == m_SunLight.get() &&
            m_BenchmarkCameraActive &&
            m_ui.Camera == CameraMode::Static &&
            m_CameraVerticalFov == preset.VerticalFovDegrees &&
            IsSponzaCameraAtPreset(
                preset,
                GetActiveCamera().GetPosition(),
                GetActiveCamera().GetDir(),
                GetActiveCamera().GetUp()) &&
            windowWidth == int(preset.ReferenceWidth) &&
            windowHeight == int(preset.ReferenceHeight) &&
            m_ui.EnablePbr &&
            m_ui.RenderMode == RendererMode::Deferred &&
            !m_ui.EnableMiniEngineTaa &&
            !m_ui.UsesMiniEngineTaa() &&
            !m_ui.BendScreenSpaceShadows.enabled &&
            !m_ui.SparseVirtualShadowMaps.enabled &&
            !m_ui.ScreenSpaceVisibility.enabled &&
            m_ui.WhiteWorld == WhiteWorldMode::Off &&
            m_ui.DiagnosticCascadedShadowMaps.enabled &&
            IsSameDiagnosticCsmTimingConfiguration(
                m_ui.DiagnosticCascadedShadowMaps,
                m_DiagnosticCsmBenchmarkSettings);
        if (valid &&
            m_DiagnosticCsmBenchmarkStateArmed &&
            shadowStateDirty)
        {
            valid = false;
        }
        if (valid && m_SunLight)
        {
            const double3 lightDirection = m_SunLight->GetDirection();
            valid =
                lightDirection.x ==
                    m_DiagnosticCsmBenchmarkLightDirection.x &&
                lightDirection.y ==
                    m_DiagnosticCsmBenchmarkLightDirection.y &&
                lightDirection.z ==
                    m_DiagnosticCsmBenchmarkLightDirection.z;
        }
        if (valid &&
            !m_DiagnosticCsmBenchmarkStateArmed &&
            !shadowStateDirty)
        {
            m_DiagnosticCsmBenchmarkStateArmed = true;
            if (m_DiagnosticCsmRecordRequested &&
                !StartDiagnosticCsmBenchmarkRecording())
            {
                m_DiagnosticCsmRecordFinished = true;
                valid = false;
            }
            else
            {
                log::info(
                    "Diagnostic CSM benchmark state is locked and ready");
            }
        }
        if (valid)
            return true;

        if (m_DiagnosticCsmRecordRequested &&
            !m_DiagnosticCsmRecordFinished)
        {
            m_DiagnosticCsmRecordFinished = true;
            WriteDiagnosticCsmBenchmarkFailure(
                "scene, camera, light, renderer configuration, or shadow state changed");
        }
        log::error(
            "Diagnostic CSM benchmark aborted because its scene, 1920 x 1080 camera, light, or renderer configuration changed");
        glfwSetWindowShouldClose(
            GetDeviceManager()->GetWindow(),
            GLFW_TRUE);
        return false;
    }

    virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        UpdateSvsmMotionBenchmark();

        int windowWidth, windowHeight;
        GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
        nvrhi::Viewport windowViewport = nvrhi::Viewport(float(windowWidth), float(windowHeight));
        nvrhi::Viewport renderViewport = windowViewport;

        const auto& sceneGraph = m_Scene->GetSceneGraph();
        const auto& sceneRoot = sceneGraph->GetRootNode();
        const auto& sceneMaterials = sceneGraph->GetMaterials();
        const bool shadowRelevantMaterialDirty =
            std::any_of(
                sceneMaterials.begin(),
                sceneMaterials.end(),
                [](const auto& material) {
                    return material && material->dirty;
                });
        using DirtyFlags = SceneGraphNode::DirtyFlags;
        const DirtyFlags pendingSceneDirtyFlags = sceneRoot
            ? sceneRoot->GetDirtyFlags()
            : DirtyFlags::None;
        const DirtyFlags fullShadowMapInvalidationFlags =
            DirtyFlags::Leaf |
            DirtyFlags::SubgraphStructure |
            DirtyFlags::SubgraphContentUpdate;
        const bool shadowMapsRequireFullSceneInvalidation =
            shadowRelevantMaterialDirty ||
            (pendingSceneDirtyFlags &
                fullShadowMapInvalidationFlags) != 0u;
        // CSM retains its existing conservative policy. SVSM can localize
        // structural add/remove and transform changes through stable
        // instance/geometry snapshots. Donut does not expose per-object
        // revisions for same-handle vertex-buffer or texture-content writes,
        // so those unassignable channels must still fail open to a full
        // refresh instead of letting one recognized caster event mask another.
        const bool svsmRequiresFullSceneInvalidation =
            shadowRelevantMaterialDirty ||
            (pendingSceneDirtyFlags &
                DirtyFlags::SubgraphContentUpdate) != 0u;
        const DirtyFlags shadowDepthBindingRebaseFlags =
            DirtyFlags::Leaf |
            DirtyFlags::SubgraphStructure;
        const bool shadowDepthBindingsRequireReset =
            (pendingSceneDirtyFlags &
                shadowDepthBindingRebaseFlags) != 0u;
        const DirtyFlags shadowRelevantDirtyFlags =
            DirtyFlags::LocalTransform |
            DirtyFlags::Leaf |
            DirtyFlags::SubgraphStructure |
            DirtyFlags::SubgraphTransforms |
            DirtyFlags::SubgraphContentUpdate;
        const bool shadowRelevantSceneDirty =
            shadowRelevantMaterialDirty ||
            (pendingSceneDirtyFlags &
                shadowRelevantDirtyFlags) != 0u;
        const bool svsmCasterRelevantSceneDirty =
            shadowRelevantMaterialDirty ||
            HasSvsmCasterRelevantDirtyState(
                sceneRoot.get(),
                m_SvsmDirtyNodeScratch);
        if (sceneRoot)
        {
            if (shadowRelevantSceneDirty)
            {
                ++m_SvsmSceneStateRevision;
            }
            if (svsmCasterRelevantSceneDirty)
            {
                ++m_SvsmCasterStateRevision;
            }
        }
        // UVSR samples transform/content dirty flags and material dirtiness
        // before RefreshSceneGraph clears them. The second revision walks only
        // dirty branches and ignores a transform whose branch contains no
        // opaque or alpha-tested casters. A moving sun therefore does not force
        // a full caster hash and binding-signature traversal, while structural
        // changes and uncertain content changes still fail open. Animation
        // channels use the same setters, so dormant animation data remains free.
        m_SvsmSceneStateRevisionReliable = sceneRoot != nullptr;
        m_Scene->RefreshSceneGraph(GetFrameIndex());
        if (!ValidateDiagnosticCsmBenchmarkState(
                windowWidth,
                windowHeight,
                shadowRelevantSceneDirty))
        {
            return;
        }
        const auto& sceneLights =
            m_Scene->GetSceneGraph()->GetLights();

        {
            uint width = windowWidth;
            uint height = windowHeight;

            constexpr uint sampleCount = 1;
            const bool visibilityResourcesRequired = m_ui.EnablePbr &&
                m_ui.UsesDeferredShading() &&
                m_ui.HasActiveScreenSpaceVisibilityConsumer();
            const bool visibilitySourceRadianceRequired =
                visibilityResourcesRequired &&
                m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
                (!sceneLights.empty() ||
                    IsImageBasedLightingLobeActive(
                        m_ui.EnableDiffuseIbl,
                        m_ui.DiffuseIblStrength) ||
                    (m_ui.ScreenSpaceVisibility.indirectDiffuse.includeEmissive &&
                        m_ui.ScreenSpaceVisibility.indirectDiffuse.emissiveGain > 0.f));
            const bool temporalAARequired = m_ui.UsesMiniEngineTaa();
            const bool motionVectorsRequired =
                temporalAARequired ||
                (visibilityResourcesRequired &&
                    m_ui.ScreenSpaceVisibility.RequiresMotionVectors());

            bool needNewPasses = false;

            if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(
                uint2(width, height), sampleCount, m_ui.EnablePbr,
                visibilityResourcesRequired,
                visibilitySourceRadianceRequired,
                motionVectorsRequired))
            {
                m_RenderTargets = nullptr;
                m_BindingCache.Clear();
                m_RenderTargets = std::make_unique<RenderTargets>();
                m_RenderTargets->Init(
                    GetDevice(), uint2(width, height), sampleCount,
                    motionVectorsRequired, true, m_ui.EnablePbr,
                    visibilityResourcesRequired,
                    visibilitySourceRadianceRequired);
                m_PreviousView.reset();
                needNewPasses = true;
            }

            if (temporalAARequired != bool(m_MiniEngineTemporalAAPass))
                needNewPasses = true;

            if (SetupView())
            {
                needNewPasses = true;
                m_PreviousView.reset();
            }

            if (m_ui.ShaderReloadRequested)
            {
                m_DiagnosticCascadedShadowMapPass.reset();
                m_ShaderFactory->ClearCache();
                // Light-probe preprocessing owns shader handles too. Recreate
                // it only for an explicit shader reload, not for resize or TAA
                // pass recreation, so static IBL remains zero-work otherwise.
                m_ImageBasedLightingEnvironment =
                    std::make_unique<ImageBasedLightingEnvironment>(
                        GetDevice(),
                        m_ShaderFactory,
                        m_CommonPasses,
                        app::GetDirectoryWithExecutable().parent_path() /
                            "media/environments");
                needNewPasses = true;
            }

            if(needNewPasses)
            {
                CreateRenderPasses();
            }

            m_ui.ShaderReloadRequested = false;
        }

        m_CommandList->open();
#ifdef _WIN32
        NameD3d12CommandList(
            m_CommandList,
            L"UVSR Main Render Command List");
        SetD3d12DredMarker(m_CommandList, L"UVSR Main Frame Start");
#endif

        m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());

        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));

        constexpr float WhiteWorldIndirectReferenceScale = 4.0f;
        const bool whiteWorldEnabled =
            m_ui.WhiteWorld != WhiteWorldMode::Off;

        if (m_ImageBasedLightingEnvironment)
        {
            m_ImageBasedLightingEnvironment->Update(
                m_CommandList,
                whiteWorldEnabled,
                whiteWorldEnabled
                    ? WhiteWorldIndirectReferenceScale
                    : 1.f,
                m_ui.EnvironmentExposureStops,
                m_ui.EnableDiffuseIbl,
                m_ui.DiffuseIblStrength,
                m_ui.EnableSpecularIbl,
                m_ui.SpecularIblStrength,
                m_ui.EnvironmentSource);
        }
        const std::vector<std::shared_ptr<LightProbe>> emptyLightProbes;
        const auto& environmentLightProbes =
            m_ui.EnablePbr && m_ImageBasedLightingEnvironment
                ? m_ImageBasedLightingEnvironment->GetLightProbes()
                : emptyLightProbes;
        const LightProbe* globalEnvironment =
            m_ui.EnablePbr && m_ImageBasedLightingEnvironment
                ? m_ImageBasedLightingEnvironment->GetLightProbe()
                : nullptr;
        nvrhi::ITexture* diffuseEnvironment =
            globalEnvironment
                ? globalEnvironment->diffuseMap.Get()
                : nullptr;
        const float diffuseEnvironmentScale =
            globalEnvironment
                ? globalEnvironment->diffuseScale
                : 0.f;

        m_RenderTargets->Clear(m_CommandList);
        BendScreenSpaceShadowResult bendShadowResult;
        SparseVirtualShadowMapResult sparseVirtualShadowMapResult;
        DiagnosticCascadedShadowMapResult diagnosticCsmResult;

        if (m_SparseVirtualShadowMapPass &&
            (!m_ui.UsesDeferredShading() || !m_ui.EnablePbr))
        {
            m_SparseVirtualShadowMapPass->Deactivate();
        }
        if (m_DiagnosticCascadedShadowMapPass &&
            (!m_ui.UsesDeferredShading() || !m_ui.EnablePbr))
        {
            m_DiagnosticCascadedShadowMapPass->Deactivate();
        }

        ForwardShadingPass::Context forwardContext;

        if (!m_ui.UsesDeferredShading())
        {
            m_ForwardPass->PrepareLights(
                forwardContext,
                m_CommandList,
                m_Scene->GetSceneGraph()->GetLights(),
                float3(0.f),
                float3(0.f),
                environmentLightProbes);
        }

        if (m_ui.UsesDeferredShading())
        {
            GBufferFillPass::Context gbufferContext;

            RenderCompositeView(m_CommandList,
                m_View.get(), m_PreviousView ? m_PreviousView.get() : m_View.get(),
                *m_RenderTargets->GBufferFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_GBufferPass,
                gbufferContext,
                "GBufferFill",
                false);

            if (m_ui.EnablePbr && m_BendScreenSpaceShadowPass)
            {
                bendShadowResult = m_BendScreenSpaceShadowPass->Render(
                    m_CommandList,
                    m_ui.BendScreenSpaceShadows,
                    *m_View,
                    m_RenderTargets->Depth,
                    m_SunLight.get());
            }

            if (m_ui.EnablePbr && m_SparseVirtualShadowMapPass)
            {
                sparseVirtualShadowMapResult =
                    m_SparseVirtualShadowMapPass->Render(
                        m_CommandList,
                        m_ui.SparseVirtualShadowMaps,
                        *m_View,
                        m_RenderTargets->Depth,
                        m_SunLight.get(),
                        m_Scene->GetSceneGraph()->GetRootNode(),
                        m_ui.SparseVirtualShadowMaps.
                                casterOnlySceneRevisionEnabled
                            ? m_SvsmCasterStateRevision
                            : m_SvsmSceneStateRevision,
                        m_SvsmSceneStateRevisionReliable,
                        svsmRequiresFullSceneInvalidation,
                        shadowDepthBindingsRequireReset,
                        *m_OpaqueDrawStrategy,
                        m_SvsmMotionBenchmarkCurrentTimingTag,
                        // Accepted runs stay total-only. An explicit detailed
                        // diagnostic keeps the independent stage queries so
                        // the debug control can identify a regression source;
                        // the result already fails accepted-evidence checks.
                        m_SvsmMotionBenchmarkActive &&
                            !m_ui.SparseVirtualShadowMaps.
                                detailedGpuTimingEnabled);
            }
            if (m_ui.EnablePbr && m_DiagnosticCascadedShadowMapPass)
            {
                diagnosticCsmResult =
                    m_DiagnosticCascadedShadowMapPass->Render(
                        m_CommandList,
                        m_ui.DiagnosticCascadedShadowMaps,
                        *m_View,
                        m_RenderTargets->Depth,
                        m_RenderTargets->GBufferNormals,
                        m_SunLight.get(),
                        m_Scene->GetSceneGraph()->GetRootNode(),
                        m_SvsmSceneStateRevision,
                        m_SvsmSceneStateRevisionReliable,
                        shadowMapsRequireFullSceneInvalidation,
                        *m_OpaqueDrawStrategy);
            }
            DeferredLightingPass::Inputs deferredInputs;
            deferredInputs.SetGBuffer(*m_RenderTargets);
            if (m_ui.EnablePbr)
            {
                // Slot 14 is unused by UVSR's current renderer and carries the
                // separate authored material ambient-occlusion attachment.
                deferredInputs.indirectDiffuse = m_RenderTargets->MaterialAmbientOcclusion;
            }
            deferredInputs.lights = &sceneLights;

            // Bend, SVSM, and diagnostic CSM remain independent visibility
            // producers. This frame-local adapter is their only shared
            // integration point, and
            // deferred lighting applies a factor only to its pointer-identical
            // light. Any producer can therefore be removed or ported
            // without changing the other.
            const DirectionalLightVisibilitySet directionalVisibility = {{
                {
                    bendShadowResult.nearVisibility,
                    bendShadowResult.light
                },
                {
                    sparseVirtualShadowMapResult.visibility,
                    sparseVirtualShadowMapResult.light
                },
                {
                    diagnosticCsmResult.visibility,
                    diagnosticCsmResult.light
                }
            }};

            const bool runScreenSpaceVisibility = m_ui.EnablePbr &&
                m_ui.HasActiveScreenSpaceVisibilityConsumer() &&
                m_ui.LightingDebugView == PbrLightingDebugView::None;
            uint32_t knownInactiveLightingSources = 0u;
            if (sceneLights.empty())
                knownInactiveLightingSources |= LightingSource_Direct;
            if (!m_ui.ScreenSpaceVisibility.indirectDiffuse.includeEmissive ||
                !(m_ui.ScreenSpaceVisibility.indirectDiffuse.emissiveGain > 0.f))
            {
                knownInactiveLightingSources |= LightingSource_Emissive;
            }
            if (!diffuseEnvironment ||
                !(diffuseEnvironmentScale > 0.f))
            {
                knownInactiveLightingSources |= LightingSource_Environment;
            }
            constexpr uint32_t firstBounceLightingSources =
                LightingSource_Direct |
                LightingSource_Emissive |
                LightingSource_Environment;
            const bool allFirstBounceSourcesInactive =
                (knownInactiveLightingSources &
                    firstBounceLightingSources) ==
                firstBounceLightingSources;
            const bool writeSourceRadiance = runScreenSpaceVisibility &&
                m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
                !allFirstBounceSourcesInactive;
            const bool writeBounceMetadata = writeSourceRadiance &&
                m_ui.ScreenSpaceVisibility.indirectDiffuse.bounceCount > 1u;
            deferredInputs.output = runScreenSpaceVisibility
                ? m_RenderTargets->BaseLighting.Get()
                : m_RenderTargets->HdrColor.Get();

            if (m_ui.EnablePbr)
            {
                m_PbrDeferredLightingPass->Render(
                    m_CommandList,
                    *m_View,
                    deferredInputs,
                    directionalVisibility,
                    globalEnvironment,
                    m_RenderTargets->DirectDiffuseRadiance,
                    runScreenSpaceVisibility,
                    writeSourceRadiance,
                    writeBounceMetadata,
                    m_ui.ScreenSpaceVisibility.indirectDiffuse.includeEmissive,
                    m_ui.ScreenSpaceVisibility.indirectDiffuse.emissiveGain,
                    uint32_t(m_ui.LightingDebugView),
                    float2(0.f));

                if (runScreenSpaceVisibility)
                {
                    ScreenSpaceVisibilityInputs visibilityInputs;
                    visibilityInputs.depth = m_RenderTargets->Depth;
                    visibilityInputs.normals = m_RenderTargets->GBufferNormals;
                    visibilityInputs.motionVectors =
                        m_RenderTargets->MotionVectors;
                    visibilityInputs.sourceRadiance = m_RenderTargets->DirectDiffuseRadiance;
                    visibilityInputs.gbufferDiffuse = m_RenderTargets->GBufferDiffuse;
                    visibilityInputs.gbufferSpecular =
                        m_RenderTargets->GBufferSpecular;
                    visibilityInputs.gbufferEmissive = m_RenderTargets->GBufferEmissive;
                    visibilityInputs.materialAmbientOcclusion =
                        m_RenderTargets->MaterialAmbientOcclusion;
                    visibilityInputs.diffuseEnvironment =
                        diffuseEnvironment;
                    visibilityInputs.diffuseEnvironmentScale =
                        diffuseEnvironmentScale;
                    if (globalEnvironment)
                    {
                        visibilityInputs.diffuseEnvironmentArrayIndex =
                            globalEnvironment->diffuseArrayIndex;
                        visibilityInputs.specularEnvironment =
                            globalEnvironment->specularMap.Get();
                        visibilityInputs.environmentBrdf =
                            globalEnvironment->environmentBrdf.Get();
                        visibilityInputs.specularEnvironmentScale =
                            globalEnvironment->specularScale;
                        visibilityInputs.specularEnvironmentMipLevels =
                            globalEnvironment->specularMap
                                ? float(globalEnvironment->specularMap->
                                    getDesc().mipLevels)
                                : 0.f;
                        visibilityInputs.specularEnvironmentArrayIndex =
                            globalEnvironment->specularArrayIndex;
                    }
                    visibilityInputs.baseLighting = m_RenderTargets->BaseLighting;
                    visibilityInputs.output = m_RenderTargets->HdrColor;
                    visibilityInputs.knownInactiveLightingSources =
                        knownInactiveLightingSources;
                    m_ScreenSpaceVisibilityPass->Render(
                        m_CommandList,
                        m_ui.ScreenSpaceVisibility,
                        *m_View,
                        visibilityInputs,
                        // The production display path has fixed neutral
                        // exposure while lighting remains under development.
                        1.f,
                        uint32_t(GetFrameIndex()));
                }
                else
                {
                    m_ScreenSpaceVisibilityPass->Deactivate();
                }
            }
            else
            {
                m_DeferredLightingPass->Render(
                    m_CommandList, *m_View, deferredInputs, float2(0.f));
            }
        }
        else
        {
            RenderCompositeView(m_CommandList,
                m_View.get(), m_View.get(),
                *m_RenderTargets->ForwardFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_ForwardPass,
                forwardContext,
                "ForwardOpaque",
                false);
        }

        if(m_Pick)
        {
            m_CommandList->clearTextureUInt(
                m_RenderTargets->MaterialIDs,
                nvrhi::AllSubresources, 0xffffu);

            MaterialIDPass::Context materialIdContext;
            RenderCompositeView(m_CommandList,
                m_View.get(), m_View.get(),
                *m_RenderTargets->MaterialIDFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_MaterialIDPass,
                materialIdContext,
                "MaterialID");
            
            m_PixelReadbackPass->Capture(m_CommandList, m_PickPosition);
        }

        if (m_ui.ShowEnvironmentBackground &&
            m_ImageBasedLightingBackgroundPass &&
            m_ImageBasedLightingEnvironment &&
            m_ImageBasedLightingEnvironment->GetRadianceTexture())
        {
            m_ImageBasedLightingBackgroundPass->Render(
                m_CommandList,
                *m_View,
                m_ImageBasedLightingEnvironment->
                    GetRadianceScale());
        }

        if (m_MiniEngineTemporalAAPass)
        {
            // Resolve scene-linear radiance before any display transform. The
            // pass intentionally has no exposure, grading, LUT, or transfer
            // dependency, so removing or replacing the display stage does not
            // change its contract.
            m_MiniEngineTemporalAAPass->Render(
                m_CommandList,
                *m_View,
                m_PreviousView.get(),
                uint64_t(GetFrameIndex()),
                m_ui.EnableMiniEngineTaaSharpen,
                m_ui.MiniEngineTaaSharpness);
        }

        nvrhi::ITexture* displayTexture = m_RenderTargets->HdrColor;
        if (m_ui.UsesTonemapper() &&
            !bendShadowResult.showDebug &&
            !sparseVirtualShadowMapResult.showDebug &&
            !diagnosticCsmResult.showDebug)
        {
            m_AgxToneMappingPass->Render(
                m_CommandList, *m_View, m_RenderTargets->HdrColor);

            displayTexture = m_RenderTargets->LdrColor;
        }

        // The tonemapperless renderer intentionally sends forward scene-linear
        // radiance straight to the sRGB swap-chain target. The render-target
        // conversion still applies the display transfer and clamps values to
        // the target's representable range, but AgX output conversion and
        // dithering are absent from this path.
        if (diagnosticCsmResult.showDebug &&
            diagnosticCsmResult.debugVisualization)
        {
            m_CommonPasses->BlitTexture(
                m_CommandList,
                framebuffer,
                diagnosticCsmResult.debugVisualization,
                &m_BindingCache);
        }
        else if (sparseVirtualShadowMapResult.showDebug &&
            m_SparseVirtualShadowMapPass)
        {
            m_SparseVirtualShadowMapPass->PresentDebug(
                m_CommandList,
                framebuffer);
        }
        else if (bendShadowResult.showDebug &&
            m_BendScreenSpaceShadowPass)
        {
            m_BendScreenSpaceShadowPass->PresentDebug(
                m_CommandList,
                framebuffer);
        }
        else
        {
            m_CommonPasses->BlitTexture(
                m_CommandList, framebuffer, displayTexture, &m_BindingCache);
        }

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        UpdateDiagnosticCsmBenchmarkRecording();
        if (m_RenderTargets->MotionVectorsEnabled)
            CaptureCurrentViewForMotionVectors();

        if (m_ui.CopyScreenshotToClipboard)
        {
            const std::filesystem::path screenshotPath = std::filesystem::temp_directory_path()
                / ("uvsr_screenshot_" + std::to_string(GetCurrentProcessId()) + ".bmp");
            SaveTextureToFile(GetDevice(), m_CommonPasses.get(), framebufferTexture,
                nvrhi::ResourceStates::RenderTarget, screenshotPath.string().c_str());
            if (CopyBmpToClipboard(screenshotPath))
                log::info("Screenshot copied to clipboard.");
            else
                log::error("Failed to copy screenshot to clipboard.");
            DeleteFileW(screenshotPath.c_str());
            m_ui.CopyScreenshotToClipboard = false;
        }

        if (m_Pick)
        {
            m_Pick = false;
            uint4 pixelValue = m_PixelReadbackPass->ReadUInts();
            m_ui.SelectedMaterial = nullptr;
            m_ui.SelectedNode = nullptr;

            for (const auto& material : m_Scene->GetSceneGraph()->GetMaterials())
            {
                if (material->materialID == int(pixelValue.x))
                {
                    m_ui.SelectedMaterial = material;
                    break;
                }
            }

            for (const auto& instance : m_Scene->GetSceneGraph()->GetMeshInstances())
            {
                if (instance->GetInstanceIndex() == int(pixelValue.y))
                {
                    m_ui.SelectedNode = instance->GetNodeSharedPtr();
                    break;
                }
            }

            if (m_ui.SelectedNode)
            {
                log::info("Picked node: %s", m_ui.SelectedNode->GetPath().generic_string().c_str());
                PointThirdPersonCameraAt(m_ui.SelectedNode);
            }
            else
            {
                PointThirdPersonCameraAt(m_Scene->GetSceneGraph()->GetRootNode());
            }
        }

    }

    std::shared_ptr<ShaderFactory> GetShaderFactory()
    {
        return m_ShaderFactory;
    }

    void ResetImageBasedLightingHistory(
        bool resetScreenSpaceVisibility)
    {
        if (resetScreenSpaceVisibility &&
            m_ScreenSpaceVisibilityPass)
        {
            m_ScreenSpaceVisibilityPass->ResetHistory();
        }
        if (m_MiniEngineTemporalAAPass)
            m_MiniEngineTemporalAAPass->ResetHistory();
    }

    float GetSceneDiagonal() const
    {
        return m_SceneDiagonal;
    }

    float GetImageBasedLightingSourceAverageLuminance() const
    {
        return m_ImageBasedLightingEnvironment
            ? m_ImageBasedLightingEnvironment->
                GetSourceAverageLuminance()
            : 0.f;
    }

    float GetImageBasedLightingRadianceScale() const
    {
        return m_ImageBasedLightingEnvironment
            ? m_ImageBasedLightingEnvironment->GetRadianceScale()
            : 0.f;
    }

    const ScreenSpaceVisibilityTimings* GetScreenSpaceVisibilityTimings() const
    {
        return m_ScreenSpaceVisibilityPass
            ? &m_ScreenSpaceVisibilityPass->GetTimings()
            : nullptr;
    }

    const BendScreenSpaceShadowTimings*
        GetBendScreenSpaceShadowTimings() const
    {
        return m_BendScreenSpaceShadowPass
            ? &m_BendScreenSpaceShadowPass->GetTimings()
            : nullptr;
    }

    const SparseVirtualShadowMapTimings*
        GetSparseVirtualShadowMapTimings() const
    {
        return m_SparseVirtualShadowMapPass
            ? &m_SparseVirtualShadowMapPass->GetTimings()
            : nullptr;
    }

    const DiagnosticCsmTimings*
        GetDiagnosticCascadedShadowMapTimings() const
    {
        return m_DiagnosticCascadedShadowMapPass
            ? &m_DiagnosticCascadedShadowMapPass->GetTimings()
            : nullptr;
    }

    const DiagnosticCsmStats*
        GetDiagnosticCascadedShadowMapStats() const
    {
        return m_DiagnosticCascadedShadowMapPass
            ? &m_DiagnosticCascadedShadowMapPass->GetStats()
            : nullptr;
    }

    bool HasPrimaryDirectionalLight() const
    {
        return bool(m_SunLight);
    }

    const MiniEngineTemporalAATimings* GetMiniEngineTemporalAATimings() const
    {
        return m_MiniEngineTemporalAAPass
            ? &m_MiniEngineTemporalAAPass->GetTimings()
            : nullptr;
    }

};

namespace
{
    struct alignas(16) BackdropBlurConstants
    {
        float2 reciprocalSourceSize;
        float2 sampleDirection;

        float blurRadius = 0.f;
        float sigma = 1.f;
        float2 panelMin;

        float2 panelSize;
        float2 reciprocalWindowSize;

        float cornerRadius = 0.f;
        float3 padding;
    };

    static_assert(sizeof(BackdropBlurConstants) == 64u);

    class BackdropBlurPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<CommonRenderPasses> m_CommonPasses;
        BindingCache m_BindingCache;
        nvrhi::CommandListHandle m_CommandList;
        nvrhi::ShaderHandle m_BlurPixelShader;
        nvrhi::ShaderHandle m_CompositePixelShader;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::TextureHandle m_DownsampleTexture;
        nvrhi::TextureHandle m_HorizontalBlurTexture;
        nvrhi::FramebufferHandle m_DownsampleFramebuffer;
        nvrhi::FramebufferHandle m_HorizontalBlurFramebuffer;
        nvrhi::BindingSetHandle m_HorizontalBindingSet;
        nvrhi::BindingSetHandle m_CompositeBindingSet;
        nvrhi::GraphicsPipelineHandle m_HorizontalPipeline;
        nvrhi::GraphicsPipelineHandle m_CompositePipeline;
        uint32_t m_WindowWidth = 0;
        uint32_t m_WindowHeight = 0;
        uint32_t m_BlurWidth = 0;
        uint32_t m_BlurHeight = 0;
        nvrhi::Format m_FramebufferFormat = nvrhi::Format::UNKNOWN;

        void ResetResources()
        {
            m_BindingCache.Clear();
            m_DownsampleTexture = nullptr;
            m_HorizontalBlurTexture = nullptr;
            m_DownsampleFramebuffer = nullptr;
            m_HorizontalBlurFramebuffer = nullptr;
            m_HorizontalBindingSet = nullptr;
            m_CompositeBindingSet = nullptr;
            m_HorizontalPipeline = nullptr;
            m_CompositePipeline = nullptr;
            m_WindowWidth = 0;
            m_WindowHeight = 0;
            m_BlurWidth = 0;
            m_BlurHeight = 0;
            m_FramebufferFormat = nvrhi::Format::UNKNOWN;
        }

        bool EnsureResources(nvrhi::IFramebuffer* framebuffer)
        {
            const nvrhi::FramebufferInfoEx& framebufferInfo =
                framebuffer->getFramebufferInfo();
            if (framebufferInfo.colorFormats.empty())
                return false;

            const uint32_t windowWidth = framebufferInfo.width;
            const uint32_t windowHeight = framebufferInfo.height;
            const nvrhi::Format framebufferFormat =
                framebufferInfo.colorFormats[0];
            if (m_DownsampleTexture &&
                m_WindowWidth == windowWidth &&
                m_WindowHeight == windowHeight &&
                m_FramebufferFormat == framebufferFormat)
            {
                return true;
            }

            ResetResources();
            if (windowWidth == 0u ||
                windowHeight == 0u ||
                !m_BlurPixelShader ||
                !m_CompositePixelShader)
            {
                return false;
            }

            m_WindowWidth = windowWidth;
            m_WindowHeight = windowHeight;
            m_BlurWidth = std::max(1u, (windowWidth + 1u) / 2u);
            m_BlurHeight = std::max(1u, (windowHeight + 1u) / 2u);
            m_FramebufferFormat = framebufferFormat;

            nvrhi::TextureDesc textureDesc;
            textureDesc.width = m_BlurWidth;
            textureDesc.height = m_BlurHeight;
            textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
            textureDesc.mipLevels = 1u;
            textureDesc.format = framebufferFormat;
            textureDesc.isRenderTarget = true;
            textureDesc.initialState = nvrhi::ResourceStates::ShaderResource;
            textureDesc.keepInitialState = true;
            textureDesc.debugName = "UI Backdrop Blur/Downsample";
            m_DownsampleTexture = m_Device->createTexture(textureDesc);

            textureDesc.debugName = "UI Backdrop Blur/Horizontal";
            m_HorizontalBlurTexture = m_Device->createTexture(textureDesc);

            m_DownsampleFramebuffer = m_Device->createFramebuffer(
                nvrhi::FramebufferDesc()
                    .addColorAttachment(m_DownsampleTexture));
            m_HorizontalBlurFramebuffer = m_Device->createFramebuffer(
                nvrhi::FramebufferDesc()
                    .addColorAttachment(m_HorizontalBlurTexture));

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Sampler(
                    0, m_CommonPasses->m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_DownsampleTexture)
            };
            m_HorizontalBindingSet = m_Device->createBindingSet(
                bindingSetDesc, m_BindingLayout);

            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Sampler(
                    0, m_CommonPasses->m_LinearClampSampler),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_HorizontalBlurTexture)
            };
            m_CompositeBindingSet = m_Device->createBindingSet(
                bindingSetDesc, m_BindingLayout);

            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
            pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
            pipelineDesc.PS = m_BlurPixelShader;
            pipelineDesc.bindingLayouts = { m_BindingLayout };
            pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
            pipelineDesc.renderState.depthStencilState.stencilEnable = false;
            m_HorizontalPipeline = m_Device->createGraphicsPipeline(
                pipelineDesc,
                m_HorizontalBlurFramebuffer->getFramebufferInfo());

            pipelineDesc.PS = m_CompositePixelShader;
            pipelineDesc.renderState.blendState.targets[0]
                .setBlendEnable(true)
                .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
                .setDestBlendAlpha(nvrhi::BlendFactor::One);
            m_CompositePipeline = m_Device->createGraphicsPipeline(
                pipelineDesc, framebufferInfo);

            return
                m_DownsampleTexture &&
                m_HorizontalBlurTexture &&
                m_DownsampleFramebuffer &&
                m_HorizontalBlurFramebuffer &&
                m_HorizontalBindingSet &&
                m_CompositeBindingSet &&
                m_HorizontalPipeline &&
                m_CompositePipeline;
        }

    public:
        BackdropBlurPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<ShaderFactory>& shaderFactory,
            std::shared_ptr<CommonRenderPasses> commonPasses)
            : m_Device(device)
            , m_CommonPasses(std::move(commonPasses))
            , m_BindingCache(device)
        {
            m_CommandList = device->createCommandList();

            std::vector<ShaderMacro> shaderMacros;
            shaderMacros.emplace_back("COMPOSITE", "0");
            m_BlurPixelShader = shaderFactory->CreateShader(
                "uvsr/backdrop_blur_ps.hlsl",
                "main",
                &shaderMacros,
                nvrhi::ShaderType::Pixel);
            shaderMacros[0] = ShaderMacro("COMPOSITE", "1");
            m_CompositePixelShader = shaderFactory->CreateShader(
                "uvsr/backdrop_blur_ps.hlsl",
                "main",
                &shaderMacros,
                nvrhi::ShaderType::Pixel);

            nvrhi::BufferDesc constantBufferDesc;
            constantBufferDesc.byteSize = sizeof(BackdropBlurConstants);
            constantBufferDesc.debugName = "UI Backdrop Blur/Constants";
            constantBufferDesc.isConstantBuffer = true;
            constantBufferDesc.isVolatile = true;
            constantBufferDesc.maxVersions =
                engine::c_MaxRenderPassConstantBufferVersions;
            m_ConstantBuffer =
                device->createBuffer(constantBufferDesc);

            nvrhi::BindingLayoutDesc bindingLayoutDesc;
            bindingLayoutDesc.visibility = nvrhi::ShaderType::Pixel;
            bindingLayoutDesc.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
                nvrhi::BindingLayoutItem::Sampler(0),
                nvrhi::BindingLayoutItem::Texture_SRV(0)
            };
            m_BindingLayout =
                device->createBindingLayout(bindingLayoutDesc);
        }

        void BackBufferResizing()
        {
            ResetResources();
        }

        void Render(
            nvrhi::IFramebuffer* framebuffer,
            float blurPixels,
            const std::array<UiBackdropRect, 2>& backdropRects)
        {
            const float clampedBlurPixels =
                std::clamp(blurPixels, 0.f, 24.f);
            if (clampedBlurPixels <= 0.f)
                return;

            const bool hasVisibleBackdrop = std::any_of(
                backdropRects.begin(),
                backdropRects.end(),
                [](const UiBackdropRect& rect)
                {
                    return
                        rect.visible &&
                        rect.maxX > rect.minX &&
                        rect.maxY > rect.minY;
                });
            if (!hasVisibleBackdrop || !EnsureResources(framebuffer))
                return;

            nvrhi::ITexture* framebufferTexture =
                framebuffer->getDesc().colorAttachments[0].texture;

            m_CommandList->open();
#ifdef _WIN32
            NameD3d12CommandList(
                m_CommandList,
                L"UVSR Backdrop Blur Command List");
            SetD3d12DredMarker(
                m_CommandList,
                L"UVSR Backdrop Blur Start");
#endif
            m_CommandList->beginMarker("UI Backdrop Blur");

            BlitParameters downsampleParameters;
            downsampleParameters.targetFramebuffer =
                m_DownsampleFramebuffer;
            downsampleParameters.sourceTexture = framebufferTexture;
            m_CommonPasses->BlitTexture(
                m_CommandList,
                downsampleParameters,
                &m_BindingCache);

            const float blurRadius =
                std::max(0.5f, clampedBlurPixels * 0.5f);
            BackdropBlurConstants constants{};
            constants.reciprocalSourceSize = float2(
                1.f / float(m_BlurWidth),
                1.f / float(m_BlurHeight));
            constants.sampleDirection = float2(1.f, 0.f);
            constants.blurRadius = blurRadius;
            constants.sigma = std::max(0.5f, blurRadius * 0.5f);
            constants.reciprocalWindowSize = float2(
                1.f / float(m_WindowWidth),
                1.f / float(m_WindowHeight));
            m_CommandList->writeBuffer(
                m_ConstantBuffer,
                &constants,
                sizeof(constants));

            nvrhi::GraphicsState horizontalState;
            horizontalState.pipeline = m_HorizontalPipeline;
            horizontalState.framebuffer = m_HorizontalBlurFramebuffer;
            horizontalState.bindings = { m_HorizontalBindingSet };
            horizontalState.viewport.addViewport(
                nvrhi::Viewport(
                    float(m_BlurWidth),
                    float(m_BlurHeight)));
            horizontalState.viewport.addScissorRect(
                nvrhi::Rect(
                    int(m_BlurWidth),
                    int(m_BlurHeight)));
            m_CommandList->setGraphicsState(horizontalState);

            nvrhi::DrawArguments drawArguments;
            drawArguments.instanceCount = 1;
            drawArguments.vertexCount = 4;
            m_CommandList->draw(drawArguments);

            for (const UiBackdropRect& backdropRect : backdropRects)
            {
                if (!backdropRect.visible)
                    continue;

                const float minX = std::clamp(
                    backdropRect.minX,
                    0.f,
                    float(m_WindowWidth));
                const float minY = std::clamp(
                    backdropRect.minY,
                    0.f,
                    float(m_WindowHeight));
                const float maxX = std::clamp(
                    backdropRect.maxX,
                    minX,
                    float(m_WindowWidth));
                const float maxY = std::clamp(
                    backdropRect.maxY,
                    minY,
                    float(m_WindowHeight));
                if (maxX <= minX || maxY <= minY)
                    continue;

                constants.sampleDirection = float2(0.f, 1.f);
                constants.panelMin = float2(minX, minY);
                constants.panelSize = float2(
                    maxX - minX,
                    maxY - minY);
                constants.cornerRadius = backdropRect.rounding;
                m_CommandList->writeBuffer(
                    m_ConstantBuffer,
                    &constants,
                    sizeof(constants));

                const nvrhi::Viewport panelViewport(
                    minX,
                    maxX,
                    minY,
                    maxY,
                    0.f,
                    1.f);
                nvrhi::GraphicsState compositeState;
                compositeState.pipeline = m_CompositePipeline;
                compositeState.framebuffer = framebuffer;
                compositeState.bindings = { m_CompositeBindingSet };
                compositeState.viewport.addViewport(panelViewport);
                compositeState.viewport.addScissorRect(
                    nvrhi::Rect(panelViewport));
                m_CommandList->setGraphicsState(compositeState);
                m_CommandList->draw(drawArguments);
            }

            m_CommandList->endMarker();
            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
        }
    };
}

class UIRenderer : public ImGui_Renderer
{
private:
    struct StatSnapshot
    {
        int width = 0;
        int height = 0;
        double frameTimeSeconds = 0.0;
        std::string rendererName;
        GpuPerformanceMetrics gpuMetrics;
        BendScreenSpaceShadowTimings bendShadowTimings;
        SparseVirtualShadowMapTimings sparseShadowTimings;
        DiagnosticCsmTimings diagnosticCsmTimings;
        DiagnosticCsmStats diagnosticCsmStats;
        ScreenSpaceVisibilityTimings visibilityTimings;
        MiniEngineTemporalAATimings temporalAATimings;
        bool hasBendShadowTimings = false;
        bool hasSparseShadowTimings = false;
        bool hasDiagnosticCsmTimings = false;
        bool hasVisibilityTimings = false;
        bool hasTemporalAATimings = false;
    };

    std::shared_ptr<UvsrSceneViewer> m_app;

    std::shared_ptr<app::RegisteredFont> m_Font;
    std::shared_ptr<engine::Light> m_SelectedLight;
    std::chrono::steady_clock::time_point m_ProgramLaunchTime;
    ImGuiID m_AdjustedSpaceFontBakedId = 0;
    float m_BaseSpaceAdvance = 0.f;
    double m_DisplayedFrameTime = 0.0;
    double m_DisplayedGpuBandwidthGBps = 0.0;
    double m_DisplayedGpuTFlops = 0.0;
    double m_StatSnapshotElapsed = 0.0;
    double m_StatFrameTimeSum = 0.0;
    uint32_t m_StatFrameTimeCount = 0;
    std::array<std::string, 5> m_PerformanceStatValues;
    std::array<std::string, 2> m_BendShadowStatLines;
    std::array<std::string, 17> m_SparseShadowStatLines;
    std::array<std::string, 11> m_DiagnosticCsmStatLines;
    std::array<std::string, 3> m_VisibilityStatLines;
    std::array<std::string, 2> m_TemporalAAStatLines;
    std::deque<StatSnapshot> m_StatUpdateQueue;
    bool m_HasAppliedStatSnapshot = false;
    bool m_HasGpuStatSnapshot = false;
    bool m_HasBendShadowStatSnapshot = false;
    bool m_HasSparseShadowStatSnapshot = false;
    bool m_HasDiagnosticCsmStatSnapshot = false;
    bool m_HasVisibilityStatSnapshot = false;
    bool m_HasTemporalAAStatSnapshot = false;
    float m_DisplayedLoadingProgress = 0.f;
    bool m_WasSceneLoading = false;
    std::unique_ptr<BackdropBlurPass> m_BackdropBlurPass;

	UIData& m_ui;

    static void CaptureCurrentWindowBackdrop(
        UiBackdropRect& backdropRect,
        float rounding)
    {
        const ImVec2 windowPosition = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        backdropRect.minX = windowPosition.x;
        backdropRect.minY = windowPosition.y;
        backdropRect.maxX = windowPosition.x + windowSize.x;
        backdropRect.maxY = windowPosition.y + windowSize.y;
        backdropRect.rounding = rounding;
        backdropRect.visible =
            windowSize.x > 0.f &&
            windowSize.y > 0.f;
    }

    static void ApplyReferenceStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        // Reapply the experiment's authored values every frame because Donut
        // restores ImGui's default style whenever the display scale changes.
        // The outer window owns the translucent surface; the transparent child
        // lets the pinned status area and scrolling settings read as one panel.
        style.WindowRounding = 8.f;
        style.ChildRounding = 8.f;
        style.PopupRounding = 8.f;
        style.FrameRounding = 4.f;
        style.GrabRounding = 4.f;
        style.ScrollbarRounding = 8.f;
        style.TabRounding = 4.f;
        style.WindowBorderSize = 1.f;
        style.DisabledAlpha = 0.38f;

        ImVec4* colors = style.Colors;
        colors[ImGuiCol_Text] = ImVec4(0.94f, 0.95f, 0.98f, 1.f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.58f, 0.59f, 0.61f, 1.f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.018f, 0.016f, 0.020f, 0.60f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.04f, 0.045f, 0.92f);
        colors[ImGuiCol_Border] = ImVec4(0.15f, 0.15f, 0.17f, 0.92f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.018f, 0.016f, 0.020f, 0.72f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.13f, 0.14f, 0.76f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.18f, 0.19f, 0.82f);
        colors[ImGuiCol_TitleBg] = ImVec4(0.035f, 0.035f, 0.040f, 0.82f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.045f, 0.045f, 0.050f, 0.90f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.035f, 0.035f, 0.040f, 0.74f);
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.018f, 0.016f, 0.020f, 0.36f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.66f, 0.67f, 0.69f, 0.13f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.74f, 0.75f, 0.77f, 0.20f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.80f, 0.81f, 0.83f, 0.26f);
        colors[ImGuiCol_CheckMark] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
        colors[ImGuiCol_Button] = ImVec4(0.018f, 0.016f, 0.020f, 0.72f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.13f, 0.13f, 0.14f, 0.76f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.18f, 0.18f, 0.19f, 0.82f);
        colors[ImGuiCol_Header] = ImVec4(0.30f, 0.31f, 0.33f, 0.92f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.38f, 0.39f, 0.41f, 0.97f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.45f, 0.46f, 0.48f, 1.f);
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.48f, 0.49f, 0.51f, 0.28f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.60f, 0.61f, 0.63f, 0.62f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.75f, 0.76f, 0.78f, 0.90f);
    }

    struct DrawerAnimationContext
    {
        ImGuiStorage* storage = nullptr;
        ImGuiID headerId = 0;
        float openAmount = 0.f;
        bool targetOpen = false;
        bool autoMeasure = false;
        bool needsInitialMeasurement = false;
        bool bodyVisible = false;
    };

    inline static DrawerAnimationContext g_DrawerAnimationContext;

    static bool DrawCollapsingHeader(
        const char* label,
        const char* tooltip,
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None)
    {
        const ImGuiID headerId = ImGui::GetID(label);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID amountKey =
            headerId ^ ImGuiID(0x4A9D31E7u);
        const ImGuiID frameKey =
            headerId ^ ImGuiID(0x71C6B42Du);
        const ImGuiID measuredHeightKey =
            headerId ^ ImGuiID(0xD14F83A9u);
        ImGui::PushStyleColor(
            ImGuiCol_Header,
            ImVec4(0.26f, 0.59f, 0.98f, 0.31f));
        ImGui::PushStyleColor(
            ImGuiCol_HeaderHovered,
            ImVec4(0.26f, 0.59f, 0.98f, 0.48f));
        ImGui::PushStyleColor(
            ImGuiCol_HeaderActive,
            ImVec4(0.26f, 0.59f, 0.98f, 0.65f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            ImGui::GetStyle().FrameRounding);
        ImGuiStyle& style = ImGui::GetStyle();
        const float itemSpacingY = style.ItemSpacing.y;
        style.ItemSpacing.y = 0.f;
        const bool open = ImGui::CollapsingHeader(label, flags);
        style.ItemSpacing.y = itemSpacingY;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::SetItemTooltip(tooltip);

        const int frame = ImGui::GetFrameCount();
        const int lastFrame = storage->GetInt(frameKey, -2);
        float openAmount = storage->GetFloat(
            amountKey,
            open ? 1.f : 0.f);
        const float measuredHeight =
            storage->GetFloat(measuredHeightKey, 0.f);
        const float target = open ? 1.f : 0.f;
        if (lastFrame < frame - 1)
        {
            openAmount = target;
        }
        else
        {
            if (openAmount != target)
            {
                constexpr float ExponentialSpeed = 30.f;
                const float deltaTime = std::max(
                    0.f,
                    ImGui::GetIO().DeltaTime);
                const float blend =
                    1.f - std::exp(
                        -ExponentialSpeed * deltaTime);
                openAmount +=
                    (target - openAmount) * blend;
                const float remainingPixels =
                    std::abs(target - openAmount) *
                    std::max(measuredHeight, 1.f);
                if (remainingPixels < 0.05f)
                    openAmount = target;
            }
        }
        storage->SetFloat(amountKey, openAmount);
        storage->SetInt(frameKey, frame);
        g_DrawerAnimationContext = {
            storage,
            headerId,
            openAmount,
            open,
            false,
            false,
            false
        };
        return open || openAmount > 0.f;
    }

    static void BeginDrawerBody(
        const char* id,
        float controlWidth)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiID measuredHeightKey =
            g_DrawerAnimationContext.headerId ^
            ImGuiID(0xD14F83A9u);
        const float measuredHeight =
            g_DrawerAnimationContext.storage != nullptr
                ? g_DrawerAnimationContext.storage->GetFloat(
                    measuredHeightKey,
                    0.f)
                : 0.f;
        g_DrawerAnimationContext.needsInitialMeasurement =
            measuredHeight <= 0.f;
        g_DrawerAnimationContext.autoMeasure =
            g_DrawerAnimationContext.needsInitialMeasurement &&
            g_DrawerAnimationContext.openAmount >= 0.999f;
        float animatedHeight = 0.f;
        if (!g_DrawerAnimationContext.autoMeasure)
        {
            if (measuredHeight > 0.f)
            {
                animatedHeight =
                    measuredHeight *
                    g_DrawerAnimationContext.openAmount;
            }
            else
            {
                animatedHeight =
                    (style.WindowPadding.y * 2.f +
                        ImGui::GetFrameHeight()) *
                    g_DrawerAnimationContext.openAmount;
            }
            animatedHeight = std::max(animatedHeight, 0.001f);
        }
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.66f, 0.67f, 0.69f, 0.13f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            ImVec4(0.018f, 0.016f, 0.020f, 0.72f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            ImVec4(0.13f, 0.13f, 0.14f, 0.76f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgActive,
            ImVec4(0.18f, 0.18f, 0.19f, 0.82f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(style.FramePadding.x, style.ItemSpacing.y));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.f);
        ImGuiChildFlags childFlags =
            ImGuiChildFlags_AlwaysUseWindowPadding |
            ImGuiChildFlags_AllowZeroSize;
        if (g_DrawerAnimationContext.autoMeasure)
            childFlags |= ImGuiChildFlags_AutoResizeY;
        g_DrawerAnimationContext.bodyVisible =
            ImGui::BeginChild(
            id,
            ImVec2(
                0.f,
                g_DrawerAnimationContext.autoMeasure
                    ? 0.f
                    : animatedHeight),
            childFlags,
            ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushItemWidth(controlWidth);
    }

    static void DrawDrawerBodyOutline(
        const ImVec2& minimum,
        const ImVec2& maximum,
        float rounding)
    {
        constexpr float Thickness = 1.f;
        constexpr float Inset = Thickness * 0.5f;
        constexpr float TopGap = 2.f;

        const ImVec2 outlineMinimum(
            minimum.x + Inset,
            minimum.y + Inset);
        const ImVec2 outlineMaximum(
            maximum.x - Inset,
            maximum.y - Inset);
        const float width = outlineMaximum.x - outlineMinimum.x;
        const float height = outlineMaximum.y - outlineMinimum.y;
        if (width <= Thickness || height <= TopGap + Thickness)
            return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(
            ImVec2(
                outlineMinimum.x - Thickness,
                outlineMinimum.y + TopGap),
            ImVec2(
                outlineMaximum.x + Thickness,
                outlineMaximum.y + Thickness),
            true);
        const int vertexStart = drawList->VtxBuffer.Size;
        drawList->AddRect(
            outlineMinimum,
            outlineMaximum,
            IM_COL32_WHITE,
            std::max(0.f, rounding - Inset),
            ImDrawFlags_RoundCornersAll,
            Thickness);
        const int vertexEnd = drawList->VtxBuffer.Size;
        drawList->PopClipRect();

        const float gradientExtent = std::max(height, 1.f);
        for (int vertexIndex = vertexStart;
            vertexIndex < vertexEnd;
            ++vertexIndex)
        {
            ImDrawVert& vertex = drawList->VtxBuffer[vertexIndex];
            const float gradientPosition = std::clamp(
                (vertex.pos.y - outlineMinimum.y) / gradientExtent,
                0.f,
                1.f);
            vertex.col = ImGui::GetColorU32(ImVec4(
                0.88f + 0.08f * gradientPosition,
                0.90f + 0.07f * gradientPosition,
                0.94f + 0.06f * gradientPosition,
                0.10f + 0.20f * gradientPosition));
        }
    }

    static void DrawControlGradientOutline(
        ImDrawList* drawList,
        const ImVec2& minimum,
        const ImVec2& maximum,
        float rounding)
    {
        constexpr float Thickness = 1.f;
        constexpr float Inset = Thickness * 0.5f;
        const ImVec2 outlineMinimum(
            minimum.x + Inset,
            minimum.y + Inset);
        const ImVec2 outlineMaximum(
            maximum.x - Inset,
            maximum.y - Inset);
        if (outlineMaximum.x - outlineMinimum.x <= Thickness ||
            outlineMaximum.y - outlineMinimum.y <= Thickness)
        {
            return;
        }

        const int vertexStart = drawList->VtxBuffer.Size;
        drawList->AddRect(
            outlineMinimum,
            outlineMaximum,
            IM_COL32_WHITE,
            std::max(0.f, rounding - Inset),
            ImDrawFlags_RoundCornersAll,
            Thickness);
        const int vertexEnd = drawList->VtxBuffer.Size;
        const float gradientExtent = std::max(
            outlineMaximum.y - outlineMinimum.y,
            1.f);
        for (int vertexIndex = vertexStart;
            vertexIndex < vertexEnd;
            ++vertexIndex)
        {
            ImDrawVert& vertex =
                drawList->VtxBuffer[vertexIndex];
            const float gradientPosition = std::clamp(
                (vertex.pos.y - outlineMinimum.y) /
                    gradientExtent,
                0.f,
                1.f);
            vertex.col = ImGui::GetColorU32(ImVec4(
                0.88f + 0.08f * gradientPosition,
                0.90f + 0.07f * gradientPosition,
                0.94f + 0.06f * gradientPosition,
                0.10f + 0.20f * gradientPosition));
        }
    }

    static void EndDrawerBody()
    {
        const float measuredHeight = std::max(
            1.f,
            ImGui::GetCursorPosY() +
                ImGui::GetStyle().WindowPadding.y);
        ImGui::PopItemWidth();
        ImGuiStyle& style = ImGui::GetStyle();
        const float itemSpacingY = style.ItemSpacing.y;
        style.ItemSpacing.y = 0.f;
        ImGui::EndChild();
        style.ItemSpacing.y = itemSpacingY;
        if (g_DrawerAnimationContext.storage != nullptr)
        {
            const ImGuiID measuredHeightKey =
                g_DrawerAnimationContext.headerId ^
                ImGuiID(0xD14F83A9u);
            const float renderedHeight =
                ImGui::GetItemRectSize().y;
            const bool updateMeasuredHeight =
                g_DrawerAnimationContext.bodyVisible &&
                (g_DrawerAnimationContext.autoMeasure ||
                    (g_DrawerAnimationContext.targetOpen &&
                        (g_DrawerAnimationContext.openAmount >= 0.999f ||
                            g_DrawerAnimationContext
                                .needsInitialMeasurement)));
            if (updateMeasuredHeight)
            {
                g_DrawerAnimationContext.storage->SetFloat(
                    measuredHeightKey,
                    g_DrawerAnimationContext.autoMeasure
                        ? renderedHeight
                        : measuredHeight);
            }
        }
        DrawDrawerBodyOutline(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::GetStyle().ChildRounding);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
    }

    struct NestedDrawerAnimationContext
    {
        ImGuiStorage* storage = nullptr;
        ImGuiID measuredHeightKey = 0;
        float openAmount = 0.f;
        bool targetOpen = false;
        bool bodyVisible = false;
    };

    inline static NestedDrawerAnimationContext
        g_NestedDrawerAnimationContext;

    static bool BeginAnimatedTreeNode(
        const char* label,
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None)
    {
        const ImGuiID headerId = ImGui::GetID(label);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID amountKey =
            headerId ^ ImGuiID(0x5CB870A3u);
        const ImGuiID frameKey =
            headerId ^ ImGuiID(0x34A1F27Du);
        const ImGuiID measuredHeightKey =
            headerId ^ ImGuiID(0x9D63E418u);
        const bool open = ImGui::TreeNodeEx(
            label,
            flags | ImGuiTreeNodeFlags_NoTreePushOnOpen);

        const int frame = ImGui::GetFrameCount();
        const int lastFrame = storage->GetInt(frameKey, -2);
        const float measuredHeight =
            storage->GetFloat(measuredHeightKey, 0.f);
        const float target = open ? 1.f : 0.f;
        float openAmount = storage->GetFloat(
            amountKey,
            target);
        if (lastFrame < frame - 1)
        {
            openAmount = target;
        }
        else if (openAmount != target)
        {
            constexpr float ExponentialSpeed = 30.f;
            const float deltaTime = std::max(
                0.f,
                ImGui::GetIO().DeltaTime);
            const float blend =
                1.f - std::exp(
                    -ExponentialSpeed * deltaTime);
            openAmount +=
                (target - openAmount) * blend;
            const float remainingPixels =
                std::abs(target - openAmount) *
                std::max(measuredHeight, 1.f);
            if (remainingPixels < 0.05f)
                openAmount = target;
        }
        storage->SetFloat(amountKey, openAmount);
        storage->SetInt(frameKey, frame);

        if (!open && openAmount <= 0.f)
            return false;

        float animatedHeight =
            measuredHeight > 0.f
                ? measuredHeight * openAmount
                : ImGui::GetFrameHeight() * openAmount;
        animatedHeight = std::max(animatedHeight, 0.001f);

        ImGui::Indent();
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            0.f);
        const bool bodyVisible = ImGui::BeginChild(
            headerId ^ ImGuiID(0xE60792B5u),
            ImVec2(0.f, animatedHeight),
            ImGuiChildFlags_AllowZeroSize,
            ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);
        g_NestedDrawerAnimationContext = {
            storage,
            measuredHeightKey,
            openAmount,
            open,
            bodyVisible
        };
        return true;
    }

    static void EndAnimatedTreeNode()
    {
        const float measuredHeight =
            std::max(0.f, ImGui::GetCursorPosY());
        ImGuiStyle& style = ImGui::GetStyle();
        const float itemSpacingY = style.ItemSpacing.y;
        style.ItemSpacing.y = 0.f;
        ImGui::EndChild();
        style.ItemSpacing.y = itemSpacingY;

        if (g_NestedDrawerAnimationContext.storage != nullptr &&
            g_NestedDrawerAnimationContext.targetOpen &&
            g_NestedDrawerAnimationContext.bodyVisible)
        {
            g_NestedDrawerAnimationContext.storage->SetFloat(
                g_NestedDrawerAnimationContext.measuredHeightKey,
                measuredHeight);
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::Unindent();
    }

    static ImVec2 MovePointToward(
        const ImVec2& point,
        const ImVec2& target,
        float distance)
    {
        const ImVec2 delta(
            target.x - point.x,
            target.y - point.y);
        const float length =
            std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (length <= 0.f)
            return point;

        const float scale = std::min(distance / length, 1.f);
        return ImVec2(
            point.x + delta.x * scale,
            point.y + delta.y * scale);
    }

    static void DrawRoundedDownTriangle(
        ImDrawList* drawList,
        const ImVec2& center,
        float width,
        float height,
        ImU32 color)
    {
        const ImVec2 left(
            center.x - width * 0.5f,
            center.y - height * 0.45f);
        const ImVec2 right(
            center.x + width * 0.5f,
            center.y - height * 0.45f);
        const ImVec2 bottom(
            center.x,
            center.y + height * 0.55f);
        const float cornerDistance =
            std::min(width, height) * 0.22f;

        drawList->PathClear();
        drawList->PathLineTo(
            MovePointToward(left, bottom, cornerDistance));
        drawList->PathBezierQuadraticCurveTo(
            left,
            MovePointToward(left, right, cornerDistance),
            4);
        drawList->PathLineTo(
            MovePointToward(right, left, cornerDistance));
        drawList->PathBezierQuadraticCurveTo(
            right,
            MovePointToward(right, bottom, cornerDistance),
            4);
        drawList->PathLineTo(
            MovePointToward(bottom, right, cornerDistance));
        drawList->PathBezierQuadraticCurveTo(
            bottom,
            MovePointToward(bottom, left, cornerDistance),
            4);
        drawList->PathFillConvex(color);
    }

    static float GetUiHighlightFade(
        ImGuiID id,
        bool highlighted,
        float speed = 24.f)
    {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID amountKey = id ^ ImGuiID(0xA53C9E21u);
        const ImGuiID frameKey = id ^ ImGuiID(0x6D27F4B3u);
        const float target = highlighted ? 1.f : 0.f;
        float amount = storage->GetFloat(amountKey, 0.f);
        const int frame = ImGui::GetFrameCount();
        const int lastFrame = storage->GetInt(frameKey, -2);
        if (lastFrame < frame - 1)
            amount = 0.f;
        const float blend = std::clamp(
            ImGui::GetIO().DeltaTime * speed,
            0.f,
            1.f);
        amount += (target - amount) * blend;
        if (std::abs(target - amount) < 0.015f)
            amount = target;
        storage->SetFloat(amountKey, amount);
        storage->SetInt(frameKey, frame);
        return amount;
    }

    static ImVec4 LerpUiColor(
        const ImVec4& normal,
        const ImVec4& interaction,
        float amount)
    {
        return ImVec4(
            normal.x + (interaction.x - normal.x) * amount,
            normal.y + (interaction.y - normal.y) * amount,
            normal.z + (interaction.z - normal.z) * amount,
            normal.w + (interaction.w - normal.w) * amount);
    }

    static bool BeginRoundedCombo(
        const char* label,
        const char* previewValue,
        ImGuiComboFlags flags = ImGuiComboFlags_None)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 frameMin = ImGui::GetCursorScreenPos();
        const float frameHeight = ImGui::GetFrameHeight();
        const ImVec2 frameMax(
            frameMin.x + ImGui::CalcItemWidth(),
            frameMin.y + frameHeight);
        const bool hovered =
            ImGui::IsMouseHoveringRect(frameMin, frameMax, false);
        const ImGuiID comboId = ImGui::GetID(label);

        const bool open = ImGui::BeginCombo(
            label,
            previewValue,
            flags | ImGuiComboFlags_NoArrowButton);

        const ImVec2 buttonMin(
            frameMax.x - frameHeight,
            frameMin.y);
        const float highlightFade = GetUiHighlightFade(
            comboId,
            hovered || open);
        const ImVec4 buttonColor = LerpUiColor(
            style.Colors[ImGuiCol_Button],
            style.Colors[
                open
                    ? ImGuiCol_ButtonActive
                    : ImGuiCol_ButtonHovered],
            highlightFade);
        drawList->AddRectFilled(
            buttonMin,
            frameMax,
            ImGui::GetColorU32(buttonColor),
            style.FrameRounding,
            ImDrawFlags_RoundCornersAll);
        DrawRoundedDownTriangle(
            drawList,
            ImVec2(
                (buttonMin.x + frameMax.x) * 0.5f,
                (buttonMin.y + frameMax.y) * 0.5f),
            frameHeight * 0.38f,
            frameHeight * 0.27f,
            ImGui::GetColorU32(ImGuiCol_Text));
        return open;
    }

    static void ApplyExpandedWordSpacing(
        ImGuiID& adjustedFontBakedId,
        float& baseSpaceAdvance)
    {
        ImFontBaked* baked = ImGui::GetFontBaked();
        if (!baked)
            return;

        ImFontGlyph* spaceGlyph =
            baked->FindGlyphNoFallback(ImWchar(' '));
        if (!spaceGlyph)
            return;

        if (adjustedFontBakedId != baked->BakedId)
        {
            adjustedFontBakedId = baked->BakedId;
            baseSpaceAdvance = spaceGlyph->AdvanceX;
        }

        constexpr float WordSpaceScale = 1.65f;
        const float expandedSpaceAdvance =
            baseSpaceAdvance * WordSpaceScale;
        spaceGlyph->AdvanceX = expandedSpaceAdvance;
        if (baked->IndexAdvanceX.Size > int(ImWchar(' ')))
        {
            baked->IndexAdvanceX[int(ImWchar(' '))] =
                expandedSpaceAdvance;
        }
    }

    static std::string BuildPerformanceLine(
        const std::array<std::string, 5>& values)
    {
        return values[0] + " / " +
            values[3] + " / " +
            values[4] + " / " +
            values[1] + " / " +
            values[2];
    }

    static double StepTowardByTenth(
        double current,
        double target)
    {
        if (target > current)
            return std::min(target, current + 0.1);
        if (target < current)
            return std::max(target, current - 0.1);
        return current;
    }

    template <typename... Arguments>
    static void FormatStatLine(
        std::string& destination,
        const char* format,
        Arguments... arguments)
    {
        char buffer[512];
        snprintf(
            buffer,
            std::size(buffer),
            format,
            arguments...);
        destination = buffer;
    }

    void QueueStatSnapshot(
        int width,
        int height,
        const char* rendererString)
    {
        constexpr double StatUpdateIntervalSeconds = 1.0 / 24.0;
        const double currentFrameTime = std::max(
            0.0,
            double(ImGui::GetIO().DeltaTime));
        m_StatSnapshotElapsed += currentFrameTime;
        if (currentFrameTime > 0.0)
        {
            m_StatFrameTimeSum += currentFrameTime;
            ++m_StatFrameTimeCount;
        }

        const bool captureInitialSnapshot =
            !m_HasAppliedStatSnapshot &&
            m_StatUpdateQueue.empty();
        if (!captureInitialSnapshot &&
            m_StatSnapshotElapsed < StatUpdateIntervalSeconds)
        {
            return;
        }

        StatSnapshot snapshot;
        snapshot.width = width;
        snapshot.height = height;
        snapshot.rendererName = rendererString ? rendererString : "";
        snapshot.frameTimeSeconds = m_StatFrameTimeCount > 0
            ? m_StatFrameTimeSum / double(m_StatFrameTimeCount)
            : m_DisplayedFrameTime;
        snapshot.gpuMetrics =
            QueryGpuPerformanceMetrics(rendererString);
        if (const BendScreenSpaceShadowTimings* timings =
                m_app->GetBendScreenSpaceShadowTimings())
        {
            snapshot.bendShadowTimings = *timings;
            snapshot.hasBendShadowTimings = true;
        }
        if (const SparseVirtualShadowMapTimings* timings =
                m_app->GetSparseVirtualShadowMapTimings())
        {
            snapshot.sparseShadowTimings = *timings;
            snapshot.hasSparseShadowTimings = true;
        }
        if (const DiagnosticCsmTimings* timings =
                m_app->GetDiagnosticCascadedShadowMapTimings())
        {
            snapshot.diagnosticCsmTimings = *timings;
            if (const DiagnosticCsmStats* stats =
                    m_app->GetDiagnosticCascadedShadowMapStats())
            {
                snapshot.diagnosticCsmStats = *stats;
            }
            snapshot.hasDiagnosticCsmTimings = true;
        }
        if (const ScreenSpaceVisibilityTimings* timings =
                m_app->GetScreenSpaceVisibilityTimings())
        {
            snapshot.visibilityTimings = *timings;
            snapshot.hasVisibilityTimings = true;
        }
        if (const MiniEngineTemporalAATimings* timings =
                m_app->GetMiniEngineTemporalAATimings())
        {
            snapshot.temporalAATimings = *timings;
            snapshot.hasTemporalAATimings = true;
        }

        // Keep a complete snapshot as the queue's atomic update unit. If a
        // future render path ever delays consumption, replace its stale pending
        // sample instead of replaying old statistics.
        if (m_StatUpdateQueue.empty())
            m_StatUpdateQueue.push_back(std::move(snapshot));
        else
            m_StatUpdateQueue.back() = std::move(snapshot);

        m_StatSnapshotElapsed = captureInitialSnapshot
            ? 0.0
            : std::fmod(
                m_StatSnapshotElapsed,
                StatUpdateIntervalSeconds);
        m_StatFrameTimeSum = 0.0;
        m_StatFrameTimeCount = 0;
    }

    void ApplyQueuedStatSnapshot()
    {
        if (m_StatUpdateQueue.empty())
            return;

        const StatSnapshot snapshot =
            std::move(m_StatUpdateQueue.front());
        m_StatUpdateQueue.pop_front();
        m_HasAppliedStatSnapshot = true;
        m_DisplayedFrameTime = snapshot.frameTimeSeconds;

        FormatStatLine(
            m_PerformanceStatValues[0],
            "%d x %d",
            snapshot.width,
            snapshot.height);
        if (m_DisplayedFrameTime > 0.0)
        {
            FormatStatLine(
                m_PerformanceStatValues[1],
                "%.1f ms",
                m_DisplayedFrameTime * 1e3);
            FormatStatLine(
                m_PerformanceStatValues[2],
                "%.1f fps",
                1.0 / m_DisplayedFrameTime);
        }
        else
        {
            m_PerformanceStatValues[1].clear();
            m_PerformanceStatValues[2].clear();
        }

        if (snapshot.gpuMetrics.valid)
        {
            const double targetTFlops =
                snapshot.gpuMetrics.gpuGFlops / 1000.0 *
                snapshot.gpuMetrics.gpuUtilization;
            if (!m_HasGpuStatSnapshot)
            {
                m_DisplayedGpuBandwidthGBps =
                    snapshot.gpuMetrics.memoryBandwidthGBps;
                m_DisplayedGpuTFlops = targetTFlops;
                m_HasGpuStatSnapshot = true;
            }
            else
            {
                m_DisplayedGpuBandwidthGBps =
                    snapshot.gpuMetrics.memoryBandwidthGBps;
                m_DisplayedGpuTFlops = StepTowardByTenth(
                    m_DisplayedGpuTFlops,
                    targetTFlops);
            }

            FormatStatLine(
                m_PerformanceStatValues[3],
                "%.1f gb/s",
                m_DisplayedGpuBandwidthGBps);
            FormatStatLine(
                m_PerformanceStatValues[4],
                "%.1f tflops",
                m_DisplayedGpuTFlops);
        }
        else
        {
            m_PerformanceStatValues[3] = "-- gb/s";
            m_PerformanceStatValues[4] = "-- tflops";
            m_HasGpuStatSnapshot = false;
        }

        if (snapshot.hasBendShadowTimings)
        {
            const BendScreenSpaceShadowTimings& timings =
                snapshot.bendShadowTimings;
            FormatStatLine(
                m_BendShadowStatLines[0],
                "Trace %.3f ms / %u dispatches / %u groups",
                timings.traceMilliseconds,
                timings.dispatchCount,
                timings.totalGroups);
            constexpr double BytesPerMib = 1024.0 * 1024.0;
            FormatStatLine(
                m_BendShadowStatLines[1],
                "%u samples / R8 output %.2f mib",
                timings.sampleCount,
                double(timings.outputTextureBytes) / BytesPerMib);
            m_HasBendShadowStatSnapshot = timings.active;
        }
        else
        {
            m_HasBendShadowStatSnapshot = false;
        }

        if (snapshot.hasSparseShadowTimings)
        {
            const SparseVirtualShadowMapTimings& timings =
                snapshot.sparseShadowTimings;
            if (timings.gpuTimingSource ==
                SvsmGpuTimingSource::Unavailable)
            {
                m_SparseShadowStatLines[0] =
                    "GPU timing unavailable for current configuration";
                FormatStatLine(
                    m_SparseShadowStatLines[1],
                    "Packets CPU %.3f ms / GPU stages unavailable",
                    timings.cullingCpuMilliseconds);
            }
            else
            {
                if (timings.gpuTimingSource ==
                    SvsmGpuTimingSource::KnownZero)
                {
                    FormatStatLine(
                        m_SparseShadowStatLines[0],
                        "GPU All %.3f / Mark %.3f / Allocate %.3f / Clear %.3f ms / known zero",
                        timings.totalMilliseconds,
                        timings.pageMarkingMilliseconds,
                        timings.allocationMilliseconds,
                        timings.clearingMilliseconds);
                    FormatStatLine(
                        m_SparseShadowStatLines[1],
                        "Packets CPU %.3f ms / GPU stages known zero",
                        timings.cullingCpuMilliseconds);
                }
                else if (!timings.detailedGpuTimingEnabled)
                {
                    FormatStatLine(
                        m_SparseShadowStatLines[0],
                        "GPU All %.3f ms / total-only / age %u f",
                        timings.totalMilliseconds,
                        timings.gpuTimingAgeFrames);
                    FormatStatLine(
                        m_SparseShadowStatLines[1],
                        "Packets CPU %.3f ms / detailed GPU stages disabled",
                        timings.cullingCpuMilliseconds);
                }
                else
                {
                    FormatStatLine(
                        m_SparseShadowStatLines[0],
                        "GPU All %.3f / Mark %.3f / Allocate %.3f / Clear %.3f ms / age %u f",
                        timings.totalMilliseconds,
                        timings.pageMarkingMilliseconds,
                        timings.allocationMilliseconds,
                        timings.clearingMilliseconds,
                        timings.gpuTimingAgeFrames);
                    FormatStatLine(
                        m_SparseShadowStatLines[1],
                        "Packets CPU %.3f / GPU %.3f / Render %.3f / Filter %.3f ms",
                        timings.cullingCpuMilliseconds,
                        timings.packetPageCullingMilliseconds,
                        timings.pageRenderingMilliseconds,
                        timings.filteringMilliseconds);
                }
            }
            constexpr double BytesPerMib = 1024.0 * 1024.0;
            if (timings.debugCountersAvailable)
            {
                FormatStatLine(
                    m_SparseShadowStatLines[2],
                    "Pages %u required / %u resident / %u cached",
                    timings.requiredPages,
                    timings.residentPages,
                    timings.cachedPages);
                FormatStatLine(
                    m_SparseShadowStatLines[3],
                    "Pages %u dirty / %u rendered / %u over budget",
                    timings.dirtyPages,
                    timings.renderedPages,
                    timings.overBudgetPages);
                FormatStatLine(
                    m_SparseShadowStatLines[4],
                    "Memory depth %.1f / vis %.1f / packets %.1f / receiver %.2f / static HZB %.1f MiB",
                    double(timings.physicalDepthBytes) / BytesPerMib,
                    double(timings.visibilityBytes) / BytesPerMib,
                    double(timings.packetPageMetadataBytes +
                        timings.packetPageListBytes) / BytesPerMib,
                    double(timings.receiverPageMaskBytes) /
                        BytesPerMib,
                    double(timings.staticDepthHierarchyBytes) /
                        BytesPerMib);
                FormatStatLine(
                    m_SparseShadowStatLines[5],
                    "Pixels fallback %u / missing %u / out %u / alloc fail %u",
                    timings.fallbackPixels,
                    timings.resolveMissingPixels,
                    timings.outOfRangePixels,
                    timings.allocationFailures);
                FormatStatLine(
                    m_SparseShadowStatLines[6],
                    "Packet pages %u candidate / %u compact / %u fail open / age %u f",
                    timings.packetPageCandidatePackets,
                    timings.packetPageCompactedPackets,
                    timings.packetPageFailOpenPackets,
                    timings.debugCounterAgeFrames);
                FormatStatLine(
                    m_SparseShadowStatLines[7],
                    "Tile mask %u queries / %u rejected / %u fail open / %u false positive",
                    timings.scheduledTileMaskQueries,
                    timings.scheduledTileMaskEarlyRejects,
                    timings.scheduledTileMaskFailOpens,
                    timings.scheduledTileMaskPositiveExactZero);
                FormatStatLine(
                    m_SparseShadowStatLines[8],
                    "Receiver mask req %s / active %s / unavailable %s / static HZB req %s / active %s / unavailable %s",
                    timings.receiverPageMaskCullingRequested
                        ? "yes"
                        : "no",
                    timings.receiverPageMaskCullingActive
                        ? "yes"
                        : "no",
                    timings.receiverPageMaskCullingUnavailable
                        ? "yes"
                        : "no",
                    timings.staticDepthHierarchyCullingRequested
                        ? "yes"
                        : "no",
                    timings.staticDepthHierarchyCullingActive
                        ? "yes"
                        : "no",
                    timings.staticDepthHierarchyCullingUnavailable
                        ? "yes"
                        : "no");
                FormatStatLine(
                    m_SparseShadowStatLines[9],
                    "Receiver mask %u query / %u culled / %u fail open / static HZB %u query / %u culled / %u fail open / %u built",
                    timings.receiverPageMaskQueries,
                    timings.receiverPageMaskCulledPages,
                    timings.receiverPageMaskFailOpens,
                    timings.staticDepthHierarchyQueries,
                    timings.staticDepthHierarchyCulledPages,
                    timings.staticDepthHierarchyFailOpens,
                    timings.staticDepthHierarchyBuiltPages);
            }
            else
            {
                m_SparseShadowStatLines[2] =
                    "Page counters unavailable";
                m_SparseShadowStatLines[3] =
                    "Dirty and rendered-page counters unavailable";
                FormatStatLine(
                    m_SparseShadowStatLines[4],
                    "Memory depth %.1f / vis %.1f / packets %.1f / receiver %.2f / static HZB %.1f MiB",
                    double(timings.physicalDepthBytes) / BytesPerMib,
                    double(timings.visibilityBytes) / BytesPerMib,
                    double(timings.packetPageMetadataBytes +
                        timings.packetPageListBytes) / BytesPerMib,
                    double(timings.receiverPageMaskBytes) /
                        BytesPerMib,
                    double(timings.staticDepthHierarchyBytes) /
                        BytesPerMib);
                m_SparseShadowStatLines[5] =
                    "Pixel and allocation counters unavailable";
                m_SparseShadowStatLines[6] =
                    "Packet-page counters unavailable";
                m_SparseShadowStatLines[7] =
                    "Scheduled tile-mask counters unavailable";
                FormatStatLine(
                    m_SparseShadowStatLines[8],
                    "Receiver mask req %s / active %s / unavailable %s / static HZB req %s / active %s / unavailable %s",
                    timings.receiverPageMaskCullingRequested
                        ? "yes"
                        : "no",
                    timings.receiverPageMaskCullingActive
                        ? "yes"
                        : "no",
                    timings.receiverPageMaskCullingUnavailable
                        ? "yes"
                        : "no",
                    timings.staticDepthHierarchyCullingRequested
                        ? "yes"
                        : "no",
                    timings.staticDepthHierarchyCullingActive
                        ? "yes"
                        : "no",
                    timings.staticDepthHierarchyCullingUnavailable
                        ? "yes"
                        : "no");
                m_SparseShadowStatLines[9] =
                    "Receiver-mask and static-HZB counters unavailable";
            }
            const char* staticRejectReason = "none";
            switch (FirstSvsmStaticPageRequestRejectBit(
                timings.staticPageRequestReuseRejectMask))
            {
            case 0u: staticRejectReason = "toggle off"; break;
            case 1u: staticRejectReason = "cache mode off"; break;
            case 2u: staticRejectReason = "pool unsupported"; break;
            case 3u: staticRejectReason = "budget cannot settle"; break;
            case 4u: staticRejectReason = "cache warming"; break;
            case 5u: staticRejectReason = "depth resource changed"; break;
            case 6u: staticRejectReason = "resolution changed"; break;
            case 7u: staticRejectReason = "marking mode changed"; break;
            case 8u: staticRejectReason = "filter mode changed"; break;
            case 9u: staticRejectReason = "tap count changed"; break;
            case 10u: staticRejectReason = "resolution bias changed"; break;
            case 11u: staticRejectReason = "camera changed"; break;
            case 12u: staticRejectReason = "resources cleared"; break;
            case 13u: staticRejectReason = "cache invalid"; break;
            case 14u: staticRejectReason = "light direction changed"; break;
            case 15u: staticRejectReason = "light depth moved"; break;
            case 16u: staticRejectReason = "scene changed"; break;
            case 17u: staticRejectReason = "extent changed"; break;
            case 18u: staticRejectReason = "depth range changed"; break;
            case 19u: staticRejectReason = "light changed"; break;
            case 20u: staticRejectReason = "viewport changed"; break;
            case 21u: staticRejectReason = "jitter unsupported"; break;
            case 22u: staticRejectReason = "jitter warming"; break;
            case 23u: staticRejectReason = "scene revision changed"; break;
            case 26u: staticRejectReason = "distance clamp changed"; break;
            default: staticRejectReason = "unknown"; break;
            }
            FormatStatLine(
                m_SparseShadowStatLines[10],
                "Static request %s / drain %s:%u / visibility %s / reject %s",
                timings.staticPageRequestReuseActive ? "active" : "inactive",
                timings.staticPageDrainActive ? "active" : "inactive",
                timings.staticPageDrainFramesRemaining,
                timings.staticVisibilityReuseActive ? "active" : "inactive",
                staticRejectReason);
            FormatStatLine(
                m_SparseShadowStatLines[11],
                "Packet cull %s / tile hierarchy %s%s / scatter %s",
                timings.packetPageCullingActive ? "active" : "inactive",
                timings.hierarchicalScheduledPageMaskActive
                    ? "active"
                    : "inactive",
                timings.hierarchicalScheduledPageMaskUnavailable
                    ? "+unavailable"
                    : "",
                timings.dirtyPageScatterRasterActive
                    ? "active"
                    : "inactive");
            FormatStatLine(
                m_SparseShadowStatLines[12],
                "Draw lists %s:%s (%u packets) / source %s:%s (%u) / revision %s / batch %s%s / sort %s / level gate %s / packet fallback %s",
                timings.cachedShadowDrawListsRequested
                    ? "requested"
                    : "off",
                timings.cachedShadowDrawListsRebuilt
                    ? "rebuilt"
                    : timings.cachedShadowDrawListsReused
                        ? "reused"
                        : timings.cachedShadowDrawListsActive
                            ? "active"
                            : "idle",
                timings.cachedShadowDrawListPacketCount,
                timings.persistentCasterSourceRequested
                    ? "requested"
                    : "off",
                timings.persistentCasterSourceRebuilt
                    ? "rebuilt"
                    : timings.persistentCasterSourceReused
                        ? "reused"
                        : timings.persistentCasterSourceActive
                            ? "active"
                            : "fallback/idle",
                timings.persistentCasterSourceRecordCount,
                timings.casterOnlySceneRevisionActive
                    ? "caster-only"
                    : "global",
                timings.batchedDrawSupported
                    ? "supported"
                    : "unsupported",
                timings.batchedDrawActive ? "+active" : "",
                timings.packetStateSortingActive
                    ? "active"
                    : "inactive",
                timings.levelEmptyWorkSkipActive
                    ? "active"
                    : "inactive",
                timings.packetPageCullingUnavailable
                    ? "active"
                    : "inactive");
            FormatStatLine(
                m_SparseShadowStatLines[13],
                "Light %s%s / paired %s / static merge %s%s / retain %s / Z %s / bias +%u / recover %.2f / distance %s %.1fm:L%u%s",
                timings.movingLightUncachedActive
                    ? "uncached"
                    : "cached",
                timings.movingLightCacheTransitionActive
                    ? "+rebuild"
                    : "",
                timings.effectivePairedStaticDynamicDepth
                    ? "on"
                    : "off",
                timings.deferredStaticDepthMergeActive
                    ? "active"
                    : timings.deferredStaticDepthMergeRequested
                        ? "idle"
                        : "off",
                timings.deferredStaticDepthMergeUnavailable
                    ? "+unavailable"
                    : "",
                timings.physicalMappingRetentionActive
                    ? "on"
                    : "off",
                !timings.lightDepthOriginGuardBandRequested
                    ? "reference"
                    : timings.lightDepthOriginGuardBandRetained
                        ? "retained"
                        : "rebased",
                uint32_t(timings.effectiveResolutionBias),
                timings.movingLightLodRecoveryFactor,
                timings.receiverDistanceMipClampActive
                    ? "on"
                    : "off",
                timings.effectiveReceiverDistanceMipClampStart,
                timings.receiverDistanceMipClampMaximumLevel,
                timings.movingLightContinuousReceiverBiasActive
                    ? "+continuous"
                    : "");
            FormatStatLine(
                m_SparseShadowStatLines[14],
                "CPU All %.3f / Validate %.3f / Views %.3f ms",
                timings.totalCpuMilliseconds,
                timings.sceneValidationCpuMilliseconds,
                timings.clipmapUpdateCpuMilliseconds);
            if (timings.lastCompletedWorkTimingAvailable)
            {
                FormatStatLine(
                    m_SparseShadowStatLines[15],
                    "Last work GPU %.3f ms @ frame %llu / samples %llu",
                    timings.lastCompletedWorkTotalMilliseconds,
                    static_cast<unsigned long long>(
                        timings.lastCompletedWorkSourceFrame),
                    static_cast<unsigned long long>(
                        timings.completedWorkSampleCount));
            }
            else
            {
                FormatStatLine(
                    m_SparseShadowStatLines[15],
                    "Last work GPU unavailable");
            }
            FormatStatLine(
                m_SparseShadowStatLines[16],
                "GPU work submit serial %llu / zero-work streak %llu / total %llu",
                static_cast<unsigned long long>(
                    timings.gpuWorkSubmissionSerial),
                static_cast<unsigned long long>(
                    timings.staticZeroWorkFrameStreak),
                static_cast<unsigned long long>(
                    timings.staticZeroWorkFrameTotal));
            m_HasSparseShadowStatSnapshot = timings.active;
        }
        else
        {
            m_HasSparseShadowStatSnapshot = false;
        }

        if (snapshot.hasDiagnosticCsmTimings)
        {
            const DiagnosticCsmTimings& timings =
                snapshot.diagnosticCsmTimings;
            const DiagnosticCsmStats& stats =
                snapshot.diagnosticCsmStats;
            if (timings.gpuTimingSource ==
                DiagnosticCsmGpuTimingSource::Unavailable)
            {
                m_DiagnosticCsmStatLines[0] =
                    "GPU timing unavailable or warming";
            }
            else if (!timings.detailedGpuTimingEnabled)
            {
                FormatStatLine(
                    m_DiagnosticCsmStatLines[0],
                    "GPU All %.3f ms / total-only / age %u f",
                    timings.totalMilliseconds,
                    timings.gpuTimingAgeFrames);
            }
            else
            {
                FormatStatLine(
                    m_DiagnosticCsmStatLines[0],
                    "GPU All %.3f / Clear %.3f / Raster %.3f / Sample %.3f ms / age %u f",
                    timings.totalMilliseconds,
                    timings.clearUpdateMilliseconds,
                    timings.rasterMilliseconds,
                    timings.samplingMilliseconds,
                    timings.gpuTimingAgeFrames);
            }
            FormatStatLine(
                m_DiagnosticCsmStatLines[1],
                "CPU All %.3f / Setup %.3f / Cull %.3f / Record %.3f ms / GPU Cull %.3f ms (known zero)",
                timings.totalCpuMilliseconds,
                timings.setupCpuMilliseconds,
                timings.cullingCpuMilliseconds,
                timings.recordingCpuMilliseconds,
                timings.cullingGpuMilliseconds);
            FormatStatLine(
                m_DiagnosticCsmStatLines[2],
                    "%u x %u output / %u x %u x %u D%u maps / %.1f distance / %u fetches (%u comparisons)",
                stats.outputWidth,
                stats.outputHeight,
                    stats.shadowMapResolution,
                    stats.shadowMapResolution,
                    stats.cascadeCount,
                    stats.depthBitsPerTexel,
                    stats.maximumShadowDistance,
                stats.filterSampleCount,
                stats.filterComparisonCount);
            FormatStatLine(
                m_DiagnosticCsmStatLines[3],
                "Cascades %u reused / %u scrolled / %u dirty / %u redrawn / %u rects / %u receiver-scissored / draw lists %s %u hit %u miss / %u entries %u pairs",
                stats.reusedCascades,
                stats.scrolledCascades,
                stats.dirtyCascades,
                stats.redrawnCascades,
                stats.dirtyRectangleCount,
                stats.receiverRasterScissoredCascades,
                stats.cachedShadowDrawListsActive
                    ? "active"
                    : stats.cachedShadowDrawListsRequested
                        ? "warming/idle"
                        : "off",
                stats.cachedShadowDrawListHits,
                stats.cachedShadowDrawListMisses,
                stats.cachedShadowDrawListEntries,
                stats.cachedShadowDrawListCasterProjectionPairs);
            if (stats.submissionStatsAvailable)
            {
                FormatStatLine(
                    m_DiagnosticCsmStatLines[4],
                    "Casters %u coarse / %u radius-culled / %u hull-culled / %u candidate / %u rendered / %u alpha / route %u manual + %u IA / draws %u (%u alpha) / %.2f m triangles / translated %u draws %.2f m triangles",
                    stats.coarseCasterProjectionPairs,
                    stats.radiusCulledCasterProjectionPairs,
                    stats.accuratelyCulledCasterProjectionPairs,
                    stats.candidateCasterProjectionPairs,
                    stats.renderedCasterProjectionPairs,
                    stats.alphaTestedCasterProjectionPairs,
                    stats.manualCasterProjectionPairs,
                    stats.inputAssemblerCasterProjectionPairs,
                    stats.submittedDrawCalls,
                    stats.submittedAlphaTestedDrawCalls,
                    double(stats.submittedTriangles) / 1e6,
                    stats.submittedTranslationOnlyDrawCalls,
                    double(stats.submittedTranslationOnlyTriangles) / 1e6);
            }
            else
            {
                FormatStatLine(
                    m_DiagnosticCsmStatLines[4],
                    "Casters %u coarse / %u radius-culled / %u hull-culled / %u candidate / %u rendered / route %u manual + %u IA / submission stats not collected",
                    stats.coarseCasterProjectionPairs,
                    stats.radiusCulledCasterProjectionPairs,
                    stats.accuratelyCulledCasterProjectionPairs,
                    stats.candidateCasterProjectionPairs,
                    stats.renderedCasterProjectionPairs,
                    stats.manualCasterProjectionPairs,
                    stats.inputAssemblerCasterProjectionPairs);
            }
            constexpr double BytesPerMib = 1024.0 * 1024.0;
            const uint64_t totalPersistentBytes =
                stats.depthBytes +
                stats.visibilityBytes +
                stats.debugVisualizationBytes +
                stats.scrollingScratchBytes;
            FormatStatLine(
                m_DiagnosticCsmStatLines[5],
                "Texel work issued %.2f m logical / %.2f m updated / %.2f m copied / %.2f m cleared / %.2f m full-redraw scissor bounds (%.2f m excluded)",
                double(stats.logicalTexels) / 1e6,
                double(stats.updatedTexels) / 1e6,
                double(stats.copiedTexels) / 1e6,
                double(stats.clearedTexels) / 1e6,
                double(stats.fullRedrawRasterBoundTexels) / 1e6,
                double(stats.fullRedrawRasterExcludedTexels) / 1e6);
            FormatStatLine(
                m_DiagnosticCsmStatLines[6],
                "Memory %.2f mib total (depth %.2f / visibility+debug %.2f / scroll %.2f) / invalidation 0x%08x",
                double(totalPersistentBytes) / BytesPerMib,
                double(stats.depthBytes) / BytesPerMib,
                double(stats.visibilityBytes +
                    stats.debugVisualizationBytes) / BytesPerMib,
                double(stats.scrollingScratchBytes) / BytesPerMib,
                stats.invalidationMask);
            FormatStatLine(
                m_DiagnosticCsmStatLines[7],
                "Coverage %.3f fine / %.3f coarse; texel %.6f / %.6f; light depth %.3f requested / %.3f actual max",
                stats.finestCoverageExtent,
                stats.coarsestCoverageExtent,
                stats.finestWorldTexelSize,
                stats.coarsestWorldTexelSize,
                stats.maximumLightDepth,
                stats.maximumActualLightDepthSpan);
            FormatStatLine(
                m_DiagnosticCsmStatLines[8],
                "Gather %s / submit %s / caster transform %s%s / full-clear batch %s / %u walks / %u sorts; depth axis %s%s / saturated slope %s%s / algebraic slow %s%s / receiver light %s%s / receiver transform %s%s / hull %s%s / axes %s%s / shared light %s%s / receiver scissor %s%s / UE radius %.3f %s%s / cache-safe gating %s",
                stats.singleTraversalCasterClassificationEnabled
                    ? "one-pass active"
                    : stats.singleTraversalCasterClassificationRequested
                        ? "one-pass inactive"
                        : "per-cascade",
                stats.directCasterSubmissionEnabled
                    ? "direct"
                    : stats.directCasterSubmissionRequested
                        ? "direct inactive"
                        : "copy",
                stats.translationOnlyCasterTransformRequested
                    ? "translation requested"
                    : "legacy",
                stats.translationOnlyCasterTransformRequested &&
                    !stats.translationOnlyCasterTransformEnabled
                    ? "+inactive"
                    : "",
                stats.batchedFullRedrawClearActive
                    ? "active"
                    : stats.batchedFullRedrawClearRequested
                        ? "requested+inactive"
                        : "off",
                stats.casterSceneTraversals,
                stats.casterSorts,
                stats.precomputedDepthAxisInverseLengthRequested
                    ? "requested"
                    : "off",
                stats.precomputedDepthAxisInverseLengthRequested &&
                    !stats.precomputedDepthAxisInverseLengthEnabled
                    ? "+inactive"
                    : "",
                stats.conservativeSaturatedSlopeRequested
                    ? "requested"
                    : "off",
                stats.conservativeSaturatedSlopeRequested &&
                    !stats.conservativeSaturatedSlopeActive
                    ? "+inactive"
                    : "",
                stats.algebraicSlowSlopeRequested
                    ? "requested"
                    : "off",
                stats.algebraicSlowSlopeRequested &&
                    !stats.algebraicSlowSlopeActive
                    ? "+inactive"
                    : "",
                stats.preNormalizedReceiverLightDirectionRequested
                    ? "requested"
                    : "legacy",
                stats.preNormalizedReceiverLightDirectionRequested &&
                    !stats.preNormalizedReceiverLightDirectionEnabled
                    ? "+inactive"
                    : "",
                stats.precomposedClipToShadowRequested
                    ? "precomposed"
                    : "legacy",
                stats.precomposedClipToShadowRequested &&
                    !stats.precomposedClipToShadowEnabled
                    ? "+inactive"
                    : "",
                stats.accurateCasterCullingRequested ? "requested" : "off",
                stats.accurateCasterCullingRequested &&
                    !stats.accurateCasterCullingEnabled
                    ? "+gated"
                    : "",
                stats.precomputedReceiverHullAxesRequested
                    ? "requested"
                    : "off",
                stats.precomputedReceiverHullAxesRequested &&
                    !stats.precomputedReceiverHullAxesEnabled
                    ? "+inactive"
                    : "",
                stats.sharedCasterLightProjectionRequested
                    ? "requested"
                    : "off",
                stats.sharedCasterLightProjectionRequested &&
                    !stats.sharedCasterLightProjectionEnabled
                    ? "+inactive"
                    : "",
                stats.receiverRasterScissorRequested
                    ? "requested"
                    : "off",
                stats.receiverRasterScissorRequested &&
                    !stats.receiverRasterScissorEnabled
                    ? "+gated"
                    : "",
                stats.casterRadiusThreshold,
                stats.ueCasterRadiusThresholdRequested ? "requested" : "off",
                stats.ueCasterRadiusThresholdRequested &&
                    !stats.ueCasterRadiusThresholdEnabled
                    ? "+gated"
                    : "",
                (!stats.accurateCasterCullingRequested ||
                    stats.accurateCasterCullingEnabled) &&
                    (!stats.receiverRasterScissorRequested ||
                        stats.receiverRasterScissorEnabled) &&
                    (!stats.ueCasterRadiusThresholdRequested ||
                        stats.ueCasterRadiusThresholdEnabled)
                    ? "inactive"
                    : "active");

            if (snapshot.hasSparseShadowTimings &&
                snapshot.sparseShadowTimings.active)
            {
                const SparseVirtualShadowMapTimings& sparse =
                    snapshot.sparseShadowTimings;
                const auto extentMatches = [](float left,
                    float right, float leftTexel, float rightTexel) {
                    const float tolerance = 8.f * std::max(
                        std::abs(leftTexel), std::abs(rightTexel));
                    return std::isfinite(left) &&
                        std::isfinite(right) &&
                        std::abs(left - right) <= tolerance;
                };
                const bool coverageMatched = extentMatches(
                        stats.finestCoverageExtent,
                        sparse.comparisonFinestCoverageExtent,
                        stats.finestWorldTexelSize,
                        sparse.comparisonFinestWorldTexelSize) &&
                    extentMatches(
                        stats.coarsestCoverageExtent,
                        sparse.comparisonCoarsestCoverageExtent,
                        stats.coarsestWorldTexelSize,
                        sparse.comparisonCoarsestWorldTexelSize);
                const bool resolutionMatched =
                    stats.shadowMapResolution ==
                    sparse.comparisonVirtualResolution;
                const bool filterMatched =
                    stats.filter == DiagnosticCsmFilter::Poisson &&
                    stats.filterSampleCount ==
                        sparse.comparisonFilterSampleCount &&
                    stats.filterComparisonCount ==
                        sparse.comparisonFilterComparisonCount &&
                    std::abs(stats.filterRadiusTexels -
                        sparse.comparisonFilterRadiusTexels) <= 1e-5f &&
                    sparse.comparisonFilterMode ==
                        SvsmFilterMode::ManualPageSafe &&
                    !sparse.comparisonAdaptiveFiltering;
                const bool depthMatched =
                    std::abs(stats.maximumActualLightDepthSpan -
                        sparse.comparisonMaximumLightDepth) <= 1e-5f;
                const bool allMatched = coverageMatched &&
                    resolutionMatched && filterMatched && depthMatched;
                FormatStatLine(
                    m_DiagnosticCsmStatLines[9],
                    "SVSM tuple %s / coverage %s / resolution %s / filter %s / depth %s",
                    allMatched ? "matched" : "mismatch",
                    coverageMatched ? "yes" : "no",
                    resolutionMatched ? "yes" : "no",
                    filterMatched ? "yes" : "no",
                    depthMatched ? "yes" : "no");
            }
            else
            {
                m_DiagnosticCsmStatLines[9] =
                    "SVSM comparison unavailable; enable SVSM to validate a matched tuple";
            }
            const GpuTimingNormalizationEstimate normalizedTiming =
                NormalizeGpuTimingMilliseconds(
                    timings.totalMilliseconds,
                    snapshot.gpuMetrics,
                    snapshot.rendererName);
            if (timings.gpuTimingSource !=
                    DiagnosticCsmGpuTimingSource::Unavailable &&
                normalizedTiming.valid)
            {
                FormatStatLine(
                    m_DiagnosticCsmStatLines[10],
                    "Unofficial estimate %.3f ms @ %.1f clock TFLOPS / clock %.1f / utilized %.1f / grade %s / raw above",
                    normalizedTiming.estimatedMilliseconds,
                    normalizedTiming.referenceTFlops,
                    normalizedTiming.currentClockCapacityTFlops,
                    normalizedTiming.utilizedTFlops,
                    GetGpuTimingNormalizationGradeLabel(
                        normalizedTiming.grade));
            }
            else
            {
                m_DiagnosticCsmStatLines[10] =
                    "Unofficial clock-normalized estimate unavailable; raw GPU timing remains authoritative";
            }
            m_HasDiagnosticCsmStatSnapshot = timings.active;
        }
        else
        {
            m_HasDiagnosticCsmStatSnapshot = false;
        }

        if (snapshot.hasVisibilityTimings)
        {
            const ScreenSpaceVisibilityTimings* timings =
                &snapshot.visibilityTimings;
            const float traceMilliseconds =
                timings->depthHierarchyMs + timings->samplingMs;
            const float otherMilliseconds =
                timings->temporalMs + timings->compositionMs;
            FormatStatLine(
                m_VisibilityStatLines[0],
                "All %.1f / Trace %.1f / Filter %.1f / Other %.1f ms",
                timings->CompleteEffectMs(),
                traceMilliseconds,
                timings->filteringMs,
                otherMilliseconds);

            constexpr double BytesPerMib = 1024.0 * 1024.0;
            FormatStatLine(
                m_VisibilityStatLines[1],
                "Outputs %.1f / Working %.1f / Mask Cache %.1f mib",
                double(timings->outputTextureBytes) / BytesPerMib,
                double(timings->workingTextureBytes) / BytesPerMib,
                double(timings->maskCacheBytes) / BytesPerMib);
            FormatStatLine(
                m_VisibilityStatLines[2],
                "Avoided %.1f / Shared %.1f mib",
                double(timings->avoidedTextureBytes) / BytesPerMib,
                double(timings->sharedMaskPayloadBytes) / BytesPerMib);
            m_HasVisibilityStatSnapshot = true;
        }
        else
        {
            m_HasVisibilityStatSnapshot = false;
        }

        if (snapshot.hasTemporalAATimings)
        {
            const MiniEngineTemporalAATimings* timings =
                &snapshot.temporalAATimings;
            FormatStatLine(
                m_TemporalAAStatLines[0],
                "All %.1f / Blend %.1f / %s %.1f ms",
                timings->CompleteEffectMilliseconds(),
                timings->blendMilliseconds,
                timings->outputWasSharpened ? "Sharpen" : "Resolve",
                timings->outputMilliseconds);
            constexpr double BytesPerMib = 1024.0 * 1024.0;
            FormatStatLine(
                m_TemporalAAStatLines[1],
                "History %.1f mib",
                double(timings->historyTextureBytes) / BytesPerMib);
            m_HasTemporalAAStatSnapshot = true;
        }
        else
        {
            m_HasTemporalAAStatSnapshot = false;
        }
    }

    static void PushPanelSliderTrackStyle()
    {
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            ImVec4(0.018f, 0.016f, 0.020f, 0.72f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            ImVec4(0.13f, 0.13f, 0.14f, 0.76f));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgActive,
            ImVec4(0.18f, 0.18f, 0.19f, 0.82f));
    }

    static bool DrawSliderFloat(
        const char* label,
        float* value,
        float minimum,
        float maximum,
        const char* format = "%.3f",
        ImGuiSliderFlags flags = 0)
    {
        PushPanelSliderTrackStyle();
        const bool changed = ImGui::SliderFloat(
            label,
            value,
            minimum,
            maximum,
            format,
            flags);
        ImGui::PopStyleColor(3);
        return changed;
    }

    static bool DrawSliderInt(
        const char* label,
        int* value,
        int minimum,
        int maximum,
        const char* format = "%d",
        ImGuiSliderFlags flags = 0)
    {
        PushPanelSliderTrackStyle();
        const bool changed = ImGui::SliderInt(
            label,
            value,
            minimum,
            maximum,
            format,
            flags);
        ImGui::PopStyleColor(3);
        return changed;
    }

    static bool DrawSvsmSettingsSurface(
        SparseVirtualShadowMapSettings& shadows,
        float settingsControlWidth)
    {
        // These implementation booleans remain in the settings contract for
        // benchmark compatibility, but the UI exposes one authoritative state
        // for each policy so it cannot construct contradictory combinations.
        shadows.cachingEnabled =
            shadows.mode == SvsmMode::SparseCached;
        shadows.movingLightLodBiasEnabled =
            shadows.movingLightResolutionBias !=
                SvsmResolutionBias::Zero;
        if (shadows.dirtyPageScatterRasterEnabled)
        {
            shadows.dirtyPageScatterAmplificationGuardEnabled =
                true;
        }

        bool customChanged = false;

        const auto drawCheckbox = [&](
            const char* label,
            bool& value,
            bool available,
            const char* tooltip)
        {
            if (!available)
                ImGui::BeginDisabled();
            const bool changed = ImGui::Checkbox(label, &value);
            if (!available)
                ImGui::EndDisabled();
            if (tooltip != nullptr)
                ImGui::SetItemTooltip("%s", tooltip);
            return changed;
        };

        const auto drawCombo = [&](
            const char* label,
            int currentIndex,
            const char* const* labels,
            int labelCount,
            bool available,
            const char* tooltip)
        {
            int selectedIndex = -1;
            currentIndex = std::clamp(
                currentIndex,
                0,
                labelCount - 1);
            if (!available)
                ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(label, labels[currentIndex]))
            {
                for (int index = 0; index < labelCount; ++index)
                {
                    const bool selected = index == currentIndex;
                    if (ImGui::Selectable(labels[index], selected))
                        selectedIndex = index;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (!available)
                ImGui::EndDisabled();
            if (tooltip != nullptr)
                ImGui::SetItemTooltip("%s", tooltip);
            return selectedIndex;
        };

        const auto drawFloat = [&](
            const char* label,
            float& value,
            float minimum,
            float maximum,
            const char* format,
            bool available,
            const char* tooltip)
        {
            if (!available)
                ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(settingsControlWidth);
            const bool changed = DrawSliderFloat(
                label,
                &value,
                minimum,
                maximum,
                format);
            if (!available)
                ImGui::EndDisabled();
            if (tooltip != nullptr)
                ImGui::SetItemTooltip("%s", tooltip);
            return changed;
        };

        const auto sparseMode = [&]()
        {
            return shadows.mode != SvsmMode::DenseReference;
        };
        const auto cachedSparseMode = [&]()
        {
            return shadows.mode == SvsmMode::SparseCached;
        };
        const auto cacheReuseAvailable = [&]()
        {
            return cachedSparseMode() && shadows.cachingEnabled;
        };
        const auto finitePageBudget = [&]()
        {
            return shadows.pageRenderBudget !=
                std::numeric_limits<uint32_t>::max();
        };

        static const char* presetLabels[] = {
            "Performance",
            "Balanced",
            "Quality",
            "Custom"
        };
        const int selectedPreset = drawCombo(
            "Profile##SparseVirtualShadowMaps",
            int(shadows.preset),
            presetLabels,
            int(std::size(presetLabels)),
            true,
            "Choose the speed-to-quality balance. Named profiles reset "
            "Developer and Unabstracted controls while retaining "
            "the scene extent, light-depth range, pool size, and Enabled state.");
        if (selectedPreset >= 0)
        {
            ApplySvsmPreset(
                shadows,
                SvsmPreset(selectedPreset));
        }

        customChanged |= drawFloat(
            "First Clipmap Extent",
            shadows.firstClipmapExtent,
            1.f,
            500.f,
            "%.1f",
            true,
            "Set the world-space width covered by the finest clipmap.");
        customChanged |= drawFloat(
            "Maximum Light Depth",
            shadows.maximumLightDepth,
            1.f,
            2000.f,
            "%.1f",
            true,
            "Set the caster depth range centered on each clipmap.");

        static const char* filterKernelLabels[] = {
            "Nearest Poisson Reference",
            "Bilinear PCF"
        };
        const int selectedKernel = drawCombo(
            "Filter Kernel",
            int(shadows.filterKernel),
            filterKernelLabels,
            int(std::size(filterKernelLabels)),
            sparseMode(),
            sparseMode()
                ? "Choose the deterministic nearest reference or bilinear "
                  "page-safe PCF."
                : "Dense Reference retains its point-load receiver.");
        if (selectedKernel >= 0)
        {
            shadows.filterKernel = SvsmFilterKernel(selectedKernel);
            customChanged = true;
        }

        static const char* tapLabels[] = {
            "1",
            "4",
            "8",
            "16"
        };
        static const SvsmTapCount tapValues[] = {
            SvsmTapCount::One,
            SvsmTapCount::Four,
            SvsmTapCount::Eight,
            SvsmTapCount::Sixteen
        };
        int currentTapIndex = 0;
        for (int index = 0; index < int(std::size(tapValues)); ++index)
        {
            if (tapValues[index] == shadows.tapCount)
                currentTapIndex = index;
        }
        const int selectedTap = drawCombo(
            "Filter Taps",
            currentTapIndex,
            tapLabels,
            int(std::size(tapLabels)),
            true,
            "Select the page-safe 1, 4, 8, or 16-tap receiver.");
        if (selectedTap >= 0)
        {
            shadows.tapCount = tapValues[selectedTap];
            customChanged = true;
        }
        customChanged |= drawCheckbox(
            "Adaptive Filtering",
            shadows.adaptiveFiltering,
            shadows.tapCount != SvsmTapCount::One,
            "Use a page-safe agreement probe set before the selected full "
            "filter. One-tap filtering has no adaptive shortcut.");

        static const char* biasLabels[] = {
            "0",
            "+1 Mip",
            "+2 Mips"
        };
        const int selectedBias = drawCombo(
            "Resolution Bias",
            int(shadows.resolutionBias),
            biasLabels,
            int(std::size(biasLabels)),
            true,
            "Apply the same clipmap bias to marking, allocation, resolve, "
            "fallback, dense clipmap drawing, and diagnostics.");
        if (selectedBias >= 0)
        {
            shadows.resolutionBias = SvsmResolutionBias(selectedBias);
            customChanged = true;
        }

        customChanged |=
            drawCheckbox("Receiver Distance Mip Clamp",
                         shadows.receiverDistanceMipClampEnabled,
                         sparseMode(),
                         "Prevent distant receivers from requesting unnecessarily fine "
                         "clipmaps while retaining the complete coarsest fallback.");

        if (BeginAnimatedTreeNode("Developer Options##SparseVirtualShadowMaps"))
        {
            const bool movingLightPolicyAvailable =
                cacheReuseAvailable() && shadows.movingLightUncachedEnabled;
            const bool movingBiasActive =
                movingLightPolicyAvailable &&
                shadows.movingLightLodBiasEnabled &&
                shadows.movingLightResolutionBias != SvsmResolutionBias::Zero;
            const bool receiverDistanceClampAvailable =
                sparseMode() && shadows.receiverDistanceMipClampEnabled;
            const bool packetCullingAvailable =
                sparseMode() && shadows.gpuGatedDrawSubmission;
            const bool packetPageCullingActive =
                packetCullingAvailable && shadows.packetPageCullingEnabled;
            const bool dirtyScatterAvailable = packetPageCullingActive;

            const bool resourcesAndCacheOpen =
                ImGui::TreeNodeEx("Resources And Cache Policy##SparseVirtualShadowMaps");
            ImGui::SetItemTooltip(
                "Configure sparse pool capacity, scheduling limits, cache mode, "
                "paired depth, eviction, and reusable draw lists.");
            if (resourcesAndCacheOpen)
            {
                if (sparseMode())
                {
                    int physicalPageCount = int(shadows.physicalPageCount);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (DrawSliderInt("Physical Pool Pages",
                                      &physicalPageCount,
                                      64,
                                      int(SvsmPagesPerClipmap)))
                    {
                        shadows.physicalPageCount = uint32_t(physicalPageCount);
                        if (finitePageBudget())
                        {
                            shadows.pageRenderBudget = std::min(
                                shadows.pageRenderBudget, shadows.physicalPageCount);
                        }
                        customChanged = true;
                    }
                    ImGui::SetItemTooltip("Set the logical size of the shared fixed "
                                          "physical page pool.");

                    static uint32_t rememberedFinitePageBudget = 256u;
                    if (finitePageBudget())
                    {
                        rememberedFinitePageBudget =
                            std::min(shadows.pageRenderBudget, shadows.physicalPageCount);
                    }
                    bool unlimitedBudget = !finitePageBudget();
                    if (ImGui::Checkbox("Unlimited Page Render Budget", &unlimitedBudget))
                    {
                        if (unlimitedBudget)
                        {
                            shadows.pageRenderBudget =
                                std::numeric_limits<uint32_t>::max();
                        }
                        else
                        {
                            shadows.pageRenderBudget = std::min(
                                rememberedFinitePageBudget, shadows.physicalPageCount);
                        }
                        customChanged = true;
                    }
                    ImGui::SetItemTooltip(
                        "Remove the finite dirty-page scheduling limit. Returning "
                        "to a finite budget restores the last finite value.");

                    if (!unlimitedBudget)
                    {
                        int pageBudget = int(std::min(shadows.pageRenderBudget,
                                                      shadows.physicalPageCount));
                        ImGui::SetNextItemWidth(settingsControlWidth);
                        if (DrawSliderInt("Page Render Budget",
                                          &pageBudget,
                                          0,
                                          int(shadows.physicalPageCount)))
                        {
                            shadows.pageRenderBudget = uint32_t(pageBudget);
                            rememberedFinitePageBudget = uint32_t(pageBudget);
                            customChanged = true;
                        }
                        ImGui::SetItemTooltip(
                            "Limit dirty-page scheduling across the fine clipmaps.");
                    }
                }
                else
                {
                    ImGui::TextDisabled(
                        "Dense Reference does not use a physical page pool.");
                }

                customChanged |= drawCheckbox(
                    "Paired Static/Dynamic Depth",
                    shadows.pairedStaticDynamicDepthEnabled,
                    cacheReuseAvailable(),
                    "Use one page identity with persistent static depth and a "
                    "receiver-visible merged slice. This doubles physical-depth "
                    "pool memory and therefore requires Sparse Cached mode.");

                ImGui::Separator();

                static const char* modeLabels[] = {
                    "Dense Reference", "Sparse Uncached", "Sparse Cached"};
                const int selectedMode =
                    drawCombo("Mode##SparseVirtualShadowMaps",
                              int(shadows.mode),
                              modeLabels,
                              int(std::size(modeLabels)),
                              true,
                              "Select fully backed validation, sparse full redraw, or "
                              "sparse cached page reuse. Cache state follows this mode.");
                if (selectedMode >= 0)
                {
                    shadows.mode = SvsmMode(selectedMode);
                    shadows.cachingEnabled = shadows.mode == SvsmMode::SparseCached;
                    customChanged = true;
                }

                customChanged |= drawCheckbox(
                    "Include Coarsest in Page Budget",
                    shadows.coarsestPageRenderBudgetEnabled,
                    sparseMode() && finitePageBudget(),
                    "Apply the finite shared render reservation to all six "
                    "clipmaps instead of preserving the complete coarse fallback.");

                static const char* markingLabels[] = {
                    "Per Pixel", "8 by 8 Tile", "16 by 16 Tile"};
                const int selectedMarking = drawCombo(
                    "Page Marking",
                    int(shadows.markingMode),
                    markingLabels,
                    int(std::size(markingLabels)),
                    sparseMode(),
                    "Choose exact per-pixel requests or conservative deduplicated "
                    "tile marking.");
                if (selectedMarking >= 0)
                {
                    shadows.markingMode = SvsmMarkingMode(selectedMarking);
                    customChanged = true;
                }

                customChanged |=
                    drawCheckbox("Recent Page Eviction Grace",
                                 shadows.recentPageEvictionGraceEnabled,
                                 cacheReuseAvailable(),
                                 "Protect recently used pages from immediate fixed-pool "
                                 "eviction.");
                customChanged |=
                    drawCheckbox("Cached Shadow Draw Lists",
                                 shadows.renderPacketCachingEnabled,
                                 sparseMode(),
                                 "Reuse compatible conservative per-clipmap caster "
                                 "packet lists.");

                ImGui::TreePop();
            }

            const bool movementAndInvalidationOpen =
                ImGui::TreeNodeEx("Movement And Invalidation##SparseVirtualShadowMaps");
            ImGui::SetItemTooltip(
                "Tune moving-light recovery, receiver-distance clamping, and "
                "localized caster invalidation policy.");
            if (movementAndInvalidationOpen)
            {
                static const char* movingBiasLabels[] = {"Off", "+1 Mip", "+2 Mips"};
                const int effectiveMovingBias =
                    shadows.movingLightLodBiasEnabled
                        ? int(shadows.movingLightResolutionBias)
                        : 0;
                const int selectedMovingBias = drawCombo(
                    "Moving-Light Resolution Bias",
                    effectiveMovingBias,
                    movingBiasLabels,
                    int(std::size(movingBiasLabels)),
                    movingLightPolicyAvailable,
                    "Temporarily coarsen moving-light work, then recover. Off is "
                    "the exact no-bias path.");
                if (selectedMovingBias >= 0)
                {
                    shadows.movingLightResolutionBias =
                        SvsmResolutionBias(selectedMovingBias);
                    shadows.movingLightLodBiasEnabled = selectedMovingBias != 0;
                    customChanged = true;
                }

                int movingLightRecoveryFrames = int(shadows.movingLightLodRecoveryFrames);
                if (!movingBiasActive)
                    ImGui::BeginDisabled();
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawSliderInt("Moving-Light Recovery Frames",
                                  &movingLightRecoveryFrames,
                                  0,
                                  60))
                {
                    shadows.movingLightLodRecoveryFrames =
                        uint32_t(movingLightRecoveryFrames);
                    customChanged = true;
                }
                if (!movingBiasActive)
                    ImGui::EndDisabled();
                ImGui::SetItemTooltip(
                    "Hold the selected moving-light bias, then recover after this "
                    "many successful sparse frames.");

                if (!receiverDistanceClampAvailable)
                    ImGui::BeginDisabled();
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (ImGui::DragFloat("Distance Clamp Start x Extent",
                                     &shadows.receiverDistanceMipClampStartScale,
                                     0.05f,
                                     0.25f,
                                     8.f,
                                     "%.2f"))
                {
                    customChanged = true;
                }
                int receiverDistanceMaximumLevel =
                    int(shadows.receiverDistanceMipClampMaximumLevel);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawSliderInt("Maximum Distance Clamp Level",
                                  &receiverDistanceMaximumLevel,
                                  0,
                                  int(SvsmMaximumReceiverDistanceMipClampLevel)))
                {
                    shadows.receiverDistanceMipClampMaximumLevel =
                        uint32_t(receiverDistanceMaximumLevel);
                    customChanged = true;
                }
                if (!receiverDistanceClampAvailable)
                    ImGui::EndDisabled();
                ImGui::SetItemTooltip(
                    "Set the first distance threshold and maximum fine-clipmap "
                    "clamp level.");

                const bool adaptiveCasterClassificationAvailable =
                    cacheReuseAvailable() && shadows.localizedInvalidationEnabled &&
                    shadows.pairedStaticDynamicDepthEnabled;
                customChanged |= drawCheckbox(
                    "Adaptive Static Caster Cache",
                    shadows.adaptiveCasterCacheClassificationEnabled,
                    adaptiveCasterClassificationAvailable,
                    "Demote changed rigid opaque casters immediately and promote "
                    "them back to persistent static depth after stabilization.");

                static const char* invalidationModeLabels[] = {
                    "Auto", "Always", "Rigid", "Static"};
                const int selectedInvalidationMode = drawCombo(
                    "Object Invalidation Mode",
                    int(shadows.defaultObjectInvalidationMode),
                    invalidationModeLabels,
                    int(std::size(invalidationModeLabels)),
                    cacheReuseAvailable() && shadows.localizedInvalidationEnabled,
                    "Choose the default per-object transform and deformation "
                    "invalidation policy.");
                if (selectedInvalidationMode >= 0)
                {
                    shadows.defaultObjectInvalidationMode =
                        SvsmObjectInvalidationMode(selectedInvalidationMode);
                    customChanged = true;
                }

                ImGui::TreePop();
            }

            const bool cullingAndRasterOpen =
                ImGui::TreeNodeEx("Culling And Raster##SparseVirtualShadowMaps");
            ImGui::SetItemTooltip(
                "Tune packet rejection, scheduled-page masks, static-depth "
                "occlusion, and dirty-page raster.");
            if (cullingAndRasterOpen)
            {
                customChanged |= drawCheckbox(
                    "Packet State Sorting",
                    shadows.packetStateSortingEnabled,
                    sparseMode() &&
                        shadows.gpuGatedDrawSubmission &&
                        shadows.batchedDrawSubmissionEnabled,
                    "Group compatible packet state before indirect submission.");
                customChanged |= drawCheckbox("Packet Page Culling",
                                              shadows.packetPageCullingEnabled,
                                              packetCullingAvailable,
                                              "Intersect each cached caster packet "
                                              "with scheduled dirty work.");
                customChanged |= drawCheckbox(
                    "Hierarchical Scheduled-Page Mask",
                    shadows.hierarchicalScheduledPageMaskEnabled,
                    packetPageCullingActive,
                    "Reject packet rectangles against an exact-validated coarse "
                    "scheduled-page hierarchy.");
                customChanged |= drawCheckbox(
                    "Receiver Subpage Mask Culling",
                    shadows.receiverPageMaskCullingEnabled,
                    packetPageCullingActive,
                    "Cull caster/page work outside the current receiver subpage "
                    "mask.");

                const bool staticDepthHierarchyAvailable =
                    packetPageCullingActive && cacheReuseAvailable() &&
                    shadows.pairedStaticDynamicDepthEnabled &&
                    !shadows.dirtyPageScatterRasterEnabled;
                customChanged |= drawCheckbox(
                    "Static-Depth Page HZB Culling",
                    shadows.staticDepthHierarchyCullingEnabled,
                    staticDepthHierarchyAvailable,
                    "Reject only fully occluded dynamic caster/page pairs against "
                    "the complete persistent static page hierarchy.");
                customChanged |=
                    drawFloat("Static HZB Conservative Bias",
                              shadows.staticDepthHierarchyBias,
                              0.f,
                              0.01f,
                              "%.6f",
                              staticDepthHierarchyAvailable &&
                                  shadows.staticDepthHierarchyCullingEnabled,
                              "Increase the reverse-Z fail-open guard used by "
                              "static HZB culling.");

                bool dirtyScatterEnabled = shadows.dirtyPageScatterRasterEnabled;
                if (drawCheckbox(
                        "Dirty Page Scatter Raster",
                        dirtyScatterEnabled,
                        dirtyScatterAvailable,
                        "Request one virtual-space draw per intersecting packet. "
                        "The amplification guard is intrinsic and falls back to "
                        "the exact page list when a rectangle covers too many "
                        "holes."))
                {
                    shadows.dirtyPageScatterRasterEnabled = dirtyScatterEnabled;
                    if (dirtyScatterEnabled)
                    {
                        shadows.dirtyPageScatterAmplificationGuardEnabled = true;
                    }
                    customChanged = true;
                }
                int scatterMaximumAmplification =
                    int(shadows.dirtyPageScatterMaximumAmplification);
                if (!dirtyScatterAvailable || !shadows.dirtyPageScatterRasterEnabled)
                {
                    ImGui::BeginDisabled();
                }
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawSliderInt("Scatter Maximum Page Amplification",
                                  &scatterMaximumAmplification,
                                  1,
                                  int(SvsmMaximumDirtyPageScatterAmplification)))
                {
                    shadows.dirtyPageScatterMaximumAmplification =
                        uint32_t(scatterMaximumAmplification);
                    customChanged = true;
                }
                if (!dirtyScatterAvailable || !shadows.dirtyPageScatterRasterEnabled)
                {
                    ImGui::EndDisabled();
                }
                ImGui::SetItemTooltip(
                    "Fall back to the exact per-page packet list above this "
                    "rectangle-to-exact-page amplification ratio.");

                ImGui::TreePop();
            }

            const bool unabstractedOpen =
                ImGui::TreeNodeEx("Unabstracted##SparseVirtualShadowMaps");
            ImGui::SetItemTooltip(
                "Raw independently toggleable optimizations that are proven "
                "enough to trend toward an abstracted always-on policy, but "
                "retain their reference paths during validation.");
            if (unabstractedOpen)
            {
                customChanged |=
                    drawCheckbox("Allocation Budget Saturation Early-Out",
                                 shadows.allocationBudgetSaturationEarlyOutEnabled,
                                 sparseMode() && finitePageBudget(),
                                 "Bypass redundant reservation atomics after the shared "
                                 "finite budget is saturated.");
                customChanged |= drawCheckbox(
                    "Deduplicate Per-Pixel Requests",
                    shadows.perPixelMarkingDedupeEnabled,
                    sparseMode() && shadows.markingMode == SvsmMarkingMode::PerPixel,
                    "Deduplicate each 8-by-8 group's page requests through a "
                    "bounded shared hash that fails open on collisions.");

                static const char* poissonOrderingLabels[] = {"Legacy Stride Reference",
                                                              "Balanced Progressive"};
                const int selectedPoissonOrdering = drawCombo(
                    "Poisson Ordering",
                    int(shadows.poissonOrdering),
                    poissonOrderingLabels,
                    int(std::size(poissonOrderingLabels)),
                    sparseMode(),
                    "Choose the legacy stride or balanced progressive tap prefix.");
                if (selectedPoissonOrdering >= 0)
                {
                    shadows.poissonOrdering =
                        SvsmPoissonOrdering(selectedPoissonOrdering);
                    customChanged = true;
                }

                static const char* filteringLabels[] = {"Manual Page Safe", "Hybrid"};
                const int selectedFiltering =
                    drawCombo("Filtering",
                              int(shadows.filterMode),
                              filteringLabels,
                              int(std::size(filteringLabels)),
                              sparseMode(),
                              "Hybrid filtering reuses one translation only when the "
                              "complete footprint stays in one valid page.");
                if (selectedFiltering >= 0)
                {
                    shadows.filterMode = SvsmFilterMode(selectedFiltering);
                    customChanged = true;
                }

                customChanged |=
                    drawCheckbox("Precomposed Clipmap Transforms",
                                 shadows.precomposedClipmapTransformsEnabled,
                                 sparseMode(),
                                 "Transform camera-clip positions directly into each "
                                 "clipmap in marking and resolve.");
                customChanged |=
                    drawCheckbox("Page Translation Cache",
                                 shadows.pageTranslationCachingEnabled,
                                 sparseMode(),
                                 "Reuse exact page-table translations within one pixel's "
                                 "validated filter footprint.");

                customChanged |= drawCheckbox(
                    "Light-Depth Origin Guard Band",
                    shadows.lightDepthOriginGuardBandEnabled,
                    cacheReuseAvailable(),
                    "Keep the light-depth origin stable until the configured "
                    "scene-depth guard band is exceeded.");
                customChanged |= drawFloat(
                    "Light-Depth Guard Fraction",
                    shadows.lightDepthOriginGuardBandFraction,
                    0.1f,
                    1.f,
                    "%.2f",
                    cacheReuseAvailable() && shadows.lightDepthOriginGuardBandEnabled,
                    "Set the usable fraction of the cached light-depth range.");
                customChanged |=
                    drawCheckbox("Finite-Budget Static Drain",
                                 shadows.finiteBudgetStaticDrainEnabled,
                                 cacheReuseAvailable() && finitePageBudget(),
                                 "Drain persistent static-dirty work without starving "
                                 "dynamic fine pages under a finite budget.");
                customChanged |= drawCheckbox(
                    "Static Page Request Reuse",
                    shadows.staticPageRequestReuseEnabled,
                    cacheReuseAvailable(),
                    "Reuse validated static page requests while camera, light, "
                    "and scene mapping remain compatible.");
                customChanged |=
                    drawCheckbox("Static Visibility Cache",
                                 shadows.staticVisibilityCachingEnabled,
                                 cacheReuseAvailable(),
                                 "Reuse the full-resolution visibility result when all "
                                 "receiver and shadow inputs remain compatible.");
                customChanged |=
                    drawCheckbox("Scene State Caching",
                                 shadows.sceneStateCachingEnabled,
                                 true,
                                 "Reuse validated scene revisions until UVSR reports a "
                                 "shadow-relevant scene change.");
                customChanged |=
                    drawCheckbox("Caster-Only Scene Revision",
                                 shadows.casterOnlySceneRevisionEnabled,
                                 shadows.sceneStateCachingEnabled,
                                 "Distinguish caster changes from light-only transforms "
                                 "using Donut's dirty scene branches.");
                customChanged |= drawCheckbox(
                    "Shared Six-Clipmap Packet Builder",
                    shadows.sharedClipmapPacketBuilderEnabled,
                    sparseMode(),
                    "Traverse compatible casters once and classify the shared "
                    "metadata across all six clipmaps.");
                customChanged |= drawCheckbox(
                    "Persistent Caster Source Cache",
                    shadows.persistentCasterSourceCachingEnabled,
                    sparseMode() && shadows.sharedClipmapPacketBuilderEnabled,
                    "Reuse validated caster source references, transforms, "
                    "bounds, topology, materials, and draw arguments.");
                customChanged |= drawCheckbox(
                    "Opaque Raster Specialization",
                    shadows.opaqueRasterSpecializationEnabled,
                    sparseMode(),
                    "Use the position-only, material-free opaque depth path.");
                customChanged |=
                    drawCheckbox("Lean Alpha-Tested Bindings",
                                 shadows.leanAlphaTestedBindingsEnabled,
                                 sparseMode(),
                                 "Bind only shadow-relevant alpha material resources.");
                customChanged |= drawCheckbox(
                    "Deferred Static-Depth Merge",
                    shadows.deferredStaticDepthMergeEnabled,
                    cacheReuseAvailable() && shadows.pairedStaticDynamicDepthEnabled,
                    "Merge scheduled static-dirty pages after static raster "
                    "instead of issuing duplicate static depth atomics.");
                customChanged |=
                    drawCheckbox("Moving-Light Uncached Policy",
                                 shadows.movingLightUncachedEnabled,
                                 cachedSparseMode(),
                                 "Use the receiver-visible merged slice while the light "
                                 "moves, then rebuild and resume compatible caching.");
                customChanged |= drawCheckbox(
                    "Preserve Page Mappings On Content Invalidation",
                    shadows.retainPhysicalMappingsOnContentInvalidationEnabled,
                    sparseMode(),
                    "Retain validated physical ownership across logical content "
                    "invalidation; resource recreation remains destructive.");
                customChanged |= drawCheckbox(
                    "Continuous Moving-Light Distance Bias",
                    shadows.movingLightContinuousReceiverBiasEnabled,
                    receiverDistanceClampAvailable && movingBiasActive,
                    "Shift distance thresholds continuously during moving-light "
                    "recovery instead of globally dropping a clipmap.");
                customChanged |= drawCheckbox(
                    "Localized Caster Invalidation",
                    shadows.localizedInvalidationEnabled,
                    cacheReuseAvailable(),
                    "Dirty only conservative old-plus-new virtual coverage for "
                    "reliable stable caster identities.");
                customChanged |= drawCheckbox(
                    "Tight Localized Bounds",
                    shadows.tightLocalizedInvalidationBoundsEnabled,
                    cacheReuseAvailable() && shadows.localizedInvalidationEnabled,
                    "Project original object-space bounds directly and fail "
                    "open when identity or bounds are unreliable.");
                customChanged |=
                    drawCheckbox("GPU-Gated Draw Submission",
                                 shadows.gpuGatedDrawSubmission,
                                 sparseMode(),
                                 "Let compact GPU work determine which prepared caster "
                                 "packets reach raster.");
                customChanged |= drawCheckbox(
                    "Batched Draw Submission",
                    shadows.batchedDrawSubmissionEnabled,
                    sparseMode() && shadows.gpuGatedDrawSubmission,
                    "Reuse compatible state and submit compact indirect draw "
                    "batches.");
                customChanged |=
                    drawCheckbox("Per-Level Empty-Work Skip",
                                 shadows.levelEmptyWorkSkipEnabled,
                                 sparseMode() && shadows.gpuGatedDrawSubmission &&
                                     shadows.batchedDrawSubmissionEnabled,
                                 "Skip parsing indirect commands for clipmap levels with "
                                 "zero dirty work.");
                customChanged |= drawCheckbox(
                    "Packet Rectangle Direct Scan",
                    shadows.packetRectangleDirectScanEnabled,
                    packetPageCullingActive,
                    "Probe small wrapped packet rectangles directly instead of "
                    "scanning the full compact dirty-page list.");
                customChanged |= drawCheckbox(
                    "Scatter Alpha-Test Early Reject",
                    shadows.scatterAlphaTestEarlyRejectEnabled,
                    dirtyScatterAvailable && shadows.dirtyPageScatterRasterEnabled,
                    "Reject unscheduled scatter holes before sampling opacity.");

                ImGui::TreePop();
            }

            EndAnimatedTreeNode();
        }

        return customChanged;
    }

    static bool DrawCenteredActionButton(const char* label, float width)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 size(width, ImGui::GetFrameHeight());
        ImGui::PushID(label);
        const bool pressed = ImGui::Button("##ActionButton", size);
        ImGui::PopID();
        const ImVec2 buttonMin = ImGui::GetItemRectMin();
        const ImVec2 buttonMax = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        const ImVec2 textSize = ImGui::CalcTextSize(label);
        const ImVec2 textPosition(
            std::floor(buttonMin.x + (buttonMax.x - buttonMin.x - textSize.x) * 0.5f),
            std::floor(buttonMin.y + (buttonMax.y - buttonMin.y - textSize.y) * 0.5f +
                       1.f));
        drawList->AddText(textPosition, ImGui::GetColorU32(ImGuiCol_Text), label);
        return pressed;
    }

  public:
    UIRenderer(DeviceManager* deviceManager,
               std::shared_ptr<UvsrSceneViewer> app,
               UIData& ui,
               std::chrono::steady_clock::time_point programLaunchTime)
        : ImGui_Renderer(deviceManager), m_app(app),
          m_ProgramLaunchTime(programLaunchTime), m_ui(ui)
    {
        m_Font = CreateFontFromFile(
            *(app->GetRootFs()), "/media/fonts/System/CodexUI-Semibold.ttf", 16.f);

        ImGui::GetIO().IniFilename = nullptr;
    }

    bool Init(std::shared_ptr<ShaderFactory> shaderFactory)
    {
        if (!ImGui_Renderer::Init(shaderFactory))
            return false;

        m_BackdropBlurPass = std::make_unique<BackdropBlurPass>(
            GetDevice(), shaderFactory, m_app->GetCommonPasses());
        return true;
    }

    virtual void Render(nvrhi::IFramebuffer* framebuffer) override
    {
        if (!imgui_nvrhi)
            return;

        buildUI();
        ImGui::Render();
        if (m_BackdropBlurPass)
        {
            m_BackdropBlurPass->Render(
                framebuffer,
                UiBackgroundBlurPixels,
                m_ui.BackdropRects);
        }
        imgui_nvrhi->render(framebuffer);
        m_imguiFrameOpened = false;
    }

    virtual void BackBufferResizing() override
    {
        if (m_BackdropBlurPass)
            m_BackdropBlurPass->BackBufferResizing();
        ImGui_Renderer::BackBufferResizing();
    }

protected:
    virtual bool KeyboardUpdate(
        int key,
        int scancode,
        int action,
        int mods) override
    {
        const bool captured = ImGui_Renderer::KeyboardUpdate(
            key, scancode, action, mods);
        const bool plainMaterialEditorShortcut =
            (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0;
        if (key == GLFW_KEY_M &&
            action == GLFW_PRESS &&
            plainMaterialEditorShortcut &&
            !ImGui::GetIO().WantTextInput)
        {
            m_ui.ShowMaterialEditor = !m_ui.ShowMaterialEditor;
            return true;
        }

        return captured;
    }

    virtual void buildUI(void) override
    {
        for (UiBackdropRect& backdropRect : m_ui.BackdropRects)
            backdropRect.visible = false;

        const WorldMaterialViewAvailability worldMaterialAvailability = {
            m_ui.EnablePbr,
            m_ui.UsesDeferredShading(),
            m_ui.ScreenSpaceVisibility.enabled,
            m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse()
        };
        WorldMaterialViewState worldMaterialState =
            NormalizeWorldMaterialViewState(
                {
                    uint32_t(m_ui.WhiteWorld),
                    m_ui.ScreenSpaceVisibility.showIndirectDiffuseOnly
                },
                worldMaterialAvailability);
        m_ui.ScreenSpaceVisibility.showIndirectDiffuseOnly =
            worldMaterialState.showIndirectDiffuseOnly;
        const WorldMaterialView selectedWorldMaterial =
            ResolveWorldMaterialView(
                worldMaterialState,
                worldMaterialAvailability);

        if (!m_ui.ShowUI)
            return;

        ApplyReferenceStyle();

        int width, height;
        GetDeviceManager()->GetWindowDimensions(width, height);

        const bool sceneLoading = m_app->IsSceneLoading();
        if (sceneLoading)
        {
            if (!m_WasSceneLoading)
            {
                m_DisplayedLoadingProgress = 0.f;
                m_WasSceneLoading = true;
                m_DisplayedFrameTime = 0.0;
                m_DisplayedGpuBandwidthGBps = 0.0;
                m_DisplayedGpuTFlops = 0.0;
                m_StatSnapshotElapsed = 0.0;
                m_StatFrameTimeSum = 0.0;
                m_StatFrameTimeCount = 0;
                m_StatUpdateQueue.clear();
                for (std::string& value : m_PerformanceStatValues)
                    value.clear();
                for (std::string& value : m_BendShadowStatLines)
                    value.clear();
                for (std::string& value : m_SparseShadowStatLines)
                    value.clear();
                for (std::string& value : m_DiagnosticCsmStatLines)
                    value.clear();
                for (std::string& value : m_VisibilityStatLines)
                    value.clear();
                for (std::string& value : m_TemporalAAStatLines)
                    value.clear();
                m_HasAppliedStatSnapshot = false;
                m_HasGpuStatSnapshot = false;
                m_HasBendShadowStatSnapshot = false;
                m_HasSparseShadowStatSnapshot = false;
                m_HasDiagnosticCsmStatSnapshot = false;
                m_HasVisibilityStatSnapshot = false;
                m_HasTemporalAAStatSnapshot = false;
            }

            BeginFullScreenWindow();
            ImGui::PushFont(m_Font->GetScaledFont());
            ApplyExpandedWordSpacing(
                m_AdjustedSpaceFontBakedId,
                m_BaseSpaceAdvance);

            const auto& stats = Scene::GetLoadingStats();
            const uint32_t objectsLoaded = stats.ObjectsLoaded.load();
            const uint32_t objectsTotal = std::max(
                stats.ObjectsTotal.load(),
                objectsLoaded);
            const uint64_t importStepsCompleted =
                stats.ImportStepsCompleted.load();
            const uint64_t importStepsTotal = std::max(
                stats.ImportStepsTotal.load(),
                importStepsCompleted);
            const uint32_t texturesDecoded =
                m_app->GetTextureCache()->GetNumberOfLoadedTextures();
            const uint32_t texturesReady =
                m_app->GetTextureCache()->GetNumberOfFinalizedTextures();
            const uint32_t texturesTotal = std::max(
                m_app->GetTextureCache()->GetNumberOfRequestedTextures(),
                std::max(texturesDecoded, texturesReady));
            const double programLaunchMilliseconds =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() -
                    m_ProgramLaunchTime)
                    .count();

            char messageBuffer[512];
            const std::string sceneDisplayName =
                m_app->GetCurrentSceneDisplayName();
            snprintf(
                messageBuffer,
                std::size(messageBuffer),
                "Loading scene %s, please wait...\n"
                "Launch: %.1fms\n"
                "Objects: %u/%u / Import steps: %llu/%llu / "
                "Textures decoded: %u/%u / GPU ready: %u/%u",
                sceneDisplayName.c_str(),
                programLaunchMilliseconds,
                objectsLoaded,
                objectsTotal,
                static_cast<unsigned long long>(importStepsCompleted),
                static_cast<unsigned long long>(importStepsTotal),
                texturesDecoded,
                texturesTotal,
                texturesReady,
                texturesTotal);
            DrawScreenCenteredText(messageBuffer);

            const float objectProgress = objectsTotal > 0
                ? float(objectsLoaded) / float(objectsTotal)
                : 0.f;
            const float importProgress = importStepsTotal > 0
                ? float(importStepsCompleted) / float(importStepsTotal)
                : objectProgress;
            const float textureDecodeProgress = texturesTotal > 0
                ? float(texturesDecoded) / float(texturesTotal)
                : 0.f;
            const float textureReadyProgress = texturesTotal > 0
                ? float(texturesReady) / float(texturesTotal)
                : 0.f;

            // Real importer milestones own the first 45%, texture decoding the
            // next 20%, and render-thread upload/finalization the next 30%. Keep
            // the final 5% reserved until ApplicationBase completes its last
            // scene setup and replaces the loading screen.
            const float measuredLoadingProgress = std::clamp(
                importProgress * 0.45f +
                    textureDecodeProgress * 0.20f +
                    textureReadyProgress * 0.30f,
                0.f,
                0.95f);
            const float loadingProgressTarget = std::max(
                m_DisplayedLoadingProgress,
                measuredLoadingProgress);
            const float loadingProgressResponse = std::clamp(
                ImGui::GetIO().DeltaTime * 18.f,
                0.f,
                1.f);
            m_DisplayedLoadingProgress +=
                (loadingProgressTarget - m_DisplayedLoadingProgress) *
                loadingProgressResponse;

            const ImGuiStyle& loadingStyle = ImGui::GetStyle();
            const float loadingBarHeight =
                loadingStyle.ScrollbarSize;
            const float loadingBarWidth = std::min(float(width) * 0.84f, 1040.f);
            const ImVec2 loadingBarMin(
                (float(width) - loadingBarWidth) * 0.5f,
                float(height) * 0.5f +
                    ImGui::GetTextLineHeightWithSpacing() * 2.4f);
            const ImVec2 loadingBarMax(
                loadingBarMin.x + loadingBarWidth,
                loadingBarMin.y + loadingBarHeight);
            ImGui::SetCursorScreenPos(loadingBarMin);
            ImGui::InvisibleButton(
                "##LoadingProgress",
                ImVec2(loadingBarWidth, loadingBarHeight));
            ImDrawList* loadingDrawList = ImGui::GetWindowDrawList();
            loadingDrawList->AddRectFilled(
                loadingBarMin,
                loadingBarMax,
                ImGui::GetColorU32(ImGuiCol_ScrollbarBg),
                loadingStyle.ScrollbarRounding);

            constexpr float loadingGrabInset = 3.f;
            const ImVec2 loadingGrabTrackMin(
                loadingBarMin.x + loadingGrabInset,
                loadingBarMin.y + loadingGrabInset);
            const ImVec2 loadingGrabTrackMax(
                loadingBarMax.x - loadingGrabInset,
                loadingBarMax.y - loadingGrabInset);
            const float loadingGrabTrackWidth =
                loadingGrabTrackMax.x -
                loadingGrabTrackMin.x;
            const float loadingGrabWidth = std::min(
                loadingGrabTrackWidth,
                std::max(
                    loadingStyle.GrabMinSize,
                    loadingGrabTrackWidth * 0.10f));
            const float loadingGrabTravel =
                std::max(
                    0.f,
                    loadingGrabTrackWidth -
                        loadingGrabWidth);
            const float loadingGrabPosition =
                loadingGrabTravel *
                std::clamp(
                    m_DisplayedLoadingProgress,
                    0.f,
                    1.f);
            const ImVec2 loadingGrabMin(
                loadingGrabTrackMin.x +
                    loadingGrabPosition,
                loadingGrabTrackMin.y);
            const ImVec2 loadingGrabMax(
                loadingGrabMin.x + loadingGrabWidth,
                loadingGrabTrackMax.y);
            loadingDrawList->AddRectFilled(
                loadingGrabMin,
                loadingGrabMax,
                ImGui::GetColorU32(
                    ImGuiCol_SliderGrab),
                loadingStyle.ScrollbarRounding);
            DrawControlGradientOutline(
                loadingDrawList,
                loadingBarMin,
                loadingBarMax,
                loadingStyle.ScrollbarRounding);
            DrawControlGradientOutline(
                loadingDrawList,
                loadingGrabMin,
                loadingGrabMax,
                loadingStyle.ScrollbarRounding);

            ImGui::PopFont();
            EndFullScreenWindow();

            return;
        }
        m_WasSceneLoading = false;

        ImGui::PushFont(m_Font->GetScaledFont());
        ApplyExpandedWordSpacing(
            m_AdjustedSpaceFontBakedId,
            m_BaseSpaceAdvance);

        float const fontSize = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float settingsControlWidth =
            ImGui::CalcTextSize(
                "Filter-Adapted Spatiotemporal Noise").x +
            style.FramePadding.x * 2.f;

        const char* rendererString = GetDeviceManager()->GetRendererString();
        char rendererLine[256];
        snprintf(
            rendererLine,
            std::size(rendererLine),
            "Renderer: %s",
            rendererString);

        QueueStatSnapshot(width, height, rendererString);
        ApplyQueuedStatSnapshot();
        const std::string performanceLine =
            BuildPerformanceLine(m_PerformanceStatValues);

        const float statusContentWidth = std::max(
            ImGui::CalcTextSize(rendererLine).x,
            ImGui::CalcTextSize(performanceLine.c_str()).x);
        const float longestSettingsLabelWidth =
            ImGui::CalcTextSize("Distribution Exponent").x;
        const float drawerBodyHorizontalOverhead =
            style.ScrollbarSize +
            style.FramePadding.x * 2.f +
            style.WindowPadding.x * 2.f +
            style.ItemSpacing.x;
        const float labelledSliderContentWidth =
            settingsControlWidth + style.ItemInnerSpacing.x +
            longestSettingsLabelWidth +
            drawerBodyHorizontalOverhead;
        const float actionButtonsContentWidth =
            ImGui::CalcTextSize("Reset").x +
            ImGui::CalcTextSize("Restart").x +
            ImGui::CalcTextSize("Screenshot").x +
            style.FramePadding.x * 6.f +
            style.ItemSpacing.x * 2.f +
            style.ScrollbarSize;
        const float minimumSettingsContentWidth = std::max(
            actionButtonsContentWidth,
            labelledSliderContentWidth);
        const float settingsWidthReadabilityAllowance =
            style.FramePadding.x * 2.f + style.ItemSpacing.x;
        const float windowMargin = fontSize * 0.6f;
        const float availableWindowWidth =
            std::max(1.f, float(width) - windowMargin * 2.f);
        const float settingsWindowWidth = std::min(
            std::max(statusContentWidth, minimumSettingsContentWidth) +
                style.WindowPadding.x * 2.f +
                settingsWidthReadabilityAllowance,
            availableWindowWidth);
        ImGui::SetNextWindowPos(ImVec2(windowMargin, windowMargin), 0);
        ImGui::SetNextWindowSize(
            ImVec2(settingsWindowWidth, 0.f),
            ImGuiCond_Always);
        // This is the footer button surface composited over WindowBg, so the
        // Settings title and the three action buttons resolve to one tone.
        const ImVec4 titleAndFooterSurface(
            0.146f, 0.146f, 0.154f, 0.652f);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBg,
            titleAndFooterSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgActive,
            titleAndFooterSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgCollapsed,
            titleAndFooterSurface);
        ImGui::Begin(
            "Settings",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);
        CaptureCurrentWindowBackdrop(
            m_ui.BackdropRects[0],
            style.WindowRounding);

        ImGui::TextUnformatted(rendererLine);
        if (!m_PerformanceStatValues[1].empty())
        {
            constexpr float StatusLineSpacing = 2.f;
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() - style.ItemSpacing.y +
                    StatusLineSpacing);
            ImGui::TextUnformatted(performanceLine.c_str());
            ImGui::SetItemTooltip(
                "Bandwidth is the current theoretical limit. "
                "tflops is current-clock FP32 peak scaled by GPU utilization.");
        }
        ImGui::Separator();

        const float settingsBodyMaxHeight = std::max(
            1.f,
            float(height) - windowMargin -
                ImGui::GetCursorScreenPos().y - style.WindowPadding.y);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.f, 0.f),
            ImVec2(FLT_MAX, settingsBodyMaxHeight));
        ImGui::BeginChild(
            "##SettingsBody",
            ImVec2(0.f, 0.f),
            ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        const bool generalOpen = DrawCollapsingHeader(
            "General",
            "Show general renderer settings.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (generalOpen)
        {
            BeginDrawerBody(
                "##GeneralBody",
                settingsControlWidth);

            if (!m_ui.GpuAdapterChoices.empty())
            {
            const GpuAdapterChoice* activeAdapter = nullptr;
            for (const GpuAdapterChoice& adapter : m_ui.GpuAdapterChoices)
            {
                if (adapter.adapterIndex == m_ui.ActiveGpuAdapterIndex)
                {
                    activeAdapter = &adapter;
                    break;
                }
            }

            ImGui::TextUnformatted("Graphics Adapter");
            ImGui::SetNextItemWidth(-FLT_MIN);
            const char* activeAdapterName = activeAdapter
                ? activeAdapter->name.c_str()
                : "Unknown adapter";
            if (BeginRoundedCombo("##GraphicsAdapter", activeAdapterName))
            {
                for (const GpuAdapterChoice& adapter : m_ui.GpuAdapterChoices)
                {
                    const bool selected =
                        adapter.adapterIndex == m_ui.ActiveGpuAdapterIndex;
                    if (ImGui::Selectable(adapter.name.c_str(), selected) && !selected)
                    {
                        g_RestartAdapterIndex = adapter.adapterIndex;
                        g_RestartRequested = true;
                        glfwSetWindowShouldClose(
                            GetDeviceManager()->GetWindow(), GLFW_TRUE);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the GPU. UVSR restarts after a change.");
        }

        ImGui::TextUnformatted("Camera Mode");
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool benchmarkCameraActive = m_app->IsBenchmarkCameraActive();
        if (benchmarkCameraActive)
            ImGui::BeginDisabled();
        const bool cameraComboOpen = BeginRoundedCombo(
            "##Camera", GetCameraModeLabel(m_ui.Camera));
        ImGui::SetItemTooltip(benchmarkCameraActive
            ? "The benchmark camera is Locked."
            : "Choose Freelook or Locked. Shift doubles Freelook move and zoom speed.");
        if (cameraComboOpen)
        {
            for (CameraMode mode : SelectableCameraModes)
            {
                const bool selected = mode == m_ui.Camera;
                if (ImGui::Selectable(GetCameraModeLabel(mode), selected) && !selected)
                    m_app->SetCameraMode(mode);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (benchmarkCameraActive)
            ImGui::EndDisabled();

        const bool cameraLocationsAvailable = m_app->HasSponzaCameraLocations();
        if (cameraLocationsAvailable)
        {
            ImGui::TextUnformatted("Camera Location");
            if (benchmarkCameraActive)
                ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(-FLT_MIN);
            const SponzaCameraLocation selectedCameraLocation =
                m_app->GetSponzaCameraLocation();
            const bool cameraLocationComboOpen = BeginRoundedCombo(
                "##CameraLocation",
                GetSponzaCameraLocationLabel(selectedCameraLocation));
            ImGui::SetItemTooltip(benchmarkCameraActive
                ? "Benchmark mode locks Benchmark Position 1."
                : "Recall a stored camera location. Movement changes this status to Piloted.");
            if (cameraLocationComboOpen)
            {
                for (SponzaCameraLocation location : SelectableSponzaCameraLocations)
                {
                    const bool selected = location == selectedCameraLocation;
                    if (ImGui::Selectable(GetSponzaCameraLocationLabel(location), selected))
                        m_app->SetSponzaCameraLocation(location);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (benchmarkCameraActive)
                ImGui::EndDisabled();
        }

        ImGui::TextUnformatted("World Materials");
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool worldMaterialComboOpen = BeginRoundedCombo(
            "##WorldMaterials",
            GetWorldMaterialViewLabel(selectedWorldMaterial));
        ImGui::SetItemTooltip(
            "Choose a White World presentation or inspect each material's indirect diffuse response.");
        if (worldMaterialComboOpen)
        {
            for (WorldMaterialView view : SelectableWorldMaterialViews)
            {
                const bool available = IsWorldMaterialViewAvailable(
                    view,
                    worldMaterialAvailability);
                if (!available)
                    ImGui::BeginDisabled();
                const bool selected = view == selectedWorldMaterial;
                if (ImGui::Selectable(
                        GetWorldMaterialViewLabel(view),
                        selected))
                {
                    const WorldMaterialViewState state =
                        MakeWorldMaterialViewState(view);
                    m_ui.ScreenSpaceVisibility.showIndirectDiffuseOnly = false;
                    m_app->SetWhiteWorldMode(
                        WhiteWorldMode(state.whiteWorldMode));
                    m_ui.ScreenSpaceVisibility.showIndirectDiffuseOnly =
                        state.showIndirectDiffuseOnly;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
                if (!available)
                    ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }

        static constexpr const char* PbrLightingDebugLabels[] = {
            "Off",
            "Shading Normal",
            "Geometric Normal",
            "Normal Difference",
            "Diffuse Environment",
            "Cardinal Environment Test",
            "Prefiltered Specular",
            "Environment BRDF",
            "Final Specular IBL",
            "Combined IBL",
            "Specular Occlusion",
            "Environment Mip"
        };
        ImGui::TextUnformatted("PBR Lighting Debug");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (BeginRoundedCombo(
                "##PbrLightingDebug",
                PbrLightingDebugLabels[
                    uint32_t(m_ui.LightingDebugView)]))
        {
            for (uint32_t index = 0u;
                index < std::size(PbrLightingDebugLabels);
                ++index)
            {
                const PbrLightingDebugView view =
                    PbrLightingDebugView(index);
                const bool selected =
                    view == m_ui.LightingDebugView;
                if (ImGui::Selectable(
                        PbrLightingDebugLabels[index],
                        selected) &&
                    !selected)
                {
                    m_ui.LightingDebugView = view;
                    m_app->ResetImageBasedLightingHistory(true);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(
            "Inspect decoded normals, diffuse and specular IBL stages, the "
            "split-sum LUT response, occlusion, and roughness-selected mip.");

        ImGui::TextUnformatted("World Scenes");
        const std::string currentScene = m_app->GetCurrentSceneName();
        const std::string currentSceneDisplayName = m_app->GetCurrentSceneDisplayName();
        const float folderButtonWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(folderButtonWidth + style.ItemSpacing.x));
        const bool sceneComboOpen = BeginRoundedCombo(
            "##Scene",
            currentSceneDisplayName.c_str());
        // UI convention: every UVSR-owned interactive control explains itself on hover.
        ImGui::SetItemTooltip("Load a different scene.");
        if (sceneComboOpen)
        {
            const std::vector<SceneCatalogEntry>& scenes = m_app->GetAvailableScenes();
            for (const SceneCatalogEntry& scene : scenes)
            {
                ImGui::PushID(scene.FileName.c_str());
                const bool is_selected = scene.FileName == currentScene;
                if (ImGui::Selectable(scene.DisplayName.c_str(), is_selected))
                    m_app->SetCurrentSceneName(scene.FileName);
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        const ImGuiID folderButtonId =
            ImGui::GetID("##OpenSceneFolder");
        const bool openSceneFolderPressed = ImGui::InvisibleButton(
            "##OpenSceneFolder",
            ImVec2(folderButtonWidth, ImGui::GetFrameHeight()));
        const bool folderButtonActive = ImGui::IsItemActive();
        const bool folderButtonHovered = ImGui::IsItemHovered();
        const float folderHighlightFade = GetUiHighlightFade(
            folderButtonId,
            folderButtonHovered || folderButtonActive);
        const ImVec2 iconMin = ImGui::GetItemRectMin();
        const ImVec2 iconMax = ImGui::GetItemRectMax();
        ImDrawList* folderDrawList = ImGui::GetWindowDrawList();
        ImVec4 folderUnderlayNormal =
            style.Colors[ImGuiCol_FrameBg];
        ImVec4 folderUnderlayInteraction = style.Colors[
            folderButtonActive
                ? ImGuiCol_FrameBgActive
                : ImGuiCol_FrameBgHovered];
        folderUnderlayNormal.w = 0.88f;
        folderUnderlayInteraction.w = 0.88f;
        ImVec4 folderUnderlay = LerpUiColor(
            folderUnderlayNormal,
            folderUnderlayInteraction,
            folderHighlightFade);
        folderUnderlay.w = 0.88f;
        folderDrawList->AddRectFilled(
            iconMin,
            iconMax,
            ImGui::GetColorU32(folderUnderlay),
            style.FrameRounding,
            ImDrawFlags_RoundCornersAll);
        folderDrawList->AddRectFilled(
            iconMin,
            iconMax,
            ImGui::GetColorU32(LerpUiColor(
                style.Colors[ImGuiCol_Button],
                style.Colors[
                    folderButtonActive
                        ? ImGuiCol_ButtonActive
                        : ImGuiCol_ButtonHovered],
                folderHighlightFade)),
            style.FrameRounding,
            ImDrawFlags_RoundCornersAll);
        folderDrawList->AddRect(
            ImVec2(iconMin.x + 0.5f, iconMin.y + 0.5f),
            ImVec2(iconMax.x - 0.5f, iconMax.y - 0.5f),
            ImGui::GetColorU32(ImVec4(
                0.90f,
                0.92f,
                0.96f,
                0.10f + 0.08f * folderHighlightFade)),
            std::max(0.f, style.FrameRounding - 0.5f),
            ImDrawFlags_RoundCornersAll,
            1.f);
        if (openSceneFolderPressed)
        {
            const std::filesystem::path sceneFolder = m_app->GetSceneDir();
            ShellExecuteW(nullptr, L"open", sceneFolder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        {
            const float iconWidth = iconMax.x - iconMin.x;
            const float iconHeight = iconMax.y - iconMin.y;
            const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
            const ImVec2 bodyMin(iconMin.x + iconWidth * 0.20f, iconMin.y + iconHeight * 0.38f);
            const ImVec2 bodyMax(iconMax.x - iconWidth * 0.20f, iconMax.y - iconHeight * 0.22f);
            folderDrawList->AddRect(bodyMin, bodyMax, iconColor, 1.5f, 0, 1.5f);
            folderDrawList->AddLine(
                ImVec2(bodyMin.x, bodyMin.y),
                ImVec2(bodyMin.x + iconWidth * 0.22f, iconMin.y + iconHeight * 0.27f),
                iconColor, 1.5f);
            folderDrawList->AddLine(
                ImVec2(bodyMin.x + iconWidth * 0.22f, iconMin.y + iconHeight * 0.27f),
                ImVec2(bodyMin.x + iconWidth * 0.40f, bodyMin.y),
                iconColor, 1.5f);
        }
        ImGui::SetItemTooltip("Open the scene folder.");

        // Keep the legacy-lighting comparison path available for future
        // experiments without exposing it in the production settings UI.
        // Set this to true to restore the control and its existing behavior.
        constexpr bool ShowPbrComparisonControl = false;
        if (ShowPbrComparisonControl)
        {
            if (ImGui::Checkbox("Enable PBR", &m_ui.EnablePbr))
            {
                if (!m_ui.EnablePbr && m_ui.WhiteWorld != WhiteWorldMode::Off)
                    m_app->SetWhiteWorldMode(WhiteWorldMode::Off);
                log::info("PBR rendering %s", m_ui.EnablePbr ? "enabled" : "disabled");
            }
            ImGui::SetItemTooltip("Use UVSR PBR instead of legacy Donut shading.");
        }
        EndDrawerBody();
        }
        ImGui::Spacing();

        const bool indirectLightingOpen = DrawCollapsingHeader(
            "Visibility",
            "Set up screen-space AO and diffuse GI.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (indirectLightingOpen)
        {
            BeginDrawerBody(
                "##VisibilityBody",
                settingsControlWidth);
            ScreenSpaceVisibilitySettings& visibility = m_ui.ScreenSpaceVisibility;
            const bool visibilityAvailable = m_ui.UsesDeferredShading();
            if (!visibilityAvailable)
                ImGui::BeginDisabled();

            if (ImGui::Checkbox(
                    "Enabled##ScreenSpaceVisibility", &visibility.enabled))
            {
                m_ui.EnablePbr = visibility.enabled;
                if (!m_ui.EnablePbr && m_ui.WhiteWorld != WhiteWorldMode::Off)
                    m_app->SetWhiteWorldMode(WhiteWorldMode::Off);
                log::info("Visibility and PBR rendering %s",
                    visibility.enabled ? "enabled" : "disabled");
            }
            ImGui::SetItemTooltip(
                "Enable screen-space visibility-based lighting and PBR.");

            static const char* qualityLabels[] = {
                "Low", "Medium", "High", "Ultra", "Custom"
            };
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Quality",
                    qualityLabels[int(visibility.quality)]))
            {
                for (int qualityIndex = 0; qualityIndex < int(std::size(qualityLabels)); ++qualityIndex)
                {
                    const auto quality = ScreenSpaceVisibilityQuality(qualityIndex);
                    const bool selected = visibility.quality == quality;
                    if (ImGui::Selectable(qualityLabels[qualityIndex], selected))
                        ApplyScreenSpaceVisibilityQualityPreset(visibility, quality);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Set the first-bounce sample range.");

            static const char* resolutionLabels[] = {
                "Full", "Half", "Quarter"
            };
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Sampling Resolution",
                    resolutionLabels[int(visibility.resolution)]))
            {
                for (int index = 0;
                    index < int(std::size(resolutionLabels));
                    ++index)
                {
                    const auto resolution = VisibilityResolution(index);
                    const bool selected = visibility.resolution == resolution;
                    if (ImGui::Selectable(resolutionLabels[index], selected))
                        visibility.resolution = resolution;
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the sampling resolution for screen-space visibility.");

            if (m_HasVisibilityStatSnapshot &&
                BeginAnimatedTreeNode("Statistics"))
            {
                ImGui::TextUnformatted(
                    m_VisibilityStatLines[0].c_str());
                ImGui::SetItemTooltip(
                    "Recent GPU time for visibility work.");
                ImGui::TextUnformatted(
                    m_VisibilityStatLines[1].c_str());
                ImGui::SetItemTooltip(
                    "Texture memory by use, excluding API padding.");
                ImGui::TextUnformatted(
                    m_VisibilityStatLines[2].c_str());
                ImGui::SetItemTooltip(
                    "Avoided is saved memory; Shared is estimated reuse.");
                EndAnimatedTreeNode();
            }

            if (!visibility.enabled)
                ImGui::BeginDisabled();

            if (BeginAnimatedTreeNode(
                    "Shared Visibility Sampling",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                SharedSamplingSettings& sampling = visibility.sampling;
                bool samplingChanged = false;
                static const char* estimatorLabels[] = {
                    "Uniform Projected Angle",
                    "Uniform Solid Angle",
                    "Cosine-Weighted Solid Angle"
                };
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                    "Estimator",
                    estimatorLabels[int(visibility.estimator)]))
                {
                    for (int estimatorIndex = 0;
                        estimatorIndex < int(std::size(estimatorLabels));
                        ++estimatorIndex)
                    {
                        const auto estimator = VisibilityEstimator(estimatorIndex);
                        const bool selected = visibility.estimator == estimator;
                        if (ImGui::Selectable(
                                estimatorLabels[estimatorIndex], selected))
                            visibility.estimator = estimator;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("Choose how samples spread around each pixel.");
                samplingChanged |= DrawSliderFloat(
                    "Radius", &sampling.radius, 0.01f, std::max(m_app->GetSceneDiagonal() * 0.1f, 1.f), "%.3f");
                ImGui::SetItemTooltip("Set how far visibility rays reach.");
                samplingChanged |= DrawSliderFloat(
                    "Thickness", &sampling.thickness, 0.0f, std::max(m_app->GetSceneDiagonal() * 0.02f, 0.5f), "%.3f");
                ImGui::SetItemTooltip("Set the assumed thickness of occluders.");

                const bool miniEngineTaaActive = m_ui.UsesMiniEngineTaa();
                if (miniEngineTaaActive)
                    ImGui::BeginDisabled();
                samplingChanged |= ImGui::Checkbox(
                    "Adaptive Sparse Sampling",
                    &sampling.adaptiveSparseSamplingEnabled);
                ImGui::SetItemTooltip(miniEngineTaaActive
                        ? "Disable MiniEngine TAA before enabling adaptive sparse sampling."
                        : "Spend more samples where the image is harder to resolve.");
                if (miniEngineTaaActive)
                    ImGui::EndDisabled();

                if (sampling.adaptiveSparseSamplingEnabled)
                {
                    int minimumSamples = int(std::clamp(
                        sampling.minimumSampleCount, 1u, 64u));
                    if (DrawSliderInt(
                            "Minimum Samples / Pixel",
                            &minimumSamples, 1, 64))
                    {
                        sampling.minimumSampleCount =
                            uint32_t(minimumSamples);
                        sampling.maximumSampleCount = std::max(
                            sampling.maximumSampleCount,
                            sampling.minimumSampleCount);
                        samplingChanged = true;
                    }
                    ImGui::SetItemTooltip("Set the samples every pixel receives.");

                    int maximumSamples = int(std::clamp(
                        sampling.maximumSampleCount,
                        sampling.minimumSampleCount, 64u));
                    if (DrawSliderInt(
                            "Maximum Samples / Pixel",
                            &maximumSamples,
                            int(sampling.minimumSampleCount),
                            64))
                    {
                        sampling.maximumSampleCount =
                            uint32_t(maximumSamples);
                        samplingChanged = true;
                    }
                    ImGui::SetItemTooltip("Cap samples used on difficult pixels.");

                    const bool adaptiveControlsActive =
                        sampling.maximumSampleCount >
                            sampling.minimumSampleCount;
                    if (!adaptiveControlsActive)
                        ImGui::BeginDisabled();
                    samplingChanged |= DrawSliderFloat(
                        "Adaptive Error Strength",
                        &sampling.adaptiveStrength,
                        0.0f,
                        2.0f,
                        "%.2f");
                    ImGui::SetItemTooltip("Control how strongly errors add samples.");
                    if (!adaptiveControlsActive)
                        ImGui::EndDisabled();
                }
                else
                {
                    int fixedSamples = int(std::clamp(
                        sampling.maximumSampleCount, 1u, 64u));
                    if (DrawSliderInt(
                            "Samples Per Pixel##FixedSamplesPerPixel",
                            &fixedSamples,
                            1,
                            64))
                    {
                        sampling.maximumSampleCount =
                            uint32_t(fixedSamples);
                        sampling.minimumSampleCount = std::min(
                            sampling.minimumSampleCount,
                            sampling.maximumSampleCount);
                        samplingChanged = true;
                    }
                    ImGui::SetItemTooltip("Set the samples used by every pixel.");
                }

                samplingChanged |= DrawSliderFloat(
                    "Distribution Exponent",
                    &sampling.stepDistributionExponent,
                    0.5f,
                    4.0f,
                    "%.2f");
                ImGui::SetItemTooltip("Higher values place more samples nearby.");

                static const char* schedulerLabels[] = {
                    "Independent Hash Noise",
                    "Toroidal Blue Noise",
                    "Filter-Adapted Spatiotemporal Noise"
                };
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Sample Scheduler",
                        schedulerLabels[int(sampling.scheduler)]))
                {
                    for (int index = 0;
                        index < int(std::size(schedulerLabels));
                        ++index)
                    {
                        const auto scheduler =
                            VisibilitySampleScheduler(index);
                        const bool selected = sampling.scheduler == scheduler;
                        if (ImGui::Selectable(
                                schedulerLabels[index], selected))
                        {
                            sampling.scheduler = scheduler;
                            samplingChanged = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("Choose the noise pattern used to place samples.");

                if (samplingChanged)
                    visibility.quality = ScreenSpaceVisibilityQuality::Custom;
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Ambient Occlusion",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                AmbientOcclusionSettings& ao = visibility.ambientOcclusion;
                ImGui::Checkbox("Enabled##AmbientVisibility", &ao.enabled);
                ImGui::SetItemTooltip("Enable screen-space ambient occlusion.");
                if (!ao.enabled)
                    ImGui::BeginDisabled();
                DrawSliderFloat("Strength", &ao.strength, 0.0f, 2.0f, "%.2f");
                ImGui::SetItemTooltip("Set how strongly AO darkens indirect light.");
                if (!ao.enabled)
                    ImGui::EndDisabled();
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Indirect Diffuse",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                IndirectDiffuseSettings& gi = visibility.indirectDiffuse;
                ImGui::Checkbox("Enabled##IndirectDiffuse", &gi.enabled);
                ImGui::SetItemTooltip("Enable screen-space diffuse indirect light.");
                if (!gi.enabled)
                    ImGui::BeginDisabled();
                int bounceCount = int(std::clamp(
                    gi.bounceCount, 1u, MaxIndirectDiffuseBounceCount));
                if (DrawSliderInt(
                        "Bounces##IndirectDiffuse",
                        &bounceCount,
                        1,
                        int(MaxIndirectDiffuseBounceCount),
                        "%d",
                        ImGuiSliderFlags_AlwaysClamp))
                {
                    gi.bounceCount = uint32_t(bounceCount);
                }
                ImGui::SetItemTooltip("Set the number of diffuse-light bounces.");
                if (gi.bounceCount > 1u)
                {
                    DrawSliderFloat(
                        "Bounce Contribution Cutoff",
                        &gi.minimumBounceContribution,
                        0.0f,
                        0.02f,
                        "%.5f");
                    ImGui::SetItemTooltip(
                        "Skip dim higher-bounce light. Zero disables the cutoff.");
                }
                DrawSliderFloat("Intensity##IndirectDiffuse", &gi.intensity, 0.0f, 10.0f, "%.2f");
                ImGui::SetItemTooltip("Set screen-space diffuse GI brightness.");
                ImGui::Checkbox("Include Emissive Sources", &gi.includeEmissive);
                ImGui::SetItemTooltip("Let visible emissive surfaces light the scene.");
                if (!gi.includeEmissive)
                    ImGui::BeginDisabled();
                DrawSliderFloat(
                    "Emissive Source Gain",
                    &gi.emissiveGain,
                    0.0f,
                    10.0f,
                    "%.2f");
                ImGui::SetItemTooltip("Set emissive surfaces' GI strength.");
                if (!gi.includeEmissive)
                    ImGui::EndDisabled();
                if (!gi.enabled)
                    ImGui::EndDisabled();
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Reconstruction##VisibilityReconstruction"))
            {
                VisibilityReconstructionSettings& reconstruction =
                    visibility.reconstruction;
                const bool miniEngineTaaActive = m_ui.UsesMiniEngineTaa();
                if (miniEngineTaaActive)
                    ImGui::BeginDisabled();
                ImGui::Checkbox(
                    "Temporal Reuse##TemporalReconstruction",
                    &reconstruction.temporalEnabled);
                ImGui::SetItemTooltip(miniEngineTaaActive
                        ? "Turn off MiniEngine TAA to use temporal reuse."
                        : "Reuse stable detail from prior frames.");
                if (miniEngineTaaActive)
                    ImGui::EndDisabled();
                if (!reconstruction.temporalEnabled)
                    ImGui::BeginDisabled();
                DrawSliderFloat(
                    "Current Response##TemporalCurrentResponse",
                    &reconstruction.temporalResponse,
                    0.05f,
                    1.0f,
                    "%.2f");
                ImGui::SetItemTooltip(
                    "Higher values favor the current frame.");
                if (!reconstruction.temporalEnabled)
                    ImGui::EndDisabled();

                ImGui::Checkbox(
                    "Spatial Filter##SpatialFiltering",
                    &reconstruction.spatialEnabled);
                ImGui::SetItemTooltip("Edge-aware smoothing for AO and GI.");

                if (!reconstruction.spatialEnabled)
                    ImGui::BeginDisabled();
                static const char* filterLabels[] = {
                    "Joint Bilateral",
                    "Gaussian Joint Bilateral"
                };
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Filter Type##SpatialFilter",
                        filterLabels[int(reconstruction.spatialFilter)]))
                {
                    for (int index = 0;
                        index < int(std::size(filterLabels));
                        ++index)
                    {
                        const auto filter = VisibilitySpatialFilter(index);
                        const bool selected =
                            reconstruction.spatialFilter == filter;
                        if (ImGui::Selectable(filterLabels[index], selected))
                            reconstruction.spatialFilter = filter;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("Choose the smoothing filter.");
                if (reconstruction.spatialFilter ==
                    VisibilitySpatialFilter::GaussianJointBilateral)
                {
                    DrawSliderFloat(
                        "Filter Radius##GaussianRadius",
                        &reconstruction.spatialRadius,
                        1.0f,
                        12.0f,
                        "%.1f");
                    ImGui::SetItemTooltip("Set the Gaussian filter radius.");
                }
                if (!reconstruction.spatialEnabled)
                    ImGui::EndDisabled();
                EndAnimatedTreeNode();
            }

            if (!visibility.enabled)
                ImGui::EndDisabled();
            if (!visibilityAvailable)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Requires deferred UVSR PBR rendering.");
            }
            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool sparseVirtualShadowsOpen = DrawCollapsingHeader(
            "Sparse Virtual Shadow Maps",
            "Configure the separate directional SVSM visibility producer.");
        if (sparseVirtualShadowsOpen)
        {
            BeginDrawerBody(
                "##SparseVirtualShadowMapsBody",
                settingsControlWidth);
            SparseVirtualShadowMapSettings& shadows =
                m_ui.SparseVirtualShadowMaps;
            const bool shadowsAvailable =
                m_ui.EnablePbr &&
                m_ui.UsesDeferredShading() &&
                m_app->HasPrimaryDirectionalLight();
            if (!shadowsAvailable)
                ImGui::BeginDisabled();

            const bool motionTestRunning =
                m_app->IsSvsmMotionBenchmarkRunning();
            if (motionTestRunning)
                ImGui::BeginDisabled();
            ImGui::Checkbox(
                "Enabled##SparseVirtualShadowMaps",
                &shadows.enabled);
            ImGui::SetItemTooltip(
                "Resolve independent directional visibility before deferred lighting.");
            if (motionTestRunning)
                ImGui::EndDisabled();
            if (!shadows.enabled)
                ImGui::BeginDisabled();

            const bool motionTestAvailable =
                m_app->CanStartSvsmMotionBenchmark();
            if (motionTestRunning)
                ImGui::BeginDisabled();

            bool customChanged = DrawSvsmSettingsSurface(
                shadows,
                settingsControlWidth);

            if (BeginAnimatedTreeNode(
                    "Diagnostics##SparseVirtualShadowMaps"))
            {
                if (!motionTestAvailable)
                    ImGui::BeginDisabled();
                if (ImGui::Button(
                        motionTestRunning
                            ? "Running 45-Degree Motion Test..."
                            : "Run 45-Degree Motion Test",
                        ImVec2(settingsControlWidth, 0.f)))
                {
                    m_app->StartSvsmMotionBenchmark();
                }
                ImGui::SetItemTooltip(
                    motionTestRunning
                        ? "Benchmark Position 1 is turning by exactly 0.1 degrees per rendered frame without wall-clock pacing."
                        : (m_app->HasSponzaCameraLocations()
                            ? "Run 180 warm frames, turn Benchmark Position 1 right by exactly 0.1 degrees per rendered frame to 45 degrees, hold for 16 frames, and return at the same rate."
                            : "The motion test requires a standardized PBR Sponza scene."));
                if (ImGui::Button(
                        motionTestRunning
                            ? "Running SVSM Motion Benchmark..."
                            : "Run SunSlow Motion Test",
                        ImVec2(settingsControlWidth, 0.f)))
                {
                    m_app->StartSvsmSunMotionBenchmark();
                }
                ImGui::SetItemTooltip(
                    motionTestRunning
                        ? "The active SVSM benchmark owns the camera, light, and isolated visibility configuration."
                        : (m_app->HasSponzaCameraLocations()
                            ? "Hold Benchmark Position 1 static, rotate the sun by exactly 0.1 degrees per rendered frame, then measure cache recovery."
                            : "The SunSlow test requires a standardized PBR Sponza scene."));
                if (!motionTestAvailable)
                    ImGui::EndDisabled();

                ImGui::TextWrapped(
                    "%s",
                    m_app->GetSvsmMotionBenchmarkStatus().c_str());

                customChanged |= ImGui::Checkbox(
                    "Detailed GPU Stage Timing",
                    &shadows.detailedGpuTimingEnabled);
                ImGui::SetItemTooltip(
                    "Measure Mark, Allocate, Clear, Packet, Render, and Filter separately. Disable for the lowest-overhead total-only timing path.");

                static const char* debugLabels[] = {
                    "Off",
                    "Clipmap Selection",
                    "Required Pages",
                    "Resident Pages",
                    "Cached Pages",
                    "Dirty Pages",
                    "Rendered Pages",
                    "Physical Pool",
                    "Fallback Level",
                    "Missing Pages",
                    "Tap Count",
                    "Visibility"
                };
                const int debugIndex = std::clamp(
                    int(shadows.debugView),
                    0,
                    int(std::size(debugLabels)) - 1);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Debug View##SparseVirtualShadowMaps",
                        debugLabels[debugIndex]))
                {
                    for (int index = 0;
                        index < int(std::size(debugLabels));
                        ++index)
                    {
                        const SvsmDebugView debugView =
                            SvsmDebugView(index);
                        const bool selected =
                            shadows.debugView == debugView;
                        if (ImGui::Selectable(
                                debugLabels[index],
                                selected))
                        {
                            shadows.debugView = debugView;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Present the selected full-screen diagnostic. Debug views enable asynchronous page-counter readback and can disable otherwise reusable visibility work.");

                ImGui::TextDisabled(
                    "SVSM resolves to full-resolution R8 visibility before deferred lighting.");
                if (m_HasSparseShadowStatSnapshot)
                {
                    ImGui::BeginGroup();
                    for (const std::string& line :
                        m_SparseShadowStatLines)
                    {
                        ImGui::TextUnformatted(line.c_str());
                    }
                    ImGui::EndGroup();
                    ImGui::SetItemTooltip(
                        shadows.debugView == SvsmDebugView::None
                            ? "GPU page counters are copied to the CPU only while an SVSM debug view is active; normal cached rendering has no readback."
                            : "GPU page counters remain unavailable until a current debug readback retires. Available counters report their source-frame age.");
                }

                EndAnimatedTreeNode();
            }

            if (customChanged)
                shadows.preset = SvsmPreset::Custom;

            if (motionTestRunning)
                ImGui::EndDisabled();
            if (!shadows.enabled)
                ImGui::EndDisabled();
            if (!shadowsAvailable)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled(
                    "Requires deferred UVSR PBR rendering and a directional light.");
            }
            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool diagnosticCsmOpen =
            DrawCollapsingHeader("Diagnostic Cascaded Shadow Maps",
                                 "Compare a conventional UE5-style CSM and cache update "
                                 "policies against SVSM.");
        if (diagnosticCsmOpen)
        {
            BeginDrawerBody("##DiagnosticCascadedShadowMapsBody", settingsControlWidth);
            DiagnosticCascadedShadowMapSettings& shadows =
                m_ui.DiagnosticCascadedShadowMaps;
            const bool shadowsAvailable = m_ui.EnablePbr && m_ui.UsesDeferredShading() &&
                                          m_app->HasPrimaryDirectionalLight();
            if (!shadowsAvailable)
                ImGui::BeginDisabled();

            ImGui::Checkbox("Enabled##DiagnosticCascadedShadowMaps", &shadows.enabled);
            ImGui::SetItemTooltip("Resolve an independent full-resolution R8 directional "
                                  "visibility factor before deferred lighting.");
            if (!shadows.enabled)
                ImGui::BeginDisabled();

            static const char* profileLabels[] = {"Single-Map Reference",
                                                  "Low-Cost CSM",
                                                  "UE5 CSM Reference",
                                                  "Cached Single Shadow",
                                                  "Optimized Cached Single Shadow",
                                                  "Optimized Cached CSM",
                                                  "(Custom)"};
            const int profileIndex =
                std::clamp(int(shadows.profile), 0, int(std::size(profileLabels)) - 1);
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo("Profile##DiagnosticCascadedShadowMaps",
                                  profileLabels[profileIndex]))
            {
                for (int index = 0; index < int(std::size(profileLabels)); ++index)
                {
                    const auto profile = DiagnosticCsmProfile(index);
                    const bool selected = shadows.profile == profile;
                    if (ImGui::Selectable(profileLabels[index], selected))
                    {
                        if (profile == DiagnosticCsmProfile::Custom)
                        {
                            shadows.profile = profile;
                        }
                        else
                        {
                            shadows = ApplyDiagnosticCsmProfile(shadows, profile);
                        }
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Select the requested full-redraw or cache-policy "
                                  "diagnostic. Custom retains every edited value.");

            bool customChanged = false;
            int cascadeCount =
                int(std::clamp(shadows.cascadeCount, 1u, DiagnosticCsmMaximumCascades));
            if (DrawSliderInt("Cascades##DiagnosticCascadedShadowMaps",
                              &cascadeCount,
                              1,
                              int(DiagnosticCsmMaximumCascades)))
            {
                shadows.cascadeCount = uint32_t(cascadeCount);
                customChanged = true;
            }
            ImGui::SetItemTooltip("Use one to four directional cascades.");

            int shadowMapResolution =
                int(std::clamp(shadows.shadowMapResolution, 128u, 8192u));
            if (DrawSliderInt("Resolution Per Cascade",
                              &shadowMapResolution,
                              128,
                              8192,
                              "%d",
                              ImGuiSliderFlags_Logarithmic |
                                  ImGuiSliderFlags_AlwaysClamp))
            {
                shadows.shadowMapResolution = uint32_t(shadowMapResolution);
                customChanged = true;
            }
            ImGui::SetItemTooltip(
                "Set the persistent square resolution of every cascade. The UE path "
                "prefers D16 and falls back to D32 when required; match format, texel "
                "density, and filtering when comparing paths.");

            customChanged |= DrawSliderFloat("Maximum Shadow Distance",
                                             &shadows.maximumShadowDistance,
                                             1.f,
                                             5000.f,
                                             "%.1f");
            ImGui::SetItemTooltip("Set the camera-space range covered by all cascades.");
            customChanged |= DrawSliderFloat("Maximum Light Depth##DiagnosticCsm",
                                             &shadows.maximumLightDepth,
                                             1.f,
                                             10000.f,
                                             "%.1f");
            ImGui::SetItemTooltip(
                "Set the saved conservative caster depth range. UE Minimum Light "
                "Depth raises the effective range to at least 10,000 units.");

            static const char* filterLabels[] = {"UE5 Manual 5x5 PCF",
                                                 "SVSM-Matched Point Poisson"};
            const int filterIndex =
                std::clamp(int(shadows.filter), 0, int(std::size(filterLabels)) - 1);
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo("Filter##DiagnosticCascadedShadowMaps",
                                  filterLabels[filterIndex]))
            {
                for (int index = 0; index < int(std::size(filterLabels)); ++index)
                {
                    const auto filter = DiagnosticCsmFilter(index);
                    const bool selected = shadows.filter == filter;
                    if (ImGui::Selectable(filterLabels[index], selected))
                    {
                        shadows.filter = filter;
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Use UE's nine-Gather4 manual 5x5 kernel and soft depth transition, or "
                "SVSM's fixed point-load Poisson footprint for matched comparisons.");

            if (shadows.filter == DiagnosticCsmFilter::Poisson)
            {
                static const uint32_t tapCounts[] = {1u, 4u, 8u, 16u};
                const uint32_t normalizedTapCount =
                    NormalizeDiagnosticCsmTapCount(shadows.poissonTapCount);
                char tapPreview[16];
                snprintf(
                    tapPreview, std::size(tapPreview), "%u taps", normalizedTapCount);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo("Filter Taps##DiagnosticCsm", tapPreview))
                {
                    for (uint32_t tapCount : tapCounts)
                    {
                        char label[16];
                        snprintf(label, std::size(label), "%u taps", tapCount);
                        const bool selected = normalizedTapCount == tapCount;
                        if (ImGui::Selectable(label, selected))
                        {
                            shadows.poissonTapCount = tapCount;
                            customChanged = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Select the matched 1, 4, 8, or 16-tap Poisson receiver.");
            }
            const bool configurableFilterRadius =
                shadows.filter == DiagnosticCsmFilter::Poisson;
            if (!configurableFilterRadius)
                ImGui::BeginDisabled();
            customChanged |= DrawSliderFloat("Filter Radius##DiagnosticCsm",
                                             &shadows.filterRadiusTexels,
                                             0.f,
                                             8.f,
                                             "%.2f texels");
            if (!configurableFilterRadius)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                configurableFilterRadius
                    ? "Set the fixed point-load Poisson footprint and dirty-rectangle "
                      "safety halo in shadow texels; one tap always uses the center only."
                    : "UE's manual PCF reconstructs its fixed 5x5 footprint from nine "
                      "Gather4 fetches; this saved Poisson radius is retained.");

            if (BeginAnimatedTreeNode("Developer Options##DiagnosticCsm"))
            {
                const bool cachePolicyActive = HasAnyDiagnosticCsmCachePolicy(shadows);
                const bool viewDependentCasterCullingAvailable =
                    CanUseDiagnosticCsmViewDependentCasterCulling(shadows);

                const bool projectionAndBiasOpen =
                    ImGui::TreeNodeEx("Projection And Bias##DiagnosticCsm");
                ImGui::SetItemTooltip(
                    "Tune UE-style split distribution, transition fades, "
                    "stable projection snapping, and receiver bias.");
                if (projectionAndBiasOpen)
                {
                    customChanged |=
                        ImGui::Checkbox("UE Minimum Light Depth##DiagnosticCsm",
                                        &shadows.enforceUeMinimumLightDepth);
                    ImGui::SetItemTooltip(
                        "Enforce UE's conventional directional-shadow minimum "
                        "10,000-unit subject depth span; disable only for an explicitly "
                        "unmatched low-depth diagnostic.");
                    customChanged |= DrawSliderFloat("Split Distribution Exponent",
                                                     &shadows.cascadeDistributionExponent,
                                                     1.f,
                                                     8.f,
                                                     "%.2f");
                    ImGui::SetItemTooltip(
                        "Use UE's geometric split weighting; a fully dynamic movable "
                        "directional light resolves the reference profile to 4.");
                    customChanged |= DrawSliderFloat("Cascade Transition Fraction",
                                                     &shadows.cascadeTransitionFraction,
                                                     0.f,
                                                     0.5f,
                                                     "%.3f");
                    ImGui::SetItemTooltip("Extend and cross-fade adjacent cascades over "
                                          "this fraction of each split.");
                    customChanged |=
                        DrawSliderFloat("Shadow Distance Fade Fraction",
                                        &shadows.shadowDistanceFadeoutFraction,
                                        0.f,
                                        0.5f,
                                        "%.3f");
                    ImGui::SetItemTooltip("Fade the last cascade to unshadowed "
                                          "visibility at the maximum distance.");

                    int projectionSnapTexelMultiple =
                        int(std::clamp(shadows.projectionSnapTexelMultiple, 1u, 16u));
                    if (DrawSliderInt("Projection Snap Multiple##DiagnosticCsm",
                                      &projectionSnapTexelMultiple,
                                      1,
                                      16))
                    {
                        shadows.projectionSnapTexelMultiple =
                            uint32_t(projectionSnapTexelMultiple);
                        customChanged = true;
                    }
                    ImGui::SetItemTooltip(
                        "Snap stable cascade centers in this many shadow texels; UE's "
                        "conventional CSM path uses four.");

                    customChanged |= DrawSliderFloat("Depth Bias##DiagnosticCsm",
                                                     &shadows.depthBias,
                                                     0.f,
                                                     100.f,
                                                     "%.2f");
                    ImGui::SetItemTooltip(
                        "Set UE's r.Shadow.CSMDepthBias base value; the light bias, "
                        "light-depth span, and world-space texel size determine the "
                        "effective normalized vertex bias.");
                    customChanged |=
                        DrawSliderFloat("Slope-Scaled Depth Bias##DiagnosticCsm",
                                        &shadows.slopeScaledDepthBias,
                                        0.f,
                                        10.f,
                                        "%.2f");
                    ImGui::SetItemTooltip(
                        "Set UE's r.Shadow.CSMSlopeScaleDepthBias base value; the "
                        "directional-light slope factor and a slope clamped to one "
                        "determine the effective term.");
                    customChanged |=
                        DrawSliderFloat("Directional-Light Shadow Bias##DiagnosticCsm",
                                        &shadows.directionalLightShadowBias,
                                        0.f,
                                        1.f,
                                        "%.2f");
                    ImGui::SetItemTooltip("Match the directional-light component Shadow "
                                          "Bias multiplier; UE's default is 0.5.");
                    customChanged |=
                        DrawSliderFloat("Directional-Light Slope Bias##DiagnosticCsm",
                                        &shadows.directionalLightShadowSlopeBias,
                                        0.f,
                                        1.f,
                                        "%.2f");
                    ImGui::SetItemTooltip("Match the directional-light component Shadow "
                                          "Slope Bias multiplier; UE's default is 0.5.");
                    customChanged |= DrawSliderFloat("Receiver Depth Bias##DiagnosticCsm",
                                                     &shadows.receiverDepthBias,
                                                     0.f,
                                                     1.f,
                                                     "%.3f");
                    ImGui::SetItemTooltip(
                        "Match r.Shadow.CSMReceiverBias by widening the soft comparison "
                        "transition at grazing receiver angles; this is not a direct "
                        "depth offset.");

                    ImGui::TreePop();
                }

                const auto drawUnabstracted = [&]() {
                    const bool unabstractedOpen =
                        ImGui::TreeNodeEx("Unabstracted##DiagnosticCsm");
                    ImGui::SetItemTooltip(
                        "Retain independently reversible depth, receiver, "
                        "classification, and submission paths trending toward "
                        "abstracted always-on policy.");
                    if (unabstractedOpen)
                    {
                        customChanged |= ImGui::Checkbox("16-Bit Shadow Depth",
                                                         &shadows.use16BitDepthEnabled);
                        ImGui::SetItemTooltip(
                            "Prefer UE D3D12's R16-typeless/D16 shadow-depth format; "
                            "fall back to sampleable D32 when D16 is unsupported.");
                        customChanged |=
                            ImGui::Checkbox("Opaque Depth State Merging",
                                            &shadows.opaqueDepthStateMergingEnabled);
                        ImGui::SetItemTooltip("Canonicalize eligible opaque depth-only "
                                              "materials and sort by "
                                              "effective depth state; alpha-tested "
                                              "materials remain distinct.");
                        customChanged |=
                            ImGui::Checkbox("Position-Only Opaque Casters",
                                            &shadows.positionOnlyOpaqueEnabled);
                        ImGui::SetItemTooltip(
                            "Use a CSM-local opaque vertex permutation that omits unused "
                            "texture-coordinate loads; alpha-tested casters keep the "
                            "generic "
                            "vertex shader.");
                        customChanged |= ImGui::Checkbox(
                            "Translation-Only Caster Transforms",
                            &shadows.translationOnlyCasterTransformEnabled);
                        ImGui::SetItemTooltip(
                            "Push the exact finite translation for single-instance "
                            "casters "
                            "whose linear transform is canonical identity, avoiding "
                            "redundant instance-buffer loads and affine position/normal "
                            "transforms. Every other caster retains the unchanged "
                            "instance-buffer path.");
                        customChanged |= ImGui::Checkbox(
                            "Precomputed Depth-Axis Normalization",
                            &shadows.precomputedDepthAxisInverseLengthEnabled);
                        ImGui::SetItemTooltip(
                            "Normalize the exact directional world-to-clip depth axis "
                            "once "
                            "per cascade and reuse its inverse length in the opaque and "
                            "alpha-tested depth shaders. Disable to retain the exact "
                            "per-vertex reference calculation.");
                        if (!shadows.precomputedDepthAxisInverseLengthEnabled)
                            ImGui::BeginDisabled();
                        customChanged |=
                            ImGui::Checkbox("Conservative Saturated-Slope Shortcut",
                                            &shadows.conservativeSaturatedSlopeEnabled);
                        ImGui::SetItemTooltip(
                            "With precomputed depth-axis normalization active, branch "
                            "directly to UE's maximum clamped slope when squared NoL is "
                            "one "
                            "float step below or less than the exact 0.5 saturation "
                            "boundary. Invalid, degenerate, and higher-NoL inputs retain "
                            "the "
                            "exact reference math; disabling selects the unchanged "
                            "shader "
                            "path.");
                        customChanged |=
                            ImGui::Checkbox("Algebraic Slow-Slope Reduction",
                                            &shadows.algebraicSlowSlopeEnabled);
                        ImGui::SetItemTooltip(
                            "With precomputed depth-axis normalization active, evaluate "
                            "the "
                            "unsaturated UE slope as the perpendicular-to-parallel "
                            "normal "
                            "ratio, removing one reciprocal square root while preserving "
                            "the "
                            "same clamped result. Invalid inputs retain the exact "
                            "reference "
                            "calculation; disabling selects the preceding shader path "
                            "byte-for-byte.");
                        if (!shadows.precomputedDepthAxisInverseLengthEnabled)
                            ImGui::EndDisabled();
                        customChanged |= ImGui::Checkbox(
                            "Pre-Normalized Receiver Light Direction",
                            &shadows.preNormalizedReceiverLightDirectionEnabled);
                        ImGui::SetItemTooltip(
                            "Reuse the finite CPU-normalized directional-light vector "
                            "directly in the CSM receiver. Disable to select the exact "
                            "legacy resolve shader, which normalizes the same vector per "
                            "receiver invocation.");
                        customChanged |=
                            ImGui::Checkbox("Precomposed Clip-to-Shadow Transform",
                                            &shadows.precomposedClipToShadowEnabled);
                        ImGui::SetItemTooltip("Precompose each cascade's "
                                              "camera-clip-to-shadow transform once "
                                              "on the CPU, matching UE's "
                                              "screen-to-shadow matrix organization "
                                              "and avoiding per-pixel world "
                                              "reconstruction. Disable to select "
                                              "the exact world-space receiver path.");
                        customChanged |= ImGui::Checkbox(
                            "One-Pass Cascade Classification",
                            &shadows.singleTraversalCasterClassificationEnabled);
                        ImGui::SetItemTooltip(
                            "Traverse the scene once and classify each caster across "
                            "every "
                            "redrawn cascade, matching UE's gather organization. Disable "
                            "to "
                            "retain the original independent traversal and sort for each "
                            "cascade.");
                        const bool receiverHullAxesAvailable =
                            viewDependentCasterCullingAvailable &&
                            shadows.accurateCasterCullingEnabled;
                        if (!receiverHullAxesAvailable)
                            ImGui::BeginDisabled();
                        customChanged |=
                            ImGui::Checkbox("Precomputed Receiver Hull Axes",
                                            &shadows.precomputedReceiverHullAxesEnabled);
                        ImGui::SetItemTooltip(
                            "Precompute the normalized receiver-hull axes and intervals "
                            "once "
                            "per cascade. Disable to retain the original per-caster, "
                            "per-cascade projected-hull calculations.");
                        if (!receiverHullAxesAvailable)
                            ImGui::EndDisabled();
                        customChanged |=
                            ImGui::Checkbox("Shared Caster Light Projection",
                                            &shadows.sharedCasterLightProjectionEnabled);
                        ImGui::SetItemTooltip(
                            "Project reliable caster bounds once into a compatible "
                            "directional-light basis and reuse that shape across "
                            "updating "
                            "cascades. Disable to retain the original per-cascade "
                            "projection "
                            "path.");
                        customChanged |=
                            ImGui::Checkbox("Direct Caster Submission",
                                            &shadows.directCasterSubmissionEnabled);
                        ImGui::SetItemTooltip(
                            "Let Donut consume the sorted caster records directly "
                            "without "
                            "rebuilding a contiguous DrawItem scratch vector. Disable to "
                            "retain the original copy-based submission path.");
                        customChanged |=
                            ImGui::Checkbox("Batched Full-Redraw Clear",
                                            &shadows.batchedFullRedrawClearEnabled);
                        ImGui::SetItemTooltip(
                            "Clear a contiguous set of two or more all-full-redraw "
                            "cascade "
                            "array slices with one depth clear. Disable to retain one "
                            "full-map clear call per cascade; mixed and localized "
                            "updates "
                            "always keep their legacy commands.");
                        ImGui::TreePop();
                    }
                };

                const bool cacheUpdatePolicyOpen =
                    ImGui::TreeNodeEx("Cache Update Policy##DiagnosticCsm");
                ImGui::SetItemTooltip(
                    "Tune whole-map or per-cascade reuse, localized dirty "
                    "updates, and exactly compatible scrolling.");
                if (cacheUpdatePolicyOpen)
                {
                    if (cachePolicyActive)
                        ImGui::BeginDisabled();
                    customChanged |=
                        ImGui::Checkbox("Cached Shadow Draw Lists",
                                        &shadows.cachedShadowDrawListsEnabled);
                    if (cachePolicyActive)
                        ImGui::EndDisabled();
                    ImGui::SetItemTooltip(
                        "Reuse exact final sorted caster lists only for repeating "
                        "full-redraw configurations; map-cache policies already "
                        "skip gathering and take precedence.");

                    customChanged |=
                        ImGui::Checkbox("Whole-Map Reuse", &shadows.wholeMapReuseEnabled);
                    ImGui::SetItemTooltip("Reuse all cascades only when every projection "
                                          "and the scene revision are unchanged.");
                    customChanged |= ImGui::Checkbox("Whole-Cascade Reuse",
                                                     &shadows.wholeCascadeReuseEnabled);
                    ImGui::SetItemTooltip(
                        "Classify and reuse each cascade independently. When both reuse "
                        "modes are enabled, this finer policy takes precedence.");
                    if (!shadows.wholeCascadeReuseEnabled)
                        ImGui::BeginDisabled();
                    customChanged |= ImGui::Checkbox("Dirty Rectangles",
                                                     &shadows.dirtyRectanglesEnabled);
                    ImGui::SetItemTooltip(
                        "For compatible cached cascades, clear old and new changed "
                        "bounds and rerender every overlapping caster.");
                    customChanged |=
                        ImGui::Checkbox("Scrolling", &shadows.scrollingEnabled);
                    if (!shadows.wholeCascadeReuseEnabled)
                        ImGui::EndDisabled();
                    ImGui::SetItemTooltip(
                        "Reuse only exactly compatible integer-shifted texels, then "
                        "clear and rerender exposed regions.");
                    if (!shadows.wholeCascadeReuseEnabled || !shadows.scrollingEnabled)
                    {
                        ImGui::BeginDisabled();
                    }
                    customChanged |= DrawSliderFloat("Minimum Scroll Overlap",
                                                     &shadows.minimumScrollOverlap,
                                                     0.5f,
                                                     1.f,
                                                     "%.2f");
                    if (!shadows.wholeCascadeReuseEnabled || !shadows.scrollingEnabled)
                    {
                        ImGui::EndDisabled();
                    }
                    ImGui::SetItemTooltip("Require at least this compatible texel "
                                          "overlap before accepting a scroll update.");
                    ImGui::TreePop();
                }

                const bool cullingAndRasterOpen =
                    ImGui::TreeNodeEx("Culling And Raster##DiagnosticCsm");
                ImGui::SetItemTooltip(
                    "Tune experimental caster fetch and view-dependent "
                    "full-redraw culling without changing cached-map policy.");
                if (cullingAndRasterOpen)
                {
                    customChanged |=
                        ImGui::Checkbox("Input-Assembler Caster Fetch",
                                        &shadows.inputAssemblerCasterFetchEnabled);
                    ImGui::SetItemTooltip(
                        "Experimentally route only eligible non-deforming, "
                        "non-translation casters through a CSM-local "
                        "input-assembler path. Named profiles leave this off.");

                    if (!viewDependentCasterCullingAvailable)
                        ImGui::BeginDisabled();
                    customChanged |= ImGui::Checkbox(
                        "Receiver Raster Scissor", &shadows.receiverRasterScissorEnabled);
                    ImGui::SetItemTooltip(
                        "Conservatively scissor uncached full-redraw caster "
                        "raster to the snapped receiver footprint.");
                    customChanged |=
                        ImGui::Checkbox("Accurate Caster Hull Culling",
                                        &shadows.accurateCasterCullingEnabled);
                    ImGui::SetItemTooltip(
                        "Reject reliable bounds outside the one-sided "
                        "light-extruded receiver hull during full redraws.");
                    customChanged |=
                        ImGui::Checkbox("UE Caster Radius Threshold",
                                        &shadows.ueCasterRadiusThresholdEnabled);
                    ImGui::SetItemTooltip(
                        "Apply UE's camera-projected caster-size rejection "
                        "only when no map-cache policy is active.");
                    if (!viewDependentCasterCullingAvailable)
                        ImGui::EndDisabled();

                    const bool casterRadiusThresholdAvailable =
                        viewDependentCasterCullingAvailable &&
                        shadows.ueCasterRadiusThresholdEnabled;
                    if (!casterRadiusThresholdAvailable)
                        ImGui::BeginDisabled();
                    customChanged |=
                        DrawSliderFloat("Caster Radius Threshold##DiagnosticCsm",
                                        &shadows.casterRadiusThreshold,
                                        0.f,
                                        0.05f,
                                        "%.3f");
                    ImGui::SetItemTooltip(
                        "Cull reliable casters whose bounding-sphere radius is "
                        "below this fraction of camera distance; UE's reference "
                        "value is 0.01.");
                    if (!casterRadiusThresholdAvailable)
                        ImGui::EndDisabled();

                    ImGui::TreePop();
                }

                drawUnabstracted();

                EndAnimatedTreeNode();
            }

            if (customChanged)
                shadows.profile = DiagnosticCsmProfile::Custom;

            if (BeginAnimatedTreeNode("Diagnostics##DiagnosticCsm"))
            {
                ImGui::Checkbox("Detailed GPU Stage Timing##DiagnosticCsm",
                                &shadows.detailedGpuTimingEnabled);
                ImGui::SetItemTooltip(
                    "Measure clear/update, raster, and full-resolution sampling "
                    "separately; setup and caster culling remain CPU timings.");

                static const char* debugLabels[] = {
                    "Off", "Visibility", "Cascade Selection", "Cache Action"};
                const int debugIndex = std::clamp(
                    int(shadows.debugView), 0, int(std::size(debugLabels)) - 1);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo("Debug View##DiagnosticCascadedShadowMaps",
                                      debugLabels[debugIndex]))
                {
                    for (int index = 0; index < int(std::size(debugLabels)); ++index)
                    {
                        const auto debugView = DiagnosticCsmDebugView(index);
                        const bool selected = shadows.debugView == debugView;
                        if (ImGui::Selectable(debugLabels[index], selected))
                        {
                            shadows.debugView = debugView;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Present the CSM visibility, selected cascade, or cache action "
                    "without changing its lighting input.");

                ImGui::TextDisabled(
                    "Diagnostic only: one persistent depth array, one caster path, and "
                    "one full-resolution R8 receiver are shared by every update policy.");
                if (m_HasDiagnosticCsmStatSnapshot)
                {
                    for (size_t lineIndex = 0;
                         lineIndex < m_DiagnosticCsmStatLines.size();
                         ++lineIndex)
                    {
                        const std::string& line = m_DiagnosticCsmStatLines[lineIndex];
                        ImGui::TextUnformatted(line.c_str());
                        if (lineIndex == 10u)
                        {
                            ImGui::SetItemTooltip(
                                "Trend estimate for this same adapter and exact workload "
                                "only. "
                                "It scales raw GPU time by unsmoothed current-clock FP32 "
                                "capacity, "
                                "never by utilization. Raw GPU and every CPU timing "
                                "remain authoritative.");
                        }
                    }
                }
                else if (shadows.enabled)
                {
                    const DiagnosticCsmTimings* timings =
                        m_app->GetDiagnosticCascadedShadowMapTimings();
                    if (timings && !timings->supported)
                    {
                        ImGui::TextDisabled(
                            "Unavailable: this adapter lacks sampleable/loadable D16 and "
                            "D32 depth or R8 UAV support, or CSM initialization failed.");
                    }
                    else
                    {
                        ImGui::TextDisabled("Waiting for the first valid CSM frame and "
                                            "retired GPU timing query.");
                    }
                }

                EndAnimatedTreeNode();
            }

            if (!shadows.enabled)
                ImGui::EndDisabled();
            if (!shadowsAvailable)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled(
                    "Requires deferred UVSR PBR rendering and a directional light.");
            }
            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool bendShadowsOpen = DrawCollapsingHeader(
            "Screen-Space Shadows",
            "Configure Bend Studio's directional screen-space shadow tracer.");
        if (bendShadowsOpen)
        {
            BeginDrawerBody(
                "##BendScreenSpaceShadowsBody",
                settingsControlWidth);
            BendScreenSpaceShadowSettings& shadows =
                m_ui.BendScreenSpaceShadows;
            const bool shadowsAvailable =
                m_ui.EnablePbr &&
                m_ui.UsesDeferredShading() &&
                m_app->HasPrimaryDirectionalLight();
            if (!shadowsAvailable)
                ImGui::BeginDisabled();

            ImGui::Checkbox(
                "Enabled##BendScreenSpaceShadows",
                &shadows.enabled);
            ImGui::SetItemTooltip(
                "Trace the existing depth buffer for the primary directional light.");

            if (!shadows.enabled)
                ImGui::BeginDisabled();

            static const char* presetLabels[] = {
                "Performance",
                "Balanced",
                "Quality",
                "Custom"
            };
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Profile##BendScreenSpaceShadows",
                    presetLabels[int(shadows.preset)]))
            {
                for (int index = 0;
                    index < int(std::size(presetLabels));
                    ++index)
                {
                    const BendShadowPreset preset =
                        BendShadowPreset(index);
                    const bool selected = shadows.preset == preset;
                    if (ImGui::Selectable(
                            presetLabels[index],
                            selected))
                    {
                        ApplyBendShadowPreset(shadows, preset);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Trade trace reach and cost: 60 pixels for Performance, 240 for Balanced, or 960 for Quality.");

            bool customChanged = false;
            static const char* lengthLabels[] = {
                "60 Pixels",
                "120 Pixels",
                "240 Pixels",
                "480 Pixels",
                "960 Pixels"
            };
            const int selectedLength = FindBendShadowCompiledValue(
                BendShadowSampleCounts,
                GetBendShadowSampleCount(shadows.length));
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Length##BendScreenSpaceShadows",
                    selectedLength >= 0
                        ? lengthLabels[selectedLength]
                        : "Unsupported"))
            {
                for (int index = 0;
                    index < int(std::size(lengthLabels));
                    ++index)
                {
                    const BendShadowLength length =
                        BendShadowLength(
                            BendShadowSampleCounts[size_t(index)]);
                    const bool selected = shadows.length == length;
                    if (ImGui::Selectable(
                            lengthLabels[index],
                            selected))
                    {
                        shadows.length = length;
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Select a precompiled SAMPLE_COUNT shadow length.");

            customChanged |= DrawSliderFloat(
                "Surface Thickness##BendScreenSpaceShadows",
                &shadows.surfaceThickness,
                0.0f,
                0.05f,
                "%.4f");
            ImGui::SetItemTooltip(
                "Set Bend's nonlinear-depth occluder thickness.");
            customChanged |= DrawSliderFloat(
                "Bilinear Threshold##BendScreenSpaceShadows",
                &shadows.bilinearThreshold,
                0.0f,
                0.1f,
                "%.3f");
            ImGui::SetItemTooltip(
                "Set the relative depth discontinuity that disables interpolation.");
            customChanged |= DrawSliderFloat(
                "Shadow Contrast##BendScreenSpaceShadows",
                &shadows.shadowContrast,
                1.0f,
                16.0f,
                "%.1f");
            ImGui::SetItemTooltip(
                "Set Bend's visibility transition contrast.");

            static const char* hardSampleLabels[] = {
                "0", "4", "8"
            };
            const int selectedHard = FindBendShadowCompiledValue(
                BendShadowHardSampleCounts,
                shadows.hardShadowSamples);
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Hard Shadow Samples##BendScreenSpaceShadows",
                    selectedHard >= 0
                        ? hardSampleLabels[selectedHard]
                        : "Unsupported"))
            {
                for (int index = 0;
                    index < int(std::size(hardSampleLabels));
                    ++index)
                {
                    const uint32_t value =
                        BendShadowHardSampleCounts[size_t(index)];
                    const bool selected =
                        shadows.hardShadowSamples == value;
                    if (ImGui::Selectable(
                            hardSampleLabels[index],
                            selected))
                    {
                        shadows.hardShadowSamples = value;
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Select the compiled count of fully hard contact samples.");

            static const char* fadeSampleLabels[] = {
                "0", "8", "16"
            };
            const int selectedFade = FindBendShadowCompiledValue(
                BendShadowFadeSampleCounts,
                shadows.fadeOutSamples);
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Fade-Out Samples##BendScreenSpaceShadows",
                    selectedFade >= 0
                        ? fadeSampleLabels[selectedFade]
                        : "Unsupported"))
            {
                for (int index = 0;
                    index < int(std::size(fadeSampleLabels));
                    ++index)
                {
                    const uint32_t value =
                        BendShadowFadeSampleCounts[size_t(index)];
                    const bool selected =
                        shadows.fadeOutSamples == value;
                    if (ImGui::Selectable(
                            fadeSampleLabels[index],
                            selected))
                    {
                        shadows.fadeOutSamples = value;
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Select the compiled count of samples that soften the trace endpoint.");

            customChanged |= ImGui::Checkbox(
                "Ignore Edge Pixels##BendScreenSpaceShadows",
                &shadows.ignoreEdgePixels);
            ImGui::SetItemTooltip(
                "Prevent detected depth-edge pixels from casting shadows.");
            customChanged |= ImGui::Checkbox(
                "Precision Offset##BendScreenSpaceShadows",
                &shadows.usePrecisionOffset);
            ImGui::SetItemTooltip(
                "Apply Bend's optional depth precision offset.");
            customChanged |= ImGui::Checkbox(
                "Bilinear Offset Mode##BendScreenSpaceShadows",
                &shadows.bilinearSamplingOffsetMode);
            ImGui::SetItemTooltip(
                "Offset bilinear samples onto the shared wavefront ray.");
            customChanged |= ImGui::Checkbox(
                "Early Out##BendScreenSpaceShadows",
                &shadows.useEarlyOut);
            ImGui::SetItemTooltip(
                shadows.debugView == BendShadowDebugView::None
                    ? "Skip pixels outside Bend's directional depth bounds."
                    : "Bend suppresses effective early-out while a debug view is active.");

            static const char* debugLabels[] = {
                "Off",
                "Edge",
                "Thread",
                "Wave"
            };
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Debug View##BendScreenSpaceShadows",
                    debugLabels[int(shadows.debugView)]))
            {
                for (int index = 0;
                    index < int(std::size(debugLabels));
                    ++index)
                {
                    const BendShadowDebugView debugView =
                        BendShadowDebugView(index);
                    const bool selected =
                        shadows.debugView == debugView;
                    if (ImGui::Selectable(
                            debugLabels[index],
                            selected))
                    {
                        shadows.debugView = debugView;
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Show the raw R8 edge, thread, or wave diagnostic output.");

            if (customChanged)
                shadows.preset = BendShadowPreset::Custom;

            if (m_HasBendShadowStatSnapshot)
            {
                ImGui::TextUnformatted(
                    m_BendShadowStatLines[0].c_str());
                ImGui::SetItemTooltip(
                    "Recent GPU time includes the white clear and all Bend dispatches.");
                ImGui::TextUnformatted(
                    m_BendShadowStatLines[1].c_str());
                ImGui::SetItemTooltip(
                    "Selected compile-time sample count and logical R8 payload.");
            }

            if (!shadows.enabled)
                ImGui::EndDisabled();
            if (!shadowsAvailable)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled(
                    "Requires deferred UVSR PBR rendering and a directional light.");
            }
            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool temporalAAOpen = DrawCollapsingHeader(
            "Temporal Anti-Aliasing",
            "Configure the MiniEngine temporal anti-aliasing experiment.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (temporalAAOpen)
        {
            BeginDrawerBody(
                "##TemporalAABody",
                settingsControlWidth);
            const bool temporalAAVisibilityConflict =
                m_ui.HasMiniEngineTaaVisibilityConflict();
            const bool temporalAAAvailable = IsMiniEngineTaaAvailable(
                true,
                m_ui.EnablePbr,
                m_ui.UsesDeferredShading(),
                m_ui.ScreenSpaceVisibility.reconstruction.temporalEnabled,
                m_ui.ScreenSpaceVisibility.sampling.adaptiveSparseSamplingEnabled);
            if (!temporalAAAvailable)
                ImGui::BeginDisabled();
            if (ImGui::Checkbox(
                    "Enabled##MiniEngineTAA",
                    &m_ui.EnableMiniEngineTaa))
            {
                log::info(
                    "MiniEngine temporal anti-aliasing %s",
                    m_ui.EnableMiniEngineTaa ? "enabled" : "disabled");
            }
            ImGui::SetItemTooltip(
                "Jitter and accumulate scene-linear color using Microsoft's MiniEngine TAA.");

            ImGui::Checkbox(
                "Sharpen##MiniEngineTAA",
                &m_ui.EnableMiniEngineTaaSharpen);
            ImGui::SetItemTooltip(
                "Use MiniEngine's sharpening output pass. When disabled, use its plain resolve pass.");

            if (!m_ui.EnableMiniEngineTaaSharpen)
                ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(settingsControlWidth);
            DrawSliderFloat(
                "Sharpness##MiniEngineTAA",
                &m_ui.MiniEngineTaaSharpness,
                MiniEngineTaaMinimumSharpness,
                MiniEngineTaaMaximumSharpness,
                "%.2f");
            ImGui::SetItemTooltip(
                "MiniEngine sharpening strength. The reference default is 0.50.");
            if (!m_ui.EnableMiniEngineTaaSharpen)
                ImGui::EndDisabled();

            if (m_HasTemporalAAStatSnapshot)
            {
                ImGui::TextUnformatted(
                    m_TemporalAAStatLines[0].c_str());
                ImGui::SetItemTooltip(
                    "Recent GPU time for the two MiniEngine TAA dispatches.");
                ImGui::TextUnformatted(
                    m_TemporalAAStatLines[1].c_str());
                ImGui::SetItemTooltip(
                    "Logical color and depth history payload before API padding.");
            }

            if (!temporalAAAvailable)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled(
                    temporalAAVisibilityConflict
                        ? "Disable Visibility Temporal and Adaptive Sampling first."
                        : "Requires deferred UVSR PBR motion and depth.");
            }
            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool skyOpen = DrawCollapsingHeader(
            "Sky", "Show sky controls.");
        if (skyOpen)
        {
            BeginDrawerBody(
                "##SkyBody",
                settingsControlWidth);

            ImGui::TextUnformatted("Environment");
            const ImageBasedLightingSourceInfo&
                selectedEnvironmentInfo =
                    GetImageBasedLightingSourceInfo(
                        m_ui.EnvironmentSource);
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "##SkyEnvironment",
                    selectedEnvironmentInfo.displayName))
            {
                for (uint32_t index = 0u;
                    index <
                        uint32_t(ImageBasedLightingSource::Count);
                    ++index)
                {
                    const ImageBasedLightingSource source =
                        ImageBasedLightingSource(index);
                    const ImageBasedLightingSourceInfo& info =
                        GetImageBasedLightingSourceInfo(source);
                    const bool selected =
                        source == m_ui.EnvironmentSource;
                    if (ImGui::Selectable(
                            info.displayName,
                            selected))
                    {
                        const bool presentationChanged =
                            !selected ||
                            m_ui.EnvironmentExposureStops !=
                                info.defaultExposureStops;
                        m_ui.EnvironmentSource = source;
                        m_ui.EnvironmentExposureStops =
                            info.defaultExposureStops;
                        if (presentationChanged)
                        {
                            m_app->
                                ResetImageBasedLightingHistory(
                                    true);
                        }
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose one imported scene-linear radiance source for "
                "diffuse convolution, roughness-prefiltered specular, and "
                "the optional matching background. Night sources do not "
                "rewrite the separate scene light.");

            if (DrawSliderFloat(
                    "Exposure##ImageBasedLighting",
                    &m_ui.EnvironmentExposureStops,
                    -8.f,
                    8.f,
                    "%+.2f EV"))
            {
                m_app->ResetImageBasedLightingHistory(true);
            }
            ImGui::SetItemTooltip(
                "Scale diffuse IBL, specular IBL, and the matching "
                "background together. Changing exposure does not rebuild "
                "the source or its prefilter.");

            if (ImGui::Checkbox(
                    "Diffuse IBL",
                    &m_ui.EnableDiffuseIbl))
            {
                m_app->ResetImageBasedLightingHistory(true);
            }
            ImGui::SetItemTooltip(
                "Use the physically normalized SH9 diffuse convolution of "
                "the selected radiance source. Disabled is exactly zero "
                "with no hidden hemispherical fallback.");
            if (DrawSliderFloat(
                    "Diffuse Strength##ImageBasedLighting",
                    &m_ui.DiffuseIblStrength,
                    0.f,
                    2.f,
                    "%.2f"))
            {
                m_app->ResetImageBasedLightingHistory(true);
            }
            ImGui::SetItemTooltip(
                "Scale diffuse environment lighting after common exposure. "
                "This also scales the same environment contribution entering "
                "screen-space diffuse GI. 1.00 is the radiometric reference.");

            if (ImGui::Checkbox(
                    "Specular IBL",
                    &m_ui.EnableSpecularIbl))
            {
                m_app->ResetImageBasedLightingHistory(false);
            }
            ImGui::SetItemTooltip(
                "Use the GGX-prefiltered radiance cube and environment BRDF "
                "LUT for roughness-dependent reflections.");
            if (DrawSliderFloat(
                    "Specular Strength##ImageBasedLighting",
                    &m_ui.SpecularIblStrength,
                    0.f,
                    2.f,
                    "%.2f"))
            {
                m_app->ResetImageBasedLightingHistory(false);
            }
            ImGui::SetItemTooltip(
                "Scale only specular environment lighting after common "
                "exposure. The matching background and diffuse lobe are "
                "unchanged. 1.00 is the radiometric reference.");

            if (ImGui::Checkbox(
                    "Show Environment Background",
                    &m_ui.ShowEnvironmentBackground))
            {
                m_app->ResetImageBasedLightingHistory(false);
            }
            ImGui::SetItemTooltip(
                "Display the same selected and exposure-scaled radiance "
                "source used by diffuse and specular IBL. Disabling this "
                "does not disable environment lighting.");

            const float environmentMean =
                m_app->
                    GetImageBasedLightingSourceAverageLuminance();
            const float commonEnvironmentMean =
                environmentMean *
                m_app->GetImageBasedLightingRadianceScale();
            const float diffuseEnvironmentMean =
                IsImageBasedLightingLobeActive(
                    m_ui.EnableDiffuseIbl,
                    m_ui.DiffuseIblStrength)
                    ? commonEnvironmentMean *
                        m_ui.DiffuseIblStrength
                    : 0.f;
            const float specularEnvironmentMean =
                IsImageBasedLightingLobeActive(
                    m_ui.EnableSpecularIbl,
                    m_ui.SpecularIblStrength)
                    ? commonEnvironmentMean *
                        m_ui.SpecularIblStrength
                    : 0.f;
            ImGui::TextDisabled(
                "Source %.4f / Common %.4f",
                environmentMean,
                commonEnvironmentMean);
            ImGui::TextDisabled(
                "Diffuse %.4f / Specular %.4f",
                diffuseEnvironmentMean,
                specularEnvironmentMean);
            ImGui::SetItemTooltip(
                "Scene-linear mean source radiance after common exposure "
                "and each independent lobe strength. These diagnostics are "
                "not display-referred values.");
            EndDrawerBody();
        }
        ImGui::Spacing();

        const auto& lights = m_app->GetScene()->GetSceneGraph()->GetLights();
        if (lights.empty())
        {
            m_SelectedLight.reset();
        }
        else if (std::find(lights.begin(), lights.end(), m_SelectedLight) == lights.end())
        {
            m_SelectedLight = lights.front();
        }

        const bool lightsOpen = DrawCollapsingHeader(
            "Lights", "Show scene light controls.");
        if (lightsOpen)
        {
            BeginDrawerBody(
                "##LightsBody",
                settingsControlWidth);
            if (!lights.empty())
            {
                ImGui::SetNextItemWidth(settingsControlWidth);
                const bool lightComboOpen = BeginRoundedCombo(
                    "Select Light", m_SelectedLight ? m_SelectedLight->GetName().c_str() : "(None)");
                ImGui::SetItemTooltip("Choose a light to edit.");
                if (lightComboOpen)
                {
                    for (const auto& light : lights)
                    {
                        bool selected = m_SelectedLight == light;
                        ImGui::Selectable(light->GetName().c_str(), &selected);
                        if (selected)
                        {
                            m_SelectedLight = light;
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }

                if (m_SelectedLight)
                {
                    app::LightEditor(*m_SelectedLight);
                }
            }
            EndDrawerBody();
        }
        ImGui::Spacing();

        constexpr float ActionButtonCount = 3.f;
        const float actionButtonWidth = std::max(
            1.f,
            (ImGui::GetContentRegionAvail().x -
                style.ItemSpacing.x * (ActionButtonCount - 1.f)) /
                ActionButtonCount);

        const ImVec4 drawerBackgroundColor(
            0.66f, 0.67f, 0.69f, 0.13f);
        const ImVec4 drawerBackgroundHoveredColor(
            0.74f, 0.75f, 0.77f, 0.20f);
        const ImVec4 drawerBackgroundActiveColor(
            0.80f, 0.81f, 0.83f, 0.26f);
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            drawerBackgroundColor);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            drawerBackgroundHoveredColor);
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            drawerBackgroundActiveColor);

        if (DrawCenteredActionButton("Reset", actionButtonWidth))
            m_app->ResetAllRendererSettings();
        ImGui::SetItemTooltip(
            "Restore factory settings without changing the camera or scene.");

        ImGui::SameLine();
        if (DrawCenteredActionButton("Screenshot", actionButtonWidth))
            m_ui.CopyScreenshotToClipboard = true;
        ImGui::SetItemTooltip("Copy the current frame to the clipboard.");

        ImGui::SameLine();
        if (DrawCenteredActionButton("Restart", actionButtonWidth))
        {
            g_RestartRequested = true;
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
        }
        ImGui::SetItemTooltip("Restart UVSR.");
        ImGui::PopStyleColor(3);

        ImGui::EndChild();
        ImGui::End();
        ImGui::PopStyleColor(3);

        if (m_ui.ShowMaterialEditor)
        {
            ImGui::SetNextWindowPos(ImVec2(float(width) - fontSize * 0.6f, fontSize * 0.6f), 0, ImVec2(1.f, 0.f));
            const bool materialEditorVisible = ImGui::Begin(
                "Material Editor",
                &m_ui.ShowMaterialEditor,
                ImGuiWindowFlags_AlwaysAutoResize);
            CaptureCurrentWindowBackdrop(
                m_ui.BackdropRects[1],
                style.WindowRounding);

            if (materialEditorVisible)
            {
                auto material = m_ui.SelectedMaterial;
                if (material)
                {
                    ImGui::Text(
                        "Material %d: %s",
                        material->materialID,
                        material->name.c_str());

                    MaterialDomain previousDomain = material->domain;
                    material->dirty =
                        donut::app::MaterialEditor(material.get(), true);

                    if (material->dirty ||
                        previousDomain != material->domain)
                    {
                        m_app->GetScene()
                            ->GetSceneGraph()
                            ->GetRootNode()
                            ->InvalidateContent();
                    }
                }
                else
                {
                    ImGui::TextDisabled(
                        "Click a scene surface to select a material.");
                }
            }

            ImGui::End();
        }

        ImGui::PopFont();
    }
};

bool ProcessCommandLine(
    int argc,
    const char* const* argv,
    DeviceCreationParameters& deviceParams,
    std::string& sceneName,
    std::string& experimentDescription,
    bool& benchmarkCameraRequested,
    bool& diagnosticCsmBenchmarkRequested,
    bool& diagnosticCsmRecordRequested,
    bool& diagnosticCsmTranslationBaselineRequested,
    bool& diagnosticCsmInputAssemblerRequested,
    bool& diagnosticCsmDetailedTimingRequested,
    bool& svsmMotionBenchmarkRequested,
    bool& svsmSunMotionBenchmarkRequested,
    bool& svsmMotionDetailedTimingRequested,
    bool& svsmMotionScatterRequested,
    bool& svsmMotionIsolationRequested,
    bool& svsmMotionUnbatchedRequested,
    bool& svsmMotionBatchOnlyRequested,
    bool& svsmMotionBatchSortRequested,
    bool& svsmMotionBatchLevelSkipRequested,
    uint32_t& svsmMotionPoolPageCount,
    bool& svsmMotionPoolOverrideRequested,
    uint32_t& svsmMotionPageRenderBudget,
    bool& svsmMotionBudgetOverrideRequested,
    bool& dredDiagnosticsRequested)
{
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-width"))
        {
            int value = 0;
            if (i + 1 >= argc ||
                !ParseCommandLineInt(
                    argv[i + 1],
                    1,
                    std::numeric_limits<int>::max(),
                    value))
            {
                log::error("-width requires an exact positive integer value");
                return false;
            }
            deviceParams.backBufferWidth = value;
            ++i;
        }
        else if (!strcmp(argv[i], "-height"))
        {
            int value = 0;
            if (i + 1 >= argc ||
                !ParseCommandLineInt(
                    argv[i + 1],
                    1,
                    std::numeric_limits<int>::max(),
                    value))
            {
                log::error("-height requires an exact positive integer value");
                return false;
            }
            deviceParams.backBufferHeight = value;
            ++i;
        }
        else if (!strcmp(argv[i], "-fullscreen"))
        {
            deviceParams.startFullscreen = true;
        }
        else if (!strcmp(argv[i], "-debug"))
        {
            deviceParams.enableDebugRuntime = true;
            deviceParams.enableNvrhiValidationLayer = true;
        }
        else if (!strcmp(argv[i], "-adapter"))
        {
            int value = 0;
            if (i + 1 >= argc ||
                !ParseCommandLineInt(
                    argv[i + 1],
                    0,
                    std::numeric_limits<int>::max(),
                    value))
            {
                log::error("-adapter requires an exact nonnegative integer value");
                return false;
            }
            deviceParams.adapterIndex = value;
            ++i;
        }
        else if ((!strcmp(argv[i], "--experiment") || !strcmp(argv[i], "-experiment"))
            && i + 1 < argc)
        {
            experimentDescription = argv[++i];
        }
        else if (!strcmp(argv[i], "--benchmark-camera"))
        {
            benchmarkCameraRequested = true;
        }
        else if (!strcmp(argv[i], "--diagnostic-csm-benchmark"))
        {
            diagnosticCsmBenchmarkRequested = true;
        }
        else if (!strcmp(argv[i], "--diagnostic-csm-record"))
        {
            diagnosticCsmRecordRequested = true;
        }
        else if (!strcmp(
                     argv[i],
                     "--diagnostic-csm-translation-baseline"))
        {
            diagnosticCsmTranslationBaselineRequested = true;
        }
        else if (!strcmp(
                     argv[i],
                     "--diagnostic-csm-input-assembler"))
        {
            diagnosticCsmInputAssemblerRequested = true;
        }
        else if (!strcmp(argv[i], "--diagnostic-csm-detailed"))
        {
            diagnosticCsmDetailedTimingRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-test"))
        {
            svsmMotionBenchmarkRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-sun-motion-test"))
        {
            svsmMotionBenchmarkRequested = true;
            svsmSunMotionBenchmarkRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-detailed"))
        {
            svsmMotionDetailedTimingRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-scatter"))
        {
            svsmMotionScatterRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-isolate"))
        {
            svsmMotionIsolationRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-unbatched"))
        {
            svsmMotionUnbatchedRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-batch-only"))
        {
            svsmMotionBatchOnlyRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-batch-sort"))
        {
            svsmMotionBatchSortRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-batch-level-skip"))
        {
            svsmMotionBatchLevelSkipRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-pool") &&
            i + 1 < argc)
        {
            if (!ParseCommandLineUint32(
                    argv[++i],
                    svsmMotionPoolPageCount))
            {
                log::error(
                    "--svsm-motion-pool requires an exact unsigned integer value");
                return false;
            }
            svsmMotionPoolOverrideRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-pool"))
        {
            log::error("--svsm-motion-pool requires a value");
            return false;
        }
        else if (!strcmp(argv[i], "--svsm-motion-budget") &&
            i + 1 < argc)
        {
            if (!ParseCommandLineUint32(
                    argv[++i],
                    svsmMotionPageRenderBudget))
            {
                log::error(
                    "--svsm-motion-budget requires an exact unsigned integer value");
                return false;
            }
            svsmMotionBudgetOverrideRequested = true;
        }
        else if (!strcmp(argv[i], "--svsm-motion-budget"))
        {
            log::error("--svsm-motion-budget requires a value");
            return false;
        }
        else if (!strcmp(argv[i], "--dred"))
        {
            dredDiagnosticsRequested = true;
        }
        else if (IsDonutGraphicsApiOption(argv[i]))
        {
            // Donut consumes this token when it creates the device manager.
        }
        else if (!std::strncmp(
                     argv[i],
                     "--diagnostic-csm-",
                     17u))
        {
            log::error(
                "Unknown diagnostic CSM option '%s'",
                argv[i]);
            return false;
        }
        else if (!std::strncmp(argv[i], "--svsm-motion-", 14u))
        {
            log::error("Unknown SVSM motion option '%s'", argv[i]);
            return false;
        }
        else if (argv[i][0] != '-')
        {
            sceneName = argv[i];
        }
        else
        {
            log::error("Unknown command-line option '%s'", argv[i]);
            return false;
        }
    }
    return true;
}

std::string FormatExperimentLaunchTime(
    const std::chrono::system_clock::time_point& launchTime)
{
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(launchTime);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif

    std::ostringstream formattedTime;
    formattedTime << std::setfill('0') << std::setw(2) << localTime.tm_hour
        << std::setw(2) << localTime.tm_min;
    return formattedTime.str();
}

bool SelectGraphicsAdapter(
    DeviceManager* deviceManager,
    DeviceCreationParameters& deviceParams,
    std::vector<GpuAdapterChoice>& adapterChoices)
{
    // Donut's DX12 fallback selects DXGI adapter zero. On hybrid laptops that
    // is commonly the integrated GPU even when a much faster discrete GPU is
    // available. Enumerate once before device creation and prefer the usable
    // adapter with the most dedicated video memory. This is stable across
    // machines and avoids hard-coding a vendor name or a machine-specific
    // adapter index.
    if (!deviceManager->CreateInstance(deviceParams))
    {
        log::error("Cannot initialize DXGI while selecting a graphics adapter");
        return false;
    }

    std::vector<AdapterInfo> adapters;
    if (!deviceManager->EnumerateAdapters(adapters) || adapters.empty())
    {
        log::error("Cannot enumerate DXGI graphics adapters");
        return false;
    }

    adapterChoices.clear();
    const bool automaticSelection = deviceParams.adapterIndex < 0;
    int bestAdapterIndex = -1;
    uint64_t bestDedicatedVideoMemory = 0;
    for (size_t index = 0; index < adapters.size(); ++index)
    {
        const AdapterInfo& adapter = adapters[index];
        nvrhi::RefCountPtr<IDXGIAdapter1> adapter1;
        DXGI_ADAPTER_DESC1 adapterDescription{};
        if (FAILED(adapter.dxgiAdapter->QueryInterface(IID_PPV_ARGS(&adapter1))) ||
            FAILED(adapter1->GetDesc1(&adapterDescription)) ||
            (adapterDescription.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
        {
            continue;
        }

        if (FAILED(D3D12CreateDevice(
                adapter.dxgiAdapter,
                deviceParams.featureLevel,
                __uuidof(ID3D12Device),
                nullptr)))
        {
            continue;
        }

        adapterChoices.push_back(GpuAdapterChoice{
            static_cast<int>(index),
            adapter.name,
            adapter.dedicatedVideoMemory
        });

        if (automaticSelection &&
            (bestAdapterIndex < 0 || adapter.dedicatedVideoMemory > bestDedicatedVideoMemory))
        {
            bestAdapterIndex = static_cast<int>(index);
            bestDedicatedVideoMemory = adapter.dedicatedVideoMemory;
        }
    }

    if (adapterChoices.empty())
    {
        log::error("No enumerated adapter supports the requested D3D12 feature level");
        return false;
    }

    if (automaticSelection)
        deviceParams.adapterIndex = bestAdapterIndex;

    const auto selectedChoice = std::find_if(
        adapterChoices.begin(),
        adapterChoices.end(),
        [&deviceParams](const GpuAdapterChoice& choice)
        {
            return choice.adapterIndex == deviceParams.adapterIndex;
        });
    if (selectedChoice == adapterChoices.end())
    {
        log::error(
            "Requested DXGI adapter %d is unavailable or does not support the requested D3D12 feature level",
            deviceParams.adapterIndex);
        return false;
    }

    log::info(
        "Selected graphics adapter %d: %s (%llu mib dedicated VRAM)",
        selectedChoice->adapterIndex,
        selectedChoice->name.c_str(),
        static_cast<unsigned long long>(selectedChoice->dedicatedVideoMemory / (1024ull * 1024ull)));
    return true;
}

void CenterWindowInPrimaryWorkArea(GLFWwindow* window)
{
    if (!window)
        return;

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor)
        return;

    int workX = 0;
    int workY = 0;
    int workWidth = 0;
    int workHeight = 0;
    glfwGetMonitorWorkarea(
        monitor,
        &workX,
        &workY,
        &workWidth,
        &workHeight);

    int frameLeft = 0;
    int frameTop = 0;
    int frameRight = 0;
    int frameBottom = 0;
    glfwGetWindowFrameSize(
        window,
        &frameLeft,
        &frameTop,
        &frameRight,
        &frameBottom);

    int clientWidth = 0;
    int clientHeight = 0;
    glfwGetWindowSize(window, &clientWidth, &clientHeight);
    const int maximumClientWidth = std::max(
        1,
        workWidth - frameLeft - frameRight);
    const int maximumClientHeight = std::max(
        1,
        workHeight - frameTop - frameBottom);

    if (clientWidth > maximumClientWidth ||
        clientHeight > maximumClientHeight)
    {
        const double fitScale = std::min(
            double(maximumClientWidth) / double(clientWidth),
            double(maximumClientHeight) / double(clientHeight));
        clientWidth = std::max(
            1,
            int(std::floor(double(clientWidth) * fitScale)));
        clientHeight = std::max(
            1,
            int(std::floor(double(clientHeight) * fitScale)));
        glfwSetWindowSize(window, clientWidth, clientHeight);
    }

    const int outerWidth =
        clientWidth + frameLeft + frameRight;
    const int outerHeight =
        clientHeight + frameTop + frameBottom;
    const int clientX =
        workX + (workWidth - outerWidth) / 2 + frameLeft;
    const int clientY =
        workY + (workHeight - outerHeight) / 2 + frameTop;
    glfwSetWindowPos(window, clientX, clientY);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
    {
        log::warning(
            "UVSR could not set its process priority to High (Win32 error %lu); continuing with the current priority",
            GetLastError());
    }
    else
    {
        log::info("UVSR process priority set to High");
    }

    const auto programLaunchTime = std::chrono::steady_clock::now();
    const auto launchTime = std::chrono::system_clock::now();
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
#else //  _WIN32
int main(int __argc, const char* const* __argv)
{
    const auto programLaunchTime = std::chrono::steady_clock::now();
    const auto launchTime = std::chrono::system_clock::now();
    nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::VULKAN;
#endif //  _WIN32

    DeviceCreationParameters deviceParams;
    
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.swapChainSampleCount = 1;
    deviceParams.swapChainBufferCount = 3;
    deviceParams.startFullscreen = false;
    deviceParams.enablePerMonitorDPI = true;
    deviceParams.supportExplicitDisplayScaling = true;
    
    std::string sceneName;
    std::string experimentDescription;
    bool benchmarkCameraRequested = false;
    bool diagnosticCsmBenchmarkRequested = false;
    bool diagnosticCsmRecordRequested = false;
    bool diagnosticCsmTranslationBaselineRequested = false;
    bool diagnosticCsmInputAssemblerRequested = false;
    bool diagnosticCsmDetailedTimingRequested = false;
    bool svsmMotionBenchmarkRequested = false;
    bool svsmSunMotionBenchmarkRequested = false;
    bool svsmMotionDetailedTimingRequested = false;
    bool svsmMotionScatterRequested = false;
    bool svsmMotionIsolationRequested = false;
    bool svsmMotionUnbatchedRequested = false;
    bool svsmMotionBatchOnlyRequested = false;
    bool svsmMotionBatchSortRequested = false;
    bool svsmMotionBatchLevelSkipRequested = false;
    uint32_t svsmMotionPoolPageCount = 4096u;
    bool svsmMotionPoolOverrideRequested = false;
    uint32_t svsmMotionPageRenderBudget = 4u;
    bool svsmMotionBudgetOverrideRequested = false;
    bool dredDiagnosticsRequested = false;
    std::filesystem::path svsmMotionMeasurementReadyPath;
    if (!ProcessCommandLine(
        __argc,
        __argv,
        deviceParams,
        sceneName,
        experimentDescription,
        benchmarkCameraRequested,
        diagnosticCsmBenchmarkRequested,
        diagnosticCsmRecordRequested,
        diagnosticCsmTranslationBaselineRequested,
        diagnosticCsmInputAssemblerRequested,
        diagnosticCsmDetailedTimingRequested,
        svsmMotionBenchmarkRequested,
        svsmSunMotionBenchmarkRequested,
        svsmMotionDetailedTimingRequested,
        svsmMotionScatterRequested,
        svsmMotionIsolationRequested,
        svsmMotionUnbatchedRequested,
        svsmMotionBatchOnlyRequested,
        svsmMotionBatchSortRequested,
        svsmMotionBatchLevelSkipRequested,
        svsmMotionPoolPageCount,
        svsmMotionPoolOverrideRequested,
        svsmMotionPageRenderBudget,
        svsmMotionBudgetOverrideRequested,
        dredDiagnosticsRequested))
    {
        return 1;
    }
    if (diagnosticCsmBenchmarkRequested && !benchmarkCameraRequested)
    {
        log::error(
            "--diagnostic-csm-benchmark requires --benchmark-camera so every run uses the standardized 1920 x 1080 camera");
        return 1;
    }
    if (diagnosticCsmTranslationBaselineRequested &&
        !diagnosticCsmBenchmarkRequested)
    {
        log::error(
            "--diagnostic-csm-translation-baseline requires --diagnostic-csm-benchmark");
        return 1;
    }
    if (diagnosticCsmInputAssemblerRequested &&
        !diagnosticCsmBenchmarkRequested)
    {
        log::error(
            "--diagnostic-csm-input-assembler requires --diagnostic-csm-benchmark");
        return 1;
    }
    if (diagnosticCsmRecordRequested &&
        !diagnosticCsmBenchmarkRequested)
    {
        log::error(
            "--diagnostic-csm-record requires --diagnostic-csm-benchmark");
        return 1;
    }
    if (diagnosticCsmDetailedTimingRequested &&
        !diagnosticCsmBenchmarkRequested)
    {
        log::error(
            "--diagnostic-csm-detailed requires --diagnostic-csm-benchmark");
        return 1;
    }
    if (diagnosticCsmBenchmarkRequested &&
        svsmMotionBenchmarkRequested)
    {
        log::error(
            "Diagnostic CSM and SVSM benchmark modes cannot run together");
        return 1;
    }
    if (diagnosticCsmBenchmarkRequested &&
        (deviceParams.enableDebugRuntime ||
            deviceParams.enableNvrhiValidationLayer ||
            dredDiagnosticsRequested))
    {
        log::error(
            "Diagnostic CSM benchmark mode does not allow debug, validation, or DRED instrumentation");
        return 1;
    }
    if (svsmMotionBenchmarkRequested && !benchmarkCameraRequested)
    {
        log::error(
            "--svsm-motion-test requires --benchmark-camera so every run uses the standardized 1920 x 1080 camera");
        return 1;
    }
    if (svsmMotionDetailedTimingRequested &&
        !svsmMotionBenchmarkRequested)
    {
        log::error(
            "--svsm-motion-detailed requires --svsm-motion-test");
        return 1;
    }
    if (svsmMotionScatterRequested && !svsmMotionBenchmarkRequested)
    {
        log::error(
            "--svsm-motion-scatter requires --svsm-motion-test");
        return 1;
    }
    if (svsmMotionIsolationRequested && !svsmMotionBenchmarkRequested)
    {
        log::error(
            "--svsm-motion-isolate requires --svsm-motion-test");
        return 1;
    }
    if (svsmMotionUnbatchedRequested && !svsmMotionBenchmarkRequested)
    {
        log::error(
            "--svsm-motion-unbatched requires --svsm-motion-test");
        return 1;
    }
    if (svsmMotionBudgetOverrideRequested &&
        !svsmMotionBenchmarkRequested)
    {
        log::error(
            "--svsm-motion-budget requires --svsm-motion-test");
        return 1;
    }
    const uint32_t svsmMotionBatchedDiagnosticCount =
        uint32_t(svsmMotionBatchOnlyRequested) +
        uint32_t(svsmMotionBatchSortRequested) +
        uint32_t(svsmMotionBatchLevelSkipRequested);
    const bool svsmMotionBatchedDiagnosticRequested =
        svsmMotionBatchedDiagnosticCount != 0u;
    if (svsmMotionBatchedDiagnosticRequested &&
        !svsmMotionBenchmarkRequested)
    {
        log::error(
            "SVSM batched diagnostics require --svsm-motion-test");
        return 1;
    }
    if (svsmMotionBatchedDiagnosticCount > 1u)
    {
        log::error(
            "Select only one SVSM batched diagnostic path");
        return 1;
    }
    if (svsmMotionIsolationRequested && svsmMotionScatterRequested)
    {
        log::error(
            "--svsm-motion-isolate cannot be combined with --svsm-motion-scatter");
        return 1;
    }
    if (svsmMotionUnbatchedRequested &&
        (svsmMotionIsolationRequested || svsmMotionScatterRequested ||
            svsmMotionBatchedDiagnosticRequested))
    {
        log::error(
            "--svsm-motion-unbatched cannot be combined with the isolate or scatter diagnostic paths");
        return 1;
    }
    if (svsmMotionBatchedDiagnosticRequested &&
        (svsmMotionIsolationRequested || svsmMotionScatterRequested))
    {
        log::error(
            "SVSM batched diagnostics cannot be combined with the isolate or scatter diagnostic paths");
        return 1;
    }
    if (svsmMotionBatchedDiagnosticRequested &&
        (!svsmMotionPoolOverrideRequested ||
            svsmMotionPoolPageCount != 64u))
    {
        log::error(
            "SVSM batched diagnostics require --svsm-motion-pool 64");
        return 1;
    }
    if (svsmMotionPoolOverrideRequested &&
        !svsmMotionBenchmarkRequested)
    {
        log::error(
            "--svsm-motion-pool requires --svsm-motion-test");
        return 1;
    }
    if (!IsSvsmMotionDiagnosticPoolPageCount(
            svsmMotionPoolPageCount))
    {
        log::error(
            "--svsm-motion-pool must be 64, 256, 1024, or 4096 pages");
        return 1;
    }
    if (svsmMotionPageRenderBudget == 0u ||
        svsmMotionPageRenderBudget > svsmMotionPoolPageCount)
    {
        log::error(
            "--svsm-motion-budget must be between 1 and the selected physical pool page count");
        return 1;
    }
    if (const char* readyPath =
            std::getenv("UVSR_SVSM_MOTION_READY_PATH");
        readyPath && readyPath[0] != '\0')
    {
        if (!svsmMotionBenchmarkRequested)
        {
            log::error(
                "UVSR_SVSM_MOTION_READY_PATH requires --svsm-motion-test");
            return 1;
        }
        std::error_code pathError;
        svsmMotionMeasurementReadyPath =
            std::filesystem::absolute(readyPath, pathError);
        if (pathError || svsmMotionMeasurementReadyPath.empty())
        {
            log::error(
                "UVSR_SVSM_MOTION_READY_PATH could not be resolved");
            return 1;
        }
        if (std::filesystem::exists(
                svsmMotionMeasurementReadyPath, pathError) || pathError)
        {
            log::error(
                "UVSR_SVSM_MOTION_READY_PATH must name a new marker file");
            return 1;
        }
    }
#ifdef _WIN32
    if (dredDiagnosticsRequested && api == nvrhi::GraphicsAPI::D3D12)
    {
        if (!EnableD3d12DredDiagnostics())
            return 1;
    }
#endif
    if (benchmarkCameraRequested)
    {
        const SponzaCameraPreset& preset = GetDefaultSponzaCameraPreset();
        deviceParams.backBufferWidth = preset.ReferenceWidth;
        deviceParams.backBufferHeight = preset.ReferenceHeight;
        deviceParams.startFullscreen = false;
        deviceParams.startMaximized = false;
    }
    if (experimentDescription.empty())
    {
        // The launcher passes the validated lowercase description through the
        // environment so scene paths remain the only native command-line
        // arguments it needs to reconstruct. An explicit argument remains the
        // override for IDE-driven launches and is validated by the same
        // renderer backstop below.
        const char* environmentExperiment = std::getenv("UVSR_EXPERIMENT");
        if (environmentExperiment && environmentExperiment[0] != '\0')
            experimentDescription = environmentExperiment;
    }
    if (experimentDescription.empty())
        experimentDescription = "main";
    if (!uvsr::IsValidExperimentTitle(experimentDescription))
    {
        log::error(
            "Experiment description '%s' must contain only lowercase ASCII letters (a-z)",
            experimentDescription.c_str());
        return 1;
    }

    // UVSR intentionally runs uncapped; the renderer no longer exposes or
    // maintains a runtime VSync mode.
    deviceParams.vsyncEnabled = false;
    
    DeviceManager* deviceManager = DeviceManager::Create(api);
    std::vector<GpuAdapterChoice> adapterChoices;
    if (!SelectGraphicsAdapter(deviceManager, deviceParams, adapterChoices))
    {
        delete deviceManager;
        return 1;
    }

    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    const std::string windowTitle = "UVSR Renderer " + std::string(apiString)
        + " (" + experimentDescription + "-" + std::string(UVSR_GIT_COMMIT)
        + "-" + FormatExperimentLaunchTime(launchTime) + ")";

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
	{
		log::error("Cannot initialize a %s graphics device with the requested parameters", apiString);
		return 1;
	}
#ifdef _WIN32
    NameD3d12GraphicsQueue(deviceManager->GetDevice());
#endif
    if (diagnosticCsmBenchmarkRequested)
    {
        // Keep the requested client area exact. Fitting a decorated window to
        // the desktop work area changes 1920 x 1080 into 1902 x 1069 on this
        // machine and biases the receiver comparison by nearly two percent.
        glfwSetWindowPos(deviceManager->GetWindow(), 0, 0);
    }
    else if (!deviceParams.startFullscreen &&
        !deviceParams.startMaximized)
    {
        CenterWindowInPrimaryWorkArea(
            deviceManager->GetWindow());
    }

    if (benchmarkCameraRequested)
    {
        GLFWwindow* benchmarkWindow = deviceManager->GetWindow();
        glfwSetWindowAttrib(benchmarkWindow, GLFW_RESIZABLE, GLFW_FALSE);
        g_BenchmarkForwardKeyCallback = glfwSetKeyCallback(
            benchmarkWindow,
            BenchmarkWindowKeyCallback);
    }

    {
        UIData uiData;
        uiData.GpuAdapterChoices = std::move(adapterChoices);
        uiData.ActiveGpuAdapterIndex = deviceParams.adapterIndex;
        if (diagnosticCsmBenchmarkRequested)
        {
            // Apply the complete matched CSM comparison state before the first
            // rendered frame. This avoids spending the laptop's short thermal
            // window configuring unrelated renderer features through the UI.
            uiData.EnablePbr = true;
            uiData.RenderMode = RendererMode::Deferred;
            uiData.EnableMiniEngineTaa = false;
            uiData.EnableMiniEngineTaaSharpen = false;
            uiData.BendScreenSpaceShadows.enabled = false;
            uiData.SparseVirtualShadowMaps.enabled = false;
            uiData.ScreenSpaceVisibility.enabled = false;
            uiData.DiagnosticCascadedShadowMaps =
                ApplyDiagnosticCsmProfile(
                    uiData.DiagnosticCascadedShadowMaps,
                    DiagnosticCsmProfile::Ue5CsmReference);
            uiData.DiagnosticCascadedShadowMaps.enabled = true;
            uiData.DiagnosticCascadedShadowMaps.detailedGpuTimingEnabled =
                diagnosticCsmDetailedTimingRequested;
            if (diagnosticCsmTranslationBaselineRequested)
            {
                uiData.DiagnosticCascadedShadowMaps.
                    translationOnlyCasterTransformEnabled = false;
                uiData.DiagnosticCascadedShadowMaps.profile =
                    DiagnosticCsmProfile::Custom;
            }
            if (diagnosticCsmInputAssemblerRequested)
            {
                uiData.DiagnosticCascadedShadowMaps.
                    inputAssemblerCasterFetchEnabled = true;
                uiData.DiagnosticCascadedShadowMaps.profile =
                    DiagnosticCsmProfile::Custom;
            }
        }
        if (svsmMotionBenchmarkRequested)
        {
            uiData.EnablePbr = true;
            uiData.RenderMode = RendererMode::Deferred;
            uiData.EnableMiniEngineTaa = false;
            uiData.EnableMiniEngineTaaSharpen = false;
            uiData.BendScreenSpaceShadows.enabled = false;
            uiData.DiagnosticCascadedShadowMaps.enabled = false;
            uiData.ScreenSpaceVisibility.enabled = false;
            uiData.SparseVirtualShadowMaps =
                BuildSvsmMotionBenchmarkAcceptanceSettings();
            uiData.SparseVirtualShadowMaps.physicalPageCount =
                svsmMotionPoolPageCount;
            ApplySvsmFinePageRenderBudget(
                uiData.SparseVirtualShadowMaps,
                svsmMotionPageRenderBudget);
            uiData.SparseVirtualShadowMaps.
                dirtyPageScatterRasterEnabled =
                    svsmMotionScatterRequested;

            if (svsmMotionIsolationRequested)
            {
                // A deliberately minimal diagnostic path used only to locate
                // device-removal boundaries. It is never accepted as timing
                // evidence and does not change any interactive reference path.
                SparseVirtualShadowMapSettings& shadows =
                    uiData.SparseVirtualShadowMaps;
                shadows.mode = SvsmMode::SparseUncached;
                shadows.markingMode = SvsmMarkingMode::PerPixel;
                shadows.filterMode = SvsmFilterMode::ManualPageSafe;
                shadows.filterKernel = SvsmFilterKernel::NearestPoisson;
                shadows.poissonOrdering =
                    SvsmPoissonOrdering::LegacyStride;
                shadows.tapCount = SvsmTapCount::One;
                shadows.resolutionBias = SvsmResolutionBias::Zero;
                shadows.adaptiveFiltering = false;
                shadows.cachingEnabled = false;
                shadows.staticPageRequestReuseEnabled = false;
                shadows.staticVisibilityCachingEnabled = false;
                shadows.sceneStateCachingEnabled = false;
                shadows.casterOnlySceneRevisionEnabled = false;
                shadows.precomposedClipmapTransformsEnabled = false;
                shadows.pageTranslationCachingEnabled = false;
                shadows.detailedGpuTimingEnabled = false;
                shadows.renderPacketCachingEnabled = false;
                shadows.sharedClipmapPacketBuilderEnabled = false;
                shadows.persistentCasterSourceCachingEnabled = false;
                shadows.opaqueRasterSpecializationEnabled = false;
                shadows.leanAlphaTestedBindingsEnabled = false;
                shadows.pairedStaticDynamicDepthEnabled = false;
                shadows.deferredStaticDepthMergeEnabled = false;
                shadows.movingLightUncachedEnabled = false;
                shadows.retainPhysicalMappingsOnContentInvalidationEnabled =
                    false;
                shadows.movingLightLodBiasEnabled = false;
                shadows.movingLightResolutionBias =
                    SvsmResolutionBias::Zero;
                shadows.receiverDistanceMipClampEnabled = false;
                shadows.movingLightContinuousReceiverBiasEnabled = false;
                shadows.gpuGatedDrawSubmission = false;
                shadows.batchedDrawSubmissionEnabled = false;
                shadows.packetStateSortingEnabled = false;
                shadows.levelEmptyWorkSkipEnabled = false;
                shadows.packetPageCullingEnabled = false;
                shadows.hierarchicalScheduledPageMaskEnabled = false;
                shadows.receiverPageMaskCullingEnabled = false;
                shadows.staticDepthHierarchyCullingEnabled = false;
                shadows.dirtyPageScatterRasterEnabled = false;
                shadows.scatterAlphaTestEarlyRejectEnabled = false;
                shadows.dirtyPageScatterAmplificationGuardEnabled = false;
                shadows.packetRectangleDirectScanEnabled = false;
                shadows.recentPageEvictionGraceEnabled = false;
                shadows.perPixelMarkingDedupeEnabled = false;
            }
            else if (svsmMotionUnbatchedRequested)
            {
                SparseVirtualShadowMapSettings& shadows =
                    uiData.SparseVirtualShadowMaps;
                shadows.tapCount = SvsmTapCount::One;
                shadows.resolutionBias = SvsmResolutionBias::PlusTwo;
                shadows.adaptiveFiltering = false;
                shadows.pageTranslationCachingEnabled = false;
                shadows.detailedGpuTimingEnabled = false;
                shadows.renderPacketCachingEnabled = true;
                shadows.gpuGatedDrawSubmission = true;
                shadows.batchedDrawSubmissionEnabled = false;
                shadows.packetStateSortingEnabled = false;
                shadows.levelEmptyWorkSkipEnabled = false;
                shadows.packetPageCullingEnabled = true;
                shadows.dirtyPageScatterRasterEnabled = false;
                shadows.packetRectangleDirectScanEnabled = false;
            }
            else if (svsmMotionBatchedDiagnosticRequested)
            {
                // Exercise exactly one batched-submission rung while retaining
                // the same small-pool, exact-list diagnostic baseline.
                SparseVirtualShadowMapSettings& shadows =
                    uiData.SparseVirtualShadowMaps;
                shadows.tapCount = SvsmTapCount::One;
                shadows.resolutionBias = SvsmResolutionBias::PlusTwo;
                shadows.adaptiveFiltering = false;
                shadows.pageTranslationCachingEnabled = false;
                shadows.detailedGpuTimingEnabled = false;
                shadows.renderPacketCachingEnabled = true;
                shadows.gpuGatedDrawSubmission = true;
                shadows.batchedDrawSubmissionEnabled = true;
                shadows.packetStateSortingEnabled =
                    svsmMotionBatchSortRequested;
                shadows.levelEmptyWorkSkipEnabled =
                    svsmMotionBatchLevelSkipRequested;
                shadows.packetPageCullingEnabled = true;
                shadows.dirtyPageScatterRasterEnabled = false;
                shadows.scatterAlphaTestEarlyRejectEnabled = false;
                shadows.dirtyPageScatterAmplificationGuardEnabled = false;
                shadows.packetRectangleDirectScanEnabled = false;
            }

            uiData.SparseVirtualShadowMaps.detailedGpuTimingEnabled =
                svsmMotionDetailedTimingRequested;
        }

        const bool svsmMotionDiagnosticConfiguration =
            svsmMotionDetailedTimingRequested ||
            svsmMotionIsolationRequested ||
            svsmMotionUnbatchedRequested ||
            svsmMotionBatchedDiagnosticRequested ||
            svsmMotionScatterRequested ||
            svsmMotionBudgetOverrideRequested ||
            deviceParams.enableDebugRuntime ||
            deviceParams.enableNvrhiValidationLayer ||
            svsmMotionPoolPageCount != 4096u;

        std::shared_ptr<UvsrSceneViewer> demo = std::make_shared<UvsrSceneViewer>(
            deviceManager,
            uiData,
            sceneName,
            benchmarkCameraRequested,
            diagnosticCsmBenchmarkRequested,
            diagnosticCsmRecordRequested,
            svsmMotionBenchmarkRequested,
            svsmSunMotionBenchmarkRequested,
            dredDiagnosticsRequested,
            svsmMotionDiagnosticConfiguration,
            svsmMotionMeasurementReadyPath);
        std::shared_ptr<UIRenderer> gui = std::make_shared<UIRenderer>(
            deviceManager,
            demo,
            uiData,
            programLaunchTime);

        gui->Init(demo->GetShaderFactory());

        deviceManager->AddRenderPassToBack(demo.get());
        deviceManager->AddRenderPassToBack(gui.get());

        deviceManager->RunMessageLoop();
#ifdef _WIN32
        if (dredDiagnosticsRequested)
        {
            (void)WriteD3d12DredReport(
                deviceManager->GetDevice(),
                "outputs/dred-latest.txt");
        }
#endif
    }

    deviceManager->Shutdown();
    delete deviceManager;

    if (g_RestartRequested && !RestartCurrentProcess())
        return 1;
	
	return 0;
}
