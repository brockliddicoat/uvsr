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
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cfloat>
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
#include <donut/render/SsaoPass.h>
#include <donut/app/ApplicationBase.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/app/Camera.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_renderer.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>

#include "pbr_material.h"

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;

static bool g_RestartRequested = false;

class PbrGBufferFillPass final : public GBufferFillPass
{
public:
    using GBufferFillPass::GBufferFillPass;

protected:
    nvrhi::ShaderHandle CreatePixelShader(
        ShaderFactory& shaderFactory,
        const CreateParameters& params,
        bool alphaTested) override
    {
        std::vector<ShaderMacro> macros;
        macros.emplace_back("MOTION_VECTORS", params.enableMotionVectors ? "1" : "0");
        macros.emplace_back("ALPHA_TESTED", alphaTested ? "1" : "0");
        return shaderFactory.CreateShader(
            "uvsr/pbr_gbuffer_ps.hlsl", "main", &macros, nvrhi::ShaderType::Pixel);
    }
};

class PbrForwardShadingPass final : public ForwardShadingPass
{
public:
    using ForwardShadingPass::ForwardShadingPass;

protected:
    nvrhi::ShaderHandle CreatePixelShader(
        ShaderFactory& shaderFactory,
        const CreateParameters&,
        bool transmissiveMaterial) override
    {
        std::vector<ShaderMacro> macros;
        macros.emplace_back("TRANSMISSIVE_MATERIAL", transmissiveMaterial ? "1" : "0");
        return shaderFactory.CreateShader(
            "uvsr/pbr_forward_ps.hlsl", "main", &macros, nvrhi::ShaderType::Pixel);
    }
};

class PbrDeferredLightingPass final : public DeferredLightingPass
{
public:
    using DeferredLightingPass::DeferredLightingPass;

protected:
    nvrhi::ShaderHandle CreateComputeShader(ShaderFactory& shaderFactory) override
    {
        return shaderFactory.CreateShader(
            "uvsr/pbr_deferred_lighting_cs.hlsl", "main", nullptr, nvrhi::ShaderType::Compute);
    }
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
    nvrhi::TextureHandle LdrColor;
    nvrhi::TextureHandle MaterialIDs;
    nvrhi::TextureHandle AmbientOcclusion;
    nvrhi::TextureHandle MaterialAmbientOcclusion;

    nvrhi::HeapHandle Heap;

    std::shared_ptr<FramebufferFactory> ForwardFramebuffer;
    std::shared_ptr<FramebufferFactory> LdrFramebuffer;
    std::shared_ptr<FramebufferFactory> MaterialIDFramebuffer;
    
