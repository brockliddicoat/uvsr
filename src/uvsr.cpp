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
#include <memory>
#include <chrono>
#include <charconv>
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <system_error>
#include <Windows.h>
#include <GLFW/glfw3.h>

#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/core/string_utils.h>
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
#include "gpu_performance_monitor.h"
#include "camera_collision.h"
#include "camera_controllers.h"
#include "experiment_title.h"
#include "scene_catalog.h"
#include "screen_space_visibility.h"
#include "sponza_camera_preset.h"
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
static bool g_VisibilityBenchmarkFailed = false;
static constexpr uint32_t MaxVisibilityBenchmarkWarmupFrames = 100000u;
static constexpr uint64_t VisibilityBenchmarkQueryDrainAllowanceFrames = 120u;

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

enum class AgxPreset
{
    Base,
    Punchy,
    Golden,
    Mix,
    Custom
};

enum class RendererMode
{
    Deferred,
    Forward,
    ForwardTonemapperless
};

struct AgxToneMappingParameters
{
    float Exposure = 0.f;
    float Contrast = 1.f;
    float Saturation = 1.f;
    float Warmth = 0.f;
    float Tint = 0.f;
    float Slope = 1.f;
    float Power = 1.f;
};

struct KodakLut
{
    std::string Name;
    std::filesystem::path Path;
    nvrhi::TextureHandle Texture;
    uint32_t Size = 0;
    float3 DomainMin = 0.f;
    float3 DomainMax = 1.f;
};

struct alignas(16) AgxToneMappingConstants
{
    float4 ExposureContrastSaturationWarmth;
    float4 TintLutSizeUseLutDither;
    float4 Slope;
    float4 Power;
    float4 LutDomainMin;
    float4 LutDomainMax;
};

