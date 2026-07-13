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
#include <ctime>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cfloat>
#include <cstdlib>
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
#include "reconstructive_temporal_aa.h"
#include "screen_space_visibility.h"

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

static bool g_RestartRequested = false;
static int g_RestartAdapterIndex = -1;

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
    nvrhi::TextureHandle ReactiveMask;
    bool PbrEnabled = true;
    bool VisibilityResourcesEnabled = false;
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
        bool enableVisibilityResources)
    {
        GBufferRenderTargets::Init(device, size, sampleCount, enableMotionVectors, useReverseProjection);
        PbrEnabled = enablePbr;
        VisibilityResourcesEnabled = enableVisibilityResources;
        MotionVectorsEnabled = enableMotionVectors;

        // Stable material and instance identity is part of the primary PBR
        // G-buffer contract. The target is also reused by picking, so making it
        // always current removes the pick-only duplicate geometry draw in the
        // normal deferred PBR configuration.
        nvrhi::TextureDesc surfaceIdDesc;
        surfaceIdDesc.width = size.x;
        surfaceIdDesc.height = size.y;
        surfaceIdDesc.isRenderTarget = true;
        surfaceIdDesc.useClearValue = true;
        surfaceIdDesc.clearValue = nvrhi::Color(0xffffffffu);
        surfaceIdDesc.sampleCount = sampleCount;
        surfaceIdDesc.dimension = sampleCount > 1
            ? nvrhi::TextureDimension::Texture2DMS
            : nvrhi::TextureDimension::Texture2D;
        surfaceIdDesc.keepInitialState = true;
        surfaceIdDesc.isVirtual = device->queryFeatureSupport(nvrhi::Feature::VirtualResources);
        // Keep the full stable scene identifiers. Truncating either component
        // to 16 bits can alias unrelated materials or instances, which would
        // make temporal surface validation accept contaminated history.
        surfaceIdDesc.format = nvrhi::Format::RG32_UINT;
        surfaceIdDesc.initialState = nvrhi::ResourceStates::RenderTarget;
        surfaceIdDesc.debugName = "MaterialAndInstanceIDs";
        MaterialIDs = device->createTexture(surfaceIdDesc);

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

            nvrhi::TextureDesc reactiveMaskDesc = materialAmbientOcclusionDesc;
            reactiveMaskDesc.clearValue = nvrhi::Color(0.f);
            reactiveMaskDesc.debugName = "PbrExplicitReactiveMask";
            ReactiveMask = device->createTexture(reactiveMaskDesc);

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
                MaterialAmbientOcclusion,
                MaterialIDs,
                ReactiveMask };
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

            desc.debugName = "ScreenSpaceVisibility/DirectDiffuseRadiance";
            DirectDiffuseRadiance = device->createTexture(desc);
        }

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
        bool enableMotionVectors) const
    {
        if (any(m_Size != size) || m_SampleCount != sampleCount ||
            PbrEnabled != enablePbr ||
            VisibilityResourcesEnabled != enableVisibilityResources ||
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

        if (ReactiveMask)
            commandList->clearTextureFloat(
                ReactiveMask, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureUInt(
            MaterialIDs, nvrhi::AllSubresources, 0xffffffffu);

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

enum class PbrDebugView
{
    FinalLinearHdr,
    BaseColor,
    Metalness,
    PerceptualRoughness,
    EncodedNormal,
    DiffuseContribution,
    SpecularContribution,
    DirectLightVisibility
};

static const char* GetPbrDebugViewName(PbrDebugView view)
{
    static const char* names[] = {
        "Final linear HDR",
        "Base color",
        "Metalness",
        "Perceptual roughness",
        "Encoded normal",
        "Diffuse contribution",
        "Specular contribution",
        "Direct-light visibility"
    };
    return names[int(view)];
}

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
    float4 RtaaSharpening;
    float4 RtaaSharpeningSuppression;
    float4 RtaaDisplayFlags;
};

struct AgxSharpeningParameters
{
    bool enabled = false;
    bool diagnosticBypass = false;
    bool debugContribution = false;
    float strength = 0.f;
    float motionSuppression = 1.f;
    float reactiveSuppression = 1.f;
    float varianceSuppression = 1.f;
    float haloClamp = 0.f;
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
    nvrhi::ITexture* m_BoundRtaaMetadata = nullptr;
    nvrhi::ITexture* m_BoundRtaaMoments = nullptr;
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
            nvrhi::BindingLayoutItem::Texture_SRV(2),
            nvrhi::BindingLayoutItem::Texture_SRV(3),
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
        nvrhi::ITexture* sourceTexture,
        nvrhi::ITexture* rtaaMetadata = nullptr,
        nvrhi::ITexture* rtaaMoments = nullptr,
        const AgxSharpeningParameters& sharpening = {})
    {
        nvrhi::ITexture* lutTexture = m_ColorLut
            ? m_ColorLut.Get()
            : m_CommonPasses->m_BlackTexture3D.Get();

        nvrhi::ITexture* metadataTexture = rtaaMetadata ? rtaaMetadata : sourceTexture;
        nvrhi::ITexture* momentsTexture = rtaaMoments ? rtaaMoments : sourceTexture;

        if (!m_BindingSet || m_BoundSource != sourceTexture || m_BoundLut != lutTexture ||
            m_BoundRtaaMetadata != metadataTexture || m_BoundRtaaMoments != momentsTexture)
        {
            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(0, sourceTexture),
                nvrhi::BindingSetItem::Texture_SRV(1, lutTexture),
                nvrhi::BindingSetItem::Texture_SRV(2, metadataTexture),
                nvrhi::BindingSetItem::Texture_SRV(3, momentsTexture),
                nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearClampSampler)
            };
            m_BindingSet = m_Device->createBindingSet(bindingSetDesc, m_BindingLayout);
            m_BoundSource = sourceTexture;
            m_BoundLut = lutTexture;
            m_BoundRtaaMetadata = metadataTexture;
            m_BoundRtaaMoments = momentsTexture;
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
        const nvrhi::TextureDesc& sourceDesc = sourceTexture->getDesc();
        constants.RtaaSharpening = float4(
            1.f / std::max(float(sourceDesc.width), 1.f),
            1.f / std::max(float(sourceDesc.height), 1.f),
            std::max(sharpening.strength, 0.f),
            std::max(sharpening.haloClamp, 0.f));
        constants.RtaaSharpeningSuppression = float4(
            std::max(sharpening.motionSuppression, 0.f),
            std::max(sharpening.reactiveSuppression, 0.f),
            std::max(sharpening.varianceSuppression, 0.f),
            sharpening.enabled ? 1.f : 0.f);
        constants.RtaaDisplayFlags = float4(
            sharpening.diagnosticBypass ? 1.f : 0.f,
            sharpening.debugContribution ? 1.f : 0.f,
            0.f, 0.f);
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

struct UIData
{
    bool                                ShowUI = true;
    std::vector<GpuAdapterChoice>       GpuAdapterChoices;
    int                                 ActiveGpuAdapterIndex = -1;
    bool                                EnablePbr = true;
    RendererMode                        RenderMode = RendererMode::Deferred;
    ScreenSpaceVisibilitySettings       ScreenSpaceVisibility;
    ReconstructiveTemporalAASettings    ReconstructiveTemporalAA;
    AgxToneMappingParameters            AgxToneMappingParams;
    AgxPreset                           AgxToneMappingPreset = AgxPreset::Base;
    SkyParameters                       SkyParams;
    bool                                ShaderReloadRequested = false;
    PbrDebugView                        PbrDebug = PbrDebugView::FinalLinearHdr;
    bool                                EnableProceduralSky = true;
    WhiteWorldMode                      WhiteWorld = WhiteWorldMode::Off;
    bool                                UseThirdPersonCamera = false;
    std::shared_ptr<Material>           SelectedMaterial;
    std::shared_ptr<SceneGraphNode>     SelectedNode;
    bool                                CopyScreenshotToClipboard = false;

    [[nodiscard]] bool UsesDeferredShading() const
    {
        return RenderMode == RendererMode::Deferred;
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
	std::vector<std::string>            m_SceneFilesAvailable;
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
    std::unique_ptr<ReconstructiveTemporalAAPass> m_ReconstructiveTemporalAAPass;
    std::unique_ptr<MaterialIDPass>     m_MaterialIDPass;
    std::unique_ptr<PixelReadbackPass>  m_PixelReadbackPass;
    std::vector<KodakLut>               m_KodakLuts;
    size_t                              m_SelectedKodakLut = 0;

    std::shared_ptr<IView>              m_View;
    std::shared_ptr<PlanarView>         m_PreviousView;
    
    nvrhi::CommandListHandle            m_CommandList;
    FirstPersonCamera                   m_FirstPersonCamera;
    ThirdPersonCamera                   m_ThirdPersonCamera;
    BindingCache                        m_BindingCache;
    
    float                               m_CameraVerticalFov = 60.f;
    float                               m_SceneDiagonal = 100.f;
    float3                              m_AmbientTop = 0.f;
    float3                              m_AmbientBottom = 0.f;
    uint2                               m_PickPosition = 0u;
    bool                                m_Pick = false;
    float                               m_LastFrameDeltaSeconds = 1.f / 60.f;
    

    UIData&                             m_ui;

public:

    UvsrSceneViewer(DeviceManager* deviceManager, UIData& ui, const std::string& sceneName)
        : Super(deviceManager)
        , m_ui(ui)
        , m_BindingCache(deviceManager->GetDevice())
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
        m_SceneFilesAvailable = FindScenes(*m_NativeFs, m_SceneDir);

        if (sceneName.empty() && m_SceneFilesAvailable.empty())
        {
            log::fatal("No scene file found in media folder '%s'\n"
                "Please make sure that folder contains valid scene files.", m_SceneDir.generic_string().c_str());
        }
        
        m_TextureCache = std::make_shared<TextureCache>(GetDevice(), m_NativeFs, nullptr);

        m_ShaderFactory = std::make_shared<ShaderFactory>(GetDevice(), m_RootFs, "/shaders");
        m_CommonPasses = std::make_shared<CommonRenderPasses>(GetDevice(), m_ShaderFactory);
        DiscoverKodakLuts(mediaDir / "luts/kodak");

        m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>();


        m_CommandList = GetDevice()->createCommandList();

        m_FirstPersonCamera.SetMoveSpeed(3.0f);
        m_ThirdPersonCamera.SetMoveSpeed(3.0f);
        
        SetAsynchronousLoadingEnabled(true);

        if (sceneName.empty())
            SetCurrentSceneName(app::FindPreferredScene(m_SceneFilesAvailable, "BistroExterior.glb"));
        else
            SetCurrentSceneName(sceneName);

    }

	std::shared_ptr<vfs::IFileSystem> GetRootFs() const
    {
		return m_RootFs;
	}

    BaseCamera& GetActiveCamera() const
    {
        return m_ui.UseThirdPersonCamera ? (BaseCamera&)m_ThirdPersonCamera : (BaseCamera&)m_FirstPersonCamera;
    }

	std::vector<std::string> const& GetAvailableScenes() const
	{
		return m_SceneFilesAvailable;
	}

    std::filesystem::path const& GetSceneDir() const
    {
        return m_SceneDir;
    }

    std::string GetCurrentSceneName() const
    {
        return m_CurrentSceneName;
    }

    void SetCurrentSceneName(const std::string& sceneName)
    {
        if (m_CurrentSceneName == sceneName)
            return;

		m_CurrentSceneName = sceneName;

		BeginLoadingScene(m_NativeFs, m_CurrentSceneName);
    }

    void CopyThirdPersonToFirstPerson()
    {
        m_FirstPersonCamera.LookAt(
            m_ThirdPersonCamera.GetPosition(),
            m_ThirdPersonCamera.GetPosition() + m_ThirdPersonCamera.GetDir(),
            m_ThirdPersonCamera.GetUp());
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
        m_ui.PbrDebug = PbrDebugView::FinalLinearHdr;
        m_ui.ScreenSpaceVisibility = ScreenSpaceVisibilitySettings{};
        m_ui.ReconstructiveTemporalAA = ReconstructiveTemporalAASettings{};
        m_ui.AgxToneMappingPreset = AgxPreset::Base;
        m_ui.AgxToneMappingParams = GetAgxPresetParameters(AgxPreset::Base);
        m_ui.SkyParams = SkyParameters{};
        m_ui.EnableProceduralSky = true;

        // This also recreates any passes/resources that were absent because PBR,
        // deferred rendering, or white-world permutations had been disabled
        // before the reset.
        m_ui.ShaderReloadRequested = true;
        ResetReconstructiveTemporalAAHistory();
        log::info("All renderer settings restored to factory defaults");
    }

    void CopyCurrentViewToThirdPerson()
    {
        m_ThirdPersonCamera.LookTo(m_FirstPersonCamera.GetPosition(), m_FirstPersonCamera.GetDir(),
            std::max(10.f, m_SceneDiagonal * 0.01f));
        m_ThirdPersonCamera.Animate(0.f);
    }

    virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
		if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		{
            m_ui.ShowUI = !m_ui.ShowUI;
            return true;	
		}

        if (key == GLFW_KEY_T && action == GLFW_PRESS)
        {
            if (m_ui.UseThirdPersonCamera)
                CopyThirdPersonToFirstPerson();
            else
                CopyCurrentViewToThirdPerson();
            m_ui.UseThirdPersonCamera = !m_ui.UseThirdPersonCamera;
            if (m_ReconstructiveTemporalAAPass)
                m_ReconstructiveTemporalAAPass->ResetHistory();
            return true;
        }

        if (m_ui.EnablePbr &&
            key >= GLFW_KEY_F1 && key <= GLFW_KEY_F8 && action == GLFW_PRESS)
        {
            m_ui.PbrDebug = PbrDebugView(key - GLFW_KEY_F1);
            log::info("PBR debug view: %s%s",
                GetPbrDebugViewName(m_ui.PbrDebug),
                m_ui.UsesDeferredShading() ? "" : " (available in deferred shading)");
            return true;
        }

        GetActiveCamera().KeyboardUpdate(key, scancode, action, mods);
        return true;
    }

    virtual bool MousePosUpdate(double xpos, double ypos) override
    {
        GetActiveCamera().MousePosUpdate(xpos, ypos);

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
        if (std::isfinite(fElapsedTimeSeconds) && fElapsedTimeSeconds > 0.f)
        {
            // Clamp only pathological pauses. The exact current-frame delta,
            // not the smoothed HUD value, drives RTAA's CPU-side exponential
            // response weights.
            m_LastFrameDeltaSeconds = std::clamp(
                fElapsedTimeSeconds, 1.f / 1000.f, 0.25f);
        }
        GetActiveCamera().Animate(fElapsedTimeSeconds);
    }


    virtual void SceneUnloading() override
    {
        if (m_ForwardPass) m_ForwardPass->ResetBindingCache();
        if (m_DeferredLightingPass) m_DeferredLightingPass->ResetBindingCache();
        if (m_PbrDeferredLightingPass) m_PbrDeferredLightingPass->ResetBindingCache();
        if (m_ScreenSpaceVisibilityPass) m_ScreenSpaceVisibilityPass->ResetBindingCache();
        if (m_ReconstructiveTemporalAAPass) m_ReconstructiveTemporalAAPass->Deactivate();
        if (m_GBufferPass) m_GBufferPass->ResetBindingCache();
        m_BindingCache.Clear();
        m_SunLight.reset();
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_OriginalMaterials.clear();
        m_PreviousView.reset();

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
        
        m_Scene->FinishedLoading(GetFrameIndex());

        const box3 loadedSceneBounds = m_Scene->GetSceneGraph()->GetRootNode()->GetGlobalBoundingBox();
        m_SceneDiagonal = std::max(length(loadedSceneBounds.diagonal()), 100.f);

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

        if (m_CurrentSceneName.find("nvidia_bistro") != std::string::npos)
        {
            // Blender exports the Bistro directional light at roughly
            // real-world lux (102,450). UVSR currently has no sun shadows, so
            // use the scene's established unoccluded-light calibration. Its
            // authored RGB is strongly amber and contaminated the neutral
            // white-world diagnostic, so keep the calibrated intensity but use
            // a neutral illuminant for this renderer benchmark.
            m_SunLight->irradiance = 0.35f;
            m_SunLight->color = dm::colors::white;
        }
        
        m_ThirdPersonCamera.SetRotation(dm::radians(135.f), dm::radians(20.f));
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
        PointThirdPersonCameraAt(cameraTarget, cameraDistanceScale);

        m_ui.UseThirdPersonCamera = string_utils::ends_with(m_CurrentSceneName, ".gltf")
            || string_utils::ends_with(m_CurrentSceneName, ".glb");

        CopyThirdPersonToFirstPerson();

    }

    void SetWhiteWorldMode(WhiteWorldMode mode)
    {
        const bool modeChanged = m_ui.WhiteWorld != mode;
        const bool shaderModeChanged = (m_ui.WhiteWorld == WhiteWorldMode::Off) !=
            (mode == WhiteWorldMode::Off);
        m_ui.WhiteWorld = mode;

        const bool enabled = mode != WhiteWorldMode::Off;
        const bool preserveDetailMaps = mode == WhiteWorldMode::PreserveDetail;
        const bool preserveLighting = mode == WhiteWorldMode::PreserveLighting;

        if (!m_Scene)
            return;

        // Blender's FBX export marks every Bistro material as BLEND, even
        // opaque surfaces. That sends the whole scene through the transparent
        // pass and prevents reliable depth ordering. Keep alpha cutouts, but
        // put Bistro's blended materials in the depth-writing alpha-tested
        // pass when textured mode is active.
        const bool normalizeBistroAlphaDepth =
            m_CurrentSceneName.find("nvidia_bistro") != std::string::npos;

        for (auto& [material, original] : m_OriginalMaterials)
        {
            *material = original;

            // The Bistro FBX contains thin architectural surfaces with mixed
            // winding. Keep both sides visible in textured and white-world modes so
            // back-face culling cannot erase legitimate facade/interior faces.
            if (normalizeBistroAlphaDepth)
                material->doubleSided = true;

            if (!enabled && normalizeBistroAlphaDepth &&
                (material->domain == MaterialDomain::AlphaBlended ||
                 material->domain == MaterialDomain::TransmissiveAlphaBlended))
            {
                material->domain = MaterialDomain::AlphaTested;
                material->alphaCutoff = 0.5f;
            }

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
        if (modeChanged && m_ReconstructiveTemporalAAPass)
            m_ReconstructiveTemporalAAPass->ResetHistory();
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
        float distanceScale = 1.f)
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

        m_ThirdPersonCamera.SetTargetPosition(bounds.center());
        m_ThirdPersonCamera.SetDistance(distance);
        m_ThirdPersonCamera.Animate(0.f);
        // Retargeting is an explicit camera cut for the display AA history.
        if (m_ReconstructiveTemporalAAPass)
            m_ReconstructiveTemporalAAPass->ResetHistory();
    }

    std::shared_ptr<TextureCache> GetTextureCache()
    {
        return m_TextureCache;
    }

    std::shared_ptr<Scene> GetScene()
    {
        return m_Scene;
    }

    bool IsReconstructiveTemporalAAActive() const
    {
        // The forward and Donut-legacy comparison paths do not expose the
        // complete depth/normal/identity/motion contract required for safe
        // analytical reconstruction. Bypass RTAA there instead of accepting
        // ambiguous history.
        return m_ui.ReconstructiveTemporalAA.enabled &&
            m_ui.EnablePbr &&
            m_ui.UsesDeferredShading() &&
            m_ui.PbrDebug == PbrDebugView::FinalLinearHdr;
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
        const bool rtaaActive = IsReconstructiveTemporalAAActive();
        const ReconstructiveTemporalJitter jitter = rtaaActive
            ? GenerateReconstructiveTemporalJitter(
                m_ui.ReconstructiveTemporalAA, uint64_t(GetFrameIndex()))
            : ReconstructiveTemporalJitter{};
        planarView->SetPixelOffset(float2(jitter.x, jitter.y));

        planarView->SetMatrices(viewMatrix, projection);
        planarView->UpdateCache();

        m_ThirdPersonCamera.SetView(*planarView);

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
                GetDevice(), m_ShaderFactory);
            m_ReconstructiveTemporalAAPass =
                std::make_unique<ReconstructiveTemporalAAPass>(
                    GetDevice(), m_ShaderFactory);
        }
        else
        {
            m_PbrDeferredLightingPass.reset();
            m_ScreenSpaceVisibilityPass.reset();
            m_ReconstructiveTemporalAAPass.reset();
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
        nvrhi::Viewport windowViewport = nvrhi::Viewport(float(windowWidth), float(windowHeight));
        nvrhi::Viewport renderViewport = windowViewport;

        m_Scene->RefreshSceneGraph(GetFrameIndex());

        {
            uint width = windowWidth;
            uint height = windowHeight;

            constexpr uint sampleCount = 1;
            const bool visibilityResourcesRequired = m_ui.EnablePbr &&
                m_ui.UsesDeferredShading() &&
                m_ui.ScreenSpaceVisibility.HasActiveConsumer();
            const bool motionVectorsRequired =
                IsReconstructiveTemporalAAActive();

            bool needNewPasses = false;

            if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(
                uint2(width, height), sampleCount, m_ui.EnablePbr,
                visibilityResourcesRequired, motionVectorsRequired))
            {
                m_RenderTargets = nullptr;
                m_BindingCache.Clear();
                m_RenderTargets = std::make_unique<RenderTargets>();
                m_RenderTargets->Init(
                    GetDevice(), uint2(width, height), sampleCount,
                    motionVectorsRequired, true, m_ui.EnablePbr,
                    visibilityResourcesRequired);
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
            const auto& sceneLights = m_Scene->GetSceneGraph()->GetLights();
            deferredInputs.lights = &sceneLights;

            const bool runScreenSpaceVisibility = m_ui.EnablePbr &&
                m_ui.PbrDebug == PbrDebugView::FinalLinearHdr &&
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
            const bool finalVisibilityComposite =
                m_ui.ScreenSpaceVisibility.debug.mode ==
                    ScreenSpaceVisibilityDebugMode::FinalComposite;
            const bool writeSourceRadiance = runScreenSpaceVisibility &&
                (m_ui.ScreenSpaceVisibility.indirectDiffuse.enabled ||
                    m_ui.ScreenSpaceVisibility.debug.mode ==
                        ScreenSpaceVisibilityDebugMode::DirectRadianceSource) &&
                (!finalVisibilityComposite || !allFirstBounceSourcesInactive);
            const bool exactGiTransportDebug =
                m_ui.ScreenSpaceVisibility.debug.mode >=
                    ScreenSpaceVisibilityDebugMode::IndirectDiffuse &&
                m_ui.ScreenSpaceVisibility.debug.mode <=
                    ScreenSpaceVisibilityDebugMode::GiLightOnly;
            const bool writeBounceMetadata = writeSourceRadiance &&
                m_ui.ScreenSpaceVisibility.indirectDiffuse.enabled &&
                m_ui.ScreenSpaceVisibility.indirectDiffuse.bounceCount > 1u &&
                (exactGiTransportDebug ||
                    (finalVisibilityComposite &&
                        m_ui.ScreenSpaceVisibility.indirectDiffuse.intensity > 0.f));
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
                    float2(0.f, float(m_ui.PbrDebug)));

                if (runScreenSpaceVisibility)
                {
                    ScreenSpaceVisibilityInputs visibilityInputs;
                    visibilityInputs.depth = m_RenderTargets->Depth;
                    visibilityInputs.normals = m_RenderTargets->GBufferNormals;
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
            const bool primaryGBufferHasSurfaceIds =
                m_ui.EnablePbr && m_ui.UsesDeferredShading();
            if (!primaryGBufferHasSurfaceIds)
            {
                m_CommandList->clearTextureUInt(
                    m_RenderTargets->MaterialIDs,
                    nvrhi::AllSubresources, 0xffffffffu);

                MaterialIDPass::Context materialIdContext;
                RenderCompositeView(m_CommandList,
                    m_View.get(), m_View.get(),
                    *m_RenderTargets->MaterialIDFramebuffer,
                    m_Scene->GetSceneGraph()->GetRootNode(),
                    *m_OpaqueDrawStrategy,
                    *m_MaterialIDPass,
                    materialIdContext,
                    "MaterialID");
            }
            
            m_PixelReadbackPass->Capture(m_CommandList, m_PickPosition);
        }

        if (m_ui.EnableProceduralSky)
            m_SkyPass->Render(m_CommandList, *m_View, *m_SunLight, m_ui.SkyParams);

        nvrhi::ITexture* displaySource = m_RenderTargets->HdrColor;
        nvrhi::ITexture* rtaaMetadata = nullptr;
        nvrhi::ITexture* rtaaMoments = nullptr;
        AgxSharpeningParameters sharpening;
        const bool rtaaActive = IsReconstructiveTemporalAAActive() &&
            m_ReconstructiveTemporalAAPass;
        if (rtaaActive)
        {
            ReconstructiveTemporalAAPass::Inputs rtaaInputs;
            rtaaInputs.sceneColor = m_RenderTargets->HdrColor;
            rtaaInputs.depth = m_RenderTargets->Depth;
            rtaaInputs.gbufferDiffuse = m_RenderTargets->GBufferDiffuse;
            rtaaInputs.gbufferSpecular = m_RenderTargets->GBufferSpecular;
            rtaaInputs.surfaceIds = m_RenderTargets->MaterialIDs;
            rtaaInputs.motionVectors = m_RenderTargets->MotionVectors;
            rtaaInputs.explicitReactiveMask = m_RenderTargets->ReactiveMask;
            m_ReconstructiveTemporalAAPass->Render(
                m_CommandList,
                m_ui.ReconstructiveTemporalAA,
                *m_View,
                m_PreviousView.get(),
                rtaaInputs,
                m_LastFrameDeltaSeconds,
                uint64_t(GetFrameIndex()));

            displaySource = m_ReconstructiveTemporalAAPass->GetOutput();
            rtaaMetadata = m_ReconstructiveTemporalAAPass->GetHistoryMetadata();
            rtaaMoments = m_ReconstructiveTemporalAAPass->GetHistoryMoments();

            const ReconstructiveTemporalAASettings& rtaa =
                m_ui.ReconstructiveTemporalAA;
            const bool finalOutput =
                rtaa.debugMode == ReconstructiveTemporalDebugMode::FinalOutput ||
                rtaa.debugMode ==
                    ReconstructiveTemporalDebugMode::FinalNraRtaaOutput;
            const bool profileAllowsSharpening = rtaa.performanceProfile !=
                ReconstructiveTemporalPerformanceProfile::Performance;
            sharpening.enabled = rtaa.sharpeningEnabled &&
                profileAllowsSharpening && finalOutput;
            sharpening.diagnosticBypass = !finalOutput &&
                rtaa.debugMode !=
                    ReconstructiveTemporalDebugMode::SharpeningContribution;
            sharpening.debugContribution = rtaa.debugMode ==
                ReconstructiveTemporalDebugMode::SharpeningContribution;
            if (sharpening.debugContribution)
                sharpening.enabled = rtaa.sharpeningEnabled &&
                    profileAllowsSharpening;
            sharpening.strength = rtaa.sharpeningStrength;
            sharpening.motionSuppression = rtaa.sharpeningMotionSuppression;
            sharpening.reactiveSuppression = rtaa.sharpeningReactiveSuppression;
            sharpening.varianceSuppression = rtaa.sharpeningVarianceSuppression;
            sharpening.haloClamp = rtaa.sharpeningHaloClamp;

            m_ReconstructiveTemporalAAPass->BeginFusedSharpeningTimer(m_CommandList);
        }
        else if (m_ReconstructiveTemporalAAPass)
        {
            m_ReconstructiveTemporalAAPass->Deactivate();
        }

        nvrhi::ITexture* displayTexture = displaySource;
        if (m_ui.UsesTonemapper())
        {
            m_AgxToneMappingPass->Render(
                m_CommandList, m_ui.AgxToneMappingParams, *m_View,
                displaySource, rtaaMetadata, rtaaMoments, sharpening);

            if (rtaaActive)
                m_ReconstructiveTemporalAAPass->EndFusedSharpeningTimer(m_CommandList);

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

    float GetLastFrameDeltaSeconds() const
    {
        return m_LastFrameDeltaSeconds;
    }

    const ScreenSpaceVisibilityTimings* GetScreenSpaceVisibilityTimings() const
    {
        return m_ScreenSpaceVisibilityPass
            ? &m_ScreenSpaceVisibilityPass->GetTimings()
            : nullptr;
    }

    const ReconstructiveTemporalAATimings* GetReconstructiveTemporalAATimings() const
    {
        return m_ReconstructiveTemporalAAPass
            ? &m_ReconstructiveTemporalAAPass->GetTimings()
            : nullptr;
    }

    const ReconstructiveTemporalWeights* GetReconstructiveTemporalAAWeights() const
    {
        return m_ReconstructiveTemporalAAPass
            ? &m_ReconstructiveTemporalAAPass->GetWeights()
            : nullptr;
    }

    const ReconstructiveTemporalAAMemoryStats* GetReconstructiveTemporalAAMemoryStats() const
    {
        return m_ReconstructiveTemporalAAPass
            ? &m_ReconstructiveTemporalAAPass->GetMemoryStats()
            : nullptr;
    }

    void ResetReconstructiveTemporalAAHistory()
    {
        if (m_ReconstructiveTemporalAAPass)
            m_ReconstructiveTemporalAAPass->ResetHistory();
        // Also cover a reset requested before the pass exists, such as while a
        // scene is loading or immediately after a renderer setting reset.
        m_ui.ReconstructiveTemporalAA.forceHistoryReset = true;
    }

};

class UIRenderer : public ImGui_Renderer
{
private:
    std::shared_ptr<UvsrSceneViewer> m_app;

	std::shared_ptr<app::RegisteredFont> m_FontOpenSans;
    std::shared_ptr<engine::Light> m_SelectedLight;

	UIData& m_ui;

    static bool DrawCollapsingHeader(
        const char* label,
        const char* tooltip,
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None)
    {
        const bool open = ImGui::CollapsingHeader(label, flags);
        ImGui::SetItemTooltip(tooltip);
        return open;
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
        if (!m_ui.ShowUI)
            return;

        int width, height;
        GetDeviceManager()->GetWindowDimensions(width, height);

        if (m_app->IsSceneLoading())
        {
            BeginFullScreenWindow();
            ImGui::PushFont(m_FontOpenSans->GetScaledFont());

            char messageBuffer[256];
            const auto& stats = Scene::GetLoadingStats();
            snprintf(messageBuffer, std::size(messageBuffer), "Loading scene %s, please wait...\nObjects: %d/%d, Textures: %d/%d",
                m_app->GetCurrentSceneName().c_str(), stats.ObjectsLoaded.load(), stats.ObjectsTotal.load(), m_app->GetTextureCache()->GetNumberOfLoadedTextures(), m_app->GetTextureCache()->GetNumberOfRequestedTextures());

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
        ImGui::Begin("Settings", 0, ImGuiWindowFlags_AlwaysAutoResize);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float settingsControlWidth =
            ImGui::CalcTextSize("Reload Shaders").x + style.FramePadding.x * 2.f +
            style.ItemSpacing.x +
            ImGui::CalcTextSize("Restart Renderer").x + style.FramePadding.x * 2.f;

        const bool generalOpen = DrawCollapsingHeader(
            "General",
            "Expand renderer, hardware, scene, camera, material override, and rendering-path controls.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (generalOpen)
        {
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
                    "Current-clock theoretical memory bandwidth and FP32 peak for supported Intel and NVIDIA adapters, not measured workload utilization.");
            }

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
                "Choose the GPU used by UVSR. Changing adapters restarts the renderer immediately.");
        }

        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool cameraComboOpen = ImGui::BeginCombo(
            "##Camera", m_ui.UseThirdPersonCamera ? "Third Person" : "First Person");
        ImGui::SetItemTooltip("Switch the active camera control mode.");
        if (cameraComboOpen)
        {
            if (ImGui::Selectable("First Person", !m_ui.UseThirdPersonCamera))
            {
                if (m_ui.UseThirdPersonCamera)
                {
                    m_app->CopyThirdPersonToFirstPerson();
                    m_ui.UseThirdPersonCamera = false;
                    m_app->ResetReconstructiveTemporalAAHistory();
                }
            }
            if (ImGui::Selectable("Third Person", m_ui.UseThirdPersonCamera))
            {
                if (!m_ui.UseThirdPersonCamera)
                {
                    m_app->CopyCurrentViewToThirdPerson();
                    m_ui.UseThirdPersonCamera = true;
                    m_app->ResetReconstructiveTemporalAAHistory();
                }
            }
            ImGui::EndCombo();
        }
        static const char* whiteWorldLabels[] = {
            "White World Off",
            "White World On",
            "White World Preserve Normals",
            "White World Preserve Emissives"
        };
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool whiteWorldComboOpen = ImGui::BeginCombo(
            "##WhiteWorld", whiteWorldLabels[int(m_ui.WhiteWorld)]);
        ImGui::SetItemTooltip(
            "Override material color while optionally preserving surface detail or colored GI sources.");
        if (whiteWorldComboOpen)
        {
            for (int modeIndex = 0; modeIndex < int(std::size(whiteWorldLabels)); ++modeIndex)
            {
                const bool selected = modeIndex == int(m_ui.WhiteWorld);
                if (ImGui::Selectable(whiteWorldLabels[modeIndex], selected))
                    m_app->SetWhiteWorldMode(WhiteWorldMode(modeIndex));
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        static const char* rendererModeLabels[] = {
            "Deferred",
            "Forward",
            "Forward Tonemapperless"
        };
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool rendererModeComboOpen = ImGui::BeginCombo(
            "##RendererMode", rendererModeLabels[int(m_ui.RenderMode)]);
        ImGui::SetItemTooltip(
            "Choose deferred, forward, or forward rendering that bypasses AgX and the grading LUT.");
        if (rendererModeComboOpen)
        {
            for (int modeIndex = 0;
                modeIndex < int(std::size(rendererModeLabels)); ++modeIndex)
            {
                const RendererMode mode = RendererMode(modeIndex);
                const bool selected = mode == m_ui.RenderMode;
                if (ImGui::Selectable(rendererModeLabels[modeIndex], selected))
                {
                    m_ui.RenderMode = mode;
                    log::info("Renderer mode: %s", rendererModeLabels[modeIndex]);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        const std::string sceneDir = m_app->GetSceneDir().generic_string();
        auto getRelativePath = [&sceneDir](std::string const& name)
        {
            return donut::string_utils::starts_with(name, sceneDir)
                ? name.c_str() + sceneDir.size()
                : name.c_str();
        };

        const std::string currentScene = m_app->GetCurrentSceneName();
        const float folderButtonWidth = ImGui::GetFrameHeight();
        ImGui::SetNextItemWidth(-(folderButtonWidth + style.ItemSpacing.x));
        const bool sceneComboOpen = ImGui::BeginCombo("##Scene", getRelativePath(currentScene));
        // UI convention: every UVSR-owned interactive control explains itself on hover.
        ImGui::SetItemTooltip("Choose the scene to load.");
        if (sceneComboOpen)
        {
            const std::vector<std::string>& scenes = m_app->GetAvailableScenes();
            for (const std::string& scene : scenes)
            {
                bool is_selected = scene == currentScene;
                if (ImGui::Selectable(getRelativePath(scene), is_selected))
                    m_app->SetCurrentSceneName(scene);
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("##OpenSceneFolder", ImVec2(folderButtonWidth, 0.f)))
        {
            const std::filesystem::path sceneFolder = m_app->GetSceneDir();
            ShellExecuteW(nullptr, L"open", sceneFolder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        {
            const ImVec2 iconMin = ImGui::GetItemRectMin();
            const ImVec2 iconMax = ImGui::GetItemRectMax();
            const float iconWidth = iconMax.x - iconMin.x;
            const float iconHeight = iconMax.y - iconMin.y;
            const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 bodyMin(iconMin.x + iconWidth * 0.20f, iconMin.y + iconHeight * 0.38f);
            const ImVec2 bodyMax(iconMax.x - iconWidth * 0.20f, iconMax.y - iconHeight * 0.22f);
            drawList->AddRect(bodyMin, bodyMax, iconColor, 1.5f, 0, 1.5f);
            drawList->AddLine(
                ImVec2(bodyMin.x, bodyMin.y),
                ImVec2(bodyMin.x + iconWidth * 0.22f, iconMin.y + iconHeight * 0.27f),
                iconColor, 1.5f);
            drawList->AddLine(
                ImVec2(bodyMin.x + iconWidth * 0.22f, iconMin.y + iconHeight * 0.27f),
                ImVec2(bodyMin.x + iconWidth * 0.40f, bodyMin.y),
                iconColor, 1.5f);
        }
        ImGui::SetItemTooltip("Open the scene folder in File Explorer.");

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
                m_ui.PbrDebug = PbrDebugView::FinalLinearHdr;
                log::info("PBR rendering %s", m_ui.EnablePbr ? "enabled" : "disabled");
            }
            ImGui::SetItemTooltip("Use UVSR PBR shading; disable to compare Donut legacy shading.");
        }
        }

        const bool aliasingOpen = ImGui::CollapsingHeader(
            "Aliasing",
            ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SetItemTooltip(
            "Configure native-resolution analytical reconstructive temporal anti-aliasing.");
        if (aliasingOpen)
        {
            ReconstructiveTemporalAASettings& rtaa =
                m_ui.ReconstructiveTemporalAA;
            bool advancedChanged = false;

            if (ImGui::Checkbox("Enabled##RTAA", &rtaa.enabled))
                m_app->ResetReconstructiveTemporalAAHistory();
            ImGui::SetItemTooltip(
                "Enable RTAA for the deferred UVSR PBR final-color path.");
            ImGui::TextUnformatted("Technique: Reconstructive TAA");
            ImGui::SetItemTooltip(
                "Native-resolution Analytical Reconstructive Temporal Anti-Aliasing (NRA-RTAA).");

            ImGui::SetNextItemWidth(settingsControlWidth);
            if (ImGui::BeginCombo(
                "Preset##RTAA",
                GetReconstructiveTemporalPresetName(rtaa.preset)))
            {
                for (uint32_t presetIndex = 0; presetIndex < 4; ++presetIndex)
                {
                    const auto preset =
                        ReconstructiveTemporalPreset(presetIndex);
                    const bool selected = rtaa.preset == preset;
                    if (ImGui::Selectable(
                        GetReconstructiveTemporalPresetName(preset), selected))
                    {
                        ApplyReconstructiveTemporalPreset(rtaa, preset);
                        rtaa.forceHistoryReset = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose stability-, balance-, or clarity-biased analytical settings; manual edits select Custom.");

            ImGui::SetNextItemWidth(settingsControlWidth);
            if (ImGui::BeginCombo(
                "Performance Profile##RTAA",
                GetReconstructiveTemporalPerformanceProfileName(
                    rtaa.performanceProfile)))
            {
                for (uint32_t profileIndex = 0; profileIndex < 3; ++profileIndex)
                {
                    const auto profile =
                        ReconstructiveTemporalPerformanceProfile(profileIndex);
                    const bool selected = rtaa.performanceProfile == profile;
                    if (ImGui::Selectable(
                        GetReconstructiveTemporalPerformanceProfileName(profile),
                        selected))
                    {
                        rtaa.performanceProfile = profile;
                        rtaa.forceHistoryReset = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Select a statically compiled GPU-cost tier independently of Heavy, Medium, or Light temporal response. Performance and Balanced use bilinear history; Maximum Quality enables the selected filter, thin diffusion, and optional resurrection.");

            if (const ReconstructiveTemporalAATimings* timings =
                m_app->GetReconstructiveTemporalAATimings())
            {
                ImGui::Text(
                    "GPU %.2f ms | Prepare %.2f | Resolve %.2f | AgX+sharpen %.2f",
                    timings->totalMs, timings->prepareMs, timings->resolveMs,
                    timings->fusedSharpeningMs);
                ImGui::SetItemTooltip(
                    "Latest GPU intervals. Resurrection is a conditional read included in Resolve. AgX+sharpen is the measured fused display-pass interval, an upper bound rather than isolated sharpening cost.");
            }
            else
            {
                ImGui::TextUnformatted("GPU -- ms");
            }

            if (ImGui::Button("Reset History##RTAA"))
                m_app->ResetReconstructiveTemporalAAHistory();
            ImGui::SetItemTooltip(
                "Discard all immediate and persistent RTAA history on the next frame.");
#if defined(_DEBUG)
            ImGui::SameLine();
            if (ImGui::Checkbox("Freeze Jitter", &rtaa.freezeJitter))
                rtaa.forceHistoryReset = true;
            ImGui::SetItemTooltip(
                "Developer mode: hold projection jitter at exactly zero.");
#endif

            const bool rtaaAvailable = m_ui.EnablePbr &&
                m_ui.UsesDeferredShading() &&
                m_ui.PbrDebug == PbrDebugView::FinalLinearHdr;
            if (!rtaaAvailable)
            {
                ImGui::TextDisabled(
                    "Requires deferred UVSR PBR with Final linear HDR output.");
            }

            if (ImGui::TreeNodeEx(
                "Projection Jitter", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (ImGui::BeginCombo(
                    "Sequence##RTAA", GetJitterSequenceName(rtaa.jitterSequence)))
                {
                    for (uint32_t value = 0; value < 2; ++value)
                    {
                        const auto sequence = JitterSequence(value);
                        const bool selected = sequence == rtaa.jitterSequence;
                        if (ImGui::Selectable(
                            GetJitterSequenceName(sequence), selected))
                        {
                            rtaa.jitterSequence = sequence;
                            advancedChanged = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Select deterministic R2 or Halton base-2/3 subpixel sampling.");

                int jitterPeriod = int(rtaa.jitterPeriod);
                if (ImGui::SliderInt(
                    "Sequence Length##RTAA", &jitterPeriod, 2, 64))
                {
                    rtaa.jitterPeriod = uint32_t(jitterPeriod);
                    advancedChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Set the deterministic phase period; two positions are diagnostic-only.");
                advancedChanged |= ImGui::SliderFloat(
                    "Jitter Scale##RTAA", &rtaa.jitterScale, 0.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Scale the centered projection offset up to half a pixel per axis.");

#if defined(_DEBUG)
                advancedChanged |= ImGui::Checkbox(
                    "Hold Sequence Phase##RTAA", &rtaa.holdJitterPhase);
                ImGui::SetItemTooltip(
                    "Developer mode: repeat one real sequence sample; unlike Freeze Jitter, this does not force a zero offset.");
                ImGui::BeginDisabled(!rtaa.holdJitterPhase);
                int heldJitterPhase = int(rtaa.heldJitterPhase);
                if (ImGui::SliderInt(
                    "Held Phase##RTAA", &heldJitterPhase, 0,
                    std::max(int(rtaa.jitterPeriod) - 1, 0)))
                {
                    rtaa.heldJitterPhase = uint32_t(heldJitterPhase);
                    advancedChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Select the deterministic sequence index repeated while phase hold is enabled.");
                ImGui::EndDisabled();
#endif

                bool visualizeJitter = rtaa.debugMode ==
                    ReconstructiveTemporalDebugMode::CurrentJitter;
                if (ImGui::Checkbox("Visualize Jitter##RTAA", &visualizeJitter))
                {
                    rtaa.debugMode = visualizeJitter
                        ? ReconstructiveTemporalDebugMode::CurrentJitter
                        : ReconstructiveTemporalDebugMode::FinalOutput;
                }
                ImGui::SetItemTooltip(
                    "Show the current centered jitter position without changing temporal history.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                "Motion and Reprojection", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (ImGui::BeginCombo(
                    "History Filter##RTAA",
                    GetHistorySampleFilterName(rtaa.historyFilter)))
                {
                    for (uint32_t value = 0; value < 2; ++value)
                    {
                        const auto filter = HistorySampleFilter(value);
                        const bool selected = filter == rtaa.historyFilter;
                        if (ImGui::Selectable(
                            GetHistorySampleFilterName(filter), selected))
                        {
                            rtaa.historyFilter = filter;
                            advancedChanged = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose the Maximum Quality history filter. Performance and Balanced use their statically compiled bilinear path.");
                advancedChanged |= ImGui::Checkbox(
                    "Velocity Dilation##RTAA", &rtaa.velocityDilationEnabled);
                ImGui::SetItemTooltip(
                    "Select the nearest valid 3x3 surface velocity at silhouettes.");
                advancedChanged |= ImGui::SliderFloat(
                    "Motion Response Start##RTAA",
                    &rtaa.motionResponseStartPixels, 0.f, 16.f, "%.2f px");
                ImGui::SetItemTooltip(
                    "Begin shifting from stable to moving temporal response at this velocity.");
                advancedChanged |= ImGui::SliderFloat(
                    "Motion Response End##RTAA",
                    &rtaa.motionResponseEndPixels, 0.1f, 64.f, "%.2f px");
                ImGui::SetItemTooltip(
                    "Reach the moving temporal response at this velocity.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                "Surface Validation", ImGuiTreeNodeFlags_DefaultOpen))
            {
                advancedChanged |= ImGui::SliderFloat(
                    "Absolute Depth Error##RTAA",
                    &rtaa.absoluteDepthThreshold, 0.f, 0.25f, "%.5f");
                ImGui::SetItemTooltip(
                    "Allow this absolute predicted-versus-history view-depth error.");
                advancedChanged |= ImGui::SliderFloat(
                    "Relative Depth Error##RTAA",
                    &rtaa.relativeDepthThreshold, 0.f, 0.25f, "%.5f");
                ImGui::SetItemTooltip(
                    "Scale allowed depth error by current and previous view depth.");
                advancedChanged |= ImGui::SliderFloat(
                    "Normal Reject Cosine##RTAA",
                    &rtaa.normalRejectCosine, -1.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Give zero normal confidence at or below this world-normal dot product.");
                advancedChanged |= ImGui::SliderFloat(
                    "Normal Accept Cosine##RTAA",
                    &rtaa.normalAcceptCosine, -1.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Give full normal confidence at or above this world-normal dot product.");
                advancedChanged |= ImGui::Checkbox(
                    "Validate Material Identity##RTAA",
                    &rtaa.validateMaterialIdentity);
                ImGui::SetItemTooltip(
                    "Reject history reuse across exact stable 32-bit material IDs.");
                advancedChanged |= ImGui::Checkbox(
                    "Validate Object Identity##RTAA",
                    &rtaa.validateObjectIdentity);
                ImGui::SetItemTooltip(
                    "Also require the compact stable mesh-instance token; disable for unstable animated instances.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                "Reactive Shading", ImGuiTreeNodeFlags_DefaultOpen))
            {
                advancedChanged |= ImGui::Checkbox(
                    "Explicit Reactive Mask##RTAA",
                    &rtaa.explicitReactiveMaskEnabled);
                ImGui::SetItemTooltip(
                    "Consume an application-authored transient transport mask. The current depth-writing PBR producer stays neutral so stable alpha-tested and emissive details can accumulate.");
                advancedChanged |= ImGui::Checkbox(
                    "Automatic Reactive Mask##RTAA",
                    &rtaa.automaticReactiveMaskEnabled);
                ImGui::SetItemTooltip(
                    "Estimate motion-corroborated shading change from bounded log-luminance and chroma residuals; stationary residuals remain governed by neighborhood clipping.");
                advancedChanged |= ImGui::SliderFloat(
                    "Automatic Strength##RTAA",
                    &rtaa.automaticReactiveStrength, 0.f, 2.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Scale the automatic reactive estimate before combining it with the explicit mask.");
                advancedChanged |= ImGui::SliderFloat(
                    "Luminance Threshold##RTAA",
                    &rtaa.reactiveLuminanceThreshold, 0.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Ignore smaller exposure-normalized log-luminance changes.");
                advancedChanged |= ImGui::SliderFloat(
                    "Chroma Threshold##RTAA",
                    &rtaa.reactiveChromaThreshold, 0.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Ignore smaller bounded chroma changes.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                "Thin Geometry", ImGuiTreeNodeFlags_DefaultOpen))
            {
                advancedChanged |= ImGui::Checkbox(
                    "Enabled##RTAAThin", &rtaa.thinGeometryEnabled);
                ImGui::SetItemTooltip(
                    "Track bounded temporal coverage for fences, foliage, hair cards, cables, and thin silhouettes.");
                advancedChanged |= ImGui::SliderFloat(
                    "Depth Range##RTAAThin",
                    &rtaa.thinGeometryDepthRange, 0.001f, 0.25f, "%.4f");
                ImGui::SetItemTooltip(
                    "Set the relative depth discontinuity used by thin-structure classification.");
                advancedChanged |= ImGui::SliderFloat(
                    "Contrast Threshold##RTAAThin",
                    &rtaa.thinGeometryContrastThreshold, 0.f, 2.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Require this compressed-luminance edge contrast to corroborate thin geometry.");
                advancedChanged |= ImGui::SliderFloat(
                    "Coverage Response##RTAAThin",
                    &rtaa.thinGeometryCoverageResponseMs, 1.f, 500.f, "%.1f ms");
                ImGui::SetItemTooltip(
                    "Set the frame-rate-independent response of accumulated subpixel coverage.");
                advancedChanged |= ImGui::SliderFloat(
                    "Maximum Relaxation##RTAAThin",
                    &rtaa.thinGeometryMaximumRelaxation, 0.f, 0.10f, "%.4f");
                ImGui::SetItemTooltip(
                    "Bound validation relaxation to stable, locally classified thin geometry.");
                advancedChanged |= ImGui::Checkbox(
                    "Cluster Diffusion##RTAAThin",
                    &rtaa.thinGeometryClusterDiffusion);
                ImGui::SetItemTooltip(
                    "Diffuse thin classification through a gated shared-memory 3x3 cluster in Maximum Quality. Balanced keeps center structural evidence; Performance keeps authored plus cheap silhouette coverage.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                "History Clipping", ImGuiTreeNodeFlags_DefaultOpen))
            {
                advancedChanged |= ImGui::SliderFloat(
                    "Variance Sigma##RTAA", &rtaa.varianceClipSigma,
                    0.f, 4.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Set the YCoCg current-neighborhood variance interval.");
                advancedChanged |= ImGui::SliderFloat(
                    "Luminance Clip Strength##RTAA",
                    &rtaa.luminanceClipStrength, 0.f, 2.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Scale luminance bounds without clamping scene-linear HDR to display range.");
                advancedChanged |= ImGui::SliderFloat(
                    "Chroma Clip Strength##RTAA",
                    &rtaa.chromaClipStrength, 0.f, 2.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Scale chroma bounds while preserving hue with line-box clipping.");
                advancedChanged |= ImGui::SliderFloat(
                    "Thin Clip Expansion##RTAA",
                    &rtaa.thinGeometryClipExpansion, 0.f, 0.10f, "%.4f");
                ImGui::SetItemTooltip(
                    "Bound neighborhood expansion for validated stable thin geometry.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                "Temporal Accumulation", ImGuiTreeNodeFlags_DefaultOpen))
            {
                advancedChanged |= ImGui::SliderFloat(
                    "Stable Response##RTAA", &rtaa.stableResponseMs,
                    1.f, 1000.f, "%.1f ms");
                ImGui::SetItemTooltip(
                    "Set the stable-surface time constant; higher values retain history longer.");
                advancedChanged |= ImGui::SliderFloat(
                    "Moving Response##RTAA", &rtaa.movingResponseMs,
                    1.f, 500.f, "%.1f ms");
                ImGui::SetItemTooltip(
                    "Set the moving-surface time constant for motion clarity.");
                advancedChanged |= ImGui::SliderFloat(
                    "Reactive Response##RTAA", &rtaa.reactiveResponseMs,
                    0.5f, 100.f, "%.1f ms");
                ImGui::SetItemTooltip(
                    "Set the reactive-shading time constant; shorter values favor the current frame.");

                int maximumHistory = int(rtaa.maximumHistorySamples);
                if (ImGui::SliderInt(
                    "Maximum History Samples##RTAA", &maximumHistory, 1, 255))
                {
                    rtaa.maximumHistorySamples = uint32_t(maximumHistory);
                    advancedChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Limit per-pixel stable history length and enforce its minimum current contribution.");
                int maximumMovingHistory = int(rtaa.maximumMovingHistorySamples);
                if (ImGui::SliderInt(
                    "Maximum Moving Samples##RTAA",
                    &maximumMovingHistory, 1, 64))
                {
                    rtaa.maximumMovingHistorySamples =
                        uint32_t(maximumMovingHistory);
                    advancedChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Use a shorter sample cap as motion approaches the moving response.");

                const ReconstructiveTemporalWeights weights =
                    ComputeReconstructiveTemporalWeights(
                        rtaa, m_app->GetLastFrameDeltaSeconds());
                ImGui::Text(
                    "Current weights | Stable %.3f | Moving %.3f | Reactive %.3f",
                    weights.stableCurrentWeight,
                    weights.movingCurrentWeight,
                    weights.reactiveCurrentWeight);
                ImGui::SetItemTooltip(
                    "Read-only CPU weights calculated from this frame's delta time and the three time constants.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                "Spatial Fallback", ImGuiTreeNodeFlags_DefaultOpen))
            {
                advancedChanged |= ImGui::Checkbox(
                    "Enabled##RTAASpatial", &rtaa.spatialFallbackEnabled);
                ImGui::SetItemTooltip(
                    "Use edge-aware current-frame AA only where temporal confidence is low.");
                int radius = int(rtaa.spatialFallbackRadius);
                if (ImGui::SliderInt(
                    "Radius##RTAASpatial", &radius, 1, 2))
                {
                    rtaa.spatialFallbackRadius = uint32_t(radius);
                    advancedChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Choose the Maximum Quality 3x3 or 5x5 kernel. Balanced uses fixed 3x3; Performance uses a low-cost cross.");
                advancedChanged |= ImGui::SliderFloat(
                    "Depth Weight##RTAASpatial",
                    &rtaa.spatialDepthWeight, 0.f, 256.f, "%.2f");
                ImGui::SetItemTooltip(
                    "Suppress fallback samples across view-depth discontinuities.");
                advancedChanged |= ImGui::SliderFloat(
                    "Normal Weight##RTAASpatial",
                    &rtaa.spatialNormalWeight, 0.f, 64.f, "%.2f");
                ImGui::SetItemTooltip(
                    "Suppress fallback samples across opposing surface normals.");
                advancedChanged |= ImGui::SliderFloat(
                    "Luminance Weight##RTAASpatial",
                    &rtaa.spatialLuminanceWeight, 0.f, 32.f, "%.2f");
                ImGui::SetItemTooltip(
                    "Suppress fallback samples across incompatible HDR luminance.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                "History Resurrection", ImGuiTreeNodeFlags_DefaultOpen))
            {
                advancedChanged |= ImGui::Checkbox(
                    "Enabled##RTAAResurrection", &rtaa.resurrectionEnabled);
                ImGui::SetItemTooltip(
                    "Conditionally inspect older validated history after immediate history fails. Compiled out unless Maximum Quality is selected.");
                if (!rtaa.resurrectionEnabled)
                    ImGui::BeginDisabled();
                int persistentCount = int(std::max(rtaa.persistentFrameCount, 1u));
                if (ImGui::SliderInt(
                    "Persistent Frames##RTAA", &persistentCount, 1, 2))
                {
                    rtaa.persistentFrameCount = uint32_t(persistentCount);
                    advancedChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Retain at most two older no-copy history-ring positions.");
                int persistentInterval = int(rtaa.persistentFrameInterval);
                if (ImGui::SliderInt(
                    "Frame Interval##RTAA", &persistentInterval, 1, 2))
                {
                    rtaa.persistentFrameInterval = uint32_t(persistentInterval);
                    advancedChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Choose the age spacing between persistent history candidates.");
                advancedChanged |= ImGui::SliderFloat(
                    "Maximum Weight##RTAAResurrection",
                    &rtaa.maximumResurrectionWeight, 0.f, 0.5f, "%.3f");
                ImGui::SetItemTooltip(
                    "Cap older-history contribution so stale lighting cannot dominate.");
                advancedChanged |= ImGui::SliderFloat(
                    "Match Threshold##RTAAResurrection",
                    &rtaa.resurrectionMatchThreshold, 0.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Require this validated color/surface confidence before resurrecting history.");
                if (!rtaa.resurrectionEnabled)
                    ImGui::EndDisabled();
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx(
                "Sharpening", ImGuiTreeNodeFlags_DefaultOpen))
            {
                advancedChanged |= ImGui::Checkbox(
                    "Enabled##RTAASharpen", &rtaa.sharpeningEnabled);
                ImGui::SetItemTooltip(
                    "Apply conservative luminance sharpening fused into AgX after accumulation. Performance disables the extra neighborhood reads.");
                advancedChanged |= ImGui::SliderFloat(
                    "Strength##RTAASharpen",
                    &rtaa.sharpeningStrength, 0.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Set the bounded unsharp contribution; sharpening never enters history.");
                advancedChanged |= ImGui::SliderFloat(
                    "Motion Suppression##RTAASharpen",
                    &rtaa.sharpeningMotionSuppression, 0.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Reduce sharpening as velocity approaches the moving response.");
                advancedChanged |= ImGui::SliderFloat(
                    "Reactive Suppression##RTAASharpen",
                    &rtaa.sharpeningReactiveSuppression, 0.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Reduce sharpening on temporally changing shading and transparency.");
                advancedChanged |= ImGui::SliderFloat(
                    "Variance Suppression##RTAASharpen",
                    &rtaa.sharpeningVarianceSuppression, 0.f, 1.f, "%.3f");
                ImGui::SetItemTooltip(
                    "Reduce sharpening where log-luminance history variance is high.");
                advancedChanged |= ImGui::SliderFloat(
                    "Halo Clamp##RTAASharpen",
                    &rtaa.sharpeningHaloClamp, 0.f, 0.5f, "%.3f");
                ImGui::SetItemTooltip(
                    "Limit sharpening overshoot relative to the local valid luminance range.");
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Debug##RTAA"))
            {
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (ImGui::BeginCombo(
                    "Debug Mode##RTAA",
                    GetReconstructiveTemporalDebugModeName(rtaa.debugMode)))
                {
                    for (uint32_t modeIndex = 0;
                        modeIndex < ReconstructiveTemporalDebugModeCount;
                        ++modeIndex)
                    {
                        const auto mode =
                            ReconstructiveTemporalDebugMode(modeIndex);
                        const bool selected = rtaa.debugMode == mode;
                        if (ImGui::Selectable(
                            GetReconstructiveTemporalDebugModeName(mode), selected))
                        {
                            rtaa.debugMode = mode;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Visualize RTAA inputs, classification, validation, clipping, weighting, fallback, resurrection, sharpening, or rejection reasons.");

                if (const ReconstructiveTemporalAAMemoryStats* memory =
                    m_app->GetReconstructiveTemporalAAMemoryStats())
                {
                    const double mebibytes =
                        double(memory->totalBytes) / (1024.0 * 1024.0);
                    ImGui::Text(
                        "RTAA %.1f MiB | %u history slots | ~%.0f B read + %.0f B write / pixel",
                        mebibytes, memory->historySlotCount,
                        memory->approximateReadBytesPerPixel,
                        memory->approximateWriteBytesPerPixel);
                    ImGui::SetItemTooltip(
                        "Native-size pass-owned allocation and steady-state logical texel footprint before cache reuse; conditional resurrection reads are excluded.");
                }
                ImGui::TreePop();
            }

            if (advancedChanged)
            {
                rtaa.preset = ReconstructiveTemporalPreset::Custom;
                SanitizeReconstructiveTemporalSettings(rtaa);
            }
        }

        const bool indirectLightingOpen = DrawCollapsingHeader(
            "Visibility",
            "Configure the shared visibility traversal used by ambient occlusion and diffuse GI.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (indirectLightingOpen)
        {
            ScreenSpaceVisibilitySettings& visibility = m_ui.ScreenSpaceVisibility;
            const bool visibilityAvailable = m_ui.UsesDeferredShading() && m_ui.EnablePbr;
            if (!visibilityAvailable)
                ImGui::BeginDisabled();

            ImGui::Checkbox("Enabled##ScreenSpaceVisibility", &visibility.enabled);
            ImGui::SetItemTooltip(
                "Enable the shared screen-space visibility producer and indirect-light composite.");

            static const char* qualityLabels[] = {
                "Low", "Medium", "High", "Ultra", "Custom"
            };
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (ImGui::BeginCombo("Quality", qualityLabels[int(visibility.quality)]))
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
            ImGui::SetItemTooltip(
                "Choose a full-resolution stochastic sample budget preset.");

            if (const ScreenSpaceVisibilityTimings* timings =
                m_app->GetScreenSpaceVisibilityTimings())
            {
                const float traceMilliseconds =
                    timings->depthHierarchyMs + timings->samplingMs;
                ImGui::Text(
                    "All %.2f ms | Trace %.2f ms | Composite %.2f ms",
                    timings->CompleteEffectMs(), traceMilliseconds,
                    timings->compositionMs);
                ImGui::SetItemTooltip(
                    "Latest GPU times. Trace groups hierarchy construction and visibility sampling.");
            }

            if (!visibility.enabled)
                ImGui::BeginDisabled();

            if (ImGui::TreeNodeEx("Shared Visibility Sampling", ImGuiTreeNodeFlags_DefaultOpen))
            {
                SharedSamplingSettings& sampling = visibility.sampling;
                bool samplingChanged = false;
                static const char* estimatorLabels[] = {
                    "Paper Angular",
                    "GT Uniform",
                    "GT Cosine (Gated)"
                };
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (ImGui::BeginCombo(
                    "Estimator",
                    estimatorLabels[int(visibility.estimator)]))
                {
                    for (int estimatorIndex = 0;
                        estimatorIndex < int(std::size(estimatorLabels));
                        ++estimatorIndex)
                    {
                        const bool implemented = estimatorIndex <
                            int(ImplementedVisibilityEstimatorCount);
                        if (!implemented)
                            ImGui::BeginDisabled();
                        const auto estimator = VisibilityEstimator(estimatorIndex);
                        const bool selected = visibility.estimator == estimator;
                        if (ImGui::Selectable(
                                estimatorLabels[estimatorIndex], selected) &&
                            implemented)
                        {
                            visibility.estimator = estimator;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                        if (!implemented)
                            ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "A/B the shipping paper-angular estimator against the complete no-acos GTUniform formulation. GTCosine remains disabled until GTUniform clears reference, image-quality, and hardware promotion gates.");
                samplingChanged |= ImGui::SliderFloat(
                    "Radius", &sampling.radius, 0.01f, std::max(m_app->GetSceneDiagonal() * 0.1f, 1.f), "%.3f");
                ImGui::SetItemTooltip("Set the world-space radius projected into screen space.");
                samplingChanged |= ImGui::SliderFloat(
                    "Thickness", &sampling.thickness, 0.0f, std::max(m_app->GetSceneDiagonal() * 0.02f, 0.5f), "%.3f");
                ImGui::SetItemTooltip(
                    "Set finite occluder thickness in UVSR world units; thin surfaces leave light paths open.");
                samplingChanged |= ImGui::Checkbox(
                    "Distance-Scaled Thickness", &sampling.distanceScaledThickness);
                ImGui::SetItemTooltip("Increase finite thickness gradually with view distance.");
                if (!sampling.distanceScaledThickness)
                    ImGui::BeginDisabled();
                samplingChanged |= ImGui::SliderFloat(
                    "Thickness Distance Scale", &sampling.thicknessDistanceScale,
                    0.0f, 0.02f, "%.5f");
                ImGui::SetItemTooltip("Control the per-view-depth thickness increase.");
                if (!sampling.distanceScaledThickness)
                    ImGui::EndDisabled();

                int sampleCount = int(sampling.sampleCount);
                if (ImGui::SliderInt("Samples Per Pixel", &sampleCount, 1, 64))
                {
                    sampling.sampleCount = uint32_t(sampleCount);
                    samplingChanged = true;
                }
                ImGui::SetItemTooltip(
                    "First-bounce stochastic depth taps per pixel, divided between the two near-to-far horizon directions. Later GI bounces progressively halve this budget toward 8 samples.");
                samplingChanged |= ImGui::SliderFloat(
                    "Step Distribution", &sampling.stepDistributionExponent, 0.5f, 4.0f, "%.2f");
                ImGui::SetItemTooltip("Bias radial samples toward the receiving pixel.");
                samplingChanged |= ImGui::SliderFloat(
                    "Sample Jitter", &sampling.radialJitter, 0.0f, 1.0f, "%.2f");
                ImGui::SetItemTooltip(
                    "Blend from centered samples to the deterministic spatiotemporal radial jitter sequence.");

                samplingChanged |= ImGui::Checkbox(
                    "Hierarchical View Depth", &sampling.useDepthHierarchy);
                ImGui::SetItemTooltip(
                    "Use the single-dispatch XeGTAO-style five-level view-depth pyramid for distant samples.");

                if (samplingChanged)
                    visibility.quality = ScreenSpaceVisibilityQuality::Custom;
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen))
            {
                AmbientOcclusionSettings& ao = visibility.ambientOcclusion;
                ImGui::Checkbox("Enabled##AmbientVisibility", &ao.enabled);
                ImGui::SetItemTooltip("Consume the final radial masks as scalar ambient visibility.");
                if (!ao.enabled)
                    ImGui::BeginDisabled();
                ImGui::SliderFloat("Strength", &ao.strength, 0.0f, 2.0f, "%.2f");
                ImGui::SetItemTooltip(
                    "Scale only the missing-visibility adjustment on fallback indirect diffuse.");
                ImGui::SliderFloat("Power##AmbientVisibility", &ao.power, 0.25f, 4.0f, "%.2f");
                ImGui::SetItemTooltip("Shape ambient visibility after radial-mask evaluation.");
                if (!ao.enabled)
                    ImGui::EndDisabled();
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Indirect Diffuse GI", ImGuiTreeNodeFlags_DefaultOpen))
            {
                IndirectDiffuseSettings& gi = visibility.indirectDiffuse;
                ImGui::Checkbox("Enabled##IndirectDiffuse", &gi.enabled);
                ImGui::SetItemTooltip(
                    "Accumulate diffuse irradiance only from newly claimed mask sectors.");
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
                }
                ImGui::SetItemTooltip(
                    "Choose 1-4 finite diffuse-light bounces. Later bounces run GI-only traversals and progressively halve the selected sample budget toward 8, reducing their cost while preserving full quality for bounce one.");
                ImGui::SliderFloat(
                    "Bounce Contribution Cutoff",
                    &gi.minimumBounceContribution,
                    0.0f,
                    0.02f,
                    "%.5f");
                ImGui::SetItemTooltip(
                    "Skip higher-bounce source energy whose conservative exposed scene-linear contribution cannot exceed this value before tone mapping. Set 0 for exact-zero exits only; GI-result diagnostics use the exact selected chain, while unrelated diagnostics skip secondary bounces.");
                ImGui::SliderFloat("Intensity##IndirectDiffuse", &gi.intensity, 0.0f, 10.0f, "%.2f");
                ImGui::SetItemTooltip("Scale material-applied screen-space indirect diffuse.");
                ImGui::Checkbox("Include Emissive Sources", &gi.includeEmissive);
                ImGui::SetItemTooltip(
                    "Allow visible emissive G-buffer surfaces to illuminate receivers; set their global light strength in Lights.");
                bool giLightOnly = visibility.debug.mode ==
                    ScreenSpaceVisibilityDebugMode::GiLightOnly;
                if (ImGui::Checkbox("GI Light Only", &giLightOnly))
                {
                    visibility.debug.mode = giLightOnly
                        ? ScreenSpaceVisibilityDebugMode::GiLightOnly
                        : ScreenSpaceVisibilityDebugMode::FinalComposite;
                }
                ImGui::SetItemTooltip(
                    "Show indirect irradiance before receiver albedo, metalness, and the diffuse BRDF.");
                if (!gi.enabled)
                    ImGui::EndDisabled();
                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Debug##ScreenSpaceVisibility"))
            {
                static const char* debugLabels[] = {
                    "Final Composite",
                    "Ambient Visibility",
                    "Indirect Diffuse",
                    "GI Light Only",
                    "Direct-Radiance Source",
                    "Receiver Normal",
                    "Sampled Source Normal",
                    "Sample Count",
                    "Newly Covered Sectors",
                    "Final Mask Popcount",
                    "Current Slice Orientation",
                    "Projected Normal",
                    "Front Horizon Angle",
                    "Back Horizon Angle",
                    "Thickness Interval",
                    "GT Endpoint Order Failures"
                };
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (ImGui::BeginCombo(
                    "Debug Mode", debugLabels[int(visibility.debug.mode)]))
                {
                    for (int index = 0; index < int(std::size(debugLabels)); ++index)
                    {
                        const auto mode = ScreenSpaceVisibilityDebugMode(index);
                        const bool selected = visibility.debug.mode == mode;
                        if (ImGui::Selectable(debugLabels[index], selected))
                            visibility.debug.mode = mode;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("Visualize visibility traversal or composition state.");
                ImGui::Checkbox("Freeze Sampling Phase", &visibility.debug.freezeSamplingPhase);
                ImGui::SetItemTooltip("Hold slice rotation and radial jitter at deterministic phase zero.");

                static const char* criterionLabels[] = { "Round", "Ceil", "Floor" };
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (ImGui::BeginCombo(
                    "Sector Hit Criterion",
                    criterionLabels[int(visibility.debug.sectorHitCriterion)]))
                {
                    for (int index = 0; index < int(std::size(criterionLabels)); ++index)
                    {
                        const auto criterion = SectorHitCriterion(index);
                        const bool selected = visibility.debug.sectorHitCriterion == criterion;
                        if (ImGui::Selectable(criterionLabels[index], selected))
                            visibility.debug.sectorHitCriterion = criterion;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose developer validation semantics; Round is the production paper criterion.");
                ImGui::TreePop();
            }

            if (!visibility.enabled)
                ImGui::EndDisabled();
            if (!visibilityAvailable)
            {
                ImGui::EndDisabled();
                ImGui::TextDisabled("Requires deferred UVSR PBR rendering.");
            }
        }

        const bool tonemapperOpen = DrawCollapsingHeader(
            "Tonemapper",
            "Expand AgX tonemapping and grading controls.");
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
            ImGui::SetItemTooltip(
                "Apply a predefined grade; editing a value switches to Custom.");
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
            ImGui::SetItemTooltip("Apply a film-look LUT.");
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
            ImGui::SetItemTooltip("Adjust scene exposure in EV before tonemapping.");
            gradeChanged |= ImGui::SliderFloat("Contrast", &params.Contrast, 0.5f, 2.f, "%.3f");
            ImGui::SetItemTooltip("Adjust contrast in AgX Base space.");
            gradeChanged |= ImGui::SliderFloat("Saturation", &params.Saturation, 0.f, 2.f, "%.3f");
            ImGui::SetItemTooltip("Adjust color saturation in AgX Base space.");
            gradeChanged |= ImGui::SliderFloat("Warmth", &params.Warmth, -1.f, 1.f, "%.3f");
            ImGui::SetItemTooltip("Shift the grade toward warmer or cooler colors.");
            gradeChanged |= ImGui::SliderFloat("Tint", &params.Tint, -1.f, 1.f, "%.3f");
            ImGui::SetItemTooltip("Shift the grade toward green or magenta.");
            gradeChanged |= ImGui::SliderFloat(
                "Slope", &params.Slope, 0.f, 2.f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip(
                "Multiply the AgX Base-space grade before Power. 1.0 is neutral.");
            gradeChanged |= ImGui::SliderFloat(
                "Power", &params.Power, 0.01f, 2.f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip(
                "Apply the grade exponent. Below 1 brightens; above 1 darkens.");

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
            "Sky", "Expand procedural sky controls.");
        if (skyOpen)
        {
            if (!m_ui.EnableProceduralSky)
                ImGui::BeginDisabled();
            ImGui::SliderFloat("Brightness", &m_ui.SkyParams.brightness, 0.f, 1.f);
            ImGui::SetItemTooltip("Set sky and ambient-light brightness.");
            ImGui::SliderFloat("Glow Size", &m_ui.SkyParams.glowSize, 0.f, 90.f);
            ImGui::SetItemTooltip("Set the sun-glow angular size.");
            ImGui::SliderFloat("Glow Sharpness", &m_ui.SkyParams.glowSharpness, 1.f, 10.f);
            ImGui::SetItemTooltip("Control how tightly the sun glow falls off.");
            ImGui::SliderFloat("Glow Intensity", &m_ui.SkyParams.glowIntensity, 0.f, 1.f);
            ImGui::SetItemTooltip("Set the sun-glow brightness.");
            ImGui::SliderFloat("Horizon Size", &m_ui.SkyParams.horizonSize, 0.f, 90.f);
            ImGui::SetItemTooltip("Set the horizon transition width.");
            if (!m_ui.EnableProceduralSky)
                ImGui::EndDisabled();

            ImGui::Checkbox("Enable Procedural Sky", &m_ui.EnableProceduralSky);
            ImGui::SetItemTooltip("Render the procedural sky behind the scene.");
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
            "Lights", "Expand scene-light and emissive-material lighting controls.");
        if (lightsOpen)
        {
            IndirectDiffuseSettings& gi = m_ui.ScreenSpaceVisibility.indirectDiffuse;
            ImGui::SliderFloat(
                "Emissive Material Light Strength", &gi.emissiveGain, 0.0f, 16.0f, "%.2f");
            ImGui::SetItemTooltip(
                "Globally scale light cast by emissive materials. Higher values expand the visibly illuminated area up to the Visibility sampling radius without changing the emissive surface itself.");

            if (!lights.empty())
            {
                ImGui::SetNextItemWidth(settingsControlWidth);
                const bool lightComboOpen = ImGui::BeginCombo(
                    "Select Light", m_SelectedLight ? m_SelectedLight->GetName().c_str() : "(None)");
                ImGui::SetItemTooltip("Choose which scene light to edit.");
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
        ImGui::SetItemTooltip("Recompile and reload renderer shaders.");

        ImGui::SameLine();
        if (ImGui::Button("Reset Settings", ImVec2(actionButtonWidth, 0.f)))
            m_app->ResetAllRendererSettings();
        ImGui::SetItemTooltip(
            "Restore factory renderer, aliasing, visibility, tonemapper, LUT, sky, and white-world settings. Camera and scene edits are left in place.");

        ImGui::SameLine();
        if (ImGui::Button("Restart", ImVec2(actionButtonWidth, 0.f)))
        {
            g_RestartRequested = true;
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
        }
        ImGui::SetItemTooltip("Restart UVSR and recreate the renderer.");

        ImGui::SameLine();
        if (ImGui::Button("Screenshot", ImVec2(actionButtonWidth, 0.f)))
            m_ui.CopyScreenshotToClipboard = true;
        ImGui::SetItemTooltip("Copy the current rendered frame to the clipboard.");

        ImGui::End();

        auto material = m_ui.SelectedMaterial;
        if (material)
        {
            ImGui::SetNextWindowPos(ImVec2(float(width) - fontSize * 0.6f, fontSize * 0.6f), 0, ImVec2(1.f, 0.f));
            ImGui::Begin("Material Editor");
            ImGui::Text("Material %d: %s", material->materialID, material->name.c_str());

            MaterialDomain previousDomain = material->domain;
            material->dirty = donut::app::MaterialEditor(material.get(), true);

            if (previousDomain != material->domain)
                m_app->GetScene()->GetSceneGraph()->GetRootNode()->InvalidateContent();
            
            ImGui::End();
        }

        ImGui::PopFont();
    }
};

void ProcessCommandLine(
    int argc,
    const char* const* argv,
    DeviceCreationParameters& deviceParams,
    std::string& sceneName,
    std::string& experimentName)
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
            experimentName = argv[++i];
        }
        else if (argv[i][0] != '-')
        {
            sceneName = argv[i];
        }
    }
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

    const int hour = localTime.tm_hour % 12 == 0 ? 12 : localTime.tm_hour % 12;
    std::ostringstream formattedTime;
    formattedTime << hour << ':' << std::setfill('0') << std::setw(2)
        << localTime.tm_min << (localTime.tm_hour < 12 ? " AM" : " PM");
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
    std::string experimentName;
    ProcessCommandLine(__argc, __argv, deviceParams, sceneName, experimentName);
    if (experimentName.empty())
    {
        // The launcher uses an environment variable so experiment descriptions
        // containing spaces reach WinMain without another layer of Windows
        // command-line quoting. An explicit argument remains the override for
        // direct and IDE-driven launches.
        const char* environmentExperiment = std::getenv("UVSR_EXPERIMENT");
        if (environmentExperiment && environmentExperiment[0] != '\0')
            experimentName = environmentExperiment;
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

    std::string windowTitle = "UVSR Renderer " + std::string(apiString);
    if (!experimentName.empty())
        windowTitle += " (" + experimentName + ", "
            + FormatExperimentLaunchTime(launchTime) + ")";

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
	{
        log::error("Cannot initialize a %s graphics device with the requested parameters", apiString);
		return 1;
	}

    {
        UIData uiData;
        uiData.GpuAdapterChoices = std::move(adapterChoices);
        uiData.ActiveGpuAdapterIndex = deviceParams.adapterIndex;

        std::shared_ptr<UvsrSceneViewer> demo = std::make_shared<UvsrSceneViewer>(deviceManager, uiData, sceneName);
        std::shared_ptr<UIRenderer> gui = std::make_shared<UIRenderer>(deviceManager, demo, uiData);

        gui->Init(demo->GetShaderFactory());

        deviceManager->AddRenderPassToBack(demo.get());
        deviceManager->AddRenderPassToBack(gui.get());

        deviceManager->RunMessageLoop();
    }

    deviceManager->Shutdown();
    delete deviceManager;

    if (g_RestartRequested && !RestartCurrentProcess())
        return 1;
	
	return 0;
}