    void Init(
        nvrhi::IDevice* device,
        dm::uint2 size,
        dm::uint sampleCount,
        bool enableMotionVectors,
        bool useReverseProjection) override
    {
        GBufferRenderTargets::Init(device, size, sampleCount, enableMotionVectors, useReverseProjection);

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

        desc.format = nvrhi::Format::R8_UNORM;
        desc.isUAV = true;
        desc.debugName = "AmbientOcclusion";
        AmbientOcclusion = device->createTexture(desc);

        if (desc.isVirtual)
        {
            uint64_t heapSize = 0;
            nvrhi::ITexture* const textures[] = {
                HdrColor,
                MaterialIDs,
                LdrColor,
                AmbientOcclusion
            };

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

    [[nodiscard]] bool IsUpdateRequired(uint2 size, uint sampleCount) const
    {
        if (any(m_Size != size) || m_SampleCount != sampleCount)
            return true;

        return false;
    }

    void Clear(nvrhi::ICommandList* commandList) override
    {
        GBufferRenderTargets::Clear(commandList);
        commandList->clearTextureFloat(
            MaterialAmbientOcclusion, nvrhi::AllSubresources, nvrhi::Color(1.f));

        commandList->clearTextureFloat(HdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
        commandList->clearTextureFloat(LdrColor, nvrhi::AllSubresources, nvrhi::Color(0.f));
    }
};

enum class WhiteWorldMode
{
    Off,
    On,
    PreserveDetail
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
    DirectLightVisibility,
    AmbientOcclusion
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
        "Direct-light visibility",
        "Ambient occlusion"
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

struct UIData
{
    bool                                ShowUI = true;
    bool                                UseDeferredShading = true;
    bool                                EnableSsao = true;
    SsaoParameters                      SsaoParams;
    AgxToneMappingParameters            AgxToneMappingParams;
    AgxPreset                           AgxToneMappingPreset = AgxPreset::Base;
    SkyParameters                       SkyParams;
    bool                                ShaderReloadRequested = false;
    PbrDebugView                        PbrDebug = PbrDebugView::FinalLinearHdr;
    bool                                EnableProceduralSky = true;
    WhiteWorldMode                      WhiteWorld = WhiteWorldMode::PreserveDetail;
    bool                                UseThirdPersonCamera = false;
    std::shared_ptr<Material>           SelectedMaterial;
    std::shared_ptr<SceneGraphNode>     SelectedNode;
    bool                                CopyScreenshotToClipboard = false;
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
    std::shared_ptr<PbrForwardShadingPass> m_ForwardPass;
    std::unique_ptr<PbrGBufferFillPass>  m_GBufferPass;
    std::unique_ptr<PbrDeferredLightingPass> m_DeferredLightingPass;
    std::unique_ptr<SkyPass>            m_SkyPass;
    std::unique_ptr<AgxToneMappingPass> m_AgxToneMappingPass;
    std::unique_ptr<SsaoPass>           m_SsaoPass;
    std::unique_ptr<MaterialIDPass>     m_MaterialIDPass;
    std::unique_ptr<PixelReadbackPass>  m_PixelReadbackPass;
    std::vector<KodakLut>               m_KodakLuts;
    size_t                              m_SelectedKodakLut = 0;

    std::shared_ptr<IView>              m_View;
    
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
            return true;
        }

        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F9 && action == GLFW_PRESS)
        {
            m_ui.PbrDebug = PbrDebugView(key - GLFW_KEY_F1);
            log::info("PBR debug view: %s%s",
                GetPbrDebugViewName(m_ui.PbrDebug),
                m_ui.UseDeferredShading ? "" : " (available in deferred shading)");
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
        GetActiveCamera().Animate(fElapsedTimeSeconds);
    }


    virtual void SceneUnloading() override
    {
        if (m_ForwardPass) m_ForwardPass->ResetBindingCache();
        if (m_DeferredLightingPass) m_DeferredLightingPass->ResetBindingCache();
        if (m_GBufferPass) m_GBufferPass->ResetBindingCache();
        m_BindingCache.Clear();
        m_SunLight.reset();
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_OriginalMaterials.clear();

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
            // use the scene's established unoccluded-light calibration.
            m_SunLight->irradiance = 0.35f;
        }
        
        m_ThirdPersonCamera.SetRotation(dm::radians(135.f), dm::radians(20.f));
        PointThirdPersonCameraAt(m_Scene->GetSceneGraph()->GetRootNode());

        m_ui.UseThirdPersonCamera = string_utils::ends_with(m_CurrentSceneName, ".gltf")
            || string_utils::ends_with(m_CurrentSceneName, ".glb");

        CopyThirdPersonToFirstPerson();

    }

    void SetWhiteWorldMode(WhiteWorldMode mode)
    {
        m_ui.WhiteWorld = mode;

        const bool enabled = mode != WhiteWorldMode::Off;
        const bool preserveDetailMaps = mode == WhiteWorldMode::PreserveDetail;

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
                material->domain = MaterialDomain::Opaque;
                material->useSpecularGlossModel = false;
                material->baseOrDiffuseColor = dm::float3(1.f);
                material->specularColor = dm::float3(0.04f);
                material->emissiveColor = dm::float3(0.f);
                material->emissiveIntensity = 1.f;
                material->metalness = 0.f;
                material->roughness = 0.72f;
                material->opacity = 1.f;
                material->transmissionFactor = 0.f;
                material->enableBaseOrDiffuseTexture = false;
                material->enableMetalRoughOrSpecularTexture = false;
                material->enableEmissiveTexture = false;
                material->enableTransmissionTexture = false;
                material->enableOpacityTexture = false;
                material->enableNormalTexture = preserveDetailMaps && original.enableNormalTexture;
                material->enableOcclusionTexture = preserveDetailMaps && original.enableOcclusionTexture;
                material->enableSubsurfaceScattering = false;
                material->enableHair = false;
            }

            ApplyPbrMaterialParameters(*material);
        }

