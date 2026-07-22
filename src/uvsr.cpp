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
#include <donut/render/SkyPass.h>
#include <donut/app/ApplicationBase.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/app/Camera.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_renderer.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>

#include "pbr_material.h"
#include "pbr_deferred_lighting_pass.h"
#include "bend_screen_space_shadows.h"
#include "gpu_performance_monitor.h"
#include "gpu_crash_diagnostics.h"
#include "camera_collision.h"
#include "camera_controllers.h"
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
    ScreenSpaceVisibilitySettings       ScreenSpaceVisibility;
    SkyParameters                       SkyParams;
    bool                                ShaderReloadRequested = false;
    bool                                EnableProceduralSky = true;
    WhiteWorldMode                      WhiteWorld = WhiteWorldMode::Off;
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
        return ScreenSpaceVisibility.reconstruction.temporalEnabled ||
               ScreenSpaceVisibility.sampling.adaptiveSparseSamplingEnabled;
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
    std::unique_ptr<SkyPass>            m_SkyPass;
    std::unique_ptr<AgxToneMappingPass> m_AgxToneMappingPass;
    std::unique_ptr<BendScreenSpaceShadowPass>
                                        m_BendScreenSpaceShadowPass;
    std::unique_ptr<SparseVirtualShadowMapPass>
                                        m_SparseVirtualShadowMapPass;
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
    float3                              m_AmbientTop = 0.f;
    float3                              m_AmbientBottom = 0.f;
    uint2                               m_PickPosition = 0u;
    bool                                m_Pick = false;
    bool                                m_BenchmarkCameraRequested = false;
    bool                                m_BenchmarkCameraActive = false;
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
    SparseVirtualShadowMapSettings      m_SvsmMotionAutostartTargetSettings;
    SvsmMotionAutostartStage            m_SvsmMotionAutostartStage =
        SvsmMotionAutostartStage::Baseline;
    uint32_t                            m_SvsmMotionAutostartStageFrames = 0u;
    uint32_t                            m_SvsmMotionAutostartStableFrames = 0u;
    bool                                m_SvsmMotionAutostartStageWritten =
        false;
    bool                                m_SvsmMotionBenchmarkPreviousBenchmarkCameraActive =
        false;
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
    bool                                m_SvsmMotionBenchmarkStartTaaEnabled =
        false;
    bool                                m_SvsmMotionBenchmarkStartUsesTaa = false;
    const Scene*                        m_SvsmMotionBenchmarkStartScene =
        nullptr;
    const DirectionalLight*             m_SvsmMotionBenchmarkStartLight =
        nullptr;
    double3                             m_SvsmMotionBenchmarkStartLightDirection =
        0.0;
    SparseVirtualShadowMapPass*         m_SvsmMotionBenchmarkTimingPass =
        nullptr;
    std::array<bool, SvsmMotionBenchmarkMeasurementFrames>
                                        m_SvsmMotionBenchmarkSeenGpuFrames{};
    bool                                m_SvsmMotionBenchmarkDuplicateGpuTag =
        false;
    bool                                m_SvsmMotionBenchmarkInvalidGpuTag =
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
    bool                                m_SvsmMotionBenchmarkScatterRasterActive =
        false;
    bool                                m_SvsmMotionBenchmarkPacketCullingUnavailable =
        false;
    bool                                m_SvsmMotionBenchmarkRequestedPathInactive =
        false;
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
    bool                                m_SvsmSceneStateRevisionReliable = true;
    SponzaCameraLocation                m_SponzaCameraLocation =
        SponzaCameraLocation::SimplifiedApproximation;

    UIData&                             m_ui;