class AgxToneMappingPass
{
private:
    nvrhi::DeviceHandle m_Device;
    nvrhi::ShaderHandle m_PixelShader;
    nvrhi::BufferHandle m_ConstantBuffer;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::GraphicsPipelineHandle m_Pipeline;
    nvrhi::ITexture* m_BoundSource = nullptr;
    nvrhi::ITexture* m_BoundLut = nullptr;
    nvrhi::TextureHandle m_ColorLut;
    uint32_t m_ColorLutSize = 0;
    float3 m_LutDomainMin = 0.f;
    float3 m_LutDomainMax = 1.f;
    std::shared_ptr<CommonRenderPasses> m_CommonPasses;
    std::shared_ptr<FramebufferFactory> m_FramebufferFactory;

public:
    AgxToneMappingPass(
        nvrhi::IDevice* device,
        const std::shared_ptr<ShaderFactory>& shaderFactory,
        const std::shared_ptr<CommonRenderPasses>& commonPasses,
        const std::shared_ptr<FramebufferFactory>& framebufferFactory)
        : m_Device(device)
        , m_CommonPasses(commonPasses)
        , m_FramebufferFactory(framebufferFactory)
    {
        m_PixelShader = shaderFactory->CreateShader(
            "uvsr/agx_tonemapping_ps.hlsl", "main", nullptr, nvrhi::ShaderType::Pixel);

        nvrhi::BufferDesc bufferDesc;
        bufferDesc.byteSize = sizeof(AgxToneMappingConstants);
        bufferDesc.debugName = "AgxToneMappingConstants";
        bufferDesc.isConstantBuffer = true;
        bufferDesc.isVolatile = true;
        bufferDesc.maxVersions = c_MaxRenderPassConstantBufferVersions;
        m_ConstantBuffer = device->createBuffer(bufferDesc);

        nvrhi::BindingLayoutDesc layoutDesc;
        layoutDesc.visibility = nvrhi::ShaderType::Pixel;
        layoutDesc.bindings = {
            nvrhi::BindingLayoutItem::VolatileConstantBuffer(0),
            nvrhi::BindingLayoutItem::Texture_SRV(0),
            nvrhi::BindingLayoutItem::Texture_SRV(1),
            nvrhi::BindingLayoutItem::Sampler(0)
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

    void SetColorLut(const KodakLut* lut)
    {
        m_ColorLut = lut ? lut->Texture : nullptr;
        m_ColorLutSize = lut ? lut->Size : 0;
        m_LutDomainMin = lut ? lut->DomainMin : float3(0.f);
        m_LutDomainMax = lut ? lut->DomainMax : float3(1.f);
        m_BindingSet = nullptr;
        m_BoundLut = nullptr;
    }

    void Render(
        nvrhi::ICommandList* commandList,
        const AgxToneMappingParameters& params,
        const ICompositeView& compositeView,
        nvrhi::ITexture* sourceTexture)
    {
        nvrhi::ITexture* lutTexture = m_ColorLut
            ? m_ColorLut.Get()
            : m_CommonPasses->m_BlackTexture3D.Get();

        if (!m_BindingSet || m_BoundSource != sourceTexture || m_BoundLut != lutTexture)
        {
            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
                nvrhi::BindingSetItem::Texture_SRV(1, lutTexture),
                nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler)
            };
            m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
            m_BoundSource = sourceTexture;
            m_BoundLut = lutTexture;
        }

        AgxToneMappingConstants constants{};
        constants.ExposureContrastSaturationWarmth = float4(
            params.Exposure, params.Contrast, params.Saturation, params.Warmth);
        constants.TintLutSizeUseLutDither = float4(
            params.Tint, float(m_ColorLutSize), m_ColorLut ? 1.f : 0.f, 1.f);
        constants.Slope = float4(float3(params.Slope), 0.f);
        constants.Power = float4(float3(params.Power), 0.f);
        constants.LutDomainMin = float4(m_LutDomainMin, 0.f);
        constants.LutDomainMax = float4(m_LutDomainMax, 0.f);
        commandList->writeBuffer(m_ConstantBuffer, &constants, sizeof(constants));

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

static bool LoadCubeLut(
    nvrhi::IDevice* device,
    const std::filesystem::path& path,
    KodakLut& result)
{
    std::ifstream file(path);
    if (!file)
    {
        log::error("Cannot open Kodak LUT '%s'", path.generic_string().c_str());
        return false;
    }

    uint32_t size = 0;
    std::string title;
    float3 domainMin = 0.f;
    float3 domainMax = 1.f;
    std::vector<float4> values;
    std::string line;

    while (std::getline(file, line))
    {
        const size_t comment = line.find('#');
        if (comment != std::string::npos)
            line.erase(comment);

        std::istringstream tokens(line);
        std::string keyword;
        if (!(tokens >> keyword))
            continue;

        if (keyword == "TITLE")
        {
            std::getline(tokens >> std::ws, title);
            if (title.size() >= 2 && title.front() == '"' && title.back() == '"')
                title = title.substr(1, title.size() - 2);
        }
        else if (keyword == "LUT_3D_SIZE")
        {
            tokens >> size;
            if (size < 2 || size > 128)
            {
                log::error("Kodak LUT '%s' has unsupported size %u (expected 2-128)",
                    path.generic_string().c_str(), size);
                return false;
            }
        }
        else if (keyword == "LUT_1D_SIZE")
        {
            log::error("Kodak LUT '%s' contains an unsupported 1D table",
                path.generic_string().c_str());
            return false;
        }
        else if (keyword == "DOMAIN_MIN")
        {
            tokens >> domainMin.x >> domainMin.y >> domainMin.z;
        }
        else if (keyword == "DOMAIN_MAX")
        {
            tokens >> domainMax.x >> domainMax.y >> domainMax.z;
        }
        else
        {
            std::istringstream sample(line);
            float r, g, b;
            if (sample >> r >> g >> b)
                values.emplace_back(r, g, b, 1.f);
        }
    }

    const uint64_t expectedValueCount = uint64_t(size) * size * size;
    if (size == 0 || values.size() != expectedValueCount)
    {
        log::error("Kodak LUT '%s' has %zu values; expected %llu",
            path.generic_string().c_str(), values.size(), expectedValueCount);
        return false;
    }

    nvrhi::TextureDesc textureDesc;
    textureDesc.width = size;
    textureDesc.height = size;
    textureDesc.depth = size;
    textureDesc.dimension = nvrhi::TextureDimension::Texture3D;
    textureDesc.format = nvrhi::Format::RGBA32_FLOAT;
    textureDesc.initialState = nvrhi::ResourceStates::Common;
    textureDesc.debugName = path.stem().string();

    nvrhi::TextureHandle texture = device->createTexture(textureDesc);
    if (!texture)
    {
        log::error("Cannot create GPU texture for Kodak LUT '%s'",
            path.generic_string().c_str());
        return false;
    }

    nvrhi::CommandListHandle commandList = device->createCommandList();
    commandList->open();
    commandList->beginTrackingTextureState(
        texture, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
    commandList->writeTexture(
        texture, 0, 0, values.data(),
        size_t(size) * sizeof(float4),
        size_t(size) * size * sizeof(float4));
    commandList->setPermanentTextureState(
        texture, nvrhi::ResourceStates::ShaderResource);
    commandList->commitBarriers();
    commandList->close();
    device->executeCommandList(commandList);

    result.Name = title.empty() ? path.stem().string() : title;
    result.Path = path;
    result.Texture = texture;
    result.Size = size;
    result.DomainMin = domainMin;
    result.DomainMax = domainMax;
    return true;
}

static AgxToneMappingParameters GetAgxPresetParameters(AgxPreset preset)
{
    AgxToneMappingParameters params;

    switch (preset)
    {
    case AgxPreset::Base:
        break;

    case AgxPreset::Punchy:
        params.Exposure = 0.45f;
        params.Contrast = 1.04f;
        params.Saturation = 1.20f;
        params.Power = 0.98f;
        break;

    case AgxPreset::Golden:
        params.Exposure = 0.40f;
        params.Contrast = 0.98f;
        params.Saturation = 1.14f;
        params.Warmth = 0.18f;
        params.Tint = 0.03f;
        params.Slope = 1.01f;
        params.Power = 0.97f;
        break;

    case AgxPreset::Mix:
        params.Exposure = 0.40f;
        params.Contrast = 1.00f;
        params.Saturation = 1.16f;
        params.Warmth = 0.08f;
        params.Tint = 0.01f;
        params.Power = 0.98f;
        break;

    case AgxPreset::Custom:
        break;
    }

    return params;
}

static uint32_t GetVisibilityLaterBounceSampleCount(
    uint32_t firstBounceSampleCount)
{
    firstBounceSampleCount = std::clamp(
        firstBounceSampleCount, 1u, 64u);
    const uint32_t floor = std::min(firstBounceSampleCount, 8u);
    return std::max(firstBounceSampleCount >> 1u, floor);
}

static VisibilityPerformanceWorkload BuildVisibilityPerformanceWorkload(
    const ScreenSpaceVisibilitySettings& visibility,
    uint32_t outputWidth,
    uint32_t outputHeight)
{
    VisibilityPerformanceWorkload workload;
    const bool ambientEnabled = visibility.HasActiveAmbientOcclusion();
    const bool indirectEnabled = visibility.HasActiveIndirectDiffuse();
    if (ambientEnabled && indirectEnabled)
    {
        workload.consumer = VisibilityPerformanceConsumer::
            AmbientOcclusionAndIndirectDiffuse;
    }
    else if (indirectEnabled)
    {
        workload.consumer = VisibilityPerformanceConsumer::IndirectDiffuse;
    }
    else
    {
        workload.consumer = VisibilityPerformanceConsumer::AmbientOcclusion;
    }

    workload.estimator = static_cast<VisibilityPerformanceEstimator>(
        visibility.estimator);
    workload.resolution = static_cast<VisibilityPerformanceResolution>(
        visibility.resolution);
    workload.scheduler = static_cast<VisibilityPerformanceScheduler>(
        visibility.sampling.scheduler);

    const VisibilityPerformanceProfileConfiguration configuration =
        GetVisibilityPerformanceProfileConfiguration(
            visibility.performanceProfile);
    if (configuration.noise ==
        VisibilityNoiseDelivery::ActivisionInterleavedGradient)
    {
        workload.scheduler =
            VisibilityPerformanceScheduler::Activision4x4SixPhase;
    }
    else if (configuration.noise ==
            VisibilityNoiseDelivery::XeGtaoHilbertR2 ||
        configuration.noise ==
            VisibilityNoiseDelivery::XeGtaoInlineHilbertR2)
    {
        workload.scheduler = VisibilityPerformanceScheduler::XeGtaoHilbertR2;
    }
    else if (configuration.noise ==
        VisibilityNoiseDelivery::ConstantDiagnostic)
    {
        workload.scheduler =
            VisibilityPerformanceScheduler::ConstantDiagnostic;
    }

    workload.firstBounceSampleCount = std::clamp(
        visibility.sampling.maximumSampleCount, 1u, 64u);
    workload.bounceCount = indirectEnabled
        ? std::clamp(visibility.indirectDiffuse.bounceCount,
            1u, MaxIndirectDiffuseBounceCount)
        : 1u;
    workload.laterBounceSampleCount =
        GetVisibilityLaterBounceSampleCount(
            workload.firstBounceSampleCount);
    workload.outputWidth = outputWidth;
    workload.outputHeight = outputHeight;
    workload.radius = visibility.sampling.radius;
    workload.thickness = visibility.sampling.thickness;
    workload.radialExponent = visibility.sampling.stepDistributionExponent;
    workload.threadGroupSizeX = 8u;
    workload.threadGroupSizeY = 8u;
    if (visibility.performanceProfile ==
        VisibilityPerformanceProfile::ExactGroup16x8)
    {
        workload.threadGroupSizeX = 16u;
    }
    else if (visibility.performanceProfile ==
        VisibilityPerformanceProfile::ExactGroup8x16)
    {
        workload.threadGroupSizeY = 16u;
    }
    workload.adaptiveSamplingEnabled = visibility.UsesAdaptiveSampling();
    workload.temporalEnabled = visibility.reconstruction.temporalEnabled;
    workload.spatialEnabled = visibility.reconstruction.spatialEnabled;
    workload.depthHierarchyEnabled = ambientEnabled && !indirectEnabled &&
        visibility.sampling.radius >= 8.f;
    return workload;
}

static void SetCanonicalVisibilityBenchmarkDefaults(
    ScreenSpaceVisibilitySettings& visibility)
{
    visibility.enabled = true;
    visibility.quality = ScreenSpaceVisibilityQuality::Custom;
    visibility.estimator = VisibilityEstimator::UniformSolidAngle;
    visibility.resolution = VisibilityResolution::Half;
    visibility.sampling.minimumSampleCount = 8u;
    visibility.sampling.maximumSampleCount = 8u;
    visibility.sampling.adaptiveSparseSamplingEnabled = false;
    visibility.sampling.radius = 3.f;
    visibility.sampling.thickness = 0.5f;
    visibility.sampling.stepDistributionExponent = 2.f;
    visibility.sampling.adaptiveStrength = 1.f;
    visibility.sampling.scheduler =
        VisibilitySampleScheduler::ToroidalBlueNoiseRankField;
    visibility.ambientOcclusion.enabled = true;
    visibility.ambientOcclusion.strength = 1.f;
    visibility.indirectDiffuse.enabled = false;
    visibility.indirectDiffuse.bounceCount = 1u;
    visibility.indirectDiffuse.minimumBounceContribution = 0.001f;
    visibility.indirectDiffuse.intensity = 4.f;
    visibility.indirectDiffuse.includeEmissive = true;
    visibility.indirectDiffuse.emissiveGain = 4.f;
    visibility.reconstruction.temporalEnabled = false;
    visibility.reconstruction.spatialEnabled = false;
    visibility.reconstruction.temporalResponse = 0.35f;
    visibility.reconstruction.spatialFilter =
        VisibilitySpatialFilter::GaussianJointBilateral;
    visibility.reconstruction.spatialRadius = 4.f;
    visibility.showIndirectDiffuseOnly = false;
}

static uint32_t GetVisibilityFixedSampleCount(
    VisibilitySampleSpecialization specialization)
{
    switch (specialization)
    {
    case VisibilitySampleSpecialization::Fixed8: return 8u;
    case VisibilitySampleSpecialization::Fixed12: return 12u;
    case VisibilitySampleSpecialization::Fixed16: return 16u;
    case VisibilitySampleSpecialization::Fixed18: return 18u;
    case VisibilitySampleSpecialization::Fixed20: return 20u;
    default: return 0u;
    }
}

static bool ApplyVisibilityVerificationProfileDefaults(
    ScreenSpaceVisibilitySettings& visibility,
    VisibilityVerificationProfile profile)
{
    const VisibilityVerificationProfileDefinition definition =
        GetVisibilityVerificationProfileDefinition(profile);
    if (definition.implementationStatus ==
            VisibilityImplementationStatus::Unavailable ||
        definition.implementationStatus ==
            VisibilityImplementationStatus::Unset ||
        definition.implementationProfile ==
            VisibilityPerformanceProfile::Unset)
    {
        return false;
    }

    SetCanonicalVisibilityBenchmarkDefaults(visibility);
    const VisibilityPerformanceWorkload& workload =
        definition.expectedWorkload;
    visibility.estimator = static_cast<VisibilityEstimator>(
        workload.estimator);
    visibility.resolution = static_cast<VisibilityResolution>(
        workload.resolution);
    visibility.sampling.minimumSampleCount =
        workload.firstBounceSampleCount;
    visibility.sampling.maximumSampleCount =
        workload.firstBounceSampleCount;
    visibility.sampling.adaptiveSparseSamplingEnabled =
        workload.adaptiveSamplingEnabled;
    visibility.sampling.radius = workload.radius;
    visibility.sampling.thickness = workload.thickness;
    visibility.sampling.stepDistributionExponent = workload.radialExponent;
    switch (workload.scheduler)
    {
    case VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField:
        visibility.sampling.scheduler =
            VisibilitySampleScheduler::ToroidalBlueNoiseRankField;
        break;
    case VisibilityPerformanceScheduler::
            FilterAdaptedSpatiotemporalRankField:
        visibility.sampling.scheduler = VisibilitySampleScheduler::
            FilterAdaptedSpatiotemporalRankField;
        break;
    default:
        // Advanced schedulers are selected by their implementation profile
        // and have no generic user-facing scheduler enum.
        visibility.sampling.scheduler =
            VisibilitySampleScheduler::IndependentHash;
        break;
    }

    visibility.ambientOcclusion.enabled = workload.consumer !=
        VisibilityPerformanceConsumer::IndirectDiffuse;
    visibility.indirectDiffuse.enabled = workload.consumer !=
        VisibilityPerformanceConsumer::AmbientOcclusion;
    visibility.indirectDiffuse.bounceCount = workload.bounceCount;
    visibility.reconstruction.temporalEnabled = workload.temporalEnabled;
    visibility.reconstruction.spatialEnabled = workload.spatialEnabled;
    visibility.performanceProfile = definition.implementationProfile;
    return true;
}

static bool ApplyVisibilityPerformanceProfileDefaults(
    ScreenSpaceVisibilitySettings& visibility,
    VisibilityPerformanceProfile profile)
{
    const VisibilityPerformanceProfileConfiguration configuration =
        GetVisibilityPerformanceProfileConfiguration(profile);
    if (configuration.profile == VisibilityPerformanceProfile::Unset ||
        configuration.implementationStatus ==
            VisibilityImplementationStatus::Unavailable ||
        configuration.implementationStatus ==
            VisibilityImplementationStatus::Unset)
    {
        return false;
    }

    SetCanonicalVisibilityBenchmarkDefaults(visibility);
    visibility.performanceProfile = profile;

    uint32_t firstBounceSampleCount = GetVisibilityFixedSampleCount(
        configuration.firstBounceSamples);
    if (firstBounceSampleCount == 0u)
        firstBounceSampleCount = 8u;
    if (profile == VisibilityPerformanceProfile::ExactFixedLaterBounce8)
        firstBounceSampleCount = 16u;
    visibility.sampling.minimumSampleCount = firstBounceSampleCount;
    visibility.sampling.maximumSampleCount = firstBounceSampleCount;

    switch (configuration.consumerRequirement)
    {
    case VisibilityConsumerRequirement::AmbientOcclusionOnly:
    case VisibilityConsumerRequirement::IncludesAmbientOcclusion:
        visibility.ambientOcclusion.enabled = true;
        visibility.indirectDiffuse.enabled = false;
        break;
    case VisibilityConsumerRequirement::IncludesIndirectDiffuse:
        visibility.ambientOcclusion.enabled = true;
        visibility.indirectDiffuse.enabled = true;
        visibility.indirectDiffuse.bounceCount = 2u;
        break;
    default:
        break;
    }

    switch (configuration.estimatorRequirement)
    {
    case VisibilityEstimatorRequirement::UniformProjectedAngle:
        visibility.estimator = VisibilityEstimator::UniformProjectedAngle;
        break;
    case VisibilityEstimatorRequirement::UniformSolidAngle:
        visibility.estimator = VisibilityEstimator::UniformSolidAngle;
        break;
    case VisibilityEstimatorRequirement::CosineWeightedSolidAngle:
        visibility.estimator =
            VisibilityEstimator::CosineWeightedSolidAngle;
        break;
    default:
        break;
    }

    if (configuration.resolutionRequirement ==
            VisibilityResolutionRequirement::Reduced ||
        configuration.resolutionRequirement ==
            VisibilityResolutionRequirement::Half)
    {
        visibility.resolution = VisibilityResolution::Half;
    }

    if (configuration.noise == VisibilityNoiseDelivery::PackedCurrentFast)
    {
        visibility.sampling.scheduler = VisibilitySampleScheduler::
            FilterAdaptedSpatiotemporalRankField;
    }
    else if (configuration.noise ==
            VisibilityNoiseDelivery::ActivisionInterleavedGradient ||
        configuration.noise == VisibilityNoiseDelivery::XeGtaoHilbertR2 ||
        configuration.noise ==
            VisibilityNoiseDelivery::XeGtaoInlineHilbertR2)
    {
        visibility.sampling.scheduler =
            VisibilitySampleScheduler::IndependentHash;
    }

    visibility.reconstruction.temporalEnabled =
        configuration.temporal == VisibilityTemporalMode::CopyDiagnostic ||
        configuration.temporal ==
            VisibilityTemporalMode::ActivisionSixDirectionEma;
    visibility.reconstruction.spatialEnabled =
        configuration.reconstruction ==
            VisibilityReconstructionMode::ActivisionBilateral4x4;
    if (configuration.reconstruction ==
        VisibilityReconstructionMode::ActivisionBilateral4x4)
    {
        // Published GTAO distributes four taps per side directly over the
        // radius. The temporal response is a disclosed UVSR prototype value;
        // Activision's shipping coefficient was not published.
        visibility.sampling.stepDistributionExponent = 1.f;
        visibility.reconstruction.temporalResponse = 0.12f;
        visibility.reconstruction.spatialRadius = 2.f;
    }
    else if (configuration.reconstruction ==
        VisibilityReconstructionMode::XeGtaoDenoise)
    {
        visibility.resolution = VisibilityResolution::Full;
        visibility.sampling.minimumSampleCount = 18u;
        visibility.sampling.maximumSampleCount = 18u;
        visibility.sampling.radius = 0.5f;
        visibility.sampling.thickness = 0.f;
        visibility.sampling.stepDistributionExponent = 2.f;
        visibility.reconstruction.temporalEnabled = false;
        visibility.reconstruction.spatialEnabled = true;
        visibility.reconstruction.spatialRadius = 1.f;
    }
    return true;
}

static std::string_view GetVisibilityPerformanceProfileDisplayName(
    VisibilityPerformanceProfile profile)
{
    switch (profile)
    {
    case VisibilityPerformanceProfile::ActivisionPs4Schedule:
        return "PS4 GTAO - Scalar 4x4";
    case VisibilityPerformanceProfile::ActivisionPs4PackedGather:
        return "PS4 GTAO - Packed 4x4 Gather";
    case VisibilityPerformanceProfile::XeGtaoClosestMatch:
        return "XeGTAO High - LUT / Mixed";
    case VisibilityPerformanceProfile::XeGtaoHighInlineHilbert:
        return "XeGTAO High - Inline Hilbert / Mixed";
    case VisibilityPerformanceProfile::XeGtaoHighFp32:
        return "XeGTAO High - LUT / FP32";
    default:
        return GetVisibilityPerformanceProfileConfiguration(profile).name;
    }
}

static std::string NormalizeVisibilityProfileName(std::string_view name)
{
    std::string normalized;
    normalized.reserve(name.size());
    for (const unsigned char character : name)
    {
        if (std::isalnum(character))
            normalized.push_back(char(std::tolower(character)));
    }
    return normalized;
}

static bool TryParseVisibilityVerificationProfile(
    std::string_view name,
    VisibilityVerificationProfile& profile)
{
    const std::string normalized = NormalizeVisibilityProfileName(name);
    for (uint32_t profileIndex = 1u;
        profileIndex < static_cast<uint32_t>(
            VisibilityVerificationProfile::Count);
        ++profileIndex)
    {
        const auto candidate = static_cast<VisibilityVerificationProfile>(
            profileIndex);
        const VisibilityVerificationProfileDefinition definition =
            GetVisibilityVerificationProfileDefinition(candidate);
        if (NormalizeVisibilityProfileName(definition.name) == normalized)
        {
            profile = candidate;
            return true;
        }
    }
    return false;
}

static bool TryParseVisibilityPerformanceProfile(
    std::string_view name,
    VisibilityPerformanceProfile& profile)
{
    const std::string normalized = NormalizeVisibilityProfileName(name);
    for (uint32_t profileIndex = 1u;
        profileIndex < static_cast<uint32_t>(
            VisibilityPerformanceProfile::Count);
        ++profileIndex)
    {
        const auto candidate = static_cast<VisibilityPerformanceProfile>(
            profileIndex);
        const VisibilityPerformanceProfileConfiguration configuration =
            GetVisibilityPerformanceProfileConfiguration(candidate);
        if (NormalizeVisibilityProfileName(configuration.name) == normalized ||
            NormalizeVisibilityProfileName(
                GetVisibilityPerformanceProfileDisplayName(candidate)) ==
                    normalized)
        {
            profile = candidate;
            return true;
        }
    }
    return false;
}

struct VisibilityBenchmarkLaunchOptions
{
    VisibilityVerificationProfile profile =
        VisibilityVerificationProfile::ReferenceAo8T;
    bool profileSpecified = false;
    VisibilityPerformanceProfile implementationProfile =
        VisibilityPerformanceProfile::Unset;
    bool implementationProfileSpecified = false;
    bool benchmarkRequested = false;
    uint8_t sequenceKind = 0u;
    uint32_t warmupFrameCount = 120u;
    uint32_t measuredFrameCount = 240u;
    std::filesystem::path outputDirectory;
    bool autoClose = false;
};

struct UIData
{
    bool                                ShowUI = true;
    std::vector<GpuAdapterChoice>       GpuAdapterChoices;
    int                                 ActiveGpuAdapterIndex = -1;
    bool                                EnablePbr = true;
    RendererMode                        RenderMode = RendererMode::Deferred;
    ScreenSpaceVisibilitySettings       ScreenSpaceVisibility;
    AgxToneMappingParameters            AgxToneMappingParams;
    AgxPreset                           AgxToneMappingPreset = AgxPreset::Base;
    SkyParameters                       SkyParams;
    bool                                ShaderReloadRequested = false;
    bool                                EnableProceduralSky = true;
    WhiteWorldMode                      WhiteWorld = WhiteWorldMode::Off;
    CameraMode                          Camera = CameraMode::ThirdPerson;
    std::shared_ptr<Material>           SelectedMaterial;
    std::shared_ptr<SceneGraphNode>     SelectedNode;
    bool                                CopyScreenshotToClipboard = false;
    VisibilityVerificationProfile       VisibilityVerification =
        VisibilityVerificationProfile::ReferenceAo8T;

    [[nodiscard]] bool UsesDeferredShading() const
    {
        return RenderMode == RendererMode::Deferred;
    }

    [[nodiscard]] bool UsesTonemapper() const
    {
        return RenderMode != RendererMode::ForwardTonemapperless;
    }
};

struct VisibilityBenchmarkExportPaths
{
    std::filesystem::path json;
    std::filesystem::path csv;
    std::filesystem::path presentedFrame;
};

struct VisibilityBenchmarkRunSettings
{
    std::string canonical;
    uint64_t hash = 0u;
    std::string implementationProfile;
    std::string optimizationClass;
    std::string consumer;
    bool visibilityEnabled = false;
    bool ambientOcclusionEnabled = false;
    float ambientOcclusionStrength = 0.f;
    bool indirectDiffuseEnabled = false;
    uint32_t indirectDiffuseBounceCount = 0u;
    float indirectDiffuseMinimumBounceContribution = 0.f;
    float indirectDiffuseIntensity = 0.f;
    bool indirectDiffuseIncludeEmissive = false;
    float indirectDiffuseEmissiveGain = 0.f;
    bool showIndirectDiffuseOnly = false;
    uint32_t outputWidth = 0u;
    uint32_t outputHeight = 0u;
    std::string traceResolution;
    uint32_t traceWidth = 0u;
    uint32_t traceHeight = 0u;
    std::string estimator;
    uint32_t minimumSampleCount = 0u;
    uint32_t maximumSampleCount = 0u;
    bool adaptiveSamplingRequested = false;
    bool adaptiveSamplingActive = false;
    float adaptiveStrength = 0.f;
    float radius = 0.f;
    float thickness = 0.f;
    bool thicknessActive = true;
    float radialDistributionExponent = 0.f;
    std::string scheduler;
    std::string noiseDelivery;
    std::string traceImplementation;
    std::string firstBounceSpecialization;
    std::string laterBounceSpecialization;
    std::string mathProfile;
    std::string precisionProfile;
    bool temporalEnabled = false;
    std::string temporalMode;
    float temporalResponse = 0.f;
    std::string temporalAoFormat;
    std::string temporalDepthFormat;
    std::string temporalNormalFormat;
    bool spatialEnabled = false;
    std::string spatialFilter;
    float spatialRadius = 0.f;
    bool spatialRadiusActive = false;
    std::string reconstruction;
    std::string application;
    uint32_t threadGroupSizeX = 0u;
    uint32_t threadGroupSizeY = 0u;
    std::string rawAoFormat;
    std::string edgeMetadataFormat;
    std::string finalResolvedAoFormat;
    std::string depthMode;
    std::string bindingStrategy;
    uint32_t plannedDispatchCount = 0u;
    uint32_t firstTraceSrvCount = 0u;
    uint32_t firstTraceUavCount = 0u;
    uint32_t peakSrvCount = 0u;
    uint32_t peakUavCount = 0u;
    uint64_t optionalTextureBytes = 0u;
    uint64_t fullResolutionIntermediateBytes = 0u;
    uint64_t logicalTrafficAvoidedBytes = 0u;
};

struct VisibilityBenchmarkArtifactMetadata
{
    std::string sceneName;
    std::string cameraPresetId;
    std::string graphicsApi;
    std::string buildIdentity;
    uint64_t shaderPermutationKey = 0u;
    uint32_t outputWidth = 0u;
    uint32_t outputHeight = 0u;
    std::string sequenceName;
    std::string sequenceEntryName;
    uint32_t sequenceEntryIndex = 0u;
    uint32_t sequenceEntryCount = 0u;
    VisibilityBenchmarkRunSettings runSettings;
};

enum class VisibilityBenchmarkSequenceKind : uint8_t
{
    None,
    ReferenceVersusCurrent,
    FixedSample,
    Noise,
    Reconstruction,
    Math,
    AllImplemented,
    NewCandidates,
    Precision
};

static bool TryParseVisibilityBenchmarkSequenceKind(
    std::string_view name,
    VisibilityBenchmarkSequenceKind& kind)
{
    const std::string normalized = NormalizeVisibilityProfileName(name);
    if (normalized == "referenceversuscurrent" || normalized == "reference")
        kind = VisibilityBenchmarkSequenceKind::ReferenceVersusCurrent;
    else if (normalized == "fixed" || normalized == "fixedsample" ||
        normalized == "fixedsamplematrix")
        kind = VisibilityBenchmarkSequenceKind::FixedSample;
    else if (normalized == "noise" || normalized == "noisematrix")
        kind = VisibilityBenchmarkSequenceKind::Noise;
    else if (normalized == "reconstruction" ||
        normalized == "reconstructionmatrix")
        kind = VisibilityBenchmarkSequenceKind::Reconstruction;
    else if (normalized == "math" || normalized == "mathmatrix")
        kind = VisibilityBenchmarkSequenceKind::Math;
    else if (normalized == "all" || normalized == "smoke" ||
        normalized == "allprofiles" || normalized == "allimplemented")
        kind = VisibilityBenchmarkSequenceKind::AllImplemented;
    else if (normalized == "new" || normalized == "newcandidates" ||
        normalized == "sourceports" || normalized == "gtao")
        kind = VisibilityBenchmarkSequenceKind::NewCandidates;
    else if (normalized == "precision" || normalized == "precisionmatrix")
        kind = VisibilityBenchmarkSequenceKind::Precision;
    else
        return false;
    return true;
}

struct VisibilityBenchmarkSequenceEntry
{
    std::string name;
    VisibilityPerformanceProfile profile =
        VisibilityPerformanceProfile::Unset;
    bool useSavedCurrentSettings = false;
    bool overrideScheduler = false;
    VisibilitySampleScheduler scheduler =
        VisibilitySampleScheduler::ToroidalBlueNoiseRankField;
    bool overrideSpatialFilter = false;
    bool spatialEnabled = false;
    VisibilitySpatialFilter spatialFilter =
        VisibilitySpatialFilter::GaussianJointBilateral;
    bool overrideConsumer = false;
    bool ambientOcclusionEnabled = true;
    bool indirectDiffuseEnabled = false;
    uint32_t indirectDiffuseBounceCount = 1u;
    bool overrideTemporal = false;
    bool temporalEnabled = false;
    float temporalResponse = 0.35f;
};

static const char* DescribeVisibilityOptimizationClass(
    VisibilityOptimizationClass value)
{
    switch (value)
    {
    case VisibilityOptimizationClass::Reference: return "Reference";
    case VisibilityOptimizationClass::Diagnostic: return "Diagnostic Only";
    case VisibilityOptimizationClass::Exact:
        return "Exact Implementation Change";
    case VisibilityOptimizationClass::Numerical:
        return "Numerical Approximation";
    case VisibilityOptimizationClass::Algorithmic:
        return "Algorithmic Approximation";
    default: return "Unclassified";
    }
}

static const char* DescribeVisibilityConsumer(
    const VisibilityPerformanceWorkload& workload)
{
    switch (workload.consumer)
    {
    case VisibilityPerformanceConsumer::AmbientOcclusion:
        return "AO Only";
    case VisibilityPerformanceConsumer::IndirectDiffuse:
        return workload.bounceCount > 1u
            ? "GI Only, Multi-Bounce" : "GI Only";
    case VisibilityPerformanceConsumer::AmbientOcclusionAndIndirectDiffuse:
        return workload.bounceCount > 1u
            ? "AO + GI Multi-Bounce" : "AO + GI";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityEstimator(
    VisibilityPerformanceEstimator value)
{
    switch (value)
    {
    case VisibilityPerformanceEstimator::UniformProjectedAngle:
        return "Uniform Projected Angle";
    case VisibilityPerformanceEstimator::UniformSolidAngle:
        return "Uniform Solid Angle";
    case VisibilityPerformanceEstimator::CosineWeightedSolidAngle:
        return "Cosine-Weighted Solid Angle";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityResolution(
    VisibilityPerformanceResolution value)
{
    switch (value)
    {
    case VisibilityPerformanceResolution::Full: return "Full";
    case VisibilityPerformanceResolution::Half: return "Half";
    case VisibilityPerformanceResolution::Quarter: return "Quarter";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityScheduler(
    VisibilityPerformanceScheduler value)
{
    switch (value)
    {
    case VisibilityPerformanceScheduler::IndependentHash:
        return "Independent Hash";
    case VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField:
        return "Current Toroidal Blue Noise";
    case VisibilityPerformanceScheduler::
            FilterAdaptedSpatiotemporalRankField:
        return "Current Scalar FAST";
    case VisibilityPerformanceScheduler::Activision4x4SixPhase:
        return "Activision 4x4 x 6 Schedule";
    case VisibilityPerformanceScheduler::XeGtaoHilbertR2:
        return "XeGTAO Hilbert + R2";
    case VisibilityPerformanceScheduler::ConstantDiagnostic:
        return "Constant Diagnostic";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityTrace(
    VisibilityTraceImplementation value)
{
    switch (value)
    {
    case VisibilityTraceImplementation::LegacyGenericBitmask:
        return "Reference Generic Bitmask";
    case VisibilityTraceImplementation::FixedInterleavedBitmask:
        return "Fixed Interleaved Bitmask";
    case VisibilityTraceImplementation::ConstantDiagnostic:
        return "Constant-Output Diagnostic";
    case VisibilityTraceImplementation::DepthOnlyDiagnostic:
        return "Depth-Read-Only Diagnostic";
    case VisibilityTraceImplementation::BitmaskOnlyDiagnostic:
        return "Bitmask-Math-Only Diagnostic";
    case VisibilityTraceImplementation::ActivisionHorizon:
        return "Activision Horizon Control";
    case VisibilityTraceImplementation::XeGtaoHorizon:
        return "Intel XeGTAO 1.30 Source Port";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilitySampleSpecialization(
    VisibilitySampleSpecialization value)
{
    switch (value)
    {
    case VisibilitySampleSpecialization::Runtime: return "Generic";
    case VisibilitySampleSpecialization::Fixed8: return "Fixed 8";
    case VisibilitySampleSpecialization::Fixed12: return "Fixed 12";
    case VisibilitySampleSpecialization::Fixed16: return "Fixed 16";
    case VisibilitySampleSpecialization::Fixed18: return "Fixed 18";
    case VisibilitySampleSpecialization::Fixed20: return "Fixed 20";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityNoiseDelivery(
    VisibilityNoiseDelivery value)
{
    switch (value)
    {
    case VisibilityNoiseDelivery::Legacy:
        return "Reference Scheduler Resources";
    case VisibilityNoiseDelivery::PackedCurrentFast:
        return "Packed RGBA8 Current FAST";
    case VisibilityNoiseDelivery::ConstantDiagnostic:
        return "Constant Diagnostic";
    case VisibilityNoiseDelivery::ActivisionInterleavedGradient:
        return "Activision Interleaved Gradient";
    case VisibilityNoiseDelivery::XeGtaoHilbertR2:
        return "XeGTAO Hilbert + R2";
    case VisibilityNoiseDelivery::XeGtaoInlineHilbertR2:
        return "XeGTAO Inline Hilbert + R2";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityMath(VisibilityMathMode value)
{
    switch (value)
    {
    case VisibilityMathMode::ReferenceFp32: return "Reference FP32";
    case VisibilityMathMode::ConservativeNumericalFp32:
        return "Conservative Numerical FP32";
    case VisibilityMathMode::ActivisionFastFp32:
        return "Activision Fast FP32";
    case VisibilityMathMode::XeGtaoMixedPrecision:
        return "XeGTAO Mixed Precision";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityRawAoFormat(
    VisibilityRawAoStorage value)
{
    switch (value)
    {
    case VisibilityRawAoStorage::R16Float: return "R16_FLOAT";
    case VisibilityRawAoStorage::R8Unorm: return "R8_UNORM";
    case VisibilityRawAoStorage::PackedCountAndEdgesR16Uint:
        return "Packed R16_UINT Count + Edges";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityEdgeFormat(VisibilityEdgeStorage value)
{
    switch (value)
    {
    case VisibilityEdgeStorage::None: return "None";
    case VisibilityEdgeStorage::R8Uint:
        return "R8_UINT Packed L/R/T/B";
    case VisibilityEdgeStorage::R8Unorm:
        return "R8_UNORM Packed L/R/T/B";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityReconstruction(
    VisibilityReconstructionMode value)
{
    switch (value)
    {
    case VisibilityReconstructionMode::Legacy:
        return "Legacy Joint Bilateral";
    case VisibilityReconstructionMode::NearestDiagnostic:
        return "Nearest Diagnostic";
    case VisibilityReconstructionMode::BilinearDiagnostic:
        return "Bilinear Diagnostic";
    case VisibilityReconstructionMode::PackedEdges2x2:
        return "Packed Edge 2x2";
    case VisibilityReconstructionMode::PackedEdges4x4:
        return "Packed Edge 4x4";
    case VisibilityReconstructionMode::ActivisionBilateral4x4:
        return "Activision Bilateral 4x4";
    case VisibilityReconstructionMode::XeGtaoDenoise:
        return "XeGTAO Denoise";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityTemporal(VisibilityTemporalMode value)
{
    switch (value)
    {
    case VisibilityTemporalMode::Legacy: return "Legacy Temporal";
    case VisibilityTemporalMode::CopyDiagnostic:
        return "Temporal Copy Diagnostic";
    case VisibilityTemporalMode::ActivisionSixDirectionEma:
        return "Activision Six-Phase EMA";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityApplication(
    VisibilityApplicationMode value)
{
    switch (value)
    {
    case VisibilityApplicationMode::LegacySeparateComposition:
        return "Separate Filter + Composite";
    case VisibilityApplicationMode::FusedResolveAndApplyExact:
        return "Fused Resolve + Apply";
    case VisibilityApplicationMode::FusedResolveAndApplyPackedEdges:
        return "Fused Packed Edge Resolve + Apply";
    case VisibilityApplicationMode::IsolatedCompositionDiagnostic:
        return "Isolated Composition Diagnostic";
    case VisibilityApplicationMode::BypassCompositionDiagnostic:
        return "Composition Bypass Diagnostic";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityDepth(VisibilityDepthMode value)
{
    switch (value)
    {
    case VisibilityDepthMode::Legacy: return "Direct Device Depth";
    case VisibilityDepthMode::ActivisionClampedScreenRadius:
        return "Activision Packed Closest-Depth Guide + 64 px Clamp";
    case VisibilityDepthMode::XeGtaoPrefilteredMips:
        return "XeGTAO Prefiltered Mips";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityBindings(
    VisibilityBindingStrategy value)
{
    switch (value)
    {
    case VisibilityBindingStrategy::LegacyBroad:
        return "Reference Broad Bindings";
    case VisibilityBindingStrategy::MinimalConditional:
        return "Minimal Consumer-Driven Bindings";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilitySpatialFilter(
    VisibilitySpatialFilter value)
{
    switch (value)
    {
    case VisibilitySpatialFilter::JointBilateral:
        return "Compact Joint Bilateral";
    case VisibilitySpatialFilter::GaussianJointBilateral:
        return "Gaussian Joint Bilateral";
    default: return "Unknown";
    }
}

static uint64_t HashVisibilityBenchmarkRunSettings(std::string_view value)
{
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : value)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

static std::string FormatVisibilityBenchmarkHash(uint64_t hash)
{
    std::ostringstream formatted;
    formatted << "0x" << std::uppercase << std::hex << std::setfill('0')
        << std::setw(16) << static_cast<unsigned long long>(hash);
    return formatted.str();
}

static VisibilityBenchmarkRunSettings BuildVisibilityBenchmarkRunSettings(
    const ScreenSpaceVisibilitySettings& visibility,
    const VisibilityPerformanceWorkload& workload,
    const VisibilityPerformanceProfileConfiguration& configuration,
    const VisibilityExecutionPlan& plan,
    const ScreenSpaceVisibilityTimings& timings)
{
    VisibilityBenchmarkRunSettings result;
    const bool activisionPs4Pipeline =
        configuration.depth ==
            VisibilityDepthMode::ActivisionClampedScreenRadius &&
        configuration.reconstruction ==
            VisibilityReconstructionMode::ActivisionBilateral4x4;
    result.implementationProfile.assign(configuration.name);
    result.optimizationClass = DescribeVisibilityOptimizationClass(
        configuration.optimizationClass);
    result.consumer = DescribeVisibilityConsumer(workload);
    result.visibilityEnabled = visibility.enabled;
    result.ambientOcclusionEnabled = visibility.ambientOcclusion.enabled;
    result.ambientOcclusionStrength = visibility.ambientOcclusion.strength;
    result.indirectDiffuseEnabled = visibility.indirectDiffuse.enabled;
    result.indirectDiffuseBounceCount =
        visibility.indirectDiffuse.bounceCount;
    result.indirectDiffuseMinimumBounceContribution =
        visibility.indirectDiffuse.minimumBounceContribution;
    result.indirectDiffuseIntensity = visibility.indirectDiffuse.intensity;
    result.indirectDiffuseIncludeEmissive =
        visibility.indirectDiffuse.includeEmissive;
    result.indirectDiffuseEmissiveGain =
        visibility.indirectDiffuse.emissiveGain;
    result.showIndirectDiffuseOnly = visibility.showIndirectDiffuseOnly;
    result.outputWidth = workload.outputWidth;
    result.outputHeight = workload.outputHeight;
    result.traceResolution = DescribeVisibilityResolution(workload.resolution);
    uint32_t traceDivisor = 1u;
    if (workload.resolution == VisibilityPerformanceResolution::Half)
        traceDivisor = 2u;
    else if (workload.resolution == VisibilityPerformanceResolution::Quarter)
        traceDivisor = 4u;
    result.traceWidth = (workload.outputWidth + traceDivisor - 1u) /
        traceDivisor;
    result.traceHeight = (workload.outputHeight + traceDivisor - 1u) /
        traceDivisor;
    result.estimator = DescribeVisibilityEstimator(workload.estimator);
    result.minimumSampleCount = visibility.sampling.minimumSampleCount;
    result.maximumSampleCount = visibility.sampling.maximumSampleCount;
    result.adaptiveSamplingRequested =
        visibility.sampling.adaptiveSparseSamplingEnabled;
    result.adaptiveSamplingActive = visibility.UsesAdaptiveSampling();
    result.adaptiveStrength = visibility.sampling.adaptiveStrength;
    result.radius = visibility.sampling.radius;
    result.thickness = visibility.sampling.thickness;
    result.thicknessActive = !activisionPs4Pipeline;
    result.radialDistributionExponent =
        visibility.sampling.stepDistributionExponent;
    result.scheduler = DescribeVisibilityScheduler(workload.scheduler);
    result.noiseDelivery = DescribeVisibilityNoiseDelivery(
        configuration.noise);
    result.traceImplementation = DescribeVisibilityTrace(configuration.trace);
    result.firstBounceSpecialization =
        DescribeVisibilitySampleSpecialization(
            configuration.firstBounceSamples);
    result.laterBounceSpecialization =
        DescribeVisibilitySampleSpecialization(
            configuration.laterBounceSamples);
    result.mathProfile = DescribeVisibilityMath(configuration.math);
    result.precisionProfile = configuration.math ==
            VisibilityMathMode::XeGtaoMixedPrecision
        ? "Mixed Precision" : "Reference FP32";
    result.temporalEnabled = visibility.reconstruction.temporalEnabled;
    result.temporalMode = DescribeVisibilityTemporal(configuration.temporal);
    result.temporalResponse = visibility.reconstruction.temporalResponse;
    result.temporalAoFormat = result.temporalEnabled
        ? "R16_FLOAT" : "Not Allocated";
    result.temporalDepthFormat = result.temporalEnabled
        ? "R32_FLOAT" : "Not Allocated";
    result.temporalNormalFormat =
        result.temporalEnabled && !activisionPs4Pipeline
        ? "RGBA8_UNORM" : "Not Allocated";
    result.spatialEnabled = visibility.reconstruction.spatialEnabled;
    result.spatialFilter = activisionPs4Pipeline
        ? "Fixed 4x4 Linear Relative-Depth"
        : DescribeVisibilitySpatialFilter(
            visibility.reconstruction.spatialFilter);
    result.spatialRadius = visibility.reconstruction.spatialRadius;
    result.spatialRadiusActive =
        result.spatialEnabled && !activisionPs4Pipeline;
    result.reconstruction = DescribeVisibilityReconstruction(
        configuration.reconstruction);
    result.application = DescribeVisibilityApplication(
        configuration.application);
    result.threadGroupSizeX = workload.threadGroupSizeX;
    result.threadGroupSizeY = workload.threadGroupSizeY;
    result.rawAoFormat = DescribeVisibilityRawAoFormat(
        configuration.rawAoStorage);
    result.edgeMetadataFormat = DescribeVisibilityEdgeFormat(
        configuration.edgeStorage);
    result.finalResolvedAoFormat =
        configuration.application ==
                VisibilityApplicationMode::FusedResolveAndApplyExact ||
            configuration.application ==
                VisibilityApplicationMode::FusedResolveAndApplyPackedEdges
        ? "No Intermediate" : "R16_FLOAT";
    result.depthMode = DescribeVisibilityDepth(configuration.depth);
    result.bindingStrategy = DescribeVisibilityBindings(
        configuration.bindings);
    result.plannedDispatchCount = plan.dispatchCount;
    result.firstTraceSrvCount = plan.firstTraceSrvCount;
    result.firstTraceUavCount = plan.firstTraceUavCount;
    result.peakSrvCount = timings.peakSrvCount;
    result.peakUavCount = timings.peakUavCount;
    result.optionalTextureBytes = timings.optionalTextureBytes;
    result.fullResolutionIntermediateBytes =
        timings.fullResolutionIntermediateBytes;
    result.logicalTrafficAvoidedBytes = timings.logicalTrafficAvoidedBytes;

    std::ostringstream canonical;
    canonical << std::boolalpha
        << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "implementation_profile=" << result.implementationProfile
        << ";optimization_class=" << result.optimizationClass
        << ";consumer=" << result.consumer
        << ";visibility_enabled=" << result.visibilityEnabled
        << ";ao_enabled=" << result.ambientOcclusionEnabled
        << ";ao_strength=" << result.ambientOcclusionStrength
        << ";gi_enabled=" << result.indirectDiffuseEnabled
        << ";gi_bounces=" << result.indirectDiffuseBounceCount
        << ";gi_minimum_bounce_contribution="
        << result.indirectDiffuseMinimumBounceContribution
        << ";gi_intensity=" << result.indirectDiffuseIntensity
        << ";gi_include_emissive="
        << result.indirectDiffuseIncludeEmissive
        << ";gi_emissive_gain=" << result.indirectDiffuseEmissiveGain
        << ";show_indirect_diffuse_only="
        << result.showIndirectDiffuseOnly
        << ";output=" << result.outputWidth << 'x' << result.outputHeight
        << ";trace_resolution=" << result.traceResolution
        << ";trace_size=" << result.traceWidth << 'x' << result.traceHeight
        << ";estimator=" << result.estimator
        << ";minimum_samples=" << result.minimumSampleCount
        << ";maximum_samples=" << result.maximumSampleCount
        << ";adaptive_requested=" << result.adaptiveSamplingRequested
        << ";adaptive_active=" << result.adaptiveSamplingActive
        << ";adaptive_strength=" << result.adaptiveStrength
        << ";radius=" << result.radius
        << ";thickness=" << result.thickness
        << ";thickness_active=" << result.thicknessActive
        << ";radial_exponent=" << result.radialDistributionExponent
        << ";scheduler=" << result.scheduler
        << ";noise_delivery=" << result.noiseDelivery
        << ";trace=" << result.traceImplementation
        << ";first_specialization=" << result.firstBounceSpecialization
        << ";later_specialization=" << result.laterBounceSpecialization
        << ";math=" << result.mathProfile
        << ";precision=" << result.precisionProfile
        << ";temporal_enabled=" << result.temporalEnabled
        << ";temporal_mode=" << result.temporalMode
        << ";temporal_response=" << result.temporalResponse
        << ";temporal_ao_format=" << result.temporalAoFormat
        << ";temporal_depth_format=" << result.temporalDepthFormat
        << ";temporal_normal_format=" << result.temporalNormalFormat
        << ";spatial_enabled=" << result.spatialEnabled
        << ";spatial_filter=" << result.spatialFilter
        << ";spatial_radius=" << result.spatialRadius
        << ";spatial_radius_active=" << result.spatialRadiusActive
        << ";reconstruction=" << result.reconstruction
        << ";application=" << result.application
        << ";thread_group=" << result.threadGroupSizeX << 'x'
        << result.threadGroupSizeY
        << ";raw_ao_format=" << result.rawAoFormat
        << ";edge_metadata_format=" << result.edgeMetadataFormat
        << ";final_resolved_ao_format=" << result.finalResolvedAoFormat
        << ";depth_mode=" << result.depthMode
        << ";binding_strategy=" << result.bindingStrategy;
    result.canonical = canonical.str();
    result.hash = HashVisibilityBenchmarkRunSettings(result.canonical);
    return result;
}

static std::string FindVisibilityVerificationSettingsMismatch(
    VisibilityVerificationProfile profile,
    const ScreenSpaceVisibilitySettings& observed,
    const VisibilityPerformanceWorkload& observedWorkload)
{
    const VisibilityVerificationProfileDefinition definition =
        GetVisibilityVerificationProfileDefinition(profile);
    ScreenSpaceVisibilitySettings expected;
    if (!ApplyVisibilityVerificationProfileDefaults(expected, profile))
        return "The selected verification profile has no applicable defaults.";

    const auto mismatch = [](bool condition, const char* reason)
        -> std::string
        {
            return condition ? std::string(reason) : std::string();
        };
    std::string reason;
    if (!(reason = mismatch(observed.enabled != expected.enabled,
            "Visibility enabled state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(observed.quality != expected.quality,
            "Quality mode does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(observed.estimator != expected.estimator,
            "Estimator does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(observed.resolution != expected.resolution,
            "Trace resolution does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.sampling.minimumSampleCount !=
                expected.sampling.minimumSampleCount,
            "Minimum sample count does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.sampling.maximumSampleCount !=
                expected.sampling.maximumSampleCount,
            "Maximum sample count does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.sampling.adaptiveSparseSamplingEnabled !=
                expected.sampling.adaptiveSparseSamplingEnabled,
            "Adaptive sampling state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.sampling.adaptiveStrength !=
                expected.sampling.adaptiveStrength,
            "Adaptive strength does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(observed.sampling.radius !=
                expected.sampling.radius,
            "Radius does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(observed.sampling.thickness !=
                expected.sampling.thickness,
            "Thickness does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.sampling.stepDistributionExponent !=
                expected.sampling.stepDistributionExponent,
            "Radial exponent does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(observed.sampling.scheduler !=
                expected.sampling.scheduler,
            "User scheduler state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.ambientOcclusion.enabled !=
                expected.ambientOcclusion.enabled,
            "AO enabled state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.ambientOcclusion.strength !=
                expected.ambientOcclusion.strength,
            "AO strength does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.indirectDiffuse.enabled !=
                expected.indirectDiffuse.enabled,
            "GI enabled state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.indirectDiffuse.bounceCount !=
                expected.indirectDiffuse.bounceCount,
            "GI bounce count does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.indirectDiffuse.minimumBounceContribution !=
                expected.indirectDiffuse.minimumBounceContribution,
            "GI contribution cutoff does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.indirectDiffuse.intensity !=
                expected.indirectDiffuse.intensity,
            "GI intensity does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.indirectDiffuse.includeEmissive !=
                expected.indirectDiffuse.includeEmissive,
            "GI emissive-source state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.indirectDiffuse.emissiveGain !=
                expected.indirectDiffuse.emissiveGain,
            "GI emissive gain does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.reconstruction.temporalEnabled !=
                expected.reconstruction.temporalEnabled,
            "Temporal enabled state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.reconstruction.temporalResponse !=
                expected.reconstruction.temporalResponse,
            "Temporal response does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.reconstruction.spatialEnabled !=
                expected.reconstruction.spatialEnabled,
            "Spatial-filter enabled state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.reconstruction.spatialFilter !=
                expected.reconstruction.spatialFilter,
            "Spatial-filter type does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.reconstruction.spatialRadius !=
                expected.reconstruction.spatialRadius,
            "Spatial-filter radius does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.showIndirectDiffuseOnly !=
                expected.showIndirectDiffuseOnly,
            "Indirect-only display state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.performanceProfile != definition.implementationProfile,
            "Implementation profile does not match the verification profile."
            )).empty())
        return reason;

    const VisibilityPerformanceProfileConfiguration expectedConfiguration =
        GetVisibilityPerformanceProfileConfiguration(
            definition.implementationProfile);
    const VisibilityPerformanceProfileConfiguration observedConfiguration =
        GetVisibilityPerformanceProfileConfiguration(
            observed.performanceProfile);
    if (!(reason = mismatch(
            observedConfiguration.trace != expectedConfiguration.trace ||
            observedConfiguration.firstBounceSamples !=
                expectedConfiguration.firstBounceSamples ||
            observedConfiguration.laterBounceSamples !=
                expectedConfiguration.laterBounceSamples,
            "Trace or sample specialization does not match the profile."
            )).empty())
        return reason;
    if (!(reason = mismatch(
            observedConfiguration.noise != expectedConfiguration.noise,
            "Noise delivery does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observedConfiguration.math != expectedConfiguration.math,
            "Math or precision profile does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observedConfiguration.temporal != expectedConfiguration.temporal,
            "Temporal implementation mode does not match the profile."
            )).empty())
        return reason;
    if (!(reason = mismatch(
            observedConfiguration.reconstruction !=
                expectedConfiguration.reconstruction,
            "Reconstruction implementation does not match the profile."
            )).empty())
        return reason;
    if (!(reason = mismatch(
            observedConfiguration.application !=
                expectedConfiguration.application,
            "Application mode does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observedConfiguration.rawAoStorage !=
                expectedConfiguration.rawAoStorage ||
            observedConfiguration.edgeStorage !=
                expectedConfiguration.edgeStorage,
            "Resource formats do not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observedWorkload.threadGroupSizeX !=
                definition.expectedWorkload.threadGroupSizeX ||
            observedWorkload.threadGroupSizeY !=
                definition.expectedWorkload.threadGroupSizeY,
            "Thread-group shape does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observedWorkload.outputWidth !=
                definition.expectedWorkload.outputWidth ||
            observedWorkload.outputHeight !=
                definition.expectedWorkload.outputHeight,
            "GPU output size does not match the profile.")).empty())
        return reason;
    return {};
}

static void WriteJsonString(std::ostream& output, std::string_view value)
{
    output.put('"');
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20u)
            {
                output << "\\u00" << std::hex << std::setw(2)
                    << std::setfill('0') << unsigned(character)
                    << std::dec << std::setfill(' ');
            }
            else
            {
                output.put(char(character));
            }
            break;
        }
    }
    output.put('"');
}

static void WriteCsvString(std::ostream& output, std::string_view value)
{
    output.put('"');
    for (const char character : value)
    {
        if (character == '"')
            output.put('"');
        output.put(character);
    }
    output.put('"');
}

static void WriteVisibilityBenchmarkRunSettingsJson(
    std::ostream& output,
    const VisibilityBenchmarkRunSettings& settings)
{
    output << "{\n    \"canonical\": ";
    WriteJsonString(output, settings.canonical);
    output << ",\n    \"implementation\": {\"profile\":";
    WriteJsonString(output, settings.implementationProfile);
    output << ",\"optimization_class\":";
    WriteJsonString(output, settings.optimizationClass);
    output << ",\"trace\":";
    WriteJsonString(output, settings.traceImplementation);
    output << ",\"first_bounce_specialization\":";
    WriteJsonString(output, settings.firstBounceSpecialization);
    output << ",\"later_bounce_specialization\":";
    WriteJsonString(output, settings.laterBounceSpecialization);
    output << ",\"noise_delivery\":";
    WriteJsonString(output, settings.noiseDelivery);
    output << ",\"math_profile\":";
    WriteJsonString(output, settings.mathProfile);
    output << ",\"precision_profile\":";
    WriteJsonString(output, settings.precisionProfile);
    output << ",\"reconstruction\":";
    WriteJsonString(output, settings.reconstruction);
    output << ",\"temporal_mode\":";
    WriteJsonString(output, settings.temporalMode);
    output << ",\"application\":";
    WriteJsonString(output, settings.application);
    output << ",\"depth_mode\":";
    WriteJsonString(output, settings.depthMode);
    output << ",\"binding_strategy\":";
    WriteJsonString(output, settings.bindingStrategy);
    output << "},\n    \"output\": {\"width\":"
        << settings.outputWidth << ",\"height\":" << settings.outputHeight
        << ",\"trace_resolution\":";
    WriteJsonString(output, settings.traceResolution);
    output << ",\"trace_width\":" << settings.traceWidth
        << ",\"trace_height\":" << settings.traceHeight << "},\n"
        << "    \"consumer\": {\"mode\":";
    WriteJsonString(output, settings.consumer);
    output << ",\"visibility_enabled\":"
        << (settings.visibilityEnabled ? "true" : "false")
        << ",\"ao_enabled\":"
        << (settings.ambientOcclusionEnabled ? "true" : "false")
        << ",\"ao_strength\":" << settings.ambientOcclusionStrength
        << ",\"gi_enabled\":"
        << (settings.indirectDiffuseEnabled ? "true" : "false")
        << ",\"gi_bounce_count\":"
        << settings.indirectDiffuseBounceCount
        << ",\"gi_minimum_bounce_contribution\":"
        << settings.indirectDiffuseMinimumBounceContribution
        << ",\"gi_intensity\":" << settings.indirectDiffuseIntensity
        << ",\"gi_include_emissive\":"
        << (settings.indirectDiffuseIncludeEmissive ? "true" : "false")
        << ",\"gi_emissive_gain\":"
        << settings.indirectDiffuseEmissiveGain
        << ",\"show_indirect_diffuse_only\":"
        << (settings.showIndirectDiffuseOnly ? "true" : "false")
        << "},\n    \"sampling\": {\"estimator\":";
    WriteJsonString(output, settings.estimator);
    output << ",\"minimum_sample_count\":" << settings.minimumSampleCount
        << ",\"maximum_sample_count\":" << settings.maximumSampleCount
        << ",\"adaptive_requested\":"
        << (settings.adaptiveSamplingRequested ? "true" : "false")
        << ",\"adaptive_active\":"
        << (settings.adaptiveSamplingActive ? "true" : "false")
        << ",\"adaptive_strength\":" << settings.adaptiveStrength
        << ",\"radius\":" << settings.radius
        << ",\"thickness\":" << settings.thickness
        << ",\"thickness_active\":"
        << (settings.thicknessActive ? "true" : "false")
        << ",\"radial_distribution_exponent\":"
        << settings.radialDistributionExponent << ",\"scheduler\":";
    WriteJsonString(output, settings.scheduler);
    output << "},\n    \"reconstruction_settings\": {\"temporal_enabled\":"
        << (settings.temporalEnabled ? "true" : "false")
        << ",\"temporal_response\":" << settings.temporalResponse
        << ",\"spatial_enabled\":"
        << (settings.spatialEnabled ? "true" : "false")
        << ",\"spatial_filter\":";
    WriteJsonString(output, settings.spatialFilter);
    output << ",\"spatial_radius\":" << settings.spatialRadius
        << ",\"spatial_radius_active\":"
        << (settings.spatialRadiusActive ? "true" : "false")
        << "},\n    \"dispatch\": {\"thread_group_x\":"
        << settings.threadGroupSizeX << ",\"thread_group_y\":"
        << settings.threadGroupSizeY << "},\n"
        << "    \"formats\": {\"raw_ao\":";
    WriteJsonString(output, settings.rawAoFormat);
    output << ",\"temporal_ao\":";
    WriteJsonString(output, settings.temporalAoFormat);
    output << ",\"temporal_depth\":";
    WriteJsonString(output, settings.temporalDepthFormat);
    output << ",\"temporal_normal\":";
    WriteJsonString(output, settings.temporalNormalFormat);
    output << ",\"edge_metadata\":";
    WriteJsonString(output, settings.edgeMetadataFormat);
    output << ",\"final_resolved_ao\":";
    WriteJsonString(output, settings.finalResolvedAoFormat);
    output << "},\n    \"performance_contract\": {\"planned_dispatch_count\":"
        << settings.plannedDispatchCount
        << ",\"first_trace_srv_count\":" << settings.firstTraceSrvCount
        << ",\"first_trace_uav_count\":" << settings.firstTraceUavCount
        << ",\"peak_srv_count\":" << settings.peakSrvCount
        << ",\"peak_uav_count\":" << settings.peakUavCount
        << ",\"optional_texture_bytes\":"
        << settings.optionalTextureBytes
        << ",\"full_resolution_intermediate_bytes\":"
        << settings.fullResolutionIntermediateBytes
        << ",\"logical_traffic_avoided_bytes\":"
        << settings.logicalTrafficAvoidedBytes << "}\n  }";
}

static std::string MakeFileNameToken(
    std::string_view value,
    size_t maximumTokenLength = 48u)
{
    // The full profile name is preserved in JSON. Keep only a concise display
    // token in the artifact filename so ordinary Windows MAX_PATH setups still
    // have room for the output directory, two collision-resistant hashes, the
    // timestamp, extension, and an optional collision suffix.
    maximumTokenLength = std::max(maximumTokenLength, size_t(1u));
    std::string token;
    token.reserve(std::min(value.size(), maximumTokenLength));
    bool previousDash = false;
    for (const unsigned char character : value)
    {
        if (token.size() >= maximumTokenLength)
            break;
        if (std::isalnum(character))
        {
            token.push_back(char(std::tolower(character)));
            previousDash = false;
        }
        else if (!previousDash && !token.empty())
        {
            token.push_back('-');
            previousDash = true;
        }
    }
    while (!token.empty() && token.back() == '-')
        token.pop_back();
    if (!token.empty())
        return token;
    return std::string("profile").substr(0u, maximumTokenLength);
}

static size_t GetVisibilityArtifactProfileTokenBudget(
    const std::filesystem::path& directory,
    std::string_view prefix,
    std::string_view permutationToken,
    std::string_view settingsToken,
    std::string_view timestamp,
    std::string_view extension)
{
    constexpr size_t DefaultMaximumTokenLength = 48u;
#ifdef _WIN32
    // Keep enough headroom below legacy MAX_PATH for a trailing NUL, the
    // largest collision suffix ("-9999"), and library-internal path handling.
    // The full profile name remains in JSON, so shortening only the display
    // token is lossless and lets descriptive output folders remain usable.
    constexpr size_t ConservativeMaximumPathLength = 240u;
    constexpr size_t CollisionSuffixReserve = 5u;
    const size_t fixedLength = directory.native().size() + 1u +
        prefix.size() + 3u + permutationToken.size() +
        settingsToken.size() + timestamp.size() + extension.size() +
        CollisionSuffixReserve;
    if (fixedLength >= ConservativeMaximumPathLength)
        return 1u;
    return std::clamp(
        ConservativeMaximumPathLength - fixedLength,
        size_t(1u), DefaultMaximumTokenLength);
#else
    (void)directory;
    (void)prefix;
    (void)permutationToken;
    (void)settingsToken;
    (void)timestamp;
    (void)extension;
    return DefaultMaximumTokenLength;
#endif
}

static std::string FormatVisibilityBenchmarkTimestamp(
    const std::chrono::system_clock::time_point& time)
{
    const std::time_t timestamp =
        std::chrono::system_clock::to_time_t(time);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif
    const auto milliseconds = std::chrono::duration_cast<
        std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;
    std::ostringstream formatted;
    formatted << std::put_time(&localTime, "%Y%m%d-%H%M%S") << '-'
        << std::setfill('0') << std::setw(3) << milliseconds;
    return formatted.str();
}

static void WriteVisibilityDistributionJson(
    std::ostream& output,
    const VisibilityBenchmarkDistributionSummary& distribution)
{
    output << "{\"valid\":" << (distribution.valid ? "true" : "false")
        << ",\"sample_count\":" << distribution.sampleCount
        << ",\"median_ms\":" << distribution.medianMilliseconds
        << ",\"p95_ms\":" << distribution.p95Milliseconds << '}';
}

static bool ResolveVisibilityBenchmarkOutputDirectory(
    const std::filesystem::path& requestedDirectory,
    std::filesystem::path& directory,
    std::string& error)
{
    std::error_code fileError;
    const bool useDefaultDirectory = requestedDirectory.empty();
    if (useDefaultDirectory)
    {
        directory = std::filesystem::current_path(fileError) /
            "visibility-benchmarks";
    }
    else
    {
        directory = requestedDirectory;
    }
    if (!fileError)
    {
        directory = std::filesystem::absolute(directory, fileError);
    }
    if (!fileError)
        std::filesystem::create_directories(directory, fileError);

    if (fileError && useDefaultDirectory)
    {
        // GUI launches can inherit an installed application's protected
        // working directory. Keep an explicit CLI/UI directory authoritative,
        // but give the no-argument UI action a per-user writable fallback.
        fileError.clear();
        const char* localAppData = std::getenv("LOCALAPPDATA");
        if (localAppData && localAppData[0] != '\0')
        {
            directory = std::filesystem::path(localAppData) / "UVSR" /
                "visibility-benchmarks";
        }
        else
        {
            directory = std::filesystem::temp_directory_path(fileError) /
                "UVSR" / "visibility-benchmarks";
        }
        if (!fileError)
            directory = std::filesystem::absolute(directory, fileError);
        if (!fileError)
            std::filesystem::create_directories(directory, fileError);
    }

    if (!fileError)
        return true;

    error = "Cannot create benchmark output directory '" +
        directory.generic_string() + "': " + fileError.message();
    return false;
}

static bool AllocateVisibilityBenchmarkMeasuredFramePath(
    const VisibilityBenchmarkRunMetadata& metadata,
    uint64_t runSettingsHash,
    const std::filesystem::path& requestedDirectory,
    std::filesystem::path& path,
    std::string& error)
{
    std::filesystem::path directory;
    if (!ResolveVisibilityBenchmarkOutputDirectory(
            requestedDirectory, directory, error))
    {
        return false;
    }

    const std::string_view permutationMetadata = metadata.permutationKey;
    const std::string permutationToken = MakeFileNameToken(
        permutationMetadata.substr(0, permutationMetadata.find(':')));
    const std::string settingsToken = MakeFileNameToken(
        FormatVisibilityBenchmarkHash(runSettingsHash));
    const std::string timestamp = FormatVisibilityBenchmarkTimestamp(
        std::chrono::system_clock::now());
    const std::string profileToken = MakeFileNameToken(
        metadata.profileName,
        GetVisibilityArtifactProfileTokenBudget(
            directory, ".vframe-", permutationToken, settingsToken,
            timestamp, ".bmp"));
    std::error_code fileError;
    for (uint32_t suffix = 0u; suffix < 10000u; ++suffix)
    {
        // Keep the temporary prefix shorter than the final export prefix so
        // ordinary Windows MAX_PATH configurations do not fail only while the
        // presented frame is staged.
        std::string name = ".vframe-" + profileToken +
            '-' + permutationToken + '-' + settingsToken + '-' + timestamp;
        if (suffix > 0u)
            name += '-' + std::to_string(suffix);
        const std::filesystem::path candidate = directory / (name + ".bmp");
        if (!std::filesystem::exists(candidate, fileError) && !fileError)
        {
            path = candidate;
            return true;
        }
        if (fileError)
        {
            error = "Cannot inspect benchmark output directory: " +
                fileError.message();
            return false;
        }
    }
    error = "Cannot allocate a collision-safe measured-frame filename.";
    return false;
}

static bool ExportVisibilityBenchmark(
    const VisibilityBenchmarkSummary& summary,
    const VisibilityBenchmarkArtifactMetadata& artifactMetadata,
    const std::filesystem::path& requestedDirectory,
    VisibilityBenchmarkExportPaths& paths,
    std::string& error)
{
    std::error_code fileError;
    std::filesystem::path directory;
    if (!ResolveVisibilityBenchmarkOutputDirectory(
            requestedDirectory, directory, error))
    {
        return false;
    }

    const std::string_view permutationMetadata =
        summary.configuration.metadata.permutationKey;
    const size_t permutationSeparator = permutationMetadata.find(':');
    const std::string permutationToken = MakeFileNameToken(
        permutationMetadata.substr(0, permutationSeparator));
    const std::string settingsToken = MakeFileNameToken(
        FormatVisibilityBenchmarkHash(artifactMetadata.runSettings.hash));
    const std::string timestamp = FormatVisibilityBenchmarkTimestamp(
        std::chrono::system_clock::now());
    const std::string profileToken = MakeFileNameToken(
        summary.configuration.metadata.profileName,
        GetVisibilityArtifactProfileTokenBudget(
            directory, "visibility-", permutationToken, settingsToken,
            timestamp, ".json"));
    std::filesystem::path base;
    for (uint32_t suffix = 0u; suffix < 10000u; ++suffix)
    {
        std::string name = "visibility-" + profileToken + '-' +
            permutationToken + '-' + settingsToken + '-' + timestamp;
        if (suffix > 0u)
            name += '-' + std::to_string(suffix);
        base = directory / name;
        std::filesystem::path jsonCandidate = base;
        std::filesystem::path csvCandidate = base;
        std::filesystem::path frameCandidate = base;
        jsonCandidate += ".json";
        csvCandidate += ".csv";
        frameCandidate += ".bmp";
        if (!std::filesystem::exists(jsonCandidate, fileError) &&
            !std::filesystem::exists(csvCandidate, fileError) &&
            !std::filesystem::exists(frameCandidate, fileError))
        {
            paths.json = jsonCandidate;
            paths.csv = csvCandidate;
            paths.presentedFrame = frameCandidate;
            break;
        }
        if (fileError)
        {
            error = "Cannot inspect benchmark output directory: " +
                fileError.message();
            return false;
        }
    }
    if (paths.json.empty())
    {
        error = "Cannot allocate a collision-safe benchmark filename.";
        return false;
    }

    std::ofstream csv(paths.csv, std::ios::binary | std::ios::trunc);
    if (!csv)
    {
        error = "Cannot create raw benchmark CSV '" +
            paths.csv.generic_string() + "'.";
        return false;
    }
    csv << "frame_id,run_settings_hash,run_settings";
    for (uint32_t stageIndex = 0u;
        stageIndex < static_cast<uint32_t>(VisibilityBenchmarkStage::Count);
        ++stageIndex)
    {
        csv << ',' << VisibilityBenchmarkStageKey(
            static_cast<VisibilityBenchmarkStage>(stageIndex)) << "_ms";
    }
    csv << ",producer_subtotal_ms,summed_stages_ms,"
        "unattributed_residual_signed_ms,complete_effect_ms\n";
    csv << std::setprecision(10);
    for (const VisibilityBenchmarkCompleteFrameSummary& frame :
        summary.completeFrames)
    {
        csv << frame.frameId << ',';
        WriteCsvString(csv, FormatVisibilityBenchmarkHash(
            artifactMetadata.runSettings.hash));
        csv << ',';
        WriteCsvString(csv, artifactMetadata.runSettings.canonical);
        for (double milliseconds : frame.stageMilliseconds)
            csv << ',' << milliseconds;
        csv << ',' << frame.producerSubtotalMilliseconds << ','
            << frame.summedStageMilliseconds << ','
            << frame.unattributedResidualMilliseconds << ','
            << frame.completeEffectMilliseconds << '\n';
    }
    csv.close();
    if (!csv)
    {
        error = "Failed while writing raw benchmark CSV '" +
            paths.csv.generic_string() + "'.";
        std::filesystem::remove(paths.csv, fileError);
        return false;
    }

    std::ofstream json(paths.json, std::ios::binary | std::ios::trunc);
    if (!json)
    {
        error = "Cannot create benchmark JSON '" +
            paths.json.generic_string() + "'.";
        std::filesystem::remove(paths.csv, fileError);
        return false;
    }
    json << std::setprecision(10);
    json << "{\n  \"schema_version\": 2,\n  \"profile_name\": ";
    WriteJsonString(json, summary.configuration.metadata.profileName);
    json << ",\n  \"permutation_key\": ";
    WriteJsonString(json, summary.configuration.metadata.permutationKey);
    json << ",\n  \"adapter_name\": ";
    WriteJsonString(json, summary.configuration.metadata.adapterName);
    json << ",\n  \"clock_state\": ";
    WriteJsonString(json, summary.configuration.metadata.clockState);
    json << ",\n  \"scene_name\": ";
    WriteJsonString(json, artifactMetadata.sceneName);
    json << ",\n  \"camera_preset_id\": ";
    WriteJsonString(json, artifactMetadata.cameraPresetId);
    json << ",\n  \"graphics_api\": ";
    WriteJsonString(json, artifactMetadata.graphicsApi);
    json << ",\n  \"build_identity\": ";
    WriteJsonString(json, artifactMetadata.buildIdentity);
    std::ostringstream shaderKey;
    shaderKey << "0x" << std::uppercase << std::hex << std::setfill('0')
        << std::setw(16)
        << static_cast<unsigned long long>(
            artifactMetadata.shaderPermutationKey);
    json << ",\n  \"shader_permutation_key\": ";
    WriteJsonString(json, shaderKey.str());
    json << ",\n  \"run_settings_hash\": ";
    WriteJsonString(json, FormatVisibilityBenchmarkHash(
        artifactMetadata.runSettings.hash));
    json << ",\n  \"run_settings\": ";
    WriteVisibilityBenchmarkRunSettingsJson(
        json, artifactMetadata.runSettings);
    json << ",\n  \"output_width\": " << artifactMetadata.outputWidth
        << ",\n  \"output_height\": " << artifactMetadata.outputHeight;
    json << ",\n  \"sequence_name\": ";
    WriteJsonString(json, artifactMetadata.sequenceName);
    json << ",\n  \"sequence_entry_name\": ";
    WriteJsonString(json, artifactMetadata.sequenceEntryName);
    json << ",\n  \"sequence_entry_index\": "
        << artifactMetadata.sequenceEntryIndex
        << ",\n  \"sequence_entry_count\": "
        << artifactMetadata.sequenceEntryCount;
    json << ",\n  \"warmup_frames\": "
        << summary.configuration.warmupFrameCount
        << ",\n  \"requested_measured_frames\": "
        << summary.configuration.measuredFrameCount
        << ",\n  \"first_frame_id\": "
        << summary.configuration.firstFrameId
        << ",\n  \"first_measured_frame_id\": "
        << summary.firstMeasuredFrameId
        << ",\n  \"complete_frames\": " << summary.completeFrameCount
        << ",\n  \"incomplete_frames\": " << summary.incompleteFrameCount
        << ",\n  \"observed_incomplete_frames\": "
        << summary.observedIncompleteFrameCount
        << ",\n  \"unobserved_frames\": " << summary.unobservedFrameCount
        << ",\n  \"required_stage_mask\": "
        << summary.configuration.requiredStageMask
        << ",\n  \"summed_stage_mask\": "
        << summary.configuration.summedStageMask
        << ",\n  \"producer_stage_mask\": "
        << summary.configuration.producerStageMask
        << ",\n  \"ingestion\": {\n"
        << "    \"accepted_samples\": "
        << summary.ingestion.acceptedSampleCount << ",\n"
        << "    \"warmup_samples\": "
        << summary.ingestion.warmupSampleCount << ",\n"
        << "    \"outside_window_samples\": "
        << summary.ingestion.outsideRunWindowSampleCount << ",\n"
        << "    \"extraneous_stage_samples\": "
        << summary.ingestion.extraneousStageSampleCount << ",\n"
        << "    \"duplicate_stage_samples\": "
        << summary.ingestion.duplicateStageSampleCount << ",\n"
        << "    \"invalid_stage_samples\": "
        << summary.ingestion.invalidStageSampleCount << ",\n"
        << "    \"invalid_duration_samples\": "
        << summary.ingestion.invalidDurationSampleCount << "\n  },\n"
        << "  \"stages\": {\n";
    for (size_t stageIndex = 0u;
        stageIndex < summary.stages.size(); ++stageIndex)
    {
        const VisibilityBenchmarkStageSummary& stage =
            summary.stages[stageIndex];
        json << "    ";
        WriteJsonString(json, stage.key);
        json << ": {\"required\":"
            << (stage.required ? "true" : "false")
            << ",\"distribution\":";
        WriteVisibilityDistributionJson(json, stage.distribution);
        json << '}' << (stageIndex + 1u == summary.stages.size()
            ? "\n" : ",\n");
    }
    json << "  },\n  \"producer_subtotal\": ";
    WriteVisibilityDistributionJson(json, summary.producerSubtotal);
    json << ",\n  \"summed_stages\": ";
    WriteVisibilityDistributionJson(json, summary.summedStages);
    json << ",\n  \"unattributed_residual_signed\": ";
    WriteVisibilityDistributionJson(json, summary.unattributedResidual);
    json << ",\n  \"complete_effect\": ";
    WriteVisibilityDistributionJson(json, summary.completeEffect);
    const bool controlledProtocol =
        summary.configuration.warmupFrameCount >= 120u &&
        summary.configuration.measuredFrameCount >= 240u;
    json << ",\n  \"run_class\": \""
        << (controlledProtocol ? "controlled" : "smoke") << '\"'
        << ",\n  \"controlled_protocol_valid\": "
        << (controlledProtocol ? "true" : "false");
    json << ",\n  \"raw_frames_csv\": ";
    WriteJsonString(json, paths.csv.filename().generic_string());
    json << ",\n  \"presented_frame_origin\": \"last_measured_frame\""
        << ",\n  \"presented_frame_id\": "
        << (summary.firstMeasuredFrameId +
            summary.configuration.measuredFrameCount - 1u);
    json << ",\n  \"presented_frame\": ";
    WriteJsonString(json, paths.presentedFrame.filename().generic_string());
    json << "\n}\n";
    json.close();
    if (!json)
    {
        error = "Failed while writing benchmark JSON '" +
            paths.json.generic_string() + "'.";
        std::filesystem::remove(paths.csv, fileError);
        std::filesystem::remove(paths.json, fileError);
        return false;
    }
    return true;
}

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
    std::unique_ptr<ScreenSpaceVisibilityPass> m_ScreenSpaceVisibilityPass;
    std::unique_ptr<MaterialIDPass>     m_MaterialIDPass;
    std::unique_ptr<PixelReadbackPass>  m_PixelReadbackPass;
    std::vector<KodakLut>               m_KodakLuts;
    size_t                              m_SelectedKodakLut = 0;

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
    bool                                m_VisibilityBenchmarkQueued = false;
    uint32_t                            m_VisibilityBenchmarkWarmup = 120u;
    uint32_t                            m_VisibilityBenchmarkFrames = 240u;
    uint64_t                            m_VisibilityBenchmarkRenderedFrames = 0u;
    std::filesystem::path               m_VisibilityBenchmarkOutputDirectory;
    bool                                m_VisibilityBenchmarkAutoClose = false;
    bool                                m_VisibilityBenchmarkOwnsCameraLock = false;
    CameraMode                          m_VisibilityBenchmarkPreviousCameraMode =
        CameraMode::ThirdPerson;
    bool                                m_HasVisibilityBenchmarkSummary = false;
    VisibilityBenchmarkSummary          m_LastVisibilityBenchmarkSummary;
    VisibilityBenchmarkExportPaths      m_LastVisibilityBenchmarkPaths;
    VisibilityBenchmarkArtifactMetadata m_ActiveVisibilityBenchmarkArtifact;
    VisibilityBenchmarkArtifactMetadata m_LastVisibilityBenchmarkArtifact;
    std::string                         m_VisibilityBenchmarkStatus;
    std::string                         m_VisibilityBenchmarkError;
    std::string                         m_VisibilityBenchmarkPermutation;
    std::filesystem::path               m_PendingPresentedFrameCapture;
    std::filesystem::path               m_VisibilityBenchmarkMeasuredFramePath;
    bool                                m_VisibilityBenchmarkMeasuredFrameCaptured = false;
    bool                                m_VisibilityBenchmarkSequenceActive = false;
    bool                                m_VisibilityBenchmarkSequenceAutoClose = false;
    VisibilityBenchmarkSequenceKind     m_VisibilityBenchmarkSequenceKind =
        VisibilityBenchmarkSequenceKind::None;
    std::string                         m_VisibilityBenchmarkSequenceName;
    std::vector<VisibilityBenchmarkSequenceEntry>
                                        m_VisibilityBenchmarkSequenceEntries;
    size_t                              m_VisibilityBenchmarkSequenceIndex = 0u;
    ScreenSpaceVisibilitySettings       m_VisibilityBenchmarkSavedSettings;
    VisibilityVerificationProfile       m_VisibilityBenchmarkSavedVerification =
        VisibilityVerificationProfile::Unset;
    bool                                m_VisibilityBenchmarkSavedEnablePbr = true;
    RendererMode                        m_VisibilityBenchmarkSavedRendererMode =
        RendererMode::Deferred;
    bool                                m_SponzaCameraLocationsAvailable = false;
    SponzaCameraLocation                m_SponzaCameraLocation =
        SponzaCameraLocation::SimplifiedApproximation;

    UIData&                             m_ui;

    std::string GetActiveAdapterName() const;
    void UpdateVisibilityBenchmarkAfterRender();
    void FailVisibilityBenchmark(const std::string& message);
    void ReleaseVisibilityBenchmarkCameraLock();
    void DiscardVisibilityBenchmarkMeasuredFrame();
    bool ApplyVisibilityBenchmarkSequenceEntry();
    void RestoreVisibilityBenchmarkSequenceSettings();
    bool AdvanceVisibilityBenchmarkSequence();
    void CaptureCompletedVisibilityBenchmarkFrame(
        nvrhi::ITexture* framebufferTexture);

public:

    UvsrSceneViewer(
        DeviceManager* deviceManager,
        UIData& ui,
        const std::string& sceneName,
        bool benchmarkCameraRequested)
        : Super(deviceManager)
        , m_BindingCache(deviceManager->GetDevice())
        , m_BenchmarkCameraRequested(benchmarkCameraRequested)
        , m_ui(ui)
    {
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
        DiscoverKodakLuts(mediaDir / "luts/kodak");

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

    bool QueueVisibilityBenchmark(
        uint32_t warmupFrameCount,
        uint32_t measuredFrameCount,
        const std::filesystem::path& outputDirectory,
        bool autoClose = false);
    bool QueueVisibilityBenchmarkSequence(
        VisibilityBenchmarkSequenceKind kind,
        uint32_t warmupFrameCount,
        uint32_t measuredFrameCount,
        const std::filesystem::path& outputDirectory,
        bool autoClose = false);
    void CancelVisibilityBenchmark();
    [[nodiscard]] bool IsVisibilityBenchmarkSequenceActive() const
    {
        return m_VisibilityBenchmarkSequenceActive;
    }
    [[nodiscard]] size_t GetVisibilityBenchmarkSequenceIndex() const
    {
        return m_VisibilityBenchmarkSequenceIndex;
    }
    [[nodiscard]] size_t GetVisibilityBenchmarkSequenceCount() const
    {
        return m_VisibilityBenchmarkSequenceEntries.size();
    }
    [[nodiscard]] const std::string&
        GetVisibilityBenchmarkSequenceEntryName() const
    {
        static const std::string Empty;
        return m_VisibilityBenchmarkSequenceIndex <
                m_VisibilityBenchmarkSequenceEntries.size()
            ? m_VisibilityBenchmarkSequenceEntries[
                m_VisibilityBenchmarkSequenceIndex].name
            : Empty;
    }
    [[nodiscard]] bool IsVisibilityBenchmarkQueued() const
    {
        return m_VisibilityBenchmarkQueued;
    }
    [[nodiscard]] bool IsVisibilityBenchmarkActive() const
    {
        return m_ScreenSpaceVisibilityPass &&
            m_ScreenSpaceVisibilityPass->IsBenchmarkActive();
    }
    [[nodiscard]] bool HasVisibilityBenchmarkResults() const
    {
        return m_HasVisibilityBenchmarkSummary;
    }
    [[nodiscard]] uint32_t GetVisibilityBenchmarkCompletedFrameCount() const
    {
        return IsVisibilityBenchmarkActive()
            ? m_ScreenSpaceVisibilityPass->GetBenchmarkSummary()
                .completeFrameCount
            : 0u;
    }
    [[nodiscard]] uint32_t GetVisibilityBenchmarkRequestedFrameCount() const
    {
        return m_VisibilityBenchmarkFrames;
    }
    [[nodiscard]] const std::string& GetVisibilityBenchmarkStatus() const
    {
        return m_VisibilityBenchmarkStatus;
    }
    [[nodiscard]] const std::string& GetVisibilityBenchmarkError() const
    {
        return m_VisibilityBenchmarkError;
    }
    [[nodiscard]] const VisibilityBenchmarkSummary*
        GetLastVisibilityBenchmarkSummary() const
    {
        return m_HasVisibilityBenchmarkSummary
            ? &m_LastVisibilityBenchmarkSummary
            : nullptr;
    }
    [[nodiscard]] const VisibilityBenchmarkExportPaths&
        GetLastVisibilityBenchmarkPaths() const
    {
        return m_LastVisibilityBenchmarkPaths;
    }
    bool ExportLastVisibilityBenchmark(
        const std::filesystem::path& outputDirectory);

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
    }

    void SetSponzaCameraLocation(SponzaCameraLocation location)
    {
        if (!m_SponzaCameraLocationsAvailable || m_BenchmarkCameraActive)
            return;

        if (location == SponzaCameraLocation::Free)
        {
            m_SponzaCameraLocation = location;
            log::info("Camera location is now Free");
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
            log::info("Camera location is now Free");
        }
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

    void DiscoverKodakLuts(const std::filesystem::path& directory)
    {
        m_KodakLuts.clear();
        KodakLut none;
        none.Name = "None";
        m_KodakLuts.push_back(std::move(none));

        if (!std::filesystem::exists(directory))
            return;

        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
        {
            if (!entry.is_regular_file())
                continue;

            std::string extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char c) { return char(std::tolower(c)); });
            if (extension == ".cube")
                paths.push_back(entry.path());
        }

        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths)
        {
            KodakLut lut;
            if (LoadCubeLut(GetDevice(), path, lut))
            {
                log::info("Loaded Kodak LUT: %s (%u^3)", lut.Name.c_str(), lut.Size);
                m_KodakLuts.push_back(std::move(lut));
            }
        }
    }

    const std::vector<KodakLut>& GetKodakLuts() const
    {
        return m_KodakLuts;
    }

    size_t GetSelectedKodakLut() const
    {
        return m_SelectedKodakLut;
    }

    void SetSelectedKodakLut(size_t index)
    {
        if (index >= m_KodakLuts.size() || index == m_SelectedKodakLut)
            return;

        m_SelectedKodakLut = index;
        if (m_AgxToneMappingPass)
            m_AgxToneMappingPass->SetColorLut(index == 0 ? nullptr : &m_KodakLuts[index]);
    }

    void ResetAllRendererSettings()
    {
        // Restore modes through their public setters first so material shader
        // permutations and LUT bindings cannot retain state from the old setup.
        SetWhiteWorldMode(WhiteWorldMode::Off);
        SetSelectedKodakLut(0);

        m_ui.EnablePbr = true;
        m_ui.RenderMode = RendererMode::Deferred;
        m_ui.ScreenSpaceVisibility = ScreenSpaceVisibilitySettings{};
        m_ui.VisibilityVerification =
            VisibilityVerificationProfile::ReferenceAo8T;
        m_ui.AgxToneMappingPreset = AgxPreset::Base;
        m_ui.AgxToneMappingParams = GetAgxPresetParameters(AgxPreset::Base);
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

        planarView->SetViewport(nvrhi::Viewport(renderTargetSize.x, renderTargetSize.y));
        planarView->SetPixelOffset(float2(0.f));

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
        if (m_ScreenSpaceVisibilityPass &&
            m_ScreenSpaceVisibilityPass->IsBenchmarkActive())
        {
            FailVisibilityBenchmark(
                "The renderer recreated visibility passes during the run.");
        }

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
            m_ScreenSpaceVisibilityPass = std::make_unique<ScreenSpaceVisibilityPass>(
                GetDevice(),
                m_ShaderFactory,
                app::GetDirectoryWithExecutable().parent_path() /
                    "media/noise/visibility_filter_adapted_gauss1_ema035_r8.bin");
        }
        else
        {
            m_PbrDeferredLightingPass.reset();
            m_ScreenSpaceVisibilityPass.reset();
            m_DeferredLightingPass = std::make_shared<DeferredLightingPass>(GetDevice(), m_CommonPasses);
            m_DeferredLightingPass->Init(m_ShaderFactory);
        }

        m_SkyPass = std::make_unique<SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ForwardFramebuffer, *m_View);

        m_AgxToneMappingPass = std::make_unique<AgxToneMappingPass>(
            GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer);
        m_AgxToneMappingPass->SetColorLut(
            m_SelectedKodakLut == 0 ? nullptr : &m_KodakLuts[m_SelectedKodakLut]);

    }

    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override
    {
        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->open();
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
    }

    virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        int windowWidth, windowHeight;
        GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
        if ((m_VisibilityBenchmarkQueued || IsVisibilityBenchmarkActive()) &&
            (!m_ui.EnablePbr || !m_ui.UsesDeferredShading() ||
                !m_ui.ScreenSpaceVisibility.HasActiveConsumer()))
        {
            FailVisibilityBenchmark(
                "The visibility consumer was disabled or the renderer left "
                "the deferred PBR path during the benchmark.");
        }
        nvrhi::Viewport windowViewport = nvrhi::Viewport(float(windowWidth), float(windowHeight));
        nvrhi::Viewport renderViewport = windowViewport;

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
            const bool motionVectorsRequired = visibilityResourcesRequired &&
                m_ui.ScreenSpaceVisibility.RequiresMotionVectors();

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
                        std::exp2(m_ui.AgxToneMappingParams.Exposure),
                        uint32_t(GetFrameIndex()));
                    UpdateVisibilityBenchmarkAfterRender();
                }
                else
                {
                    m_ScreenSpaceVisibilityPass->Deactivate();
                    if (m_VisibilityBenchmarkQueued ||
                        IsVisibilityBenchmarkActive())
                    {
                        FailVisibilityBenchmark(
                            "The queued or active profile has no visibility consumer.");
                    }
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

        nvrhi::ITexture* displayTexture = m_RenderTargets->HdrColor;
        if (m_ui.UsesTonemapper())
        {
            m_AgxToneMappingPass->Render(
                m_CommandList, m_ui.AgxToneMappingParams, *m_View,
                m_RenderTargets->HdrColor);

            displayTexture = m_RenderTargets->LdrColor;
        }

        // The tonemapperless renderer intentionally sends forward scene-linear
        // radiance straight to the sRGB swap-chain target. The render-target
        // conversion still applies the display transfer and clamps values to
        // the target's representable range, but AgX, exposure, grading, LUTs,
        // and dithering are all absent from this path.
        m_CommonPasses->BlitTexture(
            m_CommandList, framebuffer, displayTexture, &m_BindingCache);

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        CaptureCompletedVisibilityBenchmarkFrame(framebufferTexture);
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

};

std::string UvsrSceneViewer::GetActiveAdapterName() const
{
    for (const GpuAdapterChoice& adapter : m_ui.GpuAdapterChoices)
    {
        if (adapter.adapterIndex == m_ui.ActiveGpuAdapterIndex)
            return adapter.name;
    }
    return "Unknown Adapter";
}

bool UvsrSceneViewer::ApplyVisibilityBenchmarkSequenceEntry()
{
    if (!m_VisibilityBenchmarkSequenceActive ||
        m_VisibilityBenchmarkSequenceIndex >=
            m_VisibilityBenchmarkSequenceEntries.size())
    {
        return false;
    }

    const VisibilityBenchmarkSequenceEntry& entry =
        m_VisibilityBenchmarkSequenceEntries[
            m_VisibilityBenchmarkSequenceIndex];
    if (entry.useSavedCurrentSettings)
    {
        m_ui.ScreenSpaceVisibility = m_VisibilityBenchmarkSavedSettings;
        m_ui.VisibilityVerification =
            m_VisibilityBenchmarkSavedVerification;
    }
    else
    {
        if (!ApplyVisibilityPerformanceProfileDefaults(
                m_ui.ScreenSpaceVisibility, entry.profile))
        {
            return false;
        }
        m_ui.VisibilityVerification = VisibilityVerificationProfile::Unset;
    }

    if (entry.overrideScheduler)
        m_ui.ScreenSpaceVisibility.sampling.scheduler = entry.scheduler;
    if (entry.overrideSpatialFilter)
    {
        m_ui.ScreenSpaceVisibility.reconstruction.spatialEnabled =
            entry.spatialEnabled;
        m_ui.ScreenSpaceVisibility.reconstruction.spatialFilter =
            entry.spatialFilter;
    }
    if (entry.overrideConsumer)
    {
        m_ui.ScreenSpaceVisibility.ambientOcclusion.enabled =
            entry.ambientOcclusionEnabled;
        m_ui.ScreenSpaceVisibility.indirectDiffuse.enabled =
            entry.indirectDiffuseEnabled;
        m_ui.ScreenSpaceVisibility.indirectDiffuse.bounceCount =
            entry.indirectDiffuseBounceCount;
    }
    if (entry.overrideTemporal)
    {
        m_ui.ScreenSpaceVisibility.reconstruction.temporalEnabled =
            entry.temporalEnabled;
        m_ui.ScreenSpaceVisibility.reconstruction.temporalResponse =
            entry.temporalResponse;
    }

    m_ui.EnablePbr = true;
    m_ui.RenderMode = RendererMode::Deferred;
    if (m_ScreenSpaceVisibilityPass)
        m_ScreenSpaceVisibilityPass->ResetHistory();
    return true;
}

void UvsrSceneViewer::RestoreVisibilityBenchmarkSequenceSettings()
{
    if (!m_VisibilityBenchmarkSequenceActive)
        return;

    m_ui.ScreenSpaceVisibility = m_VisibilityBenchmarkSavedSettings;
    m_ui.VisibilityVerification = m_VisibilityBenchmarkSavedVerification;
    m_ui.EnablePbr = m_VisibilityBenchmarkSavedEnablePbr;
    m_ui.RenderMode = m_VisibilityBenchmarkSavedRendererMode;
    if (m_ScreenSpaceVisibilityPass)
        m_ScreenSpaceVisibilityPass->ResetHistory();

    m_VisibilityBenchmarkSequenceActive = false;
    m_VisibilityBenchmarkSequenceKind =
        VisibilityBenchmarkSequenceKind::None;
    m_VisibilityBenchmarkSequenceName.clear();
    m_VisibilityBenchmarkSequenceEntries.clear();
    m_VisibilityBenchmarkSequenceIndex = 0u;
    m_VisibilityBenchmarkSequenceAutoClose = false;
}

bool UvsrSceneViewer::AdvanceVisibilityBenchmarkSequence()
{
    if (!m_VisibilityBenchmarkSequenceActive)
        return false;

    ++m_VisibilityBenchmarkSequenceIndex;
    if (m_VisibilityBenchmarkSequenceIndex >=
        m_VisibilityBenchmarkSequenceEntries.size())
    {
        const std::string completedName = m_VisibilityBenchmarkSequenceName;
        const bool closeAfterCompletion =
            m_VisibilityBenchmarkSequenceAutoClose;
        RestoreVisibilityBenchmarkSequenceSettings();
        ReleaseVisibilityBenchmarkCameraLock();
        m_VisibilityBenchmarkStatus =
            "Sequence complete: " + completedName + ".";
        m_VisibilityBenchmarkError.clear();
        if (closeAfterCompletion)
        {
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(), GLFW_TRUE);
        }
        return false;
    }

    const std::string entryName =
        m_VisibilityBenchmarkSequenceEntries[
            m_VisibilityBenchmarkSequenceIndex].name;
    if (!ApplyVisibilityBenchmarkSequenceEntry())
    {
        FailVisibilityBenchmark(
            "Cannot apply sequence entry '" + entryName + "'.");
        return false;
    }
    if (!QueueVisibilityBenchmark(
            m_VisibilityBenchmarkWarmup,
            m_VisibilityBenchmarkFrames,
            m_VisibilityBenchmarkOutputDirectory,
            false))
    {
        const std::string queueError = m_VisibilityBenchmarkError;
        FailVisibilityBenchmark(
            "Cannot queue sequence entry '" + entryName + "': " +
            queueError);
        return false;
    }

    m_VisibilityBenchmarkStatus =
        "Sequence " + m_VisibilityBenchmarkSequenceName + ": queued " +
        std::to_string(m_VisibilityBenchmarkSequenceIndex + 1u) + " of " +
        std::to_string(m_VisibilityBenchmarkSequenceEntries.size()) +
        " (" + entryName + ").";
    return true;
}

bool UvsrSceneViewer::QueueVisibilityBenchmarkSequence(
    VisibilityBenchmarkSequenceKind kind,
    uint32_t warmupFrameCount,
    uint32_t measuredFrameCount,
    const std::filesystem::path& outputDirectory,
    bool autoClose)
{
    if (kind == VisibilityBenchmarkSequenceKind::None ||
        m_VisibilityBenchmarkSequenceActive ||
        m_VisibilityBenchmarkQueued || IsVisibilityBenchmarkActive())
    {
        m_VisibilityBenchmarkError =
            "A visibility benchmark or sequence is already active.";
        return false;
    }

    std::vector<VisibilityBenchmarkSequenceEntry> entries;
    std::string sequenceName;
    const auto makeProfileEntry = [](VisibilityPerformanceProfile profile)
    {
        const VisibilityPerformanceProfileConfiguration configuration =
            GetVisibilityPerformanceProfileConfiguration(profile);
        return VisibilityBenchmarkSequenceEntry{
            std::string(configuration.name), profile, false };
    };
    switch (kind)
    {
    case VisibilityBenchmarkSequenceKind::ReferenceVersusCurrent:
        sequenceName = "Reference Versus Current";
        entries = {
            { "Reference", VisibilityPerformanceProfile::Reference, false },
            { "Current Settings Snapshot", VisibilityPerformanceProfile::Unset,
                true }
        };
        break;
    case VisibilityBenchmarkSequenceKind::FixedSample:
        sequenceName = "Fixed Sample Matrix";
        entries = {
            makeProfileEntry(VisibilityPerformanceProfile::Reference),
            makeProfileEntry(VisibilityPerformanceProfile::ExactFixed8),
            makeProfileEntry(VisibilityPerformanceProfile::ExactFixed12),
            makeProfileEntry(VisibilityPerformanceProfile::ExactFixed16),
            makeProfileEntry(VisibilityPerformanceProfile::ExactFixed20)
        };
        break;
    case VisibilityBenchmarkSequenceKind::Noise:
        sequenceName = "Noise Matrix";
        entries = {
            { "Reference Independent Hash",
                VisibilityPerformanceProfile::Reference, false },
            { "Reference Toroidal Blue Noise",
                VisibilityPerformanceProfile::Reference, false },
            { "Current Scalar FAST",
                VisibilityPerformanceProfile::Reference, false },
            { "Packed Current FAST",
                VisibilityPerformanceProfile::ExactPackedCurrentFast, false },
            { "Activision 4x4 x 6",
                VisibilityPerformanceProfile::AlgorithmicActivisionSchedule,
                false },
            { "Constant Scheduler Diagnostic",
                VisibilityPerformanceProfile::DiagnosticConstantScheduler,
                false }
        };
        entries[0].overrideScheduler = true;
        entries[0].scheduler = VisibilitySampleScheduler::IndependentHash;
        entries[1].overrideScheduler = true;
        entries[1].scheduler =
            VisibilitySampleScheduler::ToroidalBlueNoiseRankField;
        entries[2].overrideScheduler = true;
        entries[2].scheduler = VisibilitySampleScheduler::
            FilterAdaptedSpatiotemporalRankField;
        break;
    case VisibilityBenchmarkSequenceKind::Reconstruction:
        sequenceName = "Reconstruction Matrix";
        entries = {
            { "Reference Compact Joint Bilateral",
                VisibilityPerformanceProfile::Reference, false },
            { "Reference Gaussian Joint Bilateral",
                VisibilityPerformanceProfile::Reference, false },
            { "Exact Fused Resolve And Apply",
                VisibilityPerformanceProfile::ExactFusedResolveApply, false },
            { "Packed Depth Edges 2x2",
                VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2,
                false },
            { "Packed Depth Edges 4x4",
                VisibilityPerformanceProfile::AlgorithmicPackedEdges4x4,
                false },
            { "Packed Depth And Normal Edges 2x2",
                VisibilityPerformanceProfile::
                    AlgorithmicPackedEdgesDepthNormal2x2, false },
            { "Packed Slope-Adjusted Edges 2x2",
                VisibilityPerformanceProfile::AlgorithmicPackedEdgesSlope2x2,
                false },
            { "Packed Controlled-Leakage Edges 2x2",
                VisibilityPerformanceProfile::AlgorithmicPackedEdgesLeakage2x2,
                false },
            { "Fused Packed Edges 2x2",
                VisibilityPerformanceProfile::AlgorithmicFusedPackedEdges2x2,
                false },
            { "Fused Packed Edges 4x4",
                VisibilityPerformanceProfile::AlgorithmicFusedPackedEdges4x4,
                false },
            { "Nearest Diagnostic",
                VisibilityPerformanceProfile::DiagnosticNearestResolve, false },
            { "Bilinear Diagnostic",
                VisibilityPerformanceProfile::DiagnosticBilinearResolve,
                false }
        };
        entries[1].overrideSpatialFilter = true;
        entries[1].spatialEnabled = true;
        entries[1].spatialFilter =
            VisibilitySpatialFilter::GaussianJointBilateral;
        break;
    case VisibilityBenchmarkSequenceKind::Math:
        sequenceName = "Math Matrix";
        entries = {
            makeProfileEntry(VisibilityPerformanceProfile::Reference),
            makeProfileEntry(
                VisibilityPerformanceProfile::ConservativeNumerical)
        };
        break;
    case VisibilityBenchmarkSequenceKind::AllImplemented:
        sequenceName = "All Implemented Profiles";
        for (uint32_t profileIndex =
                static_cast<uint32_t>(VisibilityPerformanceProfile::Reference);
            profileIndex < static_cast<uint32_t>(
                VisibilityPerformanceProfile::Count);
            ++profileIndex)
        {
            const auto profile =
                static_cast<VisibilityPerformanceProfile>(profileIndex);
            const VisibilityPerformanceProfileConfiguration configuration =
                GetVisibilityPerformanceProfileConfiguration(profile);
            if (configuration.implementationStatus ==
                    VisibilityImplementationStatus::Unavailable ||
                configuration.implementationStatus ==
                    VisibilityImplementationStatus::Unset)
            {
                continue;
            }
            entries.push_back({ std::string(configuration.name), profile,
                false });
        }
        {
            VisibilityBenchmarkSequenceEntry giOnly{
                "Reference / GI Only",
                VisibilityPerformanceProfile::Reference, false };
            giOnly.overrideConsumer = true;
            giOnly.ambientOcclusionEnabled = false;
            giOnly.indirectDiffuseEnabled = true;
            entries.push_back(std::move(giOnly));

            VisibilityBenchmarkSequenceEntry combined{
                "Reference / AO + GI",
                VisibilityPerformanceProfile::Reference, false };
            combined.overrideConsumer = true;
            combined.ambientOcclusionEnabled = true;
            combined.indirectDiffuseEnabled = true;
            entries.push_back(std::move(combined));

            VisibilityBenchmarkSequenceEntry multiBounce{
                "Reference / AO + GI Multi-Bounce",
                VisibilityPerformanceProfile::Reference, false };
            multiBounce.overrideConsumer = true;
            multiBounce.ambientOcclusionEnabled = true;
            multiBounce.indirectDiffuseEnabled = true;
            multiBounce.indirectDiffuseBounceCount = 2u;
            entries.push_back(std::move(multiBounce));
        }
        for (const auto fixedProfile : {
                VisibilityPerformanceProfile::ExactFixed8,
                VisibilityPerformanceProfile::ExactFixed12,
                VisibilityPerformanceProfile::ExactFixed16,
                VisibilityPerformanceProfile::ExactFixed20 })
        {
            const std::string fixedName(
                GetVisibilityPerformanceProfileConfiguration(
                    fixedProfile).name);
            VisibilityBenchmarkSequenceEntry giOnly{
                fixedName + " / GI Only", fixedProfile, false };
            giOnly.overrideConsumer = true;
            giOnly.ambientOcclusionEnabled = false;
            giOnly.indirectDiffuseEnabled = true;
            entries.push_back(std::move(giOnly));

            VisibilityBenchmarkSequenceEntry combined{
                fixedName + " / AO + GI", fixedProfile, false };
            combined.overrideConsumer = true;
            combined.ambientOcclusionEnabled = true;
            combined.indirectDiffuseEnabled = true;
            entries.push_back(std::move(combined));
        }
        {
            VisibilityBenchmarkSequenceEntry packedCombined{
                "Exact Packed Current FAST / AO + GI",
                VisibilityPerformanceProfile::ExactPackedCurrentFast, false };
            packedCombined.overrideConsumer = true;
            packedCombined.ambientOcclusionEnabled = true;
            packedCombined.indirectDiffuseEnabled = true;
            entries.push_back(std::move(packedCombined));
        }
        for (const float response : { 0.1f, 0.35f, 0.8f })
        {
            VisibilityBenchmarkSequenceEntry temporal{
                "Reference Temporal / Response " +
                    std::to_string(response),
                VisibilityPerformanceProfile::Reference, false };
            temporal.overrideTemporal = true;
            temporal.temporalEnabled = true;
            temporal.temporalResponse = response;
            entries.push_back(std::move(temporal));
        }
        break;
    case VisibilityBenchmarkSequenceKind::NewCandidates:
        sequenceName = "New AO Optimization Candidates";
        entries = {
            makeProfileEntry(VisibilityPerformanceProfile::Reference),
            makeProfileEntry(VisibilityPerformanceProfile::ExactFixed8),
            makeProfileEntry(
                VisibilityPerformanceProfile::ExactFusedResolveApply),
            makeProfileEntry(
                VisibilityPerformanceProfile::ExactFixed8FusedResolveApply),
            makeProfileEntry(
                VisibilityPerformanceProfile::ActivisionPs4Schedule),
            makeProfileEntry(
                VisibilityPerformanceProfile::ActivisionPs4PackedGather),
            makeProfileEntry(
                VisibilityPerformanceProfile::XeGtaoClosestMatch),
            makeProfileEntry(
                VisibilityPerformanceProfile::XeGtaoHighInlineHilbert),
            makeProfileEntry(
                VisibilityPerformanceProfile::XeGtaoHighFp32)
        };
        break;
    case VisibilityBenchmarkSequenceKind::Precision:
        sequenceName = "XeGTAO Precision Matrix";
        entries = {
            makeProfileEntry(
                VisibilityPerformanceProfile::XeGtaoClosestMatch),
            makeProfileEntry(
                VisibilityPerformanceProfile::XeGtaoHighFp32)
        };
        break;
    default:
        return false;
    }

    m_VisibilityBenchmarkSavedSettings = m_ui.ScreenSpaceVisibility;
    m_VisibilityBenchmarkSavedVerification = m_ui.VisibilityVerification;
    m_VisibilityBenchmarkSavedEnablePbr = m_ui.EnablePbr;
    m_VisibilityBenchmarkSavedRendererMode = m_ui.RenderMode;
    m_VisibilityBenchmarkSequenceActive = true;
    m_VisibilityBenchmarkSequenceKind = kind;
    m_VisibilityBenchmarkSequenceName = std::move(sequenceName);
    m_VisibilityBenchmarkSequenceEntries = std::move(entries);
    m_VisibilityBenchmarkSequenceIndex = 0u;
    m_VisibilityBenchmarkSequenceAutoClose = autoClose;

    if (!ApplyVisibilityBenchmarkSequenceEntry())
    {
        const std::string entryName =
            m_VisibilityBenchmarkSequenceEntries.front().name;
        RestoreVisibilityBenchmarkSequenceSettings();
        m_VisibilityBenchmarkError =
            "Cannot apply sequence entry '" + entryName + "'.";
        return false;
    }
    if (!QueueVisibilityBenchmark(
            warmupFrameCount,
            measuredFrameCount,
            outputDirectory,
            false))
    {
        const std::string queueError = m_VisibilityBenchmarkError;
        RestoreVisibilityBenchmarkSequenceSettings();
        m_VisibilityBenchmarkError = queueError;
        return false;
    }

    m_VisibilityBenchmarkStatus =
        "Sequence " + m_VisibilityBenchmarkSequenceName + ": queued 1 of " +
        std::to_string(m_VisibilityBenchmarkSequenceEntries.size()) +
        " (" + m_VisibilityBenchmarkSequenceEntries.front().name + ").";
    return true;
}

bool UvsrSceneViewer::QueueVisibilityBenchmark(
    uint32_t warmupFrameCount,
    uint32_t measuredFrameCount,
    const std::filesystem::path& outputDirectory,
    bool autoClose)
{
    if (m_VisibilityBenchmarkQueued || IsVisibilityBenchmarkActive())
    {
        m_VisibilityBenchmarkError =
            "A visibility benchmark is already queued or active.";
        log::warning("%s", m_VisibilityBenchmarkError.c_str());
        return false;
    }
    if (measuredFrameCount == 0u)
    {
        m_VisibilityBenchmarkError =
            "A visibility benchmark requires at least one measured frame.";
        log::warning("%s", m_VisibilityBenchmarkError.c_str());
        return false;
    }
    if (warmupFrameCount > MaxVisibilityBenchmarkWarmupFrames ||
        measuredFrameCount > VisibilityBenchmarkMaximumMeasuredFrameCount)
    {
        m_VisibilityBenchmarkError =
            "Visibility benchmark frame counts exceed the 100000-frame "
            "per-phase safety limit.";
        log::warning("%s", m_VisibilityBenchmarkError.c_str());
        return false;
    }

    if (!m_BenchmarkCameraActive)
    {
        if (!m_SponzaCameraLocationsAvailable)
        {
            if (!m_BenchmarkCameraRequested)
            {
                m_VisibilityBenchmarkError =
                    "Visibility benchmarks require PBR Sponza Decorated or "
                    "PBR Sponza Plain so Benchmark Position 1 can be locked.";
                log::warning("%s", m_VisibilityBenchmarkError.c_str());
                return false;
            }
        }
        else
        {
            m_VisibilityBenchmarkOwnsCameraLock = true;
            m_VisibilityBenchmarkPreviousCameraMode = m_ui.Camera;
            ApplySponzaCameraPreset(GetDefaultSponzaCameraPreset());
            m_SponzaCameraLocation =
                SponzaCameraLocation::SimplifiedApproximation;
            m_ui.Camera = CameraMode::Static;
            m_BenchmarkCameraActive = true;

            GLFWwindow* window = GetDeviceManager()->GetWindow();
            glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
            g_BenchmarkForwardKeyCallback = glfwSetKeyCallback(
                window,
                BenchmarkWindowKeyCallback);
        }
    }

    m_VisibilityBenchmarkWarmup = warmupFrameCount;
    m_VisibilityBenchmarkFrames = measuredFrameCount;
    m_VisibilityBenchmarkRenderedFrames = 0u;
    m_VisibilityBenchmarkOutputDirectory = outputDirectory;
    m_VisibilityBenchmarkAutoClose = autoClose;
    DiscardVisibilityBenchmarkMeasuredFrame();
    m_VisibilityBenchmarkQueued = true;
    m_VisibilityBenchmarkError.clear();
    m_VisibilityBenchmarkStatus =
        "Queued; waiting for the next resolved visibility frame.";
    log::info(
        "Queued visibility benchmark (%u warmup, %u measured frames)",
        warmupFrameCount,
        measuredFrameCount);
    return true;
}

void UvsrSceneViewer::DiscardVisibilityBenchmarkMeasuredFrame()
{
    m_PendingPresentedFrameCapture.clear();
    if (!m_VisibilityBenchmarkMeasuredFramePath.empty())
    {
        std::error_code removeError;
        std::filesystem::remove(
            m_VisibilityBenchmarkMeasuredFramePath, removeError);
    }
    m_VisibilityBenchmarkMeasuredFramePath.clear();
    m_VisibilityBenchmarkMeasuredFrameCaptured = false;
}

void UvsrSceneViewer::CancelVisibilityBenchmark()
{
    const bool wasBusy = m_VisibilityBenchmarkQueued ||
        IsVisibilityBenchmarkActive() ||
        m_VisibilityBenchmarkSequenceActive;
    const bool canceledSequence = m_VisibilityBenchmarkSequenceActive;
    const std::string canceledSequenceName =
        m_VisibilityBenchmarkSequenceName;
    m_VisibilityBenchmarkQueued = false;
    if (m_ScreenSpaceVisibilityPass &&
        m_ScreenSpaceVisibilityPass->IsBenchmarkActive())
    {
        m_ScreenSpaceVisibilityPass->CancelBenchmark();
    }
    m_VisibilityBenchmarkAutoClose = false;
    DiscardVisibilityBenchmarkMeasuredFrame();
    m_ActiveVisibilityBenchmarkArtifact = {};
    RestoreVisibilityBenchmarkSequenceSettings();
    ReleaseVisibilityBenchmarkCameraLock();
    if (wasBusy)
    {
        m_VisibilityBenchmarkStatus = canceledSequence
            ? "Sequence canceled: " + canceledSequenceName + "."
            : "Canceled.";
        m_VisibilityBenchmarkError.clear();
        log::info("Canceled visibility benchmark");
    }
}

void UvsrSceneViewer::ReleaseVisibilityBenchmarkCameraLock()
{
    if (!m_VisibilityBenchmarkOwnsCameraLock)
        return;

    m_VisibilityBenchmarkOwnsCameraLock = false;
    m_BenchmarkCameraActive = false;
    m_ui.Camera = m_VisibilityBenchmarkPreviousCameraMode;

    GLFWwindow* window = GetDeviceManager()->GetWindow();
    glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
    glfwSetKeyCallback(window, g_BenchmarkForwardKeyCallback);
    g_BenchmarkForwardKeyCallback = nullptr;
}

void UvsrSceneViewer::FailVisibilityBenchmark(const std::string& message)
{
    const bool closeAfterFailure = m_VisibilityBenchmarkAutoClose ||
        m_VisibilityBenchmarkSequenceAutoClose;
    const bool failedSequence = m_VisibilityBenchmarkSequenceActive;
    const std::string failedSequenceName =
        m_VisibilityBenchmarkSequenceName;
    const std::string failedEntryName = failedSequence &&
            m_VisibilityBenchmarkSequenceIndex <
                m_VisibilityBenchmarkSequenceEntries.size()
        ? m_VisibilityBenchmarkSequenceEntries[
            m_VisibilityBenchmarkSequenceIndex].name
        : std::string();
    m_VisibilityBenchmarkQueued = false;
    if (m_ScreenSpaceVisibilityPass &&
        m_ScreenSpaceVisibilityPass->IsBenchmarkActive())
    {
        m_ScreenSpaceVisibilityPass->CancelBenchmark();
    }
    m_VisibilityBenchmarkAutoClose = false;
    DiscardVisibilityBenchmarkMeasuredFrame();
    m_ActiveVisibilityBenchmarkArtifact = {};
    RestoreVisibilityBenchmarkSequenceSettings();
    ReleaseVisibilityBenchmarkCameraLock();
    m_VisibilityBenchmarkStatus = failedSequence
        ? "Sequence failed: " + failedSequenceName + "."
        : "Failed.";
    m_VisibilityBenchmarkError = failedSequence
        ? "Entry '" + failedEntryName + "': " + message
        : message;
    log::warning("Visibility benchmark failed: %s",
        m_VisibilityBenchmarkError.c_str());
    if (closeAfterFailure)
    {
        std::fprintf(stderr, "UVSR visibility benchmark error: %s\n",
            m_VisibilityBenchmarkError.c_str());
        std::fflush(stderr);
        g_VisibilityBenchmarkFailed = true;
        glfwSetWindowShouldClose(
            GetDeviceManager()->GetWindow(), GLFW_TRUE);
    }
}

void UvsrSceneViewer::UpdateVisibilityBenchmarkAfterRender()
{
    if (!m_ScreenSpaceVisibilityPass)
        return;

    int width = 0;
    int height = 0;
    GetDeviceManager()->GetWindowDimensions(width, height);
    if (m_VisibilityBenchmarkQueued ||
        m_ScreenSpaceVisibilityPass->IsBenchmarkActive())
    {
        const SponzaCameraPreset& preset = GetDefaultSponzaCameraPreset();
        const BaseCamera& camera = GetActiveCamera();
        if (!m_BenchmarkCameraActive || m_ui.Camera != CameraMode::Static ||
            width != int(preset.ReferenceWidth) ||
            height != int(preset.ReferenceHeight) ||
            !IsSponzaCameraAtPreset(
                preset,
                camera.GetPosition(),
                camera.GetDir(),
                camera.GetUp()))
        {
            FailVisibilityBenchmark(
                "The controlled environment changed. Visibility benchmarks "
                "require locked Benchmark Position 1 at 1920x1080.");
            return;
        }
    }
    const VisibilityPerformanceWorkload workload =
        BuildVisibilityPerformanceWorkload(
            m_ui.ScreenSpaceVisibility,
            uint32_t(std::max(width, 0)),
            uint32_t(std::max(height, 0)));
    const VisibilityExecutionPlan plan = ResolveVisibilityExecutionPlan(
        m_ui.ScreenSpaceVisibility.performanceProfile,
        workload);
    const ScreenSpaceVisibilityTimings& timings =
        m_ScreenSpaceVisibilityPass->GetTimings();

    if (m_VisibilityBenchmarkQueued)
    {
        if (!plan.valid)
        {
            FailVisibilityBenchmark(
                "The resolved implementation profile is invalid: " +
                plan.errorMessage);
            return;
        }
        if (!timings.profileValid)
        {
            FailVisibilityBenchmark(
                timings.profileError.empty()
                    ? "The renderer rejected the resolved implementation profile."
                    : timings.profileError);
            return;
        }
        if (timings.activePermutation != plan.permutationName)
        {
            FailVisibilityBenchmark(
                "The renderer active permutation does not match the CPU plan: '" +
                timings.activePermutation + "' versus '" +
                plan.permutationName + "'.");
            return;
        }

        std::string profileName;
        if (m_ui.VisibilityVerification !=
            VisibilityVerificationProfile::Unset)
        {
            const VisibilityVerificationProfileResolution verification =
                ResolveVisibilityVerificationProfile(
                    m_ui.VisibilityVerification,
                    m_ui.ScreenSpaceVisibility.performanceProfile,
                    workload);
            if (!verification.valid)
            {
                FailVisibilityBenchmark(
                    "The selected one-click profile is stale: " +
                    verification.reason);
                return;
            }
            const std::string settingsMismatch =
                FindVisibilityVerificationSettingsMismatch(
                    m_ui.VisibilityVerification,
                    m_ui.ScreenSpaceVisibility,
                    workload);
            if (!settingsMismatch.empty())
            {
                FailVisibilityBenchmark(
                    "The selected one-click profile settings are stale: " +
                    settingsMismatch);
                return;
            }
            profileName.assign(verification.definition.name);
        }
        else
        {
            const VisibilityPerformanceProfileConfiguration configuration =
                GetVisibilityPerformanceProfileConfiguration(
                    m_ui.ScreenSpaceVisibility.performanceProfile);
            profileName.assign(configuration.name);
        }

        std::ostringstream permutationMetadata;
        permutationMetadata << "0x" << std::uppercase << std::hex
            << std::setfill('0') << std::setw(16)
            << static_cast<unsigned long long>(plan.permutationKey)
            << ':' << timings.activePermutation;
        VisibilityBenchmarkRunMetadata metadata;
        metadata.profileName = std::move(profileName);
        metadata.permutationKey = permutationMetadata.str();
        metadata.adapterName = GetActiveAdapterName();
        metadata.clockState = "Unavailable (No GPU Clock Telemetry)";
        m_ActiveVisibilityBenchmarkArtifact.sceneName =
            GetCurrentSceneDisplayName();
        m_ActiveVisibilityBenchmarkArtifact.cameraPresetId =
            GetDefaultSponzaCameraPreset().Id;
        m_ActiveVisibilityBenchmarkArtifact.graphicsApi =
            nvrhi::utils::GraphicsAPIToString(
                GetDeviceManager()->GetGraphicsAPI());
        m_ActiveVisibilityBenchmarkArtifact.buildIdentity = UVSR_GIT_COMMIT;
        m_ActiveVisibilityBenchmarkArtifact.shaderPermutationKey =
            plan.shaderPermutationKey;
        m_ActiveVisibilityBenchmarkArtifact.outputWidth = uint32_t(width);
        m_ActiveVisibilityBenchmarkArtifact.outputHeight = uint32_t(height);
        m_ActiveVisibilityBenchmarkArtifact.runSettings =
            BuildVisibilityBenchmarkRunSettings(
                m_ui.ScreenSpaceVisibility,
                workload,
                plan.configuration,
                plan,
                timings);
        if (m_VisibilityBenchmarkSequenceActive)
        {
            m_ActiveVisibilityBenchmarkArtifact.sequenceName =
                m_VisibilityBenchmarkSequenceName;
            m_ActiveVisibilityBenchmarkArtifact.sequenceEntryName =
                m_VisibilityBenchmarkSequenceEntries[
                    m_VisibilityBenchmarkSequenceIndex].name;
            m_ActiveVisibilityBenchmarkArtifact.sequenceEntryIndex =
                uint32_t(m_VisibilityBenchmarkSequenceIndex + 1u);
            m_ActiveVisibilityBenchmarkArtifact.sequenceEntryCount =
                uint32_t(m_VisibilityBenchmarkSequenceEntries.size());
        }

        std::string capturePathError;
        if (!AllocateVisibilityBenchmarkMeasuredFramePath(
                metadata,
                m_ActiveVisibilityBenchmarkArtifact.runSettings.hash,
                m_VisibilityBenchmarkOutputDirectory,
                m_VisibilityBenchmarkMeasuredFramePath,
                capturePathError))
        {
            FailVisibilityBenchmark(
                "Cannot reserve the final measured-frame capture: " +
                capturePathError);
            return;
        }

        if (!m_ScreenSpaceVisibilityPass->BeginBenchmark(
                metadata,
                m_VisibilityBenchmarkWarmup,
                m_VisibilityBenchmarkFrames))
        {
            FailVisibilityBenchmark(
                "The visibility pass rejected the benchmark configuration.");
            return;
        }
        m_VisibilityBenchmarkQueued = false;
        m_VisibilityBenchmarkPermutation = timings.activePermutation;
        m_VisibilityBenchmarkRenderedFrames = 0u;
        if (m_VisibilityBenchmarkSequenceActive)
        {
            m_VisibilityBenchmarkStatus =
                "Sequence " + m_VisibilityBenchmarkSequenceName + ": running " +
                std::to_string(m_VisibilityBenchmarkSequenceIndex + 1u) +
                " of " +
                std::to_string(m_VisibilityBenchmarkSequenceEntries.size()) +
                " (" +
                m_VisibilityBenchmarkSequenceEntries[
                    m_VisibilityBenchmarkSequenceIndex].name +
                ").";
        }
        else
        {
            m_VisibilityBenchmarkStatus =
                "Running warmup and measured frames.";
        }
        log::info(
            "Started visibility benchmark '%s' on '%s' with permutation %s",
            metadata.profileName.c_str(),
            metadata.adapterName.c_str(),
            timings.activePermutation.c_str());
        // BeginBenchmark arms the next visibility frame. Do not count the
        // already-rendered frame that resolved the queued profile.
        return;
    }

    if (!m_ScreenSpaceVisibilityPass->IsBenchmarkActive())
        return;

    if (timings.activePermutation != m_VisibilityBenchmarkPermutation)
    {
        FailVisibilityBenchmark(
            "The active permutation changed during measurement from '" +
            m_VisibilityBenchmarkPermutation + "' to '" +
            timings.activePermutation + "'.");
        return;
    }
    ++m_VisibilityBenchmarkRenderedFrames;
    const VisibilityBenchmarkSummary progressSummary =
        m_ScreenSpaceVisibilityPass->GetBenchmarkSummary();
    const uint64_t finalMeasuredFrameId =
        progressSummary.configuration.firstFrameId +
        uint64_t(progressSummary.configuration.warmupFrameCount) +
        uint64_t(progressSummary.configuration.measuredFrameCount) - 1u;
    const uint64_t nextLogicalFrameId =
        m_ScreenSpaceVisibilityPass->GetBenchmarkNextLogicalFrameId();
    if (nextLogicalFrameId == finalMeasuredFrameId + 1u &&
        !m_VisibilityBenchmarkMeasuredFrameCaptured &&
        m_PendingPresentedFrameCapture.empty())
    {
        m_PendingPresentedFrameCapture =
            m_VisibilityBenchmarkMeasuredFramePath;
        m_VisibilityBenchmarkStatus =
            "Measurement window complete; capturing its final presented frame.";
    }
    if (!m_ScreenSpaceVisibilityPass->IsBenchmarkComplete())
    {
        const uint64_t maximumRenderedFrames =
            uint64_t(m_VisibilityBenchmarkWarmup) +
            uint64_t(m_VisibilityBenchmarkFrames) +
            VisibilityBenchmarkQueryDrainAllowanceFrames;
        if (m_VisibilityBenchmarkRenderedFrames > maximumRenderedFrames)
        {
            const VisibilityBenchmarkSummary partialSummary =
                m_ScreenSpaceVisibilityPass->GetBenchmarkSummary();
            FailVisibilityBenchmark(
                "Timed out while draining complete GPU timer sets (" +
                std::to_string(partialSummary.completeFrameCount) + " of " +
                std::to_string(m_VisibilityBenchmarkFrames) +
                " measured frames complete). No partial result was exported.");
        }
        return;
    }

    const VisibilityBenchmarkSummary completedSummary =
        m_ScreenSpaceVisibilityPass->GetBenchmarkSummary();
    m_ScreenSpaceVisibilityPass->CancelBenchmark();
    m_VisibilityBenchmarkError.clear();
    if (!m_VisibilityBenchmarkMeasuredFrameCaptured)
    {
        FailVisibilityBenchmark(
            "GPU timers completed before the final measured frame capture "
            "was available. No result was exported.");
        return;
    }

    VisibilityBenchmarkExportPaths exportedPaths;
    std::string exportError;
    if (!ExportVisibilityBenchmark(
            completedSummary,
            m_ActiveVisibilityBenchmarkArtifact,
            m_VisibilityBenchmarkOutputDirectory,
            exportedPaths,
            exportError))
    {
        FailVisibilityBenchmark(
            "The measurement completed, but export failed: " + exportError);
        return;
    }

    std::error_code moveError;
    std::filesystem::rename(
        m_VisibilityBenchmarkMeasuredFramePath,
        exportedPaths.presentedFrame,
        moveError);
    if (moveError)
    {
        moveError.clear();
        std::filesystem::copy_file(
            m_VisibilityBenchmarkMeasuredFramePath,
            exportedPaths.presentedFrame,
            std::filesystem::copy_options::none,
            moveError);
        if (!moveError)
        {
            std::error_code removeError;
            std::filesystem::remove(
                m_VisibilityBenchmarkMeasuredFramePath, removeError);
        }
    }
    if (moveError)
    {
        std::error_code removeError;
        std::filesystem::remove(exportedPaths.json, removeError);
        std::filesystem::remove(exportedPaths.csv, removeError);
        FailVisibilityBenchmark(
            "The measurement completed, but its final measured frame could "
            "not be finalized: " + moveError.message());
        return;
    }

    m_LastVisibilityBenchmarkSummary = completedSummary;
    m_LastVisibilityBenchmarkPaths = exportedPaths;
    m_LastVisibilityBenchmarkArtifact = m_ActiveVisibilityBenchmarkArtifact;
    m_HasVisibilityBenchmarkSummary = true;
    m_ActiveVisibilityBenchmarkArtifact = {};
    m_VisibilityBenchmarkMeasuredFramePath.clear();
    m_VisibilityBenchmarkMeasuredFrameCaptured = false;
    const bool closeAfterExport = m_VisibilityBenchmarkAutoClose;
    m_VisibilityBenchmarkAutoClose = false;
    m_VisibilityBenchmarkStatus =
        "Complete; JSON, CSV, and final measured frame exported.";
    log::info("Visibility benchmark JSON: %s",
        exportedPaths.json.generic_string().c_str());
    log::info("Visibility benchmark raw CSV: %s",
        exportedPaths.csv.generic_string().c_str());
    log::info("Visibility benchmark final measured frame: %s",
        exportedPaths.presentedFrame.generic_string().c_str());
    if (m_VisibilityBenchmarkSequenceActive)
    {
        (void)AdvanceVisibilityBenchmarkSequence();
        return;
    }
    ReleaseVisibilityBenchmarkCameraLock();
    if (closeAfterExport)
    {
        glfwSetWindowShouldClose(
            GetDeviceManager()->GetWindow(), GLFW_TRUE);
    }
}

void UvsrSceneViewer::CaptureCompletedVisibilityBenchmarkFrame(
    nvrhi::ITexture* framebufferTexture)
{
    if (m_PendingPresentedFrameCapture.empty())
        return;
    if (!framebufferTexture)
    {
        FailVisibilityBenchmark(
            "The final measured framebuffer was unavailable for capture.");
        return;
    }

    const std::filesystem::path capturePath =
        m_PendingPresentedFrameCapture;
    m_PendingPresentedFrameCapture.clear();
    SaveTextureToFile(
        GetDevice(),
        m_CommonPasses.get(),
        framebufferTexture,
        nvrhi::ResourceStates::RenderTarget,
        capturePath.string().c_str());

    std::error_code fileError;
    const bool captureCreated =
        std::filesystem::exists(capturePath, fileError) && !fileError;
    if (captureCreated)
    {
        m_VisibilityBenchmarkMeasuredFrameCaptured = true;
        m_VisibilityBenchmarkStatus =
            "Final measured frame captured; draining GPU timer queries.";
        log::info("Visibility benchmark measured frame staged at: %s",
            capturePath.generic_string().c_str());
    }
    else
    {
        FailVisibilityBenchmark(
            "The final measured frame was not created at '" +
            capturePath.generic_string() + "'.");
    }
}

bool UvsrSceneViewer::ExportLastVisibilityBenchmark(
    const std::filesystem::path& outputDirectory)
{
    if (!m_HasVisibilityBenchmarkSummary)
    {
        m_VisibilityBenchmarkError =
            "No completed visibility benchmark is available to export.";
        log::warning("%s", m_VisibilityBenchmarkError.c_str());
        return false;
    }

    VisibilityBenchmarkExportPaths exportedPaths;
    std::string exportError;
    if (!ExportVisibilityBenchmark(
            m_LastVisibilityBenchmarkSummary,
            m_LastVisibilityBenchmarkArtifact,
            outputDirectory,
            exportedPaths,
            exportError))
    {
        m_VisibilityBenchmarkError = exportError;
        log::warning("Visibility benchmark export failed: %s",
            exportError.c_str());
        return false;
    }

    std::error_code copyError;
    if (!m_LastVisibilityBenchmarkPaths.presentedFrame.empty() &&
        std::filesystem::exists(
            m_LastVisibilityBenchmarkPaths.presentedFrame, copyError) &&
        !copyError)
    {
        std::filesystem::copy_file(
            m_LastVisibilityBenchmarkPaths.presentedFrame,
            exportedPaths.presentedFrame,
            std::filesystem::copy_options::none,
            copyError);
    }
    else
    {
        copyError = std::make_error_code(std::errc::no_such_file_or_directory);
    }
    if (copyError)
    {
        // Re-export is an all-or-nothing artifact set. Do not leave a summary
        // that names a final measured frame which could not be copied.
        std::error_code cleanupError;
        std::filesystem::remove(exportedPaths.json, cleanupError);
        cleanupError.clear();
        std::filesystem::remove(exportedPaths.csv, cleanupError);
        cleanupError.clear();
        std::filesystem::remove(exportedPaths.presentedFrame, cleanupError);
        m_VisibilityBenchmarkError =
            "Benchmark re-export was rolled back because the recorded final "
            "measured frame could not be copied: " + copyError.message();
        log::warning("%s", m_VisibilityBenchmarkError.c_str());
        return false;
    }

    m_LastVisibilityBenchmarkPaths = exportedPaths;
    m_VisibilityBenchmarkError.clear();
    m_VisibilityBenchmarkStatus =
        "Last JSON, CSV, and final measured frame exported again.";
    log::info("Re-exported visibility benchmark JSON: %s",
        exportedPaths.json.generic_string().c_str());
    return true;
}

class UIRenderer : public ImGui_Renderer
{
private:
    std::shared_ptr<UvsrSceneViewer> m_app;

	std::shared_ptr<app::RegisteredFont> m_FontOpenSans;
    std::shared_ptr<engine::Light> m_SelectedLight;

	UIData& m_ui;

    static const char* GetOptimizationClassLabel(
        VisibilityOptimizationClass optimizationClass)
    {
        switch (optimizationClass)
        {
        case VisibilityOptimizationClass::Reference:
            return "Reference";
        case VisibilityOptimizationClass::Diagnostic:
            return "Diagnostic Only";
        case VisibilityOptimizationClass::Exact:
            return "Exact Implementation Change";
        case VisibilityOptimizationClass::Numerical:
            return "Numerical Approximation";
        case VisibilityOptimizationClass::Algorithmic:
            return "Algorithmic Approximation";
        default:
            return "Unclassified";
        }
    }

    static const char* GetImplementationStatusLabel(
        VisibilityImplementationStatus status)
    {
        switch (status)
        {
        case VisibilityImplementationStatus::Implemented:
            return "Implemented";
        case VisibilityImplementationStatus::PartialBenchmarkControl:
            return "Partial Benchmark Control";
        case VisibilityImplementationStatus::Unavailable:
            return "Unavailable";
        default:
            return "Unset";
        }
    }

    static std::string_view GetPerformanceProfileUiLabel(
        VisibilityPerformanceProfile profile)
    {
        return GetVisibilityPerformanceProfileDisplayName(profile);
    }

    static const char* GetConsumerLabel(
        const VisibilityPerformanceWorkload& workload)
    {
        switch (workload.consumer)
        {
        case VisibilityPerformanceConsumer::AmbientOcclusion:
            return "AO Only";
        case VisibilityPerformanceConsumer::IndirectDiffuse:
            return workload.bounceCount > 1u
                ? "GI Only, Multi-Bounce"
                : "GI Only";
        case VisibilityPerformanceConsumer::
                AmbientOcclusionAndIndirectDiffuse:
            return workload.bounceCount > 1u
                ? "AO + GI Multi-Bounce"
                : "AO + GI";
        default:
            return "Unknown";
        }
    }

    static const char* GetEstimatorLabel(
        VisibilityPerformanceEstimator estimator)
    {
        switch (estimator)
        {
        case VisibilityPerformanceEstimator::UniformProjectedAngle:
            return "Uniform Projected Angle";
        case VisibilityPerformanceEstimator::UniformSolidAngle:
            return "Uniform Solid Angle";
        case VisibilityPerformanceEstimator::CosineWeightedSolidAngle:
            return "Cosine-Weighted Solid Angle";
        default:
            return "Unknown";
        }
    }

    static const char* GetResolutionLabel(
        VisibilityPerformanceResolution resolution)
    {
        switch (resolution)
        {
        case VisibilityPerformanceResolution::Full:
            return "Full";
        case VisibilityPerformanceResolution::Half:
            return "Half";
        case VisibilityPerformanceResolution::Quarter:
            return "Quarter";
        default:
            return "Unknown";
        }
    }

    static const char* GetSchedulerLabel(
        VisibilityPerformanceScheduler scheduler)
    {
        switch (scheduler)
        {
        case VisibilityPerformanceScheduler::IndependentHash:
            return "Independent Hash";
        case VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField:
            return "Current Toroidal Blue Noise";
        case VisibilityPerformanceScheduler::
                FilterAdaptedSpatiotemporalRankField:
            return "Current FAST";
        case VisibilityPerformanceScheduler::Activision4x4SixPhase:
            return "Activision 4x4 x 6 Schedule";
        case VisibilityPerformanceScheduler::XeGtaoHilbertR2:
            return "XeGTAO Hilbert + R2";
        case VisibilityPerformanceScheduler::ConstantDiagnostic:
            return "Constant Diagnostic";
        default:
            return "Unknown";
        }
    }

    static const char* GetTraceLabel(
        VisibilityTraceImplementation trace)
    {
        switch (trace)
        {
        case VisibilityTraceImplementation::LegacyGenericBitmask:
            return "Reference Generic Bitmask";
        case VisibilityTraceImplementation::FixedInterleavedBitmask:
            return "Candidate Fixed Bitmask";
        case VisibilityTraceImplementation::ConstantDiagnostic:
            return "Constant Diagnostic";
        case VisibilityTraceImplementation::DepthOnlyDiagnostic:
            return "Depth-Read Diagnostic";
        case VisibilityTraceImplementation::BitmaskOnlyDiagnostic:
            return "Bitmask-ALU Diagnostic";
        case VisibilityTraceImplementation::ActivisionHorizon:
            return "Activision Horizon Benchmark";
        case VisibilityTraceImplementation::XeGtaoHorizon:
            return "Intel XeGTAO 1.30 Source Port";
        default:
            return "Unknown";
        }
    }

    static const char* GetSampleSpecializationLabel(
        VisibilitySampleSpecialization specialization)
    {
        switch (specialization)
        {
        case VisibilitySampleSpecialization::Runtime:
            return "Generic Runtime Count";
        case VisibilitySampleSpecialization::Fixed8:
            return "Fixed 8 Total, 4 Per Side";
        case VisibilitySampleSpecialization::Fixed12:
            return "Fixed 12 Total, 6 Per Side";
        case VisibilitySampleSpecialization::Fixed16:
            return "Fixed 16 Total, 8 Per Side";
        case VisibilitySampleSpecialization::Fixed18:
            return "Fixed 18 Total, 3 Slices x 3 Steps x 2 Sides";
        case VisibilitySampleSpecialization::Fixed20:
            return "Fixed 20 Total, 10 Per Side";
        default:
            return "Unknown";
        }
    }

    static const char* GetNoiseDeliveryLabel(VisibilityNoiseDelivery noise)
    {
        switch (noise)
        {
        case VisibilityNoiseDelivery::Legacy:
            return "Reference Scheduler Resources";
        case VisibilityNoiseDelivery::PackedCurrentFast:
            return "Packed RGBA8 Current FAST";
        case VisibilityNoiseDelivery::ConstantDiagnostic:
            return "Constant Diagnostic";
        case VisibilityNoiseDelivery::ActivisionInterleavedGradient:
            return "Activision Interleaved Gradient";
        case VisibilityNoiseDelivery::XeGtaoHilbertR2:
            return "XeGTAO Hilbert + R2";
        case VisibilityNoiseDelivery::XeGtaoInlineHilbertR2:
            return "XeGTAO Inline Hilbert + R2";
        default:
            return "Unknown";
        }
    }

    static const char* GetMathModeLabel(VisibilityMathMode math)
    {
        switch (math)
        {
        case VisibilityMathMode::ReferenceFp32:
            return "Reference FP32";
        case VisibilityMathMode::ConservativeNumericalFp32:
            return "Conservative Numerical FP32";
        case VisibilityMathMode::ActivisionFastFp32:
            return "Activision Fast FP32";
        case VisibilityMathMode::XeGtaoMixedPrecision:
            return "XeGTAO Mixed Precision";
        default:
            return "Unknown";
        }
    }

    static const char* GetRawAoStorageLabel(VisibilityRawAoStorage storage)
    {
        switch (storage)
        {
        case VisibilityRawAoStorage::R16Float:
            return "R16_FLOAT";
        case VisibilityRawAoStorage::R8Unorm:
            return "R8_UNORM";
        case VisibilityRawAoStorage::PackedCountAndEdgesR16Uint:
            return "Packed R16_UINT Count + Edges";
        default:
            return "Unknown";
        }
    }

    static const char* GetEdgeStorageLabel(VisibilityEdgeStorage storage)
    {
        switch (storage)
        {
        case VisibilityEdgeStorage::None:
            return "None";
        case VisibilityEdgeStorage::R8Uint:
            return "R8_UINT Packed L/R/T/B";
        case VisibilityEdgeStorage::R8Unorm:
            return "R8_UNORM Packed L/R/T/B";
        default:
            return "Unknown";
        }
    }

    static const char* GetReconstructionLabel(
        VisibilityReconstructionMode reconstruction)
    {
        switch (reconstruction)
        {
        case VisibilityReconstructionMode::Legacy:
            return "Legacy Joint Bilateral";
        case VisibilityReconstructionMode::NearestDiagnostic:
            return "Nearest Diagnostic";
        case VisibilityReconstructionMode::BilinearDiagnostic:
            return "Bilinear Diagnostic";
        case VisibilityReconstructionMode::PackedEdges2x2:
            return "Packed Edge 2x2";
        case VisibilityReconstructionMode::PackedEdges4x4:
            return "Packed Edge 4x4";
        case VisibilityReconstructionMode::ActivisionBilateral4x4:
            return "Activision Bilateral 4x4";
        case VisibilityReconstructionMode::XeGtaoDenoise:
            return "XeGTAO Denoise";
        default:
            return "Unknown";
        }
    }

    static const char* GetApplicationLabel(
        VisibilityApplicationMode application)
    {
        switch (application)
        {
        case VisibilityApplicationMode::LegacySeparateComposition:
            return "Separate Filter + Composite";
        case VisibilityApplicationMode::FusedResolveAndApplyExact:
            return "Fused Resolve + Apply";
        case VisibilityApplicationMode::FusedResolveAndApplyPackedEdges:
            return "Fused Packed Edge Resolve + Apply";
        case VisibilityApplicationMode::IsolatedCompositionDiagnostic:
            return "Isolated Composition Diagnostic";
        case VisibilityApplicationMode::BypassCompositionDiagnostic:
            return "Composition Bypass Diagnostic";
        default:
            return "Unknown";
        }
    }

    static const char* GetDepthModeLabel(VisibilityDepthMode depth)
    {
        switch (depth)
        {
        case VisibilityDepthMode::Legacy:
            return "Direct Device Depth";
        case VisibilityDepthMode::ActivisionClampedScreenRadius:
            return "Activision Clamped Screen Radius";
        case VisibilityDepthMode::XeGtaoPrefilteredMips:
            return "XeGTAO Prefiltered Mips";
        default:
            return "Unknown";
        }
    }

    static const char* GetBindingStrategyLabel(
        VisibilityBindingStrategy bindings)
    {
        switch (bindings)
        {
        case VisibilityBindingStrategy::LegacyBroad:
            return "Reference Broad Bindings";
        case VisibilityBindingStrategy::MinimalConditional:
            return "Minimal Consumer-Driven Bindings";
        default:
            return "Unknown";
        }
    }

    static void DrawUnavailableOption(
        const char* label,
        const char* reason)
    {
        ImGui::TextDisabled("%s: Not Exposed", label);
        ImGui::SetItemTooltip(reason);
    }

    static bool DrawCollapsingHeader(
        const char* label,
        const char* tooltip,
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None)
    {
        const bool open = ImGui::CollapsingHeader(label, flags);
        ImGui::SetItemTooltip(tooltip);
        return open;
    }

    static bool DrawFolderButton(const char* id, const char* tooltip)
    {
        const float buttonSize = ImGui::GetFrameHeight();
        const bool clicked = ImGui::Button(id, ImVec2(buttonSize, 0.f));
        const ImVec2 iconMin = ImGui::GetItemRectMin();
        const ImVec2 iconMax = ImGui::GetItemRectMax();
        const float iconWidth = iconMax.x - iconMin.x;
        const float iconHeight = iconMax.y - iconMin.y;
        const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 bodyMin(
            iconMin.x + iconWidth * 0.20f,
            iconMin.y + iconHeight * 0.38f);
        const ImVec2 bodyMax(
            iconMax.x - iconWidth * 0.20f,
            iconMax.y - iconHeight * 0.22f);
        drawList->AddRect(bodyMin, bodyMax, iconColor, 1.5f, 0, 1.5f);
        drawList->AddLine(
            ImVec2(bodyMin.x, bodyMin.y),
            ImVec2(bodyMin.x + iconWidth * 0.22f,
                iconMin.y + iconHeight * 0.27f),
            iconColor, 1.5f);
        drawList->AddLine(
            ImVec2(bodyMin.x + iconWidth * 0.22f,
                iconMin.y + iconHeight * 0.27f),
            ImVec2(bodyMin.x + iconWidth * 0.40f, bodyMin.y),
            iconColor, 1.5f);
        ImGui::SetItemTooltip("%s", tooltip);
        return clicked;
    }

    static const char* GetBenchmarkStageLabel(
        VisibilityBenchmarkStage stage)
    {
        switch (stage)
        {
        case VisibilityBenchmarkStage::DepthPreparation:
            return "Depth Preparation";
        case VisibilityBenchmarkStage::FirstTrace:
            return "First-Bounce Visibility Trace";
        case VisibilityBenchmarkStage::LaterTrace:
            return "Later-Bounce GI Trace";
        case VisibilityBenchmarkStage::LaterTraceBounce2:
            return "  GI Bounce 2";
        case VisibilityBenchmarkStage::LaterTraceBounce3:
            return "  GI Bounce 3";
        case VisibilityBenchmarkStage::LaterTraceBounce4:
            return "  GI Bounce 4";
        case VisibilityBenchmarkStage::SpatialDenoise:
            return "Spatial Denoise";
        case VisibilityBenchmarkStage::Temporal:
            return "Temporal Reconstruction";
        case VisibilityBenchmarkStage::FusedSpatialDenoiseUpsample:
            return "Fused Spatial Denoise & Upsample";
        case VisibilityBenchmarkStage::RequiredUpsample:
            return "Required Full-Resolution Upsample";
        case VisibilityBenchmarkStage::FullResolutionApply:
            return "Fused Resolve & Apply";
        case VisibilityBenchmarkStage::Composition:
            return "Indirect-Lighting Composition";
        case VisibilityBenchmarkStage::EffectEnvelope:
            return "Complete Visibility Pipeline";
        default:
            return "Unknown Stage";
        }
    }

public:
    UIRenderer(DeviceManager* deviceManager, std::shared_ptr<UvsrSceneViewer> app, UIData& ui)
        : ImGui_Renderer(deviceManager)
        , m_app(app)
        , m_ui(ui)
    {
		m_FontOpenSans = CreateFontFromFile(*(app->GetRootFs()), "/media/fonts/OpenSans/OpenSans-Regular.ttf", 17.f);

        ImGui::GetIO().IniFilename = nullptr;
    }

protected:
    virtual void buildUI(void) override
    {
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

        int width, height;
        GetDeviceManager()->GetWindowDimensions(width, height);
        const bool visibilityBenchmarkBusy =
            m_app->IsVisibilityBenchmarkQueued() ||
            m_app->IsVisibilityBenchmarkActive() ||
            m_app->IsVisibilityBenchmarkSequenceActive();

        if (m_app->IsSceneLoading())
        {
            BeginFullScreenWindow();
            ImGui::PushFont(m_FontOpenSans->GetScaledFont());

            char messageBuffer[256];
            const auto& stats = Scene::GetLoadingStats();
            const std::string sceneDisplayName = m_app->GetCurrentSceneDisplayName();
            snprintf(messageBuffer, std::size(messageBuffer), "Loading scene %s, please wait...\nObjects: %d/%d, Textures: %d/%d",
                sceneDisplayName.c_str(), stats.ObjectsLoaded.load(), stats.ObjectsTotal.load(), m_app->GetTextureCache()->GetNumberOfLoadedTextures(), m_app->GetTextureCache()->GetNumberOfRequestedTextures());

            DrawScreenCenteredText(messageBuffer);

            const int objectsLoaded = stats.ObjectsLoaded.load();
            const int objectsTotal = stats.ObjectsTotal.load();
            const int texturesLoaded = m_app->GetTextureCache()->GetNumberOfLoadedTextures();
            const int texturesTotal = m_app->GetTextureCache()->GetNumberOfRequestedTextures();
            const int itemsLoaded = std::max(objectsLoaded, 0) + std::max(texturesLoaded, 0);
            const int itemsTotal = std::max(objectsTotal, objectsLoaded) + std::max(texturesTotal, texturesLoaded);
            const float loadingProgress = itemsTotal > 0
                ? std::clamp(float(itemsLoaded) / float(itemsTotal), 0.f, 1.f)
                : 0.f;

            constexpr float loadingBarHeight = 4.f;
            const float loadingBarWidth = std::min(float(width) * 0.84f, 1040.f);
            ImGui::SetCursorScreenPos(ImVec2(
                (float(width) - loadingBarWidth) * 0.5f,
                float(height) * 0.5f + ImGui::GetTextLineHeightWithSpacing() * 1.8f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.10f, 0.14f, 0.85f));
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.16f, 0.48f, 0.92f, 1.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
            ImGui::ProgressBar(loadingProgress, ImVec2(loadingBarWidth, loadingBarHeight), "");
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);

            ImGui::PopFont();
            EndFullScreenWindow();

            return;
        }

        ImGui::PushFont(m_FontOpenSans->GetScaledFont());

        float const fontSize = ImGui::GetFontSize();

        ImGui::SetNextWindowPos(ImVec2(fontSize * 0.6f, fontSize * 0.6f), 0);
        const float settingsWindowHeight = std::max(
            fontSize * 24.f,
            std::min(float(height) - fontSize * 1.2f, fontSize * 50.f));
        ImGui::SetNextWindowSize(
            ImVec2(fontSize * 27.f, settingsWindowHeight),
            ImGuiCond_Once);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(fontSize * 24.f, fontSize * 18.f),
            ImVec2(fontSize * 32.f,
                std::max(fontSize * 18.f,
                    float(height) - fontSize * 1.2f)));
        ImGui::Begin("Settings", 0, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float settingsControlWidth =
            ImGui::CalcTextSize("Reload Shaders").x + style.FramePadding.x * 2.f +
            style.ItemSpacing.x +
            ImGui::CalcTextSize("Restart Renderer").x + style.FramePadding.x * 2.f;

        ImGui::Text("Renderer: %s", GetDeviceManager()->GetRendererString());
        double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
        if (frameTime > 0.0)
        {
            const GpuPerformanceMetrics gpuMetrics = QueryGpuPerformanceMetrics(
                GetDeviceManager()->GetRendererString());
            if (gpuMetrics.valid)
            {
                ImGui::Text("%d x %d | %.3f ms | %.1f FPS | %.1f GB/s | %.0f GFLOPS",
                    width, height, frameTime * 1e3, 1.0 / frameTime,
                    gpuMetrics.memoryBandwidthGBps, gpuMetrics.gpuGFlops);
            }
            else
            {
                ImGui::Text("%d x %d | %.3f ms | %.1f FPS | -- GB/s | -- GFLOPS",
                    width, height, frameTime * 1e3, 1.0 / frameTime);
            }
            ImGui::SetItemTooltip(
                "The GPU's current theoretical limits, not measured app use.");
        }
        if (visibilityBenchmarkBusy)
        {
            ImGui::TextDisabled(
                "Benchmark Environment Locked; Cancel Under AO Benchmarks");
        }

        const bool generalOpen = DrawCollapsingHeader(
            "General",
            "Show general renderer settings.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (generalOpen)
        {
        if (visibilityBenchmarkBusy)
            ImGui::BeginDisabled();

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
            if (ImGui::BeginCombo("##GraphicsAdapter", activeAdapterName))
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
        const bool cameraComboOpen = ImGui::BeginCombo(
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
            const bool cameraLocationComboOpen = ImGui::BeginCombo(
                "##CameraLocation",
                GetSponzaCameraLocationLabel(selectedCameraLocation));
            ImGui::SetItemTooltip(benchmarkCameraActive
                ? "Benchmark mode locks Benchmark Position 1."
                : "Recall a stored camera location. Movement changes this status to Free.");
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
        const bool worldMaterialComboOpen = ImGui::BeginCombo(
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
        const bool sceneComboOpen = ImGui::BeginCombo("##Scene", currentSceneDisplayName.c_str());
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
        if (DrawFolderButton("##OpenSceneFolder", "Open the scene folder."))
        {
            const std::filesystem::path sceneFolder = m_app->GetSceneDir();
            ShellExecuteW(nullptr, L"open", sceneFolder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

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
        if (visibilityBenchmarkBusy)
            ImGui::EndDisabled();
        }

        const bool indirectLightingOpen = DrawCollapsingHeader(
            "Ambient Occlusion & Indirect Lighting",
            "Configure AO, diffuse GI, sampling, denoising, and performance "
            "experiments.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (indirectLightingOpen)
        {
            ScreenSpaceVisibilitySettings& visibility = m_ui.ScreenSpaceVisibility;
            const bool visibilityAvailable = m_ui.UsesDeferredShading();
            if (!visibilityAvailable)
                ImGui::BeginDisabled();
            if (visibilityBenchmarkBusy)
                ImGui::BeginDisabled();

            const ScreenSpaceVisibilityTimings* visibilityTimings =
                m_app->GetScreenSpaceVisibilityTimings();
            const VisibilityPerformanceWorkload observedWorkload =
                BuildVisibilityPerformanceWorkload(
                    visibility,
                    uint32_t(std::max(width, 0)),
                    uint32_t(std::max(height, 0)));
            const VisibilityVerificationProfileDefinition selectedDefinition =
                GetVisibilityVerificationProfileDefinition(
                    m_ui.VisibilityVerification);
            const bool customImplementationProfile =
                m_ui.VisibilityVerification ==
                    VisibilityVerificationProfile::Unset;
            const VisibilityVerificationProfileResolution
                verificationResolution = ResolveVisibilityVerificationProfile(
                    m_ui.VisibilityVerification,
                    visibility.performanceProfile,
                    observedWorkload);
            const std::string verificationSettingsMismatch =
                customImplementationProfile
                    ? std::string()
                    : FindVisibilityVerificationSettingsMismatch(
                        m_ui.VisibilityVerification,
                        visibility,
                        observedWorkload);
            const VisibilityPerformanceProfileConfiguration
                activeConfiguration =
                    GetVisibilityPerformanceProfileConfiguration(
                        visibility.performanceProfile);
            const VisibilityExecutionPlan activePlan =
                ResolveVisibilityExecutionPlan(
                    visibility.performanceProfile,
                    observedWorkload);
            const bool hasActiveVisibilityConsumer =
                visibility.enabled && visibility.HasActiveConsumer();
            const bool runtimePlanValid = visibilityTimings &&
                visibilityTimings->profileValid;
            const bool runtimePermutationMatches = visibilityTimings &&
                !visibilityTimings->activePermutation.empty() &&
                visibilityTimings->activePermutation ==
                    activePlan.permutationName;
            const bool verificationProfileValid = visibilityAvailable &&
                hasActiveVisibilityConsumer &&
                (customImplementationProfile ||
                    verificationResolution.valid) &&
                verificationSettingsMismatch.empty() &&
                activePlan.valid &&
                (customImplementationProfile ||
                    visibility.performanceProfile ==
                        selectedDefinition.implementationProfile) &&
                runtimePlanValid && runtimePermutationMatches;

            auto applyVerificationProfile =
                [&](VisibilityVerificationProfile profile)
                {
                    if (ApplyVisibilityVerificationProfileDefaults(
                            visibility, profile))
                    {
                        m_ui.VisibilityVerification = profile;
                        m_ui.EnablePbr = true;
                    }
                };
            auto applyImplementationProfile =
                [&](VisibilityPerformanceProfile profile)
                {
                    if (ApplyVisibilityPerformanceProfileDefaults(
                            visibility, profile))
                    {
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                        m_ui.EnablePbr = true;
                    }
                };
            auto switchVisibilityToCustom = [&]()
                {
                    visibility.performanceProfile =
                        VisibilityPerformanceProfile::GenericFallback;
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                };

            ImGui::TextUnformatted("Verification Profile");
            ImGui::SetNextItemWidth(-FLT_MIN);
            const char* selectedProfileName =
                customImplementationProfile
                    ? "Custom Implementation Profile"
                    : selectedDefinition.name.data();
            if (ImGui::BeginCombo(
                    "##VisibilityVerificationProfile",
                    selectedProfileName))
            {
                for (uint32_t profileIndex = 1u;
                    profileIndex < static_cast<uint32_t>(
                        VisibilityVerificationProfile::Count);
                    ++profileIndex)
                {
                    const auto profile =
                        static_cast<VisibilityVerificationProfile>(
                            profileIndex);
                    const VisibilityVerificationProfileDefinition definition =
                        GetVisibilityVerificationProfileDefinition(profile);
                    const bool available =
                        definition.implementationStatus !=
                            VisibilityImplementationStatus::Unavailable &&
                        definition.implementationStatus !=
                            VisibilityImplementationStatus::Unset;
                    const bool selected =
                        profile == m_ui.VisibilityVerification;
                    if (!available)
                        ImGui::BeginDisabled();
                    if (ImGui::Selectable(
                            definition.name.data(), selected) && available)
                    {
                        applyVerificationProfile(profile);
                    }
                    const VisibilityPerformanceProfileConfiguration
                        profileConfiguration =
                            GetVisibilityPerformanceProfileConfiguration(
                                definition.implementationProfile);
                    ImGui::SetItemTooltip(
                        "%s | %s%s%s",
                        GetOptimizationClassLabel(
                            profileConfiguration.optimizationClass),
                        GetImplementationStatusLabel(
                            definition.implementationStatus),
                        definition.implementationNote.empty() ? "" : ": ",
                        definition.implementationNote.empty()
                            ? ""
                            : definition.implementationNote.data());
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    if (!available)
                        ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose a fully specified benchmark profile. Selection "
                "overwrites all profile-relevant AO and GI settings.");

            ImGui::TextUnformatted("Performance Profile");
            ImGui::SetNextItemWidth(-FLT_MIN);
            const std::string_view activeImplementationName =
                GetPerformanceProfileUiLabel(
                    visibility.performanceProfile);
            if (ImGui::BeginCombo(
                    "##VisibilityImplementationProfile",
                    activeImplementationName.empty()
                        ? "Unset"
                        : activeImplementationName.data()))
            {
                int previousCategory = -1;
                for (uint32_t profileIndex = 1u;
                    profileIndex < static_cast<uint32_t>(
                        VisibilityPerformanceProfile::Count);
                    ++profileIndex)
                {
                    const auto profile =
                        static_cast<VisibilityPerformanceProfile>(profileIndex);
                    const VisibilityPerformanceProfileConfiguration
                        configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                profile);
                    int category = 0;
                    const char* categoryLabel = "Reference & Exact";
                    if (configuration.implementationStatus ==
                            VisibilityImplementationStatus::Unavailable ||
                        configuration.implementationStatus ==
                            VisibilityImplementationStatus::Unset)
                    {
                        category = 3;
                        categoryLabel = "Unavailable";
                    }
                    else if (configuration.benchmarkOnly ||
                        configuration.optimizationClass ==
                            VisibilityOptimizationClass::Diagnostic)
                    {
                        category = 2;
                        categoryLabel = "Diagnostics";
                    }
                    else if (configuration.optimizationClass ==
                            VisibilityOptimizationClass::Algorithmic ||
                        configuration.optimizationClass ==
                            VisibilityOptimizationClass::Numerical)
                    {
                        category = 1;
                        categoryLabel = "Performance Experiments";
                    }
                    if (category != previousCategory)
                    {
                        if (previousCategory >= 0)
                            ImGui::Separator();
                        ImGui::TextDisabled("%s", categoryLabel);
                        previousCategory = category;
                    }
                    const bool available =
                        configuration.implementationStatus !=
                            VisibilityImplementationStatus::Unavailable &&
                        configuration.implementationStatus !=
                            VisibilityImplementationStatus::Unset;
                    const bool selected =
                        profile == visibility.performanceProfile;
                    if (!available)
                        ImGui::BeginDisabled();
                    const std::string_view profileUiLabel =
                        GetPerformanceProfileUiLabel(profile);
                    if (ImGui::Selectable(
                            profileUiLabel.data(), selected) && available)
                    {
                        applyImplementationProfile(profile);
                    }
                    ImGui::SetItemTooltip(
                        "%s\n%s | %s%s%s",
                        configuration.name.data(),
                        GetOptimizationClassLabel(
                            configuration.optimizationClass),
                        GetImplementationStatusLabel(
                            configuration.implementationStatus),
                        configuration.implementationNote.empty() ? "" : ": ",
                        configuration.implementationNote.empty()
                            ? ""
                            : configuration.implementationNote.data());
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    if (!available)
                        ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Select any implemented profile. Diagnostics are test modes, "
                "not quality presets.");

            const float quickProfileWidth = std::max(
                1.f,
                (ImGui::GetContentRegionAvail().x -
                    style.ItemSpacing.x * 2.f) / 3.f);
            if (ImGui::Button(
                    "Reference", ImVec2(quickProfileWidth, 0.f)))
            {
                applyImplementationProfile(
                    VisibilityPerformanceProfile::Reference);
            }
            ImGui::SetItemTooltip("Restore the canonical reference path.");
            ImGui::SameLine();
            if (ImGui::Button(
                    "Exact Fast", ImVec2(quickProfileWidth, 0.f)))
            {
                applyImplementationProfile(
                    VisibilityPerformanceProfile::
                        ExactFixed8FusedResolveApply);
            }
            ImGui::SetItemTooltip(
                "Use fixed-eight tracing plus exact fused resolve-and-apply.");
            ImGui::SameLine();
            if (ImGui::Button(
                    "Fast Edges", ImVec2(quickProfileWidth, 0.f)))
            {
                applyImplementationProfile(
                    VisibilityPerformanceProfile::
                        AlgorithmicFusedPackedEdges2x2);
            }
            ImGui::SetItemTooltip(
                "Use the faster 2x2 packed-edge experiment.");

            const ImVec4 validColor = ImVec4(0.25f, 0.85f, 0.35f, 1.f);
            const ImVec4 invalidColor = ImVec4(1.f, 0.45f, 0.30f, 1.f);
            const std::string& displayedPermutation = visibilityTimings &&
                    !visibilityTimings->activePermutation.empty()
                ? visibilityTimings->activePermutation
                : activePlan.permutationName;
            const std::string_view runningProfileLabel =
                GetPerformanceProfileUiLabel(
                    visibility.performanceProfile);
            ImGui::TextColored(
                verificationProfileValid ? validColor : invalidColor,
                "%s: %s",
                verificationProfileValid ? "Ready" : "Not Ready",
                runningProfileLabel.empty()
                    ? "Waiting for GPU plan"
                    : runningProfileLabel.data());
            if (!displayedPermutation.empty())
            {
                ImGui::SetItemTooltip(
                    "Full GPU permutation:\n%s",
                    displayedPermutation.c_str());
            }
            if (!verificationProfileValid)
            {
                const char* invalidReason = nullptr;
                if (!visibilityAvailable)
                {
                    invalidReason =
                        "Deferred UVSR PBR rendering is unavailable.";
                }
                else if (!hasActiveVisibilityConsumer)
                {
                    invalidReason = "No AO or GI consumer is active.";
                }
                else if (!customImplementationProfile &&
                    visibility.performanceProfile !=
                    selectedDefinition.implementationProfile)
                {
                    invalidReason =
                        "The active implementation no longer matches the "
                        "selected verification profile.";
                }
                else if (!customImplementationProfile &&
                    !verificationResolution.valid)
                {
                    invalidReason = verificationResolution.reason.c_str();
                }
                else if (!verificationSettingsMismatch.empty())
                {
                    invalidReason = verificationSettingsMismatch.c_str();
                }
                else if (!activePlan.valid)
                {
                    invalidReason = activePlan.errorMessage.c_str();
                }
                else if (!visibilityTimings)
                {
                    invalidReason =
                        "The renderer has not exposed runtime timing state.";
                }
                else if (!visibilityTimings->profileValid)
                {
                    invalidReason =
                        visibilityTimings->profileError.empty()
                            ? "The renderer rejected the active profile."
                            : visibilityTimings->profileError.c_str();
                }
                else if (!runtimePermutationMatches)
                {
                    invalidReason =
                        "The renderer's active permutation does not match "
                        "the CPU-resolved profile plan yet.";
                }
                if (invalidReason)
                    ImGui::SetItemTooltip("%s", invalidReason);
            }
            if (!displayedPermutation.empty())
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(
                        "Copy Details##VisibilityPermutation"))
                {
                    ImGui::SetClipboardText(displayedPermutation.c_str());
                }
                ImGui::SetItemTooltip(
                    "Copy the full GPU permutation diagnostics.");
            }

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
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("Quality", qualityLabels[int(visibility.quality)]))
            {
                for (int qualityIndex = 0; qualityIndex < int(std::size(qualityLabels)); ++qualityIndex)
                {
                    const auto quality = ScreenSpaceVisibilityQuality(qualityIndex);
                    const bool selected = visibility.quality == quality;
                    if (ImGui::Selectable(qualityLabels[qualityIndex], selected))
                    {
                        ApplyScreenSpaceVisibilityQualityPreset(visibility, quality);
                        switchVisibilityToCustom();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Set the first-bounce sample range.");

            static const char* resolutionLabels[] = {
                "Full", "Half", "Quarter"
            };
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo(
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
                    {
                        visibility.resolution = resolution;
                        switchVisibilityToCustom();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the sampling resolution for screen-space visibility.");

            const char* activeAoMethod = "UVSR Visibility Bitmask";
            const bool activisionSourceProfile =
                visibility.performanceProfile ==
                    VisibilityPerformanceProfile::ActivisionClosestMatch ||
                visibility.performanceProfile ==
                    VisibilityPerformanceProfile::ActivisionPs4Schedule ||
                visibility.performanceProfile ==
                    VisibilityPerformanceProfile::ActivisionPs4PackedGather;
            const bool activisionPs4Profile =
                visibility.performanceProfile ==
                    VisibilityPerformanceProfile::ActivisionPs4Schedule ||
                visibility.performanceProfile ==
                    VisibilityPerformanceProfile::ActivisionPs4PackedGather;
            const bool xeGtaoSourceProfile =
                visibility.performanceProfile ==
                    VisibilityPerformanceProfile::XeGtaoClosestMatch ||
                visibility.performanceProfile ==
                    VisibilityPerformanceProfile::XeGtaoHighInlineHilbert ||
                visibility.performanceProfile ==
                    VisibilityPerformanceProfile::XeGtaoHighFp32;
            if (activisionSourceProfile || xeGtaoSourceProfile)
            {
                activeAoMethod = GetPerformanceProfileUiLabel(
                    visibility.performanceProfile).data();
            }
            else if (activeConfiguration.trace ==
                VisibilityTraceImplementation::ActivisionHorizon)
                activeAoMethod = "Analytic-Horizon AO";
            else if (activeConfiguration.trace ==
                VisibilityTraceImplementation::XeGtaoHorizon)
                activeAoMethod = "XeGTAO";
            else if (activeConfiguration.trace ==
                    VisibilityTraceImplementation::ConstantDiagnostic ||
                activeConfiguration.trace ==
                    VisibilityTraceImplementation::DepthOnlyDiagnostic ||
                activeConfiguration.trace ==
                    VisibilityTraceImplementation::BitmaskOnlyDiagnostic)
            {
                activeAoMethod = GetTraceLabel(activeConfiguration.trace);
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("AO Method", activeAoMethod))
            {
                const bool bitmaskSelected =
                    activeConfiguration.trace ==
                        VisibilityTraceImplementation::
                            LegacyGenericBitmask ||
                    activeConfiguration.trace ==
                        VisibilityTraceImplementation::
                            FixedInterleavedBitmask;
                if (ImGui::Selectable(
                        "UVSR Visibility Bitmask", bitmaskSelected))
                {
                    visibility.performanceProfile =
                        VisibilityPerformanceProfile::GenericFallback;
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                }
                const VisibilityPerformanceProfile methodProfiles[] = {
                    VisibilityPerformanceProfile::ActivisionClosestMatch,
                    VisibilityPerformanceProfile::ActivisionPs4Schedule,
                    VisibilityPerformanceProfile::ActivisionPs4PackedGather,
                    VisibilityPerformanceProfile::XeGtaoClosestMatch,
                    VisibilityPerformanceProfile::XeGtaoHighInlineHilbert,
                    VisibilityPerformanceProfile::XeGtaoHighFp32,
                    VisibilityPerformanceProfile::DiagnosticConstantTrace,
                    VisibilityPerformanceProfile::DiagnosticDepthOnlyTrace,
                    VisibilityPerformanceProfile::DiagnosticBitmaskOnlyTrace
                };
                for (VisibilityPerformanceProfile profile : methodProfiles)
                {
                    if (profile == VisibilityPerformanceProfile::
                        DiagnosticConstantTrace)
                    {
                        ImGui::Separator();
                        ImGui::TextDisabled("Diagnostics");
                    }
                    const auto configuration =
                        GetVisibilityPerformanceProfileConfiguration(profile);
                    const bool available =
                        configuration.implementationStatus !=
                            VisibilityImplementationStatus::Unavailable &&
                        configuration.implementationStatus !=
                            VisibilityImplementationStatus::Unset;
                    if (!available)
                        ImGui::BeginDisabled();
                    const std::string_view profileUiLabel =
                        GetPerformanceProfileUiLabel(profile);
                    if (ImGui::Selectable(
                            profileUiLabel.data(),
                            visibility.performanceProfile == profile) &&
                        available)
                    {
                        applyImplementationProfile(profile);
                    }
                    ImGui::SetItemTooltip(
                        "%s%s%s",
                        GetImplementationStatusLabel(
                            configuration.implementationStatus),
                        configuration.implementationNote.empty()
                            ? ""
                            : ": ",
                        configuration.implementationNote.empty()
                            ? ""
                            : configuration.implementationNote.data());
                    if (!available)
                        ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose UVSR bitmask visibility or a complete source-"
                "inspired AO pipeline.");

            if (!visibility.enabled)
                ImGui::BeginDisabled();

            if (ImGui::TreeNodeEx("Shared Visibility Sampling", ImGuiTreeNodeFlags_DefaultOpen))
            {
                SharedSamplingSettings& sampling = visibility.sampling;
                bool samplingChanged = false;
                static const char* estimatorLabels[] = {
                    "Uniform Projected Angle",
                    "Uniform Solid Angle",
                    "Cosine-Weighted Solid Angle"
                };
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
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
                        {
                            visibility.estimator = estimator;
                            samplingChanged = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("Choose how samples spread around each pixel.");

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
                        "Trace Specialization",
                        GetSampleSpecializationLabel(
                            activeConfiguration.firstBounceSamples)))
                {
                    if (ImGui::Selectable(
                            "Generic",
                            activeConfiguration.firstBounceSamples ==
                                VisibilitySampleSpecialization::Runtime))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                    }
                    const VisibilityPerformanceProfile fixedProfiles[] = {
                        VisibilityPerformanceProfile::ExactFixed8,
                        VisibilityPerformanceProfile::ExactFixed12,
                        VisibilityPerformanceProfile::ExactFixed16,
                        VisibilityPerformanceProfile::ExactFixed20
                    };
                    for (VisibilityPerformanceProfile profile : fixedProfiles)
                    {
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                profile);
                        if (ImGui::Selectable(
                                configuration.name.data(),
                                visibility.performanceProfile == profile))
                        {
                            applyImplementationProfile(profile);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose a generic trace or an exact fixed sample count.");

                samplingChanged |= ImGui::SliderFloat(
                    "Radius", &sampling.radius, 0.01f, std::max(m_app->GetSceneDiagonal() * 0.1f, 1.f), "%.3f");
                ImGui::SetItemTooltip("Set how far visibility rays reach.");
                samplingChanged |= ImGui::SliderFloat(
                    "Thickness", &sampling.thickness, 0.0f, std::max(m_app->GetSceneDiagonal() * 0.02f, 0.5f), "%.3f");
                ImGui::SetItemTooltip("Set the assumed thickness of occluders.");

                const char* radiusLimitLabel = "Unlimited";
                if (visibility.performanceProfile ==
                    VisibilityPerformanceProfile::
                        AlgorithmicProjectedRadiusClamp32)
                    radiusLimitLabel = "32 Pixels";
                else if (visibility.performanceProfile ==
                    VisibilityPerformanceProfile::
                        AlgorithmicProjectedRadiusClamp64)
                    radiusLimitLabel = "64 Pixels";
                else if (visibility.performanceProfile ==
                    VisibilityPerformanceProfile::
                        AlgorithmicProjectedRadiusClamp128)
                    radiusLimitLabel = "128 Pixels";
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
                        "Screen-Radius Limit", radiusLimitLabel))
                {
                    if (ImGui::Selectable(
                            "Unlimited",
                            std::strcmp(radiusLimitLabel, "Unlimited") == 0))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                    }
                    const VisibilityPerformanceProfile clampProfiles[] = {
                        VisibilityPerformanceProfile::
                            AlgorithmicProjectedRadiusClamp32,
                        VisibilityPerformanceProfile::
                            AlgorithmicProjectedRadiusClamp64,
                        VisibilityPerformanceProfile::
                            AlgorithmicProjectedRadiusClamp128
                    };
                    for (VisibilityPerformanceProfile profile : clampProfiles)
                    {
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                profile);
                        if (ImGui::Selectable(
                                configuration.name.data(),
                                visibility.performanceProfile == profile))
                        {
                            applyImplementationProfile(profile);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Limit projected trace radius for cache experiments.");

                samplingChanged |= ImGui::Checkbox(
                    "Adaptive Sparse Sampling",
                    &sampling.adaptiveSparseSamplingEnabled);
                ImGui::SetItemTooltip(
                    "Spend more samples where the image is harder to resolve.");

                if (sampling.adaptiveSparseSamplingEnabled)
                {
                    int minimumSamples = int(std::clamp(
                        sampling.minimumSampleCount, 1u, 64u));
                    if (ImGui::SliderInt(
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
                    if (ImGui::SliderInt(
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
                    samplingChanged |= ImGui::SliderFloat(
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
                    if (ImGui::SliderInt(
                            "Fixed Samples Per Pixel",
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

                samplingChanged |= ImGui::SliderFloat(
                    "Radial Distribution Exponent",
                    &sampling.stepDistributionExponent,
                    0.5f,
                    4.0f,
                    "%.2f");
                ImGui::SetItemTooltip("Higher values place more samples nearby.");

                static const char* schedulerLabels[] = {
                    "Independent Hash",
                    "Toroidal Blue Noise",
                    "Filter-Adapted Blue Noise"
                };
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
                        "Sample Pattern",
                        GetSchedulerLabel(observedWorkload.scheduler)))
                {
                    for (int index = 0;
                        index < int(std::size(schedulerLabels));
                        ++index)
                    {
                        const auto scheduler =
                            VisibilitySampleScheduler(index);
                        const bool selected =
                            activeConfiguration.noise ==
                                VisibilityNoiseDelivery::Legacy &&
                            sampling.scheduler == scheduler;
                        if (ImGui::Selectable(
                                schedulerLabels[index], selected))
                        {
                            sampling.scheduler = scheduler;
                            samplingChanged = true;
                            visibility.performanceProfile =
                                VisibilityPerformanceProfile::GenericFallback;
                            m_ui.VisibilityVerification =
                                VisibilityVerificationProfile::Unset;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose the spatial and temporal sample sequence. "
                    "Source profiles lock this to their required schedule.");

                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
                        "Noise Delivery",
                        GetNoiseDeliveryLabel(activeConfiguration.noise)))
                {
                    if (ImGui::Selectable(
                            "Reference Scheduler Resources",
                            activeConfiguration.noise ==
                                VisibilityNoiseDelivery::Legacy))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("Optimized & Source Profiles");
                    const VisibilityPerformanceProfile noiseProfiles[] = {
                        VisibilityPerformanceProfile::ExactPackedCurrentFast,
                        VisibilityPerformanceProfile::
                            AlgorithmicActivisionSchedule,
                        VisibilityPerformanceProfile::
                            ActivisionPs4Schedule,
                        VisibilityPerformanceProfile::
                            ActivisionPs4PackedGather,
                        VisibilityPerformanceProfile::
                            DiagnosticConstantScheduler,
                        VisibilityPerformanceProfile::XeGtaoClosestMatch,
                        VisibilityPerformanceProfile::
                            XeGtaoHighInlineHilbert,
                        VisibilityPerformanceProfile::XeGtaoHighFp32
                    };
                    for (VisibilityPerformanceProfile profile : noiseProfiles)
                    {
                        const VisibilityPerformanceProfileConfiguration
                            configuration =
                                GetVisibilityPerformanceProfileConfiguration(
                                    profile);
                        const bool available =
                            configuration.implementationStatus !=
                                VisibilityImplementationStatus::Unavailable &&
                            configuration.implementationStatus !=
                                VisibilityImplementationStatus::Unset;
                        if (!available)
                            ImGui::BeginDisabled();
                        const bool selected =
                            visibility.performanceProfile == profile;
                        const std::string_view profileUiLabel =
                            GetPerformanceProfileUiLabel(profile);
                        if (ImGui::Selectable(
                                profileUiLabel.data(), selected) &&
                            available)
                        {
                            applyImplementationProfile(profile);
                        }
                        ImGui::SetItemTooltip(
                            "%s%s%s",
                            GetImplementationStatusLabel(
                                configuration.implementationStatus),
                            configuration.implementationNote.empty()
                                ? ""
                                : ": ",
                            configuration.implementationNote.empty()
                                ? ""
                                : configuration.implementationNote.data());
                        if (!available)
                            ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose noise resources or a source profile. XeGTAO "
                    "Hilbert LUT and inline Hilbert are separate choices.");

                if (samplingChanged)
                {
                    visibility.quality = ScreenSpaceVisibilityQuality::Custom;
                    visibility.performanceProfile =
                        VisibilityPerformanceProfile::GenericFallback;
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen))
            {
                AmbientOcclusionSettings& ao = visibility.ambientOcclusion;
                bool aoChanged = ImGui::Checkbox(
                    "Enabled##AmbientVisibility", &ao.enabled);
                ImGui::SetItemTooltip("Enable screen-space ambient occlusion.");
                if (!ao.enabled)
                    ImGui::BeginDisabled();
                aoChanged |= ImGui::SliderFloat(
                    "Strength", &ao.strength, 0.0f, 2.0f, "%.2f");
                ImGui::SetItemTooltip("Set how strongly AO darkens indirect light.");
                if (!ao.enabled)
                    ImGui::EndDisabled();
                if (aoChanged)
                    switchVisibilityToCustom();
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Indirect Diffuse", ImGuiTreeNodeFlags_DefaultOpen))
            {
                IndirectDiffuseSettings& gi = visibility.indirectDiffuse;
                bool giChanged = ImGui::Checkbox(
                    "Enabled##IndirectDiffuse", &gi.enabled);
                ImGui::SetItemTooltip("Enable screen-space diffuse indirect light.");
                if (!gi.enabled)
                    ImGui::BeginDisabled();
                int bounceCount = int(std::clamp(
                    gi.bounceCount, 1u, MaxIndirectDiffuseBounceCount));
                if (ImGui::SliderInt(
                        "Bounces##IndirectDiffuse",
                        &bounceCount,
                        1,
                        int(MaxIndirectDiffuseBounceCount),
                        "%d",
                        ImGuiSliderFlags_AlwaysClamp))
                {
                    gi.bounceCount = uint32_t(bounceCount);
                    giChanged = true;
                }
                ImGui::SetItemTooltip("Set the number of diffuse-light bounces.");
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
                        "Later-Bounce Trace",
                        GetSampleSpecializationLabel(
                            activeConfiguration.laterBounceSamples)))
                {
                    if (ImGui::Selectable(
                            "Generic",
                            activeConfiguration.laterBounceSamples ==
                                VisibilitySampleSpecialization::Runtime))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                    }
                    const VisibilityPerformanceProfile laterProfiles[] = {
                        VisibilityPerformanceProfile::
                            ExactFixedLaterBounce8,
                        VisibilityPerformanceProfile::
                            ExactFixedAllBounce8
                    };
                    for (VisibilityPerformanceProfile profile : laterProfiles)
                    {
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                profile);
                        if (ImGui::Selectable(
                                configuration.name.data(),
                                visibility.performanceProfile == profile))
                        {
                            applyImplementationProfile(profile);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose generic or fixed-eight higher-bounce tracing.");
                if (gi.bounceCount > 1u)
                {
                    giChanged |= ImGui::SliderFloat(
                        "Bounce Contribution Cutoff",
                        &gi.minimumBounceContribution,
                        0.0f,
                        0.02f,
                        "%.5f");
                    ImGui::SetItemTooltip(
                        "Skip dim higher-bounce light. Zero disables the cutoff.");
                }
                giChanged |= ImGui::SliderFloat(
                    "Intensity##IndirectDiffuse",
                    &gi.intensity,
                    0.0f,
                    10.0f,
                    "%.2f");
                ImGui::SetItemTooltip("Set screen-space diffuse GI brightness.");
                giChanged |= ImGui::Checkbox(
                    "Include Emissive Sources", &gi.includeEmissive);
                ImGui::SetItemTooltip("Let visible emissive surfaces light the scene.");
                if (!gi.includeEmissive)
                    ImGui::BeginDisabled();
                giChanged |= ImGui::SliderFloat(
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
                if (giChanged)
                    switchVisibilityToCustom();
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Temporal Reconstruction"))
            {
                VisibilityReconstructionSettings& reconstruction =
                    visibility.reconstruction;
                if (ImGui::Checkbox(
                        "Enabled##TemporalReconstruction",
                        &reconstruction.temporalEnabled))
                {
                    visibility.performanceProfile =
                        VisibilityPerformanceProfile::GenericFallback;
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                }
                ImGui::SetItemTooltip(
                    "Reuse valid detail from earlier frames.");
                if (!reconstruction.temporalEnabled)
                    ImGui::BeginDisabled();
                const char* temporalMethodLabel =
                    DescribeVisibilityTemporal(activeConfiguration.temporal);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
                        "Temporal Method",
                        temporalMethodLabel))
                {
                    if (ImGui::Selectable(
                            "Standard Reprojection",
                            activeConfiguration.temporal ==
                                VisibilityTemporalMode::Legacy))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                        reconstruction.temporalEnabled = true;
                    }
                    const VisibilityPerformanceProfile temporalProfiles[] = {
                        VisibilityPerformanceProfile::DiagnosticTemporalCopy,
                        VisibilityPerformanceProfile::ActivisionPs4Schedule,
                        VisibilityPerformanceProfile::
                            ActivisionPs4PackedGather
                    };
                    for (VisibilityPerformanceProfile profile :
                        temporalProfiles)
                    {
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                profile);
                        const bool available =
                            configuration.implementationStatus !=
                                VisibilityImplementationStatus::Unavailable &&
                            configuration.implementationStatus !=
                                VisibilityImplementationStatus::Unset;
                        if (!available)
                            ImGui::BeginDisabled();
                        const std::string_view profileUiLabel =
                            GetPerformanceProfileUiLabel(profile);
                        if (ImGui::Selectable(
                                profileUiLabel.data(),
                                visibility.performanceProfile == profile) &&
                            available)
                        {
                            applyImplementationProfile(profile);
                        }
                        if (!available)
                            ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose temporal reprojection or a complete source-"
                    "inspired temporal profile.");
                ImGui::SliderFloat(
                    "Current-Frame Weight",
                    &reconstruction.temporalResponse,
                    0.05f,
                    1.0f,
                    "%.2f");
                ImGui::SetItemTooltip(
                    "Higher values follow the current frame faster.");
                if (!reconstruction.temporalEnabled)
                    ImGui::EndDisabled();
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Spatial Denoising & Upsampling"))
            {
                VisibilityReconstructionSettings& reconstruction =
                    visibility.reconstruction;
                const char* denoiserLabel = GetReconstructionLabel(
                    activeConfiguration.reconstruction);
                if (activeConfiguration.reconstruction ==
                    VisibilityReconstructionMode::Legacy)
                {
                    denoiserLabel = reconstruction.spatialEnabled
                        ? (reconstruction.spatialFilter ==
                                VisibilitySpatialFilter::
                                    GaussianJointBilateral
                            ? "Gaussian Joint Bilateral"
                            : "Joint Bilateral")
                        : "Required Upsample";
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
                        "Spatial Denoiser", denoiserLabel))
                {
                    if (ImGui::Selectable(
                            "Required Upsample",
                            activeConfiguration.reconstruction ==
                                    VisibilityReconstructionMode::Legacy &&
                                !reconstruction.spatialEnabled))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                        reconstruction.spatialEnabled = false;
                    }
                    if (ImGui::Selectable(
                            "Joint Bilateral",
                            activeConfiguration.reconstruction ==
                                    VisibilityReconstructionMode::Legacy &&
                                reconstruction.spatialEnabled &&
                                reconstruction.spatialFilter ==
                                    VisibilitySpatialFilter::JointBilateral))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                        reconstruction.spatialEnabled = true;
                        reconstruction.spatialFilter =
                            VisibilitySpatialFilter::JointBilateral;
                    }
                    if (ImGui::Selectable(
                            "Gaussian Joint Bilateral",
                            activeConfiguration.reconstruction ==
                                    VisibilityReconstructionMode::Legacy &&
                                reconstruction.spatialEnabled &&
                                reconstruction.spatialFilter ==
                                    VisibilitySpatialFilter::
                                        GaussianJointBilateral))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                        reconstruction.spatialEnabled = true;
                        reconstruction.spatialFilter =
                            VisibilitySpatialFilter::
                                GaussianJointBilateral;
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("Packed & Source-Inspired");
                    const VisibilityPerformanceProfile denoiserProfiles[] = {
                        VisibilityPerformanceProfile::
                            AlgorithmicPackedEdges2x2,
                        VisibilityPerformanceProfile::
                            AlgorithmicPackedEdgesDepthNormal2x2,
                        VisibilityPerformanceProfile::
                            AlgorithmicPackedEdgesSlope2x2,
                        VisibilityPerformanceProfile::
                            AlgorithmicPackedEdgesLeakage2x2,
                        VisibilityPerformanceProfile::
                            AlgorithmicPackedEdges4x4,
                        VisibilityPerformanceProfile::
                            DiagnosticNearestResolve,
                        VisibilityPerformanceProfile::
                            DiagnosticBilinearResolve,
                        VisibilityPerformanceProfile::
                            ActivisionPs4Schedule,
                        VisibilityPerformanceProfile::
                            ActivisionPs4PackedGather,
                        VisibilityPerformanceProfile::XeGtaoClosestMatch,
                        VisibilityPerformanceProfile::
                            XeGtaoHighInlineHilbert,
                        VisibilityPerformanceProfile::XeGtaoHighFp32
                    };
                    for (VisibilityPerformanceProfile profile :
                        denoiserProfiles)
                    {
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                profile);
                        const bool available =
                            configuration.implementationStatus !=
                                VisibilityImplementationStatus::Unavailable &&
                            configuration.implementationStatus !=
                                VisibilityImplementationStatus::Unset;
                        if (!available)
                            ImGui::BeginDisabled();
                        const std::string_view profileUiLabel =
                            GetPerformanceProfileUiLabel(profile);
                        if (ImGui::Selectable(
                                profileUiLabel.data(),
                                visibility.performanceProfile == profile) &&
                            available)
                        {
                            applyImplementationProfile(profile);
                        }
                        ImGui::SetItemTooltip(
                            "%s%s%s",
                            GetImplementationStatusLabel(
                                configuration.implementationStatus),
                            configuration.implementationNote.empty()
                                ? ""
                                : ": ",
                            configuration.implementationNote.empty()
                                ? ""
                                : configuration.implementationNote.data());
                        if (!available)
                            ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose the upsample or spatial denoiser used by the "
                    "effective GPU plan.");
                if (activeConfiguration.reconstruction ==
                        VisibilityReconstructionMode::Legacy &&
                    reconstruction.spatialEnabled &&
                    reconstruction.spatialFilter ==
                        VisibilitySpatialFilter::GaussianJointBilateral)
                {
                    ImGui::SliderFloat(
                        "Gaussian Radius",
                        &reconstruction.spatialRadius,
                        1.0f,
                        12.0f,
                        "%.1f");
                    ImGui::SetItemTooltip(
                        "Set how far the Gaussian denoiser reaches.");
                }

                ImGui::SetNextItemWidth(-FLT_MIN);
                const char* applicationLabel =
                    visibility.performanceProfile ==
                            VisibilityPerformanceProfile::
                                DiagnosticFusedFullResolutionAoOutput
                        ? GetPerformanceProfileUiLabel(
                            visibility.performanceProfile).data()
                        : GetApplicationLabel(
                            activeConfiguration.application);
                if (ImGui::BeginCombo(
                        "Resolve & Apply",
                        applicationLabel))
                {
                    if (ImGui::Selectable(
                            "Separate Resolve & Composition",
                            activeConfiguration.application ==
                                VisibilityApplicationMode::
                                    LegacySeparateComposition &&
                                visibility.performanceProfile !=
                                    VisibilityPerformanceProfile::
                                        DiagnosticFusedFullResolutionAoOutput))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                    }
                    const VisibilityPerformanceProfile applicationProfiles[] = {
                        VisibilityPerformanceProfile::
                            ExactFusedResolveApply,
                        VisibilityPerformanceProfile::
                            ExactFixed8FusedResolveApply,
                        VisibilityPerformanceProfile::
                            AlgorithmicFusedPackedEdges2x2,
                        VisibilityPerformanceProfile::
                            AlgorithmicFusedPackedEdges4x4,
                        VisibilityPerformanceProfile::
                            DiagnosticFusedFullResolutionAoOutput,
                        VisibilityPerformanceProfile::
                            DiagnosticCompositionOnly,
                        VisibilityPerformanceProfile::
                            DiagnosticCompositionBypass
                    };
                    for (VisibilityPerformanceProfile profile :
                        applicationProfiles)
                    {
                        if (profile == VisibilityPerformanceProfile::
                            DiagnosticFusedFullResolutionAoOutput)
                        {
                            ImGui::Separator();
                            ImGui::TextDisabled("Diagnostics");
                        }
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                profile);
                        const std::string_view profileUiLabel =
                            GetPerformanceProfileUiLabel(profile);
                        if (ImGui::Selectable(
                                profileUiLabel.data(),
                                visibility.performanceProfile == profile))
                        {
                            applyImplementationProfile(profile);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose separate composition or a fused resolve-and-"
                    "apply dispatch. Fused work is timed as one pass.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Performance"))
            {
                const char* mathControlLabel = xeGtaoSourceProfile
                    ? "Precision"
                    : "Math";
                const char* mathValueLabel = xeGtaoSourceProfile
                    ? (visibility.performanceProfile ==
                            VisibilityPerformanceProfile::XeGtaoHighFp32
                        ? "XeGTAO FP32"
                        : "XeGTAO Mixed Precision")
                    : GetMathModeLabel(activeConfiguration.math);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
                        mathControlLabel,
                        mathValueLabel))
                {
                    if (xeGtaoSourceProfile)
                    {
                        if (ImGui::Selectable(
                                "Mixed Precision",
                                activeConfiguration.math ==
                                    VisibilityMathMode::
                                        XeGtaoMixedPrecision))
                        {
                            applyImplementationProfile(
                                visibility.performanceProfile ==
                                        VisibilityPerformanceProfile::
                                            XeGtaoHighInlineHilbert
                                    ? VisibilityPerformanceProfile::
                                        XeGtaoHighInlineHilbert
                                    : VisibilityPerformanceProfile::
                                        XeGtaoClosestMatch);
                        }
                        if (ImGui::Selectable(
                                "FP32 (Hilbert LUT)",
                                visibility.performanceProfile ==
                                    VisibilityPerformanceProfile::
                                        XeGtaoHighFp32))
                        {
                            applyImplementationProfile(
                                VisibilityPerformanceProfile::
                                    XeGtaoHighFp32);
                        }
                    }
                    else
                    {
                        if (ImGui::Selectable(
                                "Reference FP32",
                                activeConfiguration.math ==
                                    VisibilityMathMode::ReferenceFp32) &&
                            !activisionSourceProfile)
                        {
                            visibility.performanceProfile =
                                VisibilityPerformanceProfile::GenericFallback;
                            m_ui.VisibilityVerification =
                                VisibilityVerificationProfile::Unset;
                        }
                        const auto conservativeConfiguration =
                            GetVisibilityPerformanceProfileConfiguration(
                                VisibilityPerformanceProfile::
                                    ConservativeNumerical);
                        if (ImGui::Selectable(
                                conservativeConfiguration.name.data(),
                                visibility.performanceProfile ==
                                    VisibilityPerformanceProfile::
                                        ConservativeNumerical))
                        {
                            applyImplementationProfile(
                                VisibilityPerformanceProfile::
                                    ConservativeNumerical);
                        }
                        ImGui::BeginDisabled();
                        ImGui::Selectable(
                            "UVSR Bitmask Mixed Precision (Unavailable)",
                            false);
                        ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    xeGtaoSourceProfile
                        ? "Choose XeGTAO mixed precision or its LUT-based "
                            "FP32 control without leaving the XeGTAO family."
                        : "Choose reference FP32 or a compiled math "
                            "experiment.");

                const char* groupLabel = "8 x 8";
                if (visibility.performanceProfile ==
                    VisibilityPerformanceProfile::ExactGroup16x8)
                    groupLabel = "16 x 8";
                else if (visibility.performanceProfile ==
                    VisibilityPerformanceProfile::ExactGroup8x16)
                    groupLabel = "8 x 16";
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo(
                        "Trace Thread Group", groupLabel))
                {
                    if (ImGui::Selectable(
                            "8 x 8",
                            std::strcmp(groupLabel, "8 x 8") == 0))
                    {
                        visibility.performanceProfile =
                            VisibilityPerformanceProfile::GenericFallback;
                        m_ui.VisibilityVerification =
                            VisibilityVerificationProfile::Unset;
                    }
                    const VisibilityPerformanceProfile groupProfiles[] = {
                        VisibilityPerformanceProfile::ExactGroup16x8,
                        VisibilityPerformanceProfile::ExactGroup8x16
                    };
                    for (VisibilityPerformanceProfile profile : groupProfiles)
                    {
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                profile);
                        if (ImGui::Selectable(
                                configuration.name.data(),
                                visibility.performanceProfile == profile))
                        {
                            applyImplementationProfile(profile);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose a compiled trace thread-group shape.");

                const float experimentButtonWidth = std::max(
                    1.f,
                    (ImGui::GetContentRegionAvail().x -
                        style.ItemSpacing.x) / 2.f);
                if (ImGui::Button(
                        "Duplicate Check Off",
                        ImVec2(experimentButtonWidth, 0.f)))
                {
                    applyImplementationProfile(
                        VisibilityPerformanceProfile::
                            ExactDuplicatePixelRejectionOff);
                }
                ImGui::SetItemTooltip(
                    "Diagnostic: disable duplicate-sample rejection.");
                ImGui::SameLine();
                if (ImGui::Button(
                        "Mask Exit Off",
                        ImVec2(experimentButtonWidth, 0.f)))
                {
                    applyImplementationProfile(
                        VisibilityPerformanceProfile::
                            ExactFullMaskEarlyExitOff);
                }
                ImGui::SetItemTooltip(
                    "Diagnostic: disable the full-mask early exit.");
                ImGui::TreePop();
            }

            if (!visibility.enabled)
                ImGui::EndDisabled();
            if (visibilityBenchmarkBusy)
                ImGui::EndDisabled();
            if (!visibilityAvailable)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Requires deferred UVSR PBR rendering.");
            }
        }

        const bool visibilityStatisticsOpen = DrawCollapsingHeader(
            "AO Statistics",
            "Show one GPU timing or resource concept per row.");
        if (visibilityStatisticsOpen)
        {
            const ScreenSpaceVisibilitySettings& statsVisibility =
                m_ui.ScreenSpaceVisibility;
            const ScreenSpaceVisibilityTimings* timings =
                m_app->GetScreenSpaceVisibilityTimings();
            const VisibilityPerformanceWorkload statsWorkload =
                BuildVisibilityPerformanceWorkload(
                    statsVisibility,
                    uint32_t(std::max(width, 0)),
                    uint32_t(std::max(height, 0)));
            const VisibilityPerformanceProfileConfiguration statsConfig =
                GetVisibilityPerformanceProfileConfiguration(
                    statsVisibility.performanceProfile);
            const VisibilityExecutionPlan statsPlan =
                ResolveVisibilityExecutionPlan(
                    statsVisibility.performanceProfile,
                    statsWorkload);

            if (!timings)
            {
                ImGui::TextDisabled("Waiting for GPU timing data.");
            }
            else if (ImGui::BeginTable(
                    "##VisibilityLiveTimings",
                    2,
                    ImGuiTableFlags_BordersInnerH |
                        ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn(
                    "GPU Stage",
                    ImGuiTableColumnFlags_WidthStretch,
                    3.f);
                ImGui::TableSetupColumn(
                    "Current",
                    ImGuiTableColumnFlags_WidthStretch,
                    1.f);
                ImGui::TableHeadersRow();
                auto drawTimingRow =
                    [](const char* label, float milliseconds, bool active)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (active)
                            ImGui::TextUnformatted(label);
                        else
                            ImGui::TextDisabled("%s", label);
                        ImGui::TableSetColumnIndex(1);
                        if (active)
                            ImGui::Text("%.3f ms", milliseconds);
                        else
                            ImGui::TextDisabled("--");
                    };

                const bool depthActive = statsPlan.valid &&
                    HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::DepthPreparation);
                const bool firstTraceActive = statsPlan.valid &&
                    statsVisibility.HasActiveConsumer();
                const bool laterTraceActive = statsPlan.valid &&
                    (HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::LegacyLaterBounceTrace) ||
                     HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::FixedLaterBounceTrace));
                const bool reconstructionActive = statsPlan.valid &&
                    HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::Reconstruction);
                const bool dedicatedSpatialDenoise = statsPlan.valid &&
                    HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::SpatialDenoise);
                const bool fusedSpatialDenoiseUpsample =
                    reconstructionActive &&
                    !dedicatedSpatialDenoise &&
                    statsWorkload.spatialEnabled;
                const bool requiredUpsample =
                    reconstructionActive &&
                    !fusedSpatialDenoiseUpsample &&
                    (!dedicatedSpatialDenoise ||
                        statsWorkload.resolution !=
                            VisibilityPerformanceResolution::Full);
                const bool temporalActive = statsPlan.valid &&
                    HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::Temporal);
                const bool applyActive = statsPlan.valid &&
                    HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::FusedResolveAndApply);
                const bool compositionActive = statsPlan.valid &&
                    (HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::Composition) ||
                     statsConfig.application ==
                        VisibilityApplicationMode::
                            BypassCompositionDiagnostic);

                drawTimingRow(
                    "Complete Visibility Pipeline",
                    timings->CompleteEffectMs(),
                    statsPlan.valid);
                drawTimingRow(
                    "Depth Preparation",
                    timings->depthHierarchyMs,
                    depthActive);
                drawTimingRow(
                    "First-Bounce Visibility Trace",
                    timings->firstTraceMs,
                    firstTraceActive);
                drawTimingRow(
                    "Later-Bounce GI Trace",
                    timings->laterTraceMs,
                    laterTraceActive);
                drawTimingRow(
                    "  GI Bounce 2 (Inside Later Trace)",
                    timings->laterBounceMs[0],
                    laterTraceActive && statsWorkload.bounceCount >= 2u);
                drawTimingRow(
                    "  GI Bounce 3 (Inside Later Trace)",
                    timings->laterBounceMs[1],
                    laterTraceActive && statsWorkload.bounceCount >= 3u);
                drawTimingRow(
                    "  GI Bounce 4 (Inside Later Trace)",
                    timings->laterBounceMs[2],
                    laterTraceActive && statsWorkload.bounceCount >= 4u);
                drawTimingRow(
                    "Spatial Denoise",
                    timings->spatialDenoiseMs,
                    dedicatedSpatialDenoise);
                drawTimingRow(
                    "Temporal Reconstruction",
                    timings->temporalMs,
                    temporalActive);
                drawTimingRow(
                    "Fused Spatial Denoise & Upsample",
                    timings->fusedSpatialDenoiseUpsampleMs,
                    fusedSpatialDenoiseUpsample);
                drawTimingRow(
                    "Required Full-Resolution Upsample",
                    timings->requiredUpsampleMs,
                    requiredUpsample);
                drawTimingRow(
                    "Fused Resolve & Apply",
                    timings->fullResolutionApplyMs,
                    applyActive);
                drawTimingRow(
                    statsConfig.application ==
                            VisibilityApplicationMode::
                                BypassCompositionDiagnostic
                        ? "Composition Bypass Copy"
                        : "Indirect-Lighting Composition",
                    timings->compositionMs,
                    compositionActive);
                drawTimingRow(
                    "Named-Stage Total",
                    timings->SummedStageMs(),
                    statsPlan.valid);
                drawTimingRow(
                    "Unattributed Timer Difference",
                    timings->CompleteEffectMs() -
                        timings->SummedStageMs(),
                    statsPlan.valid);
                ImGui::EndTable();
            }

            if (timings && ImGui::TreeNodeEx("Resource Footprint"))
            {
                constexpr double BytesPerMiB = 1024.0 * 1024.0;
                if (ImGui::BeginTable(
                        "##VisibilityResources",
                        2,
                        ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn(
                        "Resource",
                        ImGuiTableColumnFlags_WidthStretch,
                        3.f);
                    ImGui::TableSetupColumn(
                        "Value",
                        ImGuiTableColumnFlags_WidthStretch,
                        1.f);
                    ImGui::TableHeadersRow();
                    auto drawMibRow =
                        [BytesPerMiB](const char* label, uint64_t bytes)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(label);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text(
                                "%.2f MiB",
                                double(bytes) / BytesPerMiB);
                        };
                    auto drawAllocatedMibRow =
                        [&drawMibRow](const char* label, uint64_t bytes)
                        {
                            if (bytes != 0u)
                                drawMibRow(label, bytes);
                        };
                    auto drawSectionRow = [](const char* label)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextDisabled("%s", label);
                        };
                    auto drawCountRow =
                        [](const char* label, uint32_t value)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(label);
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%u", value);
                        };
                    drawMibRow(
                        "Output Texture Total (Logical)",
                        timings->outputTextureBytes);
                    drawSectionRow("Output Textures");
                    drawAllocatedMibRow(
                        "  Raw AO Surface",
                        timings->rawAmbientTextureBytes);
                    drawAllocatedMibRow(
                        "  GI Frontier",
                        timings->rawIndirectFrontierBytes);
                    drawAllocatedMibRow(
                        "  Multi-Bounce GI Frontiers",
                        timings->multiBounceIndirectBytes);
                    drawAllocatedMibRow(
                        "  Final AO Surface",
                        timings->finalAmbientTextureBytes);
                    drawAllocatedMibRow(
                        "  Final GI Surface",
                        timings->finalIndirectTextureBytes);
                    drawMibRow(
                        "Working Texture Total (Logical)",
                        timings->workingTextureBytes);
                    drawSectionRow("Shared And History Textures");
                    drawAllocatedMibRow(
                        "  Scheduler Noise Tables",
                        timings->schedulerResourceBytes);
                    drawAllocatedMibRow(
                        "  Adaptive Feedback",
                        timings->adaptiveFeedbackBytes);
                    drawAllocatedMibRow(
                        "  Temporal AO History",
                        timings->temporalAmbientHistoryBytes);
                    drawAllocatedMibRow(
                        "  Temporal GI History",
                        timings->temporalIndirectHistoryBytes);
                    drawAllocatedMibRow(
                        "  Temporal Depth History",
                        timings->temporalDepthHistoryBytes);
                    drawAllocatedMibRow(
                        "  Temporal Normal History",
                        timings->temporalNormalHistoryBytes);
                    drawAllocatedMibRow(
                        "  Depth Hierarchy",
                        timings->depthHierarchyBytes);
                    drawMibRow(
                        "Optional Candidate Total",
                        timings->optionalTextureBytes);
                    drawSectionRow("Candidate Textures");
                    drawAllocatedMibRow(
                        "  Packed FAST Noise",
                        timings->packedFastNoiseBytes);
                    drawAllocatedMibRow(
                        "  Packed Edge Metadata",
                        timings->packedEdgeMetadataBytes);
                    drawAllocatedMibRow(
                        "  PS4 GTAO Working Surfaces",
                        timings->activisionWorkingBytes);
                    drawAllocatedMibRow(
                        "  XeGTAO Working AO",
                        timings->xeGtaoWorkingAoBytes);
                    drawAllocatedMibRow(
                        "  XeGTAO Edge Metadata",
                        timings->xeGtaoEdgeBytes);
                    drawAllocatedMibRow(
                        "  XeGTAO Hilbert LUT",
                        timings->xeGtaoHilbertLutBytes);
                    drawSectionRow("Modeled Savings");
                    drawMibRow(
                        "Mask Cache Payload",
                        timings->maskCacheBytes);
                    drawMibRow(
                        "Fusion-Eligible AO Intermediate",
                        timings->fullResolutionIntermediateBytes);
                    drawMibRow(
                        "Avoided Allocation (Modeled)",
                        timings->avoidedTextureBytes);
                    drawMibRow(
                        "Shared Mask Payload Avoided (Modeled)",
                        timings->sharedMaskPayloadBytes);
                    drawMibRow(
                        "Logical Traffic Avoided (Modeled)",
                        timings->logicalTrafficAvoidedBytes);
                    drawCountRow(
                        "First-Trace SRVs",
                        timings->activeSrvCount);
                    drawCountRow(
                        "First-Trace UAVs",
                        timings->activeUavCount);
                    drawCountRow("Peak SRVs", timings->peakSrvCount);
                    drawCountRow("Peak UAVs", timings->peakUavCount);
                    drawCountRow(
                        "Active Dispatches",
                        timings->activeDispatchCount);
                    ImGui::EndTable();
                }
                ImGui::SetItemTooltip(
                    "Payload and traffic values are logical models, not "
                    "measured memory bandwidth.");
                ImGui::TreePop();
            }
        }

        const bool visibilityBenchmarksOpen = DrawCollapsingHeader(
            "AO Benchmarks",
            "Run isolated AO comparisons and inspect complete GPU-stage "
            "results.");
        if (visibilityBenchmarksOpen)
        {
            static int benchmarkWarmupFrames = 120;
            static int benchmarkMeasuredFrames = 240;
            static int benchmarkMatrix = 0;
            static std::string benchmarkUiError;

            const bool benchmarkQueued =
                m_app->IsVisibilityBenchmarkQueued();
            const bool benchmarkActive =
                m_app->IsVisibilityBenchmarkActive();
            const bool benchmarkSequenceActive =
                m_app->IsVisibilityBenchmarkSequenceActive();
            const bool benchmarkBusy = benchmarkQueued || benchmarkActive ||
                benchmarkSequenceActive;
            const ScreenSpaceVisibilitySettings& benchmarkVisibility =
                m_ui.ScreenSpaceVisibility;
            const VisibilityPerformanceWorkload benchmarkWorkload =
                BuildVisibilityPerformanceWorkload(
                    benchmarkVisibility,
                    uint32_t(std::max(width, 0)),
                    uint32_t(std::max(height, 0)));
            const VisibilityVerificationProfileDefinition
                benchmarkDefinition =
                    GetVisibilityVerificationProfileDefinition(
                        m_ui.VisibilityVerification);
            const bool benchmarkCustomProfile =
                m_ui.VisibilityVerification ==
                    VisibilityVerificationProfile::Unset;
            const VisibilityVerificationProfileResolution
                benchmarkResolution = ResolveVisibilityVerificationProfile(
                    m_ui.VisibilityVerification,
                    benchmarkVisibility.performanceProfile,
                    benchmarkWorkload);
            const std::string benchmarkSettingsMismatch =
                benchmarkCustomProfile
                    ? std::string()
                    : FindVisibilityVerificationSettingsMismatch(
                        m_ui.VisibilityVerification,
                        benchmarkVisibility,
                        benchmarkWorkload);
            const VisibilityExecutionPlan benchmarkPlan =
                ResolveVisibilityExecutionPlan(
                    benchmarkVisibility.performanceProfile,
                    benchmarkWorkload);
            const ScreenSpaceVisibilityTimings* benchmarkTimings =
                m_app->GetScreenSpaceVisibilityTimings();
            const bool benchmarkCurrentProfileValid =
                m_ui.UsesDeferredShading() &&
                benchmarkVisibility.enabled &&
                benchmarkVisibility.HasActiveConsumer() &&
                (benchmarkCustomProfile || benchmarkResolution.valid) &&
                benchmarkSettingsMismatch.empty() &&
                benchmarkPlan.valid &&
                (benchmarkCustomProfile ||
                    benchmarkVisibility.performanceProfile ==
                        benchmarkDefinition.implementationProfile) &&
                benchmarkTimings && benchmarkTimings->profileValid &&
                !benchmarkTimings->activePermutation.empty() &&
                benchmarkTimings->activePermutation ==
                    benchmarkPlan.permutationName;
            const bool benchmarkOutputValid =
                benchmarkWorkload.outputWidth == 1920u &&
                benchmarkWorkload.outputHeight == 1080u;
            const bool benchmarkSceneValid =
                m_app->HasSponzaCameraLocations();
            const bool canRunCurrent = benchmarkCurrentProfileValid &&
                benchmarkOutputValid && benchmarkSceneValid &&
                !benchmarkBusy;
            const bool canRunMatrix = benchmarkOutputValid &&
                benchmarkSceneValid && !benchmarkBusy;

            ImGui::SliderInt(
                "Warmup Frames",
                &benchmarkWarmupFrames,
                0,
                600,
                "%d",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip(
                "Frames discarded after history reset before measurement.");
            ImGui::SliderInt(
                "Measured Frames",
                &benchmarkMeasuredFrames,
                1,
                2000,
                "%d",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip(
                "Complete, frame-correlated GPU samples retained per run.");

            const float pairedButtonWidth = std::max(
                1.f,
                (ImGui::GetContentRegionAvail().x -
                    style.ItemSpacing.x) / 2.f);
            if (!canRunCurrent)
                ImGui::BeginDisabled();
            if (ImGui::Button(
                    "Run Current",
                    ImVec2(pairedButtonWidth, 0.f)) && canRunCurrent)
            {
                benchmarkUiError.clear();
                (void)m_app->QueueVisibilityBenchmark(
                    uint32_t(std::max(benchmarkWarmupFrames, 0)),
                    uint32_t(std::max(benchmarkMeasuredFrames, 1)),
                    {},
                    false);
            }
            ImGui::SetItemTooltip(
                !benchmarkOutputValid
                    ? "Requires 1920 x 1080 output."
                    : (!benchmarkSceneValid
                        ? "Requires a Sponza benchmark scene."
                        : (!benchmarkCurrentProfileValid
                            ? "Wait for a valid compiled AO permutation."
                            : "Benchmark only the current AO profile.")));
            ImGui::SameLine();
            if (ImGui::Button(
                    "Compare Reference",
                    ImVec2(pairedButtonWidth, 0.f)) && canRunCurrent)
            {
                benchmarkUiError.clear();
                (void)m_app->QueueVisibilityBenchmarkSequence(
                    VisibilityBenchmarkSequenceKind::ReferenceVersusCurrent,
                    uint32_t(std::max(benchmarkWarmupFrames, 0)),
                    uint32_t(std::max(benchmarkMeasuredFrames, 1)),
                    {});
            }
            ImGui::SetItemTooltip(
                "Run Reference, then the current settings snapshot.");
            if (!canRunCurrent)
                ImGui::EndDisabled();

            static constexpr const char* BenchmarkMatrixLabels[] = {
                "All Implemented Profiles",
                "New AO Candidates",
                "Fixed Sample Counts",
                "Noise Patterns",
                "Spatial Denoising & Reconstruction",
                "Math Modes",
                "Precision Modes"
            };
            static constexpr VisibilityBenchmarkSequenceKind
                BenchmarkMatrixKinds[] = {
                    VisibilityBenchmarkSequenceKind::AllImplemented,
                    VisibilityBenchmarkSequenceKind::NewCandidates,
                    VisibilityBenchmarkSequenceKind::FixedSample,
                    VisibilityBenchmarkSequenceKind::Noise,
                    VisibilityBenchmarkSequenceKind::Reconstruction,
                    VisibilityBenchmarkSequenceKind::Math,
                    VisibilityBenchmarkSequenceKind::Precision
                };
            ImGui::TextUnformatted("Test Matrix");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo(
                    "##VisibilityBenchmarkMatrix",
                    BenchmarkMatrixLabels[benchmarkMatrix]))
            {
                for (int matrixIndex = 0;
                    matrixIndex < int(std::size(BenchmarkMatrixLabels));
                    ++matrixIndex)
                {
                    const bool selected = matrixIndex == benchmarkMatrix;
                    if (ImGui::Selectable(
                            BenchmarkMatrixLabels[matrixIndex], selected))
                    {
                        benchmarkMatrix = matrixIndex;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (!canRunMatrix)
                ImGui::BeginDisabled();
            if (ImGui::Button(
                    "Run Selected Matrix",
                    ImVec2(pairedButtonWidth, 0.f)) && canRunMatrix)
            {
                benchmarkUiError.clear();
                (void)m_app->QueueVisibilityBenchmarkSequence(
                    BenchmarkMatrixKinds[benchmarkMatrix],
                    uint32_t(std::max(benchmarkWarmupFrames, 0)),
                    uint32_t(std::max(benchmarkMeasuredFrames, 1)),
                    {});
            }
            ImGui::SetItemTooltip(
                !benchmarkOutputValid
                    ? "Requires 1920 x 1080 output."
                    : (!benchmarkSceneValid
                        ? "Requires a Sponza benchmark scene."
                        : "Run each profile independently and restore the "
                            "starting settings afterward."));
            if (!canRunMatrix)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (!benchmarkBusy)
                ImGui::BeginDisabled();
            if (ImGui::Button(
                    "Cancel",
                    ImVec2(pairedButtonWidth, 0.f)) && benchmarkBusy)
            {
                m_app->CancelVisibilityBenchmark();
            }
            ImGui::SetItemTooltip(
                "Cancel without publishing partial measurements.");
            if (!benchmarkBusy)
                ImGui::EndDisabled();

            if (benchmarkSequenceActive)
            {
                ImGui::Text(
                    "Matrix %zu / %zu: %s",
                    m_app->GetVisibilityBenchmarkSequenceIndex() + 1u,
                    m_app->GetVisibilityBenchmarkSequenceCount(),
                    m_app->GetVisibilityBenchmarkSequenceEntryName().c_str());
            }
            if (benchmarkActive)
            {
                const uint32_t completed =
                    m_app->GetVisibilityBenchmarkCompletedFrameCount();
                const uint32_t requested =
                    m_app->GetVisibilityBenchmarkRequestedFrameCount();
                const float progress = requested > 0u
                    ? std::clamp(
                        float(completed) / float(requested), 0.f, 1.f)
                    : 0.f;
                char progressLabel[64];
                snprintf(
                    progressLabel,
                    std::size(progressLabel),
                    "%u / %u measured frames",
                    completed,
                    requested);
                ImGui::ProgressBar(
                    progress,
                    ImVec2(-FLT_MIN, 0.f),
                    progressLabel);
            }
            const std::string& benchmarkStatus =
                m_app->GetVisibilityBenchmarkStatus();
            if (!benchmarkStatus.empty())
                ImGui::TextWrapped("%s", benchmarkStatus.c_str());
            const std::string& benchmarkError =
                m_app->GetVisibilityBenchmarkError();
            if (!benchmarkError.empty())
            {
                ImGui::TextColored(
                    ImVec4(1.f, 0.35f, 0.30f, 1.f),
                    "%s",
                    benchmarkError.c_str());
            }
            if (!benchmarkUiError.empty())
            {
                ImGui::TextColored(
                    ImVec4(1.f, 0.35f, 0.30f, 1.f),
                    "%s",
                    benchmarkUiError.c_str());
            }

            const bool canExport =
                m_app->HasVisibilityBenchmarkResults() && !benchmarkBusy;
            const float folderButtonWidth = ImGui::GetFrameHeight();
            const float exportButtonWidth = std::max(
                1.f,
                ImGui::GetContentRegionAvail().x -
                    folderButtonWidth - style.ItemSpacing.x);
            if (!canExport)
                ImGui::BeginDisabled();
            if (ImGui::Button(
                    "Export Last Run",
                    ImVec2(exportButtonWidth, 0.f)) && canExport)
            {
                benchmarkUiError.clear();
                (void)m_app->ExportLastVisibilityBenchmark({});
            }
            ImGui::SetItemTooltip(
                "Export the last summary, raw frame data, and final frame.");
            if (!canExport)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (DrawFolderButton(
                    "##OpenVisibilityBenchmarkFolder",
                    "Open the benchmark results folder."))
            {
                benchmarkUiError.clear();
                std::filesystem::path benchmarkDirectory;
                const VisibilityBenchmarkExportPaths& paths =
                    m_app->GetLastVisibilityBenchmarkPaths();
                if (!paths.json.empty())
                    benchmarkDirectory = paths.json.parent_path();
                if (benchmarkDirectory.empty() &&
                    !ResolveVisibilityBenchmarkOutputDirectory(
                        {}, benchmarkDirectory, benchmarkUiError))
                {
                    benchmarkDirectory.clear();
                }
                if (!benchmarkDirectory.empty())
                {
                    const std::wstring folder = benchmarkDirectory.wstring();
                    const HINSTANCE openResult = ShellExecuteW(
                        nullptr,
                        L"open",
                        folder.c_str(),
                        nullptr,
                        nullptr,
                        SW_SHOWNORMAL);
                    if (reinterpret_cast<INT_PTR>(openResult) <= 32)
                    {
                        benchmarkUiError =
                            "Windows could not open the benchmark folder.";
                    }
                }
            }

            if (const VisibilityBenchmarkSummary* summary =
                    m_app->GetLastVisibilityBenchmarkSummary())
            {
                if (ImGui::BeginTable(
                        "##VisibilityBenchmarkResults",
                        3,
                        ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn(
                        "Last Run Stage",
                        ImGuiTableColumnFlags_WidthStretch,
                        2.5f);
                    ImGui::TableSetupColumn(
                        "Median",
                        ImGuiTableColumnFlags_WidthStretch,
                        1.f);
                    ImGui::TableSetupColumn(
                        "p95",
                        ImGuiTableColumnFlags_WidthStretch,
                        1.f);
                    ImGui::TableHeadersRow();
                    auto drawDistributionRow =
                        [](const char* label,
                            const VisibilityBenchmarkDistributionSummary&
                                distribution)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(label);
                            ImGui::TableSetColumnIndex(1);
                            if (distribution.valid)
                            {
                                ImGui::Text(
                                    "%.3f ms",
                                    distribution.medianMilliseconds);
                            }
                            else
                            {
                                ImGui::TextDisabled("--");
                            }
                            ImGui::TableSetColumnIndex(2);
                            if (distribution.valid)
                            {
                                ImGui::Text(
                                    "%.3f ms",
                                    distribution.p95Milliseconds);
                            }
                            else
                            {
                                ImGui::TextDisabled("--");
                            }
                        };
                    for (const VisibilityBenchmarkStageSummary& stage :
                        summary->stages)
                    {
                        if (!stage.required)
                            continue;
                        drawDistributionRow(
                            GetBenchmarkStageLabel(stage.stage),
                            stage.distribution);
                    }
                    drawDistributionRow(
                        "Named-Stage Total",
                        summary->summedStages);
                    drawDistributionRow(
                        "Unattributed Timer Difference",
                        summary->unattributedResidual);
                    ImGui::EndTable();
                }
                if (summary->incompleteFrameCount > 0u)
                {
                    ImGui::TextDisabled(
                        "%u incomplete frames excluded",
                        summary->incompleteFrameCount);
                }
            }
        }

        if (visibilityBenchmarkBusy)
            ImGui::BeginDisabled();


        const bool tonemapperOpen = DrawCollapsingHeader(
            "Tonemapper",
            "Show AgX tone and color controls.");
        if (tonemapperOpen)
        {
            static const char* presetLabels[] = {
                "Base",
                "Punchy",
                "Golden",
                "Mix",
                "Custom"
            };

            ImGui::SetNextItemWidth(settingsControlWidth);
            const bool presetComboOpen = ImGui::BeginCombo(
                "Preset", presetLabels[int(m_ui.AgxToneMappingPreset)]);
            ImGui::SetItemTooltip("Choose a grade. Edits switch to Custom.");
            if (presetComboOpen)
            {
                for (int presetIndex = 0;
                    presetIndex < int(std::size(presetLabels));
                    ++presetIndex)
                {
                    const AgxPreset preset = AgxPreset(presetIndex);
                    const bool selected = preset == m_ui.AgxToneMappingPreset;
                    if (ImGui::Selectable(presetLabels[presetIndex], selected))
                    {
                        m_ui.AgxToneMappingPreset = preset;
                        if (preset != AgxPreset::Custom)
                            m_ui.AgxToneMappingParams = GetAgxPresetParameters(preset);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            const auto& luts = m_app->GetKodakLuts();
            const size_t selectedLut = m_app->GetSelectedKodakLut();
            ImGui::SetNextItemWidth(settingsControlWidth);
            const bool lutComboOpen = ImGui::BeginCombo(
                "Lut", luts[selectedLut].Name.c_str());
            ImGui::SetItemTooltip("Apply a film-style color look.");
            if (lutComboOpen)
            {
                for (size_t lutIndex = 0; lutIndex < luts.size(); ++lutIndex)
                {
                    const bool selected = lutIndex == selectedLut;
                    if (ImGui::Selectable(luts[lutIndex].Name.c_str(), selected))
                        m_app->SetSelectedKodakLut(lutIndex);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            AgxToneMappingParameters& params = m_ui.AgxToneMappingParams;
            bool gradeChanged = false;
            gradeChanged |= ImGui::SliderFloat("Exposure", &params.Exposure, -10.f, 10.f, "%.2f EV");
            ImGui::SetItemTooltip("Brighten or darken before tone mapping.");
            gradeChanged |= ImGui::SliderFloat("Contrast", &params.Contrast, 0.5f, 2.f, "%.3f");
            ImGui::SetItemTooltip("Increase or reduce contrast.");
            gradeChanged |= ImGui::SliderFloat("Saturation", &params.Saturation, 0.f, 2.f, "%.3f");
            ImGui::SetItemTooltip("Increase or reduce color intensity.");
            gradeChanged |= ImGui::SliderFloat("Warmth", &params.Warmth, -1.f, 1.f, "%.3f");
            ImGui::SetItemTooltip("Shift colors warmer or cooler.");
            gradeChanged |= ImGui::SliderFloat("Tint", &params.Tint, -1.f, 1.f, "%.3f");
            ImGui::SetItemTooltip("Shift colors toward green or magenta.");
            gradeChanged |= ImGui::SliderFloat(
                "Slope", &params.Slope, 0.f, 2.f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("Scale the color grade. 1.0 is neutral.");
            gradeChanged |= ImGui::SliderFloat(
                "Power", &params.Power, 0.01f, 2.f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("Below 1 brightens; above 1 darkens.");

            if (gradeChanged)
            {
                params.Exposure = std::clamp(params.Exposure, -10.f, 10.f);
                params.Contrast = std::clamp(params.Contrast, 0.5f, 2.f);
                params.Saturation = std::clamp(params.Saturation, 0.f, 2.f);
                params.Warmth = std::clamp(params.Warmth, -1.f, 1.f);
                params.Tint = std::clamp(params.Tint, -1.f, 1.f);
                params.Slope = std::clamp(params.Slope, 0.f, 2.f);
                params.Power = std::clamp(params.Power, 0.01f, 2.f);
                m_ui.AgxToneMappingPreset = AgxPreset::Custom;
            }
        }

        const bool skyOpen = DrawCollapsingHeader(
            "Sky", "Show sky controls.");
        if (skyOpen)
        {
            if (!m_ui.EnableProceduralSky)
                ImGui::BeginDisabled();
            ImGui::SliderFloat("Brightness", &m_ui.SkyParams.brightness, 0.f, 1.f);
            ImGui::SetItemTooltip("Set sky and ambient light brightness.");
            ImGui::SliderFloat("Glow Size", &m_ui.SkyParams.glowSize, 0.f, 90.f);
            ImGui::SetItemTooltip("Set the sun glow's size.");
            ImGui::SliderFloat("Glow Sharpness", &m_ui.SkyParams.glowSharpness, 1.f, 10.f);
            ImGui::SetItemTooltip("Set how quickly the sun glow fades.");
            ImGui::SliderFloat("Glow Intensity", &m_ui.SkyParams.glowIntensity, 0.f, 1.f);
            ImGui::SetItemTooltip("Set the sun glow's brightness.");
            ImGui::SliderFloat("Horizon Size", &m_ui.SkyParams.horizonSize, 0.f, 90.f);
            ImGui::SetItemTooltip("Set the horizon blend width.");
            if (!m_ui.EnableProceduralSky)
                ImGui::EndDisabled();

            ImGui::Checkbox("Enable Procedural Sky", &m_ui.EnableProceduralSky);
            ImGui::SetItemTooltip("Show the procedural sky.");
        }

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
            if (!lights.empty())
            {
                ImGui::SetNextItemWidth(settingsControlWidth);
                const bool lightComboOpen = ImGui::BeginCombo(
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
        }

        constexpr float ActionButtonCount = 4.f;
        const float actionButtonWidth = std::max(
            1.f,
            (ImGui::GetContentRegionAvail().x -
                style.ItemSpacing.x * (ActionButtonCount - 1.f)) /
                ActionButtonCount);

        if (ImGui::Button("Reload Shaders", ImVec2(actionButtonWidth, 0.f)))
            m_ui.ShaderReloadRequested = true;
        ImGui::SetItemTooltip("Recompile and reload shaders.");

        ImGui::SameLine();
        if (ImGui::Button("Reset Settings", ImVec2(actionButtonWidth, 0.f)))
            m_app->ResetAllRendererSettings();
        ImGui::SetItemTooltip(
            "Restore factory settings without changing the camera or scene.");

        ImGui::SameLine();
        if (ImGui::Button("Restart", ImVec2(actionButtonWidth, 0.f)))
        {
            g_RestartRequested = true;
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
        }
        ImGui::SetItemTooltip("Restart UVSR.");

        ImGui::SameLine();
        if (ImGui::Button("Screenshot", ImVec2(actionButtonWidth, 0.f)))
            m_ui.CopyScreenshotToClipboard = true;
        ImGui::SetItemTooltip("Copy the current frame to the clipboard.");

        if (visibilityBenchmarkBusy)
            ImGui::EndDisabled();

        ImGui::End();

        auto material = m_ui.SelectedMaterial;
        if (material)
        {
            ImGui::SetNextWindowPos(ImVec2(float(width) - fontSize * 0.6f, fontSize * 0.6f), 0, ImVec2(1.f, 0.f));
            ImGui::Begin("Material Editor");
            if (visibilityBenchmarkBusy)
                ImGui::BeginDisabled();
            ImGui::Text("Material %d: %s", material->materialID, material->name.c_str());

            MaterialDomain previousDomain = material->domain;
            material->dirty = donut::app::MaterialEditor(material.get(), true);

            if (previousDomain != material->domain)
                m_app->GetScene()->GetSceneGraph()->GetRootNode()->InvalidateContent();
            if (visibilityBenchmarkBusy)
                ImGui::EndDisabled();

            ImGui::End();
        }

        ImGui::PopFont();
    }
};

static bool TryParseUint32Argument(
    const char* value,
    uint32_t& parsedValue)
{
    if (!value || value[0] == '\0')
        return false;
    const char* end = value + std::strlen(value);
    uint64_t wideValue = 0u;
    const std::from_chars_result result =
        std::from_chars(value, end, wideValue);
    if (result.ec != std::errc{} || result.ptr != end ||
        wideValue > std::numeric_limits<uint32_t>::max())
    {
        return false;
    }
    parsedValue = uint32_t(wideValue);
    return true;
}

static void ReportCommandLineError(const std::string& message)
{
    const std::string formatted = "UVSR command-line error: " + message +
        "\n";
    std::fputs(formatted.c_str(), stderr);
    std::fflush(stderr);
#ifdef _WIN32
    OutputDebugStringA(formatted.c_str());
#endif
}

bool ProcessCommandLine(
    int argc,
    const char* const* argv,
    DeviceCreationParameters& deviceParams,
    std::string& sceneName,
    std::string& experimentDescription,
    bool& benchmarkCameraRequested,
    VisibilityBenchmarkLaunchOptions& visibilityBenchmark)
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
        else if (!strcmp(argv[i], "--visibility-profile"))
        {
            if (visibilityBenchmark.implementationProfileSpecified)
            {
                ReportCommandLineError(
                    "--visibility-profile cannot be combined with "
                    "--visibility-implementation-profile");
                return false;
            }
            if (i + 1 >= argc)
            {
                ReportCommandLineError(
                    "--visibility-profile requires a one-click profile name");
                return false;
            }
            const char* profileName = argv[++i];
            if (!TryParseVisibilityVerificationProfile(
                    profileName, visibilityBenchmark.profile))
            {
                ReportCommandLineError(
                    "Unknown visibility profile '" +
                    std::string(profileName) +
                    "'. Use a displayed one-click profile name or its "
                    "hyphenated form.");
                return false;
            }
            visibilityBenchmark.profileSpecified = true;
        }
        else if (!strcmp(
                argv[i], "--visibility-implementation-profile"))
        {
            if (visibilityBenchmark.profileSpecified)
            {
                ReportCommandLineError(
                    "--visibility-implementation-profile cannot be combined "
                    "with --visibility-profile");
                return false;
            }
            if (i + 1 >= argc)
            {
                ReportCommandLineError(
                    "--visibility-implementation-profile requires a "
                    "displayed Performance Profile name");
                return false;
            }
            const char* profileName = argv[++i];
            if (!TryParseVisibilityPerformanceProfile(
                    profileName,
                    visibilityBenchmark.implementationProfile))
            {
                ReportCommandLineError(
                    "Unknown visibility implementation profile '" +
                    std::string(profileName) +
                    "'. Use a displayed Performance Profile name or its "
                    "hyphenated form.");
                return false;
            }
            visibilityBenchmark.implementationProfileSpecified = true;
        }
        else if (!strcmp(argv[i], "--visibility-benchmark"))
        {
            visibilityBenchmark.benchmarkRequested = true;
        }
        else if (!strcmp(argv[i], "--benchmark-sequence"))
        {
            if (i + 1 >= argc)
            {
                ReportCommandLineError(
                    "--benchmark-sequence requires reference-versus-current, "
                    "new-candidates, fixed-sample, noise, reconstruction, "
                    "math, all, or precision");
                return false;
            }

            VisibilityBenchmarkSequenceKind sequenceKind =
                VisibilityBenchmarkSequenceKind::None;
            const char* sequenceName = argv[++i];
            if (!TryParseVisibilityBenchmarkSequenceKind(
                    sequenceName, sequenceKind))
            {
                ReportCommandLineError(
                    "Unknown benchmark sequence '" +
                    std::string(sequenceName) +
                    "'. Use reference-versus-current, fixed-sample, noise, "
                    "reconstruction, math, new-candidates, all, or precision.");
                return false;
            }

            visibilityBenchmark.sequenceKind =
                static_cast<uint8_t>(sequenceKind);
            visibilityBenchmark.benchmarkRequested = true;
        }
        else if (!strcmp(argv[i], "--benchmark-warmup"))
        {
            if (i + 1 >= argc || !TryParseUint32Argument(
                    argv[++i], visibilityBenchmark.warmupFrameCount) ||
                visibilityBenchmark.warmupFrameCount >
                    MaxVisibilityBenchmarkWarmupFrames)
            {
                ReportCommandLineError(
                    "--benchmark-warmup requires a frame count from 0 to 100000");
                return false;
            }
        }
        else if (!strcmp(argv[i], "--benchmark-frames"))
        {
            if (i + 1 >= argc || !TryParseUint32Argument(
                    argv[++i], visibilityBenchmark.measuredFrameCount) ||
                visibilityBenchmark.measuredFrameCount == 0u)
            {
                ReportCommandLineError(
                    "--benchmark-frames requires a frame count from 1 to 100000");
                return false;
            }
            if (visibilityBenchmark.measuredFrameCount >
                VisibilityBenchmarkMaximumMeasuredFrameCount)
            {
                ReportCommandLineError(
                    "--benchmark-frames requires a frame count from 1 to 100000");
                return false;
            }
        }
        else if (!strcmp(argv[i], "--benchmark-output"))
        {
            if (i + 1 >= argc || argv[i + 1][0] == '\0')
            {
                ReportCommandLineError(
                    "--benchmark-output requires a directory path");
                return false;
            }
            visibilityBenchmark.outputDirectory = argv[++i];
        }
        else if (!strcmp(argv[i], "--benchmark-auto-close"))
        {
            visibilityBenchmark.autoClose = true;
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
        "Selected graphics adapter %d: %s (%llu MiB dedicated VRAM)",
        selectedChoice->adapterIndex,
        selectedChoice->name.c_str(),
        static_cast<unsigned long long>(selectedChoice->dedicatedVideoMemory / (1024ull * 1024ull)));
    return true;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    const auto launchTime = std::chrono::system_clock::now();
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
#else //  _WIN32
int main(int __argc, const char* const* __argv)
{
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
    VisibilityBenchmarkLaunchOptions visibilityBenchmark;
    if (!ProcessCommandLine(
        __argc,
        __argv,
        deviceParams,
        sceneName,
        experimentDescription,
        benchmarkCameraRequested,
        visibilityBenchmark))
    {
        return 1;
    }
    if (visibilityBenchmark.benchmarkRequested)
        benchmarkCameraRequested = true;
    if (visibilityBenchmark.autoClose &&
        !visibilityBenchmark.benchmarkRequested)
    {
        log::warning(
            "--benchmark-auto-close has no effect without --visibility-benchmark");
    }
    if (visibilityBenchmark.implementationProfileSpecified)
    {
        const VisibilityPerformanceProfileConfiguration configuration =
            GetVisibilityPerformanceProfileConfiguration(
                visibilityBenchmark.implementationProfile);
        if (configuration.implementationStatus ==
                VisibilityImplementationStatus::Unavailable ||
            configuration.implementationStatus ==
                VisibilityImplementationStatus::Unset)
        {
            ReportCommandLineError(
                "Visibility implementation profile '" +
                std::string(configuration.name) + "' is unavailable" +
                (configuration.implementationNote.empty()
                    ? std::string(".")
                    : std::string(": ") +
                        std::string(configuration.implementationNote)));
            return 1;
        }
    }
    else if (visibilityBenchmark.profileSpecified ||
        visibilityBenchmark.benchmarkRequested)
    {
        const VisibilityVerificationProfileDefinition definition =
            GetVisibilityVerificationProfileDefinition(
                visibilityBenchmark.profile);
        if (definition.implementationStatus ==
            VisibilityImplementationStatus::Unavailable)
        {
            ReportCommandLineError(
                "Visibility profile '" + std::string(definition.name) +
                "' is unavailable: " +
                std::string(definition.implementationNote));
            return 1;
        }
    }
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
        if (visibilityBenchmark.implementationProfileSpecified)
        {
            if (!ApplyVisibilityPerformanceProfileDefaults(
                    uiData.ScreenSpaceVisibility,
                    visibilityBenchmark.implementationProfile))
            {
                ReportCommandLineError(
                    "Cannot apply the requested visibility implementation "
                    "profile.");
                deviceManager->Shutdown();
                delete deviceManager;
                return 1;
            }
            uiData.VisibilityVerification =
                VisibilityVerificationProfile::Unset;
            uiData.EnablePbr = true;
            uiData.RenderMode = RendererMode::Deferred;
        }
        else if (visibilityBenchmark.profileSpecified ||
            visibilityBenchmark.benchmarkRequested)
        {
            if (!ApplyVisibilityVerificationProfileDefaults(
                    uiData.ScreenSpaceVisibility,
                    visibilityBenchmark.profile))
            {
                ReportCommandLineError(
                    "Cannot apply the requested visibility profile.");
                deviceManager->Shutdown();
                delete deviceManager;
                return 1;
            }
            uiData.VisibilityVerification = visibilityBenchmark.profile;
            uiData.EnablePbr = true;
            uiData.RenderMode = RendererMode::Deferred;
        }
        if (visibilityBenchmark.benchmarkRequested)
            uiData.ShowUI = false;

        std::shared_ptr<UvsrSceneViewer> demo = std::make_shared<UvsrSceneViewer>(
            deviceManager,
            uiData,
            sceneName,
            benchmarkCameraRequested);
        std::shared_ptr<UIRenderer> gui = std::make_shared<UIRenderer>(deviceManager, demo, uiData);

        gui->Init(demo->GetShaderFactory());

        bool runMessageLoop = true;
        if (visibilityBenchmark.benchmarkRequested)
        {
            const auto sequenceKind =
                static_cast<VisibilityBenchmarkSequenceKind>(
                    visibilityBenchmark.sequenceKind);
            if (sequenceKind == VisibilityBenchmarkSequenceKind::None)
            {
                runMessageLoop = demo->QueueVisibilityBenchmark(
                    visibilityBenchmark.warmupFrameCount,
                    visibilityBenchmark.measuredFrameCount,
                    visibilityBenchmark.outputDirectory,
                    visibilityBenchmark.autoClose);
            }
            else
            {
                runMessageLoop = demo->QueueVisibilityBenchmarkSequence(
                    sequenceKind,
                    visibilityBenchmark.warmupFrameCount,
                    visibilityBenchmark.measuredFrameCount,
                    visibilityBenchmark.outputDirectory,
                    visibilityBenchmark.autoClose);
            }
            if (!runMessageLoop)
                g_VisibilityBenchmarkFailed = true;
        }

        deviceManager->AddRenderPassToBack(demo.get());
        deviceManager->AddRenderPassToBack(gui.get());

        if (runMessageLoop)
            deviceManager->RunMessageLoop();
    }

    deviceManager->Shutdown();
    delete deviceManager;

    if (g_RestartRequested && !RestartCurrentProcess())
        return 1;

    if (g_VisibilityBenchmarkFailed)
        return 1;
	
	return 0;
}