        m_Scene->GetSceneGraph()->GetRootNode()->InvalidateContent();
    }

    void PointThirdPersonCameraAt(const std::shared_ptr<SceneGraphNode>& node)
    {
        dm::box3 bounds = node->GetGlobalBoundingBox();
        m_ThirdPersonCamera.SetTargetPosition(bounds.center());
        float radius = length(bounds.diagonal()) * 0.5f;
        float distance = radius / sinf(dm::radians(m_CameraVerticalFov * 0.5f));
        m_ThirdPersonCamera.SetDistance(distance);
        m_ThirdPersonCamera.Animate(0.f);
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

        m_ThirdPersonCamera.SetView(*planarView);

        return topologyChanged;
    }

    void CreateRenderPasses()
    {
        ForwardShadingPass::CreateParameters ForwardParams;
        ForwardParams.trackLiveness = false;
        m_ForwardPass = std::make_shared<PbrForwardShadingPass>(GetDevice(), m_CommonPasses);
        m_ForwardPass->Init(*m_ShaderFactory, ForwardParams);
        
        GBufferFillPass::CreateParameters GBufferParams;
        GBufferParams.enableMotionVectors = false;
        m_GBufferPass = std::make_unique<PbrGBufferFillPass>(GetDevice(), m_CommonPasses);
        m_GBufferPass->Init(*m_ShaderFactory, GBufferParams);

        GBufferParams.enableMotionVectors = false;
        m_MaterialIDPass = std::make_unique<MaterialIDPass>(GetDevice(), m_CommonPasses);
        m_MaterialIDPass->Init(*m_ShaderFactory, GBufferParams);

        m_PixelReadbackPass = std::make_unique<PixelReadbackPass>(GetDevice(), m_ShaderFactory, m_RenderTargets->MaterialIDs, nvrhi::Format::RGBA32_UINT);

        m_DeferredLightingPass = std::make_unique<PbrDeferredLightingPass>(GetDevice(), m_CommonPasses);
        m_DeferredLightingPass->Init(m_ShaderFactory);

        m_SkyPass = std::make_unique<SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ForwardFramebuffer, *m_View);
        
        if (m_RenderTargets->GetSampleCount() == 1)
        {
            m_SsaoPass = std::make_unique<SsaoPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->Depth, m_RenderTargets->GBufferNormals, m_RenderTargets->AmbientOcclusion);
        }

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

            bool needNewPasses = false;

            if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(uint2(width, height), sampleCount))
            {
                m_RenderTargets = nullptr;
                m_BindingCache.Clear();
                m_RenderTargets = std::make_unique<RenderTargets>();
                m_RenderTargets->Init(GetDevice(), uint2(width, height), sampleCount, false, true);
                
                needNewPasses = true;
            }

            if (SetupView())
            {
                needNewPasses = true;
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
            m_AmbientTop = float3(topLuma);
            m_AmbientBottom = float3(bottomLuma);
        }

        m_RenderTargets->Clear(m_CommandList);

        ForwardShadingPass::Context forwardContext;

        if (!m_ui.UseDeferredShading)
            m_ForwardPass->PrepareLights(forwardContext, m_CommandList, m_Scene->GetSceneGraph()->GetLights(), m_AmbientTop, m_AmbientBottom, {});

        if (m_ui.UseDeferredShading)
        {
            GBufferFillPass::Context gbufferContext;

            RenderCompositeView(m_CommandList,
                m_View.get(), m_View.get(),
                *m_RenderTargets->GBufferFramebuffer, 
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_GBufferPass,
                gbufferContext,
                "GBufferFill",
                false);

            if (m_ui.EnableSsao && m_SsaoPass)
                m_SsaoPass->Render(m_CommandList, m_ui.SsaoParams, *m_View);

            DeferredLightingPass::Inputs deferredInputs;
            deferredInputs.SetGBuffer(*m_RenderTargets);
            deferredInputs.ambientOcclusion = m_ui.EnableSsao ? m_RenderTargets->AmbientOcclusion : nullptr;
            // Slot 14 is unused by UVSR's current renderer and carries the
            // separate authored material ambient-occlusion attachment.
            deferredInputs.indirectDiffuse = m_RenderTargets->MaterialAmbientOcclusion;
            deferredInputs.ambientColorTop = m_AmbientTop;
            deferredInputs.ambientColorBottom = m_AmbientBottom;
            deferredInputs.lights = &m_Scene->GetSceneGraph()->GetLights();
            deferredInputs.output = m_RenderTargets->HdrColor;

            // The base pass already exposes a two-component temporal offset.
            // UVSR does not animate it, so Y carries the deferred debug mode
            // without extending Donut's constant-buffer ABI.
            m_DeferredLightingPass->Render(
                m_CommandList,
                *m_View,
                deferredInputs,
                float2(0.f, float(m_ui.PbrDebug)));
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
            m_CommandList->clearTextureUInt(m_RenderTargets->MaterialIDs, nvrhi::AllSubresources, 0xffff);

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

        m_AgxToneMappingPass->Render(
            m_CommandList, m_ui.AgxToneMappingParams, *m_View, m_RenderTargets->HdrColor);
        
        m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->LdrColor, &m_BindingCache);

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

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

};

