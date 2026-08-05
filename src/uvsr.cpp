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
#include <optional>
#include <chrono>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <limits>
#include <string_view>
#include <system_error>
#include <thread>
#include <iterator>
#include <utility>
#include <Windows.h>
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/core/string_utils.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/Scene.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/ThreadPool.h>
#include <donut/engine/View.h>
#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DepthPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/GBuffer.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/GeometryPasses.h>
#include <donut/render/PixelReadbackPass.h>
#include <donut/render/PlanarShadowMap.h>
#include <donut/app/ApplicationBase.h>
#include <donut/app/UserInterfaceUtils.h>
#include <donut/app/Camera.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/imgui_renderer.h>
#include <imgui_internal.h>
#include <nvrhi/utils.h>
#include <nvrhi/common/misc.h>
#include <directx/d3d12.h>

#include "pbr_material.h"
#include "pbr_deferred_lighting_pass.h"
#include "msaa_visibility_resolve.h"
#include "image_based_lighting_background_pass.h"
#include "image_based_lighting_environment.h"
#include "image_based_lighting_shared.h"
#include "screen_space_directional_shadows.h"
#include "gpu_performance_monitor.h"
#include "camera_collision.h"
#include "camera_controllers.h"
#include "cmaa2.h"
#include "command_line_options.h"
#include "fast_approximate_aa.h"
#include "flashlight.h"
#include "heitz_ratio_estimator_shadows.h"
#include "pixel_zoom.h"
#include "renderer_statistics.h"
#include "scene_catalog.h"
#include "scene_loading.h"
#include "scene_light_names.h"
#include "screen_space_visibility.h"
#include "sponza_camera_preset.h"
#include "temporal_aa.h"
#include "ui_animation.h"
#include "ui_command_layout.h"
#include "ui_commands.h"
#include "ui_settings_command_catalog.h"
#include "ui_skin.h"
#include "visibility_blue_noise.h"
#include "world_space_representation.h"

using namespace donut;
using namespace donut::math;
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

#include <donut/shaders/light_cb.h>

class FlashlightSpotLight final : public SpotLight
{
public:
    float beamRoundness =
        DefaultFlashlightSettings.beamRoundness;
    float3 beamRight = float3(1.f, 0.f, 0.f);

    void FillLightConstants(
        LightConstants& lightConstants) const override
    {
        SpotLight::FillLightConstants(lightConstants);

        lightConstants.radius =
            EncodeFlashlightBeamShapeRadius(beamRoundness);
        lightConstants.shadowChannel[1] =
            EncodeFlashlightBeamAxisComponent(beamRight.x);
        lightConstants.shadowChannel[2] =
            EncodeFlashlightBeamAxisComponent(beamRight.y);
        lightConstants.shadowChannel[3] =
            EncodeFlashlightBeamAxisComponent(beamRight.z);
    }
};

static bool g_RestartRequested = false;
static int g_RestartAdapterIndex = -1;
static void ApplyProcessPriority()
{
    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
    {
        log::warning(
            "UVSR could not request High process priority (Win32 error %lu)",
            GetLastError());
    }
}

struct GpuAdapterChoice
{
    int adapterIndex = -1;
    std::string name;
    uint64_t dedicatedVideoMemory = 0;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
};

constexpr float UiBackgroundBlurPixels = 4.f;
constexpr float UiPanelShadowBlurPixels = 10.f;
constexpr float UiPanelShadowOpacity = 0.34f;
constexpr float UiPanelShadowOffsetYPixels = 3.f;
constexpr size_t UiBackdropRectCount = 5u;
constexpr size_t UiMaterialTitleBackdropIndex = 2u;
constexpr size_t UiMaterialBodyBackdropIndex = 3u;
constexpr size_t UiCommandBackdropIndex = 4u;

struct UiBackdropRect
{
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    float rounding = 0.f;
    float opacity = 1.f;
    float shadowBlur = 0.f;
    float shadowOpacity = 0.f;
    float shadowOffsetY = 0.f;
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

class SubmittedTriangleCountingPass final : public IGeometryPass
{
public:
    explicit SubmittedTriangleCountingPass(IGeometryPass& pass)
        : m_Pass(pass)
    {
    }

    [[nodiscard]] ViewType::Enum GetSupportedViewTypes() const override
    {
        return m_Pass.GetSupportedViewTypes();
    }

    void SetupView(
        GeometryPassContext& context,
        nvrhi::ICommandList* commandList,
        const IView* view,
        const IView* viewPrev) override
    {
        m_Pass.SetupView(context, commandList, view, viewPrev);
    }

    bool SetupMaterial(
        GeometryPassContext& context,
        const Material* material,
        nvrhi::RasterCullMode cullMode,
        nvrhi::GraphicsState& state) override
    {
        return m_Pass.SetupMaterial(
            context,
            material,
            cullMode,
            state);
    }

    void SetupInputBuffers(
        GeometryPassContext& context,
        const BufferGroup* buffers,
        nvrhi::GraphicsState& state) override
    {
        m_Pass.SetupInputBuffers(context, buffers, state);
    }

    void SetPushConstants(
        GeometryPassContext& context,
        nvrhi::ICommandList* commandList,
        nvrhi::GraphicsState& state,
        nvrhi::DrawArguments& arguments) override
    {
        m_Pass.SetPushConstants(
            context,
            commandList,
            state,
            arguments);
        m_SubmittedTriangles +=
            CountSubmittedTriangleListPrimitives(
                arguments.vertexCount,
                arguments.instanceCount);
    }

    [[nodiscard]] uint64_t GetSubmittedTriangles() const
    {
        return m_SubmittedTriangles;
    }

private:
    IGeometryPass& m_Pass;
    uint64_t m_SubmittedTriangles = 0u;
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

static uint32_t ResolveSupportedMsaaSampleCount(
    nvrhi::IDevice* device,
    uint32_t requestedSampleCount)
{
    if (!device || requestedSampleCount <= 1u)
        return 1u;
    if (device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D12)
        return 1u;

    ID3D12Device* nativeDevice =
        device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
    if (!nativeDevice)
        return 1u;

    struct MsaaSampleCountCache
    {
        ID3D12Device* device = nullptr;
        uint32_t requestedSampleCount = 0u;
        uint32_t resolvedSampleCount = 1u;
    };
    static MsaaSampleCountCache cache;
    if (cache.device == nativeDevice &&
        cache.requestedSampleCount == requestedSampleCount)
    {
        return cache.resolvedSampleCount;
    }
    const auto cacheResolution =
        [&](uint32_t resolvedSampleCount)
    {
        cache.device = nativeDevice;
        cache.requestedSampleCount = requestedSampleCount;
        cache.resolvedSampleCount = resolvedSampleCount;
        return resolvedSampleCount;
    };

    const auto supportsFormat = [nativeDevice](
        DXGI_FORMAT format,
        uint32_t sampleCount)
    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS query{};
        query.Format = format;
        query.SampleCount = sampleCount;
        query.Flags =
            D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        return SUCCEEDED(nativeDevice->CheckFeatureSupport(
                   D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                   &query,
                   sizeof(query))) &&
            query.NumQualityLevels > 0u;
    };

    // Match GBufferRenderTargets::Init exactly: it selects the first depth
    // format with the required general features before applying sampleCount to
    // the texture descriptor. Accepting a later format merely because that
    // format supports MSAA would still leave Donut allocating the earlier,
    // unsupported selection.
    constexpr nvrhi::Format depthFormats[] = {
        nvrhi::Format::D24S8,
        nvrhi::Format::D32S8,
        nvrhi::Format::D32,
        nvrhi::Format::D16
    };
    const nvrhi::FormatSupport depthFeatures =
        nvrhi::FormatSupport::Texture |
        nvrhi::FormatSupport::DepthStencil |
        nvrhi::FormatSupport::ShaderLoad;
    const nvrhi::Format selectedDepthFormat = nvrhi::utils::ChooseFormat(
        device,
        depthFeatures,
        depthFormats,
        std::size(depthFormats));
    DXGI_FORMAT selectedDepthDxgiFormat = DXGI_FORMAT_UNKNOWN;
    switch (selectedDepthFormat)
    {
    case nvrhi::Format::D24S8:
        selectedDepthDxgiFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        break;
    case nvrhi::Format::D32S8:
        selectedDepthDxgiFormat = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        break;
    case nvrhi::Format::D32:
        selectedDepthDxgiFormat = DXGI_FORMAT_D32_FLOAT;
        break;
    case nvrhi::Format::D16:
        selectedDepthDxgiFormat = DXGI_FORMAT_D16_UNORM;
        break;
    default:
        log::warning(
            "Hardware MSAA is unavailable because Donut found no compatible "
            "G-buffer depth format; presenting one sample");
        return cacheResolution(1u);
    }

    const auto supportsAllocatedFormats =
        [&](uint32_t sampleCount)
    {
        // Check every G-buffer format that receives the selected sample count,
        // not only the HDR attachment.
        constexpr DXGI_FORMAT alwaysAllocatedColorFormats[] = {
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R16G16B16A16_SNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16_FLOAT
        };
        for (DXGI_FORMAT format : alwaysAllocatedColorFormats)
        {
            if (!supportsFormat(format, sampleCount))
                return false;
        }

        constexpr DXGI_FORMAT pbrColorFormats[] = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8_UNORM
        };
        for (DXGI_FORMAT format : pbrColorFormats)
        {
            if (!supportsFormat(format, sampleCount))
                return false;
        }

        return supportsFormat(selectedDepthDxgiFormat, sampleCount);
    };

    for (uint32_t candidate : { 16u, 8u, 4u, 2u })
    {
        if (candidate <= requestedSampleCount &&
            supportsAllocatedFormats(candidate))
        {
            if (candidate != requestedSampleCount)
            {
                log::warning(
                    "Requested %ux MSAA is unsupported by all UVSR "
                    "render-target formats; using %ux",
                    requestedSampleCount,
                    candidate);
            }
            return cacheResolution(candidate);
        }
    }

    log::warning(
        "Hardware MSAA is unsupported by the active adapter for UVSR's "
        "render-target formats; presenting one sample");
    return cacheResolution(1u);
}

class RenderTargets : public GBufferRenderTargets
{
public:
    nvrhi::TextureHandle HdrColor;
    nvrhi::TextureHandle ResolvedHdrColor;
    nvrhi::TextureHandle DeferredMsaaColor;
    nvrhi::TextureHandle BaseLighting;
    nvrhi::TextureHandle DirectDiffuseRadiance;
    nvrhi::TextureHandle VisibilityComposite;
    nvrhi::TextureHandle VisibilityDepth;
    nvrhi::TextureHandle VisibilityGBufferDiffuse;
    nvrhi::TextureHandle VisibilityGBufferMaterial;
    nvrhi::TextureHandle VisibilityGBufferNormals;
    nvrhi::TextureHandle VisibilityGBufferEmissive;
    nvrhi::TextureHandle VisibilityMaterialAmbientOcclusion;
    nvrhi::TextureHandle VisibilityMotionVectors;
    nvrhi::TextureHandle LdrColor;
    nvrhi::TextureHandle MaterialIDs;
    nvrhi::TextureHandle MaterialIDDepth;
    nvrhi::TextureHandle MaterialAmbientOcclusion;
    bool VisibilityResourcesEnabled = false;
    bool VisibilitySourceRadianceEnabled = false;
    bool MotionVectorsEnabled = false;

    nvrhi::HeapHandle Heap;

    std::shared_ptr<FramebufferFactory> HdrFramebuffer;
    std::shared_ptr<FramebufferFactory> LdrFramebuffer;
    std::shared_ptr<FramebufferFactory> MaterialIDFramebuffer;
    
    void Init(
        nvrhi::IDevice* device,
        dm::uint2 size,
        dm::uint sampleCount,
        bool enableMotionVectors,
        bool useReverseProjection,
        bool enableVisibilityResources,
        bool enableVisibilitySourceRadiance)
    {
        GBufferRenderTargets::Init(device, size, sampleCount, enableMotionVectors, useReverseProjection);
        VisibilityResourcesEnabled = enableVisibilityResources;
        VisibilitySourceRadianceEnabled =
            enableVisibilityResources && enableVisibilitySourceRadiance;
        MotionVectorsEnabled = enableMotionVectors;

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

        if (sampleCount > 1u)
        {
            nvrhi::TextureDesc resolvedDesc = desc;
            resolvedDesc.sampleCount = 1u;
            resolvedDesc.dimension =
                nvrhi::TextureDimension::Texture2D;
            resolvedDesc.isRenderTarget = false;
            // This surface is only a ResolveDest followed by an SRV. D3D12
            // rejects the optimized clear value inherited from HdrColor when
            // the placed resource has no RT/DS flag. It also needs no UAV
            // capability, so strip both inherited creation requirements.
            resolvedDesc.useClearValue = false;
            resolvedDesc.isUAV = false;
            resolvedDesc.initialState =
                nvrhi::ResourceStates::ShaderResource;
            resolvedDesc.debugName = "ResolvedHdrColor";
            ResolvedHdrColor =
                device->createTexture(resolvedDesc);

            nvrhi::TextureDesc deferredMsaaDesc =
                resolvedDesc;
            deferredMsaaDesc.isUAV = true;
            deferredMsaaDesc.initialState =
                nvrhi::ResourceStates::UnorderedAccess;
            deferredMsaaDesc.debugName =
                "DeferredMsaaColor";
            DeferredMsaaColor =
                device->createTexture(deferredMsaaDesc);
        }

        if (enableVisibilityResources)
        {
            nvrhi::TextureDesc visibilityDesc = desc;
            visibilityDesc.sampleCount = 1u;
            visibilityDesc.dimension =
                nvrhi::TextureDimension::Texture2D;
            visibilityDesc.isRenderTarget = false;
            visibilityDesc.isUAV = true;
            visibilityDesc.useClearValue = false;
            visibilityDesc.initialState =
                nvrhi::ResourceStates::UnorderedAccess;
            visibilityDesc.format =
                nvrhi::Format::RGBA16_FLOAT;
            visibilityDesc.debugName =
                "ScreenSpaceVisibility/BaseLighting";
            BaseLighting =
                device->createTexture(visibilityDesc);

            if (VisibilitySourceRadianceEnabled)
            {
                visibilityDesc.debugName =
                    "ScreenSpaceVisibility/DirectDiffuseRadiance";
                DirectDiffuseRadiance =
                    device->createTexture(visibilityDesc);
            }

            if (sampleCount > 1u)
            {
                visibilityDesc.debugName =
                    "ScreenSpaceVisibility/MsaaComposite";
                VisibilityComposite =
                    device->createTexture(visibilityDesc);

                visibilityDesc.format =
                    nvrhi::Format::R32_FLOAT;
                visibilityDesc.debugName =
                    "ScreenSpaceVisibility/ResolvedDepth";
                VisibilityDepth =
                    device->createTexture(visibilityDesc);

                visibilityDesc.format =
                    nvrhi::Format::RGBA16_FLOAT;
                visibilityDesc.debugName =
                    "ScreenSpaceVisibility/ResolvedDiffuse";
                VisibilityGBufferDiffuse =
                    device->createTexture(visibilityDesc);
                visibilityDesc.debugName =
                    "ScreenSpaceVisibility/ResolvedMaterial";
                VisibilityGBufferMaterial =
                    device->createTexture(visibilityDesc);
                visibilityDesc.debugName =
                    "ScreenSpaceVisibility/ResolvedNormals";
                VisibilityGBufferNormals =
                    device->createTexture(visibilityDesc);
                visibilityDesc.debugName =
                    "ScreenSpaceVisibility/ResolvedEmissive";
                VisibilityGBufferEmissive =
                    device->createTexture(visibilityDesc);

                visibilityDesc.format =
                    nvrhi::Format::R16_FLOAT;
                visibilityDesc.debugName =
                    "ScreenSpaceVisibility/ResolvedMaterialAO";
                VisibilityMaterialAmbientOcclusion =
                    device->createTexture(visibilityDesc);

                visibilityDesc.format =
                    nvrhi::Format::RGBA16_FLOAT;
                visibilityDesc.debugName =
                    "ScreenSpaceVisibility/ResolvedMotion";
                VisibilityMotionVectors =
                    device->createTexture(visibilityDesc);
            }
        }

        // Picking is deliberately kept out of the every-frame G-buffer. The
        // failed NRA-RTAA v1 needed stable surface IDs every frame; now a
        // compact target plus the existing on-demand material-ID pass avoids
        // an otherwise permanent MRT write and restores the original cost.
        // Keep picking single-sample. Integer material IDs have no normal
        // color resolve, and PixelReadbackPass consumes Texture2D.
        desc.sampleCount = 1u;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.format = nvrhi::Format::RG16_UINT;
        desc.isUAV = false;
        desc.debugName = "MaterialIDs";
        MaterialIDs = device->createTexture(desc);

        // Keep the display-referred image linear and undithered until the
        // final presentation pass. CMAA2 can then detect and blend the same
        // post-AgX edges the user sees without an illegal sRGB UAV alias or
        // classifying quantization noise.
        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.isUAV = false;
        desc.debugName = "LdrColor";
        LdrColor = device->createTexture(desc);

        if (desc.isVirtual)
        {
            uint64_t heapSize = 0;
            std::vector<nvrhi::ITexture*> textures = {
                HdrColor, MaterialIDs, LdrColor };
            if (ResolvedHdrColor)
                textures.push_back(ResolvedHdrColor);
            if (DeferredMsaaColor)
                textures.push_back(DeferredMsaaColor);
            if (BaseLighting)
                textures.push_back(BaseLighting);
            if (DirectDiffuseRadiance)
                textures.push_back(DirectDiffuseRadiance);
            if (VisibilityComposite)
                textures.push_back(VisibilityComposite);
            if (VisibilityDepth)
                textures.push_back(VisibilityDepth);
            if (VisibilityGBufferDiffuse)
                textures.push_back(VisibilityGBufferDiffuse);
            if (VisibilityGBufferMaterial)
                textures.push_back(VisibilityGBufferMaterial);
            if (VisibilityGBufferNormals)
                textures.push_back(VisibilityGBufferNormals);
            if (VisibilityGBufferEmissive)
                textures.push_back(VisibilityGBufferEmissive);
            if (VisibilityMaterialAmbientOcclusion)
                textures.push_back(
                    VisibilityMaterialAmbientOcclusion);
            if (VisibilityMotionVectors)
                textures.push_back(VisibilityMotionVectors);

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
        
        HdrFramebuffer = std::make_shared<FramebufferFactory>(device);
        HdrFramebuffer->RenderTargets = { HdrColor };
        HdrFramebuffer->DepthTarget = Depth;

        LdrFramebuffer = std::make_shared<FramebufferFactory>(device);
        LdrFramebuffer->RenderTargets = { LdrColor };

        MaterialIDFramebuffer = std::make_shared<FramebufferFactory>(device);
        MaterialIDFramebuffer->RenderTargets = { MaterialIDs };
        if (sampleCount > 1u)
        {
            nvrhi::TextureDesc pickDepthDesc = Depth->getDesc();
            pickDepthDesc.sampleCount = 1u;
            pickDepthDesc.dimension =
                nvrhi::TextureDimension::Texture2D;
            pickDepthDesc.isVirtual = false;
            pickDepthDesc.debugName = "MaterialIDDepth";
            MaterialIDDepth =
                device->createTexture(pickDepthDesc);
        }
        else
        {
            MaterialIDDepth = Depth;
        }
        MaterialIDFramebuffer->DepthTarget = MaterialIDDepth;
    }

    [[nodiscard]] bool IsUpdateRequired(
        uint2 size,
        uint sampleCount,
        bool enableVisibilityResources,
        bool enableVisibilitySourceRadiance,
        bool enableMotionVectors) const
    {
        if (any(m_Size != size) || m_SampleCount != sampleCount ||
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
    EnvironmentDirection,
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

class AgxToneMappingPass
{
private:
    nvrhi::DeviceHandle m_Device;
    nvrhi::ShaderHandle m_PixelShader;
    nvrhi::ShaderHandle m_OutputPixelShader;
    nvrhi::BindingLayoutHandle m_BindingLayout;
    nvrhi::BindingSetHandle m_BindingSet;
    nvrhi::BindingSetHandle m_OutputBindingSet;
    nvrhi::GraphicsPipelineHandle m_Pipeline;
    nvrhi::GraphicsPipelineHandle m_OutputPipeline;
    nvrhi::ITexture* m_BoundSource = nullptr;
    nvrhi::ITexture* m_BoundOutputSource = nullptr;
    nvrhi::Format m_OutputFramebufferFormat =
        nvrhi::Format::UNKNOWN;
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
        m_OutputPixelShader = shaderFactory->CreateShader(
            "uvsr/display_output_ps.hlsl",
            "main",
            nullptr,
            nvrhi::ShaderType::Pixel);

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

    bool RenderOutput(
        nvrhi::ICommandList* commandList,
        const ICompositeView& compositeView,
        nvrhi::IFramebuffer* framebuffer,
        nvrhi::ITexture* sourceTexture)
    {
        if (!commandList || !framebuffer || !sourceTexture ||
            !m_OutputPixelShader || !m_BindingLayout)
        {
            return false;
        }

        const nvrhi::FramebufferInfoEx& framebufferInfo =
            framebuffer->getFramebufferInfo();
        if (framebufferInfo.colorFormats.empty())
            return false;
        const nvrhi::Format framebufferFormat =
            framebufferInfo.colorFormats[0];
        if (!m_OutputPipeline ||
            framebufferFormat != m_OutputFramebufferFormat)
        {
            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.primType =
                nvrhi::PrimitiveType::TriangleStrip;
            pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
            pipelineDesc.PS = m_OutputPixelShader;
            pipelineDesc.bindingLayouts = { m_BindingLayout };
            pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState.depthTestEnable =
                false;
            pipelineDesc.renderState.depthStencilState.stencilEnable =
                false;
            m_OutputPipeline = m_Device->createGraphicsPipeline(
                pipelineDesc,
                framebufferInfo);
            m_OutputFramebufferFormat = framebufferFormat;
        }
        if (!m_OutputPipeline)
            return false;

        if (!m_OutputBindingSet ||
            m_BoundOutputSource != sourceTexture)
        {
            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::Texture_SRV(
                    0,
                    sourceTexture)
            };
            m_OutputBindingSet = m_Device->createBindingSet(
                bindingSetDesc,
                m_BindingLayout);
            m_BoundOutputSource = sourceTexture;
        }
        if (!m_OutputBindingSet)
            return false;

        commandList->beginMarker("Display Transfer and Dither");
        for (uint32_t viewIndex = 0;
            viewIndex < compositeView.GetNumChildViews(ViewType::PLANAR);
            ++viewIndex)
        {
            const IView* view = compositeView.GetChildView(
                ViewType::PLANAR,
                viewIndex);
            nvrhi::GraphicsState state;
            state.pipeline = m_OutputPipeline;
            state.framebuffer = framebuffer;
            state.bindings = { m_OutputBindingSet };
            state.viewport = view->GetViewportState();
            commandList->setGraphicsState(state);

            nvrhi::DrawArguments arguments;
            arguments.instanceCount = 1;
            arguments.vertexCount = 4;
            commandList->draw(arguments);
        }
        commandList->endMarker();

        return true;
    }
};

struct UIData
{
    bool                                ShowUI = false;
    UiSkin                              Skin = DefaultUiSkin;
    std::array<UiBackdropRect, UiBackdropRectCount>
                                        BackdropRects;
    PixelZoomMode                       PixelZoom =
        PixelZoomMode::Off;
    std::vector<GpuAdapterChoice>       GpuAdapterChoices;
    int                                 ActiveGpuAdapterIndex = -1;
    AntiAliasingSettings                AntiAliasing;
    bool                                TemporalAaSharpenEnabled = false;
    float                               TemporalAaSharpness =
        TemporalAaDefaultSharpness;
    DirectionalShadowSettings           DirectionalShadows;
    ScreenSpaceDirectionalShadowSettings       ScreenSpaceDirectionalShadows;
    WorldSpaceRepresentationSettings    Representation;
    ScreenSpaceVisibilitySettings       ScreenSpaceVisibility;
    bool                                ShaderReloadRequested = false;
    bool                                FlashlightEnabled =
        DefaultFlashlightEnabled;
    FlashlightSettings                  Flashlight =
        DefaultFlashlightSettings;
    bool                                ShowEnvironmentBackground = true;
    bool                                EnableAmbientFill = true;
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

    [[nodiscard]] bool HasActiveScreenSpaceVisibilityConsumer() const
    {
        const bool lightingConsumer = HasActiveScreenSpaceLightingConsumer(
            ScreenSpaceVisibility.enabled,
            ScreenSpaceVisibility.HasActiveAmbientOcclusion(),
            ScreenSpaceVisibility.HasActiveIndirectDiffuse(),
            IsAmbientFillLobeActive(
                EnableAmbientFill,
                EnableDiffuseIbl,
                DiffuseIblStrength),
            IsAmbientFillLobeActive(
                EnableAmbientFill,
                EnableSpecularIbl,
                SpecularIblStrength));
        return lightingConsumer ||
            HasActiveScreenSpaceVisibilityDebugConsumer();
    }

    [[nodiscard]] bool
        HasActiveScreenSpaceVisibilityDebugConsumer() const
    {
        return ScreenSpaceVisibility.HasActiveConsumer() &&
            ScreenSpaceVisibility.debugView !=
                VisibilityDebugView::FinalImage;
    }

    [[nodiscard]] ResolvedAntiAliasingSettings
        GetResolvedAntiAliasingSettings(
            const AntiAliasingSettings& settings) const
    {
        return ResolveAntiAliasingSettings(settings);
    }

    [[nodiscard]] ResolvedAntiAliasingSettings
        GetResolvedAntiAliasingSettings() const
    {
        return GetResolvedAntiAliasingSettings(AntiAliasing);
    }

    [[nodiscard]] bool UsesLongTermTemporalAA() const
    {
        return GetResolvedAntiAliasingSettings().temporalEnabled;
    }

    [[nodiscard]] bool UsesCmaa2() const
    {
        return GetResolvedAntiAliasingSettings().cmaa2Enabled;
    }

    [[nodiscard]] bool UsesFastApproximateAA() const
    {
        return GetResolvedAntiAliasingSettings().fastApproximateEnabled;
    }

};

enum class RendererTimingStage : uint32_t
{
    CompleteFrame,
    SceneSetup,
    Geometry,
    MultisampleResolve,
    RatioEstimatorShadows,
    DirectLighting,
    ScreenSpaceVisibility,
    MaterialPicking,
    EnvironmentBackground,
    ToneMapping,
    FastApproximate,
    OutputBlit,
    Count
};

struct RendererTimings
{
    std::array<float, static_cast<size_t>(RendererTimingStage::Count)>
        milliseconds{};
    std::array<bool, static_cast<size_t>(RendererTimingStage::Count)>
        available{};

    [[nodiscard]] float Get(RendererTimingStage stage) const
    {
        return milliseconds[static_cast<size_t>(stage)];
    }

    [[nodiscard]] bool IsAvailable(RendererTimingStage stage) const
    {
        return available[static_cast<size_t>(stage)];
    }
};

class UvsrSceneViewer : public ApplicationBase
{
private:
    typedef ApplicationBase Super;

    enum class MaterialPickPurpose
    {
        None,
        FocusCameraAtCursor,
        OpenCenterMaterialInspector
    };

    enum class ScenePreparationStage
    {
        MeshUpload,
        SceneActivation,
        MaterialBuffers,
        WorldRepresentation,
        RenderTargets,
        RenderPasses,
        Complete
    };

    enum class RenderPassPreparationStage
    {
        Idle,
        GBuffer,
        MaterialId,
        ReadbackAndFlashlight,
        DeferredLighting,
        DeferredLightingPipelines,
        MsaaVisibilityResolvePipelines,
        Visibility,
        VisibilityPipelines,
        TemporalAA,
        TemporalAAPipelines,
        FastApproximateAA,
        Cmaa2,
        EnvironmentBackground,
        ToneMapping,
        Complete
    };

    struct PreparedSceneCpuState
    {
        CameraCollisionWorld collisionWorld;
        float sceneDiagonal = 100.f;
        float collisionRadius = 0.1f;
        std::optional<ImageBasedLightingEnvironment::PreparedRadiance>
            environmentRadiance;
    };

    std::shared_ptr<RootFileSystem>     m_RootFs;
    std::shared_ptr<NativeFileSystem>   m_NativeFs;
    std::vector<SceneCatalogEntry>      m_SceneCatalog;
    std::string                         m_CurrentSceneName;
    std::filesystem::path               m_SceneDir;
    std::shared_ptr<Scene>				m_Scene;
	std::vector<std::pair<std::shared_ptr<Material>, Material>> m_OriginalMaterials;
    std::shared_ptr<ShaderFactory>      m_ShaderFactory;
    std::shared_ptr<DirectionalLight>   m_SunLight;
    std::shared_ptr<FlashlightSpotLight> m_Flashlight;
    std::shared_ptr<SceneGraphNode>     m_FlashlightNode;
    std::shared_ptr<FlashlightSpotLight> m_FlashlightHotspot;
    std::shared_ptr<SceneGraphNode>     m_FlashlightHotspotNode;
    std::shared_ptr<PlanarShadowMap>    m_FlashlightShadowMap;
    std::shared_ptr<FramebufferFactory> m_FlashlightShadowFramebuffer;
    std::unique_ptr<DepthPass>          m_FlashlightDepthPass;
    float                               m_FlashlightTransition = 0.f;
    float                               m_FlashlightSwayTime = 0.f;
    float3                              m_FlashlightAimDirection =
                                            float3(0.f, 0.f, -1.f);
    float3                              m_FlashlightResolvedPosition = 0.f;
    float3                              m_FlashlightResolvedDirection =
                                            float3(0.f, 0.f, -1.f);
    float3                              m_FlashlightResolvedRight =
                                            float3(1.f, 0.f, 0.f);
    bool                                m_FlashlightAimInitialized = false;
    bool                                m_FlashlightPoseValid = false;
    std::vector<std::shared_ptr<Light>> m_SceneLightsWithoutFlashlight;
    std::vector<std::shared_ptr<Light>> m_EditableLights;
    std::shared_ptr<InstancedOpaqueDrawStrategy> m_OpaqueDrawStrategy;
    std::unique_ptr<RenderTargets>      m_RenderTargets;
    std::shared_ptr<GBufferFillPass>     m_GBufferPass;
    std::unique_ptr<PbrDeferredLightingPass> m_PbrDeferredLightingPass;
    std::unique_ptr<MsaaVisibilityResolvePass>
        m_MsaaVisibilityResolvePass;
    std::unique_ptr<ImageBasedLightingEnvironment>
                                        m_ImageBasedLightingEnvironment;
    std::unique_ptr<ImageBasedLightingBackgroundPass>
                                        m_ImageBasedLightingBackgroundPass;
    std::unique_ptr<AgxToneMappingPass> m_AgxToneMappingPass;
    std::unique_ptr<ScreenSpaceDirectionalShadowPass>
                                        m_ScreenSpaceDirectionalShadowPass;
    std::unique_ptr<HeitzRatioEstimatorShadowPass>
                                        m_HeitzRatioEstimatorShadowPass;
    std::unique_ptr<WorldSpaceRepresentation>
                                        m_WorldSpaceRepresentation;
    std::unique_ptr<ScreenSpaceVisibilityPass> m_ScreenSpaceVisibilityPass;
    std::unique_ptr<TemporalAAPass> m_TemporalAAPass;
    std::unique_ptr<FastApproximateAAPass>
                                        m_FastApproximateAAPass;
    std::unique_ptr<Cmaa2Pass>          m_Cmaa2Pass;
    std::unique_ptr<MaterialIDPass>     m_MaterialIDPass;
    std::unique_ptr<PixelReadbackPass>  m_PixelReadbackPass;

    std::shared_ptr<IView>              m_View;
    std::shared_ptr<PlanarView>         m_PreviousView;

    nvrhi::CommandListHandle            m_CommandList;
    static constexpr uint32_t c_RendererTimerLatency = 4u;
    std::array<std::array<nvrhi::TimerQueryHandle,
        c_RendererTimerLatency>,
        static_cast<size_t>(RendererTimingStage::Count)>
                                        m_RendererTimerQueries;
    std::array<std::array<bool, c_RendererTimerLatency>,
        static_cast<size_t>(RendererTimingStage::Count)>
                                        m_RendererTimerPending{};
    std::array<std::array<uint64_t, c_RendererTimerLatency>,
        static_cast<size_t>(RendererTimingStage::Count)>
                                        m_RendererTimerPendingEpoch{};
    std::array<uint64_t,
        static_cast<size_t>(RendererTimingStage::Count)>
                                        m_RendererTimerStageEpoch{};
    std::array<bool, static_cast<size_t>(RendererTimingStage::Count)>
                                        m_RendererTimerActive{};
    uint32_t                            m_RendererTimerFrame = 0u;
    bool                                m_RendererTimerFrameWritable = true;
    RendererTimings                     m_RendererTimings;
    UvsrFirstPersonCamera               m_FirstPersonCamera{ true };
    UvsrThirdPersonCamera               m_ThirdPersonCamera;
    UvsrFirstPersonCamera               m_PivotCamera{ false };
    StaticViewCamera                    m_StaticCamera;
    CameraCollisionWorld                m_CameraCollisionWorld;
    std::optional<PreparedSceneCpuState> m_PendingSceneCpuState;
    std::optional<CameraCollisionWorld> m_RetiredCameraCollisionWorld;
    std::vector<uint16_t>               m_PreparedVisibilityBlueNoise;
    BindingCache                        m_BindingCache;
    uint64_t                            m_SubmittedMainViewTriangles = 0u;

    float                               m_CameraVerticalFov = 60.f;
    float                               m_SceneDiagonal = 100.f;
    float                               m_CameraCollisionRadius = 0.1f;
    uint2                               m_PickPosition = 0u;
    MaterialPickPurpose                 m_MaterialPickPurpose =
        MaterialPickPurpose::None;
    const Scene*                        m_MaterialPickScene = nullptr;
    uint64_t                            m_AntiAliasingPhase = 0u;
    uint64_t                            m_HeitzRatioEstimatorPhase = 0u;
    bool                                m_HeitzRatioEstimatorContributedLastFrame =
                                            false;
    bool                                m_HeitzRatioEstimatorDispatchedThisFrame =
                                            false;
    bool                                m_HasAppliedAntiAliasingSettings =
        false;
    AntiAliasingSettings                m_AppliedAntiAliasingSettings;
    bool                                m_SceneFinishedLoading = false;
    bool                                m_SceneGpuUploadPending = false;
    ScenePreparationStage               m_ScenePreparationStage =
                                            ScenePreparationStage::Complete;
    RenderPassPreparationStage          m_RenderPassPreparationStage =
                                            RenderPassPreparationStage::Idle;
    bool                                m_RenderPassPreparationWaitForIbl =
                                            false;
    std::chrono::high_resolution_clock::time_point
                                        m_SceneGpuUploadStart;
    std::chrono::steady_clock::time_point
                                        m_LastLoadingPresentationFrame;
    double                              m_MaximumLoadingPresentationGapMs =
                                            0.0;
    uint64_t                            m_LoadingPresentationFrameCount = 0u;
    static constexpr uint64_t           c_SceneUploadBytesPerFrame =
                                            8ull * 1024ull * 1024ull;
    SponzaCameraLocation                m_SponzaCameraLocation =
        SponzaCameraLocation::SimplifiedApproximation;
    bool                                m_SponzaCameraLocationsAvailable =
                                            false;

    UIData&                             m_ui;

    void AdvanceRendererTimers();
    void BeginRendererStage(RendererTimingStage stage);
    void EndRendererStage(RendererTimingStage stage);
    void CompleteRendererTimerFrame();
    void InvalidateRendererStageTiming(RendererTimingStage stage);

public:

    bool ShouldAnimateUnfocused() override
    {
        return false;
    }

    bool ShouldRenderUnfocused() override
    {
        return false;
    }

    UvsrSceneViewer(
        DeviceManager* deviceManager,
        UIData& ui,
        const std::string& sceneName)
        : Super(deviceManager)
        , m_BindingCache(deviceManager->GetDevice())
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
        m_ImageBasedLightingEnvironment =
            std::make_unique<ImageBasedLightingEnvironment>(
                GetDevice(),
                m_ShaderFactory,
                m_CommonPasses,
                mediaDir / "environments");

        m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>();


        m_CommandList = GetDevice()->createCommandList();
        m_WorldSpaceRepresentation =
            std::make_unique<WorldSpaceRepresentation>(GetDevice());
        for (auto& stageQueries : m_RendererTimerQueries)
        {
            for (nvrhi::TimerQueryHandle& query : stageQueries)
                query = GetDevice()->createTimerQuery();
        }

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
                    "Default Sponza Decorated descriptor '%s' was not found; loading '%s' instead.",
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


    bool ToggleFlashlight()
    {
        m_ui.FlashlightEnabled = !m_ui.FlashlightEnabled;
        log::info(
            "Flashlight %s",
            m_ui.FlashlightEnabled ? "on" : "off");
        return true;
    }


    [[nodiscard]] bool HasSponzaCameraLocations() const
    {
        return m_SponzaCameraLocationsAvailable;
    }


    [[nodiscard]] SponzaCameraLocation GetSponzaCameraLocation() const
    {
        return m_SponzaCameraLocation;
    }

    void ResetAntiAliasingState()
    {
        if (m_TemporalAAPass)
            m_TemporalAAPass->ResetHistory();
        m_AntiAliasingPhase = 0u;
        m_HeitzRatioEstimatorPhase = 0u;
    }

    void ApplyCameraPose(
        float3 position,
        float3 direction,
        float3 up,
        float3 right,
        float verticalFovDegrees)
    {
        m_CameraVerticalFov = verticalFovDegrees;
        const float zoomReferenceDistance =
            m_ThirdPersonCamera.GetReferenceZoomDistance();
        m_ThirdPersonCamera.ResetZoomReferenceDistance(zoomReferenceDistance);
        m_ThirdPersonCamera.SetExactPose(
            position,
            direction,
            up,
            right);
        m_FirstPersonCamera.SetExactPose(
            position,
            direction,
            up,
            right);
        m_PivotCamera.SetExactPose(
            position,
            direction,
            up,
            right);
        m_StaticCamera.SetExactPose(
            position,
            direction,
            up,
            right);

        m_PreviousView.reset();
        ResetAntiAliasingState();
    }

    void ApplySponzaCameraPreset(const SponzaCameraPreset& preset)
    {
        ApplyCameraPose(
            preset.Position,
            preset.Direction,
            preset.Up,
            preset.Right,
            preset.VerticalFovDegrees);
    }

    void ApplySceneInitialCamera(const SceneInitialCamera& preset)
    {
        const float3 position(
            preset.Position[0],
            preset.Position[1],
            preset.Position[2]);
        const float3 direction = normalize(float3(
            preset.Direction[0],
            preset.Direction[1],
            preset.Direction[2]));
        const float3 upHint = normalize(float3(
            preset.Up[0],
            preset.Up[1],
            preset.Up[2]));
        const float3 right = normalize(cross(direction, upHint));
        const float3 up = normalize(cross(right, direction));
        ApplyCameraPose(
            position,
            direction,
            up,
            right,
            preset.VerticalFovDegrees);
    }

    void SetSponzaCameraLocation(SponzaCameraLocation location)
    {
        if (!m_SponzaCameraLocationsAvailable)
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

        m_ui.AntiAliasing = AntiAliasingSettings{};
        m_ui.TemporalAaSharpenEnabled = false;
        m_ui.TemporalAaSharpness = TemporalAaDefaultSharpness;
        m_ui.DirectionalShadows = DirectionalShadowSettings{};
        m_ui.ScreenSpaceDirectionalShadows =
            ScreenSpaceDirectionalShadowSettings{};
        m_ui.Representation = WorldSpaceRepresentationSettings{};
        if (m_HeitzRatioEstimatorShadowPass)
            m_HeitzRatioEstimatorShadowPass->ResetBindingCache();
        if (m_WorldSpaceRepresentation)
            m_WorldSpaceRepresentation->Reset();
        m_ui.ScreenSpaceVisibility = ScreenSpaceVisibilitySettings{};
        m_ui.PixelZoom = PixelZoomMode::Off;
        m_ui.FlashlightEnabled = DefaultFlashlightEnabled;
        m_ui.Flashlight = DefaultFlashlightSettings;
        m_FlashlightTransition = 0.f;
        if (m_Flashlight)
            m_Flashlight->intensity = 0.f;
        if (m_FlashlightHotspot)
            m_FlashlightHotspot->intensity = 0.f;
        ResetFlashlightMotion();
        m_ui.ShowEnvironmentBackground = true;
        m_ui.EnableAmbientFill = true;
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

        // Recreate passes and material permutations from the restored state.
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
            GLFW_KEY_SPACE,
            GLFW_KEY_LEFT,
            GLFW_KEY_RIGHT,
            GLFW_KEY_UP,
            GLFW_KEY_DOWN,
            GLFW_KEY_X,
            GLFW_KEY_C,
            GLFW_KEY_V,
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

    static CameraCollisionWorld BuildCameraCollisionWorld(
        const Scene& scene,
        float collisionRadius)
    {
        const auto extractionStart =
            std::chrono::high_resolution_clock::now();
        std::vector<CameraCollisionWorld::Triangle> triangles;
        const auto& instances =
            scene.GetSceneGraph()->GetMeshInstances();

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
        const auto extractionDuration =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                buildStart - extractionStart).count();
        CameraCollisionWorld collisionWorld;
        collisionWorld.Build(std::move(triangles));
        const auto buildDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - buildStart).count();
        log::info(
            "Camera collision: %zu triangles, %.3f-unit radius, extracted in %lld ms and built in %lld ms on the scene worker",
            collisionWorld.GetTriangleCount(),
            collisionRadius,
            static_cast<long long>(extractionDuration),
            static_cast<long long>(buildDuration));
        return collisionWorld;
    }

    virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
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

        if (m_MaterialPickPurpose == MaterialPickPurpose::None)
        {
            m_PickPosition =
                uint2(static_cast<uint>(xpos), static_cast<uint>(ypos));
        }

        return true;
    }

    virtual bool MouseButtonUpdate(int button, int action, int mods) override
    {
        GetActiveCamera().MouseButtonUpdate(button, action, mods);

        if (action == GLFW_PRESS &&
            button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            // Snapshot the cursor coordinate at the press. Subsequent camera
            // motion cannot slide the pending material-ID readback elsewhere.
            m_MaterialPickPurpose =
                MaterialPickPurpose::FocusCameraAtCursor;
            m_MaterialPickScene = m_Scene.get();
        }

        return true;
    }

    virtual bool MouseScrollUpdate(double xoffset, double yoffset) override
    {
        GetActiveCamera().MouseScrollUpdate(xoffset, yoffset);

        return true;
    }

    void ResetFlashlightMotion()
    {
        m_FlashlightSwayTime = 0.f;
        m_FlashlightAimDirection = float3(0.f, 0.f, -1.f);
        m_FlashlightResolvedPosition = 0.f;
        m_FlashlightResolvedDirection =
            float3(0.f, 0.f, -1.f);
        m_FlashlightResolvedRight =
            float3(1.f, 0.f, 0.f);
        m_FlashlightAimInitialized = false;
        m_FlashlightPoseValid = false;
    }

    void ApplyFlashlightPresentation()
    {
        if (!m_Flashlight)
            return;

        const FlashlightSettings settings =
            SanitizeFlashlightSettings(m_ui.Flashlight);
        m_ui.Flashlight = settings;
        const FlashlightLobeSettings lobes =
            ResolveFlashlightLobeSettings(settings);
        const float emissionScale =
            GetFlashlightEmissionScale(m_FlashlightTransition);
        const float3 color(
            settings.colorLinearRed,
            settings.colorLinearGreen,
            settings.colorLinearBlue);
        const std::shared_ptr<IShadowMap> activeShadowMap =
            settings.castShadows
                ? m_FlashlightShadowMap
                : nullptr;

        m_Flashlight->color = color;
        m_Flashlight->intensity =
            lobes.spillIntensityCandela * emissionScale;
        m_Flashlight->radius = FlashlightEmitterRadiusMeters;
        m_Flashlight->beamRoundness =
            settings.beamRoundness;
        m_Flashlight->range = settings.rangeMeters;
        m_Flashlight->innerAngle =
            lobes.spillInnerConeDegrees;
        m_Flashlight->outerAngle =
            lobes.spillOuterConeDegrees;
        m_Flashlight->shadowMap = activeShadowMap;

        if (m_FlashlightHotspot)
        {
            m_FlashlightHotspot->color = color;
            m_FlashlightHotspot->intensity =
                lobes.hotspotIntensityCandela * emissionScale;
            m_FlashlightHotspot->radius =
                FlashlightEmitterRadiusMeters;
            m_FlashlightHotspot->beamRoundness =
                settings.beamRoundness;
            m_FlashlightHotspot->range = settings.rangeMeters;
            m_FlashlightHotspot->innerAngle = std::max(
                lobes.hotspotInnerConeDegrees,
                0.5f);
            m_FlashlightHotspot->outerAngle = std::max(
                lobes.hotspotOuterConeDegrees,
                1.f);
            m_FlashlightHotspot->shadowMap = activeShadowMap;
        }
    }

    void UpdateFlashlightAnimation(float elapsedSeconds)
    {
        m_FlashlightTransition = AdvanceFlashlightTransition(
            m_FlashlightTransition,
            m_ui.FlashlightEnabled,
            elapsedSeconds);

        ApplyFlashlightPresentation();
        if (!ShouldSubmitFlashlight(m_FlashlightTransition))
            ResetFlashlightMotion();
    }

    static float3 ClampFlashlightAimLag(
        float3 candidate,
        float3 target)
    {
        candidate = normalize(candidate);
        target = normalize(target);
        const float maximumLagRadians =
            radians(FlashlightMaximumAimLagDegrees);
        const float maximumLagCosine =
            std::cos(maximumLagRadians);
        const float alignment = std::clamp(
            dot(candidate, target),
            -1.f,
            1.f);
        if (alignment >= maximumLagCosine)
            return candidate;

        const float3 tangent =
            candidate - target * alignment;
        const float tangentLengthSquared =
            lengthSquared(tangent);
        if (!(tangentLengthSquared > 1e-12f))
            return target;
        return normalize(
            target * maximumLagCosine +
            tangent * (
                std::sin(maximumLagRadians) /
                std::sqrt(tangentLengthSquared)));
    }

    static float3 InterpolateFlashlightAim(
        float3 current,
        float3 target,
        float blend)
    {
        current = normalize(current);
        target = normalize(target);
        blend = std::clamp(blend, 0.f, 1.f);
        const float alignment = std::clamp(
            dot(current, target),
            -1.f,
            1.f);
        if (alignment > 0.9995f)
            return normalize(
                current * (1.f - blend) +
                target * blend);
        if (alignment < -0.9995f)
            return target;

        const float angle = std::acos(alignment);
        const float inverseSine = 1.f / std::sin(angle);
        return normalize(
            current *
                (std::sin((1.f - blend) * angle) * inverseSine) +
            target *
                (std::sin(blend * angle) * inverseSine));
    }

    void UpdateFlashlightMotion(float elapsedSeconds)
    {
        if (!ShouldSubmitFlashlight(m_FlashlightTransition))
        {
            ResetFlashlightMotion();
            return;
        }

        const FlashlightSettings settings =
            SanitizeFlashlightSettings(m_ui.Flashlight);
        const BaseCamera& camera = GetActiveCamera();
        const float3 cameraDirection =
            normalize(camera.GetDir());
        const float3 cameraUp = normalize(camera.GetUp());
        float3 cameraRight = cross(cameraDirection, cameraUp);
        if (!(lengthSquared(cameraRight) > 1e-12f))
            cameraRight = float3(1.f, 0.f, 0.f);
        else
            cameraRight = normalize(cameraRight);

        const FlashlightMountPose mount =
            ResolveFlashlightMountPose(
                settings.cameraLateralOffsetMeters);
        const float3 flashlightPosition =
            camera.GetPosition() +
            cameraDirection *
                mount.positionForwardMeters +
            cameraRight *
                mount.positionRightMeters +
            cameraUp *
                mount.positionUpMeters;
        const float3 mountedDirection = normalize(
            cameraDirection *
                mount.directionForward +
            cameraRight *
                mount.directionRight +
            cameraUp *
                mount.directionUp);
        float3 mountedRight =
            cameraRight -
            mountedDirection *
                dot(cameraRight, mountedDirection);
        if (!(lengthSquared(mountedRight) > 1e-12f))
            mountedRight = cross(mountedDirection, cameraUp);
        if (!(lengthSquared(mountedRight) > 1e-12f))
            mountedRight = cameraRight;
        else
            mountedRight = normalize(mountedRight);
        m_FlashlightResolvedPosition = flashlightPosition;

        if (!settings.realisticLens)
        {
            m_FlashlightSwayTime = 0.f;
            m_FlashlightAimDirection = mountedDirection;
            m_FlashlightAimInitialized = true;
            m_FlashlightResolvedDirection = mountedDirection;
            m_FlashlightResolvedRight = mountedRight;
            m_FlashlightPoseValid = true;
            return;
        }

        if (!m_FlashlightAimInitialized)
        {
            m_FlashlightAimDirection = mountedDirection;
            m_FlashlightAimInitialized = true;
        }
        else
        {
            const float blend = GetFlashlightAimCorrectionBlend(
                elapsedSeconds,
                settings.aimCorrectionSeconds);
            m_FlashlightAimDirection = InterpolateFlashlightAim(
                m_FlashlightAimDirection,
                mountedDirection,
                blend);
            m_FlashlightAimDirection = ClampFlashlightAimLag(
                m_FlashlightAimDirection,
                mountedDirection);
        }

        m_FlashlightSwayTime = AdvanceFlashlightSwayTime(
            m_FlashlightSwayTime,
            elapsedSeconds);
        const FlashlightSwayOffset sway =
            ResolveFlashlightSwayOffset(
                m_FlashlightSwayTime,
                settings.swayDegrees *
                    GetFlashlightEmissionScale(
                        m_FlashlightTransition));
        float3 beamRight =
            cross(m_FlashlightAimDirection, cameraUp);
        if (!(lengthSquared(beamRight) > 1e-12f))
            beamRight = mountedRight;
        else
            beamRight = normalize(beamRight);
        const float3 beamUp = normalize(
            cross(beamRight, m_FlashlightAimDirection));
        m_FlashlightResolvedDirection = normalize(
            m_FlashlightAimDirection +
            beamRight * std::tan(radians(sway.yawDegrees)) +
            beamUp * std::tan(radians(sway.pitchDegrees)));
        beamRight -=
            m_FlashlightResolvedDirection *
                dot(beamRight, m_FlashlightResolvedDirection);
        if (!(lengthSquared(beamRight) > 1e-12f))
        {
            beamRight =
                mountedRight -
                m_FlashlightResolvedDirection *
                    dot(
                        mountedRight,
                        m_FlashlightResolvedDirection);
        }
        m_FlashlightResolvedRight =
            lengthSquared(beamRight) > 1e-12f
                ? normalize(beamRight)
                : float3(1.f, 0.f, 0.f);
        m_FlashlightPoseValid = true;
    }

    static void SetFlashlightDirectionAndRoll(
        const std::shared_ptr<FlashlightSpotLight>& light,
        const float3& direction,
        const float3& right)
    {
        if (!light || !light->GetNode())
            return;

        const double3 directionD =
            normalize(double3(direction));
        double3 rightD = double3(right);
        rightD -= directionD * dot(rightD, directionD);
        if (!(lengthSquared(rightD) > 1e-20))
            rightD = normalize(orthogonal(directionD));
        else
            rightD = normalize(rightD);
        const double3 upD =
            normalize(cross(rightD, directionD));

        SceneGraphNode* node = light->GetNode();
        SceneGraphNode* parent = node->GetParent();
        daffine3 parentToWorld = daffine3::identity();
        if (parent)
            parentToWorld =
                daffine3(parent->GetLocalToWorldTransform());

        const daffine3 worldToLocal =
            lookatZ(directionD, upD);
        const daffine3 localToParent =
            inverse(worldToLocal * parentToWorld);
        dquat rotation;
        double3 scaling;
        decomposeAffine<double>(
            localToParent,
            nullptr,
            &rotation,
            &scaling);
        node->SetTransform(nullptr, &rotation, &scaling);
    }

    void UpdateFlashlightTransform()
    {
        if (!m_FlashlightPoseValid ||
            !ShouldSubmitFlashlight(m_FlashlightTransition) ||
            !m_Flashlight ||
            !m_FlashlightNode)
            return;

        const double3 positionDelta =
            double3(m_FlashlightResolvedPosition) -
            m_Flashlight->GetPosition();
        if (dot(positionDelta, positionDelta) > 1e-20)
            m_Flashlight->SetPosition(
                double3(m_FlashlightResolvedPosition));

        const double3 directionDelta =
            double3(m_FlashlightResolvedDirection) -
            m_Flashlight->GetDirection();
        const float3 rightDelta =
            m_FlashlightResolvedRight -
            m_Flashlight->beamRight;
        if (dot(directionDelta, directionDelta) > 1e-20 ||
            lengthSquared(rightDelta) > 1e-12f)
        {
            SetFlashlightDirectionAndRoll(
                m_Flashlight,
                m_FlashlightResolvedDirection,
                m_FlashlightResolvedRight);
            SetFlashlightDirectionAndRoll(
                m_FlashlightHotspot,
                m_FlashlightResolvedDirection,
                m_FlashlightResolvedRight);
        }
        m_Flashlight->beamRight =
            m_FlashlightResolvedRight;

        if (m_FlashlightHotspot)
        {
            const double3 hotspotPositionDelta =
                double3(m_FlashlightResolvedPosition) -
                m_FlashlightHotspot->GetPosition();
            if (dot(
                    hotspotPositionDelta,
                    hotspotPositionDelta) > 1e-20)
            {
                m_FlashlightHotspot->SetPosition(
                    double3(m_FlashlightResolvedPosition));
            }
            m_FlashlightHotspot->beamRight =
                m_FlashlightResolvedRight;
        }
    }

    void AttachFlashlightToScene()
    {
        if (!m_Scene ||
            !m_Scene->GetSceneGraph() ||
            !m_Scene->GetSceneGraph()->GetRootNode())
        {
            return;
        }

        m_Flashlight =
            std::make_shared<FlashlightSpotLight>();
        m_Flashlight->SetName(FlashlightPublicName);
        m_FlashlightHotspot =
            std::make_shared<FlashlightSpotLight>();
        m_FlashlightHotspot->SetName(
            "flashlight_lens_hotspot");

        m_FlashlightNode = std::make_shared<SceneGraphNode>();
        m_FlashlightNode->SetName(FlashlightPublicName);
        m_FlashlightNode->SetLeaf(m_Flashlight);
        m_Scene->GetSceneGraph()->Attach(
            m_Scene->GetSceneGraph()->GetRootNode(),
            m_FlashlightNode);

        m_FlashlightHotspotNode =
            std::make_shared<SceneGraphNode>();
        m_FlashlightHotspotNode->SetName(
            "flashlight_lens_hotspot");
        m_FlashlightHotspotNode->SetLeaf(
            m_FlashlightHotspot);
        m_Scene->GetSceneGraph()->Attach(
            m_Scene->GetSceneGraph()->GetRootNode(),
            m_FlashlightHotspotNode);

        ApplyFlashlightPresentation();
        UpdateFlashlightTransform();
    }

    void CreateFlashlightShadowResources()
    {
        if (!m_FlashlightShadowMap)
        {
            constexpr nvrhi::Format depthFormats[] = {
                nvrhi::Format::D32,
                nvrhi::Format::D16
            };
            const nvrhi::FormatSupport required =
                nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::DepthStencil |
                nvrhi::FormatSupport::ShaderLoad |
                nvrhi::FormatSupport::ShaderSample;
            const nvrhi::Format depthFormat =
                nvrhi::utils::ChooseFormat(
                    GetDevice(),
                    required,
                    depthFormats,
                    std::size(depthFormats));
            if (depthFormat == nvrhi::Format::UNKNOWN)
            {
                log::error(
                    "Flashlight shadows are unavailable because the device "
                    "supports no sampled depth format.");
                m_FlashlightDepthPass.reset();
                return;
            }

            m_FlashlightShadowMap =
                std::make_shared<PlanarShadowMap>(
                    GetDevice(),
                    FlashlightShadowMapResolution,
                    depthFormat);
            m_FlashlightShadowMap->SetLitOutOfBounds(true);
            m_FlashlightShadowMap->SetFalloffDistance(0.f);
            m_FlashlightShadowFramebuffer =
                std::make_shared<FramebufferFactory>(GetDevice());
            m_FlashlightShadowFramebuffer->DepthTarget =
                m_FlashlightShadowMap->GetTexture();
        }

        m_FlashlightDepthPass =
            std::make_unique<DepthPass>(
                GetDevice(),
                m_CommonPasses);
        DepthPass::CreateParameters parameters;
        parameters.depthBias = FlashlightShadowDepthBias;
        parameters.slopeScaledDepthBias =
            FlashlightShadowSlopeScaledDepthBias;
        parameters.trackLiveness = false;
        m_FlashlightDepthPass->Init(
            *m_ShaderFactory,
            parameters);

        ApplyFlashlightPresentation();
    }

    void RenderFlashlightShadow()
    {
        if (!ShouldRenderFlashlightShadow(
                m_FlashlightTransition,
                m_ui.Flashlight.castShadows) ||
            !m_Flashlight ||
            !m_FlashlightNode ||
            !m_FlashlightShadowMap ||
            !m_FlashlightShadowFramebuffer ||
            !m_FlashlightDepthPass ||
            !m_Scene ||
            !m_OpaqueDrawStrategy)
        {
            return;
        }

        const float nearPlane = std::max(
            FlashlightShadowNearPlaneMeters,
            m_CameraCollisionRadius *
                FlashlightShadowCollisionNearScale);
        const float farPlane = std::max(
            m_Flashlight->range,
            nearPlane + 0.01f);
        const float verticalFov = radians(std::clamp(
            m_Flashlight->outerAngle +
                FlashlightShadowFovPaddingDegrees,
            1.f,
            179.f));

        daffine3 viewToWorld =
            m_FlashlightNode->GetLocalToWorldTransform();
        viewToWorld =
            scaling(double3(1.0, 1.0, -1.0)) *
            viewToWorld;
        const affine3 worldToView =
            affine3(inverse(viewToWorld));
        const float4x4 projection = perspProjD3DStyle(
            verticalFov,
            1.f,
            nearPlane,
            farPlane);
        const std::shared_ptr<PlanarView> shadowView =
            m_FlashlightShadowMap->GetPlanarView();
        shadowView->SetMatrices(worldToView, projection);
        shadowView->UpdateCache();

        const nvrhi::ITexture* shadowTexture =
            m_FlashlightShadowMap->GetTexture();
        const nvrhi::FormatInfo& shadowDepthInfo =
            nvrhi::getFormatInfo(shadowTexture->getDesc().format);
        m_CommandList->clearDepthStencilTexture(
            m_FlashlightShadowMap->GetTexture(),
            nvrhi::AllSubresources,
            true,
            1.f,
            shadowDepthInfo.hasStencil,
            0u);
        DepthPass::Context context;
        RenderCompositeView(
            m_CommandList,
            shadowView.get(),
            shadowView.get(),
            *m_FlashlightShadowFramebuffer,
            m_Scene->GetSceneGraph()->GetRootNode(),
            *m_OpaqueDrawStrategy,
            *m_FlashlightDepthPass,
            context,
            "FlashlightShadow",
            false);
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

        UpdateFlashlightAnimation(fElapsedTimeSeconds);
        UpdateFlashlightMotion(fElapsedTimeSeconds);
    }


    virtual void SceneUnloading() override
    {
        m_SceneFinishedLoading = false;
        m_SceneGpuUploadPending = false;
        m_ScenePreparationStage = ScenePreparationStage::Complete;
        m_RenderPassPreparationStage =
            RenderPassPreparationStage::Idle;
        if (m_PbrDeferredLightingPass) m_PbrDeferredLightingPass->ResetBindingCache();
        if (m_HeitzRatioEstimatorShadowPass)
            m_HeitzRatioEstimatorShadowPass->ResetBindingCache();
        if (m_WorldSpaceRepresentation)
            m_WorldSpaceRepresentation->Reset();
        if (m_ScreenSpaceVisibilityPass)
            m_ScreenSpaceVisibilityPass->ResetBindingCache();
        ResetAntiAliasingState();
        if (m_GBufferPass) m_GBufferPass->ResetBindingCache();
        if (m_FlashlightDepthPass)
            m_FlashlightDepthPass->ResetBindingCache();
        m_BindingCache.Clear();
        m_Flashlight.reset();
        m_FlashlightNode.reset();
        m_FlashlightHotspot.reset();
        m_FlashlightHotspotNode.reset();
        m_SceneLightsWithoutFlashlight.clear();
        m_EditableLights.clear();
        ResetFlashlightMotion();
        m_SunLight.reset();
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_ui.ShowMaterialEditor = false;
        m_MaterialPickPurpose = MaterialPickPurpose::None;
        m_MaterialPickScene = nullptr;
        m_OriginalMaterials.clear();
        m_PreviousView.reset();
        // Move the large vectors without freeing them here. The next loader
        // worker releases this retired world before allocating its replacement,
        // keeping hundreds of megabytes of allocator work off the render thread.
        m_RetiredCameraCollisionWorld.emplace(
            std::move(m_CameraCollisionWorld));
        m_CameraCollisionWorld = CameraCollisionWorld{};
        m_PendingSceneCpuState.reset();
        m_SubmittedMainViewTriangles = 0u;

    }

    virtual bool LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName) override
    {
        using namespace std::chrono;

        // SceneUnloading transfers the previous BVH here so its large vector
        // allocations are released by the loader rather than by a present
        // frame. This also lowers the peak before the replacement is built.
        m_RetiredCameraCollisionWorld.reset();
        m_PendingSceneCpuState.reset();

        std::unique_ptr<engine::Scene> scene = std::make_unique<engine::Scene>(GetDevice(),
            *m_ShaderFactory, fs, m_TextureCache, nullptr, nullptr);

        const auto startTime = high_resolution_clock::now();
        const uint32_t workerCount = ResolveSceneLoadWorkerCount(
            std::thread::hardware_concurrency());
        engine::ThreadPool threadPool(workerCount);
        log::info(
            "Scene import is using %u background workers",
            workerCount);

        if (scene->LoadWithThreadPool(fileName, &threadPool))
        {
            const auto importFinished = high_resolution_clock::now();

            // The scene is private to this worker, so transforms and CPU
            // importer arrays can be consumed without synchronizing with the
            // renderer. FinishedLoading later frees these arrays after their
            // bounded GPU upload completes.
            scene->RefreshSceneGraph(0u);
            const box3 loadedSceneBounds = scene->GetSceneGraph()
                ->GetRootNode()->GetGlobalBoundingBox();
            PreparedSceneCpuState prepared;
            prepared.sceneDiagonal = std::max(
                length(loadedSceneBounds.diagonal()),
                100.f);
            prepared.collisionRadius = std::max(
                0.1f,
                prepared.sceneDiagonal * 0.0005f);
            prepared.collisionWorld = BuildCameraCollisionWorld(
                *scene,
                prepared.collisionRadius);

            if (m_PreparedVisibilityBlueNoise.empty())
            {
                m_PreparedVisibilityBlueNoise =
                    GenerateVisibilityBlueNoise();
                log::info(
                    "Prepared visibility sampling ranks on the scene worker");
            }

            if (m_ImageBasedLightingEnvironment &&
                !m_ImageBasedLightingEnvironment->GetRadianceTexture())
            {
                prepared.environmentRadiance =
                    m_ImageBasedLightingEnvironment->PrepareRadiance(
                        m_ui.EnvironmentSource,
                        m_ui.WhiteWorld != WhiteWorldMode::Off);
            }

            // ApplicationBase publishes the LoadScene return value through an
            // atomic completion flag. Write the complete handoff before that
            // release so SceneLoaded never observes a partial scene state.
            m_PendingSceneCpuState.emplace(std::move(prepared));
            m_Scene = std::move(scene);

            const auto endTime = high_resolution_clock::now();
            const auto importDuration = duration_cast<milliseconds>(
                importFinished - startTime).count();
            const auto preparationDuration = duration_cast<milliseconds>(
                endTime - importFinished).count();
            log::info(
                "Scene worker completed import in %lld ms and CPU preparation in %lld ms",
                static_cast<long long>(importDuration),
                static_cast<long long>(preparationDuration));

            return true;
        }

        m_PendingSceneCpuState.reset();
        return false;
    }

    virtual void SceneLoaded() override
    {
        if (!m_PendingSceneCpuState || !m_Scene)
        {
            log::error(
                "Scene worker completed without a prepared CPU handoff");
            return;
        }

        // Do not publish a loaded scene until the worker handoff is known to
        // be internally complete. This keeps the base loading state coherent
        // even if a future importer exits without producing its CPU payload.
        Super::SceneLoaded();

        m_CameraCollisionWorld = std::move(
            m_PendingSceneCpuState->collisionWorld);
        m_SceneDiagonal = m_PendingSceneCpuState->sceneDiagonal;
        m_CameraCollisionRadius =
            m_PendingSceneCpuState->collisionRadius;
        if (m_ImageBasedLightingEnvironment &&
            m_PendingSceneCpuState->environmentRadiance)
        {
            m_ImageBasedLightingEnvironment->StagePreparedRadiance(
                std::move(
                    *m_PendingSceneCpuState->environmentRadiance));
        }
        m_PendingSceneCpuState.reset();

        m_Scene->BeginLoadingBuffers();
        m_SceneGpuUploadPending = true;
        m_ScenePreparationStage = ScenePreparationStage::MeshUpload;
        m_SceneGpuUploadStart =
            std::chrono::high_resolution_clock::now();
    }

    void CompleteSceneActivation()
    {

        m_OriginalMaterials.clear();
        for (const auto& material : m_Scene->GetSceneGraph()->GetMaterials())
            m_OriginalMaterials.emplace_back(material, *material);
        SetWhiteWorldMode(m_ui.WhiteWorld);

        for (auto light : m_Scene->GetSceneGraph()->GetLights())
        {
            const std::string normalizedLightName =
                NormalizeSceneLightName(light->GetName());
            if (normalizedLightName != light->GetName())
                light->SetName(normalizedLightName);

            if (!m_SunLight &&
                light->GetLightType() == LightType_Directional)
            {
                m_SunLight = std::static_pointer_cast<DirectionalLight>(light);
                if (m_SunLight->irradiance <= 0.f)
                    m_SunLight->irradiance = 1.f;
                if (!(m_SunLight->angularSize > 0.f) ||
                    !std::isfinite(m_SunLight->angularSize))
                {
                    m_SunLight->angularSize = 0.53f;
                }
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
            m_SunLight->SetName("sun_1");
            m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);
        }

        AttachFlashlightToScene();
        m_Scene->RefreshSceneGraph(GetFrameIndex());
        m_SceneLightsWithoutFlashlight.clear();
        m_EditableLights.clear();
        if (m_Flashlight)
            m_EditableLights.push_back(m_Flashlight);
        for (const auto& light :
            m_Scene->GetSceneGraph()->GetLights())
        {
            if (light &&
                light != m_Flashlight &&
                light != m_FlashlightHotspot)
            {
                m_SceneLightsWithoutFlashlight.push_back(light);
                m_EditableLights.push_back(light);
            }
        }

        const SponzaCameraPreset* sceneDefaultCamera = FindStandardSponzaCameraPreset(
            *m_NativeFs,
            m_CurrentSceneName);
        m_SponzaCameraLocationsAvailable = sceneDefaultCamera != nullptr;
        if (m_SponzaCameraLocationsAvailable)
        {
            m_SponzaCameraLocation =
                SponzaCameraLocation::SimplifiedApproximation;
        }
        const SponzaCameraPreset* sponzaCamera = m_SponzaCameraLocationsAvailable
            ? FindSponzaCameraPreset(m_SponzaCameraLocation)
            : nullptr;
        const SceneCatalogEntry* currentCatalogEntry =
            FindSceneCatalogEntry(m_SceneCatalog, m_CurrentSceneName);
        const SceneInitialCamera* sceneInitialCamera =
            currentCatalogEntry && currentCatalogEntry->InitialCamera
            ? &*currentCatalogEntry->InitialCamera
            : nullptr;
        if (sponzaCamera)
            m_CameraVerticalFov = sponzaCamera->VerticalFovDegrees;
        else if (sceneInitialCamera)
            m_CameraVerticalFov = sceneInitialCamera->VerticalFovDegrees;
        else
            m_CameraVerticalFov = 60.f;

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
        else if (sceneInitialCamera)
        {
            ApplySceneInitialCamera(*sceneInitialCamera);
            log::info(
                "Applied descriptor initial camera to '%s' at %.3f, %.3f, %.3f and %.1f degrees vertical FOV",
                m_CurrentSceneName.c_str(),
                sceneInitialCamera->Position[0],
                sceneInitialCamera->Position[1],
                sceneInitialCamera->Position[2],
                sceneInitialCamera->VerticalFovDegrees);
        }

        m_ui.Camera = CameraMode::ThirdPerson;

        if (!sponzaCamera && !sceneInitialCamera)
        {
            const float3 initialPosition = m_ThirdPersonCamera.GetPosition();
            const float3 initialDirection = m_ThirdPersonCamera.GetDir();
            const float3 initialUp = m_ThirdPersonCamera.GetUp();
            m_FirstPersonCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_PivotCamera.LookTo(initialPosition, initialDirection, initialUp);
            m_StaticCamera.LookTo(initialPosition, initialDirection, initialUp);
        }

        m_SceneFinishedLoading = true;

    }

    void SetWhiteWorldMode(WhiteWorldMode mode)
    {
        const bool modeChanged = m_ui.WhiteWorld != mode;
        const bool shaderModeChanged = (m_ui.WhiteWorld == WhiteWorldMode::Off) !=
            (mode == WhiteWorldMode::Off);
        m_ui.WhiteWorld = mode;

        if (modeChanged)
            ResetAntiAliasingState();

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

    [[nodiscard]] bool IsSceneBusy() const
    {
        return IsSceneLoading() || m_SceneGpuUploadPending;
    }

    [[nodiscard]] bool IsSceneGpuUploadPending() const
    {
        return m_SceneGpuUploadPending;
    }

    std::shared_ptr<Scene> GetScene()
    {
        return m_Scene;
    }

    void ToggleCenterMaterialInspector()
    {
        const bool centerPickPending =
            m_MaterialPickPurpose ==
                MaterialPickPurpose::OpenCenterMaterialInspector;
        if (m_ui.ShowMaterialEditor || centerPickPending)
        {
            m_ui.ShowMaterialEditor = false;
            if (centerPickPending)
            {
                m_MaterialPickPurpose = MaterialPickPurpose::None;
                m_MaterialPickScene = nullptr;
            }
            return;
        }

        if (!m_Scene || IsSceneBusy())
            return;

        // Never reveal the previous click selection while a fresh center sample
        // is pending. A miss therefore fails closed instead of reopening stale
        // material data.
        m_ui.ShowMaterialEditor = false;
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_MaterialPickPurpose =
            MaterialPickPurpose::OpenCenterMaterialInspector;
        m_MaterialPickScene = m_Scene.get();
    }

    const Material* GetOriginalMaterial(
        const std::shared_ptr<Material>& material) const
    {
        const auto original = std::find_if(
            m_OriginalMaterials.begin(),
            m_OriginalMaterials.end(),
            [&material](const auto& entry)
            {
                return entry.first == material;
            });
        return original != m_OriginalMaterials.end()
            ? &original->second
            : nullptr;
    }

    void NotifyMaterialCommandChanged(
        const std::shared_ptr<Material>& material)
    {
        if (!material)
            return;
        material->dirty = true;
        if (m_Scene && m_Scene->GetSceneGraph() &&
            m_Scene->GetSceneGraph()->GetRootNode())
        {
            m_Scene->GetSceneGraph()->GetRootNode()->
                InvalidateContent();
        }
        ResetImageBasedLightingHistory();
    }

    void SynchronizeAntiAliasingSettings()
    {
        const AntiAliasingSettings& applied =
            m_AppliedAntiAliasingSettings;
        const AntiAliasingSettings& requested =
            m_ui.AntiAliasing;
        if (m_HasAppliedAntiAliasingSettings && applied == requested)
        {
            return;
        }

        const bool requiresTemporalReset =
            m_HasAppliedAntiAliasingSettings &&
            AntiAliasingSettingsRequireTemporalReset(
                m_AppliedAntiAliasingSettings,
                m_ui.AntiAliasing);
        if (requiresTemporalReset)
        {
            ResetAntiAliasingState();
            // A new temporal sequence must not inherit a previous view whose
            // jitter phase belongs to the old preset or history layout.
            m_PreviousView.reset();
        }
        else if (!m_HasAppliedAntiAliasingSettings)
        {
            m_AntiAliasingPhase = 0u;
        }

        m_AppliedAntiAliasingSettings = m_ui.AntiAliasing;
        m_HasAppliedAntiAliasingSettings = true;
    }

    bool SetupView()
    {
        SynchronizeAntiAliasingSettings();

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
        TemporalAaJitterSample jitter{ 0.f, 0.f };
        const ResolvedAntiAliasingSettings antiAliasing =
            m_ui.GetResolvedAntiAliasingSettings();
        if (antiAliasing.temporalEnabled)
        {
            jitter = GetTemporalAaJitter(
                antiAliasing.temporalJitterSequence,
                m_AntiAliasingPhase);
        }
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

    [[nodiscard]] nvrhi::ITexture*
        GetPresentationAaInitializationSource() const
    {
        if (!m_RenderTargets)
            return nullptr;
        return m_RenderTargets->LdrColor.Get();
    }

    void CreateFastApproximateAAPass()
    {
        m_FastApproximateAAPass =
            std::make_unique<FastApproximateAAPass>(
                GetDevice(),
                m_ShaderFactory,
                m_CommonPasses,
                GetPresentationAaInitializationSource());
        if (!m_FastApproximateAAPass->IsValid())
        {
            log::error(
                "Fast Approximate AA initialization failed; "
                "the presentation input will be shown unchanged");
        }
    }

    void CreateCmaa2Pass()
    {
        m_Cmaa2Pass = std::make_unique<Cmaa2Pass>(
            GetDevice(),
            m_ShaderFactory,
            GetPresentationAaInitializationSource());
        if (!m_Cmaa2Pass->IsValid())
        {
            log::error(
                "Intel CMAA2 initialization failed; "
                "the presentation input will be shown unchanged");
        }
    }

    void CreateTemporalAAPass(bool deferPipelineCreation = false)
    {
        m_TemporalAAPass.reset();
        if (!m_ui.UsesLongTermTemporalAA())
            return;

        const bool multisampled =
            m_RenderTargets->GetSampleCount() > 1u;

        m_TemporalAAPass =
            std::make_unique<TemporalAAPass>(
                GetDevice(),
                m_ShaderFactory,
                m_CommonPasses,
                multisampled
                    ? m_RenderTargets->DeferredMsaaColor.Get()
                    : m_RenderTargets->HdrColor.Get(),
                multisampled
                    ? m_RenderTargets->VisibilityDepth.Get()
                    : m_RenderTargets->Depth.Get(),
                multisampled
                    ? m_RenderTargets->VisibilityMotionVectors.Get()
                    : m_RenderTargets->MotionVectors.Get(),
                deferPipelineCreation);
    }

    void EnsureMsaaVisibilityResolvePass(
        bool deferPipelineCreation = false)
    {
        if (!m_RenderTargets->VisibilityResourcesEnabled)
        {
            m_MsaaVisibilityResolvePass.reset();
            return;
        }
        if (m_MsaaVisibilityResolvePass)
            return;

        // All four sample-count PSOs are static. Materialize them while the
        // visibility renderer is first created instead of on the first Method
        // change to Multisample Adaptive.
        m_MsaaVisibilityResolvePass =
            std::make_unique<MsaaVisibilityResolvePass>(GetDevice());
        m_MsaaVisibilityResolvePass->Init(
            m_ShaderFactory, deferPipelineCreation);
    }

    void RefreshAntiAliasingTargetPasses()
    {
        // An AA method can change sample count and motion-vector topology
        // without changing the renderer, visibility consumers, or window.
        // Keep those expensive independent passes alive and refresh only the
        // objects whose shader or binding topology actually names a replaced
        // RenderTargets resource.
        m_TemporalAAPass.reset();

        GBufferFillPass::CreateParameters gbufferParams;
        gbufferParams.enableMotionVectors =
            m_RenderTargets->MotionVectorsEnabled;
        m_GBufferPass = std::make_shared<PbrGBufferFillPass>(
            GetDevice(),
            m_CommonPasses,
            m_ui.WhiteWorld != WhiteWorldMode::Off);
        m_GBufferPass->Init(*m_ShaderFactory, gbufferParams);

        m_PixelReadbackPass = std::make_unique<PixelReadbackPass>(
            GetDevice(),
            m_ShaderFactory,
            m_RenderTargets->MaterialIDs,
            nvrhi::Format::RGBA32_UINT);

        if (m_PbrDeferredLightingPass)
            m_PbrDeferredLightingPass->ResetBindingCache();
        if (m_ScreenSpaceVisibilityPass)
            m_ScreenSpaceVisibilityPass->ResetBindingCache();
        EnsureMsaaVisibilityResolvePass();

        CreateTemporalAAPass();
        if (m_FastApproximateAAPass)
        {
            m_FastApproximateAAPass->UpdateSourceColor(
                GetPresentationAaInitializationSource());
        }
        else if (m_ui.UsesFastApproximateAA())
        {
            CreateFastApproximateAAPass();
        }
        if (m_Cmaa2Pass)
        {
            // CMAA2 owns only same-sized single-sample intermediates and can
            // safely survive an MSAA/motion-vector target swap. Rebinding its
            // source avoids recreating the large candidate buffers, two edge
            // detector PSOs, and three shared pipelines on every technique
            // change.
            m_Cmaa2Pass->UpdateSourceColor(
                GetPresentationAaInitializationSource());
        }
        else if (m_ui.UsesCmaa2())
        {
            CreateCmaa2Pass();
        }

        m_ImageBasedLightingBackgroundPass =
            m_ImageBasedLightingEnvironment
                ? std::make_unique<ImageBasedLightingBackgroundPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_CommonPasses,
                    m_RenderTargets->HdrFramebuffer,
                    *m_View,
                    m_ImageBasedLightingEnvironment->
                        GetRadianceTextureResource())
                : nullptr;
        m_AgxToneMappingPass =
            std::make_unique<AgxToneMappingPass>(
                GetDevice(),
                m_ShaderFactory,
                m_CommonPasses,
                m_RenderTargets->LdrFramebuffer);
    }

    void BeginRenderPassPreparation(bool waitForIbl)
    {
        m_TemporalAAPass.reset();
        m_FastApproximateAAPass.reset();
        m_Cmaa2Pass.reset();
        m_RenderPassPreparationWaitForIbl = waitForIbl;
        m_RenderPassPreparationStage =
            RenderPassPreparationStage::GBuffer;
    }

    bool ProcessRenderPassPreparationStep()
    {
        switch (m_RenderPassPreparationStage)
        {
        case RenderPassPreparationStage::Idle:
        case RenderPassPreparationStage::Complete:
            return true;

        case RenderPassPreparationStage::GBuffer:
        {
            GBufferFillPass::CreateParameters gbufferParams;
            gbufferParams.enableMotionVectors =
                m_RenderTargets->MotionVectorsEnabled;
            m_GBufferPass =
                std::make_shared<PbrGBufferFillPass>(
                    GetDevice(),
                    m_CommonPasses,
                    m_ui.WhiteWorld != WhiteWorldMode::Off);
            m_GBufferPass->Init(*m_ShaderFactory, gbufferParams);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::MaterialId;
            return false;
        }

        case RenderPassPreparationStage::MaterialId:
        {
            GBufferFillPass::CreateParameters materialParams;
            materialParams.enableMotionVectors = false;
            m_MaterialIDPass =
                std::make_unique<MaterialIDPass>(
                    GetDevice(), m_CommonPasses);
            m_MaterialIDPass->Init(*m_ShaderFactory, materialParams);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::ReadbackAndFlashlight;
            return false;
        }

        case RenderPassPreparationStage::ReadbackAndFlashlight:
            m_PixelReadbackPass = std::make_unique<PixelReadbackPass>(
                GetDevice(),
                m_ShaderFactory,
                m_RenderTargets->MaterialIDs,
                nvrhi::Format::RGBA32_UINT);
            CreateFlashlightShadowResources();
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::DeferredLighting;
            return false;

        case RenderPassPreparationStage::DeferredLighting:
            m_PbrDeferredLightingPass =
                std::make_unique<PbrDeferredLightingPass>(
                    GetDevice(), m_CommonPasses);
            m_PbrDeferredLightingPass->Init(m_ShaderFactory, true);
            EnsureMsaaVisibilityResolvePass(true);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::DeferredLightingPipelines;
            return false;

        case RenderPassPreparationStage::DeferredLightingPipelines:
            if (m_PbrDeferredLightingPass &&
                !m_PbrDeferredLightingPass->PreparePipelinesStep())
            {
                return false;
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::MsaaVisibilityResolvePipelines;
            return false;

        case RenderPassPreparationStage::MsaaVisibilityResolvePipelines:
            if (m_MsaaVisibilityResolvePass &&
                !m_MsaaVisibilityResolvePass->PreparePipelinesStep())
            {
                return false;
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::Visibility;
            return false;

        case RenderPassPreparationStage::Visibility:
            m_ScreenSpaceVisibilityPass =
                std::make_unique<ScreenSpaceVisibilityPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_CommonPasses,
                    &m_PreparedVisibilityBlueNoise,
                    true);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::VisibilityPipelines;
            return false;

        case RenderPassPreparationStage::VisibilityPipelines:
            if (m_ScreenSpaceVisibilityPass &&
                !m_ScreenSpaceVisibilityPass->PreparePipelinesStep())
            {
                return false;
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::TemporalAA;
            return false;

        case RenderPassPreparationStage::TemporalAA:
            CreateTemporalAAPass(true);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::TemporalAAPipelines;
            return false;

        case RenderPassPreparationStage::TemporalAAPipelines:
            if (m_TemporalAAPass &&
                !m_TemporalAAPass->PreparePipelinesStep())
            {
                return false;
            }
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::FastApproximateAA;
            return false;

        case RenderPassPreparationStage::FastApproximateAA:
            if (m_ui.UsesFastApproximateAA())
                CreateFastApproximateAAPass();
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::Cmaa2;
            return false;

        case RenderPassPreparationStage::Cmaa2:
            if (m_ui.UsesCmaa2())
                CreateCmaa2Pass();
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::EnvironmentBackground;
            return false;

        case RenderPassPreparationStage::EnvironmentBackground:
            if (m_RenderPassPreparationWaitForIbl &&
                m_ImageBasedLightingEnvironment &&
                !m_ImageBasedLightingEnvironment->
                    IsPreparedRadianceReady())
            {
                return false;
            }
            m_ImageBasedLightingBackgroundPass =
                m_ImageBasedLightingEnvironment
                    ? std::make_unique<ImageBasedLightingBackgroundPass>(
                        GetDevice(),
                        m_ShaderFactory,
                        m_CommonPasses,
                        m_RenderTargets->HdrFramebuffer,
                        *m_View,
                        m_ImageBasedLightingEnvironment->
                            GetRadianceTextureResource())
                    : nullptr;
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::ToneMapping;
            return false;

        case RenderPassPreparationStage::ToneMapping:
            m_AgxToneMappingPass =
                std::make_unique<AgxToneMappingPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_CommonPasses,
                    m_RenderTargets->LdrFramebuffer);
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::Complete;
            m_RenderPassPreparationWaitForIbl = false;
            return true;
        }

        return false;
    }

    void CreateRenderPasses()
    {
        BeginRenderPassPreparation(false);
        while (!ProcessRenderPassPreparationStep())
        {
        }
    }

    void EnsureScreenSpaceDirectionalShadowPass()
    {
        if (m_ui.ScreenSpaceDirectionalShadows.enabled &&
            !m_ScreenSpaceDirectionalShadowPass)
        {
            m_ScreenSpaceDirectionalShadowPass =
                std::make_unique<ScreenSpaceDirectionalShadowPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_CommonPasses);
        }
    }

    void EnsureHeitzRatioEstimatorShadowPass()
    {
        if (!m_ui.DirectionalShadows.ratioEstimator.enabled ||
            m_HeitzRatioEstimatorShadowPass ||
            !SupportsHeitzRatioEstimatorShadows())
        {
            return;
        }
        m_HeitzRatioEstimatorShadowPass =
            std::make_unique<HeitzRatioEstimatorShadowPass>(
                GetDevice(),
                m_ShaderFactory,
                &m_PreparedVisibilityBlueNoise);
    }

    void UpdateImageBasedLighting(nvrhi::ICommandList* commandList)
    {
        if (!m_ImageBasedLightingEnvironment)
            return;

        constexpr float WhiteWorldIndirectReferenceScale = 4.0f;
        const bool whiteWorldEnabled =
            m_ui.WhiteWorld != WhiteWorldMode::Off;
        m_ImageBasedLightingEnvironment->Update(
            commandList,
            whiteWorldEnabled,
            whiteWorldEnabled
                ? WhiteWorldIndirectReferenceScale
                : 1.f,
            m_ui.EnvironmentExposureStops,
            m_ui.EnableAmbientFill &&
                m_ui.EnableDiffuseIbl,
            m_ui.DiffuseIblStrength,
            m_ui.EnableAmbientFill &&
                m_ui.EnableSpecularIbl,
            m_ui.SpecularIblStrength,
            m_ui.EnvironmentSource);
    }

    void RecordLoadingPresentationFrame()
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_LoadingPresentationFrameCount > 0u)
        {
            const double gapMilliseconds =
                std::chrono::duration<double, std::milli>(
                    now - m_LastLoadingPresentationFrame).count();
            m_MaximumLoadingPresentationGapMs = std::max(
                m_MaximumLoadingPresentationGapMs,
                gapMilliseconds);
        }
        m_LastLoadingPresentationFrame = now;
        ++m_LoadingPresentationFrameCount;
    }

    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override
    {
        RecordLoadingPresentationFrame();
        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->open();
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
    }

    bool PrepareLoadingRenderTargets(nvrhi::IFramebuffer* framebuffer)
    {
        const nvrhi::FramebufferInfoEx& framebufferInfo =
            framebuffer->getFramebufferInfo();
        if (framebufferInfo.width == 0u || framebufferInfo.height == 0u)
            return false;

        const uint2 renderSize(
            framebufferInfo.width,
            framebufferInfo.height);
        const uint32_t sampleCount = ResolveSupportedMsaaSampleCount(
            GetDevice(),
            m_ui.GetResolvedAntiAliasingSettings().rasterSampleCount);
        const bool screenSpaceVisibilityResourcesRequired =
            m_ui.HasActiveScreenSpaceVisibilityConsumer();
        const bool msaaClosestSurfaceResolveResourcesRequired =
            sampleCount > 1u;
        const bool visibilityResourcesRequired =
            screenSpaceVisibilityResourcesRequired ||
            msaaClosestSurfaceResolveResourcesRequired;
        const bool submittedLightingAvailable =
            !m_SceneLightsWithoutFlashlight.empty() ||
            (ShouldSubmitFlashlight(m_FlashlightTransition) &&
                bool(m_Flashlight));
        const bool visibilitySourceRadianceRequired =
            screenSpaceVisibilityResourcesRequired &&
            m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
            (submittedLightingAvailable ||
                IsAmbientFillLobeActive(
                    m_ui.EnableAmbientFill,
                    m_ui.EnableDiffuseIbl,
                    m_ui.DiffuseIblStrength));
        const bool motionVectorsRequired =
            m_ui.UsesLongTermTemporalAA() ||
            (visibilityResourcesRequired && sampleCount > 1u);

        bool needNewPasses = false;
        if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(
                renderSize,
                sampleCount,
                visibilityResourcesRequired,
                visibilitySourceRadianceRequired,
                motionVectorsRequired))
        {
            m_RenderTargets.reset();
            m_BindingCache.Clear();
            m_RenderTargets = std::make_unique<RenderTargets>();
            m_RenderTargets->Init(
                GetDevice(),
                renderSize,
                sampleCount,
                motionVectorsRequired,
                true,
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

        if (needNewPasses || !m_GBufferPass ||
            !m_MaterialIDPass || !m_AgxToneMappingPass)
        {
            BeginRenderPassPreparation(true);
        }
        else
        {
            m_RenderPassPreparationStage =
                RenderPassPreparationStage::Complete;
        }
        return true;
    }

    void RenderSceneGpuUploadFrame(nvrhi::IFramebuffer* framebuffer)
    {
        RecordLoadingPresentationFrame();
        nvrhi::ITexture* framebufferTexture =
            framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->open();
        m_CommandList->clearTextureFloat(
            framebufferTexture,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        switch (m_ScenePreparationStage)
        {
        case ScenePreparationStage::MeshUpload:
            if (m_Scene->ProcessLoadingBuffers(
                    m_CommandList,
                    c_SceneUploadBytesPerFrame,
                    GetFrameIndex()))
            {
                // Activation has its own loading frame so its material, light,
                // and camera setup cannot stack on the final mesh-buffer work.
                m_ScenePreparationStage =
                    ScenePreparationStage::SceneActivation;
            }
            break;

        case ScenePreparationStage::SceneActivation:
            CompleteSceneActivation();
            m_ScenePreparationStage =
                ScenePreparationStage::MaterialBuffers;
            break;

        case ScenePreparationStage::MaterialBuffers:
            // Activation applies the renderer's PBR defaults and can attach
            // its fallback lights, which dirties Donut's material and scene
            // buffers after the importer's final refresh. Consume those
            // writes on their own loading frame so the first visible scene
            // frame does not inherit the whole update.
            m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());
            m_ScenePreparationStage =
                ScenePreparationStage::WorldRepresentation;
            break;

        case ScenePreparationStage::WorldRepresentation:
            if (!m_ui.DirectionalShadows.ratioEstimator.enabled ||
                !SupportsHeitzRatioEstimatorShadows() ||
                !m_WorldSpaceRepresentation ||
                !m_WorldSpaceRepresentation->IsSupported() ||
                m_WorldSpaceRepresentation->GetStatus().state ==
                    WorldSpaceRepresentationState::Failed ||
                m_WorldSpaceRepresentation->Update(
                    m_CommandList,
                    m_Scene.get(),
                    m_ui.Representation,
                    uint32_t(GetFrameIndex()),
                    true))
            {
                m_ScenePreparationStage =
                    ScenePreparationStage::RenderTargets;
            }
            break;

        case ScenePreparationStage::RenderTargets:
            if (PrepareLoadingRenderTargets(framebuffer))
            {
                m_ScenePreparationStage =
                    ScenePreparationStage::RenderPasses;
            }
            break;

        case ScenePreparationStage::RenderPasses:
            if (ProcessRenderPassPreparationStep())
                m_ScenePreparationStage = ScenePreparationStage::Complete;
            break;

        case ScenePreparationStage::Complete:
            break;
        }

        // Consume worker-prepared HDR data one GPU unit per loading frame.
        // Partially generated environment maps are not exposed to rendering.
        UpdateImageBasedLighting(m_CommandList);
        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);

        const bool environmentReady =
            !m_ImageBasedLightingEnvironment ||
            m_ImageBasedLightingEnvironment->IsPreparedRadianceReady();
        if (m_ScenePreparationStage == ScenePreparationStage::Complete &&
            environmentReady)
        {
            m_SceneGpuUploadPending = false;
            const auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() -
                    m_SceneGpuUploadStart).count();
            log::info(
                "Staged scene upload and renderer preparation completed in %lld ms across %llu loading frames (maximum presentation gap %.2f ms)",
                static_cast<long long>(duration),
                static_cast<unsigned long long>(
                    m_LoadingPresentationFrameCount),
                m_MaximumLoadingPresentationGapMs);
            m_LastLoadingPresentationFrame = {};
            m_MaximumLoadingPresentationGapMs = 0.0;
            m_LoadingPresentationFrameCount = 0u;
        }
    }

    virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        if (m_SceneGpuUploadPending)
        {
            RenderSceneGpuUploadFrame(framebuffer);
            return;
        }

        EnsureScreenSpaceDirectionalShadowPass();
        EnsureHeitzRatioEstimatorShadowPass();

        int windowWidth, windowHeight;
        GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
        nvrhi::Viewport windowViewport = nvrhi::Viewport(float(windowWidth), float(windowHeight));
        nvrhi::Viewport renderViewport = windowViewport;

        UpdateFlashlightTransform();
        m_Scene->RefreshSceneGraph(GetFrameIndex());
        const auto& sceneLights =
            m_SceneLightsWithoutFlashlight;
        std::vector<std::shared_ptr<Light>> lightingLights;
        const std::vector<std::shared_ptr<Light>>* submittedLights =
            &sceneLights;
        if (ShouldSubmitFlashlight(m_FlashlightTransition) &&
            m_Flashlight)
        {
            // Local-light limits are deterministic: the user-controlled
            // flashlight is never silently dropped by a light-heavy scene.
            lightingLights.reserve(sceneLights.size() + 2u);
            lightingLights.push_back(m_Flashlight);
            if (m_ui.Flashlight.realisticLens &&
                m_FlashlightHotspot &&
                m_FlashlightHotspot->intensity > 0.f)
            {
                lightingLights.push_back(m_FlashlightHotspot);
            }
            for (const auto& light : sceneLights)
            {
                if (light)
                    lightingLights.push_back(light);
            }
            submittedLights = &lightingLights;
        }

        {
            uint width = windowWidth;
            uint height = windowHeight;

            const uint sampleCount = ResolveSupportedMsaaSampleCount(
                GetDevice(),
                m_ui.GetResolvedAntiAliasingSettings().rasterSampleCount);
            const bool screenSpaceVisibilityResourcesRequired =
                m_ui.HasActiveScreenSpaceVisibilityConsumer();
            // The directional visibility producers consume a single coherent
            // closest surface. Keep the resolve targets allocated for every
            // deferred PBR MSAA topology so toggling screen-space shadows does
            // not force an unrelated render-pass rebuild.
            const bool msaaClosestSurfaceResolveResourcesRequired =
                sampleCount > 1u;
            const bool visibilityResourcesRequired =
                screenSpaceVisibilityResourcesRequired ||
                msaaClosestSurfaceResolveResourcesRequired;
            const bool visibilitySourceRadianceRequired =
                screenSpaceVisibilityResourcesRequired &&
                m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
                (!submittedLights->empty() ||
                    IsAmbientFillLobeActive(
                        m_ui.EnableAmbientFill,
                        m_ui.EnableDiffuseIbl,
                        m_ui.DiffuseIblStrength));
            const bool temporalAARequired =
                m_ui.UsesLongTermTemporalAA();
            const bool fastApproximateAARequired =
                m_ui.UsesFastApproximateAA();
            const bool cmaa2Required = m_ui.UsesCmaa2();
            const bool motionVectorsRequired =
                temporalAARequired ||
                (visibilityResourcesRequired && sampleCount > 1u);

            bool needNewPasses = false;
            bool refreshAntiAliasingTargetPasses = false;
            bool antiAliasingSampleCountChanged = false;

            if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(
                uint2(width, height), sampleCount,
                visibilityResourcesRequired,
                visibilitySourceRadianceRequired,
                motionVectorsRequired))
            {
                const bool hadRenderTargets = bool(m_RenderTargets);
                const bool sameNonAaTopology = hadRenderTargets &&
                    all(m_RenderTargets->GetSize() == uint2(width, height)) &&
                    m_RenderTargets->VisibilityResourcesEnabled ==
                        visibilityResourcesRequired &&
                    m_RenderTargets->VisibilitySourceRadianceEnabled ==
                        (visibilityResourcesRequired &&
                            visibilitySourceRadianceRequired);
                antiAliasingSampleCountChanged = hadRenderTargets &&
                    m_RenderTargets->GetSampleCount() != sampleCount;
                const bool antiAliasingTopologyChanged =
                    antiAliasingSampleCountChanged ||
                    (hadRenderTargets &&
                        m_RenderTargets->MotionVectorsEnabled !=
                            motionVectorsRequired);

                if (m_HeitzRatioEstimatorShadowPass)
                    m_HeitzRatioEstimatorShadowPass->ResetBindingCache();
                m_RenderTargets = nullptr;
                m_BindingCache.Clear();
                m_RenderTargets = std::make_unique<RenderTargets>();
                m_RenderTargets->Init(
                    GetDevice(), uint2(width, height), sampleCount,
                    motionVectorsRequired, true,
                    visibilityResourcesRequired,
                    visibilitySourceRadianceRequired);
                m_PreviousView.reset();

                refreshAntiAliasingTargetPasses =
                    sameNonAaTopology && antiAliasingTopologyChanged;
                needNewPasses = !refreshAntiAliasingTargetPasses;
            }

            const bool refreshTemporalPass =
                temporalAARequired != bool(m_TemporalAAPass);
            if (SetupView())
            {
                needNewPasses = true;
                m_PreviousView.reset();
            }

            if (m_ui.ShaderReloadRequested)
            {
                m_ScreenSpaceDirectionalShadowPass.reset();
                m_HeitzRatioEstimatorShadowPass.reset();
                m_FlashlightDepthPass.reset();
                // This pass owns shader handles and PSOs independently of the
                // main pass set. Drop it before clearing the factory cache so
                // an explicit reload cannot retain the previous MSAA resolve.
                m_MsaaVisibilityResolvePass.reset();
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
            else if (refreshAntiAliasingTargetPasses)
            {
                RefreshAntiAliasingTargetPasses();
            }
            else if (refreshTemporalPass)
            {
                // A method transition does not invalidate scene, G-buffer,
                // lighting, visibility, sky, or output passes while the
                // render-target topology stays unchanged.
                CreateTemporalAAPass();
            }

            // Fast Approximate is a presentation-only spatial filter. Its
            // resources are independent of temporal history and can follow
            // any raster or temporal AA combination without rebuilding them.
            if (fastApproximateAARequired && !m_FastApproximateAAPass)
                CreateFastApproximateAAPass();
            else if (!fastApproximateAARequired && m_FastApproximateAAPass)
            {
                // A retained CMAA2 pass may still own a binding to the FXAA
                // output. Rebind it before releasing the producing pass so
                // disabling FXAA also releases that full-resolution texture.
                if (m_Cmaa2Pass)
                {
                    m_Cmaa2Pass->UpdateSourceColor(
                        GetPresentationAaInitializationSource());
                }
                m_FastApproximateAAPass.reset();
            }

            // CMAA2 is a presentation-only spatial filter when Temporal is
            // active. Allocate or retain it independently so changing only
            // morphology cannot recreate the temporal pass and lose history.
            if (cmaa2Required && !m_Cmaa2Pass)
                CreateCmaa2Pass();
            else if (!cmaa2Required &&
                m_Cmaa2Pass &&
                !temporalAARequired)
            {
                m_Cmaa2Pass.reset();
            }

            m_ui.ShaderReloadRequested = false;
        }

        EnsureScreenSpaceDirectionalShadowPass();
        EnsureHeitzRatioEstimatorShadowPass();

        m_CommandList->open();
        AdvanceRendererTimers();
        m_HeitzRatioEstimatorDispatchedThisFrame = false;
        BeginRendererStage(RendererTimingStage::CompleteFrame);
        BeginRendererStage(RendererTimingStage::SceneSetup);
        m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex());
        const bool heitzRatioEstimatorSelected =
            m_ui.DirectionalShadows.ratioEstimator.enabled &&
            m_RenderTargets->GetSampleCount() == 1u &&
            SupportsHeitzRatioEstimatorShadows();
        const uint64_t worldRepresentationGenerationBefore =
            m_WorldSpaceRepresentation
            ? m_WorldSpaceRepresentation->GetStatus().generation
            : 0u;
        const bool worldRepresentationReady =
            m_WorldSpaceRepresentation &&
            m_WorldSpaceRepresentation->Update(
                m_CommandList,
                m_Scene.get(),
                m_ui.Representation,
                uint32_t(GetFrameIndex()),
                heitzRatioEstimatorSelected);
        if (m_HeitzRatioEstimatorShadowPass &&
            m_WorldSpaceRepresentation &&
            (m_WorldSpaceRepresentation->GetStatus().generation !=
                    worldRepresentationGenerationBefore ||
                (heitzRatioEstimatorSelected &&
                    !worldRepresentationReady)))
        {
            m_HeitzRatioEstimatorShadowPass->ResetBindingCache();
            ResetAntiAliasingState();
        }

        const ResolvedAntiAliasingSettings antiAliasing =
            m_ui.GetResolvedAntiAliasingSettings();
        const bool temporalSharpenEnabled =
            antiAliasing.sharpeningAllowed &&
            ShouldSharpenTemporalAa(
                m_ui.TemporalAaSharpenEnabled,
                m_ui.TemporalAaSharpness);
        const bool deferTemporalSharpenToPresentation =
            temporalSharpenEnabled &&
            (antiAliasing.fastApproximateEnabled ||
                antiAliasing.cmaa2Enabled ||
                antiAliasing.historyStorage ==
                    TemporalAaHistoryStorage::Compact);
        const bool temporalAaWillRender =
            m_ui.UsesLongTermTemporalAA() &&
            m_TemporalAAPass &&
            m_TemporalAAPass->PrepareForRender(
                antiAliasing,
                temporalSharpenEnabled &&
                    !deferTemporalSharpenToPresentation,
                deferTemporalSharpenToPresentation,
                m_ui.TemporalAaSharpness);
        bool temporalAaRenderedThisFrame = false;
        const bool heitzRatioEstimatorExpectedToContribute =
            heitzRatioEstimatorSelected &&
            m_HeitzRatioEstimatorShadowPass &&
            worldRepresentationReady &&
            m_SunLight;
        if (heitzRatioEstimatorExpectedToContribute !=
            m_HeitzRatioEstimatorContributedLastFrame)
        {
            ResetAntiAliasingState();
            InvalidateRendererStageTiming(
                RendererTimingStage::RatioEstimatorShadows);
            m_HeitzRatioEstimatorContributedLastFrame =
                heitzRatioEstimatorExpectedToContribute;
        }

        nvrhi::ITexture* framebufferTexture = framebuffer->getDesc().colorAttachments[0].texture;
        m_CommandList->clearTextureFloat(framebufferTexture, nvrhi::AllSubresources, nvrhi::Color(0.f));

        UpdateImageBasedLighting(m_CommandList);
        const LightProbe* globalEnvironment =
            m_ImageBasedLightingEnvironment
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
        const bool runScreenSpaceVisibility =
            m_ui.HasActiveScreenSpaceVisibilityConsumer();
        const bool writeSourceRadiance = runScreenSpaceVisibility &&
            m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
            (!submittedLights->empty() ||
                (diffuseEnvironment && diffuseEnvironmentScale > 0.f));

        m_RenderTargets->Clear(m_CommandList);
        ScreenSpaceDirectionalShadowResult screenSpaceShadowResult;
        HeitzRatioEstimatorShadowResult heitzShadowResult;
        DirectionalLightVisibilities directionalVisibilities;
        MsaaVisibilityResolveOutputs closestSurfaceOutputs;
        bool closestSurfaceResolved = false;

        const auto resolveClosestMsaaSurface = [&]()
        {
            if (closestSurfaceResolved ||
                m_RenderTargets->GetSampleCount() <= 1u ||
                !m_MsaaVisibilityResolvePass)
            {
                return closestSurfaceResolved;
            }

            MsaaVisibilityResolveInputs resolveInputs;
            resolveInputs.depth = m_RenderTargets->Depth;
            resolveInputs.diffuse = m_RenderTargets->GBufferDiffuse;
            resolveInputs.material = m_RenderTargets->GBufferSpecular;
            resolveInputs.normals = m_RenderTargets->GBufferNormals;
            resolveInputs.emissive = m_RenderTargets->GBufferEmissive;
            resolveInputs.materialAmbientOcclusion =
                m_RenderTargets->MaterialAmbientOcclusion;
            resolveInputs.motionVectors =
                m_RenderTargets->MotionVectors;

            closestSurfaceOutputs.depth =
                m_RenderTargets->VisibilityDepth;
            closestSurfaceOutputs.diffuse =
                m_RenderTargets->VisibilityGBufferDiffuse;
            closestSurfaceOutputs.material =
                m_RenderTargets->VisibilityGBufferMaterial;
            closestSurfaceOutputs.normals =
                m_RenderTargets->VisibilityGBufferNormals;
            closestSurfaceOutputs.emissive =
                m_RenderTargets->VisibilityGBufferEmissive;
            closestSurfaceOutputs.materialAmbientOcclusion =
                m_RenderTargets->VisibilityMaterialAmbientOcclusion;
            closestSurfaceOutputs.motionVectors =
                m_RenderTargets->VisibilityMotionVectors;

            if (!closestSurfaceOutputs.depth ||
                !closestSurfaceOutputs.diffuse ||
                !closestSurfaceOutputs.material ||
                !closestSurfaceOutputs.normals ||
                !closestSurfaceOutputs.emissive ||
                !closestSurfaceOutputs.materialAmbientOcclusion ||
                !closestSurfaceOutputs.motionVectors)
            {
                return false;
            }

            BeginRendererStage(
                RendererTimingStage::MultisampleResolve);
            m_MsaaVisibilityResolvePass->Render(
                m_CommandList,
                resolveInputs,
                closestSurfaceOutputs,
                m_RenderTargets->GetSampleCount());
            EndRendererStage(
                RendererTimingStage::MultisampleResolve);
            closestSurfaceResolved = true;
            return true;
        };

        DeferredLightingPass::Inputs deferredMsaaInputs;
        bool deferredMsaaLightingPending = false;
        bool deferredMsaaVisibilityPending = false;
        m_SubmittedMainViewTriangles = 0u;
        RenderFlashlightShadow();

        EndRendererStage(RendererTimingStage::SceneSetup);

        {
        {
            GBufferFillPass::Context gbufferContext;
            SubmittedTriangleCountingPass geometryPass(
                *m_GBufferPass);

            BeginRendererStage(RendererTimingStage::Geometry);
            RenderCompositeView(m_CommandList,
                m_View.get(), m_PreviousView ? m_PreviousView.get() : m_View.get(),
                *m_RenderTargets->GBufferFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                geometryPass,
                gbufferContext,
                "GBufferFill",
                false);
            m_SubmittedMainViewTriangles =
                geometryPass.GetSubmittedTriangles();
            EndRendererStage(RendererTimingStage::Geometry);

            const bool directionalVisibilityProducerEnabled =
                m_ui.ScreenSpaceDirectionalShadows.enabled ||
                heitzRatioEstimatorSelected;
            if (m_RenderTargets->GetSampleCount() > 1u &&
                (runScreenSpaceVisibility ||
                    directionalVisibilityProducerEnabled ||
                    m_ui.UsesLongTermTemporalAA()))
            {
                resolveClosestMsaaSurface();
            }
            const bool singleSurfaceInputsAvailable =
                m_RenderTargets->GetSampleCount() == 1u ||
                closestSurfaceResolved;
            nvrhi::ITexture* visibilityDepth =
                closestSurfaceResolved
                    ? closestSurfaceOutputs.depth
                    : m_RenderTargets->Depth.Get();
            if (m_ui.ScreenSpaceDirectionalShadows.enabled &&
                m_ScreenSpaceDirectionalShadowPass &&
                singleSurfaceInputsAvailable)
            {
                screenSpaceShadowResult = m_ScreenSpaceDirectionalShadowPass->Render(
                    m_CommandList,
                    m_ui.ScreenSpaceDirectionalShadows,
                    *m_View,
                    visibilityDepth,
                    m_SunLight.get());
                directionalVisibilities.screenSpace = {
                    screenSpaceShadowResult.nearVisibility,
                    screenSpaceShadowResult.light,
                    DirectionalLightVisibilityEncoding::ScalarR8Unorm
                };
            }
            if (heitzRatioEstimatorSelected &&
                m_HeitzRatioEstimatorShadowPass &&
                worldRepresentationReady &&
                singleSurfaceInputsAvailable)
            {
                HeitzRatioEstimatorShadowInputs shadowInputs;
                shadowInputs.depth = visibilityDepth;
                shadowInputs.diffuse = closestSurfaceResolved
                    ? closestSurfaceOutputs.diffuse
                    : m_RenderTargets->GBufferDiffuse.Get();
                shadowInputs.material = closestSurfaceResolved
                    ? closestSurfaceOutputs.material
                    : m_RenderTargets->GBufferSpecular.Get();
                shadowInputs.normals = closestSurfaceResolved
                    ? closestSurfaceOutputs.normals
                    : m_RenderTargets->GBufferNormals.Get();
                shadowInputs.emissive = closestSurfaceResolved
                    ? closestSurfaceOutputs.emissive
                    : m_RenderTargets->GBufferEmissive.Get();
                shadowInputs.materialAmbientOcclusion =
                    closestSurfaceResolved
                        ? closestSurfaceOutputs.materialAmbientOcclusion
                        : m_RenderTargets->MaterialAmbientOcclusion.Get();
                BeginRendererStage(
                    RendererTimingStage::RatioEstimatorShadows);
                heitzShadowResult =
                    m_HeitzRatioEstimatorShadowPass->Render(
                        m_CommandList,
                        m_ui.DirectionalShadows.ratioEstimator,
                        *m_View,
                        shadowInputs,
                        m_WorldSpaceRepresentation
                            ->GetTopLevelAccelerationStructure(),
                        m_SunLight.get(),
                        uint32_t(m_HeitzRatioEstimatorPhase),
                        m_SceneDiagonal);
                EndRendererStage(
                    RendererTimingStage::RatioEstimatorShadows);
                directionalVisibilities.ratioEstimator = {
                    heitzShadowResult.modulation,
                    heitzShadowResult.light,
                    DirectionalLightVisibilityEncoding::RgbRgba16Float
                };
            }
            const bool heitzRatioEstimatorContributed =
                bool(heitzShadowResult);
            m_HeitzRatioEstimatorDispatchedThisFrame =
                heitzShadowResult.dispatched;
            if (heitzRatioEstimatorContributed !=
                m_HeitzRatioEstimatorContributedLastFrame)
            {
                ResetAntiAliasingState();
                InvalidateRendererStageTiming(
                    RendererTimingStage::RatioEstimatorShadows);
                m_HeitzRatioEstimatorContributedLastFrame =
                    heitzRatioEstimatorContributed;
            }
            DeferredLightingPass::Inputs deferredInputs;
            deferredInputs.SetGBuffer(*m_RenderTargets);
            // Slot 14 carries the authored material ambient-occlusion
            // attachment.
            deferredInputs.indirectDiffuse =
                m_RenderTargets->MaterialAmbientOcclusion;
            deferredInputs.lights = submittedLights;

            deferredInputs.output = runScreenSpaceVisibility
                ? m_RenderTargets->BaseLighting.Get()
                : m_RenderTargets->HdrColor.Get();

            if (m_RenderTargets->GetSampleCount() > 1u)
            {
                // Preserve every G-buffer sample until after material decode
                // and nonlinear lighting. The resolved sky is added later as
                // the exact uncovered-sample contribution.
                deferredMsaaInputs = deferredInputs;
                deferredMsaaInputs.output =
                    m_RenderTargets->DeferredMsaaColor;
                deferredMsaaLightingPending = true;

                if (runScreenSpaceVisibility &&
                    closestSurfaceResolved &&
                    m_ScreenSpaceVisibilityPass)
                {
                    DeferredLightingPass::Inputs
                        visibilityDeferredInputs =
                            deferredInputs;
                    visibilityDeferredInputs.depth =
                        closestSurfaceOutputs.depth;
                    visibilityDeferredInputs.gbufferDiffuse =
                        closestSurfaceOutputs.diffuse;
                    visibilityDeferredInputs.gbufferSpecular =
                        closestSurfaceOutputs.material;
                    visibilityDeferredInputs.gbufferNormals =
                        closestSurfaceOutputs.normals;
                    visibilityDeferredInputs.gbufferEmissive =
                        closestSurfaceOutputs.emissive;
                    visibilityDeferredInputs.indirectDiffuse =
                        closestSurfaceOutputs
                            .materialAmbientOcclusion;
                    visibilityDeferredInputs.output =
                        m_RenderTargets->BaseLighting;
                    BeginRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    m_PbrDeferredLightingPass->Render(
                        m_CommandList,
                        *m_View,
                        visibilityDeferredInputs,
                        directionalVisibilities,
                        globalEnvironment,
                        m_RenderTargets
                            ->DirectDiffuseRadiance,
                        true,
                        writeSourceRadiance,
                        uint32_t(m_ui.LightingDebugView),
                        uint32_t(m_ui.ScreenSpaceVisibility.debugView),
                        float2(0.f));

                    ScreenSpaceVisibilityInputs
                        visibilityInputs;
                    visibilityInputs.depth =
                        closestSurfaceOutputs.depth;
                    visibilityInputs.normals =
                        closestSurfaceOutputs.normals;
                    visibilityInputs.sourceRadiance =
                        writeSourceRadiance
                            ? m_RenderTargets->DirectDiffuseRadiance.Get()
                            : nullptr;
                    visibilityInputs.gbufferDiffuse =
                        closestSurfaceOutputs.diffuse;
                    visibilityInputs.gbufferSpecular =
                        closestSurfaceOutputs.material;
                    visibilityInputs.gbufferEmissive =
                        closestSurfaceOutputs.emissive;
                    visibilityInputs.materialAmbientOcclusion =
                        closestSurfaceOutputs
                            .materialAmbientOcclusion;
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
                    visibilityInputs.baseLighting =
                        m_RenderTargets->BaseLighting;
                    visibilityInputs.lightingDebugView =
                        uint32_t(m_ui.LightingDebugView);
                    visibilityInputs.output =
                        m_RenderTargets
                            ->VisibilityComposite;
                    m_ScreenSpaceVisibilityPass->Render(
                        m_CommandList,
                        m_ui.ScreenSpaceVisibility,
                        *m_View,
                        visibilityInputs,
                        uint32_t(GetFrameIndex()));
                    EndRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    deferredMsaaVisibilityPending = true;
                }
                else if (m_ScreenSpaceVisibilityPass)
                {
                    m_ScreenSpaceVisibilityPass->Deactivate();
                }
            }
            else
            {
                BeginRendererStage(RendererTimingStage::DirectLighting);
                m_PbrDeferredLightingPass->Render(
                    m_CommandList,
                    *m_View,
                    deferredInputs,
                    directionalVisibilities,
                    globalEnvironment,
                    m_RenderTargets->DirectDiffuseRadiance,
                    runScreenSpaceVisibility,
                    writeSourceRadiance,
                    uint32_t(m_ui.LightingDebugView),
                    uint32_t(m_ui.ScreenSpaceVisibility.debugView),
                    float2(0.f));
                EndRendererStage(RendererTimingStage::DirectLighting);

                if (runScreenSpaceVisibility)
                {
                    ScreenSpaceVisibilityInputs visibilityInputs;
                    visibilityInputs.depth = m_RenderTargets->Depth;
                    visibilityInputs.normals = m_RenderTargets->GBufferNormals;
                    visibilityInputs.sourceRadiance = writeSourceRadiance
                        ? m_RenderTargets->DirectDiffuseRadiance.Get()
                        : nullptr;
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
                    visibilityInputs.lightingDebugView =
                        uint32_t(m_ui.LightingDebugView);
                    visibilityInputs.output = m_RenderTargets->HdrColor;
                    BeginRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    m_ScreenSpaceVisibilityPass->Render(
                        m_CommandList,
                        m_ui.ScreenSpaceVisibility,
                        *m_View,
                        visibilityInputs,
                        uint32_t(GetFrameIndex()));
                    EndRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                }
                else
                {
                    m_ScreenSpaceVisibilityPass->Deactivate();
                }
            }
        }

        if (m_MaterialPickPurpose != MaterialPickPurpose::None &&
            m_MaterialPickScene != m_Scene.get())
        {
            m_MaterialPickPurpose = MaterialPickPurpose::None;
            m_MaterialPickScene = nullptr;
            m_ui.ShowMaterialEditor = false;
        }
        if (m_MaterialPickPurpose ==
            MaterialPickPurpose::OpenCenterMaterialInspector)
        {
            const nvrhi::TextureDesc& materialIdDesc =
                m_RenderTargets->MaterialIDs->getDesc();
            const CenterMaterialPick centerPick =
                ResolveCenterMaterialPick(
                    materialIdDesc.width,
                    materialIdDesc.height);
            if (centerPick.valid)
            {
                m_PickPosition =
                    uint2(centerPick.x, centerPick.y);
            }
            else
            {
                m_MaterialPickPurpose = MaterialPickPurpose::None;
                m_MaterialPickScene = nullptr;
            }
        }
        if (m_MaterialPickPurpose != MaterialPickPurpose::None)
        {
            BeginRendererStage(RendererTimingStage::MaterialPicking);
            m_CommandList->clearTextureUInt(
                m_RenderTargets->MaterialIDs,
                nvrhi::AllSubresources, 0xffffu);
            if (m_RenderTargets->MaterialIDDepth !=
                m_RenderTargets->Depth)
            {
                const nvrhi::FormatInfo& depthInfo =
                    nvrhi::getFormatInfo(
                        m_RenderTargets->MaterialIDDepth
                            ->getDesc().format);
                m_CommandList->clearDepthStencilTexture(
                    m_RenderTargets->MaterialIDDepth,
                    nvrhi::AllSubresources,
                    true,
                    0.f,
                    depthInfo.hasStencil,
                    0u);
            }

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
            EndRendererStage(RendererTimingStage::MaterialPicking);
        }

        if (m_ui.LightingDebugView == PbrLightingDebugView::None &&
            !m_ui.HasActiveScreenSpaceVisibilityDebugConsumer() &&
            m_ui.ShowEnvironmentBackground &&
            m_ImageBasedLightingBackgroundPass &&
            m_ImageBasedLightingEnvironment &&
            m_ImageBasedLightingEnvironment->GetRadianceTexture())
        {
            BeginRendererStage(RendererTimingStage::EnvironmentBackground);
            m_ImageBasedLightingBackgroundPass->Render(
                m_CommandList,
                *m_View,
                m_ImageBasedLightingEnvironment->
                    GetRadianceScale());
            EndRendererStage(RendererTimingStage::EnvironmentBackground);
        }

        nvrhi::ITexture* sceneColor =
            m_RenderTargets->HdrColor;
        const bool renderDeferredMsaaLighting =
            deferredMsaaLightingPending &&
            m_PbrDeferredLightingPass &&
            m_RenderTargets->ResolvedHdrColor &&
            m_RenderTargets->DeferredMsaaColor;
        if (renderDeferredMsaaLighting)
            BeginRendererStage(RendererTimingStage::DirectLighting);
        if (m_RenderTargets->GetSampleCount() > 1u)
        {
            if (m_RenderTargets->ResolvedHdrColor)
            {
                m_CommandList->resolveTexture(
                    m_RenderTargets->ResolvedHdrColor,
                    nvrhi::AllSubresources,
                    m_RenderTargets->HdrColor,
                    nvrhi::AllSubresources);
                sceneColor =
                    m_RenderTargets->ResolvedHdrColor;
            }
            else
            {
                log::error(
                    "MSAA HDR resolve target is unavailable; "
                    "the multisample surface cannot be presented");
            }
        }
        if (renderDeferredMsaaLighting)
        {
            // Include the required multisample color resolve and final
            // per-sample material/lighting evaluation in the direct-lighting
            // envelope. The earlier closest-surface pass remains separately
            // attributed because other effects can share it.
            m_PbrDeferredLightingPass->Render(
                m_CommandList,
                *m_View,
                deferredMsaaInputs,
                directionalVisibilities,
                globalEnvironment,
                nullptr,
                deferredMsaaVisibilityPending,
                false,
                uint32_t(m_ui.LightingDebugView),
                uint32_t(m_ui.ScreenSpaceVisibility.debugView),
                float2(0.f),
                m_RenderTargets->ResolvedHdrColor,
                m_RenderTargets->GetSampleCount(),
                deferredMsaaVisibilityPending
                    ? m_RenderTargets->BaseLighting.Get()
                    : nullptr,
                deferredMsaaVisibilityPending
                    ? m_RenderTargets
                          ->VisibilityComposite.Get()
                    : nullptr);
            EndRendererStage(RendererTimingStage::DirectLighting);
            sceneColor =
                m_RenderTargets->DeferredMsaaColor;
        }

        nvrhi::ITexture* antiAliasedTexture =
            sceneColor;
        if (temporalAaWillRender)
        {
            antiAliasedTexture = m_TemporalAAPass->Render(
                m_CommandList,
                *m_View,
                m_PreviousView.get(),
                m_AntiAliasingPhase,
                antiAliasing,
                temporalSharpenEnabled &&
                    !deferTemporalSharpenToPresentation,
                deferTemporalSharpenToPresentation,
                m_ui.TemporalAaSharpness);
            temporalAaRenderedThisFrame =
                m_TemporalAAPass->DidRenderThisFrame();

            if (temporalAaRenderedThisFrame &&
                deferTemporalSharpenToPresentation)
            {
                // Keep the resolved sharpen separate from compact history,
                // then let display mapping and spatial AA observe its final
                // edges.
                antiAliasedTexture =
                    m_TemporalAAPass
                        ->SharpenPresentation(
                            m_CommandList,
                            antiAliasedTexture);
            }

        }

        nvrhi::ITexture* displayTexture = antiAliasedTexture;
        BeginRendererStage(RendererTimingStage::ToneMapping);
        m_AgxToneMappingPass->Render(
            m_CommandList, *m_View, antiAliasedTexture);
        EndRendererStage(RendererTimingStage::ToneMapping);

        displayTexture = m_RenderTargets->LdrColor;

        if (antiAliasing.fastApproximateEnabled &&
            m_FastApproximateAAPass)
        {
            BeginRendererStage(RendererTimingStage::FastApproximate);
            displayTexture = m_FastApproximateAAPass->Render(
                m_CommandList,
                *m_View,
                displayTexture,
                antiAliasing);
            EndRendererStage(RendererTimingStage::FastApproximate);
        }

        if (antiAliasing.cmaa2Enabled && m_Cmaa2Pass)
        {
            displayTexture = m_Cmaa2Pass->Render(
                m_CommandList,
                displayTexture,
                antiAliasing);
        }

        BeginRendererStage(RendererTimingStage::OutputBlit);
        if (m_AgxToneMappingPass &&
            m_AgxToneMappingPass->RenderOutput(
                m_CommandList,
                *m_View,
                framebuffer,
                displayTexture))
        {
            // Transfer and dither are applied after presentation AA.
        }
        else
        {
            m_CommonPasses->BlitTexture(
                m_CommandList,
                framebuffer,
                displayTexture,
                &m_BindingCache);
        }
        if (screenSpaceShadowResult.HasDebugOutput() &&
            m_ScreenSpaceDirectionalShadowPass)
        {
            m_ScreenSpaceDirectionalShadowPass->PresentDebug(
                m_CommandList,
                framebuffer,
                screenSpaceShadowResult);
        }
        EndRendererStage(RendererTimingStage::OutputBlit);
        EndRendererStage(RendererTimingStage::CompleteFrame);
        CompleteRendererTimerFrame();

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        if (m_RenderTargets->MotionVectorsEnabled)
            CaptureCurrentViewForMotionVectors();
        if (temporalAaRenderedThisFrame)
            ++m_AntiAliasingPhase;
        if (heitzShadowResult.dispatched &&
            heitzShadowResult.stochastic)
        {
            ++m_HeitzRatioEstimatorPhase;
        }

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

        if (m_MaterialPickPurpose != MaterialPickPurpose::None)
        {
            const MaterialPickPurpose completedPurpose =
                m_MaterialPickPurpose;
            const Scene* completedScene = m_MaterialPickScene;
            m_MaterialPickPurpose = MaterialPickPurpose::None;
            m_MaterialPickScene = nullptr;
            uint4 pixelValue = m_PixelReadbackPass->ReadUInts();
            m_ui.SelectedMaterial = nullptr;
            m_ui.SelectedNode = nullptr;

            const bool completedForCurrentScene =
                completedScene == m_Scene.get();
            if (completedForCurrentScene)
            {
                for (const auto& material :
                    m_Scene->GetSceneGraph()->GetMaterials())
                {
                    if (material->materialID == int(pixelValue.x))
                    {
                        m_ui.SelectedMaterial = material;
                        break;
                    }
                }

                for (const auto& instance :
                    m_Scene->GetSceneGraph()->GetMeshInstances())
                {
                    if (instance->GetInstanceIndex() == int(pixelValue.y))
                    {
                        m_ui.SelectedNode =
                            instance->GetNodeSharedPtr();
                        break;
                    }
                }
            }

            if (completedPurpose ==
                MaterialPickPurpose::OpenCenterMaterialInspector)
            {
                m_ui.ShowMaterialEditor =
                    m_ui.SelectedMaterial != nullptr;
                if (m_ui.SelectedMaterial)
                {
                    log::info(
                        "Center material: %s",
                        m_ui.SelectedMaterial->name.c_str());
                }
            }
            else if (completedForCurrentScene &&
                completedPurpose ==
                    MaterialPickPurpose::FocusCameraAtCursor)
            {
                if (m_ui.SelectedNode)
                {
                    log::info(
                        "Picked node: %s",
                        m_ui.SelectedNode->GetPath()
                            .generic_string().c_str());
                    PointThirdPersonCameraAt(m_ui.SelectedNode);
                }
                else
                {
                    PointThirdPersonCameraAt(
                        m_Scene->GetSceneGraph()->GetRootNode());
                }
            }
        }

    }
    }

    std::shared_ptr<ShaderFactory> GetShaderFactory()
    {
        return m_ShaderFactory;
    }

    void ResetImageBasedLightingHistory()
    {
        ResetAntiAliasingState();
        InvalidateRendererStageTiming(
            RendererTimingStage::RatioEstimatorShadows);
        InvalidateRendererStageTiming(
            RendererTimingStage::CompleteFrame);
    }

    const ScreenSpaceVisibilityTimings* GetScreenSpaceVisibilityTimings() const
    {
        return m_ScreenSpaceVisibilityPass
            ? &m_ScreenSpaceVisibilityPass->GetTimings()
            : nullptr;
    }

    [[nodiscard]] uint32_t GetActiveRasterSampleCount() const
    {
        return m_RenderTargets && m_RenderTargets->Depth
            ? m_RenderTargets->Depth->getDesc().sampleCount
            : 1u;
    }

    const ScreenSpaceDirectionalShadowTimings*
        GetScreenSpaceDirectionalShadowTimings() const
    {
        return m_ScreenSpaceDirectionalShadowPass
            ? &m_ScreenSpaceDirectionalShadowPass->GetTimings()
            : nullptr;
    }

    bool HasPrimaryDirectionalLight() const
    {
        return bool(m_SunLight);
    }

    bool HasHeitzRatioEstimatorHardwareSupport() const
    {
        return m_WorldSpaceRepresentation &&
            m_WorldSpaceRepresentation->IsSupported() &&
            HeitzRatioEstimatorShadowPass::IsDeviceSupported(GetDevice());
    }

    bool SupportsHeitzRatioEstimatorShadows() const
    {
        return HasHeitzRatioEstimatorHardwareSupport() &&
            m_ui.GetResolvedAntiAliasingSettings().rasterSampleCount == 1u;
    }

    const WorldSpaceRepresentationStatus&
        GetWorldSpaceRepresentationStatus() const
    {
        static const WorldSpaceRepresentationStatus unsupported = {
            WorldSpaceRepresentationState::Unsupported
        };
        return m_WorldSpaceRepresentation
            ? m_WorldSpaceRepresentation->GetStatus()
            : unsupported;
    }

    void InvalidateWorldSpaceRepresentation(
        WorldSpaceRepresentationInvalidation invalidation)
    {
        if (invalidation != WorldSpaceRepresentationInvalidation::None &&
            m_HeitzRatioEstimatorShadowPass)
        {
            m_HeitzRatioEstimatorShadowPass->ResetBindingCache();
            ResetAntiAliasingState();
        }
        if (m_WorldSpaceRepresentation)
            m_WorldSpaceRepresentation->Invalidate(invalidation);
    }

    std::shared_ptr<DirectionalLight> GetPrimaryDirectionalLight() const
    {
        return m_SunLight;
    }

    const std::vector<std::shared_ptr<Light>>& GetEditableLights() const
    {
        return m_EditableLights;
    }

    bool IsFlashlight(const std::shared_ptr<Light>& light) const
    {
        return light && light == m_Flashlight;
    }

    const TemporalAATimings* GetTemporalAATimings() const
    {
        return m_TemporalAAPass
            ? &m_TemporalAAPass->GetTimings()
            : nullptr;
    }

    const Cmaa2Timings* GetCmaa2Timings() const
    {
        return m_Cmaa2Pass
            ? &m_Cmaa2Pass->GetTimings()
            : nullptr;
    }

    [[nodiscard]] uint64_t GetSubmittedMainViewTriangles() const
    {
        return m_SubmittedMainViewTriangles;
    }

    [[nodiscard]] const RendererTimings& GetRendererTimings() const
    {
        return m_RendererTimings;
    }

    [[nodiscard]] bool DidDispatchHeitzRatioEstimatorThisFrame() const
    {
        return m_HeitzRatioEstimatorDispatchedThisFrame;
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
        float opacity = 1.f;
        float shadowBlur = 0.f;
        float shadowOpacity = 0.f;

        float shadowOffsetY = 0.f;
        float3 padding;
    };

    static_assert(sizeof(BackdropBlurConstants) == 80u);

    class BackdropBlurPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<CommonRenderPasses> m_CommonPasses;
        BindingCache m_BindingCache;
        nvrhi::CommandListHandle m_CommandList;
        nvrhi::ShaderHandle m_BlurPixelShader;
        nvrhi::ShaderHandle m_CompositePixelShader;
        nvrhi::ShaderHandle m_ShadowPixelShader;
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
        nvrhi::GraphicsPipelineHandle m_ShadowPipeline;
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
            m_ShadowPipeline = nullptr;
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
                !m_CompositePixelShader ||
                !m_ShadowPixelShader)
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
            pipelineDesc.PS = m_ShadowPixelShader;
            m_ShadowPipeline = m_Device->createGraphicsPipeline(
                pipelineDesc, framebufferInfo);

            return
                m_DownsampleTexture &&
                m_HorizontalBlurTexture &&
                m_DownsampleFramebuffer &&
                m_HorizontalBlurFramebuffer &&
                m_HorizontalBindingSet &&
                m_CompositeBindingSet &&
                m_HorizontalPipeline &&
                m_CompositePipeline &&
                m_ShadowPipeline;
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
            shaderMacros[0] = ShaderMacro("COMPOSITE", "2");
            m_ShadowPixelShader = shaderFactory->CreateShader(
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
            const std::array<
                UiBackdropRect,
                UiBackdropRectCount>& backdropRects)
        {
            const float clampedBlurPixels =
                std::clamp(blurPixels, 0.f, 24.f);
            const bool renderBackdrop =
                clampedBlurPixels > 0.f;

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
            const bool hasVisibleShadow = std::any_of(
                backdropRects.begin(),
                backdropRects.end(),
                [](const UiBackdropRect& rect)
                {
                    return
                        rect.visible &&
                        rect.maxX > rect.minX &&
                        rect.maxY > rect.minY &&
                        rect.shadowBlur > 0.f &&
                        rect.shadowOpacity > 0.f &&
                        rect.opacity > 0.f;
                });
            if (!hasVisibleBackdrop ||
                (!renderBackdrop && !hasVisibleShadow) ||
                !EnsureResources(framebuffer))
            {
                return;
            }

            m_CommandList->open();
            m_CommandList->beginMarker("UI Backdrop Blur");

            const float blurRadius =
                renderBackdrop
                    ? std::max(0.5f, clampedBlurPixels * 0.5f)
                    : 0.f;
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

            nvrhi::DrawArguments drawArguments;
            drawArguments.instanceCount = 1;
            drawArguments.vertexCount = 4;
            if (renderBackdrop)
            {
                nvrhi::ITexture* framebufferTexture =
                    framebuffer->getDesc().colorAttachments[0].texture;
                BlitParameters downsampleParameters;
                downsampleParameters.targetFramebuffer =
                    m_DownsampleFramebuffer;
                downsampleParameters.sourceTexture = framebufferTexture;
                m_CommonPasses->BlitTexture(
                    m_CommandList,
                    downsampleParameters,
                    &m_BindingCache);

                m_CommandList->writeBuffer(
                    m_ConstantBuffer,
                    &constants,
                    sizeof(constants));

                nvrhi::GraphicsState horizontalState;
                horizontalState.pipeline = m_HorizontalPipeline;
                horizontalState.framebuffer =
                    m_HorizontalBlurFramebuffer;
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
                m_CommandList->draw(drawArguments);
            }

            for (const UiBackdropRect& backdropRect : backdropRects)
            {
                if (!backdropRect.visible ||
                    backdropRect.shadowBlur <= 0.f ||
                    backdropRect.shadowOpacity <= 0.f ||
                    backdropRect.opacity <= 0.f)
                {
                    continue;
                }

                constants.panelMin = float2(
                    backdropRect.minX,
                    backdropRect.minY);
                constants.panelSize = float2(
                    backdropRect.maxX - backdropRect.minX,
                    backdropRect.maxY - backdropRect.minY);
                constants.cornerRadius = backdropRect.rounding;
                constants.opacity = backdropRect.opacity;
                constants.shadowBlur = backdropRect.shadowBlur;
                constants.shadowOpacity = backdropRect.shadowOpacity;
                constants.shadowOffsetY = backdropRect.shadowOffsetY;
                m_CommandList->writeBuffer(
                    m_ConstantBuffer,
                    &constants,
                    sizeof(constants));

                const float shadowExtent = std::ceil(
                    backdropRect.shadowBlur +
                    std::abs(backdropRect.shadowOffsetY));
                const float shadowMinX = std::max(
                    0.f,
                    backdropRect.minX - shadowExtent);
                const float shadowMinY = std::max(
                    0.f,
                    backdropRect.minY - shadowExtent);
                const float shadowMaxX = std::min(
                    float(m_WindowWidth),
                    backdropRect.maxX + shadowExtent);
                const float shadowMaxY = std::min(
                    float(m_WindowHeight),
                    backdropRect.maxY + shadowExtent);
                const nvrhi::Viewport shadowViewport(
                    shadowMinX,
                    shadowMaxX,
                    shadowMinY,
                    shadowMaxY,
                    0.f,
                    1.f);

                nvrhi::GraphicsState shadowState;
                shadowState.pipeline = m_ShadowPipeline;
                shadowState.framebuffer = framebuffer;
                shadowState.bindings = { m_CompositeBindingSet };
                shadowState.viewport.addViewport(shadowViewport);
                shadowState.viewport.addScissorRect(
                    nvrhi::Rect(shadowViewport));
                m_CommandList->setGraphicsState(shadowState);
                m_CommandList->draw(drawArguments);
            }

            if (renderBackdrop)
            {
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
                    constants.opacity = backdropRect.opacity;
                    constants.shadowBlur = 0.f;
                    constants.shadowOpacity = 0.f;
                    constants.shadowOffsetY = 0.f;
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
            }

            m_CommandList->endMarker();
            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
        }
    };

    struct alignas(16) PixelZoomConstants
    {
        uint2 sourceSize;
        uint2 panelMin;

        uint2 panelSize;
        uint32_t zoomFactor = 0u;
        float cornerRadius = 8.f;

        float opacity = 0.f;
        float outlineWidth = 1.5f;
        float shadowBlur = UiPanelShadowBlurPixels;
        float shadowOpacity = UiPanelShadowOpacity;

        float shadowOffsetY = UiPanelShadowOffsetYPixels;
        float3 padding;

        float4 outlineTopColor;
        float4 outlineBottomColor;
    };

    static_assert(sizeof(PixelZoomConstants) == 96u);

    class PixelZoomPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<CommonRenderPasses> m_CommonPasses;
        nvrhi::CommandListHandle m_CommandList;
        nvrhi::ShaderHandle m_PixelShader;
        nvrhi::BindingLayoutHandle m_BindingLayout;
        nvrhi::BufferHandle m_ConstantBuffer;
        nvrhi::TextureHandle m_SourceTexture;
        nvrhi::BindingSetHandle m_BindingSet;
        nvrhi::GraphicsPipelineHandle m_Pipeline;
        uint32_t m_WindowWidth = 0u;
        uint32_t m_WindowHeight = 0u;
        nvrhi::Format m_FramebufferFormat = nvrhi::Format::UNKNOWN;
        bool m_CapturedFrame = false;

        void ResetResources()
        {
            m_SourceTexture = nullptr;
            m_BindingSet = nullptr;
            m_Pipeline = nullptr;
            m_WindowWidth = 0u;
            m_WindowHeight = 0u;
            m_FramebufferFormat = nvrhi::Format::UNKNOWN;
            m_CapturedFrame = false;
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
            if (m_SourceTexture &&
                m_WindowWidth == windowWidth &&
                m_WindowHeight == windowHeight &&
                m_FramebufferFormat == framebufferFormat)
            {
                return true;
            }

            ResetResources();
            if (windowWidth == 0u ||
                windowHeight == 0u ||
                !m_PixelShader)
            {
                return false;
            }

            m_WindowWidth = windowWidth;
            m_WindowHeight = windowHeight;
            m_FramebufferFormat = framebufferFormat;

            nvrhi::TextureDesc sourceDesc;
            sourceDesc.width = windowWidth;
            sourceDesc.height = windowHeight;
            sourceDesc.dimension = nvrhi::TextureDimension::Texture2D;
            sourceDesc.mipLevels = 1u;
            sourceDesc.format = framebufferFormat;
            sourceDesc.initialState = nvrhi::ResourceStates::ShaderResource;
            sourceDesc.keepInitialState = true;
            sourceDesc.debugName = "Pixel Zoom/Unmodified Presented Frame";
            m_SourceTexture = m_Device->createTexture(sourceDesc);

            nvrhi::BindingSetDesc bindingSetDesc;
            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_SourceTexture)
            };
            m_BindingSet = m_Device->createBindingSet(
                bindingSetDesc,
                m_BindingLayout);

            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
            pipelineDesc.VS = m_CommonPasses->m_FullscreenVS;
            pipelineDesc.PS = m_PixelShader;
            pipelineDesc.bindingLayouts = { m_BindingLayout };
            pipelineDesc.renderState.rasterState.setCullNone();
            pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
            pipelineDesc.renderState.depthStencilState.stencilEnable = false;
            pipelineDesc.renderState.blendState.targets[0]
                .setBlendEnable(true)
                .setSrcBlend(nvrhi::BlendFactor::SrcAlpha)
                .setDestBlend(nvrhi::BlendFactor::InvSrcAlpha)
                .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
                .setDestBlendAlpha(nvrhi::BlendFactor::One);
            m_Pipeline = m_Device->createGraphicsPipeline(
                pipelineDesc,
                framebufferInfo);

            return
                m_SourceTexture &&
                m_BindingSet &&
                m_Pipeline;
        }

    public:
        PixelZoomPass(
            nvrhi::IDevice* device,
            const std::shared_ptr<ShaderFactory>& shaderFactory,
            std::shared_ptr<CommonRenderPasses> commonPasses)
            : m_Device(device)
            , m_CommonPasses(std::move(commonPasses))
        {
            m_CommandList = device->createCommandList();
            m_PixelShader = shaderFactory->CreateShader(
                "uvsr/pixel_zoom_ps.hlsl",
                "main",
                nullptr,
                nvrhi::ShaderType::Pixel);

            nvrhi::BufferDesc constantBufferDesc;
            constantBufferDesc.byteSize = sizeof(PixelZoomConstants);
            constantBufferDesc.debugName = "Pixel Zoom/Constants";
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
                nvrhi::BindingLayoutItem::Texture_SRV(0)
            };
            m_BindingLayout =
                device->createBindingLayout(bindingLayoutDesc);
        }

        void BackBufferResizing()
        {
            ResetResources();
        }

        bool Capture(nvrhi::IFramebuffer* framebuffer)
        {
            m_CapturedFrame = false;
            if (!EnsureResources(framebuffer))
                return false;

            nvrhi::ITexture* framebufferTexture =
                framebuffer->getDesc().colorAttachments[0].texture;
            m_CommandList->open();
            m_CommandList->beginMarker("Pixel Zoom Capture");
            m_CommandList->copyTexture(
                m_SourceTexture,
                nvrhi::TextureSlice(),
                framebufferTexture,
                nvrhi::TextureSlice());
            m_CommandList->endMarker();
            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
            m_CapturedFrame = true;
            return true;
        }

        void Composite(
            nvrhi::IFramebuffer* framebuffer,
            PixelZoomMode mode,
            uint32_t panelMarginPixels,
            float cornerRadius,
            float opacity,
            float levelTransitionScale)
        {
            if (!m_CapturedFrame ||
                !IsPixelZoomEnabled(mode) ||
                opacity <= 0.f)
            {
                return;
            }

            const PixelZoomLayout layout =
                ResolveAnimatedPixelZoomLayout(
                    ResolvePixelZoomLayout(
                        m_WindowWidth,
                        m_WindowHeight,
                        panelMarginPixels,
                        mode),
                    opacity,
                    levelTransitionScale);
            if (layout.panelWidth == 0u ||
                layout.panelHeight == 0u ||
                layout.panelMinX + layout.panelWidth > m_WindowWidth ||
                layout.panelMinY + layout.panelHeight > m_WindowHeight)
            {
                m_CapturedFrame = false;
                return;
            }

            PixelZoomConstants constants{};
            constants.sourceSize = uint2(
                layout.sourceWidth,
                layout.sourceHeight);
            constants.panelMin = uint2(
                layout.panelMinX,
                layout.panelMinY);
            constants.panelSize = uint2(
                layout.panelWidth,
                layout.panelHeight);
            constants.zoomFactor = layout.zoomFactor;
            constants.cornerRadius = cornerRadius;
            constants.opacity = std::clamp(opacity, 0.f, 1.f);
            // A centered one-pixel ImGui stroke fully covers its edge texels.
            // The 1.5-pixel signed-distance band reproduces that visual weight
            // without filtering the magnified interior.
            constants.outlineWidth = 1.5f;
            constants.shadowBlur = UiPanelShadowBlurPixels;
            constants.shadowOpacity = UiPanelShadowOpacity;
            constants.shadowOffsetY = UiPanelShadowOffsetYPixels;
            constants.outlineTopColor =
                float4(0.88f, 0.90f, 0.94f, 0.10f);
            constants.outlineBottomColor =
                float4(0.96f, 0.97f, 1.00f, 0.30f);

            const float shadowExtent =
                std::ceil(
                    constants.shadowBlur +
                    constants.shadowOffsetY);
            const float minX = std::max(
                0.f,
                float(layout.panelMinX) - shadowExtent);
            const float minY = std::max(
                0.f,
                float(layout.panelMinY) - shadowExtent);
            const float maxX = std::min(
                float(m_WindowWidth),
                float(layout.panelMinX + layout.panelWidth) +
                    shadowExtent);
            const float maxY = std::min(
                float(m_WindowHeight),
                float(layout.panelMinY + layout.panelHeight) +
                    shadowExtent);
            const nvrhi::Viewport panelViewport(
                minX,
                maxX,
                minY,
                maxY,
                0.f,
                1.f);

            m_CommandList->open();
            m_CommandList->beginMarker("Pixel Zoom Composite");
            m_CommandList->writeBuffer(
                m_ConstantBuffer,
                &constants,
                sizeof(constants));

            nvrhi::GraphicsState graphicsState;
            graphicsState.pipeline = m_Pipeline;
            graphicsState.framebuffer = framebuffer;
            graphicsState.bindings = { m_BindingSet };
            graphicsState.viewport.addViewport(panelViewport);
            graphicsState.viewport.addScissorRect(
                nvrhi::Rect(panelViewport));
            m_CommandList->setGraphicsState(graphicsState);

            nvrhi::DrawArguments drawArguments;
            drawArguments.instanceCount = 1;
            drawArguments.vertexCount = 4;
            m_CommandList->draw(drawArguments);

            m_CommandList->endMarker();
            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
            m_CapturedFrame = false;
        }
    };
}

void UvsrSceneViewer::AdvanceRendererTimers()
{
    const uint32_t slot =
        m_RendererTimerFrame % c_RendererTimerLatency;
    m_RendererTimerFrameWritable = true;
    m_RendererTimerActive.fill(false);

    for (size_t stageIndex = 0u;
        stageIndex < static_cast<size_t>(RendererTimingStage::Count);
        ++stageIndex)
    {
        if (!m_RendererTimerPending[stageIndex][slot])
        {
            m_RendererTimings.available[stageIndex] = false;
            continue;
        }

        nvrhi::ITimerQuery* query =
            m_RendererTimerQueries[stageIndex][slot];
        if (!GetDevice()->pollTimerQuery(query))
        {
            m_RendererTimerFrameWritable = false;
            continue;
        }

        const bool currentEpoch =
            m_RendererTimerPendingEpoch[stageIndex][slot] ==
                m_RendererTimerStageEpoch[stageIndex];
        if (currentEpoch)
        {
            m_RendererTimings.milliseconds[stageIndex] =
                GetDevice()->getTimerQueryTime(query) * 1000.f;
        }
        m_RendererTimings.available[stageIndex] = currentEpoch;
        GetDevice()->resetTimerQuery(query);
        m_RendererTimerPending[stageIndex][slot] = false;
    }
}

void UvsrSceneViewer::BeginRendererStage(RendererTimingStage stage)
{
    if (!m_RendererTimerFrameWritable)
        return;

    const size_t stageIndex = static_cast<size_t>(stage);
    const uint32_t slot =
        m_RendererTimerFrame % c_RendererTimerLatency;
    if (m_RendererTimerPending[stageIndex][slot])
        return;

    m_CommandList->beginTimerQuery(
        m_RendererTimerQueries[stageIndex][slot]);
    m_RendererTimerActive[stageIndex] = true;
}

void UvsrSceneViewer::EndRendererStage(RendererTimingStage stage)
{
    const size_t stageIndex = static_cast<size_t>(stage);
    if (!m_RendererTimerActive[stageIndex])
        return;

    const uint32_t slot =
        m_RendererTimerFrame % c_RendererTimerLatency;
    m_CommandList->endTimerQuery(
        m_RendererTimerQueries[stageIndex][slot]);
    m_RendererTimerPending[stageIndex][slot] = true;
    m_RendererTimerPendingEpoch[stageIndex][slot] =
        m_RendererTimerStageEpoch[stageIndex];
    m_RendererTimerActive[stageIndex] = false;
}

void UvsrSceneViewer::CompleteRendererTimerFrame()
{
    if (m_RendererTimerFrameWritable)
        ++m_RendererTimerFrame;
}

void UvsrSceneViewer::InvalidateRendererStageTiming(
    RendererTimingStage stage)
{
    const size_t stageIndex = static_cast<size_t>(stage);
    ++m_RendererTimerStageEpoch[stageIndex];
    m_RendererTimings.available[stageIndex] = false;
}


class UIRenderer : public ImGui_Renderer
{
private:
    enum class StatisticsEffect : int
    {
        CompleteRenderer,
        SceneSetup,
        Geometry,
        DirectLighting,
        Visibility,
        Shadows,
        TemporalReconstructive,
        FastApproximate,
        ConservativeMorphological,
        Multisample,
        MaterialPicking,
        EnvironmentBackground,
        ToneMapping,
        OutputBlit,
        Count
    };

    struct UiVisualTokens
    {
        ImVec4 drawerHeader;
        ImVec4 drawerHeaderHovered;
        ImVec4 drawerHeaderActive;
        ImVec4 drawerBackground;
        ImVec4 drawerFrame;
        ImVec4 drawerFrameHovered;
        ImVec4 drawerFrameActive;
        ImVec4 outlineTop;
        ImVec4 outlineBottom;
        ImVec4 panelBodySurface;
        ImVec4 settingsTitleSurface;
        ImVec4 actionButton;
        ImVec4 actionButtonHovered;
        ImVec4 actionButtonActive;
        float drawerRounding = 5.f;
        float backdropShadowBlur = UiPanelShadowBlurPixels;
        float backdropShadowOpacity = UiPanelShadowOpacity;
        float backdropShadowOffsetY = UiPanelShadowOffsetYPixels;
        float floatingPanelOpacity = 0.82f;
        ImVec4 errorText = ImVec4(0.92f, 0.12f, 0.16f, 1.f);
        // Match MaterialEditor's texture-filename accent in the staged Donut
        // override so success has one deliberate product color everywhere.
        ImVec4 successText = ImVec4(0.26f, 0.59f, 0.98f, 1.f);
        bool drawControlOutlines = true;
        bool drawScrollEdgeFades = true;
    };

    struct StatSnapshot
    {
        int width = 0;
        int height = 0;
        uint64_t submittedTriangles = 0u;
        double frameTimeSeconds = 0.0;
        std::string rendererName;
        GpuPerformanceMetrics gpuMetrics;
        ScreenSpaceDirectionalShadowTimings screenSpaceShadowTimings;
        ScreenSpaceVisibilityTimings visibilityTimings;
        TemporalAATimings temporalAATimings;
        Cmaa2Timings cmaa2Timings;
        bool hasScreenSpaceShadowTimings = false;
        bool hasVisibilityTimings = false;
        bool hasTemporalAATimings = false;
        bool hasCmaa2Timings = false;
    };

    std::shared_ptr<UvsrSceneViewer> m_app;

    std::shared_ptr<app::RegisteredFont> m_Font;
    std::shared_ptr<engine::Light> m_SelectedLight;
    ImGuiID m_AdjustedSpaceFontBakedId = 0;
    float m_BaseSpaceAdvance = 0.f;
    double m_DisplayedFrameTime = 0.0;
    double m_DisplayedGpuBandwidthGBps = 0.0;
    double m_DisplayedGpuTFlops = 0.0;
    double m_StatSnapshotElapsed = 0.0;
    double m_StatFrameTimeSum = 0.0;
    uint32_t m_StatFrameTimeCount = 0;
    std::array<std::string, 6> m_PerformanceStatValues;
    ScreenSpaceDirectionalShadowTimings m_DisplayedScreenSpaceShadowTimings;
    ScreenSpaceVisibilityTimings m_DisplayedVisibilityTimings;
    TemporalAATimings m_DisplayedTemporalAATimings;
    Cmaa2Timings m_DisplayedCmaa2Timings;
    std::deque<StatSnapshot> m_StatUpdateQueue;
    bool m_HasAppliedStatSnapshot = false;
    bool m_HasGpuStatSnapshot = false;
    bool m_HasScreenSpaceShadowStatSnapshot = false;
    bool m_HasVisibilityStatSnapshot = false;
    bool m_HasTemporalAAStatSnapshot = false;
    bool m_HasCmaa2StatSnapshot = false;
    bool m_WasSceneLoading = false;
    std::unique_ptr<BackdropBlurPass> m_BackdropBlurPass;
    std::unique_ptr<PixelZoomPass> m_PixelZoomPass;
    uint32_t m_SettingsPanelMarginPixels = 10u;
    float m_UiDisplayScale = 1.f;
    float m_SettingsAppearance = 0.f;
    PixelZoomMode m_RenderedPixelZoom = PixelZoomMode::Off;
    PixelZoomMode m_PendingPixelZoom = PixelZoomMode::Off;
    float m_PixelZoomVisibility = 0.f;
    float m_PixelZoomLevelTransition = 1.f;
    float m_MaterialInspectorZoomPlacement = 0.f;
    float m_MaterialInspectorAppearance = 0.f;
    UiSkin m_ComposedUiSkin = DefaultUiSkin;
    bool m_CommandOpen = false;
    float m_CommandAppearance = 0.f;
    bool m_CommandFocusRequested = false;
    bool m_SuppressCommandShortcutSlashCharacter = false;
    std::array<char, 512> m_CommandBuffer = {};
    std::deque<std::string> m_CommandHistory;
    int m_CommandHistoryIndex = -1;
    std::string m_CommandResult;
    bool m_CommandResultIsError = false;
    std::optional<UiCommand> m_PendingCommand;
    CommandInterfaceLayout m_CommandLayout;
    bool m_SettingsCollapsed = false;
    std::optional<bool> m_SettingsCollapsedRequest;
    int m_StatisticsEffect =
        static_cast<int>(StatisticsEffect::CompleteRenderer);

    struct LightDefaultState
    {
        int type = LightType_None;
        double3 direction = double3(0.0, -1.0, 0.0);
        float3 color = float3(1.f);
        float irradiance = 1.f;
        float angularSize = 0.f;
        float radius = 0.f;
        float intensity = 1.f;
        float innerAngle = 180.f;
        float outerAngle = 180.f;
    };

    std::unordered_map<
        std::string,
        LightDefaultState> m_LightDefaults;

	UIData& m_ui;

    inline static std::vector<ImDrawList*>
        g_SettingsAppearanceDrawLists;
    inline static UiVisualTokens g_UiVisualTokens;

    static void TrackSettingsAppearanceDrawList(ImDrawList* drawList)
    {
        if (drawList &&
            std::find(
                g_SettingsAppearanceDrawLists.begin(),
                g_SettingsAppearanceDrawLists.end(),
                drawList) == g_SettingsAppearanceDrawLists.end())
        {
            g_SettingsAppearanceDrawLists.push_back(drawList);
        }
    }

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

    static void ApplyWindowAppearance(
        ImDrawList* drawList,
        const ImVec2& pivot,
        float scale,
        float opacity)
    {
        if (!drawList)
            return;

        const float clampedScale = std::clamp(scale, 0.f, 1.f);
        const float clampedOpacity = std::clamp(opacity, 0.f, 1.f);
        if (clampedScale >= 1.f && clampedOpacity >= 1.f)
            return;

        for (ImDrawVert& vertex : drawList->VtxBuffer)
        {
            vertex.pos = ImVec2(
                pivot.x + (vertex.pos.x - pivot.x) * clampedScale,
                pivot.y + (vertex.pos.y - pivot.y) * clampedScale);
            const uint32_t alpha = (vertex.col >> 24u) & 0xffu;
            const uint32_t fadedAlpha = static_cast<uint32_t>(
                std::round(float(alpha) * clampedOpacity));
            vertex.col =
                (vertex.col & 0x00ffffffu) |
                (fadedAlpha << 24u);
        }
        for (ImDrawCmd& command : drawList->CmdBuffer)
        {
            command.ClipRect = ImVec4(
                pivot.x +
                    (command.ClipRect.x - pivot.x) * clampedScale,
                pivot.y +
                    (command.ClipRect.y - pivot.y) * clampedScale,
                pivot.x +
                    (command.ClipRect.z - pivot.x) * clampedScale,
                pivot.y +
                    (command.ClipRect.w - pivot.y) * clampedScale);
        }
    }

    static void ApplyCommandWindowAppearance(
        ImDrawList* drawList,
        float bottom,
        float verticalScale,
        float opacity)
    {
        if (!drawList)
            return;

        const float clampedScale =
            std::clamp(verticalScale, 0.f, 1.f);
        const float clampedOpacity =
            std::clamp(opacity, 0.f, 1.f);
        if (clampedScale >= 1.f && clampedOpacity >= 1.f)
            return;

        for (ImDrawVert& vertex : drawList->VtxBuffer)
        {
            vertex.pos.y =
                bottom + (vertex.pos.y - bottom) * clampedScale;
            const uint32_t alpha = (vertex.col >> 24u) & 0xffu;
            const uint32_t fadedAlpha = static_cast<uint32_t>(
                std::round(float(alpha) * clampedOpacity));
            vertex.col =
                (vertex.col & 0x00ffffffu) |
                (fadedAlpha << 24u);
        }
        for (ImDrawCmd& command : drawList->CmdBuffer)
        {
            command.ClipRect.y =
                bottom +
                (command.ClipRect.y - bottom) * clampedScale;
            command.ClipRect.w =
                bottom +
                (command.ClipRect.w - bottom) * clampedScale;
        }
    }

    static void ApplyCommandBackdropAppearance(
        UiBackdropRect& backdropRect,
        float bottom,
        float verticalScale,
        float opacity)
    {
        const float clampedScale =
            std::clamp(verticalScale, 0.f, 1.f);
        backdropRect.minY =
            bottom + (backdropRect.minY - bottom) * clampedScale;
        backdropRect.maxY =
            bottom + (backdropRect.maxY - bottom) * clampedScale;
        backdropRect.opacity = std::clamp(opacity, 0.f, 1.f);
    }

    static void ApplyBackdropAppearance(
        UiBackdropRect& backdropRect,
        const ImVec2& pivot,
        float scale,
        float opacity)
    {
        const float clampedScale = std::clamp(scale, 0.f, 1.f);
        backdropRect.minX =
            pivot.x + (backdropRect.minX - pivot.x) * clampedScale;
        backdropRect.minY =
            pivot.y + (backdropRect.minY - pivot.y) * clampedScale;
        backdropRect.maxX =
            pivot.x + (backdropRect.maxX - pivot.x) * clampedScale;
        backdropRect.maxY =
            pivot.y + (backdropRect.maxY - pivot.y) * clampedScale;
        backdropRect.opacity = std::clamp(opacity, 0.f, 1.f);
    }

    static ImVec4 CompositeUiColorOver(
        const ImVec4& foreground,
        const ImVec4& background)
    {
        const float foregroundAlpha =
            std::clamp(foreground.w, 0.f, 1.f);
        const float backgroundAlpha =
            std::clamp(background.w, 0.f, 1.f);
        const float outputAlpha =
            foregroundAlpha +
            backgroundAlpha * (1.f - foregroundAlpha);
        if (outputAlpha <= 0.f)
            return ImVec4(0.f, 0.f, 0.f, 0.f);

        const float backgroundContribution =
            backgroundAlpha * (1.f - foregroundAlpha);
        return ImVec4(
            (foreground.x * foregroundAlpha +
                background.x * backgroundContribution) /
                outputAlpha,
            (foreground.y * foregroundAlpha +
                background.y * backgroundContribution) /
                outputAlpha,
            (foreground.z * foregroundAlpha +
                background.z * backgroundContribution) /
                outputAlpha,
            outputAlpha);
    }

    static void ApplyUiSkin(
        UiSkin skin,
        float displayScale)
    {
        const UiSkin resolvedSkin =
            skin == UiSkin::Og ? UiSkin::Og : UiSkin::Amp;
        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);
        ImVec4* colors = style.Colors;
        UiVisualTokens tokens;

        if (resolvedSkin == UiSkin::Og)
        {
            style.ScrollbarRounding = 0.f;
            tokens.drawerHeader = colors[ImGuiCol_Header];
            tokens.drawerHeaderHovered =
                colors[ImGuiCol_HeaderHovered];
            tokens.drawerHeaderActive =
                colors[ImGuiCol_HeaderActive];
            tokens.drawerBackground = colors[ImGuiCol_ChildBg];
            tokens.drawerFrame = colors[ImGuiCol_FrameBg];
            tokens.drawerFrameHovered =
                colors[ImGuiCol_FrameBgHovered];
            tokens.drawerFrameActive =
                colors[ImGuiCol_FrameBgActive];
            tokens.outlineTop = colors[ImGuiCol_Border];
            tokens.outlineBottom = colors[ImGuiCol_Border];
            tokens.actionButton = colors[ImGuiCol_Button];
            tokens.actionButtonHovered =
                colors[ImGuiCol_ButtonHovered];
            tokens.actionButtonActive =
                colors[ImGuiCol_ButtonActive];
            tokens.drawerRounding = style.ChildRounding;
            tokens.drawControlOutlines = false;
            tokens.drawScrollEdgeFades = false;
            tokens.floatingPanelOpacity =
                colors[ImGuiCol_WindowBg].w;
        }
        else
        {
            style.WindowRounding = 8.f;
            style.ChildRounding = 8.f;
            style.PopupRounding = 8.f;
            style.FrameRounding = 4.f;
            style.GrabRounding = 4.f;
            style.ScrollbarRounding = 8.f;
            style.TabRounding = 4.f;
            style.WindowBorderSize = 1.f;
            style.DisabledAlpha = 0.38f;
            colors[ImGuiCol_Text] =
                ImVec4(0.94f, 0.95f, 0.98f, 1.f);
            colors[ImGuiCol_TextDisabled] =
                ImVec4(0.58f, 0.59f, 0.61f, 1.f);
            colors[ImGuiCol_WindowBg] =
                ImVec4(0.018f, 0.018f, 0.018f, 0.60f);
            colors[ImGuiCol_ChildBg] =
                ImVec4(0.f, 0.f, 0.f, 0.f);
            colors[ImGuiCol_PopupBg] =
                ImVec4(0.04f, 0.04f, 0.04f, 0.92f);
            colors[ImGuiCol_Border] =
                ImVec4(0.15f, 0.15f, 0.15f, 0.92f);
            colors[ImGuiCol_FrameBg] =
                ImVec4(0.018f, 0.018f, 0.018f, 0.72f);
            colors[ImGuiCol_FrameBgHovered] =
                ImVec4(0.13f, 0.13f, 0.14f, 0.76f);
            colors[ImGuiCol_FrameBgActive] =
                ImVec4(0.18f, 0.18f, 0.19f, 0.82f);
            colors[ImGuiCol_TitleBg] =
                ImVec4(0.035f, 0.035f, 0.035f, 0.82f);
            colors[ImGuiCol_TitleBgActive] =
                ImVec4(0.045f, 0.045f, 0.045f, 0.90f);
            colors[ImGuiCol_TitleBgCollapsed] =
                ImVec4(0.035f, 0.035f, 0.035f, 0.74f);
            colors[ImGuiCol_ScrollbarBg] =
                ImVec4(0.018f, 0.018f, 0.018f, 0.36f);
            colors[ImGuiCol_ScrollbarGrab] =
                ImVec4(0.66f, 0.67f, 0.69f, 0.13f);
            colors[ImGuiCol_ScrollbarGrabHovered] =
                ImVec4(0.74f, 0.75f, 0.77f, 0.20f);
            colors[ImGuiCol_ScrollbarGrabActive] =
                ImVec4(0.80f, 0.81f, 0.83f, 0.26f);
            colors[ImGuiCol_CheckMark] =
                ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
            colors[ImGuiCol_SliderGrab] =
                ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
            colors[ImGuiCol_SliderGrabActive] =
                ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
            colors[ImGuiCol_Button] =
                ImVec4(0.018f, 0.018f, 0.018f, 0.72f);
            colors[ImGuiCol_ButtonHovered] =
                ImVec4(0.13f, 0.13f, 0.14f, 0.76f);
            colors[ImGuiCol_ButtonActive] =
                ImVec4(0.18f, 0.18f, 0.19f, 0.82f);
            colors[ImGuiCol_Header] =
                ImVec4(0.30f, 0.31f, 0.33f, 0.92f);
            colors[ImGuiCol_HeaderHovered] =
                ImVec4(0.38f, 0.39f, 0.41f, 0.97f);
            colors[ImGuiCol_HeaderActive] =
                ImVec4(0.45f, 0.46f, 0.48f, 1.f);
            colors[ImGuiCol_ResizeGrip] =
                ImVec4(0.48f, 0.49f, 0.51f, 0.28f);
            colors[ImGuiCol_ResizeGripHovered] =
                ImVec4(0.60f, 0.61f, 0.63f, 0.62f);
            colors[ImGuiCol_ResizeGripActive] =
                ImVec4(0.75f, 0.76f, 0.78f, 0.90f);
            tokens.drawerHeader =
                ImVec4(0.26f, 0.59f, 0.98f, 0.31f);
            tokens.drawerHeaderHovered =
                ImVec4(0.26f, 0.59f, 0.98f, 0.48f);
            tokens.drawerHeaderActive =
                ImVec4(0.26f, 0.59f, 0.98f, 0.65f);
            tokens.drawerBackground =
                ImVec4(0.66f, 0.67f, 0.69f, 0.13f);
            tokens.drawerFrame = colors[ImGuiCol_FrameBg];
            tokens.drawerFrameHovered =
                colors[ImGuiCol_FrameBgHovered];
            tokens.drawerFrameActive =
                colors[ImGuiCol_FrameBgActive];
            tokens.outlineTop =
                ImVec4(0.88f, 0.90f, 0.94f, 0.10f);
            tokens.outlineBottom =
                ImVec4(0.96f, 0.97f, 1.f, 0.30f);
            tokens.actionButton =
                ImVec4(0.66f, 0.67f, 0.69f, 0.13f);
            tokens.actionButtonHovered =
                ImVec4(0.74f, 0.75f, 0.77f, 0.20f);
            tokens.actionButtonActive =
                ImVec4(0.80f, 0.81f, 0.83f, 0.26f);
        }

        tokens.panelBodySurface =
            colors[ImGuiCol_WindowBg];
        if (resolvedSkin == UiSkin::Amp)
        {
            tokens.panelBodySurface.w =
                colors[ImGuiCol_PopupBg].w;
        }
        // ImGui does not paint WindowBg underneath a title bar. Precomposing
        // the resting drawer blue over the effective Settings body surface
        // makes the standalone title pixels match a resting drawer exactly.
        tokens.settingsTitleSurface = CompositeUiColorOver(
            tokens.drawerHeader,
            tokens.panelBodySurface);

        const UiSkinBehavior behavior =
            GetUiSkinBehavior(resolvedSkin);
        ImGui::SetUvsrUiBehavior(
            behavior.motionEnabled,
            behavior.stockImGuiWidgets);
        const float safeDisplayScale =
            std::clamp(displayScale, 0.5f, 4.f);
        style.ScaleAllSizes(safeDisplayScale);
        if (resolvedSkin == UiSkin::Amp)
        {
            // Preserve the authored Amp rounding and border pixels exactly,
            // matching the renderer's established UI at every DPI scale.
            style.WindowRounding = 8.f;
            style.ChildRounding = 8.f;
            style.PopupRounding = 8.f;
            style.FrameRounding = 4.f;
            style.GrabRounding = 4.f;
            style.ScrollbarRounding = 8.f;
            style.TabRounding = 4.f;
            style.WindowBorderSize = 1.f;
        }
        else
        {
            tokens.drawerRounding *= safeDisplayScale;
        }
        g_UiVisualTokens = tokens;
        ImGui::GetStyle() = style;
    }

    static void PushPanelBodySurface()
    {
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            g_UiVisualTokens.panelBodySurface);
    }
    inline static constexpr float
        UiLayoutAnimationDurationSeconds = 0.18f;

    static float GetUiLayoutAnimationStep()
    {
        const float animationDeltaTime = std::min(
            std::max(0.f, ImGui::GetIO().DeltaTime),
            1.f / 30.f);
        return std::min(
            1.f,
            animationDeltaTime /
                UiLayoutAnimationDurationSeconds);
    }

    static float GetCommandInterfaceMinimumHeight()
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        return std::ceil(
            style.WindowPadding.y * 2.f +
            ImGui::GetFrameHeight());
    }

    static float GetCommandInterfaceReservedHeight()
    {
        return GetCommandInterfaceMinimumHeight();
    }

    static constexpr float SettingsStatusLineSpacing = 2.f;

    static float GetSettingsCollapsedWindowHeight(
        const ImGuiStyle& style,
        float fontSize,
        bool hasPerformanceStatus,
        bool splitOgPerformanceStatus)
    {
        return
            fontSize + style.FramePadding.y * 2.f +
            style.WindowPadding.y +
            fontSize +
            style.ItemSpacing.y +
            1.f +
            (hasPerformanceStatus
                ? SettingsStatusLineSpacing + fontSize
                    + (splitOgPerformanceStatus
                        ? SettingsStatusLineSpacing + fontSize
                        : 0.f)
                : 0.f);
    }

    static float AdvanceUiLayoutAnimation(
        float amount,
        bool targetVisible)
    {
        if (!ImGui::IsUvsrUiMotionEnabled())
            return targetVisible ? 1.f : 0.f;

        const float step = GetUiLayoutAnimationStep();
        return targetVisible
            ? std::min(1.f, amount + step)
            : std::max(0.f, amount - step);
    }

    static float SmoothUiLayoutAnimation(float linearAmount)
    {
        const float amount = std::clamp(linearAmount, 0.f, 1.f);
        return amount * amount * (3.f - 2.f * amount);
    }

    struct SettingsScrollAnchorPosition
    {
        ImGuiID id = 0;
        float contentY = 0.f;
    };

    struct SettingsScrollStabilityContext
    {
        bool active = false;
        bool preserveBottom = false;
        bool layoutAnimatingThisFrame = false;
        bool layoutAnimatingLastFrame = false;
        float scrollY = 0.f;
        float viewportTopScreenY = 0.f;
        float retainedViewportHeight = 0.f;
        float lastScrollY = 0.f;
        ImDrawList* rootDrawList = nullptr;
        int rootDrawVertexStart = 0;
        UiDrawerHeightDeltas drawerHeightDeltas;
        std::vector<SettingsScrollAnchorPosition> previousAnchors;
        std::vector<SettingsScrollAnchorPosition> currentAnchors;
        int lastFrame = -1;
    };

    inline static SettingsScrollStabilityContext
        g_SettingsScrollStabilityContext;

    static void PrepareSettingsScrollStability()
    {
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        if (context.lastFrame < ImGui::GetFrameCount() - 1)
        {
            context.layoutAnimatingLastFrame = false;
            context.retainedViewportHeight = 0.f;
            context.lastScrollY = 0.f;
        }
    }

    static float GetSettingsBodyMinimumHeight(
        float maximumHeight)
    {
        const SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        const bool holdPreviousHeight =
            ShouldRetainUiViewportHeight(
                context.lastScrollY > 0.5f,
                std::abs(ImGui::GetIO().MouseWheel) > 0.001f,
                ImGui::IsMouseDragging(ImGuiMouseButton_Left));
        return holdPreviousHeight
            ? std::clamp(
                context.retainedViewportHeight,
                0.f,
                maximumHeight)
            : 0.f;
    }

    static void MarkSettingsLayoutAnimationActive()
    {
        g_SettingsScrollStabilityContext
            .layoutAnimatingThisFrame = true;
    }

    static void EnsureAnimatedChildLayoutSubmission(
        bool& bodySubmitted)
    {
        if (bodySubmitted)
            return;

        // BeginChild normally skips a fully clipped child. Animated Settings
        // bodies still need their logical layout submitted while offscreen:
        // otherwise TreeNodeEx reports false, nested presentation state closes,
        // cached heights become stale, and returning to the drawer can shift
        // the viewport. Item-level clipping still prevents draw work.
        ImGui::GetCurrentWindow()->SkipItems = false;
        bodySubmitted = true;
    }

    static void BeginSettingsScrollStability()
    {
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        const int frame = ImGui::GetFrameCount();
        if (context.lastFrame < frame - 1)
            context.previousAnchors.clear();

        context.active = true;
        context.scrollY = ImGui::GetScrollY();
        const float scrollMaxY = ImGui::GetScrollMaxY();
        context.preserveBottom =
            scrollMaxY > 0.5f &&
            scrollMaxY - context.scrollY <=
                std::max(1.f, ImGui::GetFrameHeight() * 0.5f);
        context.viewportTopScreenY =
            ImGui::GetCursorScreenPos().y + context.scrollY;
        context.layoutAnimatingThisFrame = false;
        context.drawerHeightDeltas = {};
        context.currentAnchors.clear();
        context.rootDrawList = ImGui::GetWindowDrawList();
        context.rootDrawVertexStart =
            context.rootDrawList
                ? context.rootDrawList->VtxBuffer.Size
                : 0;
        context.lastFrame = frame;
    }

    static void TrackSettingsScrollAnchor(
        ImGuiID id,
        float screenY)
    {
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        if (!context.active || id == 0)
            return;

        const auto duplicate = std::find_if(
            context.currentAnchors.begin(),
            context.currentAnchors.end(),
            [id](const SettingsScrollAnchorPosition& anchor)
            {
                return anchor.id == id;
            });
        if (duplicate != context.currentAnchors.end())
            return;

        context.currentAnchors.push_back({
            id,
            screenY - context.viewportTopScreenY +
                context.scrollY
        });
    }

    static void TrackSettingsDrawerHeight(
        ImGuiStorage* storage,
        ImGuiID headerId,
        float bodyTop,
        float displayedHeight)
    {
        if (!storage || headerId == 0)
            return;

        const ImGuiID displayedHeightKey =
            headerId ^ ImGuiID(0x786A4D21u);
        const float previousDisplayedHeight =
            storage->GetFloat(
                displayedHeightKey,
                displayedHeight);
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        if (context.active)
        {
            context.drawerHeightDeltas =
                AccumulateUiDrawerHeightDelta(
                    context.drawerHeightDeltas,
                    bodyTop,
                    previousDisplayedHeight,
                    displayedHeight,
                    context.viewportTopScreenY);
        }
        storage->SetFloat(
            displayedHeightKey,
            displayedHeight);
    }

    static void EndSettingsScrollStability()
    {
        SettingsScrollStabilityContext& context =
            g_SettingsScrollStabilityContext;
        if (!context.active)
            return;

        bool foundStableAnchor = false;
        float scrollDelta = 0.f;
        for (const SettingsScrollAnchorPosition& previous :
            context.previousAnchors)
        {
            if (previous.contentY < context.scrollY - 0.5f)
                continue;

            const auto current = std::find_if(
                context.currentAnchors.begin(),
                context.currentAnchors.end(),
                [&](const SettingsScrollAnchorPosition& anchor)
                {
                    return anchor.id == previous.id;
                });
            if (current == context.currentAnchors.end())
                continue;

            scrollDelta = current->contentY - previous.contentY;
            foundStableAnchor = true;
            break;
        }

        if (!foundStableAnchor)
        {
            scrollDelta = ResolveUiScrollAnchorDelta(
                context.drawerHeightDeltas,
                context.preserveBottom);
        }

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const float currentFrameContentHeight =
            window->ContentSizeExplicit.y != 0.f
                ? window->ContentSizeExplicit.y
                : std::trunc(
                    window->DC.CursorMaxPos.y -
                    window->DC.CursorStartPos.y);
        const float currentFrameScrollMaxY = std::max(
            0.f,
            currentFrameContentHeight +
                window->WindowPadding.y * 2.f -
                window->InnerRect.GetHeight());
        const UiScrollAnchorCorrection correction =
            ResolveUiScrollAnchorCorrection(
                window->Scroll.y,
                scrollDelta,
                currentFrameScrollMaxY,
                window->ScrollTarget.y < FLT_MAX);
        if (correction.apply)
        {
            const float visualScrollDelta =
                correction.scrollY - window->Scroll.y;
            window->Scroll.y = correction.scrollY;
            if (std::abs(visualScrollDelta) > 0.01f)
            {
                for (ImDrawList* drawList :
                    g_SettingsAppearanceDrawLists)
                {
                    if (!drawList)
                        continue;
                    const int vertexStart =
                        drawList == context.rootDrawList
                            ? std::clamp(
                                context.rootDrawVertexStart,
                                0,
                                drawList->VtxBuffer.Size)
                            : 0;
                    for (int vertexIndex = vertexStart;
                        vertexIndex < drawList->VtxBuffer.Size;
                        ++vertexIndex)
                    {
                        drawList->VtxBuffer[vertexIndex].pos.y -=
                            visualScrollDelta;
                    }

                    if (drawList == context.rootDrawList)
                        continue;
                    for (ImDrawCmd& command :
                        drawList->CmdBuffer)
                    {
                        command.ClipRect.y = std::max(
                            window->InnerClipRect.Min.y,
                            command.ClipRect.y -
                                visualScrollDelta);
                        command.ClipRect.w = std::min(
                            window->InnerClipRect.Max.y,
                            command.ClipRect.w -
                                visualScrollDelta);
                        command.ClipRect.w = std::max(
                            command.ClipRect.y,
                            command.ClipRect.w);
                    }
                }
            }
        }

        context.previousAnchors =
            std::move(context.currentAnchors);
        context.currentAnchors.clear();
        context.retainedViewportHeight =
            ImGui::GetWindowSize().y;
        context.lastScrollY = ImGui::GetScrollY();
        context.layoutAnimatingLastFrame =
            context.layoutAnimatingThisFrame;
        context.active = false;
    }

    struct DrawerAnimationContext
    {
        ImGuiStorage* storage = nullptr;
        ImGuiID headerId = 0;
        float openAmount = 0.f;
        bool targetOpen = false;
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
        const ImGuiID measurementValidKey =
            headerId ^ ImGuiID(0x82E4C76Bu);
        ImGui::PushStyleColor(
            ImGuiCol_Header,
            g_UiVisualTokens.drawerHeader);
        ImGui::PushStyleColor(
            ImGuiCol_HeaderHovered,
            g_UiVisualTokens.drawerHeaderHovered);
        ImGui::PushStyleColor(
            ImGuiCol_HeaderActive,
            g_UiVisualTokens.drawerHeaderActive);
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
        TrackSettingsScrollAnchor(
            headerId,
            ImGui::GetItemRectMin().y);

        const int frame = ImGui::GetFrameCount();
        const int lastFrame = storage->GetInt(frameKey, -2);
        float openAmount = storage->GetFloat(
            amountKey,
            open ? 1.f : 0.f);
        const UiExpandedMeasurementState measurement = {
            storage->GetFloat(measuredHeightKey, 0.f),
            storage->GetBool(measurementValidKey, false)
        };
        const float measuredHeight = measurement.height;
        const bool needsInitialMeasurement =
            ImGui::IsUvsrUiMotionEnabled() &&
            NeedsInitialUiExpandedMeasurement(open, measurement);
        if (lastFrame < frame - 1)
        {
            openAmount = needsInitialMeasurement
                ? 0.f
                : open ? 1.f : 0.f;
        }
        else if (needsInitialMeasurement)
        {
            // Submit one alpha-zero layout pass before visible progress. This
            // gives every drawer a real expanded height instead of animating
            // from a one-row proxy.
            openAmount = 0.f;
        }
        else
        {
            openAmount =
                AdvanceUiLayoutAnimation(openAmount, open);
        }
        storage->SetFloat(amountKey, openAmount);
        storage->SetInt(frameKey, frame);
        if (needsInitialMeasurement ||
            (openAmount > 0.f && openAmount < 1.f))
        {
            MarkSettingsLayoutAnimationActive();
        }
        g_DrawerAnimationContext = {
            storage,
            headerId,
            openAmount,
            open,
            needsInitialMeasurement,
            false
        };
        const bool drawBody = open || openAmount > 0.f;
        if (!drawBody)
        {
            TrackSettingsDrawerHeight(
                storage,
                headerId,
                ImGui::GetItemRectMax().y,
                0.f);
        }
        return drawBody;
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
        const bool motionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        const float easedAmount = motionEnabled
            ? SmoothUiLayoutAnimation(
                g_DrawerAnimationContext.openAmount)
            : g_DrawerAnimationContext.targetOpen ? 1.f : 0.f;
        const float animatedHeight =
            !motionEnabled
                ? 0.f
                : g_DrawerAnimationContext.needsInitialMeasurement
                ? 0.001f
                : std::max(
                    measuredHeight * easedAmount,
                    0.001f);
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            g_UiVisualTokens.drawerBackground);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            g_UiVisualTokens.drawerFrame);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            g_UiVisualTokens.drawerFrameHovered);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgActive,
            g_UiVisualTokens.drawerFrameActive);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(style.FramePadding.x, style.ItemSpacing.y));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            g_UiVisualTokens.drawerRounding);
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            style.Alpha *
                (g_DrawerAnimationContext.needsInitialMeasurement
                    ? 0.f
                    : easedAmount));
        ImGuiChildFlags childFlags =
            ImGuiChildFlags_AlwaysUseWindowPadding |
            ImGuiChildFlags_AllowZeroSize;
        if (!motionEnabled)
        {
            childFlags |=
                ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysAutoResize;
        }
        ImGuiWindowFlags childWindowFlags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (motionEnabled &&
            (g_DrawerAnimationContext.needsInitialMeasurement ||
            !g_DrawerAnimationContext.targetOpen ||
            g_DrawerAnimationContext.openAmount < 1.f))
        {
            childWindowFlags |= ImGuiWindowFlags_NoInputs;
        }
        g_DrawerAnimationContext.bodyVisible =
            ImGui::BeginChild(
            id,
            ImVec2(0.f, animatedHeight),
            childFlags,
            childWindowFlags);
        EnsureAnimatedChildLayoutSubmission(
            g_DrawerAnimationContext.bodyVisible);
        TrackSettingsAppearanceDrawList(
            ImGui::GetWindowDrawList());
        ImGui::PushItemWidth(controlWidth);
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

    static void DrawDrawerBodyOutline(
        const ImVec2& minimum,
        const ImVec2& maximum,
        float rounding)
    {
        if (!g_UiVisualTokens.drawControlOutlines)
            return;

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
            vertex.col = ImGui::GetColorU32(LerpUiColor(
                g_UiVisualTokens.outlineTop,
                g_UiVisualTokens.outlineBottom,
                gradientPosition));
        }
    }

    static void DrawSettingsScrollEdgeFades()
    {
        if (!g_UiVisualTokens.drawScrollEdgeFades)
            return;

        const float scrollY = ImGui::GetScrollY();
        const float scrollMaxY = ImGui::GetScrollMaxY();
        if (scrollMaxY <= 0.5f)
            return;

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 windowMinimum = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 windowMaximum(
            windowMinimum.x +
                std::max(0.f, windowSize.x - style.ScrollbarSize),
            windowMinimum.y + windowSize.y);
        const float fadeHeight = std::min(
            ImGui::GetFrameHeight() * 1.15f,
            windowSize.y * 0.18f);
        if (fadeHeight <= 0.5f ||
            windowMaximum.x <= windowMinimum.x)
        {
            return;
        }

        ImVec4 edgeColor = style.Colors[ImGuiCol_WindowBg];
        edgeColor.w = std::max(edgeColor.w, 0.82f);
        ImVec4 clearColor = edgeColor;
        clearColor.w = 0.f;
        const ImU32 clear = ImGui::GetColorU32(clearColor);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const auto edgeForDistance =
            [&](float distance)
            {
                ImVec4 color = edgeColor;
                color.w *= std::clamp(
                    distance / std::max(fadeHeight, 1.f),
                    0.f,
                    1.f);
                return ImGui::GetColorU32(color);
            };

        if (scrollY > 0.5f)
        {
            const ImU32 edge = edgeForDistance(scrollY);
            drawList->AddRectFilledMultiColor(
                windowMinimum,
                ImVec2(
                    windowMaximum.x,
                    windowMinimum.y + fadeHeight),
                edge,
                edge,
                clear,
                clear);
        }
        const float remainingScroll =
            std::max(0.f, scrollMaxY - scrollY);
        if (remainingScroll > 0.5f)
        {
            const ImU32 edge =
                edgeForDistance(remainingScroll);
            drawList->AddRectFilledMultiColor(
                ImVec2(
                    windowMinimum.x,
                    windowMaximum.y - fadeHeight),
                windowMaximum,
                clear,
                clear,
                edge,
                edge);
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
            const ImGuiID measurementValidKey =
                g_DrawerAnimationContext.headerId ^
                ImGuiID(0x82E4C76Bu);
            UiExpandedMeasurementState measurement = {
                g_DrawerAnimationContext.storage->GetFloat(
                    measuredHeightKey, 0.f),
                g_DrawerAnimationContext.storage->GetBool(
                    measurementValidKey, false)
            };
            const float renderedHeight =
                ImGui::GetItemRectSize().y;
            measurement = SubmitUiExpandedMeasurement(
                measurement,
                measuredHeight,
                g_DrawerAnimationContext.targetOpen,
                g_DrawerAnimationContext.bodyVisible);
            g_DrawerAnimationContext.storage->SetFloat(
                measuredHeightKey,
                measurement.height);
            g_DrawerAnimationContext.storage->SetBool(
                measurementValidKey,
                measurement.valid);
            TrackSettingsDrawerHeight(
                g_DrawerAnimationContext.storage,
                g_DrawerAnimationContext.headerId,
                ImGui::GetItemRectMin().y,
                renderedHeight);
        }
        DrawDrawerBodyOutline(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::GetStyle().ChildRounding);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);
    }

    struct NestedDrawerAnimationContext
    {
        ImGuiStorage* storage = nullptr;
        ImGuiWindow* bodyWindow = nullptr;
        ImGuiID measuredHeightKey = 0;
        ImGuiID measurementValidKey = 0;
        float indentSpacing = 0.f;
        bool targetOpen = false;
        bool bodyVisible = false;
    };

    inline static std::vector<NestedDrawerAnimationContext>
        g_NestedDrawerAnimationContexts;

    static bool BeginAnimatedTreeNode(
        const char* label,
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None,
        const char* tooltip = nullptr)
    {
        // Item-width stacks belong to an ImGui window. Preserve the drawer's
        // standard control width before entering this animated child so
        // sliders and dropdowns retain identical tracks at every nesting
        // level.
        const float inheritedItemWidth = ImGui::CalcItemWidth();
        const ImGuiID headerId = ImGui::GetID(label);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID amountKey =
            headerId ^ ImGuiID(0x5CB870A3u);
        const ImGuiID frameKey =
            headerId ^ ImGuiID(0x34A1F27Du);
        const ImGuiID measuredHeightKey =
            headerId ^ ImGuiID(0x9D63E418u);
        const ImGuiID measurementValidKey =
            headerId ^ ImGuiID(0xC1A7095Fu);
        const bool open = ImGui::TreeNodeEx(
            label,
            flags | ImGuiTreeNodeFlags_NoTreePushOnOpen);
        if (tooltip != nullptr)
            ImGui::SetItemTooltip("%s", tooltip);
        TrackSettingsScrollAnchor(
            headerId,
            ImGui::GetItemRectMin().y);

        const int frame = ImGui::GetFrameCount();
        const int lastFrame = storage->GetInt(frameKey, -2);
        const UiExpandedMeasurementState measurement = {
            storage->GetFloat(measuredHeightKey, 0.f),
            storage->GetBool(measurementValidKey, false)
        };
        const float measuredHeight = measurement.height;
        const bool motionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        const bool needsInitialMeasurement =
            motionEnabled &&
            NeedsInitialUiExpandedMeasurement(open, measurement);
        float openAmount = storage->GetFloat(
            amountKey,
            open ? 1.f : 0.f);
        if (lastFrame < frame - 1)
        {
            openAmount = needsInitialMeasurement
                ? 0.f
                : open ? 1.f : 0.f;
        }
        else if (needsInitialMeasurement)
        {
            openAmount = 0.f;
        }
        else
        {
            openAmount =
                AdvanceUiLayoutAnimation(openAmount, open);
        }
        storage->SetFloat(amountKey, openAmount);
        storage->SetInt(frameKey, frame);
        if (needsInitialMeasurement ||
            (openAmount > 0.f && openAmount < 1.f))
        {
            MarkSettingsLayoutAnimationActive();
        }

        if (!open && openAmount <= 0.f)
            return false;

        const float easedAmount =
            SmoothUiLayoutAnimation(openAmount);
        const float animatedHeight =
            !motionEnabled
                ? 0.f
                : needsInitialMeasurement
                ? 0.001f
                : std::max(
                    measuredHeight * easedAmount,
                    0.001f);

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            0.f);
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha *
                (needsInitialMeasurement
                    ? 0.f
                    : easedAmount));
        ImGuiWindowFlags childWindowFlags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (motionEnabled &&
            (needsInitialMeasurement ||
            !open ||
            openAmount < 1.f))
        {
            childWindowFlags |= ImGuiWindowFlags_NoInputs;
        }
        ImGuiChildFlags childFlags =
            ImGuiChildFlags_AllowZeroSize;
        if (!motionEnabled)
        {
            childFlags |=
                ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysAutoResize;
        }
        bool bodyVisible = ImGui::BeginChild(
            headerId ^ ImGuiID(0xE60792B5u),
            ImVec2(0.f, animatedHeight),
            childFlags,
            childWindowFlags);
        EnsureAnimatedChildLayoutSubmission(bodyVisible);
        TrackSettingsAppearanceDrawList(
            ImGui::GetWindowDrawList());
        // Own the transparent indentation gutter inside the animated child so
        // nested-dropdown reset buttons can draw and receive input there. The
        // child starts one indent earlier, while this internal indent preserves
        // every existing control's absolute position and right edge.
        const float indentSpacing = ImGui::GetStyle().IndentSpacing;
        ImGui::Indent(indentSpacing);
        ImGui::PushItemWidth(inheritedItemWidth);
        g_NestedDrawerAnimationContexts.push_back({
            storage,
            ImGui::GetCurrentWindow(),
            measuredHeightKey,
            measurementValidKey,
            indentSpacing,
            open,
            bodyVisible
        });
        return true;
    }

    static void EndAnimatedTreeNode()
    {
        assert(!g_NestedDrawerAnimationContexts.empty());
        const NestedDrawerAnimationContext context =
            g_NestedDrawerAnimationContexts.back();
        g_NestedDrawerAnimationContexts.pop_back();
        const float measuredHeight =
            std::max(0.f, ImGui::GetCursorPosY());
        ImGuiStyle& style = ImGui::GetStyle();
        const float itemSpacingY = style.ItemSpacing.y;
        style.ItemSpacing.y = 0.f;
        assert(ImGui::GetCurrentWindow() == context.bodyWindow);
        ImGui::PopItemWidth();
        ImGui::Unindent(context.indentSpacing);
        ImGui::EndChild();
        style.ItemSpacing.y = itemSpacingY;

        if (context.storage != nullptr)
        {
            UiExpandedMeasurementState measurement = {
                context.storage->GetFloat(
                    context.measuredHeightKey, 0.f),
                context.storage->GetBool(
                    context.measurementValidKey, false)
            };
            measurement = SubmitUiExpandedMeasurement(
                measurement,
                measuredHeight,
                context.targetOpen,
                context.bodyVisible);
            context.storage->SetFloat(
                context.measuredHeightKey,
                measurement.height);
            context.storage->SetBool(
                context.measurementValidKey,
                measurement.valid);
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
    }

    struct UiToggleRegionAnimationState
    {
        float linearAmount = 0.f;
        UiExpandedMeasurementState measurement;
        bool targetVisible = false;
        bool initialized = false;
        int lastSeenFrame = -1;
        int transitionFrame = -1;
        int advancedFrame = -1;
    };

    struct UiToggleRegionAnimationContext
    {
        ImGuiID id = 0;
        bool bodyVisible = false;
        bool freezeVisualValues = false;
    };

    inline static std::unordered_map<
        ImGuiID,
        UiToggleRegionAnimationState>
        g_UiToggleRegionAnimationStates;
    inline static std::vector<UiToggleRegionAnimationContext>
        g_UiToggleRegionAnimationContexts;

    static bool FreezeAnimatedToggleVisualValues()
    {
        return std::any_of(
            g_UiToggleRegionAnimationContexts.begin(),
            g_UiToggleRegionAnimationContexts.end(),
            [](const UiToggleRegionAnimationContext& context)
            {
                return context.freezeVisualValues;
            });
    }

    static bool BeginAnimatedToggleRegion(
        const char* id,
        bool visible)
    {
        // BeginChild starts a fresh item-width stack. Carry the enclosing
        // drawer width into toggle regions instead of letting ImGui choose its
        // wider default slider width.
        const float inheritedItemWidth = ImGui::CalcItemWidth();
        const ImGuiID regionId = ImGui::GetID(id);
        UiToggleRegionAnimationState& state =
            g_UiToggleRegionAnimationStates[regionId];
        const int frame = ImGui::GetFrameCount();
        const bool submissionWasInterrupted =
            state.lastSeenFrame >= 0 &&
            state.lastSeenFrame < frame - 2;
        bool targetChangedThisFrame = false;

        if (!state.initialized || submissionWasInterrupted)
        {
            state.linearAmount = visible ? 1.f : 0.f;
            state.targetVisible = visible;
            state.initialized = true;
            state.transitionFrame = frame;
        }
        else if (state.targetVisible != visible)
        {
            // UpdateUI runs after the scene submission. Hold the old endpoint
            // for the frame in which the toggle changed; animation begins on
            // the next UI frame, after the renderer has consumed the setting.
            state.targetVisible = visible;
            state.transitionFrame = frame;
            targetChangedThisFrame = true;
        }

        const bool motionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        if (!motionEnabled)
        {
            state.targetVisible = visible;
            state.linearAmount = visible ? 1.f : 0.f;
            state.transitionFrame = frame;
            state.advancedFrame = frame;
            targetChangedThisFrame = false;
        }

        const bool needsInitialMeasurement =
            motionEnabled &&
            NeedsInitialUiExpandedMeasurement(
                state.targetVisible,
                state.measurement);
        if (needsInitialMeasurement)
        {
            // Keep this first layout pass invisible and at zero progress. The
            // following frame starts from the complete measured height.
            state.linearAmount = 0.f;
            state.transitionFrame = frame;
        }
        else if (frame > state.transitionFrame &&
            state.advancedFrame != frame)
        {
            state.linearAmount = AdvanceUiLayoutAnimation(
                state.linearAmount,
                state.targetVisible);
            state.advancedFrame = frame;
        }

        state.lastSeenFrame = frame;
        if (targetChangedThisFrame ||
            needsInitialMeasurement ||
            (state.linearAmount > 0.f &&
                state.linearAmount < 1.f))
        {
            MarkSettingsLayoutAnimationActive();
        }
        TrackSettingsScrollAnchor(
            regionId,
            ImGui::GetCursorScreenPos().y);
        if (!state.targetVisible && state.linearAmount <= 0.f)
            return false;

        const float easedAmount =
            SmoothUiLayoutAnimation(state.linearAmount);
        const float animatedHeight =
            !motionEnabled
                ? 0.f
                : needsInitialMeasurement
                ? 0.001f
                : std::max(
                    state.measurement.height * easedAmount,
                    0.001f);

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ChildRounding,
            0.f);
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha *
                (needsInitialMeasurement
                    ? 0.f
                    : easedAmount));
        ImGui::PushStyleVar(
            ImGuiStyleVar_DisabledAlpha,
            1.f);
        ImGuiWindowFlags childWindowFlags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (motionEnabled &&
            (needsInitialMeasurement ||
            !state.targetVisible ||
            state.linearAmount < 1.f))
        {
            childWindowFlags |=
                ImGuiWindowFlags_NoNavInputs |
                ImGuiWindowFlags_NoNavFocus;
        }
        ImGuiChildFlags childFlags =
            ImGuiChildFlags_AllowZeroSize;
        if (!motionEnabled)
        {
            childFlags |=
                ImGuiChildFlags_AutoResizeY |
                ImGuiChildFlags_AlwaysAutoResize;
        }
        bool bodyVisible = ImGui::BeginChild(
            regionId ^ ImGuiID(0x6C3E91B7u),
            ImVec2(0.f, animatedHeight),
            childFlags,
            childWindowFlags);
        EnsureAnimatedChildLayoutSubmission(bodyVisible);
        TrackSettingsAppearanceDrawList(
            ImGui::GetWindowDrawList());
        ImGui::PushItemWidth(inheritedItemWidth);

        // Interaction is blocked during both directions, but DisabledAlpha is
        // one so controls never take on the old gray gated appearance.
        ImGui::BeginDisabled(
            !state.targetVisible || state.linearAmount < 1.f);
        g_UiToggleRegionAnimationContexts.push_back({
            regionId,
            bodyVisible,
            !state.targetVisible
        });
        return true;
    }

    static void EndAnimatedToggleRegion()
    {
        assert(!g_UiToggleRegionAnimationContexts.empty());
        const UiToggleRegionAnimationContext context =
            g_UiToggleRegionAnimationContexts.back();
        g_UiToggleRegionAnimationContexts.pop_back();
        const float measuredHeight =
            std::max(0.f, ImGui::GetCursorPosY());

        ImGui::EndDisabled();
        ImGui::PopItemWidth();
        ImGui::EndChild();

        const auto stateIterator =
            g_UiToggleRegionAnimationStates.find(context.id);
        if (stateIterator != g_UiToggleRegionAnimationStates.end() &&
            context.bodyVisible)
        {
            UiToggleRegionAnimationState& state =
                stateIterator->second;
            state.measurement = SubmitUiExpandedMeasurement(
                state.measurement,
                measuredHeight,
                state.targetVisible,
                context.bodyVisible);
            // A legitimate empty child measures zero. Keep measurement state
            // separate from the numeric result so empty method-specific
            // layouts complete instead of re-entering the hidden measurement
            // pass forever and blocking deferred dropdown commits.
        }

        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor();
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
        if (!ImGui::IsUvsrUiMotionEnabled())
        {
            storage->SetFloat(amountKey, target);
            storage->SetInt(frameKey, ImGui::GetFrameCount());
            return target;
        }

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

    enum class SettingsResetIconPlacement
    {
        Trailing,
        NestedDropdownGutter
    };

    static void SetNextLabeledControlWidth(
        const char* label,
        float preferredWidth)
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        const char* visibleLabelEnd =
            ImGui::FindRenderedTextEnd(label);
        const float visibleLabelWidth = visibleLabelEnd == label
            ? 0.f
            : ImGui::CalcTextSize(label, visibleLabelEnd).x +
                style.ItemInnerSpacing.x;
        const float resetLaneWidth =
            ImGui::GetFrameHeight() * 0.78f +
            style.ItemInnerSpacing.x;
        const float minimumControlWidth =
            ImGui::GetFrameHeight() * 3.f;
        const float maximumControlWidth = std::max(
            minimumControlWidth,
            ImGui::GetContentRegionAvail().x -
                visibleLabelWidth - resetLaneWidth);
        ImGui::SetNextItemWidth(std::min(
            preferredWidth,
            maximumControlWidth));
    }

    static bool DrawPresetResetIconAtPlacement(
        const char* id,
        bool modified,
        const char* tooltip,
        SettingsResetIconPlacement placement)
    {
        ImGui::PushID(id);
        const ImGuiID resetId = ImGui::GetID("##PresetReset");
        const float visibility =
            GetUiHighlightFade(resetId, modified, 18.f);
        const ImGuiStyle& style = ImGui::GetStyle();
        const float buttonSize = ImGui::GetFrameHeight() * 0.78f;

        const bool nestedDropdownGutterRequested =
            placement == SettingsResetIconPlacement::NestedDropdownGutter;
        const bool nestedDropdownGutterAvailable =
            nestedDropdownGutterRequested &&
            !g_NestedDrawerAnimationContexts.empty() &&
            ImGui::GetCurrentWindow() ==
                g_NestedDrawerAnimationContexts.back().bodyWindow;
        if (nestedDropdownGutterRequested)
        {
            assert(ShouldPlaceUiResetInNestedDropdownGutter(
                true,
                g_NestedDrawerAnimationContexts.size()));
            assert(nestedDropdownGutterAvailable);
        }
        if (nestedDropdownGutterAvailable)
        {
            const NestedDrawerAnimationContext& context =
                g_NestedDrawerAnimationContexts.back();
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            const float resetButtonScreenX =
                ImGui::GetCursorScreenPos().x +
                ResolveNestedDropdownResetOffset(
                    context.indentSpacing,
                    buttonSize);
            const float sameLineOffset =
                resetButtonScreenX - window->Pos.x + window->Scroll.x -
                window->DC.GroupOffset.x - window->DC.ColumnsOffset.x;
            ImGui::SameLine(sameLineOffset, 0.f);
        }
        else
        {
            // Keep the established trailing lane unchanged for un-nested
            // dropdowns and every non-dropdown control.
            ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
            const float rightAlignedX =
                ImGui::GetContentRegionMax().x - buttonSize;
            if (ImGui::GetCursorPosX() < rightAlignedX)
                ImGui::SetCursorPosX(rightAlignedX);
        }

        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            style.Alpha * visibility);
        ImGui::BeginDisabled(!modified || visibility < 0.98f);
        // Route through the native button frame so the reset control receives
        // the same Amp gradient outline and interaction surface as every other
        // framed menu element.
        const bool pressed = ImGui::Button(
            "##PresetReset",
            ImVec2(buttonSize, buttonSize));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        const ImVec2 center(
            (minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f);
        constexpr float Pi = 3.14159265358979323846f;
        const float radius = buttonSize * 0.24f;
        const ImU32 iconColor = ImGui::GetColorU32(ImGuiCol_Text);
        drawList->PathClear();
        drawList->PathArcTo(
            center,
            radius,
            Pi * 0.12f,
            Pi * 1.72f,
            14);
        drawList->PathStroke(iconColor, false, 1.5f);
        const ImVec2 arrowTip(
            center.x + radius * std::cos(Pi * 0.12f),
            center.y + radius * std::sin(Pi * 0.12f));
        drawList->AddTriangleFilled(
            ImVec2(
                arrowTip.x + buttonSize * 0.01f,
                arrowTip.y - buttonSize * 0.16f),
            ImVec2(
                arrowTip.x + buttonSize * 0.16f,
                arrowTip.y + buttonSize * 0.01f),
            ImVec2(
                arrowTip.x - buttonSize * 0.05f,
                arrowTip.y + buttonSize * 0.04f),
            iconColor);
        if (modified)
            ImGui::SetItemTooltip("%s", tooltip);
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        ImGui::PopID();
        return pressed && modified;
    }

    static bool DrawPresetResetIcon(
        const char* id,
        bool modified,
        const char* tooltip = "Reset this setting to its default value.")
    {
        return DrawPresetResetIconAtPlacement(
            id,
            modified,
            tooltip,
            SettingsResetIconPlacement::Trailing);
    }

    static bool DrawNestedDropdownResetIcon(
        const char* id,
        bool modified,
        const char* tooltip = "Reset this setting to its default value.")
    {
        return DrawPresetResetIconAtPlacement(
            id,
            modified,
            tooltip,
            SettingsResetIconPlacement::NestedDropdownGutter);
    }

    struct DeferredDropdownUiPayload
    {
        std::string previewValue;
        std::function<void()> apply;
    };

    struct DeferredDropdownUiState
    {
        DeferredUiActionQueue<ImGuiID, DeferredDropdownUiPayload> actions;
        ImGuiID transitionComboId = 0;
        int transitionComboLastSubmittedFrame = -1;
        double lastRequestTime = 0.0;
        int requestFrame = -1;
        int idleStartFrame = -1;
    };

    inline static DeferredDropdownUiState
        g_DeferredDropdownUiState;
    inline static ImGuiID g_ActiveRoundedComboId = 0;

    static bool HasDeferredDropdownUiActions()
    {
        return !g_DeferredDropdownUiState.actions.Empty();
    }

    static void CancelDeferredDropdownUiActions()
    {
        ImGui::FinishComboPopupTransition(
            g_DeferredDropdownUiState.transitionComboId);
        g_DeferredDropdownUiState = {};
    }

    static bool IsDeferredDropdownPopupTransitionActive()
    {
        return ImGui::IsComboPopupTransitionActive(
            g_DeferredDropdownUiState.transitionComboId);
    }

    static void FinishDeferredDropdownPopupTransition()
    {
        ImGui::FinishComboPopupTransition(
            g_DeferredDropdownUiState.transitionComboId);
    }

    static void FinishUnsubmittedDeferredDropdownPopupTransition()
    {
        const DeferredDropdownUiState& state =
            g_DeferredDropdownUiState;
        if (state.actions.Empty() ||
            state.transitionComboId == 0 ||
            state.transitionComboLastSubmittedFrame ==
                ImGui::GetFrameCount())
        {
            return;
        }

        // A clipped row or collapsed drawer cannot submit the popup frame
        // which advances its retained roll-up. Close only that originating
        // combo so its deferred action cannot remain stranded indefinitely.
        ImGui::FinishComboPopupTransition(state.transitionComboId);
    }

    static const char* GetDeferredDropdownPreview(ImGuiID comboId)
    {
        const DeferredDropdownUiPayload* action =
            g_DeferredDropdownUiState.actions.Find(comboId);
        return action && !action->previewValue.empty()
            ? action->previewValue.c_str()
            : nullptr;
    }

    static void QueueDeferredUiAction(
        ImGuiID controlId,
        ImGuiID transitionComboId,
        const char* previewValue,
        std::function<void()> action)
    {
        assert(controlId != 0);
        DeferredDropdownUiState& state =
            g_DeferredDropdownUiState;
        state.actions.Upsert(
            controlId,
            DeferredDropdownUiPayload{
                previewValue ? previewValue : "",
                std::move(action)
            });
        state.transitionComboId = transitionComboId;
        state.transitionComboLastSubmittedFrame =
            transitionComboId != 0
                ? ImGui::GetFrameCount()
                : -1;
        state.lastRequestTime = ImGui::GetTime();
        state.requestFrame = ImGui::GetFrameCount();
        state.idleStartFrame = -1;
    }

    static void QueueDeferredControlUiAction(
        std::function<void()> action)
    {
        QueueDeferredUiAction(
            ImGui::GetItemID(),
            0,
            nullptr,
            std::move(action));
    }

    static void QueueDeferredDropdownUiAction(
        const char* previewValue,
        std::function<void()> action)
    {
        QueueDeferredUiAction(
            g_ActiveRoundedComboId,
            g_ActiveRoundedComboId,
            previewValue,
            std::move(action));
    }

    static bool TryApplyDeferredDropdownUiActions(
        bool compositionIdle,
        bool immediate = false)
    {
        DeferredDropdownUiState& state =
            g_DeferredDropdownUiState;
        if (state.actions.Empty())
            return false;

        if (!immediate)
        {
            const int frame = ImGui::GetFrameCount();
            state.idleStartFrame = UpdateUiDropdownIdleStartFrame(
                state.idleStartFrame,
                frame,
                compositionIdle);
            if (!ShouldCommitDeferredDropdownActions(
                    frame,
                    state.requestFrame,
                    state.idleStartFrame,
                    ImGui::GetTime() - state.lastRequestTime))
            {
                return false;
            }
        }
        else
        {
            FinishDeferredDropdownPopupTransition();
        }

        DeferredUiActionQueue<ImGuiID, DeferredDropdownUiPayload> actions =
            std::move(state.actions);
        state = {};
        return actions.Drain(
            [](ImGuiID, DeferredDropdownUiPayload action)
            {
                if (action.apply)
                    action.apply();
            });
    }

    static bool BeginRoundedCombo(
        const char* label,
        const char* previewValue,
        ImGuiComboFlags flags = ImGuiComboFlags_None)
    {
        const ImGuiID comboId = ImGui::GetID(label);
        const char* deferredPreview =
            GetDeferredDropdownPreview(comboId);
        const char* visiblePreview =
            deferredPreview ? deferredPreview : previewValue;
        const bool open =
            ImGui::BeginCombo(label, visiblePreview, flags);
        DeferredDropdownUiState& deferredState =
            g_DeferredDropdownUiState;
        if (open && deferredState.transitionComboId == comboId)
        {
            deferredState.transitionComboLastSubmittedFrame =
                ImGui::GetFrameCount();
        }
        g_ActiveRoundedComboId = open ? comboId : 0;
        return open;
    }

    template<typename Action>
    static bool DrawDeferredDropdownOption(
        const char* label,
        const char* previewValue,
        bool selected,
        Action action)
    {
        const bool activated = ImGui::Selectable(label, selected);
        // A selected row already describes the visible choice. Re-running its
        // callback can normalize unrelated hidden fields or rebuild resources
        // without changing that choice, so redundant activation is a no-op.
        if (!activated || selected)
            return false;

        QueueDeferredDropdownUiAction(
            previewValue,
            std::function<void()>(std::move(action)));
        return true;
    }

    static void ApplyWordSpacing(
        ImGuiID& adjustedFontBakedId,
        float& baseSpaceAdvance,
        bool expanded)
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
        const float spaceAdvance = expanded
            ? baseSpaceAdvance * WordSpaceScale
            : baseSpaceAdvance;
        spaceGlyph->AdvanceX = spaceAdvance;
        if (baked->IndexAdvanceX.Size > int(ImWchar(' ')))
        {
            baked->IndexAdvanceX[int(ImWchar(' '))] =
                spaceAdvance;
        }
    }

    ImFont* GetActiveUiFont()
    {
        if (ImGui::IsUvsrStockWidgetRenderingEnabled())
        {
            const std::shared_ptr<app::RegisteredFont> defaultFont =
                GetDefaultFont();
            if (defaultFont && defaultFont->GetScaledFont())
                return defaultFont->GetScaledFont();
        }
        return m_Font && m_Font->GetScaledFont()
            ? m_Font->GetScaledFont()
            : ImGui::GetFont();
    }

    void ApplyActiveUiWordSpacing()
    {
        ApplyWordSpacing(
            m_AdjustedSpaceFontBakedId,
            m_BaseSpaceAdvance,
            GetUiSkinBehavior(m_ComposedUiSkin).expandedWordSpacing);
    }

    void RestoreActiveUiWordSpacing()
    {
        ApplyWordSpacing(
            m_AdjustedSpaceFontBakedId,
            m_BaseSpaceAdvance,
            false);
    }

    enum class CommandValueOperation
    {
        Get,
        Set,
        Toggle,
        Reset
    };

    struct UiSettingsCommandBinding
    {
        std::string_view name;
        UiSettingsCommandSection section;
        UiSettingsCommandKind kind;
    };

    inline static constexpr auto UiSettingsCommandBindings = []()
    {
        std::array<
            UiSettingsCommandBinding,
            UiSettingsCommandCatalog.size()> bindings{};
        for (size_t index = 0u;
            index < UiSettingsCommandCatalog.size();
            ++index)
        {
            bindings[index] = {
                UiSettingsCommandCatalog[index].name,
                UiSettingsCommandCatalog[index].section,
                UiSettingsCommandCatalog[index].kind
            };
        }
        return bindings;
    }();
    static_assert(
        UiSettingsCommandBindings.size() ==
        UiSettingsCommandCatalog.size());

    static std::string NormalizeCommandAscii(
        std::string_view value,
        bool collapseSeparators = false)
    {
        std::string normalized;
        normalized.reserve(value.size());
        for (const unsigned char character : value)
        {
            if (character >= static_cast<unsigned char>('A') &&
                character <= static_cast<unsigned char>('Z'))
            {
                normalized.push_back(static_cast<char>(
                    character - static_cast<unsigned char>('A') +
                    static_cast<unsigned char>('a')));
            }
            else if (collapseSeparators &&
                (character == static_cast<unsigned char>(' ') ||
                 character == static_cast<unsigned char>('-') ||
                 character == static_cast<unsigned char>('_') ||
                 character == static_cast<unsigned char>('+')))
            {
                continue;
            }
            else
            {
                normalized.push_back(static_cast<char>(character));
            }
        }
        return normalized;
    }

    static bool StartsWithCommandPrefix(
        std::string_view value,
        std::string_view prefix)
    {
        return value.size() >= prefix.size() &&
            value.compare(0u, prefix.size(), prefix) == 0;
    }

    static std::string JoinCommandArguments(
        const std::vector<std::string>& arguments)
    {
        std::string result;
        for (const std::string& argument : arguments)
        {
            if (!result.empty())
                result.push_back(' ');
            result += argument;
        }
        return result;
    }

    static bool TryParseCommandBool(
        std::string_view value,
        bool& parsed)
    {
        const std::string normalized =
            NormalizeCommandAscii(value, true);
        if (normalized == "on" ||
            normalized == "true" ||
            normalized == "yes" ||
            normalized == "show" ||
            normalized == "shown" ||
            normalized == "enabled" ||
            normalized == "1")
        {
            parsed = true;
            return true;
        }
        if (normalized == "off" ||
            normalized == "false" ||
            normalized == "no" ||
            normalized == "hide" ||
            normalized == "hidden" ||
            normalized == "disabled" ||
            normalized == "0")
        {
            parsed = false;
            return true;
        }
        return false;
    }

    static bool TryParseCommandFloat(
        std::string_view value,
        float& parsed)
    {
        if (value.empty())
            return false;
        std::string owned(value);
        char* end = nullptr;
        const float candidate = std::strtof(owned.c_str(), &end);
        if (!end ||
            end != owned.c_str() + owned.size() ||
            !std::isfinite(candidate))
        {
            return false;
        }
        parsed = candidate;
        return true;
    }

    static bool TryParseCommandInteger(
        std::string_view value,
        int64_t& parsed)
    {
        if (value.empty())
            return false;
        std::string owned(value);
        char* end = nullptr;
        errno = 0;
        const long long candidate =
            std::strtoll(owned.c_str(), &end, 10);
        if (errno == ERANGE ||
            !end ||
            end != owned.c_str() + owned.size())
        {
            return false;
        }
        parsed = static_cast<int64_t>(candidate);
        return true;
    }

    static std::string FormatCommandFloat(float value)
    {
        char buffer[64];
        snprintf(buffer, std::size(buffer), "%.3f", value);
        return buffer;
    }

    static std::string FormatCommandFloat3(const float3& value)
    {
        return FormatCommandFloat(value.x) + " " +
            FormatCommandFloat(value.y) + " " +
            FormatCommandFloat(value.z);
    }

    static bool RejectUnchangedCommandMutation(
        std::string_view path,
        std::string& error)
    {
        error = "No change: " + std::string(path) +
            " already has the requested value.";
        return false;
    }

    static bool ApplyCommandFloat3(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        float3& current,
        const float3& defaultValue,
        float minimum,
        float maximum,
        std::string& value,
        std::string& error)
    {
        float3 candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            if (arguments.size() != 3u ||
                !TryParseCommandFloat(arguments[0], candidate.x) ||
                !TryParseCommandFloat(arguments[1], candidate.y) ||
                !TryParseCommandFloat(arguments[2], candidate.z) ||
                candidate.x < minimum || candidate.x > maximum ||
                candidate.y < minimum || candidate.y > maximum ||
                candidate.z < minimum || candidate.z > maximum)
            {
                error = std::string(path) +
                    " expects three finite numbers from " +
                    FormatCommandFloat(minimum) + " through " +
                    FormatCommandFloat(maximum) + ".";
                return false;
            }
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get &&
            candidate.x == current.x &&
            candidate.y == current.y &&
            candidate.z == current.z)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        value = FormatCommandFloat3(current);
        return true;
    }

    static const UiSettingsCommandDefinition*
        FindSettingsCommandDefinition(std::string_view rawName)
    {
        const std::string name = NormalizeCommandAscii(rawName);
        const auto definition = std::find_if(
            UiSettingsCommandCatalog.begin(),
            UiSettingsCommandCatalog.end(),
            [&name](const UiSettingsCommandDefinition& candidate)
            {
                return candidate.name == name;
            });
        return definition != UiSettingsCommandCatalog.end()
            ? &*definition
            : nullptr;
    }

    static UiSettingsCommandVerb GetSettingsCommandVerb(
        CommandValueOperation operation)
    {
        switch (operation)
        {
        case CommandValueOperation::Get:
            return UiSettingsCommandVerb::Get;
        case CommandValueOperation::Set:
            return UiSettingsCommandVerb::Set;
        case CommandValueOperation::Toggle:
            return UiSettingsCommandVerb::Toggle;
        case CommandValueOperation::Reset:
            return UiSettingsCommandVerb::Reset;
        }
        return UiSettingsCommandVerb::Get;
    }

    static std::string GetSettingsCommandVerbList(
        const UiSettingsCommandDefinition& definition)
    {
        std::string result;
        const auto append = [&](UiSettingsCommandVerb verb,
            std::string_view label)
        {
            if (!definition.Supports(verb))
                return;
            if (!result.empty())
                result += "|";
            result += label;
        };
        append(UiSettingsCommandVerb::Get, "get");
        append(UiSettingsCommandVerb::Set, "set");
        append(UiSettingsCommandVerb::Toggle, "toggle");
        append(UiSettingsCommandVerb::Reset, "reset");
        append(UiSettingsCommandVerb::Run, "run");
        return result;
    }

    void SetCommandResult(
        std::string result,
        bool error = false)
    {
        m_CommandResult = error ? "Error: " : "Success: ";
        m_CommandResult += std::move(result);
        m_CommandResultIsError = error;
    }

    bool IsCommandRuntimeMutationLocked(
        const UiSettingsCommandDefinition& definition) const
    {
        return definition.section != UiSettingsCommandSection::Ui &&
            m_app->IsSceneBusy();
    }

    bool CheckCommandMutationAllowed(
        const UiSettingsCommandDefinition& definition,
        std::string& error) const
    {
        if (IsCommandRuntimeMutationLocked(definition))
        {
            error =
                "This setting cannot change while a scene is loading.";
            return false;
        }
        return true;
    }

    static bool ApplyCommandBool(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        bool& current,
        bool defaultValue,
        std::string& value,
        std::string& error)
    {
        bool candidate = current;
        switch (operation)
        {
        case CommandValueOperation::Get:
            break;
        case CommandValueOperation::Set:
            if (arguments.size() != 1u ||
                !TryParseCommandBool(arguments.front(), candidate))
            {
                error = std::string(path) + " expects on or off.";
                return false;
            }
            break;
        case CommandValueOperation::Toggle:
            candidate = !candidate;
            break;
        case CommandValueOperation::Reset:
            candidate = defaultValue;
            break;
        }
        if (operation != CommandValueOperation::Get &&
            candidate == current)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        value = current ? "on" : "off";
        return true;
    }

    static bool ApplyCommandInteger(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        int& current,
        int defaultValue,
        int minimum,
        int maximum,
        std::string& value,
        std::string& error)
    {
        int candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            int64_t parsed = 0;
            if (arguments.size() != 1u ||
                !TryParseCommandInteger(arguments.front(), parsed) ||
                parsed < minimum ||
                parsed > maximum)
            {
                error = std::string(path) + " expects an integer from " +
                    std::to_string(minimum) + " through " +
                    std::to_string(maximum) + ".";
                return false;
            }
            candidate = static_cast<int>(parsed);
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get &&
            candidate == current)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        value = std::to_string(current);
        return true;
    }

    static bool ApplyCommandUnsigned(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        uint32_t& current,
        uint32_t defaultValue,
        uint32_t minimum,
        uint32_t maximum,
        std::string& value,
        std::string& error)
    {
        uint32_t candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            int64_t parsed = 0;
            if (arguments.size() != 1u ||
                !TryParseCommandInteger(arguments.front(), parsed) ||
                parsed < static_cast<int64_t>(minimum) ||
                parsed > static_cast<int64_t>(maximum))
            {
                error = std::string(path) + " expects an integer from " +
                    std::to_string(minimum) + " through " +
                    std::to_string(maximum) + ".";
                return false;
            }
            candidate = static_cast<uint32_t>(parsed);
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get &&
            candidate == current)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        value = std::to_string(current);
        return true;
    }

    static bool ApplyCommandFloat(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        float& current,
        float defaultValue,
        float minimum,
        float maximum,
        std::string& value,
        std::string& error)
    {
        float candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            if (arguments.size() != 1u ||
                !TryParseCommandFloat(arguments.front(), candidate) ||
                candidate < minimum ||
                candidate > maximum)
            {
                error = std::string(path) +
                    " expects a finite number from " +
                    FormatCommandFloat(minimum) + " through " +
                    FormatCommandFloat(maximum) + ".";
                return false;
            }
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }
        if (operation != CommandValueOperation::Get &&
            candidate == current)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        value = FormatCommandFloat(current);
        return true;
    }

    template <typename Enum, size_t Count>
    static bool ApplyCommandEnum(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        Enum& current,
        Enum defaultValue,
        const std::array<std::pair<std::string_view, Enum>, Count>& options,
        std::string& value,
        std::string& error,
        bool allowSameValueMutation = false)
    {
        Enum candidate = current;
        if (operation == CommandValueOperation::Set)
        {
            if (arguments.empty())
            {
                error = std::string(path) + " expects one value.";
                return false;
            }
            const std::string requested = NormalizeCommandAscii(
                JoinCommandArguments(arguments), true);
            const auto match = std::find_if(
                options.begin(),
                options.end(),
                [&requested](const auto& option)
                {
                    return NormalizeCommandAscii(
                        option.first, true) == requested;
                });
            if (match == options.end())
            {
                error = std::string(path) + " expects ";
                for (size_t index = 0u; index < options.size(); ++index)
                {
                    if (index > 0u)
                    {
                        error += index + 1u == options.size()
                            ? ", or "
                            : ", ";
                    }
                    error += options[index].first;
                }
                error += ".";
                return false;
            }
            candidate = match->second;
        }
        else if (operation == CommandValueOperation::Reset)
        {
            candidate = defaultValue;
        }
        else if (operation == CommandValueOperation::Toggle)
        {
            error = std::string(path) + " is not boolean.";
            return false;
        }

        if (operation != CommandValueOperation::Get &&
            candidate == current &&
            !allowSameValueMutation)
        {
            return RejectUnchangedCommandMutation(path, error);
        }
        current = candidate;
        const auto selected = std::find_if(
            options.begin(),
            options.end(),
            [&current](const auto& option)
            {
                return option.second == current;
            });
        value = selected != options.end()
            ? std::string(selected->first)
            : "unknown";
        return true;
    }

    bool DispatchUiCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        if (path == "ui.skin")
        {
            static constexpr std::array<
                std::pair<std::string_view, UiSkin>, 2> Options = {{
                { "amp", UiSkin::Amp },
                { "og", UiSkin::Og }
            }};
            return ApplyCommandEnum(
                operation,
                arguments,
                path,
                m_ui.Skin,
                DefaultUiSkin,
                Options,
                value,
                error);
        }
        if (path == "ui.visible")
        {
            return ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.ShowUI,
                false,
                value,
                error);
        }
        if (path == "ui.settings-collapsed")
        {
            bool collapsed = m_SettingsCollapsedRequest.value_or(
                m_SettingsCollapsed);
            if (!ApplyCommandBool(
                    operation,
                    arguments,
                    path,
                    collapsed,
                    false,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
            {
                m_SettingsCollapsedRequest = collapsed;
                m_SettingsCollapsed = collapsed;
            }
            return true;
        }
        if (path == "ui.zoom")
        {
            static constexpr std::array<
                std::pair<std::string_view, PixelZoomMode>, 5> Options = {{
                { "off", PixelZoomMode::Off },
                { "2x", PixelZoomMode::Zoom2x },
                { "3x", PixelZoomMode::Zoom3x },
                { "4x", PixelZoomMode::Zoom4x },
                { "5x", PixelZoomMode::Zoom5x }
            }};
            return ApplyCommandEnum(
                operation,
                arguments,
                path,
                m_ui.PixelZoom,
                PixelZoomMode::Off,
                Options,
                value,
                error);
        }
        if (path == "material-editor.visible")
        {
            return ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.ShowMaterialEditor,
                false,
                value,
                error);
        }
        error = "Internal UI command binding is missing for '" +
            std::string(path) + "'.";
        return false;
    }

    bool DispatchGeneralCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        if (path == "gpu.adapter")
        {
            const auto active = std::find_if(
                m_ui.GpuAdapterChoices.begin(),
                m_ui.GpuAdapterChoices.end(),
                [this](const GpuAdapterChoice& adapter)
                {
                    return adapter.adapterIndex ==
                        m_ui.ActiveGpuAdapterIndex;
                });
            if (operation == CommandValueOperation::Get)
            {
                value = active != m_ui.GpuAdapterChoices.end()
                    ? active->name
                    : "unknown";
                return true;
            }
            if (operation != CommandValueOperation::Set)
            {
                error = "gpu.adapter supports get and set only.";
                return false;
            }

            const std::string requested = JoinCommandArguments(arguments);
            if (requested.empty())
            {
                error =
                    "gpu.adapter expects an adapter index or unique name.";
                return false;
            }
            int64_t requestedIndex = -1;
            const bool numeric =
                TryParseCommandInteger(requested, requestedIndex);
            const std::string normalized =
                NormalizeCommandAscii(requested, true);
            const GpuAdapterChoice* match = nullptr;
            for (const GpuAdapterChoice& adapter :
                m_ui.GpuAdapterChoices)
            {
                const bool matches =
                    (numeric &&
                        adapter.adapterIndex == requestedIndex) ||
                    NormalizeCommandAscii(adapter.name, true) ==
                        normalized;
                if (!matches)
                    continue;
                if (match && match->adapterIndex != adapter.adapterIndex)
                {
                    error =
                        "gpu.adapter name is ambiguous; use its numeric "
                        "adapter index.";
                    return false;
                }
                match = &adapter;
            }
            if (!match)
            {
                error =
                    "Unknown graphics adapter. Use '/list gpu.adapter'.";
                return false;
            }
            if (match->adapterIndex == m_ui.ActiveGpuAdapterIndex)
                return RejectUnchangedCommandMutation(path, error);
            value = match->name;
            g_RestartAdapterIndex = match->adapterIndex;
            g_RestartRequested = true;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return true;
        }
        if (path == "camera.mode")
        {
            static constexpr std::array<
                std::pair<std::string_view, CameraMode>, 2> Options = {{
                { "freelook", CameraMode::ThirdPerson },
                { "locked", CameraMode::Static }
            }};
            CameraMode candidate = m_ui.Camera;
            if (!ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate,
                    CameraMode::ThirdPerson,
                    Options,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
                m_app->SetCameraMode(candidate);
            return true;
        }
        if (path == "camera.location")
        {
            if (!m_app->HasSponzaCameraLocations())
            {
                error =
                    "The current scene does not provide stored camera "
                    "locations.";
                return false;
            }
            static constexpr std::array<
                std::pair<std::string_view, SponzaCameraLocation>, 2>
                Options = {{
                    { "piloted", SponzaCameraLocation::Free },
                    {
                        "position-1",
                        SponzaCameraLocation::SimplifiedApproximation
                    }
                }};
            SponzaCameraLocation candidate =
                m_app->GetSponzaCameraLocation();
            if (!ApplyCommandEnum(
                    operation,
                    arguments,
                    path,
                    candidate,
                    SponzaCameraLocation::SimplifiedApproximation,
                    Options,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
                m_app->SetSponzaCameraLocation(candidate);
            return true;
        }
        if (path == "scene.current")
        {
            if (operation == CommandValueOperation::Get)
            {
                value = m_app->GetCurrentSceneDisplayName();
                return true;
            }
            if (operation != CommandValueOperation::Set)
            {
                error = "scene.current supports get and set only.";
                return false;
            }
            const std::string requested = JoinCommandArguments(arguments);
            const std::string normalized =
                NormalizeCommandAscii(requested, true);
            const SceneCatalogEntry* match = nullptr;
            for (const SceneCatalogEntry& scene :
                m_app->GetAvailableScenes())
            {
                const bool matches =
                    NormalizeCommandAscii(scene.FileName, true) ==
                        normalized ||
                    NormalizeCommandAscii(scene.DisplayName, true) ==
                        normalized;
                if (!matches)
                    continue;
                if (match && match->FileName != scene.FileName)
                {
                    error =
                        "Scene name is ambiguous; use its exact filename.";
                    return false;
                }
                match = &scene;
            }
            if (!match)
            {
                error = "Unknown scene. Use '/list scene.current'.";
                return false;
            }
            if (match->FileName == m_app->GetCurrentSceneName())
                return RejectUnchangedCommandMutation(path, error);
            m_app->SetCurrentSceneName(match->FileName);
            value = match->DisplayName;
            return true;
        }
        error = "Internal General command binding is missing for '" +
            std::string(path) + "'.";
        return false;
    }

    bool DispatchRepresentationCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        WorldSpaceRepresentationSettings candidate = m_ui.Representation;
        const WorldSpaceRepresentationSettings factoryDefaults;
        bool handled = true;

        if (path == "representation.bvh.build-preference")
        {
            static constexpr std::array<
                std::pair<std::string_view, BvhBuildPreference>, 3>
                Options = {{
                    { "fast-trace", BvhBuildPreference::FastTrace },
                    { "balanced", BvhBuildPreference::Balanced },
                    { "fast-build", BvhBuildPreference::FastBuild }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.bvhBuildPreference,
                factoryDefaults.bvhBuildPreference,
                Options,
                value,
                error);
        }
        else if (path == "representation.blas.update-mode")
        {
            static constexpr std::array<
                std::pair<std::string_view, BlasUpdateMode>, 2>
                Options = {{
                    { "rebuild", BlasUpdateMode::Rebuild },
                    { "refit", BlasUpdateMode::Refit }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.blasUpdateMode,
                factoryDefaults.blasUpdateMode,
                Options,
                value,
                error);
        }
        else if (path == "representation.tlas.update-mode")
        {
            static constexpr std::array<
                std::pair<std::string_view, TlasUpdateMode>, 2>
                Options = {{
                    { "rebuild", TlasUpdateMode::Rebuild },
                    { "refit", TlasUpdateMode::Refit }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.tlasUpdateMode,
                factoryDefaults.tlasUpdateMode,
                Options,
                value,
                error);
        }
        else
        {
            handled = false;
            error =
                "Internal Representation command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled)
            return false;
        if (operation != CommandValueOperation::Get)
        {
            const WorldSpaceRepresentationInvalidation invalidation =
                GetWorldSpaceRepresentationInvalidation(
                    m_ui.Representation,
                    candidate);
            m_ui.Representation = candidate;
            m_app->InvalidateWorldSpaceRepresentation(invalidation);
        }
        return true;
    }


    bool DispatchVisibilityCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        ScreenSpaceVisibilitySettings candidate =
            m_ui.ScreenSpaceVisibility;
        const ScreenSpaceVisibilitySettings defaults;
        bool handled = true;

        if (path == "visibility.enabled")
        {
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.enabled, defaults.enabled, value, error);
        }
        else if (path == "visibility.quality")
        {
            static constexpr std::array<
                std::pair<std::string_view, ScreenSpaceVisibilityQuality>, 5>
                Options = {{
                    { "low", ScreenSpaceVisibilityQuality::Low },
                    { "medium", ScreenSpaceVisibilityQuality::Medium },
                    { "high", ScreenSpaceVisibilityQuality::High },
                    { "ultra", ScreenSpaceVisibilityQuality::Ultra },
                    { "custom", ScreenSpaceVisibilityQuality::Custom }
                }};
            ScreenSpaceVisibilityQuality quality = candidate.quality;
            handled = ApplyCommandEnum(operation, arguments, path, quality,
                defaults.quality, Options, value, error);
            if (handled && operation != CommandValueOperation::Get)
            {
                if (quality == ScreenSpaceVisibilityQuality::Custom)
                    MarkScreenSpaceVisibilityQualityCustom(candidate);
                else
                    ApplyScreenSpaceVisibilityQualityPreset(candidate, quality);
            }
        }
        else if (path == "visibility.resolution")
        {
            static constexpr std::array<
                std::pair<std::string_view, VisibilityResolution>, 3>
                Options = {{
                    { "full", VisibilityResolution::Full },
                    { "half", VisibilityResolution::Half },
                    { "quarter", VisibilityResolution::Quarter }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.resolution, defaults.resolution,
                Options, value, error);
        }
        else if (path == "visibility.estimator")
        {
            static constexpr std::array<
                std::pair<std::string_view, VisibilityEstimator>, 3>
                Options = {{
                    { "projected-angle",
                        VisibilityEstimator::UniformProjectedAngle },
                    { "solid-angle",
                        VisibilityEstimator::UniformSolidAngle },
                    { "cosine-weighted",
                        VisibilityEstimator::CosineWeightedSolidAngle }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.estimator, defaults.estimator,
                Options, value, error);
        }
        else if (path == "visibility.noise")
        {
            static constexpr std::array<
                std::pair<std::string_view, VisibilitySampleScheduler>, 2>
                Options = {{
                    { "permutated-white-noise",
                        VisibilitySampleScheduler::PermutatedWhiteNoise },
                    { "void-cluster-blue-noise",
                        VisibilitySampleScheduler::VoidClusterBlueNoise }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.sampling.scheduler, defaults.sampling.scheduler,
                Options, value, error);
        }
        else if (path == "visibility.samples")
        {
            handled = ApplyCommandUnsigned(operation, arguments, path,
                candidate.sampling.maximumSampleCount,
                defaults.sampling.maximumSampleCount, 1u, 64u, value, error);
        }
        else if (path == "visibility.radius")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.sampling.radius, defaults.sampling.radius,
                0.1f, 10.f, value, error);
        }
        else if (path == "visibility.thickness")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.sampling.thickness, defaults.sampling.thickness,
                0.01f, 2.f, value, error);
        }
        else if (path == "visibility.distribution")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.sampling.stepDistributionExponent,
                defaults.sampling.stepDistributionExponent,
                0.25f, 4.f, value, error);
        }
        else if (path == "visibility.ao.enabled")
        {
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.ambientOcclusion.enabled,
                defaults.ambientOcclusion.enabled, value, error);
        }
        else if (path == "visibility.ao.strength")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.ambientOcclusion.strength,
                defaults.ambientOcclusion.strength,
                0.f, 4.f, value, error);
        }
        else if (path == "visibility.ao.precision")
        {
            using Precision = VisibilityScalarBufferPrecision;
            static constexpr std::array<
                std::pair<std::string_view, Precision>, 2> Options = {{
                    { "16-bit", Precision::Float16 },
                    { "32-bit", Precision::Float32 }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.bufferPrecision.ambient,
                defaults.bufferPrecision.ambient,
                Options, value, error);
        }
        else if (path == "visibility.gi.enabled")
        {
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.indirectDiffuse.enabled,
                defaults.indirectDiffuse.enabled, value, error);
        }
        else if (path == "visibility.gi.intensity")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.indirectDiffuse.intensity,
                defaults.indirectDiffuse.intensity,
                0.f, 16.f, value, error);
        }
        else if (path == "visibility.gi.precision")
        {
            using Precision = VisibilityVectorBufferPrecision;
            static constexpr std::array<
                std::pair<std::string_view, Precision>, 2> Options = {{
                    { "16-bit", Precision::Rgba16Float },
                    { "32-bit", Precision::Rgba32Float }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.bufferPrecision.indirect,
                defaults.bufferPrecision.indirect,
                Options, value, error);
        }
        else if (path == "visibility.reconstruction")
        {
            static constexpr std::array<
                std::pair<std::string_view, VisibilityReconstructionMode>, 4>
                Options = {{
                    { "direct-or-guide-aware",
                        VisibilityReconstructionMode::Standard },
                    { "packed-depth-normal",
                        VisibilityReconstructionMode::PackedDepthNormal },
                    { "packed-slope-aware",
                        VisibilityReconstructionMode::
                            PackedSlopeAdjustedDepthNormal },
                    { "packed-leak-controlled",
                        VisibilityReconstructionMode::
                            PackedControlledLeakage }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.reconstruction.mode,
                defaults.reconstruction.mode, Options, value, error);
        }
        else if (path == "visibility.spatial.enabled")
        {
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.reconstruction.spatialEnabled,
                defaults.reconstruction.spatialEnabled, value, error);
        }
        else if (path == "visibility.spatial.filter")
        {
            static constexpr std::array<
                std::pair<std::string_view, VisibilitySpatialFilter>, 2>
                Options = {{
                    { "joint-bilateral",
                        VisibilitySpatialFilter::JointBilateral },
                    { "gaussian-bilateral",
                        VisibilitySpatialFilter::GaussianJointBilateral }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.reconstruction.spatialFilter,
                defaults.reconstruction.spatialFilter,
                Options, value, error);
        }
        else if (path == "visibility.spatial.radius")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.reconstruction.spatialRadius,
                defaults.reconstruction.spatialRadius,
                1.f, 8.f, value, error);
        }
        else
        {
            handled = false;
            error = "Internal Visibility command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled)
            return false;
        if (operation != CommandValueOperation::Get)
        {
            if (path != "visibility.quality")
            {
                MarkScreenSpaceVisibilityQualityCustom(candidate);
                ReconcileScreenSpaceVisibilityQualityPreset(candidate);
            }
            m_ui.ScreenSpaceVisibility = candidate;
        }
        return true;
    }

    bool DispatchAliasingCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        AntiAliasingSettings candidate = m_ui.AntiAliasing;
        const AntiAliasingSettings defaults;
        static constexpr std::array<
            std::pair<std::string_view, AntiAliasingQuality>, 4>
            QualityOptions = {{
                { "low", AntiAliasingQuality::Low },
                { "medium", AntiAliasingQuality::Medium },
                { "high", AntiAliasingQuality::High },
                { "ultra", AntiAliasingQuality::Ultra }
            }};
        bool handled = true;

        if (path == "anti-aliasing.taa.enabled")
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.temporal.enabled, defaults.temporal.enabled,
                value, error);
        else if (path == "anti-aliasing.taa.quality")
        {
            const bool temporalQualityCustom =
                candidate.temporal.stationaryBypass !=
                    defaults.temporal.stationaryBypass ||
                !(candidate.temporal.algorithmOverrides ==
                    defaults.temporal.algorithmOverrides);
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.quality, defaults.temporal.quality,
                QualityOptions, value, error,
                temporalQualityCustom);
            if (handled && operation != CommandValueOperation::Get)
            {
                candidate.temporal.stationaryBypass =
                    defaults.temporal.stationaryBypass;
                candidate.temporal.algorithmOverrides =
                    defaults.temporal.algorithmOverrides;
            }
        }
        else if (path == "anti-aliasing.taa.jitter-sequence")
        {
            using Sequence = TemporalAaJitterSequence;
            static constexpr std::array<
                std::pair<std::string_view, Sequence>, 6> Options = {{
                    { "rotated-grid-4", Sequence::RotatedGrid4 },
                    { "uniform-helix-4", Sequence::UniformHelix4 },
                    { "halton-8", Sequence::Halton23x8 },
                    { "halton-16", Sequence::Halton23x16 },
                    { "halton-32", Sequence::Halton23x32 },
                    { "sobol-32", Sequence::Sobol32 }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.jitterSequence,
                defaults.temporal.jitterSequence,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.previous-depth")
        {
            static constexpr std::array<std::pair<
                std::string_view, TemporalAaDepthValidation>, 2> Options = {{
                    { "stationary-bypass",
                        TemporalAaDepthValidation::MovingPoint },
                    { "four-texel-footprint",
                        TemporalAaDepthValidation::FourTexelFootprint }
                }};
            TemporalAaDepthValidation validation =
                candidate.temporal.stationaryBypass
                ? TemporalAaDepthValidation::MovingPoint
                : TemporalAaDepthValidation::FourTexelFootprint;
            const TemporalAaDepthValidation defaultValidation =
                defaults.temporal.stationaryBypass
                ? TemporalAaDepthValidation::MovingPoint
                : TemporalAaDepthValidation::FourTexelFootprint;
            handled = ApplyCommandEnum(
                operation, arguments, path, validation,
                defaultValidation, Options, value, error);
            if (handled && operation != CommandValueOperation::Get)
            {
                candidate.temporal.stationaryBypass =
                    validation == TemporalAaDepthValidation::MovingPoint;
            }
        }
        else if (path == "anti-aliasing.taa.temporal-cost")
        {
            static constexpr std::array<
                std::pair<std::string_view, TemporalAaCostMode>, 3>
                Options = {{
                    { "full-quality", TemporalAaCostMode::FullQuality },
                    { "reduced", TemporalAaCostMode::Reduced },
                    { "minimum", TemporalAaCostMode::Minimum }
                }};
            const bool temporalCostCustom =
                !(candidate.temporal.behaviorOverrides ==
                    defaults.temporal.behaviorOverrides) ||
                m_ui.TemporalAaSharpenEnabled ||
                m_ui.TemporalAaSharpness !=
                    TemporalAaDefaultSharpness;
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.costMode,
                defaults.temporal.costMode, Options, value, error,
                temporalCostCustom);
            if (handled && operation != CommandValueOperation::Get)
            {
                candidate.temporal.behaviorOverrides =
                    defaults.temporal.behaviorOverrides;
                m_ui.TemporalAaSharpenEnabled = false;
                m_ui.TemporalAaSharpness = TemporalAaDefaultSharpness;
            }
        }
        else if (path == "anti-aliasing.taa.motion-source")
        {
            using Override = TemporalAaMotionSourceOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 4> Options = {{
                    { "preset", Override::FromPreset },
                    { "center", Override::Center },
                    { "closest-cross", Override::ClosestCross },
                    { "edge-dilation", Override::CenterFirstEdgeDilation }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.algorithmOverrides.motionSource,
                defaults.temporal.algorithmOverrides.motionSource,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.current-sample")
        {
            using Override = TemporalAaCurrentReconstructionOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "preset", Override::FromPreset },
                    { "direct", Override::Direct },
                    { "de-jittered", Override::DeJittered }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.algorithmOverrides.currentReconstruction,
                defaults.temporal.algorithmOverrides.currentReconstruction,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.history-filter")
        {
            using Override = TemporalAaHistoryFilterOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 5> Options = {{
                    { "preset", Override::FromPreset },
                    { "bilinear", Override::Bilinear },
                    { "bicubic", Override::OneSampleBicubic },
                    { "five-tap-bicubic", Override::FiveTapCatmullRom },
                    { "nine-tap-bicubic", Override::NineTapCatmullRom }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.algorithmOverrides.historyFilter,
                defaults.temporal.algorithmOverrides.historyFilter,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.rectification")
        {
            using Override = TemporalAaRectificationOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "preset", Override::FromPreset },
                    { "pair-tristimulus", Override::PairRgb },
                    { "variance-chroma", Override::VarianceYCoCg }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.algorithmOverrides.rectification,
                defaults.temporal.algorithmOverrides.rectification,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.history.frames")
        {
            if (operation == CommandValueOperation::Set)
            {
                int64_t requested = 0;
                if (arguments.size() != 1u ||
                    !TryParseCommandInteger(arguments.front(), requested) ||
                    (requested != -1 && (requested < 1 || requested > 32)))
                {
                    error = std::string(path) +
                        " expects -1 or an integer from 1 through 32.";
                    return false;
                }
            }
            handled = ApplyCommandInteger(operation, arguments, path,
                candidate.temporal.algorithmOverrides.historyFrames,
                defaults.temporal.algorithmOverrides.historyFrames,
                -1, 32, value, error);
        }
        else if (path == "anti-aliasing.taa.history.strength")
        {
            if (operation == CommandValueOperation::Set)
            {
                float requested = 0.f;
                if (arguments.size() != 1u ||
                    !TryParseCommandFloat(arguments.front(), requested) ||
                    (requested != -1.f &&
                        (requested < 0.f || requested > 2.f)))
                {
                    error = std::string(path) +
                        " expects -1 or a number from 0 through 2.";
                    return false;
                }
            }
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.temporal.algorithmOverrides.historyStrength,
                defaults.temporal.algorithmOverrides.historyStrength,
                -1.f, 2.f, value, error);
        }
        else if (path == "anti-aliasing.taa.history.storage")
        {
            using Override = TemporalAaHistoryStorageOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "robust", Override::Robust },
                    { "compact", Override::Compact }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.historyStorage,
                defaults.temporal.behaviorOverrides.historyStorage,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.history.weight")
        {
            using Override = TemporalAaHistoryWeightPolicyOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "confidence-recurrence",
                        Override::ConfidenceRecurrence },
                    { "immediate-horizon", Override::ImmediateHorizon }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.historyWeight,
                defaults.temporal.behaviorOverrides.historyWeight,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.motion-trust")
        {
            using Override = TemporalAaMotionTrustOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "linear-speed", Override::LinearSpeed },
                    { "squared-speed", Override::SquaredSpeed }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.motionTrust,
                defaults.temporal.behaviorOverrides.motionTrust,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.rectification-clip")
        {
            using Override = TemporalAaRectificationClipOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "velocity-dilated", Override::VelocityDilatedLine },
                    { "tight-component", Override::TightComponent }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.rectificationClip,
                defaults.temporal.behaviorOverrides.rectificationClip,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.blend-domain")
        {
            using Override = TemporalAaBlendDomainOverride;
            static constexpr std::array<
                std::pair<std::string_view, Override>, 3> Options = {{
                    { "temporal-cost", Override::FromTemporalCost },
                    { "luminance-compressed",
                        Override::LuminanceCompressed },
                    { "linear-rgb", Override::LinearRgb }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.blendDomain,
                defaults.temporal.behaviorOverrides.blendDomain,
                Options, value, error);
        }
        else if (path == "anti-aliasing.taa.preset-sharpening")
        {
            static constexpr std::array<
                std::pair<std::string_view, TemporalAaAutoToggle>, 3>
                Options = {{
                    { "auto", TemporalAaAutoToggle::Auto },
                    { "off", TemporalAaAutoToggle::Off },
                    { "on", TemporalAaAutoToggle::On }
                }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.temporal.behaviorOverrides.sharpening,
                defaults.temporal.behaviorOverrides.sharpening,
                Options, value, error);
        }
        else if (path == "anti-aliasing.fxaa.enabled")
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.fastApproximate.enabled,
                defaults.fastApproximate.enabled,
                value, error);
        else if (path == "anti-aliasing.fxaa.quality")
        {
            const bool fastApproximateQualityCustom =
                !MatchesFastApproximateAaQualityPreset(
                    candidate.fastApproximate);
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.fastApproximate.quality,
                defaults.fastApproximate.quality,
                QualityOptions, value, error,
                fastApproximateQualityCustom);
            if (handled && operation != CommandValueOperation::Get)
            {
                ApplyFastApproximateAaQualityPreset(
                    candidate.fastApproximate,
                    candidate.fastApproximate.quality);
            }
        }
        else if (path == "anti-aliasing.fxaa.edge-sharpness")
        {
            const FastApproximateAaQualityPreset preset =
                GetFastApproximateAaQualityPreset(
                    candidate.fastApproximate.quality);
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.fastApproximate.edgeSharpness,
                preset.edgeSharpness,
                FastApproximateAaMinimumEdgeSharpness,
                FastApproximateAaMaximumEdgeSharpness,
                value, error);
        }
        else if (path == "anti-aliasing.fxaa.edge-threshold")
        {
            const FastApproximateAaQualityPreset preset =
                GetFastApproximateAaQualityPreset(
                    candidate.fastApproximate.quality);
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.fastApproximate.edgeThreshold,
                preset.edgeThreshold,
                FastApproximateAaMinimumEdgeThreshold,
                FastApproximateAaMaximumEdgeThreshold,
                value, error);
        }
        else if (path == "anti-aliasing.fxaa.minimum-edge-threshold")
        {
            const FastApproximateAaQualityPreset preset =
                GetFastApproximateAaQualityPreset(
                    candidate.fastApproximate.quality);
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.fastApproximate.darkEdgeThreshold,
                preset.darkEdgeThreshold,
                FastApproximateAaMinimumDarkEdgeThreshold,
                FastApproximateAaMaximumDarkEdgeThreshold,
                value, error);
        }
        else if (path == "anti-aliasing.cmaa2.enabled")
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.cmaa2.enabled, defaults.cmaa2.enabled,
                value, error);
        else if (path == "anti-aliasing.cmaa2.quality")
        {
            const bool cmaa2QualityCustom =
                !MatchesCmaa2QualityPreset(candidate.cmaa2);
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.cmaa2.quality, defaults.cmaa2.quality,
                QualityOptions, value, error,
                cmaa2QualityCustom);
            if (handled && operation != CommandValueOperation::Get)
            {
                ApplyCmaa2QualityPreset(
                    candidate.cmaa2, candidate.cmaa2.quality);
            }
        }
        else if (path == "anti-aliasing.cmaa2.edge-threshold")
        {
            const Cmaa2QualityPreset preset = GetCmaa2QualityPreset(
                candidate.cmaa2.quality);
            handled = ApplyCommandFloat(operation, arguments, path,
                candidate.cmaa2.edgeThreshold,
                preset.edgeThreshold,
                Cmaa2MinimumEdgeThreshold,
                Cmaa2MaximumEdgeThreshold,
                value, error);
        }
        else if (path == "anti-aliasing.cmaa2.detector")
        {
            static constexpr std::array<
                std::pair<std::string_view, Cmaa2EdgeDetector>, 2>
                Options = {{
                    { "luma", Cmaa2EdgeDetector::Luma },
                    { "full-color", Cmaa2EdgeDetector::FullColor }
                }};
            const Cmaa2QualityPreset preset = GetCmaa2QualityPreset(
                candidate.cmaa2.quality);
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.cmaa2.detector, preset.detector,
                Options, value, error);
        }
        else if (path == "anti-aliasing.msaa.enabled")
            handled = ApplyCommandBool(operation, arguments, path,
                candidate.msaa.enabled, defaults.msaa.enabled,
                value, error);
        else if (path == "anti-aliasing.msaa.quality")
        {
            const bool multisampleQualityCustom =
                !MatchesMultisampleQualityPreset(candidate.msaa);
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.msaa.quality, defaults.msaa.quality,
                QualityOptions, value, error,
                multisampleQualityCustom);
            if (handled && operation != CommandValueOperation::Get)
            {
                ApplyMultisampleQualityPreset(
                    candidate.msaa, candidate.msaa.quality);
            }
        }
        else if (path == "anti-aliasing.msaa.samples")
        {
            static constexpr std::array<
                std::pair<std::string_view, uint32_t>, 4> Options = {{
                    { "2x", 2u }, { "4x", 4u },
                    { "8x", 8u }, { "16x", 16u }
            }};
            handled = ApplyCommandEnum(operation, arguments, path,
                candidate.msaa.sampleCount,
                GetMultisampleQualitySampleCount(candidate.msaa.quality),
                Options, value, error);
        }
        else if (path == "anti-aliasing.sharpen.enabled")
        {
            handled = ApplyCommandBool(operation, arguments, path,
                m_ui.TemporalAaSharpenEnabled, false, value, error);
            return handled;
        }
        else if (path == "anti-aliasing.sharpen.strength")
        {
            handled = ApplyCommandFloat(operation, arguments, path,
                m_ui.TemporalAaSharpness, TemporalAaDefaultSharpness,
                0.f, 1.f, value, error);
            return handled;
        }
        else
        {
            handled = false;
            error = "Internal Anti-Aliasing command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled)
            return false;
        if (operation != CommandValueOperation::Get)
        {
            candidate.fastApproximate.edgeSharpness =
                ClampFastApproximateAaEdgeSharpness(
                    candidate.fastApproximate.edgeSharpness);
            candidate.fastApproximate.edgeThreshold =
                ClampFastApproximateAaEdgeThreshold(
                    candidate.fastApproximate.edgeThreshold);
            candidate.fastApproximate.darkEdgeThreshold =
                ClampFastApproximateAaDarkEdgeThreshold(
                    candidate.fastApproximate.darkEdgeThreshold);
            candidate.cmaa2.edgeThreshold = ClampCmaa2EdgeThreshold(
                candidate.cmaa2.edgeThreshold);
            candidate.cmaa2.detector = SanitizeCmaa2EdgeDetector(
                candidate.cmaa2.detector);
            candidate.msaa.sampleCount =
                SanitizeMsaaSampleCount(candidate.msaa.sampleCount);
            m_ui.AntiAliasing = candidate;
        }
        return true;
    }

    bool DispatchDebugCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        if (path == "debug.world.materials")
        {
            static constexpr std::array<
                std::pair<std::string_view, WhiteWorldMode>, 4> Options = {{
                    { "scene", WhiteWorldMode::Off },
                    { "white", WhiteWorldMode::On },
                    { "white-detail", WhiteWorldMode::PreserveDetail },
                    { "white-lighting", WhiteWorldMode::PreserveLighting }
                }};
            WhiteWorldMode mode = m_ui.WhiteWorld;
            const bool handled = ApplyCommandEnum(
                operation, arguments, path, mode, WhiteWorldMode::Off,
                Options, value, error);
            if (handled && operation != CommandValueOperation::Get)
                m_app->SetWhiteWorldMode(mode);
            return handled;
        }
        if (path == "debug.visibility.view")
        {
            static constexpr std::array<std::pair<
                std::string_view, VisibilityDebugView>, 4> Options = {{
                    { "final", VisibilityDebugView::FinalImage },
                    { "ambient-visibility",
                        VisibilityDebugView::AmbientVisibility },
                    { "traced-indirect",
                        VisibilityDebugView::TracedIndirect },
                    { "applied-indirect",
                        VisibilityDebugView::AppliedIndirect }
                }};
            const VisibilityDebugView previous =
                m_ui.ScreenSpaceVisibility.debugView;
            const bool handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                m_ui.ScreenSpaceVisibility.debugView,
                VisibilityDebugView::FinalImage,
                Options,
                value,
                error);
            if (handled &&
                operation != CommandValueOperation::Get &&
                previous != m_ui.ScreenSpaceVisibility.debugView)
            {
                m_app->ResetImageBasedLightingHistory();
            }
            return handled;
        }
        if (path == "debug.pbr.filter")
        {
            static constexpr std::array<std::pair<
                std::string_view, PbrLightingDebugView>, 12> Options = {{
                    { "final", PbrLightingDebugView::None },
                    { "surface-normals", PbrLightingDebugView::ShadingNormal },
                    { "geometry-normals", PbrLightingDebugView::GeometricNormal },
                    { "normal-difference", PbrLightingDebugView::NormalDifference },
                    { "diffuse-environment", PbrLightingDebugView::DiffuseEnvironment },
                    { "environment-direction", PbrLightingDebugView::EnvironmentDirection },
                    { "reflected-environment", PbrLightingDebugView::PrefilteredSpecularEnvironment },
                    { "brdf-response", PbrLightingDebugView::EnvironmentBrdf },
                    { "specular-environment", PbrLightingDebugView::FinalSpecularEnvironment },
                    { "all-environment-light", PbrLightingDebugView::CombinedEnvironment },
                    { "specular-visibility", PbrLightingDebugView::SpecularOcclusion },
                    { "environment-level", PbrLightingDebugView::EnvironmentMip }
                }};
            const bool handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                m_ui.LightingDebugView,
                PbrLightingDebugView::None,
                Options,
                value,
                error);
            if (handled && operation != CommandValueOperation::Get)
                m_app->ResetImageBasedLightingHistory();
            return handled;
        }
        if (path == "debug.shadows.isolation")
        {
            static constexpr std::array<std::pair<
                std::string_view, ScreenSpaceShadowIsolationView>, 3>
                Options = {{
                    { "final", ScreenSpaceShadowIsolationView::None },
                    { "thread-lanes", ScreenSpaceShadowIsolationView::Thread },
                    { "wave-groups", ScreenSpaceShadowIsolationView::Wave }
                }};
            return ApplyCommandEnum(
                operation,
                arguments,
                path,
                m_ui.ScreenSpaceDirectionalShadows.isolationView,
                ScreenSpaceShadowIsolationView::None,
                Options,
                value,
                error);
        }
        error = "Internal Debug command binding is missing for '" +
            std::string(path) + "'.";
        return false;
    }

    bool DispatchSkyCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        if (path == "sky.environment")
        {
            const ImageBasedLightingSource current =
                m_ui.EnvironmentSource;
            ImageBasedLightingSource candidate = current;
            if (operation == CommandValueOperation::Set)
            {
                const std::string requested =
                    NormalizeCommandAscii(
                        JoinCommandArguments(arguments), true);
                int64_t requestedIndex = -1;
                const bool numeric = TryParseCommandInteger(
                    JoinCommandArguments(arguments), requestedIndex);
                bool found = false;
                for (uint32_t index = 0u;
                    index < uint32_t(ImageBasedLightingSource::Count);
                    ++index)
                {
                    const auto source =
                        ImageBasedLightingSource(index);
                    if ((numeric &&
                            requestedIndex ==
                                static_cast<int64_t>(index)) ||
                        NormalizeCommandAscii(
                            GetImageBasedLightingSourceInfo(source)
                                .displayName,
                            true) == requested)
                    {
                        candidate = source;
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    error =
                        "Unknown environment. Use '/list sky.environment'.";
                    return false;
                }
            }
            else if (operation == CommandValueOperation::Reset)
            {
                candidate =
                    ImageBasedLightingSource::Kloppenheim03Day;
            }
            else if (operation == CommandValueOperation::Toggle)
            {
                error = std::string(path) + " is not boolean.";
                return false;
            }
            if (operation != CommandValueOperation::Get &&
                candidate == current)
            {
                return RejectUnchangedCommandMutation(path, error);
            }
            value =
                GetImageBasedLightingSourceInfo(candidate).displayName;
            if (operation != CommandValueOperation::Get)
            {
                m_ui.EnvironmentSource = candidate;
                m_ui.EnvironmentExposureStops =
                    GetImageBasedLightingSourceInfo(candidate)
                        .defaultExposureStops;
                m_app->ResetImageBasedLightingHistory();
            }
            return true;
        }

        bool handled = true;
        bool resetVisibilityHistory = false;
        bool resetTemporalHistory = false;
        if (path == "sky.exposure")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.EnvironmentExposureStops,
                GetImageBasedLightingSourceInfo(
                    m_ui.EnvironmentSource).defaultExposureStops,
                -8.f,
                8.f,
                value,
                error);
            resetVisibilityHistory = true;
        }
        else if (path == "sky.diffuse-ibl")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.EnableDiffuseIbl,
                true,
                value,
                error);
            resetVisibilityHistory = true;
        }
        else if (path == "sky.diffuse-ibl-strength")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.DiffuseIblStrength,
                1.f,
                0.f,
                4.f,
                value,
                error);
            resetVisibilityHistory = true;
        }
        else if (path == "sky.specular-ibl")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.EnableSpecularIbl,
                true,
                value,
                error);
            resetTemporalHistory = true;
        }
        else if (path == "sky.specular-ibl-strength")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                m_ui.SpecularIblStrength,
                1.f,
                0.f,
                4.f,
                value,
                error);
            resetTemporalHistory = true;
        }
        else if (path == "sky.environment-background")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.ShowEnvironmentBackground,
                true,
                value,
                error);
            resetTemporalHistory = true;
        }
        else if (path == "sky.ambient-fill.enabled")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.EnableAmbientFill,
                true,
                value,
                error);
            resetVisibilityHistory = true;
        }
        else
        {
            handled = false;
            error = "Internal Sky command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (handled &&
            operation != CommandValueOperation::Get)
        {
            if (resetVisibilityHistory)
                m_app->ResetImageBasedLightingHistory();
            else if (resetTemporalHistory)
                m_app->ResetImageBasedLightingHistory();
        }
        return handled;
    }

    std::shared_ptr<Light> GetDefaultCommandLight() const
    {
        const auto& lights = m_app->GetEditableLights();
        std::shared_ptr<Light> selected =
            m_app->GetPrimaryDirectionalLight();
        if (!selected ||
            std::find(lights.begin(), lights.end(), selected) ==
                lights.end())
        {
            selected = lights.empty() ? nullptr : lights.front();
        }
        return selected;
    }

    std::shared_ptr<Light> EnsureCommandSelectedLight()
    {
        const auto& lights = m_app->GetEditableLights();
        if (lights.empty())
        {
            m_SelectedLight.reset();
            return nullptr;
        }
        if (std::find(
                lights.begin(), lights.end(), m_SelectedLight) ==
            lights.end())
        {
            m_SelectedLight = GetDefaultCommandLight();
        }
        return m_SelectedLight;
    }

    const LightDefaultState& GetCommandLightDefaults(
        const std::shared_ptr<Light>& light)
    {
        const auto& lights = m_app->GetEditableLights();
        const auto selected = std::find(
            lights.begin(), lights.end(), light);
        const size_t index =
            static_cast<size_t>(std::distance(lights.begin(), selected));
        const std::string key =
            m_app->GetCurrentSceneName() + "\n" +
            std::to_string(index) + "\n" +
            light->GetName();
        const auto capture = [](const Light& source)
        {
            LightDefaultState result;
            result.type = source.GetLightType();
            result.direction = source.GetDirection();
            result.color = source.color;
            switch (result.type)
            {
            case LightType_Directional:
            {
                const auto& directional =
                    static_cast<const DirectionalLight&>(source);
                result.irradiance = directional.irradiance;
                result.angularSize = directional.angularSize;
                break;
            }
            case LightType_Point:
            {
                const auto& point =
                    static_cast<const PointLight&>(source);
                result.radius = point.radius;
                result.intensity = point.intensity;
                break;
            }
            case LightType_Spot:
            {
                const auto& spot =
                    static_cast<const SpotLight&>(source);
                result.radius = spot.radius;
                result.intensity = spot.intensity;
                result.innerAngle = spot.innerAngle;
                result.outerAngle = spot.outerAngle;
                break;
            }
            default:
                break;
            }
            return result;
        };
        return m_LightDefaults.try_emplace(
            key,
            capture(*light)).first->second;
    }

    static std::pair<float, float> GetCommandLightAngles(
        const double3& storedDirection,
        bool directional)
    {
        double3 direction = normalize(storedDirection);
        if (directional)
            direction = -direction;
        const float azimuth = degrees(float(
            std::atan2(direction.z, direction.x)));
        const float elevation = degrees(float(std::asin(
            std::clamp(direction.y, -1.0, 1.0))));
        return { azimuth, elevation };
    }

    static double3 MakeCommandLightDirection(
        float azimuthDegrees,
        float elevationDegrees,
        bool directional)
    {
        const double azimuth = radians(double(azimuthDegrees));
        const double elevation = radians(double(elevationDegrees));
        const double horizontal = std::cos(elevation);
        double3 direction(
            std::cos(azimuth) * horizontal,
            std::sin(elevation),
            std::sin(azimuth) * horizontal);
        if (directional)
            direction = -direction;
        return normalize(direction);
    }

    struct FlashlightFloatCommandBinding
    {
        std::string_view path;
        float FlashlightSettings::* member = nullptr;
        float minimum = 0.f;
        float maximum = 1.f;
    };

    inline static constexpr std::array<
        FlashlightFloatCommandBinding, 10> FlashlightFloatCommandBindings = {{
            {
                "light.selected.flashlight.hotspot-size",
                &FlashlightSettings::hotspotSize,
                FlashlightMinimumHotspotSize,
                FlashlightMaximumHotspotSize
            },
            {
                "light.selected.flashlight.hotspot-strength",
                &FlashlightSettings::hotspotStrength,
                0.f,
                FlashlightMaximumHotspotStrength
            },
            {
                "light.selected.flashlight.sway",
                &FlashlightSettings::swayDegrees,
                0.f,
                FlashlightMaximumSwayDegrees
            },
            {
                "light.selected.flashlight.aim-correction",
                &FlashlightSettings::aimCorrectionSeconds,
                FlashlightMinimumAimCorrectionSeconds,
                FlashlightMaximumAimCorrectionSeconds
            },
            {
                "light.selected.flashlight.brightness",
                &FlashlightSettings::peakIntensityCandela,
                FlashlightMinimumIntensityCandela,
                FlashlightMaximumIntensityCandela
            },
            {
                "light.selected.flashlight.beam-size",
                &FlashlightSettings::beamSizeDegrees,
                FlashlightMinimumBeamSizeDegrees,
                FlashlightMaximumBeamSizeDegrees
            },
            {
                "light.selected.flashlight.beam-roundness",
                &FlashlightSettings::beamRoundness,
                0.f,
                1.f
            },
            {
                "light.selected.flashlight.edge-softness",
                &FlashlightSettings::edgeSoftness,
                0.f,
                1.f
            },
            {
                "light.selected.flashlight.range",
                &FlashlightSettings::rangeMeters,
                FlashlightMinimumRangeMeters,
                FlashlightMaximumRangeMeters
            },
            {
                "light.selected.flashlight.camera-offset",
                &FlashlightSettings::cameraLateralOffsetMeters,
                FlashlightMinimumCameraLateralOffsetMeters,
                FlashlightMaximumCameraLateralOffsetMeters
            }
        }};

    bool DispatchFlashlightCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        if (path == "light.selected.flashlight.enabled")
        {
            return ApplyCommandBool(
                operation,
                arguments,
                path,
                m_ui.FlashlightEnabled,
                DefaultFlashlightEnabled,
                value,
                error);
        }

        FlashlightSettings candidate = m_ui.Flashlight;
        const FlashlightSettings defaults = DefaultFlashlightSettings;
        bool handled = true;
        if (path == "light.selected.color")
        {
            float3 color(
                candidate.colorLinearRed,
                candidate.colorLinearGreen,
                candidate.colorLinearBlue);
            const float3 defaultColor(
                defaults.colorLinearRed,
                defaults.colorLinearGreen,
                defaults.colorLinearBlue);
            handled = ApplyCommandFloat3(
                operation,
                arguments,
                path,
                color,
                defaultColor,
                0.f,
                1.f,
                value,
                error);
            candidate.colorLinearRed = color.x;
            candidate.colorLinearGreen = color.y;
            candidate.colorLinearBlue = color.z;
        }
        else if (path == "light.selected.flashlight.cast-shadows")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.castShadows,
                defaults.castShadows,
                value,
                error);
        }
        else if (path == "light.selected.flashlight.realistic")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.realisticLens,
                defaults.realisticLens,
                value,
                error);
        }
        else
        {
            handled = false;
            for (const FlashlightFloatCommandBinding& binding :
                FlashlightFloatCommandBindings)
            {
                if (path != binding.path)
                    continue;
                handled = ApplyCommandFloat(
                    operation,
                    arguments,
                    path,
                    candidate.*(binding.member),
                    defaults.*(binding.member),
                    binding.minimum,
                    binding.maximum,
                    value,
                    error);
                break;
            }
        }

        if (!handled)
        {
            error = "Internal flashlight command binding is missing for '" +
                std::string(path) + "'.";
            return false;
        }
        if (operation != CommandValueOperation::Get)
            m_ui.Flashlight = SanitizeFlashlightSettings(candidate);
        return true;
    }

    bool DispatchLightCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        const std::shared_ptr<Scene> scene = m_app->GetScene();
        if (!scene || !scene->GetSceneGraph())
        {
            error = "No loaded scene provides light controls.";
            return false;
        }
        const auto& lights = m_app->GetEditableLights();
        if (path == "light.selected")
        {
            std::shared_ptr<Light> currentSelected = m_SelectedLight;
            if (std::find(
                    lights.begin(), lights.end(), currentSelected) ==
                lights.end())
            {
                currentSelected = GetDefaultCommandLight();
            }
            std::shared_ptr<Light> selected = currentSelected;
            if (operation == CommandValueOperation::Get)
            {
                if (!selected)
                {
                    error = "The current scene has no lights.";
                    return false;
                }
                value = selected->GetName();
                return true;
            }
            if (operation == CommandValueOperation::Toggle)
            {
                error = std::string(path) + " is not boolean.";
                return false;
            }
            if (operation == CommandValueOperation::Reset)
            {
                selected = GetDefaultCommandLight();
            }
            else
            {
                const std::string requested =
                    JoinCommandArguments(arguments);
                int64_t requestedIndex = -1;
                const bool numeric =
                    TryParseCommandInteger(requested, requestedIndex);
                const std::string normalized =
                    NormalizeCommandAscii(requested, true);
                selected.reset();
                for (size_t index = 0u; index < lights.size(); ++index)
                {
                    const auto& light = lights[index];
                    const bool matches =
                        (numeric &&
                            requestedIndex ==
                                static_cast<int64_t>(index)) ||
                        NormalizeCommandAscii(
                            light->GetName(), true) == normalized;
                    if (!matches)
                        continue;
                    if (selected && selected != light)
                    {
                        error =
                            "Light name is ambiguous; use its zero-based "
                            "editable-light index.";
                        return false;
                    }
                    selected = light;
                }
            }
            if (!selected)
            {
                error = "Unknown light. Use '/list light.selected'.";
                return false;
            }
            if (selected == currentSelected)
                return RejectUnchangedCommandMutation(path, error);
            m_SelectedLight = selected;
            GetCommandLightDefaults(selected);
            value = selected->GetName();
            return true;
        }

        const std::shared_ptr<Light> selected =
            EnsureCommandSelectedLight();
        if (!selected)
        {
            error = "The current scene has no selected light.";
            return false;
        }
        const bool flashlightSelected = m_app->IsFlashlight(selected);
        const bool flashlightPath =
            path.find("light.selected.flashlight.") == 0u;
        if (flashlightSelected)
        {
            if (path == "light.selected.color" || flashlightPath)
            {
                return DispatchFlashlightCommandValue(
                    definition,
                    operation,
                    arguments,
                    value,
                    error);
            }
            error = std::string(path) +
                " is not an editable generic flashlight property.";
            return false;
        }
        if (flashlightPath)
        {
            error = std::string(path) +
                " requires selecting flashlight_1 first.";
            return false;
        }
        const LightDefaultState& defaults =
            GetCommandLightDefaults(selected);
        const int type = selected->GetLightType();
        const bool directional = type == LightType_Directional;
        const bool spot = type == LightType_Spot;
        const bool pointOrSpot =
            type == LightType_Point || spot;

        if (path == "light.selected.azimuth" ||
            path == "light.selected.elevation")
        {
            if (!directional && !spot)
            {
                error =
                    std::string(path) +
                    " requires a directional or spot light.";
                return false;
            }
            auto [azimuth, elevation] = GetCommandLightAngles(
                selected->GetDirection(), directional);
            const auto [defaultAzimuth, defaultElevation] =
                GetCommandLightAngles(
                    defaults.direction, directional);
            float& current = path == "light.selected.azimuth"
                ? azimuth
                : elevation;
            const float defaultValue =
                path == "light.selected.azimuth"
                    ? defaultAzimuth
                    : defaultElevation;
            if (!ApplyCommandFloat(
                    operation,
                    arguments,
                    path,
                    current,
                    defaultValue,
                    path == "light.selected.azimuth" ? -180.f : -90.f,
                    path == "light.selected.azimuth" ? 180.f : 90.f,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
            {
                selected->SetDirection(MakeCommandLightDirection(
                    azimuth,
                    elevation,
                    directional));
                if (directional)
                    m_app->ResetImageBasedLightingHistory();
            }
            return true;
        }
        if (path == "light.selected.color")
        {
            return ApplyCommandFloat3(
                operation,
                arguments,
                path,
                selected->color,
                defaults.color,
                0.f,
                1.f,
                value,
                error);
        }

        if (path == "light.selected.irradiance")
        {
            if (!directional)
            {
                error =
                    "light.selected.irradiance requires a directional light.";
                return false;
            }
            auto& light = static_cast<DirectionalLight&>(*selected);
            return ApplyCommandFloat(
                operation,
                arguments,
                path,
                light.irradiance,
                defaults.irradiance,
                0.f,
                100.f,
                value,
                error);
        }
        if (path == "light.selected.angular-size")
        {
            if (!directional)
            {
                error =
                    "light.selected.angular-size requires a directional "
                    "light.";
                return false;
            }
            auto& light = static_cast<DirectionalLight&>(*selected);
            const bool handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                light.angularSize,
                defaults.angularSize,
                0.f,
                20.f,
                value,
                error);
            if (handled && operation != CommandValueOperation::Get)
                m_app->ResetImageBasedLightingHistory();
            return handled;
        }
        if (path == "light.selected.radius" ||
            path == "light.selected.intensity")
        {
            if (!pointOrSpot)
            {
                error =
                    std::string(path) +
                    " requires a point or spot light.";
                return false;
            }
            float* current = nullptr;
            const float* defaultValue = nullptr;
            if (type == LightType_Point)
            {
                auto& light = static_cast<PointLight&>(*selected);
                current = path == "light.selected.radius"
                    ? &light.radius
                    : &light.intensity;
            }
            else
            {
                auto& light = static_cast<SpotLight&>(*selected);
                current = path == "light.selected.radius"
                    ? &light.radius
                    : &light.intensity;
            }
            defaultValue = path == "light.selected.radius"
                ? &defaults.radius
                : &defaults.intensity;
            return ApplyCommandFloat(
                operation,
                arguments,
                path,
                *current,
                *defaultValue,
                path == "light.selected.radius" ? 0.01f : 0.f,
                path == "light.selected.radius" ? 1.f : 100.f,
                value,
                error);
        }
        if (path == "light.selected.inner-angle" ||
            path == "light.selected.outer-angle")
        {
            if (!spot)
            {
                error =
                    std::string(path) + " requires a spot light.";
                return false;
            }
            auto& light = static_cast<SpotLight&>(*selected);
            float candidate = path == "light.selected.inner-angle"
                ? light.innerAngle
                : light.outerAngle;
            if (!ApplyCommandFloat(
                    operation,
                    arguments,
                    path,
                    candidate,
                    path == "light.selected.inner-angle"
                        ? defaults.innerAngle
                        : defaults.outerAngle,
                    0.f,
                    180.f,
                    value,
                    error))
            {
                return false;
            }
            if (operation != CommandValueOperation::Get)
            {
                if (path == "light.selected.inner-angle" &&
                    candidate > light.outerAngle)
                {
                    error =
                        "The inner spot angle cannot exceed the outer angle.";
                    return false;
                }
                if (path == "light.selected.outer-angle" &&
                    candidate < light.innerAngle)
                {
                    error =
                        "The outer spot angle cannot be below the inner angle.";
                    return false;
                }
                if (path == "light.selected.inner-angle")
                    light.innerAngle = candidate;
                else
                    light.outerAngle = candidate;
            }
            return true;
        }

        error = "Internal Light command binding is missing for '" +
            std::string(path) + "'.";
        return false;
    }

    bool DispatchDirectionalShadowCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        DirectionalShadowSettings candidate = m_ui.DirectionalShadows;
        const DirectionalShadowSettings factoryDefaults;
        bool handled = true;

        if (path == "shadows.ratio-estimator.enabled")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.ratioEstimator.enabled,
                factoryDefaults.ratioEstimator.enabled,
                value,
                error);
        }
        else if (path == "shadows.ratio-estimator.samples-per-pixel")
        {
            static constexpr std::array<
                std::pair<std::string_view, int32_t>, 7> Options = {{
                    { "1", 0 },
                    { "2", 1 },
                    { "4", 2 },
                    { "8", 3 },
                    { "16", 4 },
                    { "32", 5 },
                    { "64", 6 }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.ratioEstimator.sampleRateLog2,
                factoryDefaults.ratioEstimator.sampleRateLog2,
                Options,
                value,
                error);
        }
        else if (path == "shadows.ratio-estimator.hard-shadows")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.ratioEstimator.hardShadows,
                factoryDefaults.ratioEstimator.hardShadows,
                value,
                error);
        }
        else if (path == "shadows.ratio-estimator.noise-pattern")
        {
            static constexpr std::array<std::pair<std::string_view,
                HeitzRatioEstimatorNoisePattern>, 2> Options = {{
                    { "permutated-white-noise",
                        HeitzRatioEstimatorNoisePattern::PermutatedWhiteNoise },
                    { "void-cluster-blue-noise",
                        HeitzRatioEstimatorNoisePattern::VoidClusterBlueNoise }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.ratioEstimator.noisePattern,
                factoryDefaults.ratioEstimator.noisePattern,
                Options,
                value,
                error);
        }
        else if (path == "shadows.ratio-estimator.animate-samples")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.ratioEstimator.animateSamples,
                factoryDefaults.ratioEstimator.animateSamples,
                value,
                error);
        }
        else if (path == "shadows.ratio-estimator.ray-bias")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.ratioEstimator.rayBias,
                factoryDefaults.ratioEstimator.rayBias,
                0.f,
                HeitzRatioEstimatorMaximumRayBias,
                value,
                error);
        }
        else
        {
            handled = false;
            error =
                "Internal Directional Shadows command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled)
            return false;
        if (operation == CommandValueOperation::Get)
            return true;
        if (!IsHeitzRatioEstimatorConfigurationSupported(
                candidate.ratioEstimator))
        {
            error =
                "The requested ratio-estimator shadow configuration is not supported.";
            return false;
        }
        if (candidate.ratioEstimator.enabled &&
            !m_app->HasPrimaryDirectionalLight())
        {
            error =
                "Directional shadow techniques require a primary directional light.";
            return false;
        }
        if (candidate.ratioEstimator.enabled &&
            !m_app->SupportsHeitzRatioEstimatorShadows())
        {
            error =
                "Heitz ratio-estimator shadows require DXR 1.1 support and "
                "single-sample rendering.";
            return false;
        }

        const bool changed =
            candidate.ratioEstimator.enabled !=
                m_ui.DirectionalShadows.ratioEstimator.enabled ||
            candidate.ratioEstimator.hardShadows !=
                m_ui.DirectionalShadows.ratioEstimator.hardShadows ||
            candidate.ratioEstimator.sampleRateLog2 !=
                m_ui.DirectionalShadows.ratioEstimator.sampleRateLog2 ||
            candidate.ratioEstimator.noisePattern !=
                m_ui.DirectionalShadows.ratioEstimator.noisePattern ||
            candidate.ratioEstimator.animateSamples !=
                m_ui.DirectionalShadows.ratioEstimator.animateSamples ||
            candidate.ratioEstimator.rayBias !=
                m_ui.DirectionalShadows.ratioEstimator.rayBias;
        m_ui.DirectionalShadows = candidate;
        if (changed)
            m_app->ResetImageBasedLightingHistory();
        return true;
    }

    static bool IsSameCommandScreenSpaceShadowConfiguration(
        const ScreenSpaceDirectionalShadowSettings& left,
        const ScreenSpaceDirectionalShadowSettings& right)
    {
        return left.length == right.length &&
            left.surfaceThickness == right.surfaceThickness &&
            left.bilinearThreshold == right.bilinearThreshold &&
            left.shadowContrast == right.shadowContrast &&
            left.hardShadowSamples == right.hardShadowSamples &&
            left.fadeOutSamples == right.fadeOutSamples &&
            left.ignoreEdgePixels == right.ignoreEdgePixels &&
            left.usePrecisionOffset == right.usePrecisionOffset &&
            left.bilinearSamplingOffsetMode ==
                right.bilinearSamplingOffsetMode &&
            left.useEarlyOut == right.useEarlyOut;
    }

    static void ReconcileCommandScreenSpaceShadowPreset(
        ScreenSpaceDirectionalShadowSettings& settings)
    {
        static constexpr ScreenSpaceShadowPreset Presets[] = {
            ScreenSpaceShadowPreset::Default,
            ScreenSpaceShadowPreset::Long,
            ScreenSpaceShadowPreset::MaximumValidation
        };
        for (const ScreenSpaceShadowPreset preset : Presets)
        {
            ScreenSpaceDirectionalShadowSettings profileSettings = settings;
            ApplyScreenSpaceShadowPreset(profileSettings, preset);
            if (IsSameCommandScreenSpaceShadowConfiguration(
                    settings, profileSettings))
            {
                settings.preset = preset;
                return;
            }
        }
        settings.preset = ScreenSpaceShadowPreset::Custom;
    }

    bool DispatchScreenSpaceShadowCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        if (path != "shadows.screen-space-directional.enabled" &&
            operation != CommandValueOperation::Get &&
            !m_app->HasPrimaryDirectionalLight())
        {
            error =
                "Screen-space directional shadow settings require a "
                "primary directional light.";
            return false;
        }
        ScreenSpaceDirectionalShadowSettings candidate =
            m_ui.ScreenSpaceDirectionalShadows;
        const ScreenSpaceDirectionalShadowSettings factoryDefaults;
        bool handled = true;

        if (path == "shadows.screen-space-directional.enabled")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enabled,
                factoryDefaults.enabled,
                value,
                error);
            if (handled && operation != CommandValueOperation::Get &&
                candidate.enabled &&
                !m_app->HasPrimaryDirectionalLight())
            {
                error =
                    "Screen-space directional shadows require a "
                    "primary directional light.";
                return false;
            }
        }
        else if (path == "shadows.screen-space-directional.profile")
        {
            static constexpr std::array<
                std::pair<std::string_view, ScreenSpaceShadowPreset>, 4>
                Options = {{
                    { "default", ScreenSpaceShadowPreset::Default },
                    { "long", ScreenSpaceShadowPreset::Long },
                    {
                        "maximum-validation",
                        ScreenSpaceShadowPreset::MaximumValidation
                    },
                    { "custom", ScreenSpaceShadowPreset::Custom }
                }};
            ScreenSpaceShadowPreset selected = candidate.preset;
            if (operation != CommandValueOperation::Get &&
                selected != ScreenSpaceShadowPreset::Custom)
            {
                ScreenSpaceDirectionalShadowSettings profileSettings =
                    candidate;
                ApplyScreenSpaceShadowPreset(profileSettings, selected);
                if (!IsSameCommandScreenSpaceShadowConfiguration(
                        candidate, profileSettings))
                {
                    selected = ScreenSpaceShadowPreset::Custom;
                }
            }
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                selected,
                ScreenSpaceShadowPreset::Default,
                Options,
                value,
                error);
            if (handled && operation != CommandValueOperation::Get)
                ApplyScreenSpaceShadowPreset(candidate, selected);
        }
        else if (path == "shadows.screen-space-directional.length")
        {
            static constexpr std::array<
                std::pair<std::string_view, ScreenSpaceShadowLength>, 5>
                Options = {{
                    { "60", ScreenSpaceShadowLength::Pixels60 },
                    { "120", ScreenSpaceShadowLength::Pixels120 },
                    { "240", ScreenSpaceShadowLength::Pixels240 },
                    { "480", ScreenSpaceShadowLength::Pixels480 },
                    { "960", ScreenSpaceShadowLength::Pixels960 }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.length,
                factoryDefaults.length,
                Options,
                value,
                error);
        }
        else if (path == "shadows.screen-space-directional.surface-thickness")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.surfaceThickness,
                factoryDefaults.surfaceThickness,
                0.f,
                0.05f,
                value,
                error);
        }
        else if (path == "shadows.screen-space-directional.bilinear-threshold")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.bilinearThreshold,
                factoryDefaults.bilinearThreshold,
                0.f,
                0.1f,
                value,
                error);
        }
        else if (path == "shadows.screen-space-directional.contrast")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.shadowContrast,
                factoryDefaults.shadowContrast,
                1.f,
                16.f,
                value,
                error);
        }
        else if (path == "shadows.screen-space-directional.hard-samples")
        {
            static constexpr std::array<
                std::pair<std::string_view, uint32_t>, 3> Options = {{
                    { "0", 0u },
                    { "4", 4u },
                    { "8", 8u }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.hardShadowSamples,
                factoryDefaults.hardShadowSamples,
                Options,
                value,
                error);
        }
        else if (path == "shadows.screen-space-directional.fade-samples")
        {
            static constexpr std::array<
                std::pair<std::string_view, uint32_t>, 3> Options = {{
                    { "0", 0u },
                    { "8", 8u },
                    { "16", 16u }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.fadeOutSamples,
                factoryDefaults.fadeOutSamples,
                Options,
                value,
                error);
        }
        else if (path == "shadows.screen-space-directional.ignore-edge-pixels")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.ignoreEdgePixels,
                factoryDefaults.ignoreEdgePixels,
                value,
                error);
        }
        else if (path == "shadows.screen-space-directional.precision-offset")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.usePrecisionOffset,
                factoryDefaults.usePrecisionOffset,
                value,
                error);
        }
        else if (path == "shadows.screen-space-directional.bilinear-offset-mode")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.bilinearSamplingOffsetMode,
                factoryDefaults.bilinearSamplingOffsetMode,
                value,
                error);
        }
        else if (path == "shadows.screen-space-directional.early-out")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.useEarlyOut,
                factoryDefaults.useEarlyOut,
                value,
                error);
        }
        else
        {
            handled = false;
            error =
                "Internal screen-space directional shadow command binding is missing for '" +
                std::string(path) + "'.";
        }

        if (!handled)
            return false;
        if (operation != CommandValueOperation::Get)
        {
            if (!IsScreenSpaceShadowConfigurationSupported(candidate))
            {
                error =
                    "The requested screen-space directional shadow sample tuple is not supported "
                    "or hard plus fade samples exceed the selected length.";
                return false;
            }
            if (path != "shadows.screen-space-directional.profile" &&
                operation == CommandValueOperation::Reset)
            {
                ReconcileCommandScreenSpaceShadowPreset(candidate);
            }
            else if (path != "shadows.screen-space-directional.profile")
            {
                candidate.preset = ScreenSpaceShadowPreset::Custom;
            }
            const bool changed =
                !IsSameCommandScreenSpaceShadowConfiguration(
                    m_ui.ScreenSpaceDirectionalShadows, candidate) ||
                m_ui.ScreenSpaceDirectionalShadows.enabled !=
                    candidate.enabled;
            m_ui.ScreenSpaceDirectionalShadows = candidate;
            if (changed)
                m_app->ResetImageBasedLightingHistory();
        }
        return true;
    }


    static bool IsCommandMaterialTransmissive(MaterialDomain domain)
    {
        return domain == MaterialDomain::Transmissive ||
            domain == MaterialDomain::TransmissiveAlphaTested ||
            domain == MaterialDomain::TransmissiveAlphaBlended;
    }

    static bool IsCommandMaterialAlphaTested(MaterialDomain domain)
    {
        return domain == MaterialDomain::AlphaTested ||
            domain == MaterialDomain::TransmissiveAlphaTested;
    }

    static bool IsCommandMaterialAlphaBlended(MaterialDomain domain)
    {
        return domain == MaterialDomain::AlphaBlended ||
            domain == MaterialDomain::TransmissiveAlphaBlended;
    }

    bool DispatchMaterialCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        const std::string_view path = definition.name;
        const std::shared_ptr<Scene> scene = m_app->GetScene();
        if (!scene || !scene->GetSceneGraph())
        {
            error = "No loaded scene provides material controls.";
            return false;
        }
        const auto& materials =
            scene->GetSceneGraph()->GetMaterials();

        if (path == "material.selected")
        {
            if (operation == CommandValueOperation::Get)
            {
                if (!m_ui.SelectedMaterial)
                {
                    value = "none";
                    return true;
                }
                value = std::to_string(
                    m_ui.SelectedMaterial->materialID) +
                    " " + m_ui.SelectedMaterial->name;
                return true;
            }
            if (operation != CommandValueOperation::Set)
            {
                error =
                    "material.selected supports get and set only.";
                return false;
            }

            const std::string requested =
                JoinCommandArguments(arguments);
            if (requested.empty())
            {
                error =
                    "material.selected expects a material id or unique "
                    "name.";
                return false;
            }
            int64_t requestedId = -1;
            const bool numeric =
                TryParseCommandInteger(requested, requestedId);
            const std::string normalized =
                NormalizeCommandAscii(requested, true);
            std::shared_ptr<Material> match;
            for (const std::shared_ptr<Material>& material : materials)
            {
                if (!material)
                    continue;
                const bool matches =
                    (numeric &&
                        requestedId ==
                            static_cast<int64_t>(
                                material->materialID)) ||
                    NormalizeCommandAscii(
                        material->name, true) == normalized;
                if (!matches)
                    continue;
                if (match && match != material)
                {
                    error =
                        "Material selection is ambiguous; use a unique "
                        "runtime material id.";
                    return false;
                }
                match = material;
            }
            if (!match)
            {
                error =
                    "Unknown material. Use '/list material.selected'.";
                return false;
            }
            if (match == m_ui.SelectedMaterial)
                return RejectUnchangedCommandMutation(path, error);
            m_ui.SelectedMaterial = match;
            value = std::to_string(match->materialID) +
                " " + match->name;
            return true;
        }

        const std::shared_ptr<Material> material =
            m_ui.SelectedMaterial;
        bool materialStillOwnedByScene = false;
        if (material)
        {
            for (const std::shared_ptr<Material>& sceneMaterial : materials)
            {
                if (sceneMaterial == material)
                {
                    materialStillOwnedByScene = true;
                    break;
                }
            }
        }
        if (!materialStillOwnedByScene)
        {
            error =
                "No scene material is selected. Use "
                "'/list material.selected' and "
                "'/set material.selected <id-or-name>'.";
            return false;
        }

        Material candidate = *material;
        bool handled = true;
        if (path == "material.selected.domain")
        {
            static constexpr std::array<
                std::pair<std::string_view, MaterialDomain>, 6>
                Options = {{
                    { "opaque", MaterialDomain::Opaque },
                    { "alpha-tested", MaterialDomain::AlphaTested },
                    { "alpha-blended", MaterialDomain::AlphaBlended },
                    { "transmissive", MaterialDomain::Transmissive },
                    {
                        "transmissive-alpha-tested",
                        MaterialDomain::TransmissiveAlphaTested
                    },
                    {
                        "transmissive-alpha-blended",
                        MaterialDomain::TransmissiveAlphaBlended
                    }
                }};
            handled = ApplyCommandEnum(
                operation,
                arguments,
                path,
                candidate.domain,
                candidate.domain,
                Options,
                value,
                error);
        }
        else if (path == "material.selected.double-sided")
        {
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.doubleSided,
                candidate.doubleSided,
                value,
                error);
        }
        else if (path ==
            "material.selected.base-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.baseOrDiffuseTexture)
            {
                error =
                    "The selected material has no base or diffuse "
                    "texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableBaseOrDiffuseTexture,
                candidate.enableBaseOrDiffuseTexture,
                value,
                error);
        }
        else if (path == "material.selected.base-color")
        {
            handled = ApplyCommandFloat3(
                operation,
                arguments,
                path,
                candidate.baseOrDiffuseColor,
                candidate.baseOrDiffuseColor,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.metal-specular-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.metalRoughOrSpecularTexture)
            {
                error =
                    "The selected material has no metal-rough or "
                    "specular texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableMetalRoughOrSpecularTexture,
                candidate.enableMetalRoughOrSpecularTexture,
                value,
                error);
        }
        else if (path == "material.selected.specular-color")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.useSpecularGlossModel)
            {
                error =
                    "Specular color requires a specular-gloss material.";
                return false;
            }
            handled = ApplyCommandFloat3(
                operation,
                arguments,
                path,
                candidate.specularColor,
                candidate.specularColor,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path == "material.selected.metalness")
        {
            if (operation != CommandValueOperation::Get &&
                candidate.useSpecularGlossModel)
            {
                error =
                    "Metalness requires a metal-rough material.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.metalness,
                candidate.metalness,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path == "material.selected.roughness")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.roughness,
                candidate.roughness,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path == "material.selected.opacity")
        {
            if (operation != CommandValueOperation::Get &&
                !IsCommandMaterialAlphaBlended(candidate.domain))
            {
                error =
                    "Opacity is available only for alpha-blended "
                    "materials.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.opacity,
                candidate.opacity,
                0.f,
                candidate.baseOrDiffuseTexture ? 2.f : 1.f,
                value,
                error);
        }
        else if (path == "material.selected.alpha-cutoff")
        {
            if (operation != CommandValueOperation::Get &&
                (!IsCommandMaterialAlphaTested(candidate.domain) ||
                 !candidate.baseOrDiffuseTexture))
            {
                error =
                    "Alpha cutoff requires an alpha-tested material "
                    "with a base or diffuse texture.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.alphaCutoff,
                candidate.alphaCutoff,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.normal-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.normalTexture)
            {
                error =
                    "The selected material has no normal texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableNormalTexture,
                candidate.enableNormalTexture,
                value,
                error);
        }
        else if (path == "material.selected.normal-scale")
        {
            const Material* original =
                m_app->GetOriginalMaterial(material);
            const float defaultScale =
                original
                    ? original->normalTextureScale
                    : 1.f;
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.normalTextureScale,
                defaultScale,
                -2.f,
                2.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.occlusion-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.occlusionTexture)
            {
                error =
                    "The selected material has no occlusion texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableOcclusionTexture,
                candidate.enableOcclusionTexture,
                value,
                error);
        }
        else if (path ==
            "material.selected.occlusion-strength")
        {
            if (operation != CommandValueOperation::Get &&
                (!candidate.occlusionTexture ||
                 !candidate.enableOcclusionTexture))
            {
                error =
                    "Occlusion strength requires an enabled occlusion "
                    "texture.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.occlusionStrength,
                candidate.occlusionStrength,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.emissive-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.emissiveTexture)
            {
                error =
                    "The selected material has no emissive texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableEmissiveTexture,
                candidate.enableEmissiveTexture,
                value,
                error);
        }
        else if (path == "material.selected.emissive-color")
        {
            handled = ApplyCommandFloat3(
                operation,
                arguments,
                path,
                candidate.emissiveColor,
                candidate.emissiveColor,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.emissive-intensity")
        {
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.emissiveIntensity,
                candidate.emissiveIntensity,
                0.f,
                1000.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.transmission-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                (!IsCommandMaterialTransmissive(candidate.domain) ||
                 !candidate.transmissionTexture))
            {
                error =
                    "Transmission texture control requires a "
                    "transmissive material with a transmission texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableTransmissionTexture,
                candidate.enableTransmissionTexture,
                value,
                error);
        }
        else if (path ==
            "material.selected.transmission-factor")
        {
            if (operation != CommandValueOperation::Get &&
                !IsCommandMaterialTransmissive(candidate.domain))
            {
                error =
                    "Transmission factor requires a transmissive "
                    "material.";
                return false;
            }
            handled = ApplyCommandFloat(
                operation,
                arguments,
                path,
                candidate.transmissionFactor,
                candidate.transmissionFactor,
                0.f,
                1.f,
                value,
                error);
        }
        else if (path ==
            "material.selected.alpha-mask-texture-enabled")
        {
            if (operation != CommandValueOperation::Get &&
                !candidate.opacityTexture)
            {
                error =
                    "The selected material has no opacity texture.";
                return false;
            }
            handled = ApplyCommandBool(
                operation,
                arguments,
                path,
                candidate.enableOpacityTexture,
                candidate.enableOpacityTexture,
                value,
                error);
        }
        else
        {
            handled = false;
        }

        if (!handled)
        {
            if (error.empty())
            {
                error =
                    "Internal Material command binding is missing for '" +
                    std::string(path) + "'.";
            }
            return false;
        }
        if (operation == CommandValueOperation::Get)
            return true;

        *material = candidate;
        m_app->NotifyMaterialCommandChanged(material);
        return true;
    }

    bool DispatchCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        switch (definition.section)
        {
        case UiSettingsCommandSection::Ui:
            return DispatchUiCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::General:
            return DispatchGeneralCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::Representation:
            return DispatchRepresentationCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::Visibility:
            return DispatchVisibilityCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::Aliasing:
            return DispatchAliasingCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::Debug:
            return DispatchDebugCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::Sky:
            return DispatchSkyCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::Lights:
            return DispatchLightCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::DirectionalShadows:
            return DispatchDirectionalShadowCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::ScreenSpaceDirectionalShadows:
            return DispatchScreenSpaceShadowCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::Materials:
            return DispatchMaterialCommandValue(
                definition, operation, arguments, value, error);
        case UiSettingsCommandSection::Footer:
        case UiSettingsCommandSection::Count:
            break;
        }
        error = "Internal value-command section dispatch is missing for '" +
            std::string(definition.name) + "'.";
        return false;
    }

    bool DispatchCommandAction(
        const UiSettingsCommandDefinition& definition,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error)
    {
        if (!arguments.empty())
        {
            error = std::string(definition.name) +
                " does not accept arguments.";
            return false;
        }

        const std::string_view action = definition.name;
        if (action == "open-scene-folder")
        {
            const std::filesystem::path sceneFolder =
                m_app->GetSceneDir();
            const HINSTANCE result = ShellExecuteW(
                nullptr,
                L"open",
                sceneFolder.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(result) <= 32)
            {
                error = "Windows could not open the scene folder.";
                return false;
            }
            value = "opened";
            return true;
        }
        if (action == "reset-settings")
        {
            m_app->ResetAllRendererSettings();
            value = "restored";
            return true;
        }
        if (action == "screenshot")
        {
            m_ui.CopyScreenshotToClipboard = true;
            value = "queued";
            return true;
        }
        if (action == "restart")
        {
            g_RestartRequested = true;
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            value = "requested";
            return true;
        }

        error = "Internal action binding is missing for '" +
            std::string(action) + "'.";
        return false;
    }

    void AppendDynamicCommandValues(
        std::string_view path,
        std::vector<std::string>& values) const
    {
        if (path == "gpu.adapter")
        {
            for (const GpuAdapterChoice& adapter :
                m_ui.GpuAdapterChoices)
            {
                values.push_back(std::to_string(adapter.adapterIndex));
                values.push_back(adapter.name);
            }
        }
        else if (path == "scene.current")
        {
            for (const SceneCatalogEntry& scene :
                m_app->GetAvailableScenes())
            {
                values.push_back(scene.FileName);
                values.push_back(scene.DisplayName);
            }
        }
        else if (path == "sky.environment")
        {
            for (uint32_t index = 0u;
                index < uint32_t(ImageBasedLightingSource::Count);
                ++index)
            {
                values.push_back(std::to_string(index));
                values.push_back(
                    GetImageBasedLightingSourceInfo(
                        ImageBasedLightingSource(index)).displayName);
            }
        }
        else if (path == "light.selected")
        {
            const auto& lights = m_app->GetEditableLights();
            for (size_t index = 0u;
                index < lights.size();
                ++index)
            {
                values.push_back(std::to_string(index));
                if (lights[index])
                    values.push_back(lights[index]->GetName());
            }
        }
        else if (path == "material.selected")
        {
            const std::shared_ptr<Scene> scene = m_app->GetScene();
            if (scene && scene->GetSceneGraph())
            {
                for (const std::shared_ptr<Material>& material :
                    scene->GetSceneGraph()->GetMaterials())
                {
                    if (!material)
                        continue;
                    values.push_back(
                        std::to_string(material->materialID));
                    values.push_back(material->name);
                }
            }
        }
    }

    std::vector<std::string> GetCommandCompletionCandidates(
        const UiCommandCompletionToken& completion) const
    {
        std::vector<std::string> candidates;
        const auto appendCandidate =
            [&candidates, &completion](std::string_view candidate)
        {
            if (candidate.empty() ||
                !UiCommandCompletionMatches(
                    candidate, completion.prefix))
            {
                return;
            }
            candidates.emplace_back(candidate);
        };

        switch (completion.target)
        {
        case UiCommandCompletionTarget::Verb:
            for (const std::string_view candidate :
                UiCommandVerbCompletionCandidates)
            {
                appendCandidate(candidate);
            }
            break;
        case UiCommandCompletionTarget::HelpTopic:
            for (const std::string_view candidate :
                { "commands", "settings", "skins" })
            {
                appendCandidate(candidate);
            }
            break;
        case UiCommandCompletionTarget::ListPrefix:
        case UiCommandCompletionTarget::Path:
            for (const UiSettingsCommandBinding& binding :
                UiSettingsCommandBindings)
            {
                if (completion.target ==
                        UiCommandCompletionTarget::Path &&
                    binding.kind == UiSettingsCommandKind::Action)
                {
                    continue;
                }
                appendCandidate(binding.name);
            }
            break;
        case UiCommandCompletionTarget::Action:
            for (const UiSettingsCommandBinding& binding :
                UiSettingsCommandBindings)
            {
                if (binding.kind ==
                    UiSettingsCommandKind::Action)
                {
                    appendCandidate(binding.name);
                }
            }
            appendCandidate(UiReloadShadersCommandAction);
            break;
        case UiCommandCompletionTarget::Value:
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition(completion.valuePath);
            if (!definition ||
                definition->kind == UiSettingsCommandKind::Action)
            {
                break;
            }

            std::string_view remaining = definition->domain;
            while (!remaining.empty())
            {
                const size_t separator = remaining.find('|');
                const std::string_view candidate =
                    remaining.substr(0u, separator);
                if (candidate.find(' ') == std::string_view::npos &&
                    candidate.find(';') == std::string_view::npos)
                {
                    appendCandidate(candidate);
                }
                if (separator == std::string_view::npos)
                    break;
                remaining.remove_prefix(separator + 1u);
            }
            if (definition->dynamic ||
                definition->name == "sky.environment")
            {
                std::vector<std::string> dynamicValues;
                AppendDynamicCommandValues(
                    definition->name, dynamicValues);
                for (const std::string& candidate : dynamicValues)
                    appendCandidate(candidate);
            }
            break;
        }
        case UiCommandCompletionTarget::Argument:
            break;
        }

        std::sort(candidates.begin(), candidates.end());
        candidates.erase(
            std::unique(candidates.begin(), candidates.end()),
            candidates.end());
        return candidates;
    }

    void ExecuteUiCommand(const UiCommand& command)
    {
        if (command.verb == UiCommandVerb::Help)
        {
            const std::string topic = command.arguments.empty()
                ? "commands"
                : NormalizeCommandAscii(
                    command.arguments.front(), true);
            if (topic == "skins")
            {
                SetCommandResult(
                    "Skins: Amp is the animated UVSR interface; OG is "
                    "stock ImGui with UI animations disabled. "
                    "Use /skin [amp|og].");
            }
            else if (topic == "settings")
            {
                SetCommandResult(
                    "Use 'list [prefix]' to discover settings and "
                    "supported verbs, then get, set, toggle, reset, or "
                    "run the listed path. Tab completes.");
            }
            else
            {
                SetCommandResult(
                    "Commands: help [topic], list [prefix], get <path>, "
                    "set <path> <value>, toggle <path>, reset <path|all>, "
                    "run <action>. Shortcuts: skin, ui, scene, camera, "
                    "camera-location, reload-shaders.");
            }
            return;
        }

        if (command.verb == UiCommandVerb::List)
        {
            const std::string prefix = command.arguments.empty()
                ? std::string{}
                : NormalizeCommandAscii(
                    command.arguments.front());
            std::string listing;
            for (const UiSettingsCommandDefinition& definition :
                UiSettingsCommandCatalog)
            {
                if (!StartsWithCommandPrefix(
                        definition.name, prefix))
                {
                    continue;
                }
                if (!listing.empty())
                    listing += "\n";
                listing += definition.name;
                listing += " [";
                listing += GetSettingsCommandVerbList(definition);
                listing += "] - ";
                listing += definition.domain;

                std::vector<std::string> dynamicValues;
                AppendDynamicCommandValues(
                    definition.name, dynamicValues);
                if (!dynamicValues.empty())
                {
                    std::sort(
                        dynamicValues.begin(), dynamicValues.end());
                    dynamicValues.erase(
                        std::unique(
                            dynamicValues.begin(),
                            dynamicValues.end()),
                        dynamicValues.end());
                    listing += "\n  values: ";
                    for (size_t index = 0u;
                        index < dynamicValues.size();
                        ++index)
                    {
                        if (index > 0u)
                            listing += " | ";
                        listing += dynamicValues[index];
                    }
                }
            }
            if (listing.empty())
            {
                SetCommandResult(
                    "No Settings command matches '" + prefix + "'.",
                    true);
            }
            else
            {
                SetCommandResult(std::move(listing));
            }
            return;
        }

        if (command.verb == UiCommandVerb::Run &&
            NormalizeCommandAscii(command.action) ==
                UiReloadShadersCommandAction)
        {
            if (!command.arguments.empty())
            {
                SetCommandResult(
                    "reload-shaders does not accept arguments.",
                    true);
                return;
            }
            if (m_app->IsSceneBusy())
            {
                SetCommandResult(
                    "Shaders cannot reload while a scene is loading.",
                    true);
                return;
            }
            m_ui.ShaderReloadRequested = true;
            SetCommandResult("reload-shaders = requested");
            return;
        }

        if (command.verb == UiCommandVerb::Run)
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition(command.action);
            if (!definition ||
                definition->kind != UiSettingsCommandKind::Action)
            {
                SetCommandResult(
                    "Unknown Settings action '" + command.action +
                    "'. Use '/list'.",
                    true);
                return;
            }
            if (!definition->Supports(UiSettingsCommandVerb::Run))
            {
                SetCommandResult(
                    std::string(definition->name) + " supports [" +
                    GetSettingsCommandVerbList(*definition) + "].",
                    true);
                return;
            }
            std::string error;
            if (!CheckCommandMutationAllowed(*definition, error))
            {
                SetCommandResult(std::move(error), true);
                return;
            }
            std::string value;
            if (!DispatchCommandAction(
                    *definition,
                    command.arguments,
                    value,
                    error))
            {
                SetCommandResult(std::move(error), true);
                return;
            }
            SetCommandResult(
                std::string(definition->name) + " = " + value);
            return;
        }

        if (command.verb == UiCommandVerb::Reset &&
            NormalizeCommandAscii(command.path) == "all")
        {
            const UiSettingsCommandDefinition* definition =
                FindSettingsCommandDefinition("reset-settings");
            std::string error;
            if (!definition ||
                !CheckCommandMutationAllowed(*definition, error))
            {
                SetCommandResult(
                    error.empty()
                        ? "The Settings reset action is unavailable."
                        : std::move(error),
                    true);
                return;
            }
            std::string value;
            if (!DispatchCommandAction(
                    *definition, {}, value, error))
            {
                SetCommandResult(std::move(error), true);
                return;
            }
            SetCommandResult("all = " + value);
            return;
        }

        const UiSettingsCommandDefinition* definition =
            FindSettingsCommandDefinition(command.path);
        if (!definition ||
            definition->kind == UiSettingsCommandKind::Action)
        {
            SetCommandResult(
                "Unknown Settings path '" + command.path +
                "'. Use '/list'.",
                true);
            return;
        }

        CommandValueOperation operation =
            CommandValueOperation::Get;
        switch (command.verb)
        {
        case UiCommandVerb::Get:
            operation = CommandValueOperation::Get;
            break;
        case UiCommandVerb::Set:
            operation = CommandValueOperation::Set;
            break;
        case UiCommandVerb::Toggle:
            operation = CommandValueOperation::Toggle;
            break;
        case UiCommandVerb::Reset:
            operation = CommandValueOperation::Reset;
            break;
        default:
            SetCommandResult("Expected a Settings value command.", true);
            return;
        }

        const UiSettingsCommandVerb settingsVerb =
            GetSettingsCommandVerb(operation);
        if (!definition->Supports(settingsVerb))
        {
            SetCommandResult(
                std::string(definition->name) + " supports [" +
                GetSettingsCommandVerbList(*definition) + "].",
                true);
            return;
        }
        std::string error;
        if (operation != CommandValueOperation::Get &&
            !CheckCommandMutationAllowed(*definition, error))
        {
            SetCommandResult(std::move(error), true);
            return;
        }

        std::string value;
        if (!DispatchCommandValue(
                *definition,
                operation,
                command.arguments,
                value,
                error))
        {
            SetCommandResult(std::move(error), true);
            return;
        }
        SetCommandResult(
            std::string(definition->name) + " = " + value);
    }

    void CompleteCommandInput(ImGuiInputTextCallbackData* data)
    {
        const std::string_view input(
            data->Buf,
            static_cast<size_t>(data->BufTextLen));
        const UiCommandCompletionToken completion =
            GetUiCommandCompletionToken(
                input,
                static_cast<size_t>(data->CursorPos));
        std::vector<std::string> candidates =
            GetCommandCompletionCandidates(completion);
        if (candidates.empty())
        {
            SetCommandResult("No completion matches.", true);
            return;
        }

        std::string replacement = candidates.front();
        if (candidates.size() > 1u)
        {
            size_t commonLength = replacement.size();
            for (size_t candidateIndex = 1u;
                candidateIndex < candidates.size();
                ++candidateIndex)
            {
                const std::string& candidate =
                    candidates[candidateIndex];
                commonLength = std::min(
                    commonLength,
                    candidate.size());
                size_t index = 0u;
                while (index < commonLength &&
                    std::tolower(static_cast<unsigned char>(
                        replacement[index])) ==
                    std::tolower(static_cast<unsigned char>(
                        candidate[index])))
                {
                    ++index;
                }
                commonLength = index;
            }
            replacement.resize(commonLength);

            std::string matches = "Matches: ";
            for (size_t index = 0u;
                index < candidates.size();
                ++index)
            {
                if (index > 0u)
                    matches += ", ";
                matches += candidates[index];
                if (matches.size() > 240u)
                {
                    matches += ", ...";
                    break;
                }
            }
            SetCommandResult(std::move(matches));
        }

        if (replacement.size() < completion.prefix.size())
            return;
        data->DeleteChars(
            static_cast<int>(completion.replaceBegin),
            static_cast<int>(
                completion.replaceEnd -
                completion.replaceBegin));
        data->InsertChars(
            static_cast<int>(completion.replaceBegin),
            replacement.c_str());
        if (candidates.size() == 1u &&
            completion.target != UiCommandCompletionTarget::Argument &&
            completion.target != UiCommandCompletionTarget::Value)
        {
            data->InsertChars(data->CursorPos, " ");
        }
    }

    void RecallCommandHistory(
        ImGuiInputTextCallbackData* data,
        bool previous)
    {
        if (m_CommandHistory.empty())
            return;
        if (previous)
        {
            if (m_CommandHistoryIndex < 0)
            {
                m_CommandHistoryIndex =
                    static_cast<int>(m_CommandHistory.size()) - 1;
            }
            else if (m_CommandHistoryIndex > 0)
            {
                --m_CommandHistoryIndex;
            }
        }
        else
        {
            if (m_CommandHistoryIndex >= 0 &&
                m_CommandHistoryIndex <
                    static_cast<int>(m_CommandHistory.size()) - 1)
            {
                ++m_CommandHistoryIndex;
            }
            else
            {
                m_CommandHistoryIndex = -1;
            }
        }

        data->DeleteChars(0, data->BufTextLen);
        if (m_CommandHistoryIndex >= 0)
        {
            data->InsertChars(
                0,
                m_CommandHistory[
                    static_cast<size_t>(m_CommandHistoryIndex)]
                    .c_str());
        }
    }

    static int CommandInputCallback(
        ImGuiInputTextCallbackData* data)
    {
        UIRenderer* renderer =
            static_cast<UIRenderer*>(data->UserData);
        if (data->EventFlag ==
            ImGuiInputTextFlags_CallbackCompletion)
        {
            renderer->CompleteCommandInput(data);
        }
        else if (data->EventFlag ==
            ImGuiInputTextFlags_CallbackHistory)
        {
            renderer->RecallCommandHistory(
                data,
                data->EventKey == ImGuiKey_UpArrow);
        }
        return 0;
    }

    void SubmitCommandInput()
    {
        const std::string input(m_CommandBuffer.data());
        if (!input.empty())
        {
            if (m_CommandHistory.empty() ||
                m_CommandHistory.back() != input)
            {
                m_CommandHistory.push_back(input);
                if (m_CommandHistory.size() > 32u)
                    m_CommandHistory.pop_front();
            }
        }
        m_CommandHistoryIndex = -1;
        m_CommandBuffer.fill('\0');

        const UiCommandParseResult parsed =
            ParseUiCommand(input);
        if (!parsed)
        {
            SetCommandResult(parsed.message, true);
            return;
        }
        m_PendingCommand = parsed.command;
    }

    void DrawCommandInterface()
    {
        const bool commandMotionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        m_CommandAppearance = commandMotionEnabled
            ? AdvancePixelZoomVisibility(
                m_CommandAppearance,
                m_CommandOpen,
                ImGui::GetIO().DeltaTime)
            : m_CommandOpen ? 1.f : 0.f;
        if (!m_CommandOpen && m_CommandAppearance <= 0.f)
            return;
        const float commandAppearanceOpacity =
            commandMotionEnabled
                ? SmoothPixelZoomVisibility(m_CommandAppearance)
                : m_CommandAppearance;
        const float commandAppearanceScale =
            PixelZoomMinimumWindowScale +
            (1.f - PixelZoomMinimumWindowScale) *
                commandAppearanceOpacity;

        const CommandInterfaceLayout& commandLayout =
            m_CommandLayout;
        if (!commandLayout.fits)
        {
            if (m_CommandOpen)
                m_CommandFocusRequested = true;
            return;
        }

        ImGuiWindowFlags commandWindowFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;
        if (!m_CommandOpen)
            commandWindowFlags |= ImGuiWindowFlags_NoInputs;
        else if (commandMotionEnabled &&
            m_CommandAppearance < 1.f)
        {
            commandWindowFlags |= ImGuiWindowFlags_NoMouseInputs;
        }

        ImGui::PushFont(GetActiveUiFont());
        ApplyActiveUiWordSpacing();
        PushPanelBodySurface();

        ImGui::SetNextWindowPos(
            ImVec2(
                commandLayout.left,
                commandLayout.bottom),
            ImGuiCond_Always,
            ImVec2(0.f, 1.f));
        ImGui::SetNextWindowSize(
            ImVec2(
                commandLayout.width,
                commandLayout.height),
            ImGuiCond_Always);
        ImGui::Begin(
            "##UvsrCommandInterface",
            nullptr,
            commandWindowFlags);
        ImDrawList* commandWindowDrawList =
            ImGui::GetWindowDrawList();
        ImGui::TextUnformatted("/");
        ImGui::SameLine();
        const bool showCommandResult =
            m_CommandBuffer.front() == '\0' &&
            !m_CommandResult.empty();
        const float commandRowWidth = ImGui::GetContentRegionAvail().x;
        const float resultDetailsButtonWidth = ImGui::GetFrameHeight();
        const bool commandResultNeedsDetails = showCommandResult &&
            (m_CommandResult.find('\n') != std::string::npos ||
                ImGui::CalcTextSize(m_CommandResult.c_str()).x >
                    commandRowWidth);
        ImGui::SetNextItemWidth(
            commandResultNeedsDetails
                ? std::max(
                    1.f,
                    commandRowWidth - resultDetailsButtonWidth -
                        ImGui::GetStyle().ItemSpacing.x)
                : -FLT_MIN);
        if (m_CommandFocusRequested)
        {
            ImGui::SetKeyboardFocusHere();
            m_CommandFocusRequested = false;
        }
        const ImGuiInputTextFlags inputFlags =
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_CallbackCompletion |
            ImGuiInputTextFlags_CallbackHistory;
        const char* commandHint = showCommandResult
            ? m_CommandResult.c_str()
            : "Try help / Enter applies / Tab completes / Up/Down history / Slash closes";
        if (showCommandResult)
        {
            ImGui::PushStyleColor(
                ImGuiCol_TextDisabled,
                m_CommandResultIsError
                    ? g_UiVisualTokens.errorText
                    : g_UiVisualTokens.successText);
        }
        const bool commandSubmitted = ImGui::InputTextWithHint(
                "##UvsrCommand",
                commandHint,
                m_CommandBuffer.data(),
                m_CommandBuffer.size(),
                inputFlags,
                CommandInputCallback,
                this);
        if (showCommandResult)
            ImGui::PopStyleColor();
        const bool commandEdited = ImGui::IsItemEdited();
        if (commandResultNeedsDetails && !commandEdited)
        {
            ImGui::SameLine();
            if (ImGui::Button(
                    "...##CommandResultDetails",
                    ImVec2(resultDetailsButtonWidth, 0.f)))
            {
                ImGui::OpenPopup("##CommandResultDetailsPopup");
            }
            ImGui::SetItemTooltip("Open the complete command result.");
        }
        if (commandEdited)
            m_CommandResult.clear();
        if (commandSubmitted)
        {
            SubmitCommandInput();
            m_CommandFocusRequested = true;
        }
        if (ImGui::IsPopupOpen("##CommandResultDetailsPopup"))
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowSize(
                ImVec2(
                    std::max(
                        1.f,
                        std::min(
                            720.f * m_UiDisplayScale,
                            viewport->WorkSize.x -
                                20.f * m_UiDisplayScale)),
                    std::max(
                        1.f,
                        std::min(
                            300.f * m_UiDisplayScale,
                            viewport->WorkSize.y * 0.5f))),
                ImGuiCond_Appearing);
            if (ImGui::BeginPopup(
                    "##CommandResultDetailsPopup",
                    ImGuiWindowFlags_NoSavedSettings))
            {
                if (m_CommandResult.empty())
                {
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        m_CommandResultIsError
                            ? g_UiVisualTokens.errorText
                            : g_UiVisualTokens.successText);
                    ImGui::InputTextMultiline(
                        "##CommandResultText",
                        m_CommandResult.data(),
                        m_CommandResult.size() + 1u,
                        ImVec2(-FLT_MIN, -FLT_MIN),
                        ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopStyleColor();
                }
                ImGui::EndPopup();
            }
        }
        const ImVec2 commandWindowPosition =
            ImGui::GetWindowPos();
        const ImVec2 commandWindowSize =
            ImGui::GetWindowSize();
        const float commandWindowBottom =
            commandWindowPosition.y + commandWindowSize.y;
        UiBackdropRect& commandBackdrop =
            m_ui.BackdropRects[UiCommandBackdropIndex];
        CaptureCurrentWindowBackdrop(
            commandBackdrop,
            ImGui::GetStyle().WindowRounding);
        ApplyCommandBackdropAppearance(
            commandBackdrop,
            commandWindowBottom,
            commandAppearanceScale,
            commandAppearanceOpacity);
        commandBackdrop.shadowBlur =
            g_UiVisualTokens.backdropShadowBlur;
        commandBackdrop.shadowOpacity =
            g_UiVisualTokens.backdropShadowOpacity;
        commandBackdrop.shadowOffsetY =
            g_UiVisualTokens.backdropShadowOffsetY;
        ImGui::End();
        ImGui::PopStyleColor();
        RestoreActiveUiWordSpacing();
        ImGui::PopFont();
        ApplyCommandWindowAppearance(
            commandWindowDrawList,
            commandWindowBottom,
            commandAppearanceScale,
            commandAppearanceOpacity);
    }

    void DrawMaterialInspector(
        int width,
        int height,
        bool uiMotionEnabled,
        const ImGuiStyle& style)
    {
        if (!IsMaterialInspectorPresentationActive(
                m_ui.ShowMaterialEditor,
                m_MaterialInspectorAppearance))
        {
            return;
        }

        const bool zoomPresentationActive =
            IsPixelZoomEnabled(m_RenderedPixelZoom) &&
            m_PixelZoomVisibility > 0.f;
        const float zoomPresentationOpacity =
            uiMotionEnabled
                ? SmoothPixelZoomVisibility(
                    m_PixelZoomVisibility)
                : m_PixelZoomVisibility;
        const float zoomLevelTransitionScale =
            uiMotionEnabled
                ? ResolvePixelZoomLevelTransitionScale(
                    m_PixelZoomLevelTransition)
                : 1.f;
        const float materialAppearanceOpacity =
            uiMotionEnabled
                ? SmoothPixelZoomVisibility(
                    m_MaterialInspectorAppearance)
                : m_MaterialInspectorAppearance;
        const float materialAppearanceScale =
            PixelZoomMinimumWindowScale +
            (1.f - PixelZoomMinimumWindowScale) *
                materialAppearanceOpacity;

        const MaterialInspectorLayout layout =
            ResolveMaterialInspectorLayout(
                uint32_t(std::max(0, width)),
                uint32_t(std::max(0, height)),
                m_SettingsPanelMarginPixels,
                m_MaterialInspectorZoomPlacement,
                zoomPresentationActive,
                zoomPresentationOpacity,
                zoomLevelTransitionScale);
        if (layout.panelWidth < style.WindowMinSize.x ||
            layout.panelMaxHeight < style.WindowMinSize.y)
        {
            m_ui.ShowMaterialEditor = false;
            m_MaterialInspectorAppearance = 0.f;
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(
                viewport->Pos.x + layout.panelMinX,
                viewport->Pos.y + layout.panelMinY),
            ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(layout.panelWidth, 0.f),
            ImVec2(layout.panelWidth, layout.panelMaxHeight));
        PushPanelBodySurface();
        ImGui::PushStyleColor(
            ImGuiCol_TitleBg,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgActive,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgCollapsed,
            g_UiVisualTokens.settingsTitleSurface);
        ImGuiWindowFlags materialWindowFlags =
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings;
        if (!m_ui.ShowMaterialEditor ||
            m_MaterialInspectorAppearance < 1.f)
        {
            materialWindowFlags |= ImGuiWindowFlags_NoInputs;
        }
        const bool materialEditorVisible = ImGui::Begin(
            "Materials",
            nullptr,
            materialWindowFlags);
        ImGuiWindow* materialEditorWindow =
            ImGui::GetCurrentWindow();
        if (materialEditorWindow->WantCollapseToggle)
        {
            // The title triangle is the Materials panel close control. Consume
            // ImGui's deferred native collapse request in the click frame so
            // the complete body remains submitted while Amp's retained
            // zoom-and-fade close presentation begins.
            materialEditorWindow->WantCollapseToggle = false;
            materialEditorWindow->Collapsed = false;
            m_ui.ShowMaterialEditor = false;
        }
        ImDrawList* materialWindowDrawList =
            ImGui::GetWindowDrawList();
        const bool deferredMaterialInputBlocked =
            HasDeferredDropdownUiActions();
        if (deferredMaterialInputBlocked)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);
            ImGui::BeginDisabled();
        }
        ImDrawList* materialControlsDrawList = nullptr;

        if (materialEditorVisible)
        {
            auto material = m_ui.SelectedMaterial;
            if (material)
            {
                ImGui::Text(
                    "Material %d: %s",
                    material->materialID,
                    material->name.c_str());

                static constexpr const char* MaterialDomainLabels[] = {
                    "Opaque",
                    "Alpha-tested",
                    "Alpha-blended",
                    "Transmissive",
                    "Transmissive alpha-tested",
                    "Transmissive alpha-blended"
                };
                static_assert(
                    std::size(MaterialDomainLabels) ==
                    size_t(MaterialDomain::Count));
                const int materialDomainIndex = std::clamp(
                    int(material->domain),
                    0,
                    int(MaterialDomain::Count) - 1);
                const float materialControlWidth =
                    ImGui::CalcItemWidth();
                ImGui::SetNextItemWidth(materialControlWidth);
                ImGui::PushID(material->materialID);
                if (BeginRoundedCombo(
                        "Material Domain",
                        MaterialDomainLabels[materialDomainIndex]))
                {
                    for (int index = 0;
                        index < int(MaterialDomain::Count);
                        ++index)
                    {
                        const MaterialDomain candidate =
                            MaterialDomain(index);
                        DrawDeferredDropdownOption(
                            MaterialDomainLabels[index],
                            MaterialDomainLabels[index],
                            material->domain == candidate,
                            [app = m_app, material, candidate]()
                            {
                                material->domain = candidate;
                                app->NotifyMaterialCommandChanged(material);
                            });
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose how the selected surface is rendered.");
                ImGui::PopID();

                ImGui::PushStyleColor(
                    ImGuiCol_ChildBg,
                    g_UiVisualTokens.drawerBackground);
                ImGui::PushStyleColor(
                    ImGuiCol_FrameBg,
                    g_UiVisualTokens.drawerFrame);
                ImGui::PushStyleColor(
                    ImGuiCol_FrameBgHovered,
                    g_UiVisualTokens.drawerFrameHovered);
                ImGui::PushStyleColor(
                    ImGuiCol_FrameBgActive,
                    g_UiVisualTokens.drawerFrameActive);
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowPadding,
                    ImVec2(style.FramePadding.x, style.ItemSpacing.y));
                ImGui::PushStyleVar(
                    ImGuiStyleVar_ChildRounding,
                    g_UiVisualTokens.drawerRounding);
                const bool materialControlsVisible =
                    ImGui::BeginChild(
                        "##MaterialControlsBody",
                        ImVec2(0.f, 0.f),
                        ImGuiChildFlags_AlwaysUseWindowPadding |
                            ImGuiChildFlags_AutoResizeY |
                            ImGuiChildFlags_AlwaysAutoResize,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse);
                materialControlsDrawList =
                    ImGui::GetWindowDrawList();
                if (materialControlsVisible)
                {
                    ImGui::PushItemWidth(materialControlWidth);
                    const bool materialChanged =
                        donut::app::MaterialEditor(
                            material.get(),
                            false,
                            false);
                    if (materialChanged)
                        m_app->NotifyMaterialCommandChanged(material);
                    ImGui::PopItemWidth();
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor(4);
            }
            else
            {
                ImGui::TextDisabled(
                    "Aim the center crosshair at an editable surface and press M.");
            }
        }

        if (deferredMaterialInputBlocked)
        {
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
        }
        const ImVec2 materialWindowPosition =
            ImGui::GetWindowPos();
        const ImVec2 materialWindowSize =
            ImGui::GetWindowSize();
        const ImVec2 materialWindowCenter(
            materialWindowPosition.x + materialWindowSize.x * 0.5f,
            materialWindowPosition.y + materialWindowSize.y * 0.5f);
        const float materialTitleHeight =
            ImGui::GetFontSize() + style.FramePadding.y * 2.f;
        UiBackdropRect& materialTitleBackdrop =
            m_ui.BackdropRects[UiMaterialTitleBackdropIndex];
        materialTitleBackdrop.minX =
            materialWindowPosition.x + 0.5f;
        materialTitleBackdrop.minY =
            materialWindowPosition.y + 0.5f;
        materialTitleBackdrop.maxX =
            materialWindowPosition.x + materialWindowSize.x - 0.5f;
        materialTitleBackdrop.maxY =
            materialWindowPosition.y + materialTitleHeight - 0.5f;
        materialTitleBackdrop.rounding = style.FrameRounding;
        materialTitleBackdrop.visible =
            materialTitleBackdrop.maxX > materialTitleBackdrop.minX &&
            materialTitleBackdrop.maxY > materialTitleBackdrop.minY;

        UiBackdropRect& materialBodyBackdrop =
            m_ui.BackdropRects[UiMaterialBodyBackdropIndex];
        materialBodyBackdrop.minX =
            materialWindowPosition.x + 0.5f;
        materialBodyBackdrop.minY =
            materialWindowPosition.y + materialTitleHeight - 1.f;
        materialBodyBackdrop.maxX =
            materialWindowPosition.x + materialWindowSize.x - 0.5f;
        materialBodyBackdrop.maxY =
            materialWindowPosition.y + materialWindowSize.y - 0.5f;
        materialBodyBackdrop.rounding = style.WindowRounding;
        materialBodyBackdrop.visible =
            !ImGui::IsWindowCollapsed() &&
            materialBodyBackdrop.maxX > materialBodyBackdrop.minX &&
            materialBodyBackdrop.maxY > materialBodyBackdrop.minY;
        for (size_t backdropIndex :
            { UiMaterialTitleBackdropIndex,
                UiMaterialBodyBackdropIndex })
        {
            UiBackdropRect& backdrop =
                m_ui.BackdropRects[backdropIndex];
            ApplyBackdropAppearance(
                backdrop,
                materialWindowCenter,
                materialAppearanceScale,
                materialAppearanceOpacity);
            backdrop.shadowBlur =
                g_UiVisualTokens.backdropShadowBlur;
            backdrop.shadowOpacity =
                g_UiVisualTokens.backdropShadowOpacity;
            backdrop.shadowOffsetY =
                g_UiVisualTokens.backdropShadowOffsetY;
        }
        ImGui::End();
        ApplyWindowAppearance(
            materialWindowDrawList,
            materialWindowCenter,
            materialAppearanceScale,
            materialAppearanceOpacity);
        if (materialControlsDrawList &&
            materialControlsDrawList != materialWindowDrawList)
        {
            ApplyWindowAppearance(
                materialControlsDrawList,
                materialWindowCenter,
                materialAppearanceScale,
                materialAppearanceOpacity);
        }
        ImGui::PopStyleColor(4);
    }

    static std::string BuildPerformanceLine(
        const std::array<std::string, 6>& values)
    {
        return values[0] + " - " +
            values[5] + " - " +
            values[3] + " - " +
            values[4] + " - " +
            values[1] + " - " +
            values[2];
    }

    static std::array<std::string, 2> BuildOgPerformanceLines(
        const std::array<std::string, 6>& values)
    {
        return {{
            values[0] + " - " +
                values[5] + " - " +
                values[3],
            values[4] + " - " +
                values[1] + " - " +
                values[2]
        }};
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
        snapshot.submittedTriangles =
            m_app->GetSubmittedMainViewTriangles();
        snapshot.rendererName = rendererString ? rendererString : "";
        snapshot.frameTimeSeconds = m_StatFrameTimeCount > 0
            ? m_StatFrameTimeSum / double(m_StatFrameTimeCount)
            : m_DisplayedFrameTime;
        snapshot.gpuMetrics =
            QueryGpuPerformanceMetrics(rendererString);
        if (const ScreenSpaceDirectionalShadowTimings* timings =
                m_app->GetScreenSpaceDirectionalShadowTimings())
        {
            snapshot.screenSpaceShadowTimings = *timings;
            snapshot.hasScreenSpaceShadowTimings =
                m_ui.ScreenSpaceDirectionalShadows.enabled &&
                timings->active && timings->available;
        }
        if (const ScreenSpaceVisibilityTimings* timings =
                m_app->GetScreenSpaceVisibilityTimings())
        {
            snapshot.visibilityTimings = *timings;
            snapshot.hasVisibilityTimings =
                timings->active && timings->available;
        }
        if (const TemporalAATimings* timings =
                m_app->GetTemporalAATimings())
        {
            snapshot.temporalAATimings = *timings;
            snapshot.hasTemporalAATimings =
                m_ui.AntiAliasing.temporal.enabled &&
                timings->dispatchCount > 0u &&
                timings->available;
        }
        if (m_ui.AntiAliasing.cmaa2.enabled)
        {
            if (const Cmaa2Timings* timings = m_app->GetCmaa2Timings())
            {
                snapshot.cmaa2Timings = *timings;
                snapshot.hasCmaa2Timings = timings->available;
            }
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
        m_PerformanceStatValues[5] =
            FormatTriangleCount(snapshot.submittedTriangles);
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
            m_DisplayedGpuBandwidthGBps =
                snapshot.gpuMetrics.memoryBandwidthGBps;
            FormatStatLine(
                m_PerformanceStatValues[3],
                "%.1f gb/s",
                m_DisplayedGpuBandwidthGBps);

            if (snapshot.gpuMetrics.utilizationValid)
            {
                const double targetTFlops =
                    snapshot.gpuMetrics.gpuGFlops / 1000.0 *
                    snapshot.gpuMetrics.gpuUtilization;
                if (!m_HasGpuStatSnapshot)
                    m_DisplayedGpuTFlops = targetTFlops;
                else
                    m_DisplayedGpuTFlops = StepTowardByTenth(
                        m_DisplayedGpuTFlops,
                        targetTFlops);
                FormatStatLine(
                    m_PerformanceStatValues[4],
                    "%.1f tflops",
                    m_DisplayedGpuTFlops);
                m_HasGpuStatSnapshot = true;
            }
            else
            {
                m_PerformanceStatValues[4] = "-- tflops";
                m_HasGpuStatSnapshot = false;
            }
        }
        else
        {
            m_PerformanceStatValues[3] = "-- gb/s";
            m_PerformanceStatValues[4] = "-- tflops";
            m_HasGpuStatSnapshot = false;
        }

        m_DisplayedScreenSpaceShadowTimings =
            snapshot.screenSpaceShadowTimings;
        m_DisplayedVisibilityTimings = snapshot.visibilityTimings;
        m_DisplayedTemporalAATimings = snapshot.temporalAATimings;
        m_DisplayedCmaa2Timings = snapshot.cmaa2Timings;
        m_HasScreenSpaceShadowStatSnapshot =
            snapshot.hasScreenSpaceShadowTimings;
        m_HasVisibilityStatSnapshot = snapshot.hasVisibilityTimings;
        m_HasTemporalAAStatSnapshot = snapshot.hasTemporalAATimings;
        m_HasCmaa2StatSnapshot = snapshot.hasCmaa2Timings;
    }

    static void PushPanelSliderTrackStyle()
    {
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            g_UiVisualTokens.drawerFrame);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgHovered,
            g_UiVisualTokens.drawerFrameHovered);
        ImGui::PushStyleColor(
            ImGuiCol_FrameBgActive,
            g_UiVisualTokens.drawerFrameActive);
    }

    static bool DrawSliderFloat(
        const char* label,
        float* value,
        float minimum,
        float maximum,
        const char* format = "%.3f",
        ImGuiSliderFlags flags = 0)
    {
        const ImGuiID sliderId = ImGui::GetID(label);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID presentationValueKey =
            sliderId ^ ImGuiID(0x2F81C6D9u);
        const bool freezePresentation =
            FreezeAnimatedToggleVisualValues();
        float presentationValue = storage->GetFloat(
            presentationValueKey,
            *value);
        float* submittedValue =
            freezePresentation
                ? &presentationValue
                : value;
        PushPanelSliderTrackStyle();
        const bool changed = ImGui::SliderFloat(
            label,
            submittedValue,
            minimum,
            maximum,
            format,
            flags);
        ImGui::PopStyleColor(3);
        if (!freezePresentation)
            storage->SetFloat(presentationValueKey, *value);
        return changed && !freezePresentation;
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
    UIRenderer(
        DeviceManager* deviceManager,
        std::shared_ptr<UvsrSceneViewer> app,
        UIData& ui)
        : ImGui_Renderer(deviceManager)
        , m_app(app)
        , m_ui(ui)
    {
        m_Font = CreateFontFromFile(
            *(app->GetRootFs()), "/media/fonts/System/CodexUI-Semibold.ttf", 16.f);

        ImGui::GetIO().IniFilename = nullptr;
    }

    bool ShouldSuppressFullscreenShortcut() const override
    {
        return m_CommandOpen;
    }

    bool Init(std::shared_ptr<ShaderFactory> shaderFactory)
    {
        if (!ImGui_Renderer::Init(shaderFactory))
            return false;

        m_BackdropBlurPass = std::make_unique<BackdropBlurPass>(
            GetDevice(),
            shaderFactory,
            m_app->GetCommonPasses());
        m_PixelZoomPass = std::make_unique<PixelZoomPass>(
            GetDevice(),
            shaderFactory,
            m_app->GetCommonPasses());
        return true;
    }

    virtual void Render(nvrhi::IFramebuffer* framebuffer) override
    {
        if (!imgui_nvrhi)
            return;

        const float deltaTime = ImGui::GetIO().DeltaTime;
        const bool uiMotionEnabled =
            GetUiSkinBehavior(m_ui.Skin).motionEnabled;
        const bool pixelZoomRequestedByUi =
            IsPixelZoomEnabled(m_ui.PixelZoom);
        const bool zoomPresentationActiveBeforeUpdate =
            IsPixelZoomEnabled(m_RenderedPixelZoom) &&
            m_PixelZoomVisibility > 0.f;
        m_MaterialInspectorAppearance =
            AdvanceMaterialInspectorAppearance(
                m_MaterialInspectorAppearance,
                m_ui.ShowMaterialEditor,
                uiMotionEnabled,
                deltaTime);
        const bool materialInspectorPresentationActive =
            IsMaterialInspectorPresentationActive(
                m_ui.ShowMaterialEditor,
                m_MaterialInspectorAppearance);
        m_MaterialInspectorZoomPlacement =
            AdvanceMaterialInspectorZoomPlacement(
                m_MaterialInspectorZoomPlacement,
                materialInspectorPresentationActive,
                pixelZoomRequestedByUi,
                zoomPresentationActiveBeforeUpdate,
                uiMotionEnabled,
                deltaTime);
        const bool pixelZoomOpeningDelayed =
            ShouldDelayPixelZoomForMaterialInspector(
                materialInspectorPresentationActive,
                pixelZoomRequestedByUi,
                uiMotionEnabled,
                m_MaterialInspectorZoomPlacement);
        const bool pixelZoomRequested =
            pixelZoomRequestedByUi &&
            !pixelZoomOpeningDelayed;
        if (!uiMotionEnabled)
        {
            m_PixelZoomVisibility = pixelZoomRequested ? 1.f : 0.f;
            m_RenderedPixelZoom = pixelZoomRequested
                ? m_ui.PixelZoom
                : PixelZoomMode::Off;
            m_PendingPixelZoom = m_RenderedPixelZoom;
            m_PixelZoomLevelTransition = 1.f;
        }
        else
        {
            m_PixelZoomVisibility = AdvancePixelZoomVisibility(
                m_PixelZoomVisibility,
                pixelZoomRequested,
                deltaTime);
            if (pixelZoomRequested)
            {
                if (!IsPixelZoomEnabled(m_RenderedPixelZoom))
                {
                    m_RenderedPixelZoom = m_ui.PixelZoom;
                    m_PendingPixelZoom = m_ui.PixelZoom;
                    m_PixelZoomLevelTransition = 1.f;
                }
                else if (m_PixelZoomVisibility < 1.f)
                {
                    // Opening remains responsive to rapid level changes. The
                    // dedicated level pulse begins only from the stable,
                    // fully-visible endpoint.
                    m_RenderedPixelZoom = m_ui.PixelZoom;
                    m_PendingPixelZoom = m_ui.PixelZoom;
                }
                else
                {
                    if (m_PixelZoomLevelTransition >= 1.f &&
                        m_ui.PixelZoom != m_RenderedPixelZoom)
                    {
                        m_PendingPixelZoom = m_ui.PixelZoom;
                        m_PixelZoomLevelTransition = 0.f;
                    }
                    else if (m_PixelZoomLevelTransition < 1.f)
                    {
                        m_PendingPixelZoom = m_ui.PixelZoom;
                        m_PixelZoomLevelTransition =
                            AdvancePixelZoomLevelTransition(
                                m_PixelZoomLevelTransition,
                                deltaTime);
                        if (ShouldSwitchPixelZoomLevel(
                            m_PixelZoomLevelTransition))
                        {
                            m_RenderedPixelZoom = m_PendingPixelZoom;
                        }
                    }
                }
            }
            else
            {
                m_PendingPixelZoom = m_RenderedPixelZoom;
                m_PixelZoomLevelTransition = 1.f;
            }
            if (!pixelZoomRequested && m_PixelZoomVisibility <= 0.f)
            {
                m_RenderedPixelZoom = PixelZoomMode::Off;
                m_PendingPixelZoom = PixelZoomMode::Off;
            }
        }
        buildUI();
        DrawCommandInterface();
        const float pixelZoomOpacity =
            uiMotionEnabled
                ? SmoothPixelZoomVisibility(m_PixelZoomVisibility)
                : m_PixelZoomVisibility;
        const float pixelZoomLevelTransitionScale =
            uiMotionEnabled
                ? ResolvePixelZoomLevelTransitionScale(
                    m_PixelZoomLevelTransition)
                : 1.f;
        const bool pixelZoomPassActive =
            IsPixelZoomEnabled(m_RenderedPixelZoom) &&
            pixelZoomOpacity > 0.f;
        const float materialInspectorOpacity =
            uiMotionEnabled
                ? SmoothPixelZoomVisibility(
                    m_MaterialInspectorAppearance)
                : m_MaterialInspectorAppearance;
        const float crosshairOpacity = std::max(
            pixelZoomRequested ? pixelZoomOpacity : 0.f,
            materialInspectorOpacity);
        if (crosshairOpacity > 0.f)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const ImVec2 crosshairCenter(
                viewport->Pos.x + std::floor(viewport->Size.x * 0.5f) + 0.5f,
                viewport->Pos.y + std::floor(viewport->Size.y * 0.5f) + 0.5f);
            ImGui::GetForegroundDrawList()->AddCircleFilled(
                crosshairCenter,
                2.f,
                IM_COL32(
                    255,
                    255,
                    255,
                    int(std::round(128.f * crosshairOpacity))),
                12);
        }
        if (pixelZoomPassActive)
        {
            const nvrhi::FramebufferInfoEx& framebufferInfo =
                framebuffer->getFramebufferInfo();
            const PixelZoomLayout zoomLabelLayout =
                ResolveAnimatedPixelZoomLayout(
                    ResolvePixelZoomLayout(
                        framebufferInfo.width,
                        framebufferInfo.height,
                        m_SettingsPanelMarginPixels,
                        m_RenderedPixelZoom),
                    pixelZoomOpacity,
                    pixelZoomLevelTransitionScale);
            const char* zoomAreaLabel =
                GetPixelZoomAreaLabel(m_RenderedPixelZoom);
            ImFont* zoomLabelFont = GetActiveUiFont();
            ImGui::PushFont(zoomLabelFont);
            const ImVec2 zoomAreaLabelSize =
                ImGui::CalcTextSize(zoomAreaLabel);
            ImGui::PopFont();

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float labelInset =
                float(m_SettingsPanelMarginPixels);
            const ImVec2 zoomAreaLabelPosition(
                viewport->Pos.x +
                    float(zoomLabelLayout.panelMinX) +
                    std::floor(
                        (float(zoomLabelLayout.panelWidth) -
                            zoomAreaLabelSize.x) *
                        0.5f),
                viewport->Pos.y +
                    float(zoomLabelLayout.panelMinY +
                        zoomLabelLayout.panelHeight) -
                    labelInset -
                    zoomAreaLabelSize.y);
            const int zoomLabelAlpha = int(std::round(
                230.f * pixelZoomOpacity));
            const int zoomLabelShadowAlpha = int(std::round(
                150.f * pixelZoomOpacity));
            ImDrawList* foregroundDrawList =
                ImGui::GetForegroundDrawList();
            const ImVec2 zoomAreaLabelShadowPosition(
                zoomAreaLabelPosition.x + 1.f,
                zoomAreaLabelPosition.y + 1.f);
            foregroundDrawList->AddText(
                zoomLabelFont,
                zoomLabelFont->LegacySize,
                zoomAreaLabelShadowPosition,
                IM_COL32(0, 0, 0, zoomLabelShadowAlpha),
                zoomAreaLabel);
            foregroundDrawList->AddText(
                zoomLabelFont,
                zoomLabelFont->LegacySize,
                zoomAreaLabelPosition,
                IM_COL32(
                    255,
                    255,
                    255,
                    zoomLabelAlpha),
                zoomAreaLabel);
        }
        ImGui::Render();
        if (m_PendingCommand)
        {
            // A slash command is the newest input. Fast-forward and commit
            // older deferred UI choices at the same safe composition barrier
            // so they cannot resurface later and overwrite the command.
            TryApplyDeferredDropdownUiActions(true, true);
            UiCommand command = std::move(*m_PendingCommand);
            m_PendingCommand.reset();
            ExecuteUiCommand(command);
        }
        if (pixelZoomPassActive && m_PixelZoomPass)
            m_PixelZoomPass->Capture(framebuffer);
        if (m_BackdropBlurPass)
        {
            const bool backdropEnabled =
                GetUiSkinBehavior(m_ComposedUiSkin).backdropEnabled;
            m_BackdropBlurPass->Render(
                framebuffer,
                backdropEnabled ? UiBackgroundBlurPixels : 0.f,
                m_ui.BackdropRects);
        }
        if (pixelZoomPassActive && m_PixelZoomPass)
        {
            m_PixelZoomPass->Composite(
                framebuffer,
                m_RenderedPixelZoom,
                m_SettingsPanelMarginPixels,
                ImGui::GetStyle().WindowRounding,
                pixelZoomOpacity,
                pixelZoomLevelTransitionScale);
        }
        imgui_nvrhi->render(framebuffer);
        m_imguiFrameOpened = false;
    }

    virtual void BackBufferResizing() override
    {
        if (m_BackdropBlurPass)
            m_BackdropBlurPass->BackBufferResizing();
        if (m_PixelZoomPass)
            m_PixelZoomPass->BackBufferResizing();
        ImGui_Renderer::BackBufferResizing();
    }

    virtual void DisplayScaleChanged(
        float scaleX,
        float scaleY) override
    {
        ImGui_Renderer::DisplayScaleChanged(scaleX, scaleY);
        m_UiDisplayScale = std::clamp(scaleX, 0.5f, 4.f);
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
        const bool plainCommandShortcut =
            (mods & (GLFW_MOD_CONTROL |
                GLFW_MOD_ALT |
                GLFW_MOD_SUPER |
                GLFW_MOD_SHIFT)) == 0;
        if (m_CommandOpen)
        {
            if (key == GLFW_KEY_SLASH &&
                action == GLFW_PRESS &&
                plainCommandShortcut)
            {
                m_CommandOpen = false;
                m_CommandFocusRequested = false;
                m_SuppressCommandShortcutSlashCharacter = true;
                ImGui::ClearActiveID();
            }
            return true;
        }

        if (key == GLFW_KEY_SLASH &&
            action == GLFW_PRESS &&
            plainCommandShortcut &&
            !ImGui::GetIO().WantTextInput)
        {
            m_CommandOpen = true;
            m_CommandFocusRequested = true;
            m_SuppressCommandShortcutSlashCharacter = true;
            return true;
        }

        if (key == GLFW_KEY_ESCAPE &&
            action == GLFW_PRESS &&
            !ImGui::GetIO().WantTextInput)
        {
            m_ui.ShowUI = !m_ui.ShowUI;
            return true;
        }
        const bool plainFlashlightShortcut =
            (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0;
        if (key == GLFW_KEY_F &&
            action == GLFW_PRESS &&
            plainFlashlightShortcut &&
            !captured &&
            !ImGui::GetIO().WantTextInput)
        {
            m_app->ToggleFlashlight();
            return true;
        }
        const bool plainZoomShortcut =
            (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0;
        if (key == GLFW_KEY_Z &&
            action == GLFW_PRESS &&
            plainZoomShortcut &&
            !ImGui::GetIO().WantTextInput)
        {
            m_ui.PixelZoom =
                AdvancePixelZoomMode(m_ui.PixelZoom);
            return true;
        }
        const bool plainMaterialEditorShortcut =
            (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0;
        if (key == GLFW_KEY_M &&
            action == GLFW_PRESS &&
            plainMaterialEditorShortcut &&
            !ImGui::GetIO().WantTextInput)
        {
            m_app->ToggleCenterMaterialInspector();
            return true;
        }

        return captured;
    }

    virtual bool KeyboardCharInput(
        unsigned int unicode,
        int mods) override
    {
        if (m_SuppressCommandShortcutSlashCharacter)
        {
            m_SuppressCommandShortcutSlashCharacter = false;
            if (unicode == static_cast<unsigned int>('/'))
                return true;
        }
        if (m_CommandOpen)
        {
            ImGui_Renderer::KeyboardCharInput(unicode, mods);
            return true;
        }
        return ImGui_Renderer::KeyboardCharInput(unicode, mods);
    }

    virtual void buildUI(void) override
    {
        g_SettingsAppearanceDrawLists.clear();
        for (UiBackdropRect& backdropRect : m_ui.BackdropRects)
            backdropRect.visible = false;

        m_ComposedUiSkin = m_ui.Skin;
        ApplyUiSkin(m_ComposedUiSkin, m_UiDisplayScale);
        const bool uiMotionEnabled =
            ImGui::IsUvsrUiMotionEnabled();
        int width, height;
        GetDeviceManager()->GetWindowDimensions(width, height);
        ImFont* activeUiFont = GetActiveUiFont();
        const ImFont* scaledUiFont = activeUiFont;
        const float panelReferenceFontSize = scaledUiFont
            ? scaledUiFont->LegacySize
            : ImGui::GetFontSize();
        m_SettingsPanelMarginPixels = static_cast<uint32_t>(
            std::max(
                1.f,
                std::round(panelReferenceFontSize * 0.6f)));
        const ImGuiViewport* mainViewport =
            ImGui::GetMainViewport();
        const UiCommandLayoutRect workRectangle = {
            mainViewport->WorkPos.x,
            mainViewport->WorkPos.y,
            mainViewport->WorkPos.x + mainViewport->WorkSize.x,
            mainViewport->WorkPos.y + mainViewport->WorkSize.y
        };
        ImGui::PushFont(activeUiFont);
        const float minimumCommandHeight =
            std::max(
                GetCommandInterfaceMinimumHeight(),
                ImGui::GetStyle().WindowMinSize.y);
        const float reservedCommandHeight =
            GetCommandInterfaceReservedHeight();
        const float minimumCommandWidth =
            ImGui::GetStyle().WindowMinSize.x;
        const float minimumSettingsHeight =
            std::max(
                GetSettingsCollapsedWindowHeight(
                    ImGui::GetStyle(),
                    ImGui::GetFontSize(),
                    true,
                    true),
                ImGui::GetStyle().WindowMinSize.y);
        ImGui::PopFont();
        m_CommandLayout = ResolveCommandInterfaceLayout(
            workRectangle,
            float(m_SettingsPanelMarginPixels),
            260.f * m_UiDisplayScale,
            minimumCommandWidth,
            reservedCommandHeight,
            minimumCommandHeight,
            minimumSettingsHeight);
        const float settingsMaximumBottom =
            m_CommandLayout.fits
                ? m_CommandLayout.settingsMaximumBottom
                : workRectangle.maxY -
                    float(m_SettingsPanelMarginPixels);
        const bool sceneLoading = m_app->IsSceneBusy();
        if (sceneLoading)
        {
            if (!m_WasSceneLoading)
            {
                // A load can replace scene-owned objects referenced by queued
                // UI actions. Discard those stale choices before the loading
                // screen starts; a scene choice that initiated this load has
                // already been moved out of the queue and applied.
                CancelDeferredDropdownUiActions();
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
                m_DisplayedScreenSpaceShadowTimings = {};
                m_DisplayedVisibilityTimings = {};
                m_DisplayedTemporalAATimings = {};
                m_DisplayedCmaa2Timings = {};
                m_HasAppliedStatSnapshot = false;
                m_HasGpuStatSnapshot = false;
                m_HasScreenSpaceShadowStatSnapshot = false;
                m_HasVisibilityStatSnapshot = false;
                m_HasTemporalAAStatSnapshot = false;
                m_HasCmaa2StatSnapshot = false;
                m_SettingsAppearance = 0.f;
            }

            BeginFullScreenWindow();
            ImGui::PushFont(activeUiFont);
            ApplyActiveUiWordSpacing();

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
            static constexpr const char* LoadingDots[] = {
                ".",
                "..",
                "..."
            };
            const size_t loadingDotIndex =
                uiMotionEnabled
                    ? size_t(ImGui::GetTime() * 2.0) %
                        std::size(LoadingDots)
                    : std::size(LoadingDots) - 1u;

            char messageBuffer[512];
            const std::string sceneDisplayName =
                m_app->GetCurrentSceneDisplayName();
            const char* loadingPhase =
                m_app->IsSceneGpuUploadPending()
                    ? "Uploading mesh buffers in bounded chunks"
                    : "Importing and preparing scene data";
            snprintf(
                messageBuffer,
                std::size(messageBuffer),
                "Loading scene: %s, please wait%s\n"
                "%s\n"
                "Objects: %u/%u / Import steps: %llu/%llu / "
                "Textures decoded: %u/%u / GPU ready: %u/%u",
                sceneDisplayName.c_str(),
                LoadingDots[loadingDotIndex],
                loadingPhase,
                objectsLoaded,
                objectsTotal,
                static_cast<unsigned long long>(importStepsCompleted),
                static_cast<unsigned long long>(importStepsTotal),
                texturesDecoded,
                texturesTotal,
                texturesReady,
                texturesTotal);
            DrawScreenCenteredText(messageBuffer);

            RestoreActiveUiWordSpacing();
            ImGui::PopFont();
            EndFullScreenWindow();

            return;
        }
        m_WasSceneLoading = false;

        ImGui::PushFont(activeUiFont);
        ApplyActiveUiWordSpacing();

        float const fontSize = ImGui::GetFontSize();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float settingsControlWidth =
            ImGui::CalcTextSize(
                "Cosine-Weighted Solid Angle").x +
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
        m_SettingsAppearance = uiMotionEnabled
            ? AdvancePixelZoomVisibility(
                m_SettingsAppearance,
                m_ui.ShowUI,
                ImGui::GetIO().DeltaTime)
            : m_ui.ShowUI ? 1.f : 0.f;
        const auto deferredDropdownCompositionIdle =
            [&](bool settingsLayoutIdle, bool settingsScrollIdle)
            {
                const bool settingsAppearanceIdle =
                    m_SettingsAppearance <= 0.f ||
                    m_SettingsAppearance >= 1.f;
                const bool pixelZoomAppearanceIdle =
                    IsPixelZoomCompositionIdle(
                        m_ui.PixelZoom,
                        m_RenderedPixelZoom,
                        m_PendingPixelZoom,
                        m_PixelZoomVisibility,
                        m_PixelZoomLevelTransition);
                const bool materialInspectorPlacementIdle =
                    m_MaterialInspectorZoomPlacement <= 0.f ||
                    m_MaterialInspectorZoomPlacement >= 1.f;
                const bool materialInspectorAppearanceIdle =
                    IsMaterialInspectorAppearanceIdle(
                        m_ui.ShowMaterialEditor,
                        m_MaterialInspectorAppearance);
                const bool interactionIdle =
                    !ImGui::IsAnyItemActive() &&
                    std::abs(ImGui::GetIO().MouseWheel) <= 0.001f &&
                    !ImGui::IsMouseDragging(
                        ImGuiMouseButton_Left);
                const bool dropdownPopupIdle =
                    !IsDeferredDropdownPopupTransitionActive();
                return settingsLayoutIdle &&
                    settingsScrollIdle &&
                    settingsAppearanceIdle &&
                    pixelZoomAppearanceIdle &&
                    materialInspectorPlacementIdle &&
                    materialInspectorAppearanceIdle &&
                    dropdownPopupIdle &&
                    interactionIdle;
            };
        if (!m_ui.ShowUI && m_SettingsAppearance <= 0.f)
        {
            DrawMaterialInspector(
                width,
                height,
                uiMotionEnabled,
                style);
            // Settings is hidden, but the independently composed material
            // inspector may still own the active dropdown this frame.
            FinishUnsubmittedDeferredDropdownPopupTransition();
            const SettingsScrollStabilityContext& scrollContext =
                g_SettingsScrollStabilityContext;
            const bool recentLayoutAnimation =
                scrollContext.lastFrame >= ImGui::GetFrameCount() - 1 &&
                scrollContext.layoutAnimatingLastFrame;
            TryApplyDeferredDropdownUiActions(
                deferredDropdownCompositionIdle(
                    !recentLayoutAnimation,
                    true),
                !uiMotionEnabled);
            RestoreActiveUiWordSpacing();
            ImGui::PopFont();
            return;
        }
        const float settingsAppearanceOpacity =
            SmoothPixelZoomVisibility(m_SettingsAppearance);
        const float settingsAppearanceScale =
            PixelZoomMinimumWindowScale +
            (1.f - PixelZoomMinimumWindowScale) *
                settingsAppearanceOpacity;
        const std::string performanceLine =
            BuildPerformanceLine(m_PerformanceStatValues);
        const std::array<std::string, 2> ogPerformanceLines =
            BuildOgPerformanceLines(m_PerformanceStatValues);

        // Keep Settings independent of live status digits. At the current
        // 20-pixel UI font, 29.3 font heights place the visible right border
        // at the screenshot's marked boundary while retaining the resolution,
        // triangle counter, and performance line.
        constexpr float SettingsWindowWidthInFontHeights = 29.3f;
        const float settingsPanelMarginPixels =
            float(m_SettingsPanelMarginPixels);
        const float availableWindowWidth =
            std::max(
                1.f,
                workRectangle.maxX -
                    workRectangle.minX -
                    settingsPanelMarginPixels * 2.f);
        const float settingsWindowWidth = std::min(
            fontSize * SettingsWindowWidthInFontHeights,
            availableWindowWidth);
        const float settingsWindowTop =
            workRectangle.minY + settingsPanelMarginPixels;
        const float settingsMaximumWindowHeight =
            std::max(
                1.f,
                settingsMaximumBottom - settingsWindowTop);
        ImGui::SetNextWindowPos(
            ImVec2(
                workRectangle.minX + settingsPanelMarginPixels,
                settingsWindowTop),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(settingsWindowWidth, 0.f),
            ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(settingsWindowWidth, 0.f),
            ImVec2(
                settingsWindowWidth,
                settingsMaximumWindowHeight));
        const bool hasPerformanceStatus =
            !m_PerformanceStatValues[1].empty();
        const bool splitOgPerformanceStatus =
            hasPerformanceStatus &&
            m_ComposedUiSkin == UiSkin::Og;
        const float settingsCollapsedHeight =
            GetSettingsCollapsedWindowHeight(
                style,
                fontSize,
                hasPerformanceStatus,
                splitOgPerformanceStatus);
        ImGui::SetNextSettingsWindowCollapsedHeight(
            settingsCollapsedHeight);
        if (m_SettingsCollapsedRequest)
        {
            ImGui::SetNextWindowCollapsed(
                *m_SettingsCollapsedRequest,
                ImGuiCond_Always);
            m_SettingsCollapsedRequest.reset();
        }
        else
        {
            ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once);
        }
        // WindowBg is absent beneath title bars. This precomposed Settings-only
        // token reproduces the resting drawer blue over the body surface.
        PushPanelBodySurface();
        ImGui::PushStyleColor(
            ImGuiCol_TitleBg,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgActive,
            g_UiVisualTokens.settingsTitleSurface);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgCollapsed,
            g_UiVisualTokens.settingsTitleSurface);
        ImGuiWindowFlags settingsWindowFlags =
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (!m_ui.ShowUI ||
            m_SettingsAppearance < 1.f)
        {
            settingsWindowFlags |= ImGuiWindowFlags_NoInputs;
        }
        ImGui::Begin(
            "Settings",
            nullptr,
            settingsWindowFlags);
        ImDrawList* settingsWindowDrawList =
            ImGui::GetWindowDrawList();

        ImGui::TextUnformatted(rendererLine);
        if (hasPerformanceStatus)
        {
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() - style.ItemSpacing.y +
                    SettingsStatusLineSpacing);
            const char* performanceTooltip =
                "tris counts frustum-culled triangle instances submitted by "
                "the main geometry pass; occluded, back-facing, and "
                "alpha-discarded triangles can still be included. "
                "Bandwidth is the current theoretical limit. "
                "Floating-point throughput is current-clock single-precision "
                "peak scaled by graphics-processor utilization.";
            if (splitOgPerformanceStatus)
            {
                ImGui::TextUnformatted(
                    ogPerformanceLines[0].c_str());
                ImGui::SetItemTooltip("%s", performanceTooltip);
                ImGui::SetCursorPosY(
                    ImGui::GetCursorPosY() - style.ItemSpacing.y +
                        SettingsStatusLineSpacing);
                ImGui::TextUnformatted(
                    ogPerformanceLines[1].c_str());
                ImGui::SetItemTooltip("%s", performanceTooltip);
            }
            else
            {
                ImGui::TextUnformatted(performanceLine.c_str());
                ImGui::SetItemTooltip("%s", performanceTooltip);
            }
        }
        ImGui::Separator();

        const float settingsBodyMaxHeight = std::max(
            1.f,
            settingsMaximumBottom -
                ImGui::GetCursorScreenPos().y - style.WindowPadding.y);
        PrepareSettingsScrollStability();
        const float settingsBodyMinimumHeight =
            GetSettingsBodyMinimumHeight(
                settingsBodyMaxHeight);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.f, settingsBodyMinimumHeight),
            ImVec2(FLT_MAX, settingsBodyMaxHeight));
        ImGui::BeginChild(
            "##SettingsBody",
            ImVec2(0.f, 0.f),
            ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);
        ImDrawList* settingsBodyDrawList =
            ImGui::GetWindowDrawList();
        TrackSettingsAppearanceDrawList(settingsBodyDrawList);
        BeginSettingsScrollStability();

        // Keep the panel visually unchanged while a selection waits for its
        // stable commit frame. BeginDisabled blocks another mutation but, in
        // contrast to NoInputs, the hovered ImGui window continues capturing
        // the mouse so clicks and cursor motion cannot leak to the camera.
        const bool deferredDropdownInputBlocked =
            HasDeferredDropdownUiActions();
        if (deferredDropdownInputBlocked)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);
            ImGui::BeginDisabled();
        }
        // A same-frame anchor correction translates submitted vertices after
        // item hit rectangles have been resolved. Keep widgets noninteractive
        // on every continuation/finalization frame of that motion so rendered
        // controls and hit testing can never disagree.
        const bool settingsScrollInputBlocked =
            uiMotionEnabled &&
            g_SettingsScrollStabilityContext.layoutAnimatingLastFrame;
        if (settingsScrollInputBlocked)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);
            ImGui::BeginDisabled();
        }

        const bool generalOpen = DrawCollapsingHeader(
            "General",
            "Show general renderer settings.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (generalOpen)
        {
            BeginDrawerBody(
                "##GeneralBody",
                settingsControlWidth);

            ImGui::TextUnformatted("Interface Skin");
            if (DrawPresetResetIcon(
                    "Interface Skin",
                    m_ui.Skin != DefaultUiSkin))
            {
                QueueDeferredControlUiAction(
                    [this]()
                    {
                        m_ui.Skin = DefaultUiSkin;
                    });
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (BeginRoundedCombo(
                    "##UiSkin",
                    UiSkinLabel(m_ui.Skin).data()))
            {
                for (const UiSkin candidate : UiSkinValues)
                {
                    const bool selected = candidate == m_ui.Skin;
                    DrawDeferredDropdownOption(
                        UiSkinLabel(candidate).data(),
                        UiSkinLabel(candidate).data(),
                        selected,
                        [this, candidate]()
                        {
                            m_ui.Skin = candidate;
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the complete interface appearance. OG uses standard "
                "controls and disables interface motion for fast automated "
                "experiments.");

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
                    DrawDeferredDropdownOption(
                        adapter.name.c_str(),
                        adapter.name.c_str(),
                        selected,
                        [this, adapterIndex = adapter.adapterIndex]()
                        {
                            g_RestartAdapterIndex = adapterIndex;
                            g_RestartRequested = true;
                            glfwSetWindowShouldClose(
                                GetDeviceManager()->GetWindow(),
                                GLFW_TRUE);
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the graphics processor. UVSR restarts after a change.");
        }

        ImGui::TextUnformatted("Camera Mode");
        if (DrawPresetResetIcon(
                "Camera Mode",
                m_ui.Camera != CameraMode::ThirdPerson))
        {
            m_app->SetCameraMode(CameraMode::ThirdPerson);
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (BeginRoundedCombo(
                "##Camera", GetCameraModeLabel(m_ui.Camera)))
        {
            for (CameraMode mode : SelectableCameraModes)
            {
                const bool selected = mode == m_ui.Camera;
                DrawDeferredDropdownOption(
                    GetCameraModeLabel(mode),
                    GetCameraModeLabel(mode),
                    selected,
                    [this, mode]()
                    {
                        m_app->SetCameraMode(mode);
                    });
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip(
            "Choose Freelook or Locked. Space moves up, Shift moves "
            "down, X/C roll, and V levels the roll.");

        if (m_app->HasSponzaCameraLocations())
        {
            ImGui::TextUnformatted("Camera Location");
            const SponzaCameraLocation location =
                m_app->GetSponzaCameraLocation();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (BeginRoundedCombo(
                    "##CameraLocation",
                    GetSponzaCameraLocationLabel(location)))
            {
                for (SponzaCameraLocation candidate :
                    SelectableSponzaCameraLocations)
                {
                    const bool selected = candidate == location;
                    DrawDeferredDropdownOption(
                        GetSponzaCameraLocationLabel(candidate),
                        GetSponzaCameraLocationLabel(candidate),
                        selected,
                        [this, candidate]()
                        {
                            m_app->SetSponzaCameraLocation(candidate);
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
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
                DrawDeferredDropdownOption(
                    scene.DisplayName.c_str(),
                    scene.DisplayName.c_str(),
                    is_selected,
                    [this, sceneName = scene.FileName]()
                    {
                        m_app->SetCurrentSceneName(sceneName);
                    });
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        // A single native button frame matches the neighboring scene combo.
        // The previous two translucent fills composited into a darker surface.
        const bool openSceneFolderPressed = ImGui::Button(
            "##OpenSceneFolder",
            ImVec2(folderButtonWidth, ImGui::GetFrameHeight()));
        const ImVec2 iconMin = ImGui::GetItemRectMin();
        const ImVec2 iconMax = ImGui::GetItemRectMax();
        ImDrawList* folderDrawList = ImGui::GetWindowDrawList();
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

        EndDrawerBody();
        }
        ImGui::Spacing();

        const bool representationOpen = DrawCollapsingHeader(
            "Representation",
            "Configure the world-space hierarchy shared by ray-traced "
            "techniques.");
        if (representationOpen)
        {
            BeginDrawerBody(
                "##RepresentationBody",
                settingsControlWidth);

            WorldSpaceRepresentationSettings& representation =
                m_ui.Representation;
            const WorldSpaceRepresentationSettings representationDefaults{};
            const WorldSpaceRepresentationStatus& representationStatus =
                m_app->GetWorldSpaceRepresentationStatus();
            const char* representationState = "Inactive";
            switch (representationStatus.state)
            {
            case WorldSpaceRepresentationState::Unsupported:
                representationState = "Unsupported";
                break;
            case WorldSpaceRepresentationState::BuildingBlas:
                representationState = "Building BLAS";
                break;
            case WorldSpaceRepresentationState::BuildingTlas:
                representationState = "Building TLAS";
                break;
            case WorldSpaceRepresentationState::Ready:
                representationState = "Ready";
                break;
            case WorldSpaceRepresentationState::Failed:
                representationState = "Failed";
                break;
            case WorldSpaceRepresentationState::Idle:
            default:
                break;
            }
            ImGui::Text("Status: %s", representationState);
            if (representationStatus.totalBlasCount > 0u)
            {
                ImGui::TextDisabled(
                    "BLAS %u/%u  |  TLAS Instances %u",
                    representationStatus.builtBlasCount,
                    representationStatus.totalBlasCount,
                    representationStatus.instanceCount);
            }
            else if (!representationStatus.accelerationStructuresSupported ||
                !representationStatus.rayQueriesSupported)
            {
                ImGui::TextDisabled(
                    "Requires DirectX Raytracing 1.1 inline ray queries.");
            }
            else
            {
                ImGui::TextDisabled(
                    "Builds lazily when a ray-traced technique needs it.");
            }

            if (BeginAnimatedTreeNode(
                    "Bounding Volume Hierarchy##Representation",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure the shared world-space BVH family."))
            {
                static constexpr const char* BuildPreferenceLabels[] = {
                    "Fast Trace", "Balanced", "Fast Build"
                };
                const int buildPreferenceIndex = std::clamp(
                    int(representation.bvhBuildPreference),
                    0,
                    int(std::size(BuildPreferenceLabels)) - 1);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Build Preference##BVH",
                        BuildPreferenceLabels[buildPreferenceIndex]))
                {
                    for (int index = 0;
                        index < int(std::size(BuildPreferenceLabels));
                        ++index)
                    {
                        const BvhBuildPreference candidate =
                            BvhBuildPreference(index);
                        DrawDeferredDropdownOption(
                            BuildPreferenceLabels[index],
                            BuildPreferenceLabels[index],
                            representation.bvhBuildPreference == candidate,
                            [settings = &representation,
                                app = m_app,
                                candidate]()
                            {
                                const WorldSpaceRepresentationSettings before =
                                    *settings;
                                settings->bvhBuildPreference = candidate;
                                app->InvalidateWorldSpaceRepresentation(
                                    GetWorldSpaceRepresentationInvalidation(
                                        before, *settings));
                            });
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose whether BVH construction prioritizes ray traversal, "
                    "balanced work, or construction speed.");
                if (DrawNestedDropdownResetIcon(
                        "RepresentationBvhBuildPreference",
                        representation.bvhBuildPreference !=
                            representationDefaults.bvhBuildPreference))
                {
                    QueueDeferredControlUiAction(
                        [settings = &representation,
                            app = m_app,
                            defaultValue = representationDefaults
                                .bvhBuildPreference]()
                        {
                            const WorldSpaceRepresentationSettings before =
                                *settings;
                            settings->bvhBuildPreference = defaultValue;
                            app->InvalidateWorldSpaceRepresentation(
                                GetWorldSpaceRepresentationInvalidation(
                                    before, *settings));
                        });
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Bottom-Level Acceleration Structures##Representation",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure per-mesh triangle acceleration structures."))
            {
                static constexpr const char* BlasUpdateLabels[] = {
                    "Rebuild", "Refit"
                };
                const int updateIndex = std::clamp(
                    int(representation.blasUpdateMode),
                    0,
                    int(std::size(BlasUpdateLabels)) - 1);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Dynamic Updates##BLAS",
                        BlasUpdateLabels[updateIndex]))
                {
                    for (int index = 0;
                        index < int(std::size(BlasUpdateLabels));
                        ++index)
                    {
                        const BlasUpdateMode candidate =
                            BlasUpdateMode(index);
                        DrawDeferredDropdownOption(
                            BlasUpdateLabels[index],
                            BlasUpdateLabels[index],
                            representation.blasUpdateMode == candidate,
                            [settings = &representation,
                                app = m_app,
                                candidate]()
                            {
                                const WorldSpaceRepresentationSettings before =
                                    *settings;
                                settings->blasUpdateMode = candidate;
                                app->InvalidateWorldSpaceRepresentation(
                                    GetWorldSpaceRepresentationInvalidation(
                                        before, *settings));
                            });
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Rebuild or refit changed skinned-mesh BLAS geometry.");
                if (DrawNestedDropdownResetIcon(
                        "RepresentationBlasUpdateMode",
                        representation.blasUpdateMode !=
                            representationDefaults.blasUpdateMode))
                {
                    QueueDeferredControlUiAction(
                        [settings = &representation,
                            app = m_app,
                            defaultValue = representationDefaults
                                .blasUpdateMode]()
                        {
                            const WorldSpaceRepresentationSettings before =
                                *settings;
                            settings->blasUpdateMode = defaultValue;
                            app->InvalidateWorldSpaceRepresentation(
                                GetWorldSpaceRepresentationInvalidation(
                                    before, *settings));
                        });
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Top-Level Acceleration Structure##Representation",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure the instance hierarchy consumed by ray queries."))
            {
                static constexpr const char* TlasUpdateLabels[] = {
                    "Rebuild", "Refit"
                };
                const int updateIndex = std::clamp(
                    int(representation.tlasUpdateMode),
                    0,
                    int(std::size(TlasUpdateLabels)) - 1);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Transform Updates##TLAS",
                        TlasUpdateLabels[updateIndex]))
                {
                    for (int index = 0;
                        index < int(std::size(TlasUpdateLabels));
                        ++index)
                    {
                        const TlasUpdateMode candidate =
                            TlasUpdateMode(index);
                        DrawDeferredDropdownOption(
                            TlasUpdateLabels[index],
                            TlasUpdateLabels[index],
                            representation.tlasUpdateMode == candidate,
                            [settings = &representation,
                                app = m_app,
                                candidate]()
                            {
                                const WorldSpaceRepresentationSettings before =
                                    *settings;
                                settings->tlasUpdateMode = candidate;
                                app->InvalidateWorldSpaceRepresentation(
                                    GetWorldSpaceRepresentationInvalidation(
                                        before, *settings));
                            });
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Rebuild or refit the TLAS when instance transforms change.");
                if (DrawNestedDropdownResetIcon(
                        "RepresentationTlasUpdateMode",
                        representation.tlasUpdateMode !=
                            representationDefaults.tlasUpdateMode))
                {
                    QueueDeferredControlUiAction(
                        [settings = &representation,
                            app = m_app,
                            defaultValue = representationDefaults
                                .tlasUpdateMode]()
                        {
                            const WorldSpaceRepresentationSettings before =
                                *settings;
                            settings->tlasUpdateMode = defaultValue;
                            app->InvalidateWorldSpaceRepresentation(
                                GetWorldSpaceRepresentationInvalidation(
                                    before, *settings));
                        });
                }
                EndAnimatedTreeNode();
            }

            EndDrawerBody();
        }
        ImGui::Spacing();

        const auto drawRatioEstimatorShadowControls = [&]()
        {
            if (BeginAnimatedTreeNode(
                    "Ratio-Estimator Ray-Traced Shadows##Shadows",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure correlated stochastic numerator and denominator "
                    "estimates for the directional emitter."))
            {
                HeitzRatioEstimatorShadowSettings& ratio =
                    m_ui.DirectionalShadows.ratioEstimator;
                const HeitzRatioEstimatorShadowSettings ratioDefaults{};
                const bool ratioAvailable =
                    m_app->HasPrimaryDirectionalLight() &&
                    m_app->SupportsHeitzRatioEstimatorShadows();
                const bool disableRatioEnable =
                    !ratioAvailable && !ratio.enabled;
                if (disableRatioEnable)
                    ImGui::BeginDisabled();
                if (ImGui::Checkbox(
                        "Enabled##RatioEstimatorShadows",
                        &ratio.enabled))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Trace world-space directional-shadow rays independently "
                    "of the screen-space technique.");
                if (disableRatioEnable)
                    ImGui::EndDisabled();
                if (DrawPresetResetIcon(
                        "RatioEstimatorShadowEnabled",
                        ratio.enabled != ratioDefaults.enabled))
                {
                    ratio.enabled = ratioDefaults.enabled;
                    m_app->ResetImageBasedLightingHistory();
                }
                if (BeginAnimatedToggleRegion(
                        "##RatioEstimatorShadowControls",
                        ratio.enabled && ratioAvailable))
                {
                    if (ImGui::Checkbox(
                            "Hard Shadows##RatioEstimatorShadows",
                            &ratio.hardShadows))
                    {
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        "Trace one center ray and skip emitter sampling, full "
                        "material preparation, and ratio evaluation. The "
                        "light's Angular Size is preserved for soft mode.");
                    if (DrawPresetResetIcon(
                            "RatioEstimatorHardShadows",
                            ratio.hardShadows !=
                                ratioDefaults.hardShadows))
                    {
                        ratio.hardShadows = ratioDefaults.hardShadows;
                        m_app->ResetImageBasedLightingHistory();
                    }

                    const std::shared_ptr<DirectionalLight> primaryLight =
                        m_app->GetPrimaryDirectionalLight();
                    const float angularSize = primaryLight
                        ? primaryLight->angularSize
                        : 0.f;
                    const bool softSamplingControlsEnabled =
                        !ratio.hardShadows && angularSize > 1e-4f;
                    if (!softSamplingControlsEnabled)
                        ImGui::BeginDisabled();
                    if (ImGui::Checkbox(
                            "Animate Samples##RatioEstimatorShadows",
                            &ratio.animateSamples))
                    {
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        "Advance emitter samples after each stochastic shadow "
                        "dispatch. This changes the frame-local sample set with "
                        "or without TAA; only the renderer's final-color TAA "
                        "may accumulate the result over time.");
                    if (DrawPresetResetIcon(
                            "RatioEstimatorShadowAnimateSamples",
                            ratio.animateSamples !=
                                ratioDefaults.animateSamples))
                    {
                        ratio.animateSamples =
                            ratioDefaults.animateSamples;
                        m_app->ResetImageBasedLightingHistory();
                    }

                    int sampleRateLog2 = ratio.sampleRateLog2;
                    const std::string_view sampleRateLabel =
                        GetHeitzRatioEstimatorSampleRateLabel(
                            sampleRateLog2);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (ImGui::SliderInt(
                            "Samples Per Pixel##RatioEstimatorShadows",
                            &sampleRateLog2,
                            HeitzRatioEstimatorMinimumSampleRateLog2,
                            HeitzRatioEstimatorMaximumSampleRateLog2,
                            sampleRateLabel.data(),
                            ImGuiSliderFlags_AlwaysClamp))
                    {
                        ratio.sampleRateLog2 = sampleRateLog2;
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        "Choose 1 through 64 matched rays per pixel. Every "
                        "numerator and denominator sample is evaluated in the "
                        "current frame; this shadow pass keeps no private "
                        "temporal history.");
                    if (DrawPresetResetIcon(
                            "RatioEstimatorShadowSamples",
                            ratio.sampleRateLog2 !=
                                ratioDefaults.sampleRateLog2))
                    {
                        ratio.sampleRateLog2 =
                            ratioDefaults.sampleRateLog2;
                        m_app->ResetImageBasedLightingHistory();
                    }

                    static constexpr const char* NoisePatternLabels[] = {
                        "Permutated White Noise",
                        "Void Cluster Blue Noise"
                    };
                    int noisePattern =
                        static_cast<int>(ratio.noisePattern);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (ImGui::Combo(
                            "Noise Pattern##RatioEstimatorShadows",
                            &noisePattern,
                            NoisePatternLabels,
                            static_cast<int>(
                                std::size(NoisePatternLabels))))
                    {
                        ratio.noisePattern =
                            static_cast<HeitzRatioEstimatorNoisePattern>(
                                noisePattern);
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        "Choose the current-frame emitter-direction noise "
                        "sequence.");
                    if (DrawPresetResetIcon(
                            "RatioEstimatorShadowNoisePattern",
                            ratio.noisePattern !=
                                ratioDefaults.noisePattern))
                    {
                        ratio.noisePattern =
                            ratioDefaults.noisePattern;
                        m_app->ResetImageBasedLightingHistory();
                    }

                    if (!softSamplingControlsEnabled)
                        ImGui::EndDisabled();

                    if (DrawSliderFloat(
                            "Ray Bias##RatioEstimatorShadows",
                            &ratio.rayBias,
                            0.f,
                            HeitzRatioEstimatorMaximumRayBias,
                            "%.4f"))
                    {
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        "Move the ray origin this many world units along the "
                        "view-facing raster triangle normal. This does not add "
                        "rays or shorten their reach; larger values can detach "
                        "contact shadows.");
                    if (DrawPresetResetIcon(
                            "RatioEstimatorShadowRayBias",
                            ratio.rayBias != ratioDefaults.rayBias))
                    {
                        ratio.rayBias = ratioDefaults.rayBias;
                        m_app->ResetImageBasedLightingHistory();
                    }

                    if (ratio.hardShadows)
                    {
                        ImGui::TextDisabled(
                            "Hard Shadows uses one center ray; soft settings are preserved.");
                    }
                    else if (!(angularSize > 1e-4f))
                    {
                        ImGui::TextDisabled(
                            "Angular Size is 0 deg: this directional emitter has zero extent.");
                    }

                    const WorldSpaceRepresentationStatus& status =
                        m_app->GetWorldSpaceRepresentationStatus();
                    if (status.state ==
                        WorldSpaceRepresentationState::BuildingBlas ||
                        status.state ==
                            WorldSpaceRepresentationState::BuildingTlas)
                    {
                        ImGui::TextDisabled(
                            "Preparing world hierarchy: BLAS %u/%u.",
                            status.builtBlasCount,
                            status.totalBlasCount);
                    }
                    EndAnimatedToggleRegion();
                }
                EndAnimatedTreeNode();
            }
        };

        const bool indirectLightingOpen = DrawCollapsingHeader(
            "Visibility",
            "Configure ambient occlusion, indirect diffuse, sampling, "
            "reconstruction, and buffer precision.");
        if (indirectLightingOpen)
        {
            BeginDrawerBody("##VisibilityBody", settingsControlWidth);
            ScreenSpaceVisibilitySettings& visibility =
                m_ui.ScreenSpaceVisibility;
            const auto finishVisibilityEdit =
                [](ScreenSpaceVisibilitySettings& settings)
            {
                MarkScreenSpaceVisibilityQualityCustom(settings);
                ReconcileScreenSpaceVisibilityQualityPreset(settings);
            };
            ScreenSpaceVisibilityQuality profileOrigin =
                visibility.quality == ScreenSpaceVisibilityQuality::Custom
                ? visibility.qualityPresetOrigin
                : visibility.quality;
            if (profileOrigin == ScreenSpaceVisibilityQuality::Custom)
                profileOrigin = ScreenSpaceVisibilityQuality::High;
            ScreenSpaceVisibilitySettings profileDefaults = visibility;
            ApplyScreenSpaceVisibilityQualityPreset(
                profileDefaults, profileOrigin);

            if (ImGui::Checkbox("Enabled", &visibility.enabled))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Enable screen-space ambient occlusion and indirect diffuse. "
                "Other lighting and material effects remain independent.");
            if (DrawPresetResetIcon(
                    "VisibilityEnabled",
                    visibility.enabled != profileDefaults.enabled))
            {
                visibility.enabled = profileDefaults.enabled;
                finishVisibilityEdit(visibility);
            }

            static constexpr const char* QualityLabels[] = {
                "Low", "Medium", "High", "Ultra"
            };
            const int profileIndex = std::clamp(
                static_cast<int>(profileOrigin),
                0,
                static_cast<int>(std::size(QualityLabels)) - 1);
            std::string profilePreview = QualityLabels[profileIndex];
            if (visibility.quality ==
                ScreenSpaceVisibilityQuality::Custom)
            {
                profilePreview += " (Custom)";
            }
            SetNextLabeledControlWidth(
                "Profile##Visibility", settingsControlWidth);
            if (BeginRoundedCombo(
                    "Profile##Visibility",
                    profilePreview.c_str()))
            {
                static constexpr const char* ProfileTooltips[] = {
                    "Quarter-resolution projected-angle sampling with eight samples, Void Cluster Blue Noise, joint-bilateral reconstruction, and 16-bit buffers.",
                    "Half-resolution solid-angle sampling with eight samples, Void Cluster Blue Noise, joint-bilateral reconstruction, and 16-bit buffers.",
                    "Full-resolution solid-angle sampling with twenty samples, Void Cluster Blue Noise, and 16-bit buffers.",
                    "Full-resolution solid-angle sampling with forty-eight samples, Void Cluster Blue Noise, and 32-bit buffers."
                };
                for (int index = 0;
                    index < static_cast<int>(std::size(QualityLabels));
                    ++index)
                {
                    const auto selected =
                        static_cast<ScreenSpaceVisibilityQuality>(index);
                    DrawDeferredDropdownOption(
                        QualityLabels[index],
                        QualityLabels[index],
                        visibility.quality == selected,
                        [settings = &visibility, selected]()
                        {
                            ApplyScreenSpaceVisibilityQualityPreset(
                                *settings, selected);
                        });
                    ImGui::SetItemTooltip("%s", ProfileTooltips[index]);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose a complete visibility recipe. Individual changes "
                "retain the originating profile and append (Custom); the "
                "circular arrow restores the complete High recipe.");
            if (DrawPresetResetIcon(
                    "VisibilityProfile",
                    !MatchesScreenSpaceVisibilityQualityPreset(
                        visibility, ScreenSpaceVisibilityQuality::High),
                    "Restore the complete High profile."))
            {
                QueueDeferredControlUiAction(
                    [settings = &visibility]()
                    {
                        ApplyScreenSpaceVisibilityQualityPreset(
                            *settings,
                            ScreenSpaceVisibilityQuality::High);
                    });
            }

            if (BeginAnimatedToggleRegion(
                    "##VisibilityControls", visibility.enabled))
            {

            if (BeginAnimatedTreeNode(
                    "Ambient Occlusion##Visibility",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure contact darkening from nearby geometry."))
            {
            if (ImGui::Checkbox(
                    "Enabled##VisibilityAmbient",
                    &visibility.ambientOcclusion.enabled))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Darken surfaces whose nearby geometry blocks ambient light.");
            if (DrawPresetResetIcon(
                    "VisibilityAmbientEnabled",
                    visibility.ambientOcclusion.enabled !=
                        profileDefaults.ambientOcclusion.enabled))
            {
                visibility.ambientOcclusion.enabled =
                    profileDefaults.ambientOcclusion.enabled;
                finishVisibilityEdit(visibility);
            }
            if (BeginAnimatedToggleRegion(
                    "##VisibilityAmbientControls",
                    visibility.ambientOcclusion.enabled))
            {
            if (ImGui::SliderFloat(
                    "Strength", &visibility.ambientOcclusion.strength,
                    0.f, 4.f, "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Scale how strongly nearby occluders darken the final image.");
            if (DrawPresetResetIcon(
                    "VisibilityAmbientStrength",
                    visibility.ambientOcclusion.strength !=
                        profileDefaults.ambientOcclusion.strength))
            {
                visibility.ambientOcclusion.strength =
                    profileDefaults.ambientOcclusion.strength;
                finishVisibilityEdit(visibility);
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Indirect Diffuse##Visibility",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure diffuse light reflected from visible surfaces."))
            {
            if (ImGui::Checkbox(
                    "Enabled##VisibilityIndirect",
                    &visibility.indirectDiffuse.enabled))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Add diffuse light reflected from nearby visible surfaces.");
            if (DrawPresetResetIcon(
                    "VisibilityIndirectEnabled",
                    visibility.indirectDiffuse.enabled !=
                        profileDefaults.indirectDiffuse.enabled))
            {
                visibility.indirectDiffuse.enabled =
                    profileDefaults.indirectDiffuse.enabled;
                finishVisibilityEdit(visibility);
            }
            if (BeginAnimatedToggleRegion(
                    "##VisibilityIndirectControls",
                    visibility.indirectDiffuse.enabled))
            {
            if (ImGui::SliderFloat(
                    "Intensity",
                    &visibility.indirectDiffuse.intensity,
                    0.f, 16.f, "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Scale the diffuse light gathered from nearby surfaces.");
            if (DrawPresetResetIcon(
                    "VisibilityIndirectIntensity",
                    visibility.indirectDiffuse.intensity !=
                        profileDefaults.indirectDiffuse.intensity))
            {
                visibility.indirectDiffuse.intensity =
                    profileDefaults.indirectDiffuse.intensity;
                finishVisibilityEdit(visibility);
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Sampling##Visibility",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Configure where and how visibility rays sample the scene."))
            {
            static constexpr const char* ResolutionLabels[] = {
                "Full Resolution", "Half Resolution", "Quarter Resolution"
            };
            int resolution = static_cast<int>(visibility.resolution);
            SetNextLabeledControlWidth(
                "Sampling Resolution", settingsControlWidth);
            if (ImGui::Combo(
                    "Sampling Resolution", &resolution, ResolutionLabels,
                    static_cast<int>(std::size(ResolutionLabels))))
            {
                visibility.resolution =
                    static_cast<VisibilityResolution>(resolution);
                finishVisibilityEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Choose the resolution used for visibility tracing. Reduced "
                "resolutions are reconstructed before composition.");
            if (DrawNestedDropdownResetIcon(
                    "VisibilityResolution",
                    visibility.resolution != profileDefaults.resolution))
            {
                visibility.resolution = profileDefaults.resolution;
                finishVisibilityEdit(visibility);
            }

            static constexpr const char* EstimatorLabels[] = {
                "Uniform Projected Angle",
                "Uniform Solid Angle",
                "Cosine-Weighted Solid Angle"
            };
            int estimator = static_cast<int>(visibility.estimator);
            SetNextLabeledControlWidth(
                "Estimator", settingsControlWidth);
            if (ImGui::Combo(
                    "Estimator", &estimator, EstimatorLabels,
                    static_cast<int>(std::size(EstimatorLabels))))
            {
                visibility.estimator =
                    static_cast<VisibilityEstimator>(estimator);
                finishVisibilityEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Choose how directions are distributed around each receiver.");
            if (DrawNestedDropdownResetIcon(
                    "VisibilityEstimator",
                    visibility.estimator != profileDefaults.estimator))
            {
                visibility.estimator = profileDefaults.estimator;
                finishVisibilityEdit(visibility);
            }

            static constexpr const char* NoiseLabels[] = {
                "Permutated White Noise",
                "Void Cluster Blue Noise"
            };
            int scheduler =
                static_cast<int>(visibility.sampling.scheduler);
            SetNextLabeledControlWidth(
                "Noise Pattern", settingsControlWidth);
            if (ImGui::Combo(
                    "Noise Pattern", &scheduler, NoiseLabels,
                    static_cast<int>(std::size(NoiseLabels))))
            {
                visibility.sampling.scheduler =
                    static_cast<VisibilitySampleScheduler>(scheduler);
                finishVisibilityEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Permutated White Noise is the baseline; Void Cluster Blue "
                "Noise distributes error more evenly.");
            if (DrawNestedDropdownResetIcon(
                    "VisibilityNoise",
                    visibility.sampling.scheduler !=
                        profileDefaults.sampling.scheduler))
            {
                visibility.sampling.scheduler =
                    profileDefaults.sampling.scheduler;
                finishVisibilityEdit(visibility);
            }

            int samples =
                static_cast<int>(visibility.sampling.maximumSampleCount);
            if (ImGui::SliderInt("Samples", &samples, 1, 64))
            {
                visibility.sampling.maximumSampleCount =
                    static_cast<uint32_t>(samples);
                finishVisibilityEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Set the number of visibility samples traced per pixel.");
            if (DrawPresetResetIcon(
                    "VisibilitySamples",
                    visibility.sampling.maximumSampleCount !=
                        profileDefaults.sampling.maximumSampleCount))
            {
                visibility.sampling.maximumSampleCount =
                    profileDefaults.sampling.maximumSampleCount;
                finishVisibilityEdit(visibility);
            }
            if (ImGui::SliderFloat(
                    "Radius", &visibility.sampling.radius,
                    0.1f, 10.f, "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Set the world-space reach of nearby visibility samples.");
            if (DrawPresetResetIcon(
                    "VisibilityRadius",
                    visibility.sampling.radius !=
                        profileDefaults.sampling.radius))
            {
                visibility.sampling.radius =
                    profileDefaults.sampling.radius;
                finishVisibilityEdit(visibility);
            }
            if (ImGui::SliderFloat(
                    "Thickness", &visibility.sampling.thickness,
                    0.01f, 2.f, "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Set the accepted thickness of potential occluding surfaces.");
            if (DrawPresetResetIcon(
                    "VisibilityThickness",
                    visibility.sampling.thickness !=
                        profileDefaults.sampling.thickness))
            {
                visibility.sampling.thickness =
                    profileDefaults.sampling.thickness;
                finishVisibilityEdit(visibility);
            }
            if (ImGui::SliderFloat(
                    "Distribution",
                    &visibility.sampling.stepDistributionExponent,
                    0.25f, 4.f, "%.2f"))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Bias samples toward the receiver or toward the trace edge.");
            if (DrawPresetResetIcon(
                    "VisibilityDistribution",
                    visibility.sampling.stepDistributionExponent !=
                        profileDefaults.sampling.stepDistributionExponent))
            {
                visibility.sampling.stepDistributionExponent =
                    profileDefaults.sampling.stepDistributionExponent;
                finishVisibilityEdit(visibility);
            }
            EndAnimatedTreeNode();
            }

            const ImGuiTreeNodeFlags reconstructionDisclosureFlags =
                visibility.resolution == VisibilityResolution::Full
                ? ImGuiTreeNodeFlags_None
                : ImGuiTreeNodeFlags_DefaultOpen;
            if (BeginAnimatedTreeNode(
                    "Reconstruction##Visibility",
                    reconstructionDisclosureFlags,
                    "Configure full-resolution reconstruction and optional smoothing."))
            {
            const char* directReconstructionLabel =
                visibility.resolution == VisibilityResolution::Full
                ? "Full Resolution"
                : "Guide-Aware Upsampling";
            const char* ReconstructionLabels[] = {
                directReconstructionLabel,
                "Packed Depth-Normal",
                "Packed Slope-Aware",
                "Packed Leak-Controlled"
            };
            int reconstruction =
                static_cast<int>(visibility.reconstruction.mode);
            SetNextLabeledControlWidth(
                "Method", settingsControlWidth);
            if (ImGui::Combo(
                    "Method", &reconstruction, ReconstructionLabels,
                    static_cast<int>(std::size(ReconstructionLabels))))
            {
                visibility.reconstruction.mode =
                    static_cast<VisibilityReconstructionMode>(
                        reconstruction);
                finishVisibilityEdit(visibility);
            }
            ImGui::SetItemTooltip(
                visibility.resolution == VisibilityResolution::Full
                ? "Use the already full-resolution trace directly. Packed "
                    "modes add edge-aware reconstruction metadata."
                : "Upsample reduced-resolution traces with depth and normal "
                    "guides. Packed modes use compact edge metadata.");
            if (DrawNestedDropdownResetIcon(
                    "VisibilityReconstruction",
                    visibility.reconstruction.mode !=
                        profileDefaults.reconstruction.mode))
            {
                visibility.reconstruction.mode =
                    profileDefaults.reconstruction.mode;
                finishVisibilityEdit(visibility);
            }

            const bool packedReconstruction =
                IsPackedVisibilityReconstruction(
                    visibility.reconstruction.mode);
            if (BeginAnimatedToggleRegion(
                    "##VisibilitySpatialFilterAvailability",
                    !packedReconstruction))
            {
            if (ImGui::Checkbox(
                    "Spatial Filter",
                    &visibility.reconstruction.spatialEnabled))
                finishVisibilityEdit(visibility);
            ImGui::SetItemTooltip(
                "Smooth direct or upsampled visibility while respecting "
                "surface depth and orientation.");
            if (DrawPresetResetIcon(
                    "VisibilitySpatialEnabled",
                    visibility.reconstruction.spatialEnabled !=
                        profileDefaults.reconstruction.spatialEnabled))
            {
                visibility.reconstruction.spatialEnabled =
                    profileDefaults.reconstruction.spatialEnabled;
                finishVisibilityEdit(visibility);
            }
            if (BeginAnimatedToggleRegion(
                    "##VisibilitySpatialFilterControls",
                    visibility.reconstruction.spatialEnabled))
            {
                static constexpr const char* FilterLabels[] = {
                    "Joint Bilateral", "Gaussian Bilateral"
                };
                int filter = static_cast<int>(
                    visibility.reconstruction.spatialFilter);
                SetNextLabeledControlWidth(
                    "Filter", settingsControlWidth);
                if (ImGui::Combo(
                        "Filter", &filter, FilterLabels,
                        static_cast<int>(std::size(FilterLabels))))
                {
                    visibility.reconstruction.spatialFilter =
                        static_cast<VisibilitySpatialFilter>(filter);
                    finishVisibilityEdit(visibility);
                }
                ImGui::SetItemTooltip(
                    "Choose the edge-aware weighting used by the spatial filter.");
                if (DrawPresetResetIcon(
                        "VisibilitySpatialFilter",
                        visibility.reconstruction.spatialFilter !=
                            profileDefaults.reconstruction.spatialFilter))
                {
                    visibility.reconstruction.spatialFilter =
                        profileDefaults.reconstruction.spatialFilter;
                    finishVisibilityEdit(visibility);
                }
                if (ImGui::SliderFloat(
                        "Radius##VisibilitySpatial",
                        &visibility.reconstruction.spatialRadius,
                        1.f, 8.f, "%.1f"))
                    finishVisibilityEdit(visibility);
                ImGui::SetItemTooltip(
                    "Set the sampling radius used by the spatial filter.");
                if (DrawPresetResetIcon(
                        "VisibilitySpatialRadius",
                        visibility.reconstruction.spatialRadius !=
                            profileDefaults.reconstruction.spatialRadius))
                {
                    visibility.reconstruction.spatialRadius =
                        profileDefaults.reconstruction.spatialRadius;
                    finishVisibilityEdit(visibility);
                }
                EndAnimatedToggleRegion();
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            EndAnimatedToggleRegion();
            }

            EndDrawerBody();
        }
        ImGui::Spacing();
        const bool buffersOpen = DrawCollapsingHeader(
            "Buffers",
            "Configure the retained visibility buffer precision controls.");
        if (buffersOpen)
        {
            BeginDrawerBody("##BuffersBody", settingsControlWidth);
            ScreenSpaceVisibilitySettings& visibility =
                m_ui.ScreenSpaceVisibility;
            ScreenSpaceVisibilityQuality profileOrigin =
                visibility.quality == ScreenSpaceVisibilityQuality::Custom
                ? visibility.qualityPresetOrigin
                : visibility.quality;
            if (profileOrigin == ScreenSpaceVisibilityQuality::Custom)
                profileOrigin = ScreenSpaceVisibilityQuality::High;
            ScreenSpaceVisibilitySettings profileDefaults = visibility;
            ApplyScreenSpaceVisibilityQualityPreset(
                profileDefaults, profileOrigin);
            const auto finishBufferEdit =
                [](ScreenSpaceVisibilitySettings& settings)
            {
                MarkScreenSpaceVisibilityQualityCustom(settings);
                ReconcileScreenSpaceVisibilityQualityPreset(settings);
            };

            const bool ambient16 =
                visibility.bufferPrecision.ambient ==
                    VisibilityScalarBufferPrecision::Float16;
            const bool indirect16 =
                visibility.bufferPrecision.indirect ==
                    VisibilityVectorBufferPrecision::Rgba16Float;
            const int bufferProfile = ambient16
                ? (indirect16 ? 0 : 2)
                : (indirect16 ? 3 : 1);
            static constexpr const char* BufferProfileLabels[] = {
                "Performance",
                "Maximum Precision",
                "Compact Occlusion",
                "Compact Indirect"
            };
            SetNextLabeledControlWidth(
                "Profile##Buffers", settingsControlWidth);
            if (BeginRoundedCombo(
                    "Profile##Buffers",
                    BufferProfileLabels[bufferProfile]))
            {
                static constexpr bool Ambient16[] = {
                    true, false, true, false
                };
                static constexpr bool Indirect16[] = {
                    true, false, false, true
                };
                static constexpr const char* BufferProfileTooltips[] = {
                    "Use 16-bit floating point for both retained visibility buffers.",
                    "Use 32-bit floating point for both retained visibility buffers.",
                    "Use 16-bit ambient occlusion and 32-bit indirect diffuse.",
                    "Use 32-bit ambient occlusion and 16-bit indirect diffuse."
                };
                for (int index = 0;
                    index < static_cast<int>(
                        std::size(BufferProfileLabels));
                    ++index)
                {
                    DrawDeferredDropdownOption(
                        BufferProfileLabels[index],
                        BufferProfileLabels[index],
                        index == bufferProfile,
                        [settings = &visibility, index]()
                        {
                            ApplyVisibilityBufferPrecisionPreset(
                                settings->bufferPrecision,
                                Ambient16[index],
                                Indirect16[index]);
                            MarkScreenSpaceVisibilityQualityCustom(*settings);
                            ReconcileScreenSpaceVisibilityQualityPreset(
                                *settings);
                        });
                    ImGui::SetItemTooltip(
                        "%s", BufferProfileTooltips[index]);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose a compact precision combination for the two "
                "visibility buffers that remain in production.");
            const bool bufferProfileModified =
                visibility.bufferPrecision.ambient !=
                    profileDefaults.bufferPrecision.ambient ||
                visibility.bufferPrecision.indirect !=
                    profileDefaults.bufferPrecision.indirect;
            if (DrawPresetResetIcon(
                    "VisibilityBufferProfile",
                    bufferProfileModified,
                    "Restore both buffer precisions to the originating visibility profile."))
            {
                visibility.bufferPrecision =
                    profileDefaults.bufferPrecision;
                finishBufferEdit(visibility);
            }

            static constexpr const char* PrecisionLabels[] = {
                "16-Bit Floating Point", "32-Bit Floating Point"
            };
            int ambientPrecision = ambient16 ? 0 : 1;
            SetNextLabeledControlWidth(
                "Ambient Occlusion", settingsControlWidth);
            if (ImGui::Combo(
                    "Ambient Occlusion",
                    &ambientPrecision,
                    PrecisionLabels,
                    static_cast<int>(std::size(PrecisionLabels))))
            {
                visibility.bufferPrecision.ambient =
                    ambientPrecision == 0
                    ? VisibilityScalarBufferPrecision::Float16
                    : VisibilityScalarBufferPrecision::Float32;
                finishBufferEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Set the storage precision of the ambient visibility field.");
            if (DrawPresetResetIcon(
                    "VisibilityAmbientPrecision",
                    visibility.bufferPrecision.ambient !=
                        profileDefaults.bufferPrecision.ambient))
            {
                visibility.bufferPrecision.ambient =
                    profileDefaults.bufferPrecision.ambient;
                finishBufferEdit(visibility);
            }

            int indirectPrecision = indirect16 ? 0 : 1;
            SetNextLabeledControlWidth(
                "Indirect Diffuse", settingsControlWidth);
            if (ImGui::Combo(
                    "Indirect Diffuse",
                    &indirectPrecision,
                    PrecisionLabels,
                    static_cast<int>(std::size(PrecisionLabels))))
            {
                visibility.bufferPrecision.indirect =
                    indirectPrecision == 0
                    ? VisibilityVectorBufferPrecision::Rgba16Float
                    : VisibilityVectorBufferPrecision::Rgba32Float;
                finishBufferEdit(visibility);
            }
            ImGui::SetItemTooltip(
                "Set the storage precision of the indirect diffuse field.");
            if (DrawPresetResetIcon(
                    "VisibilityIndirectPrecision",
                    visibility.bufferPrecision.indirect !=
                        profileDefaults.bufferPrecision.indirect))
            {
                visibility.bufferPrecision.indirect =
                    profileDefaults.bufferPrecision.indirect;
                finishBufferEdit(visibility);
            }

            EndDrawerBody();
        }
        ImGui::Spacing();
        const bool statisticsOpen = DrawCollapsingHeader(
            "Statistics",
            "Inspect frame performance and the retained renderer effects.");
        if (statisticsOpen)
        {
            BeginDrawerBody("##StatisticsBody", settingsControlWidth);

            const std::string performanceLine =
                BuildPerformanceLine(m_PerformanceStatValues);
            ImGui::TextWrapped("%s", performanceLine.c_str());

            static constexpr const char* StatisticsEffectLabels[] = {
                "Complete Renderer",
                "Scene Setup",
                "Geometry",
                "Direct Lighting",
                "Screen-Space Visibility",
                "Directional Shadows",
                "Temporal Reconstructive",
                "Fast Approximate",
                "Conservative Morphological",
                "Multisample Adaptive",
                "Material Picking",
                "Environment Background",
                "Tone Mapping",
                "Output Blit"
            };
            static_assert(
                std::size(StatisticsEffectLabels) ==
                static_cast<size_t>(StatisticsEffect::Count));
            static constexpr int DefaultStatisticsEffect =
                static_cast<int>(StatisticsEffect::CompleteRenderer);
            m_StatisticsEffect = std::clamp(
                m_StatisticsEffect,
                0,
                static_cast<int>(StatisticsEffect::Count) - 1);
            ImGui::TextUnformatted("Effect");
            if (DrawPresetResetIcon(
                    "StatisticsEffect",
                    m_StatisticsEffect != DefaultStatisticsEffect))
            {
                m_StatisticsEffect = DefaultStatisticsEffect;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (BeginRoundedCombo(
                    "##StatisticsEffect",
                    StatisticsEffectLabels[m_StatisticsEffect]))
            {
                for (int index = 0;
                    index < static_cast<int>(
                        std::size(StatisticsEffectLabels));
                    ++index)
                {
                    DrawDeferredDropdownOption(
                        StatisticsEffectLabels[index],
                        StatisticsEffectLabels[index],
                        m_StatisticsEffect == index,
                        [selected = &m_StatisticsEffect, index]()
                        {
                            *selected = index;
                        });
                    if (m_StatisticsEffect == index)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the renderer effect whose graphics cost is shown below.");
            const StatisticsEffect selectedEffect =
                static_cast<StatisticsEffect>(m_StatisticsEffect);

            const RendererTimings& timings =
                m_app->GetRendererTimings();
            static constexpr ImGuiTableFlags StatisticsTableFlags =
                ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp;
            const auto beginStatisticsTable =
                [](const char* identifier, const char* firstColumn)
            {
                if (!ImGui::BeginTable(
                        identifier,
                        2,
                        StatisticsTableFlags))
                    return false;
                ImGui::TableSetupColumn(
                    firstColumn,
                    ImGuiTableColumnFlags_WidthStretch,
                    3.f);
                ImGui::TableSetupColumn(
                    "Current",
                    ImGuiTableColumnFlags_WidthStretch,
                    1.35f);
                ImGui::TableHeadersRow();
                return true;
            };
            const auto beginStatisticsRow =
                [](const char* label, bool available)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (available)
                    ImGui::TextUnformatted(label);
                else
                    ImGui::TextDisabled("%s", label);
                ImGui::TableSetColumnIndex(1);
            };
            const auto drawMilliseconds =
                [&beginStatisticsRow](
                    const char* label, double value, bool available)
            {
                beginStatisticsRow(label, available);
                if (available)
                    ImGui::Text("%.3f ms", value);
                else
                    ImGui::TextDisabled("--");
            };
            const auto drawCount =
                [&beginStatisticsRow](
                    const char* label, uint64_t value, bool available)
            {
                beginStatisticsRow(label, available);
                if (available)
                    ImGui::Text("%llu", static_cast<unsigned long long>(value));
                else
                    ImGui::TextDisabled("--");
            };
            const auto drawMemory =
                [&beginStatisticsRow](
                    const char* label, uint64_t bytes, bool available)
            {
                beginStatisticsRow(label, available);
                if (available)
                {
                    constexpr double BytesPerMebibyte = 1024.0 * 1024.0;
                    ImGui::Text(
                        "%.2f MiB",
                        double(bytes) / BytesPerMebibyte);
                }
                else
                    ImGui::TextDisabled("--");
            };
            const auto drawText =
                [&beginStatisticsRow](
                    const char* label, const char* value, bool available)
            {
                beginStatisticsRow(label, available);
                if (available)
                    ImGui::TextUnformatted(value);
                else
                    ImGui::TextDisabled("--");
            };
            const auto drawRendererTiming =
                [this, &timings, &drawMilliseconds](
                    const char* label, RendererTimingStage stage)
            {
                const bool currentDispatchAvailable =
                    stage != RendererTimingStage::RatioEstimatorShadows ||
                    m_app->DidDispatchHeitzRatioEstimatorThisFrame();
                const bool available =
                    currentDispatchAvailable &&
                    timings.IsAvailable(stage);
                drawMilliseconds(label, timings.Get(stage), available);
            };
            const auto drawSelectedRendererTable =
                [&beginStatisticsTable, &drawRendererTiming](
                    const char* label, RendererTimingStage stage)
            {
                if (!beginStatisticsTable(
                        "##SelectedRendererStatistics", "Graphics Stage"))
                    return;
                drawRendererTiming(label, stage);
                drawRendererTiming(
                    "Complete Renderer Frame",
                    RendererTimingStage::CompleteFrame);
                ImGui::EndTable();
            };

            switch (selectedEffect)
            {
            case StatisticsEffect::CompleteRenderer:
                if (beginStatisticsTable(
                        "##CompleteRendererStatistics", "Graphics Stage"))
                {
                    static constexpr std::pair<
                        const char*, RendererTimingStage> CompleteRows[] = {
                        { "Complete Renderer Frame",
                            RendererTimingStage::CompleteFrame },
                        { "Scene Setup and Clears",
                            RendererTimingStage::SceneSetup },
                        { "Geometry", RendererTimingStage::Geometry },
                        { "Closest Surface Resolve",
                            RendererTimingStage::MultisampleResolve },
                        { "Ratio-Estimator Ray Dispatch",
                            RendererTimingStage::RatioEstimatorShadows },
                        { "Direct Lighting",
                            RendererTimingStage::DirectLighting },
                        { "Screen-Space Visibility",
                            RendererTimingStage::ScreenSpaceVisibility },
                        { "Material Picking",
                            RendererTimingStage::MaterialPicking },
                        { "Environment Background",
                            RendererTimingStage::EnvironmentBackground },
                        { "Tone Mapping", RendererTimingStage::ToneMapping },
                        { "Fast Approximate",
                            RendererTimingStage::FastApproximate },
                        { "Output Blit", RendererTimingStage::OutputBlit }
                    };
                    static_assert(
                        std::size(CompleteRows) ==
                        static_cast<size_t>(RendererTimingStage::Count));
                    for (const auto& [label, stage] : CompleteRows)
                        drawRendererTiming(label, stage);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::SceneSetup:
                drawSelectedRendererTable(
                    "Scene Setup", RendererTimingStage::SceneSetup);
                break;
            case StatisticsEffect::Geometry:
                drawSelectedRendererTable(
                    "Geometry", RendererTimingStage::Geometry);
                break;
            case StatisticsEffect::DirectLighting:
                drawSelectedRendererTable(
                    "Direct Lighting",
                    RendererTimingStage::DirectLighting);
                break;
            case StatisticsEffect::Visibility:
                if (beginStatisticsTable(
                        "##VisibilityStatistics", "Visibility Metric"))
                {
                    const ScreenSpaceVisibilityTimings& visibility =
                        m_DisplayedVisibilityTimings;
                    const bool available = m_HasVisibilityStatSnapshot;
                    drawMilliseconds(
                        "Complete Effect",
                        visibility.CompleteEffectMs(),
                        available);
                    drawMilliseconds(
                        "First Trace", visibility.firstTraceMs, available);
                    drawMilliseconds(
                        "Reconstruction",
                        visibility.reconstructionMs,
                        available);
                    drawMilliseconds(
                        "Composition", visibility.compositionMs, available);
                    const float namedStageMilliseconds =
                        visibility.firstTraceMs +
                        visibility.reconstructionMs +
                        visibility.compositionMs;
                    drawMilliseconds(
                        "Named-Stage Total",
                        namedStageMilliseconds,
                        available);
                    drawMilliseconds(
                        "Unattributed Timer Difference",
                        visibility.CompleteEffectMs() - namedStageMilliseconds,
                        available);
                    drawMemory(
                        "Output Texture Memory",
                        visibility.outputTextureBytes,
                        available);
                    drawMemory(
                        "Working Texture Memory",
                        visibility.workingTextureBytes,
                        available);
                    drawMemory(
                        "Sampling Resource Memory",
                        visibility.schedulerResourceBytes,
                        available);
                    drawCount(
                        "Dispatches",
                        visibility.activeDispatchCount,
                        available);
                    drawCount(
                        "Read Resources",
                        visibility.activeSrvCount,
                        available);
                    drawCount(
                        "Write Resources",
                        visibility.activeUavCount,
                        available);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::Shadows:
                if (beginStatisticsTable(
                        "##ShadowStatistics", "Shadow Metric"))
                {
                    const ScreenSpaceDirectionalShadowTimings& shadows =
                        m_DisplayedScreenSpaceShadowTimings;
                    const bool available =
                        m_HasScreenSpaceShadowStatSnapshot;
                    drawText(
                        "Screen-Space Status",
                        available ? "Active" :
                            shadows.supported ? "Unavailable" : "Unsupported",
                        true);
                    drawMilliseconds(
                        "Screen-Space Trace",
                        shadows.traceMilliseconds,
                        available);
                    drawCount(
                        "Screen-Space Dispatches",
                        shadows.dispatchCount,
                        available);
                    drawCount(
                        "Screen-Space Work Groups",
                        shadows.totalGroups,
                        available);
                    drawCount(
                        "Screen-Space Samples",
                        shadows.sampleCount,
                        available);
                    drawMemory(
                        "Screen-Space Output Memory",
                        shadows.outputTextureBytes,
                        available);
                    drawRendererTiming(
                        "Ratio-Estimator Ray Dispatch",
                        RendererTimingStage::RatioEstimatorShadows);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::TemporalReconstructive:
                if (beginStatisticsTable(
                        "##TemporalStatistics", "Temporal Metric"))
                {
                    const TemporalAATimings& temporal =
                        m_DisplayedTemporalAATimings;
                    const bool available =
                        m_ui.AntiAliasing.temporal.enabled &&
                        m_HasTemporalAAStatSnapshot;
                    drawMilliseconds(
                        "Complete Effect",
                        temporal.CompleteEffectMilliseconds(),
                        available);
                    drawMilliseconds(
                        "History Blend",
                        temporal.blendMilliseconds,
                        available);
                    drawMilliseconds(
                        "Output",
                        temporal.outputMilliseconds,
                        available);
                    drawMilliseconds(
                        "Presentation Sharpen",
                        temporal.presentationSharpenMilliseconds,
                        available);
                    drawMemory(
                        "Active History Memory",
                        temporal.activeHistoryTextureBytes,
                        available);
                    drawMemory(
                        "Resident History Memory",
                        temporal.residentHistoryTextureBytes,
                        available);
                    drawMemory(
                        "Full-Quality History Memory",
                        temporal.robustHistoryTextureBytes,
                        available);
                    drawMemory(
                        "Minimum-Cost History Memory",
                        temporal.minimumHistoryTextureBytes,
                        available);
                    drawText(
                        "Effective Cost",
                        GetTemporalAaCostModeLabel(
                            temporal.effectiveCostMode),
                        available);
                    beginStatisticsRow(
                        "Minimum History Formats", available);
                    if (!available)
                        ImGui::TextDisabled("--");
                    else if (!temporal.minimumPathSupported)
                        ImGui::TextDisabled("Unsupported");
                    else
                    {
                        ImGui::Text(
                            "%s + %s",
                            temporal.minimumColorIsR11G11B10
                                ? "R11G11B10" : "RGBA16F",
                            temporal.minimumDepthIsR16
                                ? "R16F" : "R32F");
                    }
                    drawText(
                        "History Status",
                        temporal.historyValid ? "Valid" : "Invalid",
                        available);
                    drawCount(
                        "Accumulated Frames",
                        temporal.accumulationCount,
                        available);
                    drawCount(
                        "History Resets",
                        temporal.historyResetCount,
                        available);
                    drawCount(
                        "Dispatches", temporal.dispatchCount, available);
                    drawCount(
                        "History Color Samples",
                        temporal.historyColorSamples,
                        available);
                    drawCount(
                        "History Depth Gathers",
                        temporal.historyDepthGathers,
                        available);
                    drawCount(
                        "History Depth Samples",
                        temporal.historyDepthSamples,
                        available);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::FastApproximate:
                drawSelectedRendererTable(
                    "Fast Approximate",
                    RendererTimingStage::FastApproximate);
                break;
            case StatisticsEffect::ConservativeMorphological:
                if (beginStatisticsTable(
                        "##MorphologicalStatistics", "Morphological Stage"))
                {
                    const Cmaa2Timings& morphological =
                        m_DisplayedCmaa2Timings;
                    const bool available =
                        m_ui.AntiAliasing.cmaa2.enabled &&
                        m_HasCmaa2StatSnapshot;
                    drawMilliseconds(
                        "Complete Effect",
                        morphological.CompleteEffectMilliseconds(),
                        available);
                    drawMilliseconds(
                        "Edge Detection",
                        morphological.edgeMilliseconds,
                        available);
                    drawMilliseconds(
                        "Candidate Processing",
                        morphological.candidateMilliseconds,
                        available);
                    drawMilliseconds(
                        "Apply",
                        morphological.applyMilliseconds,
                        available);
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::Multisample:
                if (beginStatisticsTable(
                        "##MultisampleStatistics", "Multisample Metric"))
                {
                    const bool enabled = m_ui.AntiAliasing.msaa.enabled;
                    const uint32_t requestedSamples =
                        m_ui.AntiAliasing.msaa.sampleCount;
                    const uint32_t activeSamples =
                        m_app->GetActiveRasterSampleCount();
                    const bool active = enabled && activeSamples > 1u;
                    drawText(
                        "Status",
                        active ? "Active" :
                            enabled ? "Format Unsupported" : "Disabled",
                        true);
                    drawCount(
                        "Requested Samples", requestedSamples, enabled);
                    drawCount("Active Samples", activeSamples, enabled);
                    if (active)
                    {
                        drawRendererTiming(
                            "Geometry", RendererTimingStage::Geometry);
                        drawRendererTiming(
                            "Direct Lighting",
                            RendererTimingStage::DirectLighting);
                        drawRendererTiming(
                            "Closest Surface Resolve",
                            RendererTimingStage::MultisampleResolve);
                    }
                    else
                    {
                        drawMilliseconds("Geometry", 0.0, false);
                        drawMilliseconds("Direct Lighting", 0.0, false);
                        drawMilliseconds(
                            "Closest Surface Resolve", 0.0, false);
                    }
                    drawRendererTiming(
                        "Complete Renderer Frame",
                        RendererTimingStage::CompleteFrame);
                    ImGui::EndTable();
                }
                break;
            case StatisticsEffect::MaterialPicking:
                drawSelectedRendererTable(
                    "Material Picking",
                    RendererTimingStage::MaterialPicking);
                break;
            case StatisticsEffect::EnvironmentBackground:
                drawSelectedRendererTable(
                    "Environment Background",
                    RendererTimingStage::EnvironmentBackground);
                break;
            case StatisticsEffect::ToneMapping:
                drawSelectedRendererTable(
                    "Tone Mapping", RendererTimingStage::ToneMapping);
                break;
            case StatisticsEffect::OutputBlit:
                drawSelectedRendererTable(
                    "Output Blit", RendererTimingStage::OutputBlit);
                break;
            default:
                break;
            }

            EndDrawerBody();
        }
        ImGui::Spacing();
        const bool antiAliasingOpen = DrawCollapsingHeader(
            "Aliasing",
            "Enable temporal, fast approximate, morphological, and "
            "multisample techniques independently.");
        if (antiAliasingOpen)
        {
            BeginDrawerBody("##AliasingBody", settingsControlWidth);
            ImGui::PushID("AliasingControls");
            AntiAliasingSettings& aliasing = m_ui.AntiAliasing;
            const AntiAliasingSettings aliasingDefaults{};

            const auto drawPresetEnum =
                [settingsControlWidth](const char* label,
                    auto value,
                    const char* const* labels,
                    int count,
                    bool custom,
                    auto applySelection)
                {
                    using Value = std::decay_t<decltype(value)>;
                    const int selected = std::clamp(
                        static_cast<int>(value), 0, count - 1);
                    std::string preview = labels[selected];
                    if (custom)
                        preview += " (Custom)";
                    SetNextLabeledControlWidth(
                        label, settingsControlWidth);
                    if (!BeginRoundedCombo(label, preview.c_str()))
                        return;
                    for (int index = 0; index < count; ++index)
                    {
                        const bool isSelected =
                            !custom && selected == index;
                        DrawDeferredDropdownOption(
                            labels[index],
                            labels[index],
                            isSelected,
                            [applySelection, index]() mutable
                            {
                                applySelection(
                                    static_cast<Value>(index));
                            });
                        if (selected == index)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                };

            static constexpr const char* QualityLabels[] = {
                "Low", "Medium", "High", "Ultra"
            };

            if (BeginAnimatedTreeNode(
                    "Temporal Reconstructive##Aliasing",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Reconstruct stable detail from current and previous frames."))
            {
            ImGui::Checkbox(
                "Enable##TemporalReconstructive",
                &aliasing.temporal.enabled);
            ImGui::SetItemTooltip(
                "Combine current and previous frames to reduce visible aliasing.");
            if (DrawPresetResetIcon(
                    "TemporalEnabled",
                    aliasing.temporal.enabled !=
                        aliasingDefaults.temporal.enabled))
            {
                aliasing.temporal.enabled =
                    aliasingDefaults.temporal.enabled;
            }
            if (BeginAnimatedToggleRegion(
                    "##TemporalReconstructiveControls",
                    aliasing.temporal.enabled))
            {
            const bool temporalQualityCustom =
                aliasing.temporal.stationaryBypass !=
                    aliasingDefaults.temporal.stationaryBypass ||
                !(aliasing.temporal.algorithmOverrides ==
                    aliasingDefaults.temporal.algorithmOverrides);
            const auto applyTemporalQualityPreset =
                [settings = &aliasing,
                    stationaryBypass =
                        aliasingDefaults.temporal.stationaryBypass,
                    algorithmOverrides =
                        aliasingDefaults.temporal.algorithmOverrides](
                    AntiAliasingQuality quality)
                {
                    settings->temporal.quality = quality;
                    settings->temporal.stationaryBypass =
                        stationaryBypass;
                    settings->temporal.algorithmOverrides =
                        algorithmOverrides;
                };
            drawPresetEnum(
                "Quality##TemporalReconstructive",
                aliasing.temporal.quality,
                QualityLabels,
                static_cast<int>(std::size(QualityLabels)),
                temporalQualityCustom,
                applyTemporalQualityPreset);
            ImGui::SetItemTooltip(
                "Choose the default reconstruction quality recipe. "
                "Recipe-owned Algorithm changes append (Custom). The circular "
                "arrow restores factory Quality and its owned Algorithm "
                "controls.");
            if (DrawPresetResetIcon(
                    "TemporalQuality",
                    aliasing.temporal.quality !=
                        aliasingDefaults.temporal.quality ||
                    temporalQualityCustom))
            {
                applyTemporalQualityPreset(
                    aliasingDefaults.temporal.quality);
            }

            static constexpr const char* CostLabels[] = {
                "Full Quality", "Reduced", "Minimum"
            };
            const bool temporalCostCustom =
                !(aliasing.temporal.behaviorOverrides ==
                    aliasingDefaults.temporal.behaviorOverrides) ||
                m_ui.TemporalAaSharpenEnabled ||
                m_ui.TemporalAaSharpness != TemporalAaDefaultSharpness;
            const auto applyTemporalCostPreset =
                [settings = &aliasing,
                    behaviorOverrides =
                        aliasingDefaults.temporal.behaviorOverrides,
                    ui = &m_ui](TemporalAaCostMode costMode)
                {
                    settings->temporal.costMode = costMode;
                    settings->temporal.behaviorOverrides =
                        behaviorOverrides;
                    ui->TemporalAaSharpenEnabled = false;
                    ui->TemporalAaSharpness =
                        TemporalAaDefaultSharpness;
                };
            drawPresetEnum(
                "Cost",
                aliasing.temporal.costMode,
                CostLabels,
                static_cast<int>(std::size(CostLabels)),
                temporalCostCustom,
                applyTemporalCostPreset);
            ImGui::SetItemTooltip(
                "Choose the retained history quality and processing cost. Cost "
                "changes append (Custom). The circular arrow restores the "
                "factory Cost and every Cost control.");
            if (DrawPresetResetIcon(
                    "TemporalCost",
                    aliasing.temporal.costMode !=
                        aliasingDefaults.temporal.costMode ||
                    temporalCostCustom))
            {
                applyTemporalCostPreset(
                    aliasingDefaults.temporal.costMode);
            }

            ImGui::SetNextItemOpen(false, ImGuiCond_Once);
            if (BeginAnimatedTreeNode(
                    "Advanced##TemporalReconstructive",
                    ImGuiTreeNodeFlags_None,
                    "Override the temporal recipe. This section is closed by default."))
            {
                AntiAliasingSettings inheritedAliasing = aliasing;
                inheritedAliasing.temporal.algorithmOverrides = {};
                inheritedAliasing.temporal.behaviorOverrides = {};
                const ResolvedAntiAliasingSettings resolvedAliasing =
                    ResolveAntiAliasingSettings(inheritedAliasing);

                const auto drawAdvancedEnum =
                    [settingsControlWidth](
                        const char* label,
                        const char* resetId,
                        auto& value,
                        auto defaultValue,
                        int inheritedIndex,
                        const char* const* labels,
                        int count,
                        const char* tooltip,
                        const char* inheritedOption = nullptr)
                {
                    using Value = std::decay_t<decltype(value)>;
                    Value* setting = &value;
                    const int selectedValue = static_cast<int>(value);
                    const int selectedIndex = std::clamp(
                        selectedValue == 0
                            ? inheritedIndex
                            : selectedValue - 1,
                        0,
                        count - 1);
                    const char* preview =
                        selectedValue == 0 && inheritedOption
                            ? inheritedOption
                            : labels[selectedIndex];
                    SetNextLabeledControlWidth(label, settingsControlWidth);
                    if (BeginRoundedCombo(label, preview))
                    {
                        if (inheritedOption)
                        {
                            const bool selected = selectedValue == 0;
                            DrawDeferredDropdownOption(
                                inheritedOption,
                                inheritedOption,
                                selected,
                                [setting]()
                                {
                                    *setting = static_cast<Value>(0);
                                });
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        for (int index = 0; index < count; ++index)
                        {
                            const int optionValue = index + 1;
                            const bool useInherited =
                                !inheritedOption &&
                                index == inheritedIndex;
                            const bool selected =
                                (!useInherited &&
                                    selectedValue == optionValue) ||
                                (!inheritedOption &&
                                    selectedValue == 0 &&
                                    index == selectedIndex);
                            DrawDeferredDropdownOption(
                                labels[index],
                                labels[index],
                                selected,
                                [setting, optionValue, useInherited]()
                                {
                                    *setting =
                                        static_cast<Value>(
                                            useInherited
                                            ? 0
                                            : optionValue);
                                });
                            if (selected ||
                                (!inheritedOption &&
                                    index == selectedIndex))
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip("%s", tooltip);
                    if (DrawNestedDropdownResetIcon(
                            resetId, value != defaultValue))
                    {
                        value = defaultValue;
                    }
                };

                ImGui::SeparatorText("Algorithm");
                static constexpr const char* JitterSequenceLabels[] = {
                    "Rotated Grid 4",
                    "Uniform Helix 4",
                    "Halton 8",
                    "Halton 16",
                    "Halton 32",
                    "Sobol 32"
                };
                const int jitterSequence = static_cast<int>(
                    SanitizeTemporalAaJitterSequence(
                        aliasing.temporal.jitterSequence));
                SetNextLabeledControlWidth(
                    "Jitter Sequence##TemporalReconstructive",
                    settingsControlWidth);
                if (BeginRoundedCombo(
                        "Jitter Sequence##TemporalReconstructive",
                        JitterSequenceLabels[jitterSequence]))
                {
                    for (int index = 0;
                        index < static_cast<int>(
                            std::size(JitterSequenceLabels));
                        ++index)
                    {
                        const bool selected = index == jitterSequence;
                        DrawDeferredDropdownOption(
                            JitterSequenceLabels[index],
                            JitterSequenceLabels[index],
                            selected,
                            [settings = &aliasing, index]()
                            {
                                settings->temporal.jitterSequence =
                                    static_cast<TemporalAaJitterSequence>(
                                        index);
                            });
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose the subpixel camera pattern. Sobol 32 is a fixed "
                    "seed-43 stochastic Sobol sequence. The seed produces the "
                    "first point; each later point is chosen from 100 "
                    "same-stratum candidates to maximize minimum toroidal "
                    "separation. Its measured spacing advantage is only over "
                    "matching Filament Halton prefixes. Changing this resets "
                    "temporal history.");
                if (DrawNestedDropdownResetIcon(
                        "TemporalJitterSequence",
                        aliasing.temporal.jitterSequence !=
                            aliasingDefaults.temporal.jitterSequence))
                {
                    aliasing.temporal.jitterSequence =
                        aliasingDefaults.temporal.jitterSequence;
                }

                static constexpr const char* DepthValidationLabels[] = {
                    "Stationary Bypass", "Four-Texel Footprint"
                };
                int depthValidation =
                    aliasing.temporal.stationaryBypass ? 0 : 1;
                SetNextLabeledControlWidth(
                    "Depth Validation",
                    settingsControlWidth);
                if (ImGui::Combo(
                        "Depth Validation",
                        &depthValidation,
                        DepthValidationLabels,
                        static_cast<int>(
                            std::size(DepthValidationLabels))))
                {
                    aliasing.temporal.stationaryBypass =
                        depthValidation == 0;
                }
                ImGui::SetItemTooltip(
                    "Use a moving-point check for stationary samples or "
                    "validate the complete four-texel footprint.");
                if (DrawNestedDropdownResetIcon(
                        "TemporalDepthValidation",
                        aliasing.temporal.stationaryBypass !=
                            aliasingDefaults.temporal.stationaryBypass))
                {
                    aliasing.temporal.stationaryBypass =
                        aliasingDefaults.temporal.stationaryBypass;
                }

                static constexpr const char* MotionSourceLabels[] = {
                    "Center", "Closest Cross", "Edge Dilation"
                };
                drawAdvancedEnum(
                    "Motion Source",
                    "TemporalMotionSource",
                    aliasing.temporal.algorithmOverrides.motionSource,
                    aliasingDefaults.temporal.algorithmOverrides.motionSource,
                    static_cast<int>(
                        resolvedAliasing.temporal.motionSource),
                    MotionSourceLabels,
                    static_cast<int>(std::size(MotionSourceLabels)),
                    "Choose where motion is sampled for history reprojection.");

                static constexpr const char*
                    CurrentSampleLabels[] = {
                        "Direct", "De-Jittered"
                    };
                drawAdvancedEnum(
                    "Current Sample",
                    "TemporalCurrentSample",
                    aliasing.temporal.algorithmOverrides.
                        currentReconstruction,
                    aliasingDefaults.temporal.algorithmOverrides.
                        currentReconstruction,
                    static_cast<int>(
                        resolvedAliasing.temporal.currentReconstruction),
                    CurrentSampleLabels,
                    static_cast<int>(
                        std::size(CurrentSampleLabels)),
                    "Choose how the current frame is sampled before blending.");

                static constexpr const char*
                    HistoryFilterLabels[] = {
                        "1x Bilinear",
                        "1x Bicubic",
                        "5x Bicubic",
                        "9x Bicubic"
                    };
                drawAdvancedEnum(
                    "History Filter",
                    "TemporalHistoryFilter",
                    aliasing.temporal.algorithmOverrides.historyFilter,
                    aliasingDefaults.temporal.algorithmOverrides.historyFilter,
                    static_cast<int>(
                        resolvedAliasing.temporal.historyFilter),
                    HistoryFilterLabels,
                    static_cast<int>(
                        std::size(HistoryFilterLabels)),
                    "Choose the reconstruction filter used for stored history.");

                static constexpr const char*
                    RectificationLabels[] = {
                        "Pair Tristimulus", "Variance Chroma"
                    };
                drawAdvancedEnum(
                    "Rectification",
                    "TemporalRectification",
                    aliasing.temporal.algorithmOverrides.rectification,
                    aliasingDefaults.temporal.algorithmOverrides.rectification,
                    static_cast<int>(
                        resolvedAliasing.temporal.rectification),
                    RectificationLabels,
                    static_cast<int>(
                        std::size(RectificationLabels)),
                    "Choose how reprojected history is constrained to current detail.");

                const int presetHistoryFrames = static_cast<int>(
                    GetPresetHistoryFrames(aliasing.temporal.quality));
                int historyFrames =
                    aliasing.temporal.algorithmOverrides.historyFrames < 0
                    ? presetHistoryFrames
                    : aliasing.temporal.algorithmOverrides.historyFrames;
                if (ImGui::SliderInt(
                        "History Frames", &historyFrames, 1, 32))
                {
                    aliasing.temporal.algorithmOverrides.historyFrames =
                        historyFrames == presetHistoryFrames
                        ? -1
                        : historyFrames;
                }
                ImGui::SetItemTooltip(
                    "Set the visible history horizon. The quality recipe is "
                    "used automatically when this matches its frame count.");
                if (DrawPresetResetIcon(
                        "TemporalHistoryFrames",
                        aliasing.temporal.algorithmOverrides.historyFrames >= 0))
                {
                    aliasing.temporal.algorithmOverrides.historyFrames = -1;
                }

                const float resolvedHistoryStrength =
                    aliasing.temporal.algorithmOverrides.historyStrength < 0.f
                    ? 1.f
                    : aliasing.temporal.algorithmOverrides.historyStrength;
                float historyStrengthPercent =
                    resolvedHistoryStrength * 100.f;
                if (ImGui::SliderFloat(
                    "History Strength",
                    &historyStrengthPercent,
                    0.f,
                    200.f,
                    "%.0f%%"))
                {
                    const float selectedStrength =
                        historyStrengthPercent * 0.01f;
                    aliasing.temporal.algorithmOverrides.historyStrength =
                        std::abs(selectedStrength - 1.f) < 1e-4f
                        ? -1.f
                        : selectedStrength;
                }
                ImGui::SetItemTooltip(
                    "Scale the contribution of reprojected history. One "
                    "hundred percent follows the quality recipe.");
                if (DrawPresetResetIcon(
                        "TemporalHistoryStrength",
                        aliasing.temporal.algorithmOverrides.historyStrength >=
                            0.f))
                {
                    aliasing.temporal.algorithmOverrides.historyStrength =
                        -1.f;
                }

                ImGui::SeparatorText("Cost");
                static constexpr const char*
                    StorageLabels[] = { "Robust", "Compact" };
                drawAdvancedEnum(
                    "History Storage",
                    "TemporalHistoryStorage",
                    aliasing.temporal.behaviorOverrides.historyStorage,
                    aliasingDefaults.temporal.behaviorOverrides.historyStorage,
                    static_cast<int>(resolvedAliasing.historyStorage),
                    StorageLabels,
                    static_cast<int>(std::size(StorageLabels)),
                    "Choose robust or compact storage for the retained history.");

                static constexpr const char*
                    WeightLabels[] = {
                        "Confidence Recurrence", "Immediate Horizon"
                    };
                drawAdvancedEnum(
                    "History Weight",
                    "TemporalHistoryWeight",
                    aliasing.temporal.behaviorOverrides.historyWeight,
                    aliasingDefaults.temporal.behaviorOverrides.historyWeight,
                    static_cast<int>(resolvedAliasing.historyWeight),
                    WeightLabels,
                    static_cast<int>(std::size(WeightLabels)),
                    "Choose how confidence changes the retained history weight.");

                static constexpr const char*
                    TrustLabels[] = {
                        "Linear Speed", "Squared Speed"
                    };
                drawAdvancedEnum(
                    "Motion Trust",
                    "TemporalMotionTrust",
                    aliasing.temporal.behaviorOverrides.motionTrust,
                    aliasingDefaults.temporal.behaviorOverrides.motionTrust,
                    static_cast<int>(resolvedAliasing.motionTrust),
                    TrustLabels,
                    static_cast<int>(std::size(TrustLabels)),
                    "Choose how motion speed reduces trust in stored history.");

                static constexpr const char*
                    ClipLabels[] = {
                        "Velocity-Dilated", "Tight Component"
                    };
                drawAdvancedEnum(
                    "Rectification Clip",
                    "TemporalRectificationClip",
                    aliasing.temporal.behaviorOverrides.rectificationClip,
                    aliasingDefaults.temporal.behaviorOverrides.
                        rectificationClip,
                    static_cast<int>(resolvedAliasing.rectificationClip),
                    ClipLabels,
                    static_cast<int>(std::size(ClipLabels)),
                    "Choose the boundary used to clip reprojected history.");

                static constexpr const char*
                    BlendLabels[] = {
                        "Luminance-Compressed", "Linear Color"
                    };
                drawAdvancedEnum(
                    "Blend Domain",
                    "TemporalBlendDomain",
                    aliasing.temporal.behaviorOverrides.blendDomain,
                    aliasingDefaults.temporal.behaviorOverrides.blendDomain,
                    static_cast<int>(resolvedAliasing.blendDomain),
                    BlendLabels,
                    static_cast<int>(std::size(BlendLabels)),
                    "Choose the color domain used to blend current and stored samples.");

                static constexpr const char* SharpenModeLabels[] = {
                    "Off", "On"
                };
                const int automaticSharpeningIndex =
                    resolvedAliasing.sharpeningAllowed ? 1 : 0;
                const std::string automaticSharpening =
                    std::string(SharpenModeLabels[automaticSharpeningIndex]) +
                    " (Automatic)";
                drawAdvancedEnum(
                    "Preset Sharpening",
                    "TemporalPresetSharpening",
                    aliasing.temporal.behaviorOverrides.sharpening,
                    aliasingDefaults.temporal.behaviorOverrides.sharpening,
                    automaticSharpeningIndex,
                    SharpenModeLabels,
                    static_cast<int>(
                        std::size(SharpenModeLabels)),
                    "Choose whether the temporal recipe may sharpen its output.",
                    automaticSharpening.c_str());

                ImGui::Checkbox(
                    "Output Sharpening",
                    &m_ui.TemporalAaSharpenEnabled);
                ImGui::SetItemTooltip(
                    "Apply a final sharpening pass after temporal reconstruction.");
                if (DrawPresetResetIcon(
                        "TemporalOutputSharpening",
                        m_ui.TemporalAaSharpenEnabled))
                {
                    m_ui.TemporalAaSharpenEnabled = false;
                }
                if (BeginAnimatedToggleRegion(
                        "##TemporalSharpenControls",
                        m_ui.TemporalAaSharpenEnabled))
                {
                    ImGui::SliderFloat(
                        "Sharpness",
                        &m_ui.TemporalAaSharpness,
                        TemporalAaMinimumSharpness,
                        TemporalAaMaximumSharpness,
                        "%.2f");
                    ImGui::SetItemTooltip(
                        "Set the strength of final output sharpening.");
                    if (DrawPresetResetIcon(
                            "TemporalSharpness",
                            m_ui.TemporalAaSharpness !=
                                TemporalAaDefaultSharpness))
                    {
                        m_ui.TemporalAaSharpness =
                            TemporalAaDefaultSharpness;
                    }
                    EndAnimatedToggleRegion();
                }
                EndAnimatedTreeNode();
            }

            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Fast Approximate##Aliasing",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Smooth current-frame edges with Filament-based FXAA."))
            {
            ImGui::Checkbox(
                "Enable##FastApproximate",
                &aliasing.fastApproximate.enabled);
            ImGui::SetItemTooltip(
                "Apply a fast post-tone-map edge filter before morphological AA.");
            if (DrawPresetResetIcon(
                    "FastApproximateEnabled",
                    aliasing.fastApproximate.enabled !=
                        aliasingDefaults.fastApproximate.enabled))
            {
                aliasing.fastApproximate.enabled =
                    aliasingDefaults.fastApproximate.enabled;
            }
            if (BeginAnimatedToggleRegion(
                    "##FastApproximateControls",
                    aliasing.fastApproximate.enabled))
            {
            const bool fastApproximateQualityCustom =
                !MatchesFastApproximateAaQualityPreset(
                    aliasing.fastApproximate);
            const auto applyFastApproximateQualityPreset =
                [settings = &aliasing.fastApproximate](
                    AntiAliasingQuality quality)
                {
                    ApplyFastApproximateAaQualityPreset(
                        *settings, quality);
                };
            drawPresetEnum(
                "Quality##FastApproximate",
                aliasing.fastApproximate.quality,
                QualityLabels,
                static_cast<int>(std::size(QualityLabels)),
                fastApproximateQualityCustom,
                applyFastApproximateQualityPreset);
            ImGui::SetItemTooltip(
                "Choose the FXAA edge-filter recipe. Advanced changes append "
                "(Custom). The circular arrow restores the factory Quality "
                "and every FXAA control.");
            if (DrawPresetResetIcon(
                    "FastApproximateQuality",
                    aliasing.fastApproximate.quality !=
                        aliasingDefaults.fastApproximate.quality ||
                    fastApproximateQualityCustom))
            {
                applyFastApproximateQualityPreset(
                    aliasingDefaults.fastApproximate.quality);
            }

            const FastApproximateAaQualityPreset
                fastApproximatePreset =
                    GetFastApproximateAaQualityPreset(
                        aliasing.fastApproximate.quality);
            ImGui::SetNextItemOpen(false, ImGuiCond_Once);
            if (BeginAnimatedTreeNode(
                    "Advanced##FastApproximate",
                    ImGuiTreeNodeFlags_None,
                    "Tune edge detection and filtering. This section is closed by default."))
            {
                SetNextLabeledControlWidth(
                    "Edge Sharpness##FastApproximate",
                    settingsControlWidth);
                ImGui::SliderFloat(
                    "Edge Sharpness##FastApproximate",
                    &aliasing.fastApproximate.edgeSharpness,
                    FastApproximateAaMinimumEdgeSharpness,
                    FastApproximateAaMaximumEdgeSharpness,
                    "%.2f");
                ImGui::SetItemTooltip(
                    "Increase to keep the edge filter narrower and sharper.");
                if (DrawNestedDropdownResetIcon(
                        "FastApproximateEdgeSharpness",
                        aliasing.fastApproximate.edgeSharpness !=
                            fastApproximatePreset.edgeSharpness))
                {
                    aliasing.fastApproximate.edgeSharpness =
                        fastApproximatePreset.edgeSharpness;
                }

                SetNextLabeledControlWidth(
                    "Relative Edge Threshold##FastApproximate",
                    settingsControlWidth);
                ImGui::SliderFloat(
                    "Relative Edge Threshold##FastApproximate",
                    &aliasing.fastApproximate.edgeThreshold,
                    FastApproximateAaMinimumEdgeThreshold,
                    FastApproximateAaMaximumEdgeThreshold,
                    "%.3f");
                ImGui::SetItemTooltip(
                    "Increase to skip more edges relative to local brightness.");
                if (DrawNestedDropdownResetIcon(
                        "FastApproximateEdgeThreshold",
                        aliasing.fastApproximate.edgeThreshold !=
                            fastApproximatePreset.edgeThreshold))
                {
                    aliasing.fastApproximate.edgeThreshold =
                        fastApproximatePreset.edgeThreshold;
                }

                SetNextLabeledControlWidth(
                    "Minimum Edge Threshold##FastApproximate",
                    settingsControlWidth);
                ImGui::SliderFloat(
                    "Minimum Edge Threshold##FastApproximate",
                    &aliasing.fastApproximate.darkEdgeThreshold,
                    FastApproximateAaMinimumDarkEdgeThreshold,
                    FastApproximateAaMaximumDarkEdgeThreshold,
                    "%.3f");
                ImGui::SetItemTooltip(
                    "Increase to skip more low-contrast edges in dark regions.");
                if (DrawNestedDropdownResetIcon(
                        "FastApproximateMinimumEdgeThreshold",
                        aliasing.fastApproximate.darkEdgeThreshold !=
                            fastApproximatePreset.darkEdgeThreshold))
                {
                    aliasing.fastApproximate.darkEdgeThreshold =
                        fastApproximatePreset.darkEdgeThreshold;
                }
                EndAnimatedTreeNode();
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Conservative Morphological##Aliasing",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Detect and smooth visible edge patterns in the current frame."))
            {
            ImGui::Checkbox(
                "Enable##ConservativeMorphological",
                &aliasing.cmaa2.enabled);
            ImGui::SetItemTooltip(
                "Apply conservative morphological edge smoothing after tone mapping.");
            if (DrawPresetResetIcon(
                    "MorphologicalEnabled",
                    aliasing.cmaa2.enabled !=
                        aliasingDefaults.cmaa2.enabled))
            {
                aliasing.cmaa2.enabled = aliasingDefaults.cmaa2.enabled;
            }
            if (BeginAnimatedToggleRegion(
                    "##ConservativeMorphologicalControls",
                    aliasing.cmaa2.enabled))
            {
            const bool cmaa2QualityCustom =
                !MatchesCmaa2QualityPreset(aliasing.cmaa2);
            const auto applyCmaa2QualityPreset =
                [settings = &aliasing.cmaa2](AntiAliasingQuality quality)
                {
                    ApplyCmaa2QualityPreset(*settings, quality);
                };
            drawPresetEnum(
                "Quality##ConservativeMorphological",
                aliasing.cmaa2.quality,
                QualityLabels,
                static_cast<int>(std::size(QualityLabels)),
                cmaa2QualityCustom,
                applyCmaa2QualityPreset);
            ImGui::SetItemTooltip(
                "Choose the CMAA2 edge-detection recipe. Advanced changes "
                "append (Custom). The circular arrow restores the factory "
                "Quality and both CMAA2 controls.");
            if (DrawPresetResetIcon(
                    "MorphologicalQuality",
                    aliasing.cmaa2.quality !=
                        aliasingDefaults.cmaa2.quality ||
                    cmaa2QualityCustom))
            {
                applyCmaa2QualityPreset(
                    aliasingDefaults.cmaa2.quality);
            }

            const Cmaa2QualityPreset cmaa2Preset =
                GetCmaa2QualityPreset(aliasing.cmaa2.quality);
            ImGui::SetNextItemOpen(false, ImGuiCond_Once);
            if (BeginAnimatedTreeNode(
                    "Advanced##ConservativeMorphological",
                    ImGuiTreeNodeFlags_None,
                    "Tune edge detection. This section is closed by default."))
            {
                SetNextLabeledControlWidth(
                    "Edge Threshold##ConservativeMorphological",
                    settingsControlWidth);
                ImGui::SliderFloat(
                    "Edge Threshold##ConservativeMorphological",
                    &aliasing.cmaa2.edgeThreshold,
                    Cmaa2MinimumEdgeThreshold,
                    Cmaa2MaximumEdgeThreshold,
                    "%.3f");
                ImGui::SetItemTooltip(
                    "Lower values detect and smooth lower-contrast edges.");
                if (DrawNestedDropdownResetIcon(
                        "MorphologicalEdgeThreshold",
                        aliasing.cmaa2.edgeThreshold !=
                            cmaa2Preset.edgeThreshold))
                {
                    aliasing.cmaa2.edgeThreshold =
                        cmaa2Preset.edgeThreshold;
                }

                static constexpr const char* DetectorLabels[] = {
                    "Luma", "Full Color"
                };
                int detector = std::clamp(
                    static_cast<int>(aliasing.cmaa2.detector),
                    0,
                    static_cast<int>(std::size(DetectorLabels)) - 1);
                SetNextLabeledControlWidth(
                    "Detector##ConservativeMorphological",
                    settingsControlWidth);
                if (ImGui::Combo(
                        "Detector##ConservativeMorphological",
                        &detector,
                        DetectorLabels,
                        static_cast<int>(std::size(DetectorLabels))))
                {
                    aliasing.cmaa2.detector =
                        static_cast<Cmaa2EdgeDetector>(detector);
                }
                ImGui::SetItemTooltip(
                    "Use the faster luma detector or full-color detection "
                    "that also catches isoluminant chromatic edges.");
                if (DrawNestedDropdownResetIcon(
                        "MorphologicalDetector",
                        aliasing.cmaa2.detector !=
                            cmaa2Preset.detector))
                {
                    aliasing.cmaa2.detector = cmaa2Preset.detector;
                }
            EndAnimatedTreeNode();
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Multisample Adaptive##Aliasing",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Render multiple coverage samples for each pixel."))
            {
            ImGui::Checkbox(
                "Enable##MultisampleAdaptive",
                &aliasing.msaa.enabled);
            ImGui::SetItemTooltip(
                "Render multiple geometry coverage samples per pixel.");
            if (DrawPresetResetIcon(
                    "MultisampleEnabled",
                    aliasing.msaa.enabled != aliasingDefaults.msaa.enabled))
            {
                aliasing.msaa.enabled = aliasingDefaults.msaa.enabled;
            }
            if (BeginAnimatedToggleRegion(
                    "##MultisampleAdaptiveControls",
                    aliasing.msaa.enabled))
            {
            const bool multisampleQualityCustom =
                !MatchesMultisampleQualityPreset(aliasing.msaa);
            const auto applyMultisampleQualityPreset =
                [settings = &aliasing.msaa](AntiAliasingQuality quality)
                {
                    ApplyMultisampleQualityPreset(*settings, quality);
                };
            drawPresetEnum(
                "Quality##MultisampleAdaptive",
                aliasing.msaa.quality,
                QualityLabels,
                static_cast<int>(std::size(QualityLabels)),
                multisampleQualityCustom,
                applyMultisampleQualityPreset);
            ImGui::SetItemTooltip(
                "Choose the raster sample-count recipe: 2x, 4x, 8x, or "
                "16x. Advanced changes append (Custom).");
            if (DrawPresetResetIcon(
                    "MultisampleQuality",
                    aliasing.msaa.quality !=
                        aliasingDefaults.msaa.quality ||
                    multisampleQualityCustom))
            {
                applyMultisampleQualityPreset(
                    aliasingDefaults.msaa.quality);
            }

            const uint32_t multisamplePresetSamples =
                GetMultisampleQualitySampleCount(aliasing.msaa.quality);
            ImGui::SetNextItemOpen(false, ImGuiCond_Once);
            if (BeginAnimatedTreeNode(
                    "Advanced##MultisampleAdaptive",
                    ImGuiTreeNodeFlags_None,
                    "Choose the raster sample count. This section is closed by default."))
            {
            static constexpr uint32_t SampleCounts[] = {
                2u, 4u, 8u, 16u
            };
            static constexpr const char* SampleLabels[] = {
                "2x", "4x", "8x", "16x"
            };
            int sampleIndex = 1;
            for (int index = 0;
                index < static_cast<int>(std::size(SampleCounts));
                ++index)
            {
                if (aliasing.msaa.sampleCount == SampleCounts[index])
                    sampleIndex = index;
            }
            SetNextLabeledControlWidth(
                "Samples##MultisampleAdaptive",
                settingsControlWidth);
            if (ImGui::Combo(
                    "Samples##MultisampleAdaptive",
                    &sampleIndex,
                    SampleLabels,
                    static_cast<int>(std::size(SampleLabels))))
            {
                aliasing.msaa.sampleCount = SampleCounts[sampleIndex];
            }
            ImGui::SetItemTooltip(
                "Choose the number of geometry coverage samples per pixel.");
            if (DrawNestedDropdownResetIcon(
                    "MultisampleSamples",
                    aliasing.msaa.sampleCount !=
                        multisamplePresetSamples))
            {
                aliasing.msaa.sampleCount =
                    multisamplePresetSamples;
            }
            EndAnimatedTreeNode();
            }
            EndAnimatedToggleRegion();
            }
            EndAnimatedTreeNode();
            }

            ImGui::PopID();
            EndDrawerBody();
        }
        ImGui::Spacing();
        const bool debugOpen = DrawCollapsingHeader(
            "Debug",
            "Combine world appearance and effect-specific information views.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (debugOpen)
        {
            BeginDrawerBody("##DebugBody", settingsControlWidth);

            if (BeginAnimatedTreeNode(
                    "World##Debug",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Change material presentation without changing lighting effects."))
            {
            static constexpr const char* WorldLabels[] = {
                "Default",
                "White",
                "White Detail",
                "White Lighting"
            };
            int worldMode = static_cast<int>(m_ui.WhiteWorld);
            SetNextLabeledControlWidth(
                "Materials", settingsControlWidth);
            if (ImGui::Combo(
                    "Materials",
                    &worldMode,
                    WorldLabels,
                    static_cast<int>(std::size(WorldLabels))))
            {
                m_app->SetWhiteWorldMode(
                    static_cast<WhiteWorldMode>(worldMode));
            }
            ImGui::SetItemTooltip(
                "Choose default scene materials or a white-world presentation. "
                "This can be combined with every effect-specific debug view.");
            if (DrawNestedDropdownResetIcon(
                    "DebugWorld",
                    m_ui.WhiteWorld != WhiteWorldMode::Off))
            {
                m_app->SetWhiteWorldMode(WhiteWorldMode::Off);
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Visibility##Debug",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Inspect composition-stage visibility information."))
            {
            static constexpr const char* VisibilityDebugLabels[] = {
                "Default",
                "Ambient Visibility",
                "Traced Indirect",
                "Applied Indirect"
            };
            int visibilityDebugView = static_cast<int>(
                m_ui.ScreenSpaceVisibility.debugView);
            SetNextLabeledControlWidth(
                "View##VisibilityDebug", settingsControlWidth);
            if (ImGui::Combo(
                    "View##VisibilityDebug",
                    &visibilityDebugView,
                    VisibilityDebugLabels,
                    static_cast<int>(
                        std::size(VisibilityDebugLabels))))
            {
                m_ui.ScreenSpaceVisibility.debugView =
                    static_cast<VisibilityDebugView>(visibilityDebugView);
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Show the default composite, ambient visibility, traced indirect "
                "light, or the indirect response after material application.");
            if (DrawNestedDropdownResetIcon(
                    "DebugVisibility",
                    m_ui.ScreenSpaceVisibility.debugView !=
                        VisibilityDebugView::FinalImage))
            {
                m_ui.ScreenSpaceVisibility.debugView =
                    VisibilityDebugView::FinalImage;
                m_app->ResetImageBasedLightingHistory();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Physically Based Lighting##Debug",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Inspect material and environment-lighting information."))
            {
            static constexpr const char* LightingLabels[] = {
                "Default",
                "Surface Normals",
                "Geometry Normals",
                "Normal Difference",
                "Diffuse Environment",
                "Environment Direction",
                "Reflected Environment",
                "Reflectance Response",
                "Specular Environment",
                "All Environment Light",
                "Specular Visibility",
                "Environment Level"
            };
            int lightingView =
                static_cast<int>(m_ui.LightingDebugView);
            SetNextLabeledControlWidth(
                "Information Filter", settingsControlWidth);
            if (ImGui::Combo(
                    "Information Filter",
                    &lightingView,
                    LightingLabels,
                    static_cast<int>(std::size(LightingLabels))))
            {
                m_ui.LightingDebugView =
                    static_cast<PbrLightingDebugView>(lightingView);
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Choose which material or environment-lighting quantity is "
                "shown. Visibility remains enabled and can still be inspected.");
            if (DrawNestedDropdownResetIcon(
                    "DebugLighting",
                    m_ui.LightingDebugView != PbrLightingDebugView::None))
            {
                m_ui.LightingDebugView = PbrLightingDebugView::None;
                m_app->ResetImageBasedLightingHistory();
            }
            EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Screen-Space Shadows##Debug",
                    ImGuiTreeNodeFlags_DefaultOpen,
                    "Inspect how the directional-shadow trace groups work."))
            {
            ScreenSpaceDirectionalShadowSettings& shadows =
                m_ui.ScreenSpaceDirectionalShadows;
            static constexpr const char* IsolationLabels[] = {
                "Default", "Thread Lanes", "Wave Groups"
            };
            int isolationView =
                shadows.isolationView ==
                    ScreenSpaceShadowIsolationView::Thread
                ? 1
                : shadows.isolationView ==
                    ScreenSpaceShadowIsolationView::Wave
                    ? 2
                    : 0;
            SetNextLabeledControlWidth(
                "Isolation View", settingsControlWidth);
            if (ImGui::Combo(
                    "Isolation View",
                    &isolationView,
                    IsolationLabels,
                    static_cast<int>(std::size(IsolationLabels))))
            {
                shadows.isolationView =
                    isolationView == 1
                    ? ScreenSpaceShadowIsolationView::Thread
                    : isolationView == 2
                        ? ScreenSpaceShadowIsolationView::Wave
                        : ScreenSpaceShadowIsolationView::None;
            }
            ImGui::SetItemTooltip(
                "Show the default image, individual thread lanes, or wave groups.");
            if (DrawNestedDropdownResetIcon(
                    "DebugShadowIsolation",
                    shadows.isolationView !=
                        ScreenSpaceShadowIsolationView::None))
            {
                shadows.isolationView =
                    ScreenSpaceShadowIsolationView::None;
            }
            EndAnimatedTreeNode();
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
                    index < uint32_t(ImageBasedLightingSource::Count);
                    ++index)
                {
                    const ImageBasedLightingSource source =
                        ImageBasedLightingSource(index);
                    const ImageBasedLightingSourceInfo& info =
                        GetImageBasedLightingSourceInfo(source);
                    const bool selected =
                        source == m_ui.EnvironmentSource;
                    DrawDeferredDropdownOption(
                        info.displayName,
                        info.displayName,
                        selected,
                        [this, source]()
                        {
                            const ImageBasedLightingSourceInfo& selectedInfo =
                                GetImageBasedLightingSourceInfo(source);
                            const bool presentationChanged =
                                m_ui.EnvironmentSource != source ||
                                m_ui.EnvironmentExposureStops !=
                                    selectedInfo.defaultExposureStops;
                            m_ui.EnvironmentSource = source;
                            m_ui.EnvironmentExposureStops =
                                selectedInfo.defaultExposureStops;
                            if (presentationChanged)
                            {
                                m_app->ResetImageBasedLightingHistory();
                            }
                        });
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the imported radiance source used by image-based "
                "lighting and the "
                "optional matching background.");
            constexpr ImageBasedLightingSource DefaultEnvironmentSource =
                ImageBasedLightingSource::Kloppenheim03Day;
            if (DrawPresetResetIcon(
                    "Environment Source",
                    m_ui.EnvironmentSource != DefaultEnvironmentSource))
            {
                QueueDeferredControlUiAction(
                    [this]()
                    {
                        constexpr ImageBasedLightingSource
                            DefaultSource =
                                ImageBasedLightingSource::Kloppenheim03Day;
                        m_ui.EnvironmentSource = DefaultSource;
                        m_ui.EnvironmentExposureStops =
                            GetImageBasedLightingSourceInfo(
                                DefaultSource).defaultExposureStops;
                        m_app->ResetImageBasedLightingHistory();
                    });
            }

            const float defaultEnvironmentExposure =
                GetImageBasedLightingSourceInfo(
                    m_ui.EnvironmentSource).defaultExposureStops;
            if (DrawSliderFloat(
                    "Exposure##ImageBasedLighting",
                    &m_ui.EnvironmentExposureStops,
                    -8.f,
                    8.f,
                    "%+.2f stops"))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Scale lighting and the matching background together.");
            if (DrawPresetResetIcon(
                    "Environment Exposure",
                    m_ui.EnvironmentExposureStops !=
                        defaultEnvironmentExposure))
            {
                m_ui.EnvironmentExposureStops =
                    defaultEnvironmentExposure;
                m_app->ResetImageBasedLightingHistory();
            }

            if (ImGui::Checkbox(
                    "Ambient Fill",
                    &m_ui.EnableAmbientFill))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Enable physical diffuse and specular environment fill. "
                "Disable it to isolate direct lights. Ambient-occlusion "
                "settings are preserved, but ambient occlusion needs indirect light to "
                "affect the beauty image.");
            if (DrawPresetResetIcon(
                    "Ambient Fill Enabled",
                    !m_ui.EnableAmbientFill))
            {
                m_ui.EnableAmbientFill = true;
                m_app->ResetImageBasedLightingHistory();
            }
            if (BeginAnimatedToggleRegion(
                    "##AmbientFillControls",
                    m_ui.EnableAmbientFill))
            {
                if (ImGui::Checkbox(
                        "Diffuse Environment",
                        &m_ui.EnableDiffuseIbl))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Use the selected environment for diffuse lighting.");
                if (DrawPresetResetIcon(
                        "Diffuse Environment Enabled",
                        !m_ui.EnableDiffuseIbl))
                {
                    m_ui.EnableDiffuseIbl = true;
                    m_app->ResetImageBasedLightingHistory();
                }
                if (BeginAnimatedToggleRegion(
                        "##DiffuseIblControls",
                        m_ui.EnableDiffuseIbl))
                {
                    if (DrawSliderFloat(
                            "Diffuse Strength##ImageBasedLighting",
                            &m_ui.DiffuseIblStrength,
                            0.f,
                            4.f,
                            "%.2f"))
                    {
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        "Scale diffuse environment lighting after exposure.");
                    if (DrawPresetResetIcon(
                            "Diffuse Environment Strength",
                            m_ui.DiffuseIblStrength != 1.f))
                    {
                        m_ui.DiffuseIblStrength = 1.f;
                        m_app->ResetImageBasedLightingHistory();
                    }
                    EndAnimatedToggleRegion();
                }

                if (ImGui::Checkbox(
                        "Specular Environment",
                        &m_ui.EnableSpecularIbl))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Use the selected environment for specular reflections.");
                if (DrawPresetResetIcon(
                        "Specular Environment Enabled",
                        !m_ui.EnableSpecularIbl))
                {
                    m_ui.EnableSpecularIbl = true;
                    m_app->ResetImageBasedLightingHistory();
                }
                if (BeginAnimatedToggleRegion(
                        "##SpecularIblControls",
                        m_ui.EnableSpecularIbl))
                {
                    if (DrawSliderFloat(
                            "Specular Strength##ImageBasedLighting",
                            &m_ui.SpecularIblStrength,
                            0.f,
                            4.f,
                            "%.2f"))
                    {
                        m_app->ResetImageBasedLightingHistory();
                    }
                    ImGui::SetItemTooltip(
                        "Scale specular environment lighting after exposure.");
                    if (DrawPresetResetIcon(
                            "Specular Environment Strength",
                            m_ui.SpecularIblStrength != 1.f))
                    {
                        m_ui.SpecularIblStrength = 1.f;
                        m_app->ResetImageBasedLightingHistory();
                    }
                    EndAnimatedToggleRegion();
                }

                EndAnimatedToggleRegion();
            }

            if (ImGui::Checkbox(
                    "Show Environment Background",
                    &m_ui.ShowEnvironmentBackground))
            {
                m_app->ResetImageBasedLightingHistory();
            }
            ImGui::SetItemTooltip(
                "Show the same environment used for lighting.");
            if (DrawPresetResetIcon(
                    "Environment Background Enabled",
                    !m_ui.ShowEnvironmentBackground))
            {
                m_ui.ShowEnvironmentBackground = true;
                m_app->ResetImageBasedLightingHistory();
            }

            EndDrawerBody();
        }
        ImGui::Spacing();

        const auto& lights = m_app->GetEditableLights();
        std::shared_ptr<Light> defaultSelectedLight =
            m_app->GetPrimaryDirectionalLight();
        if (!defaultSelectedLight ||
            std::find(
                lights.begin(),
                lights.end(),
                defaultSelectedLight) == lights.end())
        {
            defaultSelectedLight =
                lights.empty() ? nullptr : lights.front();
        }
        if (lights.empty())
        {
            m_SelectedLight.reset();
        }
        else if (std::find(lights.begin(), lights.end(), m_SelectedLight) == lights.end())
        {
            m_SelectedLight = defaultSelectedLight;
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
                        const bool selected = m_SelectedLight == light;
                        DrawDeferredDropdownOption(
                            light->GetName().c_str(),
                            light->GetName().c_str(),
                            selected,
                            [this, light]()
                            {
                                m_SelectedLight = light;
                            });
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (DrawPresetResetIcon(
                        "Selected Light",
                        m_SelectedLight != defaultSelectedLight,
                        "Select the scene's primary directional light."))
                {
                    m_SelectedLight = defaultSelectedLight;
                }

                if (m_SelectedLight)
                {
                    if (m_app->IsFlashlight(m_SelectedLight))
                    {
                        FlashlightSettings& flashlight =
                            m_ui.Flashlight;
                        const FlashlightSettings defaults =
                            DefaultFlashlightSettings;
                        const auto floatChanged =
                            [](float left, float right)
                            {
                                return std::abs(left - right) > 1e-5f;
                            };

                        ImGui::Checkbox(
                            "Enabled (F)",
                            &m_ui.FlashlightEnabled);
                        ImGui::SetItemTooltip(
                            "Turn the camera flashlight on or off. Plain F "
                            "uses the same setting.");
                        if (DrawPresetResetIcon(
                                "Flashlight Enabled",
                                m_ui.FlashlightEnabled !=
                                    DefaultFlashlightEnabled))
                        {
                            m_ui.FlashlightEnabled =
                                DefaultFlashlightEnabled;
                        }
                        ImGui::Checkbox(
                            "Cast Shadows",
                            &flashlight.castShadows);
                        ImGui::SetItemTooltip(
                            "Render geometry visibility for both flashlight "
                            "beam lobes.");
                        if (DrawPresetResetIcon(
                                "Flashlight Cast Shadows",
                                flashlight.castShadows !=
                                    defaults.castShadows))
                        {
                            flashlight.castShadows =
                                defaults.castShadows;
                        }

                        ImGui::Checkbox(
                            "Realistic Flashlight",
                            &flashlight.realisticLens);
                        ImGui::SetItemTooltip(
                            "Add a lens hotspot, bounded sway, and aim "
                            "correction. This uses one extra local light, plus "
                            "one extra shadow sample when Cast Shadows is "
                            "enabled.");
                        if (DrawPresetResetIcon(
                                "Realistic Flashlight",
                                flashlight.realisticLens !=
                                    defaults.realisticLens))
                        {
                            flashlight.realisticLens =
                                defaults.realisticLens;
                        }

                        if (BeginAnimatedToggleRegion(
                                "##RealisticFlashlightControls",
                                flashlight.realisticLens))
                        {
                            DrawSliderFloat(
                                "Hotspot Size",
                                &flashlight.hotspotSize,
                                FlashlightMinimumHotspotSize,
                                FlashlightMaximumHotspotSize,
                                "%.2f");
                            ImGui::SetItemTooltip(
                                "Set the focused lens hotspot width relative "
                                "to the complete beam.");
                            if (DrawPresetResetIcon(
                                    "Flashlight Hotspot Size",
                                    floatChanged(
                                        flashlight.hotspotSize,
                                        defaults.hotspotSize)))
                            {
                                flashlight.hotspotSize =
                                    defaults.hotspotSize;
                            }

                            DrawSliderFloat(
                                "Hotspot Strength",
                                &flashlight.hotspotStrength,
                                0.f,
                                FlashlightMaximumHotspotStrength,
                                "%.2f");
                            ImGui::SetItemTooltip(
                                "Move peak candela from the broad spill into "
                                "the focused lens hotspot.");
                            if (DrawPresetResetIcon(
                                    "Flashlight Hotspot Strength",
                                    floatChanged(
                                        flashlight.hotspotStrength,
                                        defaults.hotspotStrength)))
                            {
                                flashlight.hotspotStrength =
                                    defaults.hotspotStrength;
                            }

                            DrawSliderFloat(
                                "Sway",
                                &flashlight.swayDegrees,
                                0.f,
                                FlashlightMaximumSwayDegrees,
                                "%.2f degrees");
                            ImGui::SetItemTooltip(
                                "Set the maximum subtle handheld aim motion. "
                                "Zero keeps the corrected beam perfectly still.");
                            if (DrawPresetResetIcon(
                                    "Flashlight Sway",
                                    floatChanged(
                                        flashlight.swayDegrees,
                                        defaults.swayDegrees)))
                            {
                                flashlight.swayDegrees =
                                    defaults.swayDegrees;
                            }

                            DrawSliderFloat(
                                "Aim Correction",
                                &flashlight.aimCorrectionSeconds,
                                FlashlightMinimumAimCorrectionSeconds,
                                FlashlightMaximumAimCorrectionSeconds,
                                "%.2f s");
                            ImGui::SetItemTooltip(
                                "Set the half-life for the beam to catch up "
                                "after the camera turns.");
                            if (DrawPresetResetIcon(
                                    "Flashlight Aim Correction",
                                    floatChanged(
                                        flashlight.aimCorrectionSeconds,
                                        defaults.aimCorrectionSeconds)))
                            {
                                flashlight.aimCorrectionSeconds =
                                    defaults.aimCorrectionSeconds;
                            }

                            EndAnimatedToggleRegion();
                        }

                        DrawSliderFloat(
                            "Brightness",
                            &flashlight.peakIntensityCandela,
                            FlashlightMinimumIntensityCandela,
                            FlashlightMaximumIntensityCandela,
                            "%.0f candela",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the peak on-axis luminous intensity.");
                        if (DrawPresetResetIcon(
                                "Flashlight Brightness",
                                floatChanged(
                                    flashlight.peakIntensityCandela,
                                    defaults.peakIntensityCandela)))
                        {
                            flashlight.peakIntensityCandela =
                                defaults.peakIntensityCandela;
                        }

                        DrawSliderFloat(
                            "Beam Size",
                            &flashlight.beamSizeDegrees,
                            FlashlightMinimumBeamSizeDegrees,
                            FlashlightMaximumBeamSizeDegrees,
                            "%.1f degrees");
                        ImGui::SetItemTooltip(
                            "Set the full horizontal and vertical outer beam "
                            "width.");
                        if (DrawPresetResetIcon(
                                "Flashlight Beam Size",
                                floatChanged(
                                    flashlight.beamSizeDegrees,
                                    defaults.beamSizeDegrees)))
                        {
                            flashlight.beamSizeDegrees =
                                defaults.beamSizeDegrees;
                        }

                        DrawSliderFloat(
                            "Beam Roundness",
                            &flashlight.beamRoundness,
                            0.f,
                            1.f,
                            "%.2f");
                        ImGui::SetItemTooltip(
                            "Morph the beam footprint from a softly rounded "
                            "square to an exact circle.");
                        if (DrawPresetResetIcon(
                                "Flashlight Beam Roundness",
                                floatChanged(
                                    flashlight.beamRoundness,
                                    defaults.beamRoundness)))
                        {
                            flashlight.beamRoundness =
                                defaults.beamRoundness;
                        }

                        DrawSliderFloat(
                            "Edge Softness",
                            &flashlight.edgeSoftness,
                            0.f,
                            1.f,
                            "%.2f");
                        ImGui::SetItemTooltip(
                            "Set the falloff width without changing the "
                            "projected beam shape.");
                        if (DrawPresetResetIcon(
                                "Flashlight Edge Softness",
                                floatChanged(
                                    flashlight.edgeSoftness,
                                    defaults.edgeSoftness)))
                        {
                            flashlight.edgeSoftness =
                                defaults.edgeSoftness;
                        }

                        DrawSliderFloat(
                            "Range",
                            &flashlight.rangeMeters,
                            FlashlightMinimumRangeMeters,
                            FlashlightMaximumRangeMeters,
                            "%.1f m",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the finite distance where the beam fades out.");
                        if (DrawPresetResetIcon(
                                "Flashlight Range",
                                floatChanged(
                                    flashlight.rangeMeters,
                                    defaults.rangeMeters)))
                        {
                            flashlight.rangeMeters =
                                defaults.rangeMeters;
                        }

                        float flashlightColor[] = {
                            flashlight.colorLinearRed,
                            flashlight.colorLinearGreen,
                            flashlight.colorLinearBlue
                        };
                        if (ImGui::ColorEdit3(
                                "Color",
                                flashlightColor,
                                ImGuiColorEditFlags_Float |
                                    ImGuiColorEditFlags_DisplayRGB))
                        {
                            flashlight.colorLinearRed =
                                flashlightColor[0];
                            flashlight.colorLinearGreen =
                                flashlightColor[1];
                            flashlight.colorLinearBlue =
                                flashlightColor[2];
                        }
                        ImGui::SetItemTooltip(
                            "Set the flashlight's scene-linear red, green, and "
                            "blue color.");
                        if (DrawPresetResetIcon(
                                "Flashlight Color",
                                floatChanged(
                                    flashlight.colorLinearRed,
                                    defaults.colorLinearRed) ||
                                floatChanged(
                                    flashlight.colorLinearGreen,
                                    defaults.colorLinearGreen) ||
                                floatChanged(
                                    flashlight.colorLinearBlue,
                                    defaults.colorLinearBlue)))
                        {
                            flashlight.colorLinearRed =
                                defaults.colorLinearRed;
                            flashlight.colorLinearGreen =
                                defaults.colorLinearGreen;
                            flashlight.colorLinearBlue =
                                defaults.colorLinearBlue;
                        }

                        float cameraOffsetCentimeters =
                            flashlight.cameraLateralOffsetMeters * 100.f;
                        if (DrawSliderFloat(
                                "Camera Offset",
                                &cameraOffsetCentimeters,
                                FlashlightMinimumCameraLateralOffsetMeters *
                                    100.f,
                                FlashlightMaximumCameraLateralOffsetMeters *
                                    100.f,
                                "%.1f centimeters"))
                        {
                            flashlight.cameraLateralOffsetMeters =
                                cameraOffsetCentimeters * 0.01f;
                        }
                        ImGui::SetItemTooltip(
                            "Move the flashlight sideways from the camera. "
                            "Larger offsets separate projected shadows farther "
                            "from their casters; zero centers the emitter. The "
                            "beam still converges on the camera aim at 6 m. "
                            "Large offsets can intersect nearby geometry.");
                        if (DrawPresetResetIcon(
                                "Flashlight Camera Offset",
                                floatChanged(
                                    flashlight.cameraLateralOffsetMeters,
                                    defaults.cameraLateralOffsetMeters)))
                        {
                            flashlight.cameraLateralOffsetMeters =
                                defaults.cameraLateralOffsetMeters;
                        }

                    }
                    else
                    {
                    const auto selectedLightIterator = std::find(
                        lights.begin(), lights.end(), m_SelectedLight);
                    const size_t selectedLightIndex = size_t(std::distance(
                        lights.begin(), selectedLightIterator));
                    const std::string defaultLightKey =
                        m_app->GetCurrentSceneName() + "\n" +
                        std::to_string(selectedLightIndex) + "\n" +
                        m_SelectedLight->GetName();
                    auto captureLightDefaults =
                        [](const Light& light)
                        {
                            LightDefaultState result;
                            result.type = light.GetLightType();
                            result.direction = light.GetDirection();
                            result.color = light.color;
                            switch (result.type)
                            {
                            case LightType_Directional:
                            {
                                const auto& directional =
                                    static_cast<const DirectionalLight&>(
                                        light);
                                result.irradiance =
                                    directional.irradiance;
                                result.angularSize =
                                    directional.angularSize;
                                break;
                            }
                            case LightType_Point:
                            {
                                const auto& point =
                                    static_cast<const PointLight&>(light);
                                result.radius = point.radius;
                                result.intensity = point.intensity;
                                break;
                            }
                            case LightType_Spot:
                            {
                                const auto& spot =
                                    static_cast<const SpotLight&>(light);
                                result.radius = spot.radius;
                                result.intensity = spot.intensity;
                                result.innerAngle = spot.innerAngle;
                                result.outerAngle = spot.outerAngle;
                                break;
                            }
                            default:
                                break;
                            }
                            return result;
                        };
                    const auto [defaultLightIterator, inserted] =
                        m_LightDefaults.try_emplace(
                            defaultLightKey,
                            captureLightDefaults(*m_SelectedLight));
                    (void)inserted;
                    const LightDefaultState& defaultLight =
                        defaultLightIterator->second;
                    const auto floatChanged =
                        [](float left, float right)
                        {
                            return std::abs(left - right) > 1e-5f;
                        };
                    const auto colorChanged =
                        [&](const float3& left, const float3& right)
                        {
                            return floatChanged(left.x, right.x) ||
                                floatChanged(left.y, right.y) ||
                                floatChanged(left.z, right.z);
                        };
                    const auto directionChanged =
                        [](const double3& left, const double3& right)
                        {
                            return std::abs(left.x - right.x) > 1e-7 ||
                                std::abs(left.y - right.y) > 1e-7 ||
                                std::abs(left.z - right.z) > 1e-7;
                        };
                    const auto drawLightColor =
                        [&](Light& light)
                        {
                            ImGui::ColorEdit3(
                                "Color",
                                &light.color.x,
                                ImGuiColorEditFlags_Float);
                            ImGui::SetItemTooltip(
                                "Set the selected light's color.");
                            if (DrawPresetResetIcon(
                                    "Light Color",
                                    colorChanged(
                                        light.color,
                                        defaultLight.color)))
                            {
                                light.color = defaultLight.color;
                            }
                        };
                    const auto drawLightDirection =
                        [&](Light& light, bool negative)
                        {
                            double3 direction = light.GetDirection();
                            if (app::AzimuthElevationSliders(
                                    direction, negative))
                            {
                                light.SetDirection(direction);
                                m_app->ResetImageBasedLightingHistory();
                            }
                            ImGui::SetItemTooltip(
                                "Set the selected light's direction.");
                            if (DrawPresetResetIcon(
                                    "Light Direction",
                                    directionChanged(
                                        light.GetDirection(),
                                        defaultLight.direction)))
                            {
                                light.SetDirection(
                                    defaultLight.direction);
                                m_app->ResetImageBasedLightingHistory();
                            }
                        };

                    switch (m_SelectedLight->GetLightType())
                    {
                    case LightType_Directional:
                    {
                        auto& light = static_cast<DirectionalLight&>(
                            *m_SelectedLight);
                        drawLightDirection(light, true);
                        drawLightColor(light);
                        DrawSliderFloat(
                            "Irradiance",
                            &light.irradiance,
                            0.f,
                            100.f,
                            "%.2f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the directional light irradiance.");
                        if (DrawPresetResetIcon(
                                "Light Irradiance",
                                floatChanged(
                                    light.irradiance,
                                    defaultLight.irradiance)))
                        {
                            light.irradiance =
                                defaultLight.irradiance;
                        }
                        if (DrawSliderFloat(
                                "Angular Size",
                                &light.angularSize,
                                0.f,
                                20.f))
                        {
                            m_app->ResetImageBasedLightingHistory();
                        }
                        ImGui::SetItemTooltip(
                            "Set the directional light's full angular diameter. "
                            "Zero degrees is a zero-extent directional emitter "
                            "with geometrically hard shadows.");
                        if (DrawPresetResetIcon(
                                "Light Angular Size",
                                floatChanged(
                                    light.angularSize,
                                    defaultLight.angularSize)))
                        {
                            light.angularSize =
                                defaultLight.angularSize;
                            m_app->ResetImageBasedLightingHistory();
                        }
                        break;
                    }
                    case LightType_Point:
                    {
                        auto& light = static_cast<PointLight&>(
                            *m_SelectedLight);
                        DrawSliderFloat(
                            "Radius",
                            &light.radius,
                            0.01f,
                            1.f,
                            "%.3f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the point light radius.");
                        if (DrawPresetResetIcon(
                                "Light Radius",
                                floatChanged(
                                    light.radius,
                                    defaultLight.radius)))
                        {
                            light.radius = defaultLight.radius;
                        }
                        drawLightColor(light);
                        DrawSliderFloat(
                            "Intensity",
                            &light.intensity,
                            0.f,
                            100.f,
                            "%.2f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the point light intensity.");
                        if (DrawPresetResetIcon(
                                "Light Intensity",
                                floatChanged(
                                    light.intensity,
                                    defaultLight.intensity)))
                        {
                            light.intensity =
                                defaultLight.intensity;
                        }
                        break;
                    }
                    case LightType_Spot:
                    {
                        auto& light = static_cast<SpotLight&>(
                            *m_SelectedLight);
                        drawLightDirection(light, false);
                        DrawSliderFloat(
                            "Radius",
                            &light.radius,
                            0.01f,
                            1.f,
                            "%.3f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the spot light radius.");
                        if (DrawPresetResetIcon(
                                "Light Radius",
                                floatChanged(
                                    light.radius,
                                    defaultLight.radius)))
                        {
                            light.radius = defaultLight.radius;
                        }
                        drawLightColor(light);
                        DrawSliderFloat(
                            "Intensity",
                            &light.intensity,
                            0.f,
                            100.f,
                            "%.2f",
                            ImGuiSliderFlags_Logarithmic);
                        ImGui::SetItemTooltip(
                            "Set the spot light intensity.");
                        if (DrawPresetResetIcon(
                                "Light Intensity",
                                floatChanged(
                                    light.intensity,
                                    defaultLight.intensity)))
                        {
                            light.intensity =
                                defaultLight.intensity;
                        }
                        DrawSliderFloat(
                            "Inner Angle",
                            &light.innerAngle,
                            0.f,
                            180.f);
                        ImGui::SetItemTooltip(
                            "Set the full-bright spot cone angle.");
                        if (DrawPresetResetIcon(
                                "Light Inner Angle",
                                floatChanged(
                                    light.innerAngle,
                                    defaultLight.innerAngle)))
                        {
                            light.innerAngle =
                                defaultLight.innerAngle;
                        }
                        DrawSliderFloat(
                            "Outer Angle",
                            &light.outerAngle,
                            0.f,
                            180.f);
                        ImGui::SetItemTooltip(
                            "Set the outer spot cone angle.");
                        if (DrawPresetResetIcon(
                                "Light Outer Angle",
                                floatChanged(
                                    light.outerAngle,
                                    defaultLight.outerAngle)))
                        {
                            light.outerAngle =
                                defaultLight.outerAngle;
                        }
                        break;
                    }
                    default:
                        ImGui::TextDisabled(
                            "This light type has no editable settings.");
                        break;
                    }
                    }
                }
            }

            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool shadowsOpen = DrawCollapsingHeader(
            "Shadows", "Configure independent directional-shadow producers.");
        if (shadowsOpen)
        {
            BeginDrawerBody(
                "##ShadowsBody",
                settingsControlWidth);

            const bool directionalVisibilityAvailable =
                m_app->HasPrimaryDirectionalLight();
            const bool rayTracedShadowHardwareAvailable =
                m_app->HasHeitzRatioEstimatorHardwareSupport();
            const bool rayTracedShadowsAvailable =
                directionalVisibilityAvailable &&
                m_app->SupportsHeitzRatioEstimatorShadows();

            if (!directionalVisibilityAvailable)
            {
                ImGui::TextDisabled(
                    "Directional techniques require a directional light.");
            }
            else if (!rayTracedShadowHardwareAvailable)
            {
                ImGui::TextDisabled(
                    "Ray-traced ratio estimation requires DXR 1.1 support.");
            }
            else if (!rayTracedShadowsAvailable)
            {
                ImGui::TextDisabled(
                    "Ray-traced ratio estimation requires single-sample "
                    "rendering; disable MSAA to use it.");
            }

            if (BeginAnimatedTreeNode(
                    "Screen-Space Directional Shadows##Shadows",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                ScreenSpaceDirectionalShadowSettings& shadows =
                    m_ui.ScreenSpaceDirectionalShadows;
                const ScreenSpaceDirectionalShadowSettings shadowDefaults{};
                const bool disableScreenSpaceEnable =
                    !directionalVisibilityAvailable && !shadows.enabled;
                if (disableScreenSpaceEnable)
                    ImGui::BeginDisabled();
                if (ImGui::Checkbox(
                        "Enabled##ScreenSpaceShadows",
                        &shadows.enabled))
                {
                    m_app->ResetImageBasedLightingHistory();
                }
                ImGui::SetItemTooltip(
                    "Trace screen-space directional shadows independently "
                    "of the ray-traced ratio estimator.");
                if (disableScreenSpaceEnable)
                    ImGui::EndDisabled();
                if (DrawPresetResetIcon(
                        "ScreenSpaceShadowEnabled",
                        shadows.enabled != shadowDefaults.enabled))
                {
                    shadows.enabled = shadowDefaults.enabled;
                    m_app->ResetImageBasedLightingHistory();
                }
                static constexpr auto isSameShadowConfiguration =
                    [](const ScreenSpaceDirectionalShadowSettings& left,
                        const ScreenSpaceDirectionalShadowSettings& right)
                    {
                        return left.length == right.length &&
                            left.surfaceThickness ==
                                right.surfaceThickness &&
                            left.bilinearThreshold ==
                                right.bilinearThreshold &&
                            left.shadowContrast ==
                                right.shadowContrast &&
                            left.hardShadowSamples ==
                                right.hardShadowSamples &&
                            left.fadeOutSamples ==
                                right.fadeOutSamples &&
                            left.ignoreEdgePixels ==
                                right.ignoreEdgePixels &&
                            left.usePrecisionOffset ==
                                right.usePrecisionOffset &&
                            left.bilinearSamplingOffsetMode ==
                                right.bilinearSamplingOffsetMode &&
                            left.useEarlyOut == right.useEarlyOut;
                    };
                static constexpr auto reconcileShadowPreset =
                    [](ScreenSpaceDirectionalShadowSettings& settings)
                    {
                        constexpr ScreenSpaceShadowPreset Presets[] = {
                            ScreenSpaceShadowPreset::Default,
                            ScreenSpaceShadowPreset::Long,
                            ScreenSpaceShadowPreset::MaximumValidation
                        };
                        for (const ScreenSpaceShadowPreset preset : Presets)
                        {
                            ScreenSpaceDirectionalShadowSettings presetSettings =
                                settings;
                            ApplyScreenSpaceShadowPreset(
                                presetSettings,
                                preset);
                            if (isSameShadowConfiguration(
                                    settings,
                                    presetSettings))
                            {
                                settings.preset = preset;
                                return;
                            }
                        }
                        settings.preset = ScreenSpaceShadowPreset::Custom;
                    };
                if (BeginAnimatedToggleRegion(
                        "##ScreenSpaceShadowControls",
                        shadows.enabled && directionalVisibilityAvailable))
                {
                    bool shadowCustomChanged = false;
                    bool shadowResetApplied = false;
                    static constexpr const char* PresetLabels[] = {
                        "Default",
                        "Long",
                        "Maximum Validation",
                        "Custom"
                    };
                    const int presetIndex = std::clamp(
                        int(shadows.preset),
                        0,
                        int(std::size(PresetLabels)) - 1);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Profile##ScreenSpaceShadows",
                            PresetLabels[presetIndex]))
                    {
                        for (int index = 0;
                            index < int(std::size(PresetLabels));
                            ++index)
                        {
                            const ScreenSpaceShadowPreset preset =
                                ScreenSpaceShadowPreset(index);
                            DrawDeferredDropdownOption(
                                PresetLabels[index],
                                PresetLabels[index],
                                shadows.preset == preset,
                                [settings = &shadows, preset]()
                                {
                                    ApplyScreenSpaceShadowPreset(
                                        *settings,
                                        preset);
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Trade trace reach and cost: 60 pixels for Default, "
                        "240 for Long, or 960 for Maximum Validation.");
                    ScreenSpaceDirectionalShadowSettings defaultSettings =
                        shadows;
                    ApplyScreenSpaceShadowPreset(
                        defaultSettings,
                        ScreenSpaceShadowPreset::Default);
                    if (DrawNestedDropdownResetIcon(
                            "ScreenSpaceShadowProfile",
                            shadows.preset !=
                                    ScreenSpaceShadowPreset::Default ||
                                !isSameShadowConfiguration(
                                    shadows,
                                    defaultSettings),
                            "Reset every screen-space shadow setting to Default."))
                    {
                        QueueDeferredControlUiAction(
                            [settings = &shadows]()
                            {
                                ApplyScreenSpaceShadowPreset(
                                    *settings,
                                    ScreenSpaceShadowPreset::Default);
                            });
                    }

                    static constexpr const char* LengthLabels[] = {
                        "60 pixels", "120 pixels", "240 pixels",
                        "480 pixels", "960 pixels"
                    };
                    int lengthIndex = FindScreenSpaceShadowSupportedValue(
                        ScreenSpaceShadowTraceReaches,
                        GetScreenSpaceShadowTraceReach(shadows.length));
                    lengthIndex = std::clamp(
                        lengthIndex,
                        0,
                        int(std::size(LengthLabels)) - 1);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Length##ScreenSpaceShadows",
                            LengthLabels[lengthIndex]))
                    {
                        for (int index = 0;
                            index < int(std::size(LengthLabels));
                            ++index)
                        {
                            const ScreenSpaceShadowLength length =
                                ScreenSpaceShadowLength(
                                    ScreenSpaceShadowTraceReaches[size_t(index)]);
                            DrawDeferredDropdownOption(
                                LengthLabels[index],
                                LengthLabels[index],
                                shadows.length == length,
                                [settings = &shadows, length]()
                                {
                                    settings->length = length;
                                    settings->preset =
                                        ScreenSpaceShadowPreset::Custom;
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Set the runtime screen-space trace reach.");
                    if (DrawNestedDropdownResetIcon(
                            "ScreenSpaceShadowLength",
                            shadows.length != shadowDefaults.length))
                    {
                        const ScreenSpaceShadowLength defaultLength =
                            shadowDefaults.length;
                        QueueDeferredControlUiAction(
                            [settings = &shadows, defaultLength]()
                            {
                                settings->length = defaultLength;
                                reconcileShadowPreset(*settings);
                            });
                    }

                    shadowCustomChanged |= DrawSliderFloat(
                        "Surface Thickness##ScreenSpaceShadows",
                        &shadows.surfaceThickness,
                        0.f,
                        0.05f,
                        "%.4f");
                    ImGui::SetItemTooltip(
                        "Set nonlinear-depth occluder thickness.");
                    if (DrawPresetResetIcon(
                            "ScreenSpaceShadowSurfaceThickness",
                            shadows.surfaceThickness !=
                                shadowDefaults.surfaceThickness))
                    {
                        shadows.surfaceThickness =
                            shadowDefaults.surfaceThickness;
                        shadowResetApplied = true;
                    }
                    shadowCustomChanged |= DrawSliderFloat(
                        "Bilinear Threshold##ScreenSpaceShadows",
                        &shadows.bilinearThreshold,
                        0.f,
                        0.1f,
                        "%.3f");
                    ImGui::SetItemTooltip(
                        "Set the relative depth discontinuity that disables "
                        "interpolation.");
                    if (DrawPresetResetIcon(
                            "ScreenSpaceShadowBilinearThreshold",
                            shadows.bilinearThreshold !=
                                shadowDefaults.bilinearThreshold))
                    {
                        shadows.bilinearThreshold =
                            shadowDefaults.bilinearThreshold;
                        shadowResetApplied = true;
                    }
                    shadowCustomChanged |= DrawSliderFloat(
                        "Shadow Contrast##ScreenSpaceShadows",
                        &shadows.shadowContrast,
                        1.f,
                        16.f,
                        "%.1f");
                    ImGui::SetItemTooltip(
                        "Set visibility transition contrast.");
                    if (DrawPresetResetIcon(
                            "ScreenSpaceShadowContrast",
                            shadows.shadowContrast !=
                                shadowDefaults.shadowContrast))
                    {
                        shadows.shadowContrast =
                            shadowDefaults.shadowContrast;
                        shadowResetApplied = true;
                    }

                    static constexpr const char* HardSampleLabels[] = {
                        "0", "4", "8"
                    };
                    const int selectedHard =
                        FindScreenSpaceShadowSupportedValue(
                            ScreenSpaceShadowHardSampleCounts,
                            shadows.hardShadowSamples);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Hard Shadow Samples##ScreenSpaceShadows",
                            selectedHard >= 0
                                ? HardSampleLabels[selectedHard]
                                : "Unsupported"))
                    {
                        for (int index = 0;
                            index < int(std::size(HardSampleLabels));
                            ++index)
                        {
                            const uint32_t value =
                                ScreenSpaceShadowHardSampleCounts[size_t(index)];
                            DrawDeferredDropdownOption(
                                HardSampleLabels[index],
                                HardSampleLabels[index],
                                shadows.hardShadowSamples == value,
                                [settings = &shadows, value]()
                                {
                                    settings->hardShadowSamples = value;
                                    settings->preset =
                                        ScreenSpaceShadowPreset::Custom;
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Set the runtime count of fully hard contact "
                        "samples.");
                    if (DrawNestedDropdownResetIcon(
                            "ScreenSpaceShadowHardSamples",
                            shadows.hardShadowSamples !=
                                shadowDefaults.hardShadowSamples))
                    {
                        const uint32_t defaultHardShadowSamples =
                            shadowDefaults.hardShadowSamples;
                        QueueDeferredControlUiAction(
                            [settings = &shadows,
                                defaultHardShadowSamples]()
                            {
                                settings->hardShadowSamples =
                                    defaultHardShadowSamples;
                                reconcileShadowPreset(*settings);
                            });
                    }

                    static constexpr const char* FadeSampleLabels[] = {
                        "0", "8", "16"
                    };
                    const int selectedFade =
                        FindScreenSpaceShadowSupportedValue(
                            ScreenSpaceShadowFadeSampleCounts,
                            shadows.fadeOutSamples);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Fade-Out Samples##ScreenSpaceShadows",
                            selectedFade >= 0
                                ? FadeSampleLabels[selectedFade]
                                : "Unsupported"))
                    {
                        for (int index = 0;
                            index < int(std::size(FadeSampleLabels));
                            ++index)
                        {
                            const uint32_t value =
                                ScreenSpaceShadowFadeSampleCounts[size_t(index)];
                            DrawDeferredDropdownOption(
                                FadeSampleLabels[index],
                                FadeSampleLabels[index],
                                shadows.fadeOutSamples == value,
                                [settings = &shadows, value]()
                                {
                                    settings->fadeOutSamples = value;
                                    settings->preset =
                                        ScreenSpaceShadowPreset::Custom;
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Set the runtime count of samples that soften "
                        "the trace endpoint.");
                    if (DrawNestedDropdownResetIcon(
                            "ScreenSpaceShadowFade-OutSamples",
                            shadows.fadeOutSamples !=
                                shadowDefaults.fadeOutSamples))
                    {
                        const uint32_t defaultFadeOutSamples =
                            shadowDefaults.fadeOutSamples;
                        QueueDeferredControlUiAction(
                            [settings = &shadows,
                                defaultFadeOutSamples]()
                            {
                                settings->fadeOutSamples =
                                    defaultFadeOutSamples;
                                reconcileShadowPreset(*settings);
                            });
                    }

                    shadowCustomChanged |= ImGui::Checkbox(
                        "Ignore Edge Pixels##ScreenSpaceShadows",
                        &shadows.ignoreEdgePixels);
                    ImGui::SetItemTooltip(
                        "Prevent detected depth-edge pixels from casting "
                        "shadows.");
                    if (DrawPresetResetIcon(
                            "ScreenSpaceShadowIgnoreEdgePixels",
                            shadows.ignoreEdgePixels !=
                                shadowDefaults.ignoreEdgePixels))
                    {
                        shadows.ignoreEdgePixels =
                            shadowDefaults.ignoreEdgePixels;
                        shadowResetApplied = true;
                    }
                    shadowCustomChanged |= ImGui::Checkbox(
                        "Precision Offset##ScreenSpaceShadows",
                        &shadows.usePrecisionOffset);
                    ImGui::SetItemTooltip(
                        "Bias the ray origin slightly toward the near plane "
                        "to compensate for depth quantization.");
                    if (DrawPresetResetIcon(
                            "ScreenSpaceShadowPrecisionOffset",
                            shadows.usePrecisionOffset !=
                                shadowDefaults.usePrecisionOffset))
                    {
                        shadows.usePrecisionOffset =
                            shadowDefaults.usePrecisionOffset;
                        shadowResetApplied = true;
                    }
                    shadowCustomChanged |= ImGui::Checkbox(
                        "Bilinear Offset Mode##ScreenSpaceShadows",
                        &shadows.bilinearSamplingOffsetMode);
                    ImGui::SetItemTooltip(
                        "Add a second depth candidate one pixel farther "
                        "along the trace, allowing neighbor-only crossings.");
                    if (DrawPresetResetIcon(
                            "ScreenSpaceShadowBilinearOffsetMode",
                            shadows.bilinearSamplingOffsetMode !=
                                shadowDefaults.bilinearSamplingOffsetMode))
                    {
                        shadows.bilinearSamplingOffsetMode =
                            shadowDefaults.bilinearSamplingOffsetMode;
                        shadowResetApplied = true;
                    }
                    shadowCustomChanged |= ImGui::Checkbox(
                        "Early Out##ScreenSpaceShadows",
                        &shadows.useEarlyOut);
                    ImGui::SetItemTooltip(
                        shadows.isolationView ==
                                ScreenSpaceShadowIsolationView::None
                            ? "Skip depth-bound receivers, usually sky, when "
                                "a complete wavefront can exit together."
                            : "Keep tracing to preserve complete debug "
                                "diagnostics.");
                    if (DrawPresetResetIcon(
                            "ScreenSpaceShadowEarlyOut",
                            shadows.useEarlyOut !=
                                shadowDefaults.useEarlyOut))
                    {
                        shadows.useEarlyOut =
                            shadowDefaults.useEarlyOut;
                        shadowResetApplied = true;
                    }

                    if (shadowCustomChanged)
                        shadows.preset = ScreenSpaceShadowPreset::Custom;
                    else if (shadowResetApplied)
                        reconcileShadowPreset(shadows);
                    EndAnimatedToggleRegion();
                }
                EndAnimatedTreeNode();
            }

            drawRatioEstimatorShadowControls();
            EndDrawerBody();
        }
        ImGui::Spacing();

        TrackSettingsScrollAnchor(
            ImGui::GetID("##SettingsFooterAnchor"),
            ImGui::GetCursorScreenPos().y);
        constexpr float ActionButtonCount = 4.f;
        const float actionButtonWidth = std::max(
            1.f,
            (ImGui::GetContentRegionAvail().x -
                style.ItemSpacing.x * (ActionButtonCount - 1.f)) /
                ActionButtonCount);

        const ImVec4 drawerBackgroundColor =
            g_UiVisualTokens.actionButton;
        const ImVec4 drawerBackgroundHoveredColor =
            g_UiVisualTokens.actionButtonHovered;
        const ImVec4 drawerBackgroundActiveColor =
            g_UiVisualTokens.actionButtonActive;
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
        if (DrawCenteredActionButton(
                GetPixelZoomButtonLabel(m_ui.PixelZoom),
                actionButtonWidth))
        {
            m_ui.PixelZoom =
                AdvancePixelZoomMode(m_ui.PixelZoom);
        }
        ImGui::SetItemTooltip(
            "Cycle exact Off, 2x, 3x, 4x, and 5x pixel zoom. Z uses the "
            "same cycle.");

        ImGui::SameLine();
        if (DrawCenteredActionButton("Restart", actionButtonWidth))
        {
            g_RestartRequested = true;
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), GLFW_TRUE);
        }
        ImGui::SetItemTooltip("Restart UVSR.");
        ImGui::PopStyleColor(3);

        if (settingsScrollInputBlocked)
        {
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
        }
        if (deferredDropdownInputBlocked)
        {
            ImGui::EndDisabled();
            ImGui::PopStyleVar();
        }

        EndSettingsScrollStability();
        ImGuiWindow* settingsBodyWindow =
            ImGui::GetCurrentWindow();
        const bool settingsScrollIdle =
            settingsBodyWindow->ScrollTarget.y >= FLT_MAX;
        const bool settingsLayoutIdle =
            !g_SettingsScrollStabilityContext
                .layoutAnimatingLastFrame;
        // Wheel motion is eased by the UI layer; this viewport fade keeps
        // partially clipped rows from popping into full contrast at either
        // edge while the settings list travels.
        DrawSettingsScrollEdgeFades();
        ImGui::EndChild();
        const ImVec2 settingsWindowPosition =
            ImGui::GetWindowPos();
        const ImVec2 settingsWindowSize =
            ImGui::GetWindowSize();
        const ImVec2 settingsWindowCenter(
            settingsWindowPosition.x + settingsWindowSize.x * 0.5f,
            settingsWindowPosition.y + settingsWindowSize.y * 0.5f);
        const bool settingsCollapsed =
            ImGui::IsWindowCollapsed();
        m_SettingsCollapsed = settingsCollapsed;
        const float settingsTitleHeight =
            fontSize + style.FramePadding.y * 2.f;
        if (settingsCollapsed)
        {
            UiBackdropRect& titleBackdrop =
                m_ui.BackdropRects[0];
            titleBackdrop.minX = settingsWindowPosition.x + 0.5f;
            titleBackdrop.minY = settingsWindowPosition.y + 0.5f;
            titleBackdrop.maxX =
                settingsWindowPosition.x + settingsWindowSize.x - 0.5f;
            titleBackdrop.maxY =
                settingsWindowPosition.y + settingsTitleHeight - 0.5f;
            titleBackdrop.rounding = style.FrameRounding;
            titleBackdrop.visible =
                titleBackdrop.maxX > titleBackdrop.minX &&
                titleBackdrop.maxY > titleBackdrop.minY;

            UiBackdropRect& statusBackdrop =
                m_ui.BackdropRects[1];
            statusBackdrop.minX = settingsWindowPosition.x + 0.5f;
            statusBackdrop.minY =
                settingsWindowPosition.y + settingsTitleHeight - 1.f;
            statusBackdrop.maxX =
                settingsWindowPosition.x + settingsWindowSize.x - 0.5f;
            statusBackdrop.maxY =
                settingsWindowPosition.y + settingsWindowSize.y - 0.5f;
            statusBackdrop.rounding = std::min(
                style.WindowRounding,
                std::max(
                    0.f,
                    (settingsWindowSize.y -
                        settingsTitleHeight + 1.f) * 0.15f));
            statusBackdrop.visible =
                statusBackdrop.maxX > statusBackdrop.minX &&
                statusBackdrop.maxY > statusBackdrop.minY;
        }
        else
        {
            // Match the two actual rounded surfaces drawn by the ImGui
            // override. A single union rectangle would blur the empty upper
            // corner wedges of the body between it and the title.
            UiBackdropRect& titleBackdrop =
                m_ui.BackdropRects[0];
            titleBackdrop.minX = settingsWindowPosition.x + 0.5f;
            titleBackdrop.minY = settingsWindowPosition.y + 0.5f;
            titleBackdrop.maxX =
                settingsWindowPosition.x + settingsWindowSize.x - 0.5f;
            titleBackdrop.maxY =
                settingsWindowPosition.y + settingsTitleHeight - 0.5f;
            titleBackdrop.rounding = style.FrameRounding;
            titleBackdrop.visible =
                titleBackdrop.maxX > titleBackdrop.minX &&
                titleBackdrop.maxY > titleBackdrop.minY;

            UiBackdropRect& bodyBackdrop =
                m_ui.BackdropRects[1];
            bodyBackdrop.minX = settingsWindowPosition.x + 0.5f;
            bodyBackdrop.minY =
                settingsWindowPosition.y + settingsTitleHeight - 1.f;
            bodyBackdrop.maxX =
                settingsWindowPosition.x + settingsWindowSize.x - 0.5f;
            bodyBackdrop.maxY =
                settingsWindowPosition.y + settingsWindowSize.y - 0.5f;
            bodyBackdrop.rounding = style.WindowRounding;
            bodyBackdrop.visible =
                bodyBackdrop.maxX > bodyBackdrop.minX &&
                bodyBackdrop.maxY > bodyBackdrop.minY;
        }
        constexpr size_t settingsBackdropCount = 2u;
        for (size_t backdropIndex = 0u;
            backdropIndex < settingsBackdropCount;
            ++backdropIndex)
        {
            UiBackdropRect& backdrop =
                m_ui.BackdropRects[backdropIndex];
            ApplyBackdropAppearance(
                backdrop,
                settingsWindowCenter,
                settingsAppearanceScale,
                settingsAppearanceOpacity);
            backdrop.shadowBlur =
                g_UiVisualTokens.backdropShadowBlur;
            backdrop.shadowOpacity =
                g_UiVisualTokens.backdropShadowOpacity;
            backdrop.shadowOffsetY =
                g_UiVisualTokens.backdropShadowOffsetY;
        }
        ImGui::End();
        ApplyWindowAppearance(
            settingsWindowDrawList,
            settingsWindowCenter,
            settingsAppearanceScale,
            settingsAppearanceOpacity);
        for (ImDrawList* drawList :
            g_SettingsAppearanceDrawLists)
        {
            ApplyWindowAppearance(
                drawList,
                settingsWindowCenter,
                settingsAppearanceScale,
                settingsAppearanceOpacity);
        }
        ImGui::PopStyleColor(4);

        DrawMaterialInspector(
            width,
            height,
            uiMotionEnabled,
            style);

        // Commit only after every UI window has finished composing. Any
        // synchronous renderer work then holds a previously presented stable
        // frame instead of interrupting popup, drawer, scroll, Settings, or
        // magnifier motion.
        FinishUnsubmittedDeferredDropdownPopupTransition();
        TryApplyDeferredDropdownUiActions(
            deferredDropdownCompositionIdle(
                settingsLayoutIdle,
                settingsScrollIdle),
            !uiMotionEnabled);
        RestoreActiveUiWordSpacing();
        ImGui::PopFont();
    }
};

bool ProcessCommandLine(
    int argc,
    const char* const* argv,
    DeviceCreationParameters& deviceParams,
    std::string& sceneName)
{
    for (int index = 1; index < argc; ++index)
    {
        const char* argument = argv[index];
        const auto readInteger =
            [&](const char* option, int minimum, auto& value)
            {
                int parsedValue = 0;
                if (index + 1 >= argc ||
                    !ParseCommandLineInt(
                        argv[index + 1],
                        minimum,
                        std::numeric_limits<int>::max(),
                        parsedValue))
                {
                    log::error(
                        "%s requires an exact integer of at least %d",
                        option,
                        minimum);
                    return false;
                }
                value = static_cast<
                    std::decay_t<decltype(value)>>(parsedValue);
                ++index;
                return true;
            };

        if (!std::strcmp(argument, "-width"))
        {
            if (!readInteger(
                    argument, 1, deviceParams.backBufferWidth))
                return false;
        }
        else if (!std::strcmp(argument, "-height"))
        {
            if (!readInteger(
                    argument, 1, deviceParams.backBufferHeight))
                return false;
        }
        else if (!std::strcmp(argument, "-fullscreen"))
        {
            deviceParams.startFullscreen = true;
        }
        else if (!std::strcmp(argument, "-debug"))
        {
            deviceParams.enableDebugRuntime = true;
            deviceParams.enableNvrhiValidationLayer = true;
        }
        else if (!std::strcmp(argument, "-adapter"))
        {
            if (!readInteger(argument, 0, deviceParams.adapterIndex))
                return false;
        }
        else if (argument[0] != '-')
        {
            sceneName = argument;
        }
        else
        {
            log::error(
                "Unknown command-line option '%s'",
                argument);
            return false;
        }
    }
    return true;
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
            adapter.dedicatedVideoMemory,
            adapterDescription.VendorId,
            adapterDescription.DeviceId
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
        "Selected graphics adapter %d: %s "
        "(PCI %04X:%04X, %llu MiB dedicated VRAM)",
        selectedChoice->adapterIndex,
        selectedChoice->name.c_str(),
        selectedChoice->vendorId,
        selectedChoice->deviceId,
        static_cast<unsigned long long>(selectedChoice->dedicatedVideoMemory / (1024ull * 1024ull)));
    return true;
}

void CenterWindowInMonitorWorkArea(GLFWwindow* window)
{
    if (!window)
        return;

    HWND nativeWindow = glfwGetWin32Window(window);
    if (!nativeWindow)
        return;

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!GetMonitorInfoW(
            MonitorFromWindow(
                nativeWindow,
                MONITOR_DEFAULTTONEAREST),
            &monitorInfo))
    {
        return;
    }

    int clientWidth = 0;
    int clientHeight = 0;
    glfwGetWindowSize(window, &clientWidth, &clientHeight);
    RECT nativeRect{};
    RECT visibleRect{};
    if (!GetWindowRect(nativeWindow, &nativeRect))
        return;
    if (FAILED(DwmGetWindowAttribute(
            nativeWindow,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &visibleRect,
            sizeof(visibleRect))))
    {
        visibleRect = nativeRect;
    }

    const auto alignDownToEight = [](int value)
    {
        return std::max(8, value & ~7);
    };
    const int alignedClientWidth =
        alignDownToEight(clientWidth);
    const int alignedClientHeight =
        alignDownToEight(clientHeight);
    if (clientWidth != alignedClientWidth ||
        clientHeight != alignedClientHeight)
    {
        clientWidth = alignedClientWidth;
        clientHeight = alignedClientHeight;
        glfwSetWindowSize(window, clientWidth, clientHeight);
    }

    // Re-read both rectangles after any client alignment, then move only the
    // native window. Centering the DWM-visible frame in rcWork balances the
    // top gap against the taskbar-side gap without changing 1920 x 1080.
    if (!GetWindowRect(nativeWindow, &nativeRect))
        return;
    if (FAILED(DwmGetWindowAttribute(
            nativeWindow,
            DWMWA_EXTENDED_FRAME_BOUNDS,
            &visibleRect,
            sizeof(visibleRect))))
    {
        visibleRect = nativeRect;
    }
    const int workWidth =
        monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    const int workHeight =
        monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    const int visibleWidth = visibleRect.right - visibleRect.left;
    const int visibleHeight = visibleRect.bottom - visibleRect.top;
    const int targetVisibleLeft =
        monitorInfo.rcWork.left + (workWidth - visibleWidth) / 2;
    const int targetVisibleTop =
        monitorInfo.rcWork.top + (workHeight - visibleHeight) / 2;
    const int nativeLeft =
        targetVisibleLeft - (visibleRect.left - nativeRect.left);
    const int nativeTop =
        targetVisibleTop - (visibleRect.top - nativeRect.top);
    SetWindowPos(
        nativeWindow,
        nullptr,
        nativeLeft,
        nativeTop,
        0,
        0,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER |
            SWP_NOSIZE);
}

int WINAPI WinMain(
    HINSTANCE,
    HINSTANCE,
    LPSTR,
    int)
{
    ApplyProcessPriority();
    constexpr nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::D3D12;

    DeviceCreationParameters deviceParams;
    deviceParams.backBufferWidth = 1920;
    deviceParams.backBufferHeight = 1080;
    deviceParams.swapChainSampleCount = 1;
    deviceParams.swapChainBufferCount = 3;
    deviceParams.startFullscreen = false;
    deviceParams.enablePerMonitorDPI = true;
    deviceParams.supportExplicitDisplayScaling = true;
    deviceParams.vsyncEnabled = false;

    std::string sceneName;
    if (!ProcessCommandLine(
            __argc,
            __argv,
            deviceParams,
            sceneName))
    {
        return 1;
    }

    DeviceManager* deviceManager = DeviceManager::Create(api);
    std::vector<GpuAdapterChoice> adapterChoices;
    if (!SelectGraphicsAdapter(
            deviceManager,
            deviceParams,
            adapterChoices))
    {
        delete deviceManager;
        return 1;
    }

    const char* apiName = nvrhi::utils::GraphicsAPIToString(
        deviceManager->GetGraphicsAPI());
    const std::string windowTitle =
        "UVSR Renderer " + std::string(apiName) +
        " (" + std::string(UVSR_GIT_COMMIT) + ")";
    if (!deviceManager->CreateWindowDeviceAndSwapChain(
            deviceParams,
            windowTitle.c_str()))
    {
        log::error(
            "Cannot initialize a %s graphics device",
            apiName);
        delete deviceManager;
        return 1;
    }

    if (!deviceParams.startFullscreen &&
        !deviceParams.startMaximized)
    {
        CenterWindowInMonitorWorkArea(
            deviceManager->GetWindow());
    }

    {
        UIData uiData;
        uiData.GpuAdapterChoices = std::move(adapterChoices);
        uiData.ActiveGpuAdapterIndex = deviceParams.adapterIndex;

        auto demo = std::make_shared<UvsrSceneViewer>(
            deviceManager,
            uiData,
            sceneName);
        auto gui = std::make_shared<UIRenderer>(
            deviceManager,
            demo,
            uiData);
        if (!gui->Init(demo->GetShaderFactory()))
        {
            deviceManager->Shutdown();
            delete deviceManager;
            return 1;
        }

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