public:

    UvsrSceneViewer(
        DeviceManager* deviceManager,
        UIData& ui,
        const std::string& sceneName,
        bool benchmarkCameraRequested,
        bool svsmMotionBenchmarkRequested,
        bool dredDiagnosticsActive,
        bool svsmMotionDiagnosticConfiguration,
        const std::filesystem::path& svsmMotionMeasurementReadyPath)
        : Super(deviceManager)
        , m_BindingCache(deviceManager->GetDevice())
        , m_BenchmarkCameraRequested(benchmarkCameraRequested)
        , m_SvsmMotionBenchmarkAutostartPending(
            svsmMotionBenchmarkRequested)
        , m_SvsmMotionBenchmarkWriteResultFile(
            svsmMotionBenchmarkRequested)
        , m_DredDiagnosticsActive(dredDiagnosticsActive)
        , m_SvsmMotionDiagnosticConfiguration(
            svsmMotionDiagnosticConfiguration)
        , m_SvsmMotionMeasurementReadyPath(
            svsmMotionMeasurementReadyPath)
        , m_SvsmMotionAutostartTargetSettings(
            ui.SparseVirtualShadowMaps)
        , m_ui(ui)
    {
        if (svsmMotionBenchmarkRequested)
        {
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

    [[nodiscard]] bool CanStartSvsmMotionBenchmark() const
    {
        return IsSceneLoaded() &&
            m_SponzaCameraLocationsAvailable &&
            m_ui.EnablePbr &&
            m_ui.UsesDeferredShading() &&
            m_ui.SparseVirtualShadowMaps.enabled &&
            m_SparseVirtualShadowMapPass &&
            m_SunLight &&
            !m_SvsmMotionBenchmarkActive;
    }

    [[nodiscard]] bool CanStageSvsmMotionBenchmark() const
    {
        return IsSceneLoaded() &&
            m_SponzaCameraLocationsAvailable &&
            m_ui.EnablePbr &&
            m_ui.UsesDeferredShading() &&
            m_SparseVirtualShadowMapPass &&
            m_SunLight &&
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
            status << "Draining total-only GPU timings: " << outstanding
                << " outstanding, frame "
                << m_SvsmMotionBenchmarkDrainFrames << " / "
                << SvsmMotionBenchmarkDrainFrameLimit;
            return status.str();
        }

        const SvsmMotionBenchmarkSegment segment =
            GetSvsmMotionBenchmarkSegment(
                m_SvsmMotionBenchmarkFrame);
        const char* segmentLabel = "Complete";
        switch (segment)
        {
        case SvsmMotionBenchmarkSegment::Warm:
            segmentLabel = "Warm";
            break;
        case SvsmMotionBenchmarkSegment::TurnRight:
            segmentLabel = "Turn Right";
            break;
        case SvsmMotionBenchmarkSegment::HoldRight:
            segmentLabel = "Hold Right";
            break;
        case SvsmMotionBenchmarkSegment::TurnBack:
            segmentLabel = "Turn Back";
            break;
        case SvsmMotionBenchmarkSegment::Complete:
            break;
        }

        std::ostringstream status;
        status << segmentLabel << ": frame "
            << m_SvsmMotionBenchmarkFrame << " / "
            << SvsmMotionBenchmarkEndFrame << ", "
            << std::fixed << std::setprecision(1)
            << GetSvsmMotionBenchmarkAngleDegrees(
                m_SvsmMotionBenchmarkFrame)
            << " degrees, total-only GPU timing";
        return status.str();
    }

    bool StartSvsmMotionBenchmark()
    {
        if (!CanStartSvsmMotionBenchmark())
            return false;

        // A manual start during a pending automated run owns the single
        // benchmark lifecycle and must prevent a second autostart afterward.
        m_SvsmMotionBenchmarkAutostartPending = false;

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
        m_SvsmMotionBenchmarkStartSettings =
            m_ui.SparseVirtualShadowMaps;
        m_SvsmMotionBenchmarkStartTaaEnabled =
            m_ui.EnableMiniEngineTaa;
        m_SvsmMotionBenchmarkStartUsesTaa = m_ui.UsesMiniEngineTaa();
        m_SvsmMotionBenchmarkStartScene = m_Scene.get();
        m_SvsmMotionBenchmarkStartLight = m_SunLight.get();
        m_SvsmMotionBenchmarkStartLightDirection =
            m_SunLight->GetDirection();
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
        m_SvsmMotionBenchmarkSeenGpuFrames.fill(false);
        m_SvsmMotionBenchmarkDuplicateGpuTag = false;
        m_SvsmMotionBenchmarkInvalidGpuTag = false;
        m_SvsmMotionBenchmarkDetailedTimingObserved = false;
        m_SvsmMotionBenchmarkBatchedSupported = false;
        m_SvsmMotionBenchmarkBatchedActive = false;
        m_SvsmMotionBenchmarkPacketSortingActive = false;
        m_SvsmMotionBenchmarkLevelSkipActive = false;
        m_SvsmMotionBenchmarkPacketCullingActive = false;
        m_SvsmMotionBenchmarkScatterRasterActive = false;
        m_SvsmMotionBenchmarkPacketCullingUnavailable = false;
        m_SvsmMotionBenchmarkRequestedPathInactive = false;
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
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkCpuTimings.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkGpuSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkMarkSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkAllocationSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkClearingSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkPacketGpuSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkRenderSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkFilterSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkSceneValidationCpuSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkClipmapUpdateCpuSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkPacketCpuSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        m_SvsmMotionBenchmarkCpuSamples.reserve(
            SvsmMotionBenchmarkMeasurementFrames);
        // UI- and command-line-started runs share one durable latest-result
        // artifact so a spike observed interactively has the same evidence as
        // an automated run.
        m_SvsmMotionBenchmarkWriteResultFile = true;
        m_SvsmMotionBenchmarkActive = true;
        m_SvsmMotionBenchmarkStatus =
            "Preparing Benchmark Position 1 at 1920 x 1080 with total-only GPU timing...";
        WriteSvsmMotionBenchmarkResultFile(
            "state=running\nstatus=" +
            m_SvsmMotionBenchmarkStatus + "\n");
        log::info(
            "SVSM motion benchmark started with total-only GPU timing: 180 warm frames, a 45-degree right turn at 0.1 degrees per rendered frame without pacing, a 16-frame hold, and the same return");
        return true;
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
        std::error_code directoryError;
        std::filesystem::create_directories("outputs", directoryError);
        if (directoryError)
        {
            log::warning(
                "SVSM motion benchmark could not create output directory (%s)",
                directoryError.message().c_str());
            return;
        }
        std::ofstream result(
            ResultPath,
            std::ios::binary | std::ios::trunc);
        if (!result)
        {
            log::warning(
                "SVSM motion benchmark could not open result file '%s'",
                ResultPath);
            return;
        }

        result << contents;
        result.flush();
        if (!result.good())
        {
            log::warning(
                "SVSM motion benchmark could not finish result file '%s'",
                ResultPath);
        }
    }

    void WriteSvsmMotionAutostartStage(const char* stage)
    {
        std::ostringstream result;
        result << "state=staging\n"
            << "stage=" << stage << "\n"
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
            << "physicalPages="
            << m_SvsmMotionAutostartTargetSettings.physicalPageCount
            << "\n"
            << "pageRenderBudget="
            << m_SvsmMotionAutostartTargetSettings.pageRenderBudget
            << "\n"
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
        WriteSvsmMotionBenchmarkResultFile(
            "state=aborted\nstatus=" +
            m_SvsmMotionBenchmarkStatus + "\n");
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
            if (!IsSvsmMotionBenchmarkMeasurementFrame(timing.sourceTag))
            {
                m_SvsmMotionBenchmarkInvalidGpuTag = true;
                continue;
            }
            const size_t index = size_t(
                timing.sourceTag - SvsmMotionBenchmarkWarmFrames);
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
        auto summarizeSegment = [this](
                                    SvsmMotionBenchmarkSegment segment) {
            std::vector<float> values;
            values.reserve(m_SvsmMotionBenchmarkGpuTimings.size());
            for (const SparseVirtualShadowMapGpuTiming& timing :
                m_SvsmMotionBenchmarkGpuTimings)
            {
                if (GetSvsmMotionBenchmarkSegment(timing.sourceTag) == segment)
                    values.push_back(timing.totalMilliseconds);
            }
            return SummarizeSvsmMotionBenchmarkSamples(std::move(values));
        };
        auto segmentName = [](SvsmMotionBenchmarkSegment segment) {
            switch (segment)
            {
            case SvsmMotionBenchmarkSegment::Warm: return "warm";
            case SvsmMotionBenchmarkSegment::TurnRight: return "turnRight";
            case SvsmMotionBenchmarkSegment::HoldRight: return "holdRight";
            case SvsmMotionBenchmarkSegment::TurnBack: return "turnBack";
            case SvsmMotionBenchmarkSegment::Complete: return "complete";
            }
            return "unknown";
        };

        const SparseVirtualShadowMapTimingAccounting accounting =
            m_SvsmMotionBenchmarkTimingPass
            ? m_SvsmMotionBenchmarkTimingPass->GetTimingAccounting()
            : SparseVirtualShadowMapTimingAccounting{};
        const bool diagnosticConfiguration =
            m_SvsmMotionDiagnosticConfiguration ||
            !IsSvsmMotionBenchmarkAcceptanceConfiguration(
                m_SvsmMotionBenchmarkStartSettings.physicalPageCount,
                m_SvsmMotionBenchmarkStartSettings.
                    renderPacketCachingEnabled,
                m_SvsmMotionBenchmarkStartSettings.
                    gpuGatedDrawSubmission);
        const bool environmentValid =
            IsSvsmMotionBenchmarkEnvironmentValid(
                m_DredDiagnosticsActive,
                diagnosticConfiguration);
        const bool measurementGateValid =
            !m_SvsmMotionMeasurementReadyPath.empty() &&
            m_SvsmMotionMeasurementGateReady &&
            m_SvsmMotionMeasurementGateComplete &&
            !m_SvsmMotionMeasurementGateContaminated;
        const bool evidenceValid = environmentValid &&
            measurementGateValid &&
            !m_SvsmMotionBenchmarkDetailedTimingObserved &&
            IsSvsmMotionBenchmarkEvidenceValid(
                m_SvsmMotionBenchmarkGpuSamples.size(),
                m_SvsmMotionBenchmarkCpuSamples.size(),
                accounting.issued,
                accounting.dropped,
                accounting.retired,
                accounting.outstanding,
                m_SvsmMotionBenchmarkDuplicateGpuTag,
                m_SvsmMotionBenchmarkInvalidGpuTag);
        const SvsmMotionBenchmarkTimingSummary gpuSummary =
            SummarizeSvsmMotionBenchmarkSamples(
                m_SvsmMotionBenchmarkGpuSamples);
        const SvsmMotionBenchmarkTimingSummary turnRightSummary =
            summarizeSegment(
                SvsmMotionBenchmarkSegment::TurnRight);
        const SvsmMotionBenchmarkTimingSummary holdRightSummary =
            summarizeSegment(
                SvsmMotionBenchmarkSegment::HoldRight);
        const SvsmMotionBenchmarkTimingSummary turnBackSummary =
            summarizeSegment(
                SvsmMotionBenchmarkSegment::TurnBack);
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
            (!scatterRasterRequested ||
                m_SvsmMotionBenchmarkScatterRasterActive) &&
            !m_SvsmMotionBenchmarkPacketCullingUnavailable;
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
            << " motion-test samples, GPU median "
            << std::fixed << std::setprecision(3)
            << gpuMedian
            << " ms / p95 "
            << gpuSummary.p95
            << " ms / p99 "
            << gpuSummary.p99
            << " ms / worst "
            << gpuWorst
            << " ms; total-only GPU timing; CPU stage medians validate "
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
            << "dredDiagnostics=" <<
                (m_DredDiagnosticsActive ? 1 : 0) << "\n"
            << "diagnosticConfiguration=" <<
                (diagnosticConfiguration ? 1 : 0) << "\n"
            << "requestedPathActive=" <<
                (requestedPathActive ? 1 : 0) << "\n"
            << "gpuTimingMode=totalOnly\n"
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
            << "width=1920\n"
            << "height=1080\n"
            << "cameraStepDegrees=0.1\n"
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
            << "markingMode=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.markingMode) << "\n"
            << "perPixelMarkingDedupe=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .perPixelMarkingDedupeEnabled ? 1u : 0u) << "\n"
            << "filterMode=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.filterMode) << "\n"
            << "tapCount=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.tapCount) << "\n"
            << "resolutionBias=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.resolutionBias) << "\n"
            << "debugView=" << uint32_t(
                m_SvsmMotionBenchmarkStartSettings.debugView) << "\n"
            << "caching=" << (
                m_SvsmMotionBenchmarkStartSettings.cachingEnabled
                    ? 1u : 0u) << "\n"
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
            << "renderPacketCaching=" << (
                m_SvsmMotionBenchmarkStartSettings
                    .renderPacketCachingEnabled ? 1u : 0u) << "\n"
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
            << "fineCasterExclusion=" << (
                m_SvsmMotionBenchmarkStartSettings.fineCasterExclusion
                    ? 1u : 0u) << "\n"
            << "taaEnabled=" << (
                m_SvsmMotionBenchmarkStartTaaEnabled ? 1u : 0u) << "\n"
            << "taaActive=" << (
                m_SvsmMotionBenchmarkStartUsesTaa ? 1u : 0u) << "\n"
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
        writeTimingSummary("turnRight", turnRightSummary);
        writeTimingSummary("holdRight", holdRightSummary);
        writeTimingSummary("turnBack", turnBackSummary);
        result << "slowFrameCount=" << slowestTimings.size() << "\n";
        for (size_t index = 0u; index < slowestTimings.size(); ++index)
        {
            const SparseVirtualShadowMapGpuTiming& timing =
                slowestTimings[index];
            const SvsmMotionBenchmarkSegment segment =
                GetSvsmMotionBenchmarkSegment(timing.sourceTag);
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
                << segmentName(segment) << "\n"
                << "slowFrame" << index << "AngleDegrees="
                << GetSvsmMotionBenchmarkAngleDegrees(timing.sourceTag) << "\n"
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
            SvsmMotionBenchmarkEndFrame,
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
                    StartSvsmMotionBenchmark();
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
                m_SvsmMotionBenchmarkStartUsesTaa)
        {
            AbortSvsmMotionBenchmark("the TAA state changed");
            return;
        }
        if (!m_SunLight ||
            m_SunLight.get() != m_SvsmMotionBenchmarkStartLight)
        {
            AbortSvsmMotionBenchmark("the producing directional light changed");
            return;
        }
        const double3 lightDirection = m_SunLight->GetDirection();
        if (lightDirection.x !=
                m_SvsmMotionBenchmarkStartLightDirection.x ||
            lightDirection.y !=
                m_SvsmMotionBenchmarkStartLightDirection.y ||
            lightDirection.z !=
                m_SvsmMotionBenchmarkStartLightDirection.z)
        {
            AbortSvsmMotionBenchmark("the directional light rotated");
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
                    m_SvsmMotionBenchmarkPreparedFrame))
            {
                const SparseVirtualShadowMapTimings& timings =
                    m_SparseVirtualShadowMapPass->GetTimings();
                if (timings.active)
                {
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
                    const bool frameRequestedPathActive =
                        (!batchedDrawRequested ||
                            timings.batchedDrawActive) &&
                        (!packetSortingRequested ||
                            timings.packetStateSortingActive) &&
                        (!levelSkipRequested ||
                            timings.levelEmptyWorkSkipActive) &&
                        (!packetCullingRequested ||
                            timings.packetPageCullingActive) &&
                        (!scatterRasterRequested ||
                            timings.dirtyPageScatterRasterActive) &&
                        !timings.packetPageCullingUnavailable;
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
                    m_SvsmMotionBenchmarkScatterRasterActive |=
                        timings.dirtyPageScatterRasterActive;
                    m_SvsmMotionBenchmarkPacketCullingUnavailable |=
                        timings.packetPageCullingUnavailable;
                    m_SvsmMotionBenchmarkCpuTimings.push_back({
                        m_SvsmMotionBenchmarkPreparedFrame,
                        timings.sceneValidationCpuMilliseconds,
                        timings.clipmapUpdateCpuMilliseconds,
                        timings.cullingCpuMilliseconds,
                        timings.totalCpuMilliseconds
                    });
                    m_SvsmMotionBenchmarkSceneValidationCpuSamples.push_back(
                        timings.sceneValidationCpuMilliseconds);
                    m_SvsmMotionBenchmarkClipmapUpdateCpuSamples.push_back(
                        timings.clipmapUpdateCpuMilliseconds);
                    m_SvsmMotionBenchmarkPacketCpuSamples.push_back(
                        timings.cullingCpuMilliseconds);
                    m_SvsmMotionBenchmarkCpuSamples.push_back(
                        timings.totalCpuMilliseconds);
                }
            }
            m_SvsmMotionBenchmarkFramePrepared = false;
        }

        if (m_SvsmMotionBenchmarkFrame >=
            SvsmMotionBenchmarkEndFrame)
        {
            m_SvsmMotionBenchmarkDraining = true;
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
            SvsmMotionBenchmarkWarmFrames)
        {
            m_SvsmMotionBenchmarkTimingPass =
                m_SparseVirtualShadowMapPass.get();
            m_SvsmMotionBenchmarkTimingPass->ResetTimingAccounting();
        }

        const uint64_t sourceFrame = m_SvsmMotionBenchmarkFrame;
        const float angleDegrees =
            GetSvsmMotionBenchmarkAngleDegrees(
                sourceFrame);
        const affine3 turn = rotation(
            preset.Up,
            -radians(angleDegrees));
        m_StaticCamera.SetExactPose(
            preset.Position,
            normalize(turn.transformVector(preset.Direction)),
            normalize(turn.transformVector(preset.Up)),
            normalize(turn.transformVector(preset.Right)));
        m_SvsmMotionBenchmarkPreparedFrame = sourceFrame;
        if (IsSvsmMotionBenchmarkMeasurementFrame(sourceFrame))
            m_SvsmMotionBenchmarkCurrentTimingTag = sourceFrame;
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
        m_ui.ScreenSpaceVisibility = ScreenSpaceVisibilitySettings{};
        m_ui.SkyParams = SkyParameters{};
        m_ui.EnableProceduralSky = true;

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
            m_ScreenSpaceVisibilityPass = std::make_unique<ScreenSpaceVisibilityPass>(
                GetDevice(),
                m_ShaderFactory,
                app::GetDirectoryWithExecutable().parent_path() /
                    "media/noise/visibility_filter_adapted_gauss1_ema035_r8.bin");
        }
        else
        {
            m_PbrDeferredLightingPass.reset();
            m_BendScreenSpaceShadowPass.reset();
            m_SparseVirtualShadowMapPass.reset();
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

        m_SkyPass = std::make_unique<SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ForwardFramebuffer, *m_View);
        
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
        if (sceneRoot)
        {
            using DirtyFlags = SceneGraphNode::DirtyFlags;
            const DirtyFlags shadowRelevantDirtyFlags =
                DirtyFlags::LocalTransform |
                DirtyFlags::Leaf |
                DirtyFlags::SubgraphStructure |
                DirtyFlags::SubgraphTransforms |
                DirtyFlags::SubgraphContentUpdate;
            if ((sceneRoot->GetDirtyFlags() &
                    shadowRelevantDirtyFlags) != 0u ||
                shadowRelevantMaterialDirty)
            {
                ++m_SvsmSceneStateRevision;
            }
        }
        // UVSR samples transform/content dirty flags and material dirtiness
        // before RefreshSceneGraph clears them. Animation channels use those
        // same setters when applied, so merely having dormant animation data
        // does not require a full scene hash walk every frame.
        m_SvsmSceneStateRevisionReliable = sceneRoot != nullptr;
        m_Scene->RefreshSceneGraph(GetFrameIndex());
        const auto& sceneLights =
            m_Scene->GetSceneGraph()->GetLights();

        {
            uint width = windowWidth;
            uint height = windowHeight;

            constexpr uint sampleCount = 1;
            const bool visibilityResourcesRequired = m_ui.EnablePbr &&
                m_ui.UsesDeferredShading() &&
                m_ui.ScreenSpaceVisibility.HasActiveConsumer();
            const bool visibilitySourceRadianceRequired =
                visibilityResourcesRequired &&
                m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
                (!sceneLights.empty() ||
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
                m_ShaderFactory->ClearCache();
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
        
        m_AmbientTop = m_ui.SkyParams.skyColor * m_ui.SkyParams.brightness;
        m_AmbientBottom = m_ui.SkyParams.groundColor * m_ui.SkyParams.brightness;

        if (m_ui.WhiteWorld != WhiteWorldMode::Off)
        {
            // Keep white-world illumination neutral instead of baking the blue sky
            // tint into otherwise white reference surfaces.
            const float topLuma = dot(m_AmbientTop, float3(0.2126f, 0.7152f, 0.0722f));
            const float bottomLuma = dot(m_AmbientBottom, float3(0.2126f, 0.7152f, 0.0722f));
            // White World is the paper/reference inspection mode. Keep direct
            // light untouched, but give the neutral indirect term enough range
            // for ambient visibility to be legible instead of limiting AO to a
            // barely visible 3-6% sky contribution.
            constexpr float WhiteWorldIndirectReferenceScale = 4.0f;
            m_AmbientTop = float3(topLuma * WhiteWorldIndirectReferenceScale);
            m_AmbientBottom = float3(bottomLuma * WhiteWorldIndirectReferenceScale);
        }

        m_RenderTargets->Clear(m_CommandList);
        BendScreenSpaceShadowResult bendShadowResult;
        SparseVirtualShadowMapResult sparseVirtualShadowMapResult;

        if (m_SparseVirtualShadowMapPass &&
            (!m_ui.UsesDeferredShading() || !m_ui.EnablePbr))
        {
            m_SparseVirtualShadowMapPass->Deactivate();
        }

        ForwardShadingPass::Context forwardContext;

        if (!m_ui.UsesDeferredShading())
            m_ForwardPass->PrepareLights(forwardContext, m_CommandList, m_Scene->GetSceneGraph()->GetLights(), m_AmbientTop, m_AmbientBottom, {});

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
                        m_SvsmSceneStateRevision,
                        m_SvsmSceneStateRevisionReliable,
                        *m_OpaqueDrawStrategy,
                        m_SvsmMotionBenchmarkCurrentTimingTag,
                        m_SvsmMotionBenchmarkActive);
            }
            DeferredLightingPass::Inputs deferredInputs;
            deferredInputs.SetGBuffer(*m_RenderTargets);
            if (m_ui.EnablePbr)
            {
                // Slot 14 is unused by UVSR's current renderer and carries the
                // separate authored material ambient-occlusion attachment.
                deferredInputs.indirectDiffuse = m_RenderTargets->MaterialAmbientOcclusion;
            }
            deferredInputs.ambientColorTop = m_AmbientTop;
            deferredInputs.ambientColorBottom = m_AmbientBottom;
            deferredInputs.lights = &sceneLights;

            // Bend and SVSM remain independent visibility producers. This
            // frame-local adapter is their only shared integration point, and
            // deferred lighting applies a factor only to its pointer-identical
            // light. Either producer can therefore be removed or ported
            // without changing the other.
            const DirectionalLightVisibilitySet directionalVisibility = {{
                {
                    bendShadowResult.nearVisibility,
                    bendShadowResult.light
                },
                {
                    sparseVirtualShadowMapResult.visibility,
                    sparseVirtualShadowMapResult.light
                }
            }};

            const bool runScreenSpaceVisibility = m_ui.EnablePbr &&
                m_ui.ScreenSpaceVisibility.HasActiveConsumer();
            uint32_t knownInactiveLightingSources = 0u;
            if (sceneLights.empty())
                knownInactiveLightingSources |= LightingSource_Direct;
            if (!m_ui.ScreenSpaceVisibility.indirectDiffuse.includeEmissive ||
                !(m_ui.ScreenSpaceVisibility.indirectDiffuse.emissiveGain > 0.f))
            {
                knownInactiveLightingSources |= LightingSource_Emissive;
            }
            const bool allFirstBounceSourcesInactive =
                (knownInactiveLightingSources &
                    (LightingSource_Direct | LightingSource_Emissive)) ==
                (LightingSource_Direct | LightingSource_Emissive);
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
                    m_RenderTargets->DirectDiffuseRadiance,
                    runScreenSpaceVisibility,
                    writeSourceRadiance,
                    writeBounceMetadata,
                    m_ui.ScreenSpaceVisibility.indirectDiffuse.includeEmissive,
                    m_ui.ScreenSpaceVisibility.indirectDiffuse.emissiveGain,
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
                    visibilityInputs.gbufferSpecular = m_RenderTargets->GBufferSpecular;
                    visibilityInputs.gbufferEmissive = m_RenderTargets->GBufferEmissive;
                    visibilityInputs.materialAmbientOcclusion =
                        m_RenderTargets->MaterialAmbientOcclusion;
                    visibilityInputs.baseLighting = m_RenderTargets->BaseLighting;
                    visibilityInputs.output = m_RenderTargets->HdrColor;
                    visibilityInputs.knownInactiveLightingSources =
                        knownInactiveLightingSources;
                    m_ScreenSpaceVisibilityPass->Render(
                        m_CommandList,
                        m_ui.ScreenSpaceVisibility,
                        *m_View,
                        visibilityInputs,
                        m_AmbientTop,
                        m_AmbientBottom,
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

        if (m_ui.EnableProceduralSky)
            m_SkyPass->Render(m_CommandList, *m_View, *m_SunLight, m_ui.SkyParams);

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
            !sparseVirtualShadowMapResult.showDebug)
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
        if (sparseVirtualShadowMapResult.showDebug &&
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

    float GetSceneDiagonal() const
    {
        return m_SceneDiagonal;
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
        GpuPerformanceMetrics gpuMetrics;
        BendScreenSpaceShadowTimings bendShadowTimings;
        SparseVirtualShadowMapTimings sparseShadowTimings;
        ScreenSpaceVisibilityTimings visibilityTimings;
        MiniEngineTemporalAATimings temporalAATimings;
        bool hasBendShadowTimings = false;
        bool hasSparseShadowTimings = false;
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
    std::array<std::string, 10> m_SparseShadowStatLines;
    std::array<std::string, 3> m_VisibilityStatLines;
    std::array<std::string, 2> m_TemporalAAStatLines;
    std::deque<StatSnapshot> m_StatUpdateQueue;
    bool m_HasAppliedStatSnapshot = false;
    bool m_HasGpuStatSnapshot = false;
    bool m_HasBendShadowStatSnapshot = false;
    bool m_HasSparseShadowStatSnapshot = false;
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
        char buffer[256];
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
                    "Coarser %u / %.1f + %.1f + %.1f mib",
                    timings.fallbackPixels,
                    double(timings.physicalDepthBytes) / BytesPerMib,
                    double(timings.visibilityBytes) / BytesPerMib,
                    double(timings.packetPageMetadataBytes +
                        timings.packetPageListBytes) / BytesPerMib);
                FormatStatLine(
                    m_SparseShadowStatLines[5],
                    "Missing %u px / Alloc fail %u / Out %u px",
                    timings.resolveMissingPixels,
                    timings.allocationFailures,
                    timings.outOfRangePixels);
                FormatStatLine(
                    m_SparseShadowStatLines[6],
                    "Packet pages %u candidates / %u compacted / %u fail open / age %u f",
                    timings.packetPageCandidatePackets,
                    timings.packetPageCompactedPackets,
                    timings.packetPageFailOpenPackets,
                    timings.debugCounterAgeFrames);
            }
            else
            {
                m_SparseShadowStatLines[2] =
                    "Page counters unavailable";
                m_SparseShadowStatLines[3] =
                    "Dirty and rendered-page counters unavailable";
                FormatStatLine(
                    m_SparseShadowStatLines[4],
                    "Counters unavailable / %.1f + %.1f + %.1f mib",
                    double(timings.physicalDepthBytes) / BytesPerMib,
                    double(timings.visibilityBytes) / BytesPerMib,
                    double(timings.packetPageMetadataBytes +
                        timings.packetPageListBytes) / BytesPerMib);
                m_SparseShadowStatLines[5] =
                    "Pixel and allocation counters unavailable";
                m_SparseShadowStatLines[6] =
                    "Packet-page counters unavailable";
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
            default: staticRejectReason = "unknown"; break;
            }
            FormatStatLine(
                m_SparseShadowStatLines[7],
                "Static %s / drain %s:%u / vis %s / packet %s / scatter %s / reject %s",
                timings.staticPageRequestReuseActive ? "active" : "inactive",
                timings.staticPageDrainActive ? "active" : "inactive",
                timings.staticPageDrainFramesRemaining,
                timings.staticVisibilityReuseActive ? "active" : "inactive",
                timings.packetPageCullingActive ? "active" : "inactive",
                timings.dirtyPageScatterRasterActive
                    ? "active"
                    : "inactive",
                staticRejectReason);
            FormatStatLine(
                m_SparseShadowStatLines[8],
                "Batch %s%s / sort %s / level gate %s / packet fallback %s",
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
                m_SparseShadowStatLines[9],
                "CPU All %.3f / Validate %.3f / Views %.3f ms",
                timings.totalCpuMilliseconds,
                timings.sceneValidationCpuMilliseconds,
                timings.clipmapUpdateCpuMilliseconds);
            m_HasSparseShadowStatSnapshot = timings.active;
        }
        else
        {
            m_HasSparseShadowStatSnapshot = false;
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

    static bool DrawCenteredActionButton(
        const char* label,
        float width)
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
            std::floor(
                buttonMin.x +
                (buttonMax.x - buttonMin.x - textSize.x) * 0.5f),
            std::floor(
                buttonMin.y +
                (buttonMax.y - buttonMin.y - textSize.y) * 0.5f +
                1.f));
        drawList->AddText(
            textPosition,
            ImGui::GetColorU32(ImGuiCol_Text),
            label);
        return pressed;
    }