class UIRenderer : public ImGui_Renderer
{
private:
    std::shared_ptr<UvsrSceneViewer> m_app;

	std::shared_ptr<app::RegisteredFont> m_FontOpenSans;
    std::shared_ptr<engine::Light> m_SelectedLight;

	UIData& m_ui;

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
        ImGui::Text("Renderer: %s", GetDeviceManager()->GetRendererString());
        double frameTime = GetDeviceManager()->GetAverageFrameTimeSeconds();
        if (frameTime > 0.0)
            ImGui::Text("%.3f ms/frame (%.1f FPS)  |  %d x %d", frameTime * 1e3, 1.0 / frameTime, width, height);

        const ImGuiStyle& style = ImGui::GetStyle();
        const float settingsControlWidth =
            ImGui::CalcTextSize("Reload Shaders").x + style.FramePadding.x * 2.f +
            style.ItemSpacing.x +
            ImGui::CalcTextSize("Restart Renderer").x + style.FramePadding.x * 2.f;

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
        if (ImGui::BeginCombo("##Scene", getRelativePath(currentScene)))
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

        if (ImGui::Button("Reload Shaders"))
            m_ui.ShaderReloadRequested = true;

        ImGui::SameLine();
        if (ImGui::Button("Restart Renderer"))
        {
            g_RestartRequested = true;
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
        }

        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##Camera", m_ui.UseThirdPersonCamera ? "Third-Person" : "First-Person"))
        {
            if (ImGui::Selectable("First-Person", !m_ui.UseThirdPersonCamera))
            {
                if (m_ui.UseThirdPersonCamera)
                {
                    m_app->CopyThirdPersonToFirstPerson();
                    m_ui.UseThirdPersonCamera = false;
                }
            }
            if (ImGui::Selectable("Third-Person", m_ui.UseThirdPersonCamera))
            {
                if (!m_ui.UseThirdPersonCamera)
                {
                    m_app->CopyCurrentViewToThirdPerson();
                    m_ui.UseThirdPersonCamera = true;
                }
            }
            ImGui::EndCombo();
        }
        static const char* whiteWorldLabels[] = {
            "White World Off",
            "White World On",
            "White World Preserve Detail"
        };
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##WhiteWorld", whiteWorldLabels[int(m_ui.WhiteWorld)]))
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

        ImGui::Checkbox("Deferred Shading", &m_ui.UseDeferredShading);
        ImGui::Checkbox("Enable SSAO", &m_ui.EnableSsao);

        if (ImGui::CollapsingHeader("Tonemapper"))
        {
            static const char* presetLabels[] = {
                "Base",
                "Punchy",
                "Golden",
                "Mix",
                "Custom"
            };

            ImGui::SetNextItemWidth(settingsControlWidth);
            if (ImGui::BeginCombo(
                "Preset", presetLabels[int(m_ui.AgxToneMappingPreset)]))
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
            if (ImGui::BeginCombo("Lut", luts[selectedLut].Name.c_str()))
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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Film LUT (.cube)");

            AgxToneMappingParameters& params = m_ui.AgxToneMappingParams;
            bool gradeChanged = false;
            gradeChanged |= ImGui::SliderFloat("Exposure", &params.Exposure, -10.f, 10.f, "%.2f EV");
            gradeChanged |= ImGui::SliderFloat("Contrast", &params.Contrast, 0.5f, 2.f, "%.3f");
            gradeChanged |= ImGui::SliderFloat("Saturation", &params.Saturation, 0.f, 2.f, "%.3f");
            gradeChanged |= ImGui::SliderFloat("Warmth", &params.Warmth, -1.f, 1.f, "%.3f");
            gradeChanged |= ImGui::SliderFloat("Tint", &params.Tint, -1.f, 1.f, "%.3f");
            gradeChanged |= ImGui::InputFloat("Slope", &params.Slope, 0.f, 0.f, "%.4f");
            gradeChanged |= ImGui::InputFloat("Power", &params.Power, 0.f, 0.f, "%.4f");

            if (gradeChanged)
            {
                params.Exposure = std::clamp(params.Exposure, -10.f, 10.f);
                params.Contrast = std::clamp(params.Contrast, 0.5f, 2.f);
                params.Saturation = std::clamp(params.Saturation, 0.f, 2.f);
                params.Warmth = std::clamp(params.Warmth, -1.f, 1.f);
                params.Tint = std::clamp(params.Tint, -1.f, 1.f);
                params.Slope = std::max(params.Slope, 0.f);
                params.Power = std::max(params.Power, 0.01f);
                m_ui.AgxToneMappingPreset = AgxPreset::Custom;
            }

            ImGui::TextDisabled("Ctrl+click a slider to type an exact value.");
        }

        if (ImGui::CollapsingHeader("Sky"))
        {
            ImGui::Checkbox("Enable Procedural Sky", &m_ui.EnableProceduralSky);
            if (m_ui.EnableProceduralSky)
            {
                ImGui::SliderFloat("Brightness", &m_ui.SkyParams.brightness, 0.f, 1.f);
                ImGui::SliderFloat("Glow Size", &m_ui.SkyParams.glowSize, 0.f, 90.f);
                ImGui::SliderFloat("Glow Sharpness", &m_ui.SkyParams.glowSharpness, 1.f, 10.f);
                ImGui::SliderFloat("Glow Intensity", &m_ui.SkyParams.glowIntensity, 0.f, 1.f);
                ImGui::SliderFloat("Horizon Size", &m_ui.SkyParams.horizonSize, 0.f, 90.f);
            }
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

        if (!lights.empty() && ImGui::CollapsingHeader("Lights"))
        {
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (ImGui::BeginCombo("Select Light", m_SelectedLight ? m_SelectedLight->GetName().c_str() : "(None)"))
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

        if (ImGui::Button("Screenshot"))
            m_ui.CopyScreenshotToClipboard = true;

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

        if (!m_ui.UseDeferredShading)
            m_ui.EnableSsao = false;

        ImGui::PopFont();
    }
};

void ProcessCommandLine(int argc, const char* const* argv, DeviceCreationParameters& deviceParams, std::string& sceneName)
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
        else if (argv[i][0] != '-')
        {
            sceneName = argv[i];
        }
    }
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
#else //  _WIN32
int main(int __argc, const char* const* __argv)
{
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
    ProcessCommandLine(__argc, __argv, deviceParams, sceneName);

    // UVSR intentionally runs uncapped; the renderer no longer exposes or
    // maintains a runtime VSync mode.
    deviceParams.vsyncEnabled = false;
    
    DeviceManager* deviceManager = DeviceManager::Create(api);
    const char* apiString = nvrhi::utils::GraphicsAPIToString(deviceManager->GetGraphicsAPI());

    std::string windowTitle = "UVSR NVIDIA Bistro (" + std::string(apiString) + ")";

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, windowTitle.c_str()))
	{
        log::error("Cannot initialize a %s graphics device with the requested parameters", apiString);
		return 1;
	}

    {
        UIData uiData;

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