public:
    UIRenderer(
        DeviceManager* deviceManager,
        std::shared_ptr<UvsrSceneViewer> app,
        UIData& ui,
        std::chrono::steady_clock::time_point programLaunchTime)
        : ImGui_Renderer(deviceManager)
        , m_app(app)
        , m_ProgramLaunchTime(programLaunchTime)
        , m_ui(ui)
    {
        m_Font = CreateFontFromFile(
            *(app->GetRootFs()),
            "/media/fonts/System/CodexUI-Semibold.ttf",
            16.f);

        ImGui::GetIO().IniFilename = nullptr;
    }

    bool Init(std::shared_ptr<ShaderFactory> shaderFactory)
    {
        if (!ImGui_Renderer::Init(shaderFactory))
            return false;

        m_BackdropBlurPass = std::make_unique<BackdropBlurPass>(
            GetDevice(),
            shaderFactory,
            m_app->GetCommonPasses());
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
                for (std::string& value : m_VisibilityStatLines)
                    value.clear();
                for (std::string& value : m_TemporalAAStatLines)
                    value.clear();
                m_HasAppliedStatSnapshot = false;
                m_HasGpuStatSnapshot = false;
                m_HasBendShadowStatSnapshot = false;
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
            if (!motionTestAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                motionTestRunning
                    ? "Benchmark Position 1 is turning by exactly 0.1 degrees per rendered frame without wall-clock pacing."
                    : (m_app->HasSponzaCameraLocations()
                        ? "Run 180 warm frames, turn Benchmark Position 1 right by exactly 0.1 degrees per rendered frame to 45 degrees, hold for 16 frames, and return at the same rate. The sequence has no 40 Hz lock or other time-based throttle."
                        : "The motion test requires a standardized PBR Sponza scene."));
            ImGui::TextWrapped(
                "%s",
                m_app->GetSvsmMotionBenchmarkStatus().c_str());
            if (motionTestRunning)
                ImGui::BeginDisabled();

            static const char* presetLabels[] = {
                "Performance",
                "Balanced",
                "Quality",
                "Custom"
            };
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Profile##SparseVirtualShadowMaps",
                    presetLabels[int(shadows.preset)]))
            {
                for (int index = 0;
                    index < int(std::size(presetLabels));
                    ++index)
                {
                    const SvsmPreset preset = SvsmPreset(index);
                    const bool selected = shadows.preset == preset;
                    if (ImGui::Selectable(
                            presetLabels[index], selected))
                    {
                        ApplySvsmPreset(shadows, preset);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the SVSM speed-to-quality balance.");

            bool customChanged = false;
            static const char* modeLabels[] = {
                "Dense Reference",
                "Sparse Uncached",
                "Sparse Cached"
            };
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Mode##SparseVirtualShadowMaps",
                    modeLabels[int(shadows.mode)]))
            {
                for (int index = 0;
                    index < int(std::size(modeLabels));
                    ++index)
                {
                    const SvsmMode mode = SvsmMode(index);
                    const bool selected = shadows.mode == mode;
                    if (ImGui::Selectable(modeLabels[index], selected))
                    {
                        shadows.mode = mode;
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                shadows.mode == SvsmMode::DenseReference
                    ? "Fully backs six 8192-square R32_UINT clipmaps; this explicit validation mode can require about 1.5 GiB for atomic depth."
                    : "Use the software-managed fixed physical page pool, with optional page reuse in cached mode.");

            const bool sparseMode =
                shadows.mode != SvsmMode::DenseReference;
            const bool cachedSparseMode =
                shadows.mode == SvsmMode::SparseCached;
            const bool cacheReuseAvailable =
                cachedSparseMode && shadows.cachingEnabled;

            customChanged |= DrawSliderFloat(
                "First Clipmap Extent",
                &shadows.firstClipmapExtent,
                1.f,
                500.f,
                "%.1f");
            ImGui::SetItemTooltip(
                "Set the world-space width covered by the finest clipmap.");
            customChanged |= DrawSliderFloat(
                "Maximum Light Depth",
                &shadows.maximumLightDepth,
                1.f,
                2000.f,
                "%.1f");
            ImGui::SetItemTooltip(
                "Set the caster depth range centered on each clipmap.");

            if (shadows.mode != SvsmMode::DenseReference)
            {
                int physicalPageCount =
                    int(shadows.physicalPageCount);
                if (DrawSliderInt(
                        "Physical Pool Pages",
                        &physicalPageCount,
                        64,
                        int(SvsmPagesPerClipmap)))
                {
                    shadows.physicalPageCount =
                        uint32_t(physicalPageCount);
                    customChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Set the logical size of the shared fixed physical page pool.");

                bool unlimitedBudget =
                    shadows.pageRenderBudget ==
                    std::numeric_limits<uint32_t>::max();
                if (ImGui::Checkbox(
                        "Unlimited Page Render Budget",
                        &unlimitedBudget))
                {
                    shadows.pageRenderBudget = unlimitedBudget
                        ? std::numeric_limits<uint32_t>::max()
                        : 256u;
                    customChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Disable the finite dirty-page scheduling limit. The reference path is unlimited and leaves the coarsest clipmap outside finite budgets.");
                if (!unlimitedBudget)
                {
                    int pageBudget =
                        int(std::min(
                            shadows.pageRenderBudget,
                            shadows.physicalPageCount));
                    if (DrawSliderInt(
                            "Page Render Budget",
                            &pageBudget,
                            0,
                            int(shadows.physicalPageCount)))
                    {
                        shadows.pageRenderBudget =
                            uint32_t(pageBudget);
                        customChanged = true;
                    }
                    ImGui::SetItemTooltip(
                        "Limit dirty-page scheduling across fine clipmaps 0 through 4. Include Coarsest in Page Budget independently extends this shared limit to all six clipmaps.");
                }

                if (unlimitedBudget)
                    ImGui::BeginDisabled();
                customChanged |= ImGui::Checkbox(
                    "Include Coarsest in Page Budget",
                    &shadows.coarsestPageRenderBudgetEnabled);
                if (unlimitedBudget)
                    ImGui::EndDisabled();
                ImGui::SetItemTooltip(
                    unlimitedBudget
                        ? "The coarsest-page budget switch applies only to a finite page-render budget; the saved value is retained."
                        : "Share this frame's finite page-render reservation with the coarsest clipmap. This hard-bounds scheduled clear, culling, and raster work after allocation; disable to retain the published coarse-fallback exemption.");

                if (unlimitedBudget)
                    ImGui::BeginDisabled();
                customChanged |= ImGui::Checkbox(
                    "Allocation Budget Saturation Early-Out",
                    &shadows.allocationBudgetSaturationEarlyOutEnabled);
                if (unlimitedBudget)
                    ImGui::EndDisabled();
                ImGui::SetItemTooltip(
                    unlimitedBudget
                        ? "The allocation saturation early-out applies only to a finite page-render budget; the saved value is retained."
                        : "After the shared page budget is saturated, use a relaxed counter probe to bypass redundant reservation atomics. The existing atomic and post-check remain authoritative. Disable to retain the reference allocation path.");
            }

            static const char* markingLabels[] = {
                "Per Pixel",
                "8 by 8 Tile",
                "16 by 16 Tile"
            };
            if (!sparseMode)
                ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Page Marking",
                    markingLabels[int(shadows.markingMode)]))
            {
                for (int index = 0;
                    index < int(std::size(markingLabels));
                    ++index)
                {
                    const SvsmMarkingMode mode =
                        SvsmMarkingMode(index);
                    const bool selected =
                        shadows.markingMode == mode;
                    if (ImGui::Selectable(
                            markingLabels[index], selected))
                    {
                        shadows.markingMode = mode;
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (!sparseMode)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                sparseMode
                    ? "Choose exact per-pixel requests or a conservative deduplicated tile volume."
                    : "Page marking applies only to sparse modes; the saved value is retained in Dense Reference.");

            const bool perPixelMarkingDedupeAvailable =
                sparseMode &&
                shadows.markingMode == SvsmMarkingMode::PerPixel;
            if (!perPixelMarkingDedupeAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Deduplicate Per-Pixel Requests",
                &shadows.perPixelMarkingDedupeEnabled);
            if (!perPixelMarkingDedupeAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                perPixelMarkingDedupeAvailable
                    ? "Deduplicate each 8-by-8 group's page requests through a bounded shared hash; collisions fail open to direct page-table atomics."
                    : "This optimization applies only to Per Pixel sparse marking; the saved value is retained in other modes.");

            static const char* tapLabels[] = {
                "1", "4", "8", "16"
            };
            static const SvsmTapCount tapValues[] = {
                SvsmTapCount::One,
                SvsmTapCount::Four,
                SvsmTapCount::Eight,
                SvsmTapCount::Sixteen
            };
            int selectedTap = 0;
            for (int index = 0; index < int(std::size(tapValues)); ++index)
            {
                if (shadows.tapCount == tapValues[index])
                    selectedTap = index;
            }
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Filter Taps",
                    tapLabels[selectedTap]))
            {
                for (int index = 0;
                    index < int(std::size(tapValues));
                    ++index)
                {
                    const bool selected =
                        shadows.tapCount == tapValues[index];
                    if (ImGui::Selectable(tapLabels[index], selected))
                    {
                        shadows.tapCount = tapValues[index];
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            static const char* filterLabels[] = {
                "Manual Page Safe",
                "Hybrid"
            };
            if (!sparseMode)
                ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Filtering",
                    filterLabels[int(shadows.filterMode)]))
            {
                for (int index = 0;
                    index < int(std::size(filterLabels));
                    ++index)
                {
                    const SvsmFilterMode mode =
                        SvsmFilterMode(index);
                    const bool selected = shadows.filterMode == mode;
                    if (ImGui::Selectable(filterLabels[index], selected))
                    {
                        shadows.filterMode = mode;
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (!sparseMode)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                sparseMode
                    ? "Manual mode translates every tap independently. Hybrid reuses one translation only for a complete same-page footprint."
                    : "Page-translation filtering modes apply only to sparse modes; the saved value is retained in Dense Reference.");

            static const char* biasLabels[] = {
                "0", "+1 Mip", "+2 Mips"
            };
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Resolution Bias",
                    biasLabels[int(shadows.resolutionBias)]))
            {
                for (int index = 0;
                    index < int(std::size(biasLabels));
                    ++index)
                {
                    const SvsmResolutionBias bias =
                        SvsmResolutionBias(index);
                    const bool selected =
                        shadows.resolutionBias == bias;
                    if (ImGui::Selectable(biasLabels[index], selected))
                    {
                        shadows.resolutionBias = bias;
                        customChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (!cachedSparseMode)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Caching", &shadows.cachingEnabled);
            if (!cachedSparseMode)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                cachedSparseMode
                    ? "Reuse valid physical pages across frames."
                    : "Caching is available only in Sparse Cached mode; the saved value is retained.");
            if (!cacheReuseAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Recent Page Eviction Grace",
                &shadows.recentPageEvictionGraceEnabled);
            if (!cacheReuseAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                cacheReuseAvailable
                    ? "Protect pages used within the last eight frames until free and older cached pages are exhausted. Disable to retain the original unordered cached-page allocator."
                    : "Recent-page protection requires active Sparse Cached page reuse; the saved value is retained.");
            const bool finiteBudgetStaticDrainAvailable =
                cacheReuseAvailable &&
                shadows.pageRenderBudget > 0u &&
                shadows.pageRenderBudget < shadows.physicalPageCount;
            if (!finiteBudgetStaticDrainAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Finite-Budget Static Drain",
                &shadows.finiteBudgetStaticDrainEnabled);
            if (!finiteBudgetStaticDrainAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                finiteBudgetStaticDrainAvailable
                    ? "Preserve an unchanged request union and run a conservative bounded number of allocation, clear, cull, and raster passes before entering the zero-work static cache path. Disable to retain continuous finite-budget maintenance."
                    : "Finite-budget static draining requires active Sparse Cached page reuse and a nonzero fine-page budget below the physical-pool size; the saved value is retained.");
            const bool staticPageRequestReuseAvailable =
                cacheReuseAvailable &&
                (shadows.pageRenderBudget >= shadows.physicalPageCount ||
                    (shadows.finiteBudgetStaticDrainEnabled &&
                        shadows.pageRenderBudget > 0u));
            if (!staticPageRequestReuseAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Static Page Request Reuse",
                &shadows.staticPageRequestReuseEnabled);
            if (!staticPageRequestReuseAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                staticPageRequestReuseAvailable
                    ? "Reuse the proven page-request set for an unchanged static frame. A finite budget drains conservatively when its independent toggle is enabled; missing pages retain normal coarser or white fallback."
                    : "Static request reuse requires active Sparse Cached page reuse plus either a complete-pool budget or an enabled nonzero finite-budget drain; the saved value is retained.");
            const bool staticVisibilityCacheAvailable =
                staticPageRequestReuseAvailable &&
                shadows.staticPageRequestReuseEnabled &&
                shadows.debugView == SvsmDebugView::None;
            if (!staticVisibilityCacheAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Static Visibility Cache",
                &shadows.staticVisibilityCachingEnabled);
            if (!staticVisibilityCacheAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                staticVisibilityCacheAvailable
                    ? "Cache one exact full-resolution R8 resolve for each of UVSR's eight repeating TAA jitter positions; disable to run the selected resolve shader every frame."
                    : "Static visibility reuse requires active static page-request reuse and debug view Off; the saved value is retained.");
            customChanged |= ImGui::Checkbox(
                "Scene State Caching",
                &shadows.sceneStateCachingEnabled);
            ImGui::SetItemTooltip(
                "Reuse scene validation while UVSR's pre-refresh transform, content, and material revision is unchanged. Applied animation changes advance the same revision; dormant animation data does not force a full hash walk.");
            if (!cacheReuseAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Render Packet Caching",
                &shadows.renderPacketCachingEnabled);
            if (!cacheReuseAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                cacheReuseAvailable
                    ? "Reuse conservative per-clipmap caster packets while the scene and page-aligned light views remain unchanged."
                    : "Render-packet reuse requires active Sparse Cached page reuse; GPU-gated sparse drawing may still use transient packets. The saved value is retained.");
            if (!sparseMode)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "GPU-Gated Draw Submission",
                &shadows.gpuGatedDrawSubmission);
            if (!sparseMode)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                sparseMode
                    ? "Use GPU page counts to suppress empty cached shadow draws; disable to retain the original per-item submission path."
                    : "GPU-gated draw submission applies only to sparse modes; the saved value is retained.");
            const bool batchedDrawAvailable =
                sparseMode && shadows.gpuGatedDrawSubmission;
            if (!batchedDrawAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Batched Draw Submission",
                &shadows.batchedDrawSubmissionEnabled);
            if (!batchedDrawAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                batchedDrawAvailable
                    ? "Group consecutive compatible cached caster packets into multi-draw commands on supported D3D12 hardware. The SVSM status lines report whether this path is supported and active."
                    : "Batched draw submission requires sparse mode and GPU-gated draw submission; the saved value is retained.");
            const bool packetStateSortingAvailable =
                shadows.mode != SvsmMode::DenseReference &&
                shadows.gpuGatedDrawSubmission &&
                shadows.batchedDrawSubmissionEnabled;
            if (!packetStateSortingAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Packet State Sorting",
                &shadows.packetStateSortingEnabled);
            if (!packetStateSortingAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                packetStateSortingAvailable
                    ? "Stable-sort cached packets per clipmap by compatible buffer, raster, and exact alpha-tested state so batched packets collapse into fewer multi-draw groups. Nonbatchable packets retain exact material identity and singleton draws. Disable to retain DrawStrategy order."
                    : "Packet state sorting requires sparse mode, GPU-gated draw submission, and Batched Draw Submission. Its saved value is retained while unavailable.");
            const bool levelEmptyWorkSkipAvailable =
                sparseMode &&
                shadows.gpuGatedDrawSubmission &&
                shadows.batchedDrawSubmissionEnabled;
            if (!levelEmptyWorkSkipAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Per-Level Empty-Work Skip",
                &shadows.levelEmptyWorkSkipEnabled);
            if (!levelEmptyWorkSkipAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                levelEmptyWorkSkipAvailable
                    ? "Use one persistent GPU count word per clipmap so a batched level with zero dirty pages parses zero indirect draw commands. Any nonzero level still permits the complete group; disable to retain the original multi-draw path."
                    : "Per-level empty-work skipping requires sparse mode, GPU-gated draw submission, and Batched Draw Submission. Its saved value is retained while unavailable.");
            const bool packetPageCullingAvailable =
                shadows.mode != SvsmMode::DenseReference &&
                shadows.gpuGatedDrawSubmission;
            if (!packetPageCullingAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Packet Page Culling",
                &shadows.packetPageCullingEnabled);
            if (!packetPageCullingAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                packetPageCullingAvailable
                    ? "Intersect each cached caster packet's conservative bounds with scheduled dirty work on the GPU. Scatter mode uses one conservative dirty rectangle; its fragment ownership checks reject holes. The per-page path retains exact compact lists. Invalid state fails open. Off by default."
                    : "Packet page culling requires sparse mode and GPU-gated draw submission. Its saved value is retained while unavailable.");
            const bool dirtyPageScatterRasterAvailable =
                packetPageCullingAvailable &&
                shadows.packetPageCullingEnabled;
            if (!dirtyPageScatterRasterAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Dirty Page Scatter Raster",
                &shadows.dirtyPageScatterRasterEnabled);
            if (!dirtyPageScatterRasterAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                dirtyPageScatterRasterAvailable
                    ? "Request one virtual-space draw per intersecting caster packet. Runtime activation additionally requires Scatter Amplification Guard, a nonzero all-clipmap budget of at most four, and budget times maximum amplification no greater than 16 pages. Otherwise the exact per-page path remains active. Off by default."
                    : "Dirty page scatter raster requires sparse mode, GPU-gated draw submission, and active Packet Page Culling. Its saved value is retained while unavailable.");
            const bool scatterAlphaTestEarlyRejectAvailable =
                dirtyPageScatterRasterAvailable &&
                shadows.dirtyPageScatterRasterEnabled;
            if (!scatterAlphaTestEarlyRejectAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Scatter Alpha-Test Early Reject",
                &shadows.scatterAlphaTestEarlyRejectEnabled);
            if (!scatterAlphaTestEarlyRejectAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                scatterAlphaTestEarlyRejectAvailable
                    ? "For actual virtual scatter packets, reject unscheduled page holes before sampling alpha textures. Explicit gradients preserve the original texture LOD. Per-page fallback and the disabled reference path retain the original alpha-test order. Off by default."
                    : "Scatter alpha-test early rejection requires active Dirty Page Scatter Raster. Its saved value is retained while unavailable.");
            const bool dirtyPageScatterAmplificationGuardAvailable =
                dirtyPageScatterRasterAvailable &&
                shadows.dirtyPageScatterRasterEnabled;
            if (!dirtyPageScatterAmplificationGuardAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Scatter Amplification Guard",
                &shadows.dirtyPageScatterAmplificationGuardEnabled);
            if (!dirtyPageScatterAmplificationGuardAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                dirtyPageScatterAmplificationGuardAvailable
                    ? "When a packet's dirty rectangle covers too many holes, or packet bounds fail open, draw through compact scheduled-page instances instead. This is required for scatter activation; disabling it leaves the exact per-page path active. Off by default."
                    : "The amplification guard requires active Dirty Page Scatter Raster. Its saved value is retained while unavailable.");
            const bool dirtyPageScatterAmplificationLimitAvailable =
                dirtyPageScatterAmplificationGuardAvailable &&
                shadows.dirtyPageScatterAmplificationGuardEnabled;
            if (!dirtyPageScatterAmplificationLimitAvailable)
                ImGui::BeginDisabled();
            int dirtyPageScatterMaximumAmplification = int(
                shadows.dirtyPageScatterMaximumAmplification);
            if (DrawSliderInt(
                    "Scatter Maximum Page Amplification",
                    &dirtyPageScatterMaximumAmplification,
                    1,
                    int(SvsmMaximumDirtyPageScatterAmplification)))
            {
                shadows.dirtyPageScatterMaximumAmplification = uint32_t(
                    dirtyPageScatterMaximumAmplification);
                customChanged = true;
            }
            if (!dirtyPageScatterAmplificationLimitAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                dirtyPageScatterAmplificationLimitAvailable
                    ? "Use per-page packet instances only when rectangle pages exceed this multiple of the scheduled compact-page count."
                    : "Enable Scatter Amplification Guard to edit this retained threshold.");
            const bool packetRectangleDirectScanAvailable =
                packetPageCullingAvailable &&
                shadows.packetPageCullingEnabled &&
                !shadows.dirtyPageScatterRasterEnabled;
            if (!packetRectangleDirectScanAvailable)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Packet Rectangle Direct Scan",
                &shadows.packetRectangleDirectScanEnabled);
            if (!packetRectangleDirectScanAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                packetRectangleDirectScanAvailable
                    ? "When a packet rectangle has at most half as many pages as the current dirty list, probe the wrapped page table directly. Disable to retain the compact dirty-list packet-culling path. Off by default."
                    : "Packet rectangle direct scan requires active Packet Page Culling with Dirty Page Scatter Raster disabled. Scatter already uses one constant-time rectangle overlap. Its saved value is retained while unavailable.");
            if (!sparseMode)
                ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Page Translation Cache",
                &shadows.pageTranslationCachingEnabled);
            if (!sparseMode)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                sparseMode
                    ? "Reuse exact page-table translations only among this pixel's filter taps; disable to retain the per-tap reference shader."
                    : "Page translation caching applies only to sparse resolve; the saved value is retained.");
            customChanged |= ImGui::Checkbox(
                "Adaptive Filtering", &shadows.adaptiveFiltering);
            ImGui::BeginDisabled();
            customChanged |= ImGui::Checkbox(
                "Fine Caster Exclusion", &shadows.fineCasterExclusion);
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                "Unavailable: UVSR does not yet expose conservative camera-depth proof for every required exclusion condition. The saved value is retained and no caster is excluded.");

            customChanged |= ImGui::Checkbox(
                "Detailed GPU Stage Timing",
                &shadows.detailedGpuTimingEnabled);
            ImGui::SetItemTooltip(
                "Enable separate Mark, Allocate, Clear, Packet, Render, and Filter GPU queries. Disable to issue only the outer total query and avoid per-stage query-resolve overhead. The motion benchmark always uses total-only timing.");

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
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Debug View##SparseVirtualShadowMaps",
                    debugLabels[int(shadows.debugView)]))
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
                            debugLabels[index], selected))
                    {
                        shadows.debugView = debugView;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Present the selected full-screen SVSM diagnostic without changing the lighting visibility texture.");

            if (customChanged)
                shadows.preset = SvsmPreset::Custom;

            ImGui::TextDisabled(
                "SVSM resolves to full-resolution R8 visibility before deferred lighting.");
            if (m_HasSparseShadowStatSnapshot)
            {
                for (const std::string& line :
                    m_SparseShadowStatLines)
                {
                    ImGui::TextUnformatted(line.c_str());
                }
                ImGui::SetItemTooltip(
                    shadows.debugView == SvsmDebugView::None
                        ? "GPU page counters are copied to the CPU only while an SVSM debug view is active; normal cached rendering has no readback."
                        : "GPU page counters remain unavailable until a current debug readback retires. Available counters show their source-frame age; stale backend or debug-mode samples are discarded.");
            }

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
            if (!m_ui.EnableProceduralSky)
                ImGui::BeginDisabled();
            DrawSliderFloat("Brightness", &m_ui.SkyParams.brightness, 0.f, 1.f);
            ImGui::SetItemTooltip("Set sky and ambient light brightness.");
            DrawSliderFloat("Glow Size", &m_ui.SkyParams.glowSize, 0.f, 90.f);
            ImGui::SetItemTooltip("Set the sun glow's size.");
            DrawSliderFloat("Glow Sharpness", &m_ui.SkyParams.glowSharpness, 1.f, 10.f);
            ImGui::SetItemTooltip("Set how quickly the sun glow fades.");
            DrawSliderFloat("Glow Intensity", &m_ui.SkyParams.glowIntensity, 0.f, 1.f);
            ImGui::SetItemTooltip("Set the sun glow's brightness.");
            DrawSliderFloat("Horizon Size", &m_ui.SkyParams.horizonSize, 0.f, 90.f);
            ImGui::SetItemTooltip("Set the horizon blend width.");
            if (!m_ui.EnableProceduralSky)
                ImGui::EndDisabled();

            ImGui::Checkbox("Enable Procedural Sky", &m_ui.EnableProceduralSky);
            ImGui::SetItemTooltip("Show the procedural sky.");
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
    bool& svsmMotionBenchmarkRequested,
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
        if (!strcmp(argv[i], "-width") && i + 1 < argc)
        {
            deviceParams.backBufferWidth = std::stoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-height") && i + 1 < argc)
        {
            deviceParams.backBufferHeight = std::stoi(argv[++i]);
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
        else if (!strcmp(argv[i], "-adapter") && i + 1 < argc)
        {
            deviceParams.adapterIndex = std::stoi(argv[++i]);
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
        else if (!strcmp(argv[i], "--svsm-motion-test"))
        {
            svsmMotionBenchmarkRequested = true;
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
            const char* valueText = argv[++i];
            const char* valueEnd = valueText + std::strlen(valueText);
            const auto parseResult = std::from_chars(
                valueText,
                valueEnd,
                svsmMotionPoolPageCount);
            if (parseResult.ec != std::errc{} ||
                parseResult.ptr != valueEnd)
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
            const char* valueText = argv[++i];
            const char* valueEnd = valueText + std::strlen(valueText);
            const auto parseResult = std::from_chars(
                valueText,
                valueEnd,
                svsmMotionPageRenderBudget);
            if (parseResult.ec != std::errc{} ||
                parseResult.ptr != valueEnd)
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
        else if (!std::strncmp(argv[i], "--svsm-motion-", 14u))
        {
            log::error("Unknown SVSM motion option '%s'", argv[i]);
            return false;
        }
        else if (argv[i][0] != '-')
        {
            sceneName = argv[i];
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
    bool svsmMotionBenchmarkRequested = false;
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
        svsmMotionBenchmarkRequested,
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
    if (svsmMotionBenchmarkRequested && !benchmarkCameraRequested)
    {
        log::error(
            "--svsm-motion-test requires --benchmark-camera so every run uses the standardized 1920 x 1080 camera");
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
    if (!deviceParams.startFullscreen &&
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
        if (svsmMotionBenchmarkRequested)
        {
            uiData.EnablePbr = true;
            uiData.RenderMode = RendererMode::Deferred;
            uiData.SparseVirtualShadowMaps.enabled = true;
            ApplySvsmPreset(
                uiData.SparseVirtualShadowMaps,
                SvsmPreset::Performance);
            uiData.SparseVirtualShadowMaps.physicalPageCount =
                svsmMotionPoolPageCount;
            uiData.SparseVirtualShadowMaps.packetStateSortingEnabled =
                true;
            uiData.SparseVirtualShadowMaps.levelEmptyWorkSkipEnabled =
                true;
            uiData.SparseVirtualShadowMaps.pageRenderBudget =
                svsmMotionPageRenderBudget;
            uiData.SparseVirtualShadowMaps.
                coarsestPageRenderBudgetEnabled = true;
            uiData.SparseVirtualShadowMaps.
                finiteBudgetStaticDrainEnabled = true;
            uiData.SparseVirtualShadowMaps.
                allocationBudgetSaturationEarlyOutEnabled = true;
            uiData.SparseVirtualShadowMaps.
                perPixelMarkingDedupeEnabled = true;
            uiData.SparseVirtualShadowMaps.packetPageCullingEnabled =
                true;
            uiData.SparseVirtualShadowMaps.
                dirtyPageScatterRasterEnabled =
                    svsmMotionScatterRequested;
            uiData.SparseVirtualShadowMaps.
                scatterAlphaTestEarlyRejectEnabled = true;
            uiData.SparseVirtualShadowMaps.
                dirtyPageScatterAmplificationGuardEnabled = true;
            uiData.SparseVirtualShadowMaps.
                dirtyPageScatterMaximumAmplification = 4u;
            uiData.SparseVirtualShadowMaps.
                packetRectangleDirectScanEnabled = true;
            uiData.SparseVirtualShadowMaps.
                recentPageEvictionGraceEnabled = true;
            uiData.SparseVirtualShadowMaps.preset =
                SvsmPreset::Custom;

            if (svsmMotionIsolationRequested)
            {
                // A deliberately minimal diagnostic path used only to locate
                // device-removal boundaries. It is never accepted as timing
                // evidence and does not change any interactive reference path.
                SparseVirtualShadowMapSettings& shadows =
                    uiData.SparseVirtualShadowMaps;
                shadows.tapCount = SvsmTapCount::One;
                shadows.resolutionBias = SvsmResolutionBias::PlusTwo;
                shadows.adaptiveFiltering = false;
                shadows.pageTranslationCachingEnabled = false;
                shadows.detailedGpuTimingEnabled = false;
                shadows.renderPacketCachingEnabled = false;
                shadows.gpuGatedDrawSubmission = false;
                shadows.batchedDrawSubmissionEnabled = false;
                shadows.packetStateSortingEnabled = false;
                shadows.levelEmptyWorkSkipEnabled = false;
                shadows.packetPageCullingEnabled = false;
                shadows.dirtyPageScatterRasterEnabled = false;
                shadows.packetRectangleDirectScanEnabled = false;
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
        }

        const bool svsmMotionDiagnosticConfiguration =
            svsmMotionIsolationRequested ||
            svsmMotionUnbatchedRequested ||
            svsmMotionBatchedDiagnosticRequested ||
            svsmMotionPoolPageCount != 4096u;

        std::shared_ptr<UvsrSceneViewer> demo = std::make_shared<UvsrSceneViewer>(
            deviceManager,
            uiData,
            sceneName,
            benchmarkCameraRequested,
            svsmMotionBenchmarkRequested,
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
