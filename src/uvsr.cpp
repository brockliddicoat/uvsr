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
#include <charconv>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <limits>
#include <string_view>
#include <system_error>
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
#include <directx/d3d12.h>

#include "pbr_material.h"
#include "pbr_deferred_lighting_pass.h"
#include "msaa_visibility_resolve.h"
#include "gpu_performance_monitor.h"
#include "camera_collision.h"
#include "camera_controllers.h"
#include "cmaa2.h"
#include "experiment_title.h"
#include "scene_catalog.h"
#include "screen_space_visibility.h"
#include "smaa.h"
#include "sponza_camera_preset.h"
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
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
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

struct AaBenchmarkConfig
{
    bool enabled = false;
    std::filesystem::path outputPath;
    AntiAliasingSettings settings;
    float sharpness = 0.f;
};

enum class AaBenchmarkSegment : uint32_t
{
    Warm,
    TurnRight,
    HoldRight,
    TurnBack,
    Drain
};

static constexpr uint32_t AaBenchmarkWarmFrames = 180u;
static constexpr uint32_t AaBenchmarkTurnFrames = 120u;
static constexpr uint32_t AaBenchmarkHoldFrames = 16u;
static constexpr uint32_t AaBenchmarkTargetFramesPerSecond = 40u;
static constexpr float AaBenchmarkTurnDegreesPerSecond =
    0.375f * float(AaBenchmarkTargetFramesPerSecond);
static constexpr uint32_t AaBenchmarkMotionEndFrame =
    AaBenchmarkWarmFrames +
    AaBenchmarkTurnFrames +
    AaBenchmarkHoldFrames +
    AaBenchmarkTurnFrames;
static constexpr uint64_t AaBenchmarkTimingTagFlag = 1ull << 63u;
static constexpr uint64_t AaBenchmarkTimingTagPayloadMask =
    ~(AaBenchmarkTimingTagFlag | SmaaTimingCollectTagFlag);

static AaBenchmarkSegment GetAaBenchmarkSegment(uint64_t sourceFrame)
{
    if (sourceFrame < AaBenchmarkWarmFrames)
        return AaBenchmarkSegment::Warm;
    if (sourceFrame <
        AaBenchmarkWarmFrames + AaBenchmarkTurnFrames)
    {
        return AaBenchmarkSegment::TurnRight;
    }
    if (sourceFrame <
        AaBenchmarkWarmFrames +
        AaBenchmarkTurnFrames +
        AaBenchmarkHoldFrames)
    {
        return AaBenchmarkSegment::HoldRight;
    }
    if (sourceFrame < AaBenchmarkMotionEndFrame)
        return AaBenchmarkSegment::TurnBack;
    return AaBenchmarkSegment::Drain;
}

static bool IsAaBenchmarkMeasurementFrame(uint64_t sourceFrame)
{
    const AaBenchmarkSegment segment =
        GetAaBenchmarkSegment(sourceFrame);
    return segment == AaBenchmarkSegment::TurnRight ||
        segment == AaBenchmarkSegment::HoldRight ||
        segment == AaBenchmarkSegment::TurnBack;
}

struct AaBenchmarkTimerTag
{
    uint32_t sourceFrame = 0u;
    uint32_t phase = 0u;
    AaBenchmarkSegment segment = AaBenchmarkSegment::Warm;
    bool collect = false;
};

struct AaBenchmarkSample
{
    float milliseconds = 0.f;
    uint32_t sourceFrame = 0u;
    uint32_t phase = 0u;
    AaBenchmarkSegment segment = AaBenchmarkSegment::Warm;
};

struct SmaaBenchmarkSample
{
    SmaaTimings timings;
    AaBenchmarkSegment segment = AaBenchmarkSegment::Warm;
};

struct AaBenchmarkStatistics
{
    float median = 0.f;
    float worst = 0.f;
    size_t count = 0u;
};

static AaBenchmarkStatistics CalculateAaBenchmarkStatistics(
    const std::vector<float>& samples)
{
    std::vector<float> ordered = samples;
    std::sort(ordered.begin(), ordered.end());
    AaBenchmarkStatistics result;
    result.count = ordered.size();
    if (ordered.empty())
        return result;

    const size_t middle = ordered.size() / 2u;
    result.median = ordered.size() % 2u
        ? ordered[middle]
        : 0.5f * (ordered[middle - 1u] + ordered[middle]);
    result.worst = ordered.back();
    return result;
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

    const auto supportsAllocatedFormats =
        [&](uint32_t sampleCount)
    {
        // RenderTargets still allocates the deferred attachments while MSAA
        // routes drawing through forward PBR. Check every format that receives
        // the selected sample count rather than only the HDR attachment.
        constexpr DXGI_FORMAT colorFormats[] = {
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R16G16B16A16_SNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16_FLOAT,
            DXGI_FORMAT_R8_UNORM
        };
        for (DXGI_FORMAT format : colorFormats)
        {
            if (!supportsFormat(format, sampleCount))
                return false;
        }

        constexpr DXGI_FORMAT depthFormats[] = {
            DXGI_FORMAT_D24_UNORM_S8_UINT,
            DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
            DXGI_FORMAT_D32_FLOAT,
            DXGI_FORMAT_D16_UNORM
        };
        return std::any_of(
            std::begin(depthFormats),
            std::end(depthFormats),
            [&](DXGI_FORMAT format)
            {
                return supportsFormat(format, sampleCount);
            });
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
            return candidate;
        }
    }

    log::warning(
        "Hardware MSAA is unsupported by the active adapter for UVSR's "
        "render-target formats; presenting one sample");
    return 1u;
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

        // The render targets below this point are non-MSAA
        desc.format = nvrhi::Format::SRGBA8_UNORM;
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
        
        ForwardFramebuffer = std::make_shared<FramebufferFactory>(device);
        ForwardFramebuffer->RenderTargets = { HdrColor };
        ForwardFramebuffer->DepthTarget = Depth;

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

#if 0
// The visibility branch forked before the canonical tonemapper/LUT sunset.
// Keep its stale helper block out of the composed product; the destination's
// fixed AgX path and newest UI remain authoritative.
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

#endif

static uint32_t GetVisibilityLaterBounceSampleCount(
    uint32_t firstBounceSampleCount)
{
    return std::clamp(firstBounceSampleCount, 1u, 64u);
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

    workload.firstBounceSampleCount = std::clamp(
        visibility.sampling.maximumSampleCount, 1u, 64u);
    workload.bounceCount = indirectEnabled
        ? (visibility.indirectDiffuse.limitBounces
            ? std::clamp(visibility.indirectDiffuse.bounceCount,
                1u, MaxIndirectDiffuseBounceCount)
            : MaxContributionTerminatedBounceCount)
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
    workload.temporalEnabled = visibility.reconstruction.temporalEnabled;
    workload.spatialEnabled = visibility.reconstruction.spatialEnabled;
    workload.depthHierarchyEnabled = ambientEnabled && !indirectEnabled &&
        visibility.sampling.radius >= 8.f;
    workload.runtimeConfigurationKey =
        GetVisibilityRuntimeConfigurationKey(visibility);
    return workload;
}

static VisibilityPerformanceWorkload GetRenderedVisibilityPerformanceWorkload(
    const ScreenSpaceVisibilitySettings& visibility,
    uint32_t outputWidth,
    uint32_t outputHeight,
    const ScreenSpaceVisibilityTimings* timings)
{
    if (timings && timings->hasActiveWorkload &&
        timings->activeWorkload.outputWidth == outputWidth &&
        timings->activeWorkload.outputHeight == outputHeight)
    {
        return timings->activeWorkload;
    }

    return BuildVisibilityPerformanceWorkload(
        visibility, outputWidth, outputHeight);
}

static void SetCanonicalVisibilityBenchmarkDefaults(
    ScreenSpaceVisibilitySettings& visibility)
{
    visibility.enabled = true;
    visibility.quality = ScreenSpaceVisibilityQuality::Custom;
    visibility.estimator = VisibilityEstimator::UniformSolidAngle;
    visibility.resolution = VisibilityResolution::Half;
    visibility.sampling.maximumSampleCount = 8u;
    visibility.sampling.radius = 3.f;
    visibility.sampling.thickness = 0.5f;
    visibility.sampling.stepDistributionExponent = 2.f;
    visibility.sampling.scheduler =
        VisibilitySampleScheduler::ToroidalBlueNoiseRankField;
    visibility.ambientOcclusion.enabled = true;
    visibility.ambientOcclusion.strength = 1.f;
    visibility.ambientOcclusion.power = 1.f;
    visibility.indirectDiffuse.enabled = false;
    visibility.indirectDiffuse.limitBounces = true;
    visibility.indirectDiffuse.bounceCount = 1u;
    visibility.indirectDiffuse.minimumBounceContribution = 0.001f;
    visibility.indirectDiffuse.intensity = 4.f;
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
    case VisibilitySampleSpecialization::Fixed20: return 20u;
    case VisibilitySampleSpecialization::Fixed24: return 24u;
    case VisibilitySampleSpecialization::Fixed48: return 48u;
    case VisibilitySampleSpecialization::Fixed64: return 64u;
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
    visibility.sampling.maximumSampleCount =
        workload.firstBounceSampleCount;
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
    ResetVisibilityComposableSettings(
        visibility, definition.implementationProfile);
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
    ResetVisibilityComposableSettings(visibility, profile);

    uint32_t firstBounceSampleCount = GetVisibilityFixedSampleCount(
        configuration.firstBounceSamples);
    if (firstBounceSampleCount == 0u)
        firstBounceSampleCount = 8u;
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
    visibility.reconstruction.temporalEnabled = false;
    visibility.reconstruction.spatialEnabled = false;
    return true;
}

static std::string_view GetVisibilityPerformanceProfileDisplayName(
    VisibilityPerformanceProfile profile)
{
    return GetVisibilityPerformanceProfileConfiguration(profile).name;
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
    uint32_t warmupFrameCount = 120u;
    uint32_t measuredFrameCount = 240u;
    std::filesystem::path outputDirectory;
    bool autoClose = false;
    bool contributionTerminatedBounces = false;
};

struct UIData
{
    bool                                ShowUI = true;
    std::array<UiBackdropRect, 2>        BackdropRects;
    std::vector<GpuAdapterChoice>       GpuAdapterChoices;
    int                                 ActiveGpuAdapterIndex = -1;
    bool                                EnablePbr = true;
    RendererMode                        RenderMode = RendererMode::Deferred;
    AntiAliasingSettings                AntiAliasing;
    bool                                MiniEngineTaaSharpenEnabled = true;
    float                               MiniEngineTaaSharpness =
        MiniEngineTaaDefaultSharpness;
    MiniEngineTaaDebugView              MiniEngineTaaVisualization =
        MiniEngineTaaDebugView::Off;
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
    VisibilityVerificationProfile       VisibilityVerification =
        VisibilityVerificationProfile::Unset;

    [[nodiscard]] bool UsesHardwareMsaa() const
    {
        if (!AntiAliasing.enabled)
            return false;
        const AntiAliasingPreset implementation =
            GetAntiAliasingImplementation(
                AntiAliasing.method,
                SanitizeAntiAliasingQuality(
                    AntiAliasing.method,
                    AntiAliasing.quality));
        return implementation == AntiAliasingPreset::Msaa2x ||
            implementation == AntiAliasingPreset::Msaa4x ||
            implementation == AntiAliasingPreset::Msaa8x ||
            implementation == AntiAliasingPreset::Msaa16x;
    }

    [[nodiscard]] bool UsesDeferredShading() const
    {
        return RenderMode == RendererMode::Deferred &&
            (!UsesHardwareMsaa() || EnablePbr);
    }

    [[nodiscard]] bool IsScreenSpaceVisibilityAvailable() const
    {
        // Deferred MSAA supplies visibility with one coherent closest-surface
        // owner per pixel, then coverage-weights the visibility correction
        // back into its per-sample lighting resolve.
        return UsesDeferredShading();
    }

    [[nodiscard]] bool HasMiniEngineTaaVisibilityConflict() const
    {
        // These visibility histories do not yet receive TAA's subpixel jitter delta.
        return ScreenSpaceVisibility.reconstruction.temporalEnabled;
    }

    [[nodiscard]] bool IsTemporalAntiAliasingAvailable() const
    {
        return IsMiniEngineTaaAvailable(
            true,
            EnablePbr,
            RenderMode == RendererMode::Deferred,
            ScreenSpaceVisibility.reconstruction.temporalEnabled);
    }

    [[nodiscard]] ResolvedAntiAliasingSettings
        GetResolvedAntiAliasingSettings(
            const AntiAliasingSettings& settings) const
    {
        ResolvedAntiAliasingSettings result =
            ResolveCompiledAntiAliasingSettings(settings);

        const auto activeAdapter = std::find_if(
            GpuAdapterChoices.begin(),
            GpuAdapterChoices.end(),
            [this](const GpuAdapterChoice& choice)
            {
                return choice.adapterIndex ==
                    ActiveGpuAdapterIndex;
            });

        // Adapter-specific Auto entries are deliberately narrow and are only
        // added after paired warm-camera measurements preserve the image and
        // improve both the normal path and the camera-turn path. The Intel
        // Core Ultra 9 185H Arc iGPU (8086:7D55) consistently benefits from
        // sharing adjacent MiniEngine pixels' overlapping neighborhoods.
        // Split/packed LDS, MiniEngine fusion, early rejection, and cache
        // blocking remain explicit experiments because their measured gains
        // were not repeatable.
#if UVSR_AA_DEVELOPER_OVERRIDES
        const bool sharedReuseAutoRequested =
            settings.performanceOverrides.sharedWorkReuse ==
                MiniEngineTaaAutoToggle::Auto;
#else
        // Hidden override state is sanitized from production. The validated
        // adapter table therefore remains authoritative there.
        constexpr bool sharedReuseAutoRequested = true;
#endif
        const bool intelCoreUltra185h =
            activeAdapter != GpuAdapterChoices.end() &&
            activeAdapter->vendorId == 0x8086u &&
            activeAdapter->deviceId == 0x7D55u;
        const AntiAliasingPreset implementation =
            GetAntiAliasingImplementation(
                settings.method,
                settings.quality);
        if (intelCoreUltra185h &&
            settings.enabled &&
            IsLongTermTemporalPreset(
                implementation) &&
            !UsesSampleResurrection(result.sampleResurrection) &&
            sharedReuseAutoRequested)
        {
            result.sharedWorkReuse = true;
        }
        return result;
    }

    [[nodiscard]] ResolvedAntiAliasingSettings
        GetResolvedAntiAliasingSettings() const
    {
        return GetResolvedAntiAliasingSettings(AntiAliasing);
    }

    [[nodiscard]] bool UsesLongTermTemporalAA() const
    {
        return AntiAliasing.enabled &&
            IsLongTermTemporalPreset(
                GetAntiAliasingImplementation(
                    AntiAliasing.method,
                    AntiAliasing.quality)) &&
            IsTemporalAntiAliasingAvailable();
    }

    [[nodiscard]] bool UsesJitteredAntiAliasing() const
    {
        return UsesLongTermTemporalAA();
    }

    [[nodiscard]] bool UsesSmaaPass() const
    {
        if (!AntiAliasing.enabled)
            return false;
        const ResolvedAntiAliasingSettings resolved =
            GetResolvedAntiAliasingSettings();
        const AntiAliasingPreset implementation =
            resolved.implementation;
        if (implementation == AntiAliasingPreset::Smaa1x)
            return true;
        // Keep presentation SMAA resources alive across Temporal morphology
        // changes so switching a presentation-only filter cannot recreate
        // the temporal pass or discard its history.
        if (UsesLongTermTemporalAA())
            return true;
        return resolved.subpixelMorphology ==
                SmaaApplication::FullScreen ||
            resolved.subpixelMorphology ==
                SmaaApplication::SelectiveShort ||
            resolved.subpixelMorphology ==
                SmaaApplication::SelectiveLong;
    }

    [[nodiscard]] bool UsesCmaa2() const
    {
        return AntiAliasing.enabled &&
            GetResolvedAntiAliasingSettings()
                    .subpixelMorphology ==
                SmaaApplication::ConservativeMorphological;
    }

    [[nodiscard]] bool RequiresAntiAliasingMotionVectors() const
    {
        return UsesLongTermTemporalAA();
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
    float ambientOcclusionPower = 0.f;
    bool indirectDiffuseEnabled = false;
    bool indirectDiffuseLimitBounces = true;
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
    uint32_t maximumSampleCount = 0u;
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
    std::string rawGiFormat;
    std::string cumulativeGiFormat;
    std::string temporalGiFormat;
    std::string edgeMetadataFormat;
    std::string finalResolvedAoFormat;
    std::string finalResolvedGiFormat;
    std::string depthHierarchyFormat;
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

static const char* DescribeVisibilityOptimizationClass(
    VisibilityOptimizationClass value)
{
    switch (value)
    {
    case VisibilityOptimizationClass::Reference: return "Reference";
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
        return "Independent Hash Noise";
    case VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField:
        return "Toroidal Blue Noise";
    case VisibilityPerformanceScheduler::
            FilterAdaptedSpatiotemporalRankField:
        return "Offline Spacetime Noise";
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
    case VisibilitySampleSpecialization::Fixed20: return "Fixed 20";
    case VisibilitySampleSpecialization::Fixed24: return "Fixed 24";
    case VisibilitySampleSpecialization::Fixed48: return "Fixed 48";
    case VisibilitySampleSpecialization::Fixed64: return "Fixed 64";
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
        return "Offline Packed Spacetime Noise";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityMath(VisibilityMathMode value)
{
    switch (value)
    {
    case VisibilityMathMode::ReferenceFp32: return "Reference FP32";
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
    case VisibilityReconstructionMode::PackedEdges2x2:
        return "Edge-Guided Reconstruction";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityTemporal(VisibilityTemporalMode value)
{
    switch (value)
    {
    case VisibilityTemporalMode::Legacy: return "Legacy Temporal";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityApplication(
    VisibilityApplicationMode value)
{
    switch (value)
    {
    case VisibilityApplicationMode::LegacySeparateComposition:
        return "Separate Composition";
    case VisibilityApplicationMode::FusedResolveAndApplyExact:
        return "Fused Apply";
    case VisibilityApplicationMode::FusedResolveAndApplyPackedEdges:
        return "Fused Edge Apply";
    default: return "Unknown";
    }
}

static const char* DescribeVisibilityDepth(VisibilityDepthMode value)
{
    switch (value)
    {
    case VisibilityDepthMode::Legacy: return "Direct Device Depth";
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

static const char* DescribeVisibilityScalarPrecision(
    VisibilityScalarBufferPrecision value)
{
    return value == VisibilityScalarBufferPrecision::Float32
        ? "R32_FLOAT" : "R16_FLOAT";
}

static const char* DescribeVisibilityVectorPrecision(
    VisibilityVectorBufferPrecision value)
{
    return value == VisibilityVectorBufferPrecision::Rgba32Float
        ? "RGBA32_FLOAT" : "RGBA16_FLOAT";
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
    result.implementationProfile.assign(configuration.name);
    result.optimizationClass = DescribeVisibilityOptimizationClass(
        configuration.optimizationClass);
    result.consumer = DescribeVisibilityConsumer(workload);
    result.visibilityEnabled = visibility.enabled;
    result.ambientOcclusionEnabled = visibility.ambientOcclusion.enabled;
    result.ambientOcclusionStrength = visibility.ambientOcclusion.strength;
    result.ambientOcclusionPower = visibility.ambientOcclusion.power;
    result.indirectDiffuseEnabled = visibility.indirectDiffuse.enabled;
    result.indirectDiffuseLimitBounces =
        visibility.indirectDiffuse.limitBounces;
    result.indirectDiffuseBounceCount = workload.bounceCount;
    result.indirectDiffuseMinimumBounceContribution =
        visibility.indirectDiffuse.minimumBounceContribution;
    result.indirectDiffuseIntensity = visibility.indirectDiffuse.intensity;
    result.indirectDiffuseIncludeEmissive = true;
    result.indirectDiffuseEmissiveGain = VisibilityEmissiveSourceGain;
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
    result.maximumSampleCount = visibility.sampling.maximumSampleCount;
    result.radius = visibility.sampling.radius;
    result.thickness = visibility.sampling.thickness;
    result.thicknessActive = true;
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
    result.precisionProfile = "Reference FP32";
    const VisibilityBufferPrecisionSettings& bufferPrecision =
        visibility.performance.bufferPrecision;
    result.temporalEnabled = visibility.reconstruction.temporalEnabled;
    result.temporalMode = DescribeVisibilityTemporal(configuration.temporal);
    result.temporalResponse = visibility.reconstruction.temporalResponse;
    result.temporalAoFormat = result.temporalEnabled
        ? DescribeVisibilityScalarPrecision(
            bufferPrecision.temporalAmbient) : "Not Allocated";
    result.temporalGiFormat =
        result.temporalEnabled && visibility.indirectDiffuse.enabled
        ? DescribeVisibilityVectorPrecision(
            bufferPrecision.temporalIndirect) : "Not Allocated";
    result.temporalDepthFormat = result.temporalEnabled
        ? DescribeVisibilityScalarPrecision(
            bufferPrecision.temporalDepth) : "Not Allocated";
    result.temporalNormalFormat = result.temporalEnabled
        ? "RGBA8_UNORM" : "Not Allocated";
    result.spatialEnabled = visibility.reconstruction.spatialEnabled;
    result.spatialFilter = DescribeVisibilitySpatialFilter(
        visibility.reconstruction.spatialFilter);
    result.spatialRadius = visibility.reconstruction.spatialRadius;
    result.spatialRadiusActive = result.spatialEnabled;
    result.reconstruction = DescribeVisibilityReconstruction(
        configuration.reconstruction);
    result.application = DescribeVisibilityApplication(
        configuration.application);
    result.threadGroupSizeX = workload.threadGroupSizeX;
    result.threadGroupSizeY = workload.threadGroupSizeY;
    result.rawAoFormat =
        DescribeVisibilityScalarPrecision(bufferPrecision.rawAmbient);
    result.rawGiFormat = DescribeVisibilityVectorPrecision(
        bufferPrecision.rawIndirect);
    result.cumulativeGiFormat = DescribeVisibilityVectorPrecision(
        bufferPrecision.cumulativeIndirect);
    result.edgeMetadataFormat = DescribeVisibilityEdgeFormat(
        configuration.edgeStorage);
    result.finalResolvedAoFormat =
        configuration.application ==
                VisibilityApplicationMode::FusedResolveAndApplyExact ||
            configuration.application ==
                VisibilityApplicationMode::FusedResolveAndApplyPackedEdges
        ? "No Intermediate"
        : DescribeVisibilityScalarPrecision(
            bufferPrecision.finalAmbient);
    result.finalResolvedGiFormat = DescribeVisibilityVectorPrecision(
        bufferPrecision.finalIndirect);
    result.depthHierarchyFormat = DescribeVisibilityScalarPrecision(
        bufferPrecision.depthHierarchy);
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
        << ";ao_power=" << result.ambientOcclusionPower
        << ";gi_enabled=" << result.indirectDiffuseEnabled
        << ";gi_limit_bounces=" << result.indirectDiffuseLimitBounces
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
        << ";maximum_samples=" << result.maximumSampleCount
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
        << ";temporal_gi_format=" << result.temporalGiFormat
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
        << ";raw_gi_format=" << result.rawGiFormat
        << ";cumulative_gi_format=" << result.cumulativeGiFormat
        << ";edge_metadata_format=" << result.edgeMetadataFormat
        << ";final_resolved_ao_format=" << result.finalResolvedAoFormat
        << ";final_resolved_gi_format=" << result.finalResolvedGiFormat
        << ";depth_hierarchy_format=" << result.depthHierarchyFormat
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
            observed.sampling.maximumSampleCount !=
                expected.sampling.maximumSampleCount,
            "Maximum sample count does not match the profile.")).empty())
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
            observed.ambientOcclusion.power !=
                expected.ambientOcclusion.power,
            "AO power does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.indirectDiffuse.enabled !=
                expected.indirectDiffuse.enabled,
            "GI enabled state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.indirectDiffuse.limitBounces !=
                expected.indirectDiffuse.limitBounces,
            "GI bounce-limit mode does not match the profile.")).empty())
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
        << ",\"ao_power\":" << settings.ambientOcclusionPower
        << ",\"gi_enabled\":"
        << (settings.indirectDiffuseEnabled ? "true" : "false")
        << ",\"gi_limit_bounces\":"
        << (settings.indirectDiffuseLimitBounces ? "true" : "false")
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
    output << ",\"sample_count\":" << settings.maximumSampleCount
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
    output << ",\"raw_gi\":";
    WriteJsonString(output, settings.rawGiFormat);
    output << ",\"cumulative_gi\":";
    WriteJsonString(output, settings.cumulativeGiFormat);
    output << ",\"temporal_ao\":";
    WriteJsonString(output, settings.temporalAoFormat);
    output << ",\"temporal_gi\":";
    WriteJsonString(output, settings.temporalGiFormat);
    output << ",\"temporal_depth\":";
    WriteJsonString(output, settings.temporalDepthFormat);
    output << ",\"temporal_normal\":";
    WriteJsonString(output, settings.temporalNormalFormat);
    output << ",\"edge_metadata\":";
    WriteJsonString(output, settings.edgeMetadataFormat);
    output << ",\"final_resolved_ao\":";
    WriteJsonString(output, settings.finalResolvedAoFormat);
    output << ",\"final_resolved_gi\":";
    WriteJsonString(output, settings.finalResolvedGiFormat);
    output << ",\"depth_hierarchy\":";
    WriteJsonString(output, settings.depthHierarchyFormat);
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

enum class RendererTimingStage : uint32_t
{
    CompleteFrame,
    SceneSetup,
    Geometry,
    DirectLighting,
    ScreenSpaceVisibility,
    MaterialPicking,
    ProceduralSky,
    ToneMapping,
    OutputBlit,
    Count
};

struct RendererTimings
{
    std::array<float, static_cast<size_t>(RendererTimingStage::Count)>
        milliseconds{};

    [[nodiscard]] float Get(RendererTimingStage stage) const
    {
        return milliseconds[static_cast<size_t>(stage)];
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
    std::unique_ptr<MsaaVisibilityResolvePass>
        m_MsaaVisibilityResolvePass;
    std::unique_ptr<SkyPass>            m_SkyPass;
    std::unique_ptr<AgxToneMappingPass> m_AgxToneMappingPass;
    std::unique_ptr<ScreenSpaceVisibilityPass> m_ScreenSpaceVisibilityPass;
    std::unique_ptr<MiniEngineTemporalAAPass> m_MiniEngineTemporalAAPass;
    std::unique_ptr<SmaaPass>           m_SmaaPass;
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
    AaBenchmarkConfig                   m_AaBenchmark;
    static constexpr uint32_t           c_AaTimerLatency = 8u;
    std::array<nvrhi::TimerQueryHandle,
        c_AaTimerLatency>               m_AaTimerQueries;
    std::array<bool,
        c_AaTimerLatency>               m_AaTimerPending{};
    std::array<AaBenchmarkTimerTag,
        c_AaTimerLatency>               m_AaTimerTags{};
    uint32_t                            m_AaTimerFrame = 0u;
    float                               m_AaGpuMilliseconds = 0.f;
    uint32_t                            m_AaBenchmarkFrame = 0u;
    AaBenchmarkTimerTag                 m_AaBenchmarkCurrentTag;
    uint32_t                            m_AaBenchmarkIssuedSamples = 0u;
    uint32_t                            m_AaBenchmarkDroppedSamples = 0u;
    uint32_t                            m_AaBenchmarkOutstandingSamples = 0u;
    bool                                m_AaBenchmarkStarted = false;
    bool                                m_AaBenchmarkPacingActive = false;
    std::chrono::steady_clock::time_point
                                        m_AaBenchmarkNextFrameDeadline;
    bool                                m_InteractiveAaMotionTest = false;
    int                                 m_AaMotionTestPreviousWidth = 0;
    int                                 m_AaMotionTestPreviousHeight = 0;
    std::string                         m_AaMotionTestStatus =
        "Ready to test the current anti-aliasing configuration.";
    std::vector<AaBenchmarkSample>      m_AaBenchmarkSamples;
    std::vector<SmaaBenchmarkSample>    m_SmaaBenchmarkSamples;
    uint64_t                            m_AntiAliasingPhase = 0u;
    bool                                m_HasAppliedAntiAliasingSettings =
        false;
    AntiAliasingSettings                m_AppliedAntiAliasingSettings;
    bool                                m_VisibilityBenchmarkQueued = false;
    uint32_t                            m_VisibilityBenchmarkWarmup = 120u;
    uint32_t                            m_VisibilityBenchmarkFrames = 240u;
    uint64_t                            m_VisibilityBenchmarkRenderedFrames = 0u;
    std::filesystem::path               m_VisibilityBenchmarkOutputDirectory;
    bool                                m_VisibilityBenchmarkAutoClose = false;
    bool                                m_VisibilityBenchmarkOwnsCameraLock = false;
    CameraMode                          m_VisibilityBenchmarkPreviousCameraMode =
        CameraMode::ThirdPerson;
    int                                 m_VisibilityBenchmarkPreviousWindowWidth = 0;
    int                                 m_VisibilityBenchmarkPreviousWindowHeight = 0;
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
    bool                                m_SceneFinishedLoading = false;
    bool                                m_SponzaCameraLocationsAvailable = false;
    SponzaCameraLocation                m_SponzaCameraLocation =
        SponzaCameraLocation::SimplifiedApproximation;

    UIData&                             m_ui;

    std::string GetActiveAdapterName() const;
    void UpdateVisibilityBenchmarkAfterRender();
    void FailVisibilityBenchmark(const std::string& message);
    void ReleaseVisibilityBenchmarkCameraLock();
    void DiscardVisibilityBenchmarkMeasuredFrame();
    void CaptureCompletedVisibilityBenchmarkFrame(
        nvrhi::ITexture* framebufferTexture);
    void AdvanceRendererTimers();
    void BeginRendererStage(RendererTimingStage stage);
    void EndRendererStage(RendererTimingStage stage);
    void CompleteRendererTimerFrame();

public:

    bool ShouldAnimateUnfocused() override
    {
        return m_VisibilityBenchmarkQueued ||
            IsVisibilityBenchmarkActive();
    }

    bool ShouldRenderUnfocused() override
    {
        return m_VisibilityBenchmarkQueued ||
            IsVisibilityBenchmarkActive();
    }

    UvsrSceneViewer(
        DeviceManager* deviceManager,
        UIData& ui,
        const std::string& sceneName,
        bool benchmarkCameraRequested,
        const AaBenchmarkConfig& aaBenchmark)
        : Super(deviceManager)
        , m_BindingCache(deviceManager->GetDevice())
        , m_BenchmarkCameraRequested(benchmarkCameraRequested)
        , m_AaBenchmark(aaBenchmark)
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

        m_OpaqueDrawStrategy = std::make_shared<InstancedOpaqueDrawStrategy>();


        m_CommandList = GetDevice()->createCommandList();
        for (nvrhi::TimerQueryHandle& query : m_AaTimerQueries)
            query = GetDevice()->createTimerQuery();
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
    void CancelVisibilityBenchmark();
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

    [[nodiscard]] bool CanStartAntiAliasingMotionTest() const
    {
        return IsSceneLoaded() &&
            m_SponzaCameraLocationsAvailable &&
            !m_AaBenchmark.enabled &&
            !m_BenchmarkCameraRequested;
    }

    [[nodiscard]] bool IsAntiAliasingMotionTestRunning() const
    {
        return m_InteractiveAaMotionTest && m_AaBenchmark.enabled;
    }

    [[nodiscard]] std::string GetAntiAliasingMotionTestStatus() const
    {
        if (!IsAntiAliasingMotionTestRunning())
            return m_AaMotionTestStatus;

        if (!m_AaBenchmarkStarted)
            return "Preparing Benchmark Position 1 at 1920 x 1080...";

        std::ostringstream status;
        const uint32_t frame = m_AaBenchmarkFrame;
        const AaBenchmarkSegment segment =
            GetAaBenchmarkSegment(frame);
        switch (segment)
        {
        case AaBenchmarkSegment::Warm:
            status << "Warming history " <<
                std::min(frame, AaBenchmarkWarmFrames) <<
                " / " << AaBenchmarkWarmFrames;
            break;

        case AaBenchmarkSegment::TurnRight:
            status << "Turning right " <<
                (frame - AaBenchmarkWarmFrames) <<
                " / " << AaBenchmarkTurnFrames;
            break;

        case AaBenchmarkSegment::HoldRight:
            status << "Holding at +45 degrees " <<
                (frame - AaBenchmarkWarmFrames -
                    AaBenchmarkTurnFrames) <<
                " / " << AaBenchmarkHoldFrames;
            break;

        case AaBenchmarkSegment::TurnBack:
            status << "Turning back " <<
                (frame - AaBenchmarkWarmFrames -
                    AaBenchmarkTurnFrames -
                    AaBenchmarkHoldFrames) <<
                " / " << AaBenchmarkTurnFrames;
            break;

        case AaBenchmarkSegment::Drain:
            status << "Draining GPU timings ("
                << m_AaBenchmarkOutstandingSamples
                << " outstanding)";
            break;
        }
        return status.str();
    }

    bool StartAntiAliasingMotionTest()
    {
        if (!CanStartAntiAliasingMotionTest())
            return false;

        const SponzaCameraPreset& preset =
            GetDefaultSponzaCameraPreset();
        m_AaBenchmark = AaBenchmarkConfig{};
        m_AaBenchmark.enabled = true;
        m_AaBenchmark.outputPath =
            app::GetDirectoryWithExecutable() /
            "aa-motion-test-latest.json";
        m_AaBenchmark.settings = m_ui.AntiAliasing;
        m_AaBenchmark.sharpness = m_ui.MiniEngineTaaSharpness;
        m_AaBenchmarkStarted = false;
        m_AaBenchmarkPacingActive = false;
        m_AaBenchmarkCurrentTag = AaBenchmarkTimerTag{};
        m_InteractiveAaMotionTest = true;
        m_AaMotionTestStatus =
            "Preparing Benchmark Position 1 at 1920 x 1080...";

        GetDeviceManager()->GetWindowDimensions(
            m_AaMotionTestPreviousWidth,
            m_AaMotionTestPreviousHeight);
        ApplySponzaCameraPreset(preset);
        m_SponzaCameraLocation =
            SponzaCameraLocation::SimplifiedApproximation;
        m_BenchmarkCameraActive = true;
        m_ui.Camera = CameraMode::Static;

        GLFWwindow* window = GetDeviceManager()->GetWindow();
        glfwSetWindowSize(
            window,
            int(preset.ReferenceWidth),
            int(preset.ReferenceHeight));
        log::info(
            "Interactive AA motion test requested for '%s'; evidence will be written to %s",
            preset.Label,
            m_AaBenchmark.outputPath.generic_string().c_str());
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
        if (m_MiniEngineTemporalAAPass)
            m_MiniEngineTemporalAAPass->ResetHistory();
        m_AntiAliasingPhase = 0u;
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
        ResetAntiAliasingState();
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
        m_ui.AntiAliasing = AntiAliasingSettings{};
        m_ui.MiniEngineTaaSharpenEnabled = true;
        m_ui.MiniEngineTaaSharpness = MiniEngineTaaDefaultSharpness;
        m_ui.MiniEngineTaaVisualization = MiniEngineTaaDebugView::Off;
        m_ui.ScreenSpaceVisibility = ScreenSpaceVisibilitySettings{};
        m_ui.VisibilityVerification =
            VisibilityVerificationProfile::Unset;
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

    void CollectCompletedSmaaTimings()
    {
        if (!m_SmaaPass)
            return;

        SmaaTimings timing;
        while (m_SmaaPass->PopCompletedTiming(timing))
        {
            if (!m_AaBenchmark.enabled ||
                (timing.completedTag & AaBenchmarkTimingTagFlag) == 0u ||
                (timing.completedTag & SmaaTimingCollectTagFlag) == 0u)
            {
                continue;
            }

            const uint64_t sourceFrame =
                timing.completedTag & AaBenchmarkTimingTagPayloadMask;
            if (!IsAaBenchmarkMeasurementFrame(sourceFrame))
                continue;

            m_SmaaBenchmarkSamples.push_back({
                timing,
                GetAaBenchmarkSegment(sourceFrame)
            });
        }
    }

    void AdvanceAntiAliasingTimer()
    {
        // Poll every outstanding query. Waiting only for the current modulo
        // slot delayed some samples by another full latency cycle and made
        // drain-frame timings appear inside the motion interval.
        for (uint32_t slot = 0u; slot < c_AaTimerLatency; ++slot)
        {
            if (!m_AaTimerPending[slot])
                continue;

            nvrhi::ITimerQuery* query = m_AaTimerQueries[slot];
            if (!GetDevice()->pollTimerQuery(query))
                continue;

            m_AaGpuMilliseconds =
                GetDevice()->getTimerQueryTime(query) * 1000.f;
            GetDevice()->resetTimerQuery(query);
            m_AaTimerPending[slot] = false;

            const AaBenchmarkTimerTag tag = m_AaTimerTags[slot];
            if (m_AaBenchmark.enabled && tag.collect)
            {
                m_AaBenchmarkSamples.push_back({
                    m_AaGpuMilliseconds,
                    tag.sourceFrame,
                    tag.phase,
                    tag.segment
                });
                if (m_AaBenchmarkOutstandingSamples > 0u)
                    --m_AaBenchmarkOutstandingSamples;
            }
        }
    }

    bool BeginAntiAliasingTimer()
    {
        const uint32_t slot =
            m_AaTimerFrame % c_AaTimerLatency;
        if (m_AaTimerPending[slot])
        {
            if (m_AaBenchmark.enabled &&
                m_AaBenchmarkCurrentTag.collect)
            {
                ++m_AaBenchmarkDroppedSamples;
            }
            return false;
        }

        m_CommandList->beginTimerQuery(m_AaTimerQueries[slot]);
        m_AaTimerTags[slot] = m_AaBenchmarkCurrentTag;
        return true;
    }

    void EndAntiAliasingTimer(bool active)
    {
        const uint32_t slot =
            m_AaTimerFrame % c_AaTimerLatency;
        if (active)
        {
            m_CommandList->endTimerQuery(m_AaTimerQueries[slot]);
            m_AaTimerPending[slot] = true;
            if (m_AaBenchmark.enabled &&
                m_AaTimerTags[slot].collect)
            {
                ++m_AaBenchmarkIssuedSamples;
                ++m_AaBenchmarkOutstandingSamples;
            }
        }
        ++m_AaTimerFrame;
    }

    void FinishAntiAliasingBenchmark()
    {
        const auto collect = [this](
            const auto& predicate)
        {
            std::vector<float> result;
            result.reserve(m_AaBenchmarkSamples.size());
            for (const AaBenchmarkSample& sample : m_AaBenchmarkSamples)
            {
                if (predicate(sample))
                    result.push_back(sample.milliseconds);
            }
            return result;
        };
        const AaBenchmarkStatistics complete =
            CalculateAaBenchmarkStatistics(collect(
                [](const AaBenchmarkSample&) { return true; }));
        const AaBenchmarkStatistics phase0 =
            CalculateAaBenchmarkStatistics(collect(
                [](const AaBenchmarkSample& sample)
                {
                    return (sample.phase & 1u) == 0u;
                }));
        const AaBenchmarkStatistics phase1 =
            CalculateAaBenchmarkStatistics(collect(
                [](const AaBenchmarkSample& sample)
                {
                    return (sample.phase & 1u) != 0u;
                }));
        const auto segmentStatistics = [&](AaBenchmarkSegment segment)
        {
            return CalculateAaBenchmarkStatistics(collect(
                [segment](const AaBenchmarkSample& sample)
                {
                    return sample.segment == segment;
                }));
        };
        const AaBenchmarkStatistics turnRight =
            segmentStatistics(AaBenchmarkSegment::TurnRight);
        const AaBenchmarkStatistics holdRight =
            segmentStatistics(AaBenchmarkSegment::HoldRight);
        const AaBenchmarkStatistics turnBack =
            segmentStatistics(AaBenchmarkSegment::TurnBack);
        struct SmaaStageStatistics
        {
            AaBenchmarkStatistics edge;
            AaBenchmarkStatistics weight;
            AaBenchmarkStatistics neighborhood;
            AaBenchmarkStatistics complete;
        };
        const auto smaaStageStatistics = [this](
            const auto& predicate)
        {
            std::vector<float> edge;
            std::vector<float> weight;
            std::vector<float> neighborhood;
            std::vector<float> complete;
            edge.reserve(m_SmaaBenchmarkSamples.size());
            weight.reserve(m_SmaaBenchmarkSamples.size());
            neighborhood.reserve(m_SmaaBenchmarkSamples.size());
            complete.reserve(m_SmaaBenchmarkSamples.size());
            for (const SmaaBenchmarkSample& sample :
                m_SmaaBenchmarkSamples)
            {
                if (!predicate(sample))
                    continue;
                edge.push_back(sample.timings.edgeMilliseconds);
                weight.push_back(sample.timings.weightMilliseconds);
                neighborhood.push_back(
                    sample.timings.neighborhoodMilliseconds);
                complete.push_back(
                    sample.timings.CompleteEffectMilliseconds());
            }
            return SmaaStageStatistics{
                CalculateAaBenchmarkStatistics(edge),
                CalculateAaBenchmarkStatistics(weight),
                CalculateAaBenchmarkStatistics(neighborhood),
                CalculateAaBenchmarkStatistics(complete)
            };
        };
        const SmaaStageStatistics allSmaaStages =
            smaaStageStatistics(
                [](const SmaaBenchmarkSample&) { return true; });
        const SmaaTimingAccounting smaaTimingAccounting =
            m_SmaaPass
                ? m_SmaaPass->GetTimingAccounting()
                : SmaaTimingAccounting{};
        const ResolvedAntiAliasingSettings resolved =
            m_ui.GetResolvedAntiAliasingSettings();
        const uint64_t smaaExecutionValidSampleCount =
            static_cast<uint64_t>(std::count_if(
                m_SmaaBenchmarkSamples.begin(),
                m_SmaaBenchmarkSamples.end(),
                [](const SmaaBenchmarkSample& sample)
                {
                    return sample.timings.executionValid;
                }));
        const uint64_t smaaExecutionFallbackSampleCount =
            static_cast<uint64_t>(m_SmaaBenchmarkSamples.size()) -
            smaaExecutionValidSampleCount;
        const bool expectsSmaaTimings =
            resolved.enabled &&
            UsesSmaaApplication(
                resolved.subpixelMorphology);
        const bool smaaTimingEvidenceValid =
            !expectsSmaaTimings ||
            (m_SmaaBenchmarkSamples.size() == 256u &&
                smaaExecutionFallbackSampleCount == 0u &&
                smaaTimingAccounting.issuedSampleCount == 256u &&
                smaaTimingAccounting.droppedSampleCount == 0u &&
                smaaTimingAccounting.retiredSampleCount == 256u &&
                smaaTimingAccounting.OutstandingSampleCount() == 0u);
        const bool benchmarkEvidenceValid =
            complete.count == 256u &&
            m_AaBenchmarkIssuedSamples == 256u &&
            m_AaBenchmarkDroppedSamples == 0u &&
            m_AaBenchmarkOutstandingSamples == 0u &&
            smaaTimingEvidenceValid;
        const auto activeAdapter = std::find_if(
            m_ui.GpuAdapterChoices.begin(),
            m_ui.GpuAdapterChoices.end(),
            [&](const GpuAdapterChoice& adapter)
            {
                return adapter.adapterIndex ==
                    m_ui.ActiveGpuAdapterIndex;
            });
        const std::string adapterName =
            activeAdapter != m_ui.GpuAdapterChoices.end()
                ? activeAdapter->name
                : "Unknown";
        const uint32_t adapterVendorId =
            activeAdapter != m_ui.GpuAdapterChoices.end()
                ? activeAdapter->vendorId
                : 0u;
        const uint32_t adapterDeviceId =
            activeAdapter != m_ui.GpuAdapterChoices.end()
                ? activeAdapter->deviceId
                : 0u;
        const SponzaCameraPreset& benchmarkPreset =
            GetDefaultSponzaCameraPreset();

        std::ofstream output(m_AaBenchmark.outputPath);
        if (!output.is_open())
        {
            log::error(
                "AA benchmark could not open output path %s",
                m_AaBenchmark.outputPath.generic_string().c_str());
            if (m_InteractiveAaMotionTest)
            {
                FinishInteractiveAntiAliasingMotionTest(
                    false,
                    0.f,
                    0.f,
                    "Could not create the timing report.");
                return;
            }
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return;
        }
        const auto writeStatistics = [&output](
            const char* indentation,
            const char* name,
            const AaBenchmarkStatistics& statistics,
            bool trailingComma)
        {
            output << indentation << std::quoted(name)
                << ": { \"sample_count\": " << statistics.count
                << ", \"median_gpu_ms\": " << statistics.median
                << ", \"worst_gpu_ms\": " << statistics.worst
                << " }" << (trailingComma ? "," : "") << "\n";
        };
        const auto writeStageSet = [&](
            const char* indentation,
            const char* name,
            const SmaaStageStatistics& statistics,
            bool trailingComma)
        {
            output << indentation << std::quoted(name) << ": {\n";
            const std::string childIndent =
                std::string(indentation) + "  ";
            writeStatistics(
                childIndent.c_str(), "edge",
                statistics.edge, true);
            writeStatistics(
                childIndent.c_str(), "weight",
                statistics.weight, true);
            writeStatistics(
                childIndent.c_str(), "neighborhood",
                statistics.neighborhood, true);
            writeStatistics(
                childIndent.c_str(), "complete",
                statistics.complete, false);
            output << indentation << "}"
                << (trailingComma ? "," : "") << "\n";
        };
        output << std::fixed << std::setprecision(6)
            << "{\n"
            << "  \"adapter\": " << std::quoted(adapterName) << ",\n"
            << "  \"adapter_vendor_id\": " << adapterVendorId << ",\n"
            << "  \"adapter_device_id\": " << adapterDeviceId << ",\n"
            << "  \"camera_preset_id\": "
            << std::quoted(benchmarkPreset.Id) << ",\n"
            << "  \"camera_preset_label\": "
            << std::quoted(benchmarkPreset.Label) << ",\n"
            << "  \"render_width\": "
            << benchmarkPreset.ReferenceWidth << ",\n"
            << "  \"render_height\": "
            << benchmarkPreset.ReferenceHeight << ",\n"
            << "  \"method\": "
            << std::quoted(GetAntiAliasingMethodLabel(resolved.method))
            << ",\n"
            << "  \"quality\": "
            << std::quoted(GetAntiAliasingQualityLabel(resolved.quality))
            << ",\n"
            << "  \"implementation\": "
            << std::quoted(GetAntiAliasingPresetLabel(
                resolved.implementation))
            << ",\n"
            << "  \"developer_overrides_compiled\": "
            << (UVSR_AA_DEVELOPER_OVERRIDES ? "true" : "false")
            << ",\n"
            << "  \"execution_path\": "
            << std::quoted(GetMiniEngineTaaExecutionPathLabel(
                resolved.executionPath)) << ",\n"
            << "  \"compute_kernel\": "
            << std::quoted(GetMiniEngineTaaComputeKernelLabel(
                resolved.computeKernel)) << ",\n"
            << "  \"lds_layout\": "
            << std::quoted(GetMiniEngineTaaLdsLayoutLabel(
                resolved.ldsLayout)) << ",\n"
            << "  \"shared_work_reuse\": "
            << (resolved.sharedWorkReuse ? "true" : "false") << ",\n"
            << "  \"early_history_rejection\": "
            << (resolved.earlyHistoryRejection ? "true" : "false")
            << ",\n"
            << "  \"pass_fusion\": "
            << std::quoted(GetMiniEngineTaaPassFusionLabel(
                resolved.passFusion)) << ",\n"
            << "  \"cache_blocking\": "
            << std::quoted(GetMiniEngineTaaCacheBlockingLabel(
                resolved.cacheBlocking)) << ",\n"
            << "  \"subpixel_morphology\": "
            << std::quoted(GetSmaaApplicationLabel(
                resolved.subpixelMorphology)) << ",\n"
            << "  \"motion_source\": "
            << std::quoted(GetMiniEngineTaaMotionSourceLabel(
                resolved.temporal.motionSource)) << ",\n"
            << "  \"current_reconstruction\": "
            << std::quoted(
                GetMiniEngineTaaCurrentReconstructionLabel(
                    resolved.temporal.currentReconstruction))
            << ",\n"
            << "  \"history_filter\": "
            << std::quoted(GetMiniEngineTaaHistoryFilterLabel(
                resolved.temporal.historyFilter)) << ",\n"
            << "  \"rectification\": "
            << std::quoted(GetMiniEngineTaaRectificationLabel(
                resolved.temporal.rectification)) << ",\n"
            << "  \"stable_interior\": "
            << std::quoted(GetMiniEngineTaaInteriorWeightingLabel(
                resolved.temporal.interiorWeighting)) << ",\n"
            << "  \"sample_resurrection\": "
            << std::quoted(GetMiniEngineTaaSampleResurrectionLabel(
                resolved.sampleResurrection)) << ",\n"
            << "  \"sharpness_enabled\": "
            << (m_ui.MiniEngineTaaSharpenEnabled ? "true" : "false")
            << ",\n"
            << "  \"sharpness\": " << m_ui.MiniEngineTaaSharpness
            << ",\n"
            << "  \"turn_degrees\": 45.000000,\n"
            << "  \"turn_degrees_per_frame\": 0.375000,\n"
            << "  \"target_frames_per_second\": "
            << AaBenchmarkTargetFramesPerSecond << ",\n"
            << "  \"turn_degrees_per_second\": "
            << AaBenchmarkTurnDegreesPerSecond << ",\n"
            << "  \"expected_sample_count\": 256,\n"
            << "  \"issued_sample_count\": "
            << m_AaBenchmarkIssuedSamples << ",\n"
            << "  \"dropped_sample_count\": "
            << m_AaBenchmarkDroppedSamples << ",\n"
            << "  \"sample_count\": " << complete.count << ",\n"
            << "  \"benchmark_evidence_valid\": "
            << (benchmarkEvidenceValid ? "true" : "false") << ",\n"
            << "  \"smaa_timing_issued_sample_count\": "
            << smaaTimingAccounting.issuedSampleCount << ",\n"
            << "  \"smaa_timing_dropped_sample_count\": "
            << smaaTimingAccounting.droppedSampleCount << ",\n"
            << "  \"smaa_timing_retired_sample_count\": "
            << smaaTimingAccounting.retiredSampleCount << ",\n"
            << "  \"smaa_timing_outstanding_sample_count\": "
            << smaaTimingAccounting.OutstandingSampleCount() << ",\n"
            << "  \"smaa_stage_sample_count\": "
            << m_SmaaBenchmarkSamples.size() << ",\n"
            << "  \"smaa_execution_valid_sample_count\": "
            << smaaExecutionValidSampleCount << ",\n"
            << "  \"smaa_execution_fallback_sample_count\": "
            << smaaExecutionFallbackSampleCount << ",\n"
            << "  \"warm_median_gpu_ms\": " << complete.median
            << ",\n"
            << "  \"worst_case_gpu_ms\": " << complete.worst
            << ",\n"
            << "  \"phase_0\": { \"sample_count\": " << phase0.count
            << ", \"median_gpu_ms\": " << phase0.median
            << ", \"worst_gpu_ms\": " << phase0.worst << " },\n"
            << "  \"phase_1\": { \"sample_count\": " << phase1.count
            << ", \"median_gpu_ms\": " << phase1.median
            << ", \"worst_gpu_ms\": " << phase1.worst << " },\n"
            << "  \"segments\": {\n"
            << "    \"turn_right\": { \"sample_count\": "
            << turnRight.count << ", \"median_gpu_ms\": "
            << turnRight.median << ", \"worst_gpu_ms\": "
            << turnRight.worst << " },\n"
            << "    \"hold_right\": { \"sample_count\": "
            << holdRight.count << ", \"median_gpu_ms\": "
            << holdRight.median << ", \"worst_gpu_ms\": "
            << holdRight.worst << " },\n"
            << "    \"turn_back\": { \"sample_count\": "
            << turnBack.count << ", \"median_gpu_ms\": "
            << turnBack.median << ", \"worst_gpu_ms\": "
            << turnBack.worst << " }\n"
            << "  },\n"
            << "  \"smaa_stage_timings\": {\n";
        writeStageSet(
            "    ", "all", allSmaaStages, false);
        output << "  }\n"
            << "}\n";
        output.flush();
        const bool outputWriteSucceeded = output.good();
        output.close();
        if (!outputWriteSucceeded)
        {
            log::error(
                "AA benchmark failed while writing %s",
                m_AaBenchmark.outputPath.generic_string().c_str());
            if (m_InteractiveAaMotionTest)
            {
                FinishInteractiveAntiAliasingMotionTest(
                    false,
                    complete.median,
                    complete.worst,
                    "The timing report could not be written completely.");
                return;
            }
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(),
                GLFW_TRUE);
            return;
        }

        if (benchmarkEvidenceValid)
        {
            log::info(
                "AA benchmark wrote %s: median %.4f ms, worst %.4f ms (%llu validated samples)",
                m_AaBenchmark.outputPath.generic_string().c_str(),
                complete.median,
                complete.worst,
                static_cast<unsigned long long>(complete.count));
        }
        else
        {
            log::error(
                "AA benchmark wrote INVALID evidence to %s "
                "(AA %llu/256, AA drops %llu, SMAA %llu/256, "
                "SMAA fallbacks %llu)",
                m_AaBenchmark.outputPath.generic_string().c_str(),
                static_cast<unsigned long long>(complete.count),
                static_cast<unsigned long long>(
                    m_AaBenchmarkDroppedSamples),
                static_cast<unsigned long long>(
                    m_SmaaBenchmarkSamples.size()),
                static_cast<unsigned long long>(
                    smaaExecutionFallbackSampleCount));
        }
        if (m_InteractiveAaMotionTest)
        {
            FinishInteractiveAntiAliasingMotionTest(
                benchmarkEvidenceValid,
                complete.median,
                complete.worst);
            return;
        }
        glfwSetWindowShouldClose(
            GetDeviceManager()->GetWindow(),
            GLFW_TRUE);
    }

    void FinishInteractiveAntiAliasingMotionTest(
        bool evidenceValid,
        float medianMilliseconds,
        float worstMilliseconds,
        const char* failureReason = nullptr)
    {
        std::ostringstream status;
        if (failureReason)
        {
            status << "Motion test failed: " << failureReason;
        }
        else
        {
            status << (evidenceValid ? "Complete" : "Complete (invalid evidence)")
                << ": median " << std::fixed << std::setprecision(3)
                << medianMilliseconds << " ms, worst "
                << worstMilliseconds << " ms. Report: "
                << m_AaBenchmark.outputPath.generic_string();
        }
        m_AaMotionTestStatus = status.str();

        m_AaBenchmark.enabled = false;
        m_AaBenchmarkStarted = false;
        m_AaBenchmarkPacingActive = false;
        m_AaBenchmarkCurrentTag = AaBenchmarkTimerTag{};
        m_InteractiveAaMotionTest = false;
        m_BenchmarkCameraActive = false;

        // Copy the final zero-degree benchmark pose into the interactive
        // camera before changing its location label. The test therefore ends
        // exactly where it began but never leaves the user in Locked mode.
        SetCameraMode(CameraMode::ThirdPerson);
        m_SponzaCameraLocation = SponzaCameraLocation::Free;
        log::info("Camera location is now Piloted");

        if (m_AaMotionTestPreviousWidth > 0 &&
            m_AaMotionTestPreviousHeight > 0)
        {
            glfwSetWindowSize(
                GetDeviceManager()->GetWindow(),
                m_AaMotionTestPreviousWidth,
                m_AaMotionTestPreviousHeight);
        }
    }

    void UpdateAntiAliasingBenchmark()
    {
        if (!m_AaBenchmark.enabled ||
            !m_BenchmarkCameraActive ||
            !IsSceneLoaded())
        {
            return;
        }

        constexpr uint32_t MinimumDrainFrames =
            c_AaTimerLatency * 2u;

        if (m_InteractiveAaMotionTest && !m_AaBenchmarkStarted)
        {
            const SponzaCameraPreset& preset =
                GetDefaultSponzaCameraPreset();
            int width = 0;
            int height = 0;
            GetDeviceManager()->GetWindowDimensions(width, height);
            if (width != int(preset.ReferenceWidth) ||
                height != int(preset.ReferenceHeight))
            {
                return;
            }
        }

        // The camera sequence is defined in rendered frames because temporal
        // image behavior depends on a fixed 0.375-degree inter-frame motion.
        // Pace those frames at 40 Hz so the same sample sequence also has a
        // stable wall-clock rate on both a fast discrete GPU and the Intel
        // benchmark adapter. GPU timer queries remain scoped only to the AA
        // command block; the CPU wait is outside the measured interval.
        using BenchmarkClock = std::chrono::steady_clock;
        constexpr auto BenchmarkFrameInterval =
            std::chrono::nanoseconds(
                1000000000ull /
                AaBenchmarkTargetFramesPerSecond);
        const BenchmarkClock::time_point now = BenchmarkClock::now();
        if (!m_AaBenchmarkPacingActive)
        {
            m_AaBenchmarkPacingActive = true;
            m_AaBenchmarkNextFrameDeadline = now;
        }
        else
        {
            m_AaBenchmarkNextFrameDeadline +=
                BenchmarkFrameInterval;
            if (m_AaBenchmarkNextFrameDeadline > now)
            {
                std::this_thread::sleep_until(
                    m_AaBenchmarkNextFrameDeadline);
            }
            else if (now - m_AaBenchmarkNextFrameDeadline >
                BenchmarkFrameInterval * 4)
            {
                // A breakpoint, resize, or slow scene load must not cause a
                // burst of catch-up frames with a visibly faster sweep.
                m_AaBenchmarkNextFrameDeadline = now;
            }
        }

        if (!m_AaBenchmarkStarted)
        {
            m_AaBenchmarkStarted = true;
            m_AaBenchmarkFrame = 0u;
            m_AaBenchmarkSamples.clear();
            m_SmaaBenchmarkSamples.clear();
            m_AaBenchmarkIssuedSamples = 0u;
            m_AaBenchmarkDroppedSamples = 0u;
            m_AaBenchmarkOutstandingSamples = 0u;
            if (m_SmaaPass)
                m_SmaaPass->ResetTimingAccounting();
            ResetAntiAliasingState();
            log::info(
                "AA benchmark started: 180 warm frames, 45-degree right turn at 0.375 degrees/frame and a 40 Hz target, then return");
        }

        float angleDegrees = 0.f;
        AaBenchmarkSegment segment = AaBenchmarkSegment::Warm;
        bool collectCurrentFrame = false;
        if (m_AaBenchmarkFrame >= AaBenchmarkWarmFrames &&
            m_AaBenchmarkFrame <
                AaBenchmarkWarmFrames + AaBenchmarkTurnFrames)
        {
            const uint32_t turnStep =
                m_AaBenchmarkFrame - AaBenchmarkWarmFrames + 1u;
            angleDegrees = 45.f *
                float(turnStep) /
                float(AaBenchmarkTurnFrames);
            segment = AaBenchmarkSegment::TurnRight;
            collectCurrentFrame = true;
        }
        else if (m_AaBenchmarkFrame <
            AaBenchmarkWarmFrames +
                AaBenchmarkTurnFrames +
                AaBenchmarkHoldFrames)
        {
            angleDegrees = 45.f;
            if (m_AaBenchmarkFrame >=
                AaBenchmarkWarmFrames + AaBenchmarkTurnFrames)
            {
                segment = AaBenchmarkSegment::HoldRight;
                collectCurrentFrame = true;
            }
        }
        else if (m_AaBenchmarkFrame < AaBenchmarkMotionEndFrame)
        {
            const uint32_t turnStep =
                m_AaBenchmarkFrame -
                AaBenchmarkWarmFrames -
                AaBenchmarkTurnFrames -
                AaBenchmarkHoldFrames + 1u;
            angleDegrees = 45.f * (1.f -
                float(turnStep) /
                    float(AaBenchmarkTurnFrames));
            segment = AaBenchmarkSegment::TurnBack;
            collectCurrentFrame = true;
        }
        else
        {
            segment = AaBenchmarkSegment::Drain;
        }

        m_AaBenchmarkCurrentTag.sourceFrame =
            m_AaBenchmarkFrame;
        m_AaBenchmarkCurrentTag.phase =
            uint32_t(m_AntiAliasingPhase & 1u);
        m_AaBenchmarkCurrentTag.segment = segment;
        m_AaBenchmarkCurrentTag.collect = collectCurrentFrame;

        const SponzaCameraPreset& preset =
            GetDefaultSponzaCameraPreset();
        const affine3 turn = rotation(
            preset.Up,
            -radians(angleDegrees));
        m_StaticCamera.SetExactPose(
            preset.Position,
            normalize(turn.transformVector(preset.Direction)),
            normalize(turn.transformVector(preset.Up)),
            normalize(turn.transformVector(preset.Right)));

        ++m_AaBenchmarkFrame;
        const bool smaaTimingsDrained =
            !m_SmaaPass ||
            m_SmaaPass->GetTimingAccounting()
                    .OutstandingSampleCount() == 0u;
        if (m_AaBenchmarkFrame >=
                AaBenchmarkMotionEndFrame + MinimumDrainFrames &&
            m_AaBenchmarkOutstandingSamples == 0u &&
            smaaTimingsDrained)
        {
            FinishAntiAliasingBenchmark();
        }
    }

    virtual void Animate(float fElapsedTimeSeconds) override
    {
        UpdateAntiAliasingBenchmark();
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
        m_SceneFinishedLoading = false;
        if (m_ForwardPass) m_ForwardPass->ResetBindingCache();
        if (m_DeferredLightingPass) m_DeferredLightingPass->ResetBindingCache();
        if (m_PbrDeferredLightingPass) m_PbrDeferredLightingPass->ResetBindingCache();
        if (m_ScreenSpaceVisibilityPass)
        {
            m_ScreenSpaceVisibilityPass->ResetBindingCache();
            m_ScreenSpaceVisibilityPass->ResetHistory();
        }
        ResetAntiAliasingState();
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
            const std::string& lightName = light->GetName();
            if (string_utils::strcasecmp(lightName, std::string("sun_1")) ||
                string_utils::strcasecmp(lightName, std::string("Sun")))
            {
                light->SetName("sun_1");
            }
            else if (string_utils::strcasecmp(
                lightName,
                std::string("hdri_sky_1")))
            {
                light->SetName("hdri_sky_1");
            }

            if (!m_SunLight &&
                light->GetLightType() == LightType_Directional)
            {
                m_SunLight = std::static_pointer_cast<DirectionalLight>(light);
                if (m_SunLight->irradiance <= 0.f)
                    m_SunLight->irradiance = 1.f;
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
        m_SceneFinishedLoading = true;

    }

    void SetWhiteWorldMode(WhiteWorldMode mode)
    {
        const bool modeChanged = m_ui.WhiteWorld != mode;
        const bool shaderModeChanged = (m_ui.WhiteWorld == WhiteWorldMode::Off) !=
            (mode == WhiteWorldMode::Off);
        m_ui.WhiteWorld = mode;

        if (modeChanged && m_ScreenSpaceVisibilityPass)
            m_ScreenSpaceVisibilityPass->ResetHistory();
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

    std::shared_ptr<Scene> GetScene()
    {
        return m_Scene;
    }

    void SynchronizeAntiAliasingSettings()
    {
        const bool requiresTemporalReset =
            m_HasAppliedAntiAliasingSettings &&
            CompiledAntiAliasingSettingsRequireTemporalReset(
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
        MiniEngineTaaJitterSample jitter{ 0.f, 0.f };
        if (m_ui.UsesLongTermTemporalAA())
        {
            jitter = GetMiniEngineTaaJitter(
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
        GetResolvedMorphologySource() const
    {
        if (!m_RenderTargets)
            return nullptr;
        return m_RenderTargets->GetSampleCount() > 1u
            ? m_RenderTargets->DeferredMsaaColor.Get()
            : m_RenderTargets->HdrColor.Get();
    }

    void CreateCmaa2Pass()
    {
        m_Cmaa2Pass = std::make_unique<Cmaa2Pass>(
            GetDevice(),
            m_ShaderFactory,
            m_CommonPasses,
            GetResolvedMorphologySource());
        if (!m_Cmaa2Pass->IsValid())
        {
            log::error(
                "Intel CMAA2 initialization failed; "
                "the scene-color input will be presented unchanged");
        }
    }

    void CreateRenderPasses()
    {
        m_MiniEngineTemporalAAPass.reset();
        m_SmaaPass.reset();
        m_Cmaa2Pass.reset();
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
            if (m_RenderTargets->GetSampleCount() > 1u &&
                m_RenderTargets->VisibilityResourcesEnabled)
            {
                m_MsaaVisibilityResolvePass =
                    std::make_unique<
                        MsaaVisibilityResolvePass>(
                        GetDevice());
                m_MsaaVisibilityResolvePass->Init(
                    m_ShaderFactory);
            }
            else
            {
                m_MsaaVisibilityResolvePass.reset();
            }
            m_ScreenSpaceVisibilityPass = std::make_unique<ScreenSpaceVisibilityPass>(
                GetDevice(),
                m_ShaderFactory,
                app::GetDirectoryWithExecutable().parent_path() /
                    "media/noise/visibility_filter_adapted_gauss1_ema035_r8.bin");
        }
        else
        {
            m_PbrDeferredLightingPass.reset();
            m_MsaaVisibilityResolvePass.reset();
            m_ScreenSpaceVisibilityPass.reset();
            m_DeferredLightingPass = std::make_shared<DeferredLightingPass>(GetDevice(), m_CommonPasses);
            m_DeferredLightingPass->Init(m_ShaderFactory);
        }

        nvrhi::ITexture* morphologySceneColor =
            GetResolvedMorphologySource();

        if (m_ui.UsesLongTermTemporalAA())
        {
            m_MiniEngineTemporalAAPass =
                std::make_unique<MiniEngineTemporalAAPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_CommonPasses,
                    m_RenderTargets->HdrColor,
                    m_RenderTargets->Depth,
                    m_RenderTargets->MotionVectors,
                    m_ui.GetResolvedAntiAliasingSettings()
                            .temporal.interiorWeighting ==
                        MiniEngineTaaInteriorWeighting::StableInterior);
        }

        if (m_ui.UsesSmaaPass())
        {
            m_SmaaPass = std::make_unique<SmaaPass>(
                GetDevice(),
                m_ShaderFactory,
                m_CommonPasses,
                morphologySceneColor);
        }

        if (m_ui.UsesCmaa2())
            CreateCmaa2Pass();

        m_SkyPass = std::make_unique<SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->ForwardFramebuffer, *m_View);

        m_AgxToneMappingPass = std::make_unique<AgxToneMappingPass>(
            GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer);

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

            const ResolvedAntiAliasingSettings
                resolvedAntiAliasing =
                    m_ui.GetResolvedAntiAliasingSettings();
            const uint sampleCount =
                ResolveSupportedMsaaSampleCount(
                    GetDevice(),
                    resolvedAntiAliasing.rasterSampleCount);
            const bool visibilityResourcesRequired = m_ui.EnablePbr &&
                m_ui.IsScreenSpaceVisibilityAvailable() &&
                m_ui.ScreenSpaceVisibility.HasActiveConsumer();
            const bool visibilitySourceRadianceRequired =
                visibilityResourcesRequired &&
                m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
                (!sceneLights.empty() ||
                    VisibilityEmissiveSourceGain > 0.f);
            const bool temporalAARequired =
                m_ui.UsesLongTermTemporalAA();
            const bool smaaRequired = m_ui.UsesSmaaPass();
            const bool cmaa2Required = m_ui.UsesCmaa2();
            const bool motionVectorsRequired =
                m_ui.RequiresAntiAliasingMotionVectors() ||
                (visibilityResourcesRequired &&
                    (m_ui.ScreenSpaceVisibility
                            .RequiresMotionVectors() ||
                        sampleCount > 1u));

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
            if (m_MiniEngineTemporalAAPass &&
                m_MiniEngineTemporalAAPass
                        ->IsMomentHistoryRequested() !=
                    (m_ui.GetResolvedAntiAliasingSettings()
                            .temporal.interiorWeighting ==
                        MiniEngineTaaInteriorWeighting::StableInterior))
            {
                // Stable Interior is the only consumer of moment history.
                // Its image-key transition already resets temporal state;
                // recreate here to add or remove the full-resolution pair.
                needNewPasses = true;
            }
            if (smaaRequired != bool(m_SmaaPass))
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

        AdvanceAntiAliasingTimer();
        m_CommandList->open();
        AdvanceRendererTimers();
        BeginRendererStage(RendererTimingStage::CompleteFrame);
        BeginRendererStage(RendererTimingStage::SceneSetup);

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
        DeferredLightingPass::Inputs deferredMsaaInputs;
        bool deferredMsaaLightingPending = false;
        bool deferredMsaaVisibilityPending = false;

        if (!m_ui.UsesDeferredShading())
            m_ForwardPass->PrepareLights(forwardContext, m_CommandList, m_Scene->GetSceneGraph()->GetLights(), m_AmbientTop, m_AmbientBottom, {});
        EndRendererStage(RendererTimingStage::SceneSetup);

        if (m_ui.UsesDeferredShading())
        {
            GBufferFillPass::Context gbufferContext;

            BeginRendererStage(RendererTimingStage::Geometry);
            RenderCompositeView(m_CommandList,
                m_View.get(), m_PreviousView ? m_PreviousView.get() : m_View.get(),
                *m_RenderTargets->GBufferFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_GBufferPass,
                gbufferContext,
                "GBufferFill",
                false);
            EndRendererStage(RendererTimingStage::Geometry);

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
                m_ui.IsScreenSpaceVisibilityAvailable() &&
                m_ui.ScreenSpaceVisibility.HasActiveConsumer();
            uint32_t knownInactiveLightingSources = 0u;
            if (sceneLights.empty())
                knownInactiveLightingSources |= LightingSource_Direct;
            const bool allFirstBounceSourcesInactive =
                (knownInactiveLightingSources &
                    (LightingSource_Direct | LightingSource_Emissive)) ==
                (LightingSource_Direct | LightingSource_Emissive);
            const bool writeSourceRadiance = runScreenSpaceVisibility &&
                m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
                !allFirstBounceSourcesInactive;
            const bool writeBounceMetadata = writeSourceRadiance &&
                (!m_ui.ScreenSpaceVisibility.indirectDiffuse.limitBounces ||
                    m_ui.ScreenSpaceVisibility.indirectDiffuse.bounceCount >
                        1u);
            deferredInputs.output = runScreenSpaceVisibility
                ? m_RenderTargets->BaseLighting.Get()
                : m_RenderTargets->HdrColor.Get();

            if (m_ui.UsesHardwareMsaa())
            {
                // Preserve every G-buffer sample until after material decode
                // and nonlinear lighting. The resolved sky is added later as
                // the exact uncovered-sample contribution.
                deferredMsaaInputs = deferredInputs;
                deferredMsaaInputs.output =
                    m_RenderTargets->DeferredMsaaColor;
                deferredMsaaLightingPending = true;

                if (runScreenSpaceVisibility &&
                    m_MsaaVisibilityResolvePass &&
                    m_ScreenSpaceVisibilityPass)
                {
                    MsaaVisibilityResolveInputs resolveInputs;
                    resolveInputs.depth =
                        m_RenderTargets->Depth;
                    resolveInputs.diffuse =
                        m_RenderTargets->GBufferDiffuse;
                    resolveInputs.material =
                        m_RenderTargets->GBufferSpecular;
                    resolveInputs.normals =
                        m_RenderTargets->GBufferNormals;
                    resolveInputs.emissive =
                        m_RenderTargets->GBufferEmissive;
                    resolveInputs.materialAmbientOcclusion =
                        m_RenderTargets
                            ->MaterialAmbientOcclusion;
                    resolveInputs.motionVectors =
                        m_RenderTargets->MotionVectors;

                    MsaaVisibilityResolveOutputs
                        resolveOutputs;
                    resolveOutputs.depth =
                        m_RenderTargets->VisibilityDepth;
                    resolveOutputs.diffuse =
                        m_RenderTargets
                            ->VisibilityGBufferDiffuse;
                    resolveOutputs.material =
                        m_RenderTargets
                            ->VisibilityGBufferMaterial;
                    resolveOutputs.normals =
                        m_RenderTargets
                            ->VisibilityGBufferNormals;
                    resolveOutputs.emissive =
                        m_RenderTargets
                            ->VisibilityGBufferEmissive;
                    resolveOutputs.materialAmbientOcclusion =
                        m_RenderTargets
                            ->VisibilityMaterialAmbientOcclusion;
                    resolveOutputs.motionVectors =
                        m_RenderTargets
                            ->VisibilityMotionVectors;
                    m_MsaaVisibilityResolvePass->Render(
                        m_CommandList,
                        resolveInputs,
                        resolveOutputs,
                        m_RenderTargets->GetSampleCount());

                    DeferredLightingPass::Inputs
                        visibilityDeferredInputs =
                            deferredInputs;
                    visibilityDeferredInputs.depth =
                        resolveOutputs.depth;
                    visibilityDeferredInputs.gbufferDiffuse =
                        resolveOutputs.diffuse;
                    visibilityDeferredInputs.gbufferSpecular =
                        resolveOutputs.material;
                    visibilityDeferredInputs.gbufferNormals =
                        resolveOutputs.normals;
                    visibilityDeferredInputs.gbufferEmissive =
                        resolveOutputs.emissive;
                    visibilityDeferredInputs.indirectDiffuse =
                        resolveOutputs
                            .materialAmbientOcclusion;
                    visibilityDeferredInputs.output =
                        m_RenderTargets->BaseLighting;
                    m_PbrDeferredLightingPass->Render(
                        m_CommandList,
                        *m_View,
                        visibilityDeferredInputs,
                        m_RenderTargets
                            ->DirectDiffuseRadiance,
                        true,
                        writeSourceRadiance,
                        writeBounceMetadata,
                        true,
                        VisibilityEmissiveSourceGain,
                        float2(0.f));

                    ScreenSpaceVisibilityInputs
                        visibilityInputs;
                    visibilityInputs.depth =
                        resolveOutputs.depth;
                    visibilityInputs.normals =
                        resolveOutputs.normals;
                    visibilityInputs.motionVectors =
                        resolveOutputs.motionVectors;
                    visibilityInputs.sourceRadiance =
                        m_RenderTargets
                            ->DirectDiffuseRadiance;
                    visibilityInputs.gbufferDiffuse =
                        resolveOutputs.diffuse;
                    visibilityInputs.gbufferSpecular =
                        resolveOutputs.material;
                    visibilityInputs.gbufferEmissive =
                        resolveOutputs.emissive;
                    visibilityInputs.materialAmbientOcclusion =
                        resolveOutputs
                            .materialAmbientOcclusion;
                    visibilityInputs.baseLighting =
                        m_RenderTargets->BaseLighting;
                    visibilityInputs.output =
                        m_RenderTargets
                            ->VisibilityComposite;
                    visibilityInputs
                        .knownInactiveLightingSources =
                            knownInactiveLightingSources;
                    m_ScreenSpaceVisibilityPass->Render(
                        m_CommandList,
                        m_ui.ScreenSpaceVisibility,
                        *m_View,
                        visibilityInputs,
                        m_AmbientTop,
                        m_AmbientBottom,
                        1.f,
                        uint32_t(GetFrameIndex()));
                    deferredMsaaVisibilityPending = true;
                }
                else if (m_ScreenSpaceVisibilityPass)
                {
                    m_ScreenSpaceVisibilityPass->Deactivate();
                }
            }
            else if (m_ui.EnablePbr)
            {
                BeginRendererStage(RendererTimingStage::DirectLighting);
                m_PbrDeferredLightingPass->Render(
                    m_CommandList,
                    *m_View,
                    deferredInputs,
                    m_RenderTargets->DirectDiffuseRadiance,
                    runScreenSpaceVisibility,
                    writeSourceRadiance,
                    writeBounceMetadata,
                    true,
                    VisibilityEmissiveSourceGain,
                    float2(0.f));
                EndRendererStage(RendererTimingStage::DirectLighting);

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
                    BeginRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
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
                    EndRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
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
                BeginRendererStage(RendererTimingStage::DirectLighting);
                m_DeferredLightingPass->Render(
                    m_CommandList, *m_View, deferredInputs, float2(0.f));
                EndRendererStage(RendererTimingStage::DirectLighting);
            }
        }
        else
        {
            BeginRendererStage(RendererTimingStage::Geometry);
            RenderCompositeView(m_CommandList,
                m_View.get(), m_View.get(),
                *m_RenderTargets->ForwardFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                *m_ForwardPass,
                forwardContext,
                "ForwardOpaque",
                false);
            EndRendererStage(RendererTimingStage::Geometry);
        }

        if(m_Pick)
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

        if (m_ui.EnableProceduralSky)
        {
            BeginRendererStage(RendererTimingStage::ProceduralSky);
            m_SkyPass->Render(m_CommandList, *m_View, *m_SunLight, m_ui.SkyParams);
            EndRendererStage(RendererTimingStage::ProceduralSky);
        }

        nvrhi::ITexture* sceneColor =
            m_RenderTargets->HdrColor;
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
        if (deferredMsaaLightingPending &&
            m_PbrDeferredLightingPass &&
            m_RenderTargets->ResolvedHdrColor &&
            m_RenderTargets->DeferredMsaaColor)
        {
            m_PbrDeferredLightingPass->Render(
                m_CommandList,
                *m_View,
                deferredMsaaInputs,
                nullptr,
                deferredMsaaVisibilityPending,
                false,
                false,
                false,
                0.f,
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
            sceneColor =
                m_RenderTargets->DeferredMsaaColor;
        }

        const bool antiAliasingTimerActive =
            BeginAntiAliasingTimer();
        const ResolvedAntiAliasingSettings antiAliasing =
            m_ui.GetResolvedAntiAliasingSettings();
#if UVSR_AA_DEVELOPER_OVERRIDES
        const MiniEngineTaaDebugView activeAaVisualization =
            m_ui.MiniEngineTaaVisualization;
#else
        // Debug shaders and routing are absent from production. Ignore stale
        // or hostile programmatic state before it can suppress presentation
        // SMAA or alter the shipping render graph.
        constexpr MiniEngineTaaDebugView activeAaVisualization =
            MiniEngineTaaDebugView::Off;
#endif
        const bool miniEngineDebugVisualizationActive =
            m_MiniEngineTemporalAAPass &&
            IsLongTermTemporalPreset(
                antiAliasing.implementation) &&
            IsMiniEngineTaaDebugVisualization(
                activeAaVisualization);
        const bool temporalSharpenEnabled =
            ShouldSharpenMiniEngineTaa(
                m_ui.MiniEngineTaaSharpenEnabled,
                m_ui.MiniEngineTaaSharpness);
        const bool deferTemporalSharpenToPresentation =
            m_MiniEngineTemporalAAPass &&
            m_SmaaPass &&
            IsLongTermTemporalPreset(
                antiAliasing.implementation) &&
            antiAliasing.subpixelMorphology !=
                SmaaApplication::Off &&
            !miniEngineDebugVisualizationActive &&
            temporalSharpenEnabled;
        nvrhi::ITexture* antiAliasedTexture =
            sceneColor;
        if (m_MiniEngineTemporalAAPass)
        {
            // Resolve scene-linear radiance before any display transform. The
            // pass intentionally has no exposure, grading, LUT, or transfer
            // dependency, so removing or replacing the display stage does not
            // change its contract.
            antiAliasedTexture =
                m_MiniEngineTemporalAAPass->Render(
                m_CommandList,
                *m_View,
                m_PreviousView.get(),
                m_AntiAliasingPhase,
                antiAliasing,
                activeAaVisualization,
                antiAliasing.subpixelMorphology ==
                        SmaaApplication::SelectiveShort ||
                    antiAliasing.subpixelMorphology ==
                        SmaaApplication::SelectiveLong,
                temporalSharpenEnabled &&
                    !deferTemporalSharpenToPresentation,
                deferTemporalSharpenToPresentation,
                m_ui.MiniEngineTaaSharpness);
        }

        bool cmaa2RenderedThisFrame = false;
        if (m_Cmaa2Pass &&
            antiAliasing.subpixelMorphology ==
                SmaaApplication::ConservativeMorphological)
        {
            antiAliasedTexture = m_Cmaa2Pass->Render(
                m_CommandList,
                antiAliasedTexture,
                antiAliasing.quality);
            cmaa2RenderedThisFrame = true;
        }
        if (m_Cmaa2Pass && !cmaa2RenderedThisFrame)
            m_Cmaa2Pass->MarkInactiveFrame();

        [[maybe_unused]] nvrhi::ITexture* smaaInputTexture =
            antiAliasedTexture;
        [[maybe_unused]] nvrhi::ITexture* smaaFilteredTexture =
            nullptr;
        const uint64_t smaaTimingTag = m_AaBenchmark.enabled
            ? AaBenchmarkTimingTagFlag |
                (m_AaBenchmarkCurrentTag.collect
                    ? SmaaTimingCollectTagFlag
                    : 0u) |
                uint64_t(m_AaBenchmarkCurrentTag.sourceFrame)
            : uint64_t(m_AaTimerFrame);
        bool smaaRenderedThisFrame = false;
        if (m_SmaaPass &&
            !miniEngineDebugVisualizationActive)
        {
            switch (antiAliasing.implementation)
            {
            case AntiAliasingPreset::Smaa1x:
                smaaInputTexture = antiAliasedTexture;
                antiAliasedTexture = m_SmaaPass->Render1x(
                    m_CommandList,
                    antiAliasedTexture,
                    antiAliasing.quality,
                    smaaTimingTag);
                smaaFilteredTexture = antiAliasedTexture;
                smaaRenderedThisFrame = true;
                break;
            default:
                if (antiAliasing.subpixelMorphology ==
                    SmaaApplication::FullScreen)
                {
                    smaaInputTexture = antiAliasedTexture;
                    antiAliasedTexture =
                        m_SmaaPass->RenderFullScreenPresentation(
                            m_CommandList,
                            antiAliasedTexture,
                            antiAliasing.quality,
                            smaaTimingTag);
                    smaaFilteredTexture = antiAliasedTexture;
                    smaaRenderedThisFrame = true;
                }
                else if (
                    m_MiniEngineTemporalAAPass &&
                    (antiAliasing.subpixelMorphology ==
                            SmaaApplication::SelectiveShort ||
                        antiAliasing.subpixelMorphology ==
                            SmaaApplication::SelectiveLong))
                {
                    const bool historyGloballyValid =
                        m_MiniEngineTemporalAAPass
                            ->WasHistoryInputValidLastRender();
                    if (!historyGloballyValid)
                    {
                        // A camera cut/current-only frame has no trustworthy
                        // temporal coverage to make sparse. Run complete
                        // full-quality SMAA over de-jittered current.
                        smaaInputTexture =
                            m_MiniEngineTemporalAAPass
                                ->GetSelectiveCurrentTexture();
                        antiAliasedTexture =
                            m_SmaaPass
                                ->RenderFullScreenPresentation(
                                    m_CommandList,
                                    m_MiniEngineTemporalAAPass
                                        ->GetSelectiveCurrentTexture(),
                                    antiAliasing.quality,
                                    smaaTimingTag);
                        smaaFilteredTexture = antiAliasedTexture;
                        smaaRenderedThisFrame = true;
                    }
                    else
                    {
                        smaaInputTexture = antiAliasedTexture;
                        antiAliasedTexture =
                            m_SmaaPass->RenderSelective(
                                m_CommandList,
                                m_MiniEngineTemporalAAPass
                                    ->GetSelectiveCurrentTexture(),
                                antiAliasedTexture,
                                m_MiniEngineTemporalAAPass
                                    ->GetSelectiveRejectionTexture(),
                                antiAliasing.quality,
                                smaaTimingTag);
                        smaaFilteredTexture = antiAliasedTexture;
                        smaaRenderedThisFrame = true;
                    }
                }
                break;
            }
        }
        if (m_SmaaPass && !smaaRenderedThisFrame)
            m_SmaaPass->MarkInactiveFrame();
        if (deferTemporalSharpenToPresentation)
        {
            // Apply the same sharpness to the composed presentation result.
            // Blending sharpened temporal against unsharpened spatial current
            // made a changing selective rejection mask modulate edge detail.
            antiAliasedTexture =
                m_MiniEngineTemporalAAPass
                    ->SharpenPresentation(
                        m_CommandList,
                        antiAliasedTexture);
        }
#if UVSR_AA_DEVELOPER_OVERRIDES
        if (m_SmaaPass &&
            smaaFilteredTexture &&
            IsSmaaDebugVisualization(
                m_ui.MiniEngineTaaVisualization))
        {
            antiAliasedTexture = m_SmaaPass->RenderDebugView(
                m_CommandList,
                m_ui.MiniEngineTaaVisualization,
                smaaInputTexture,
                smaaFilteredTexture);
        }
#endif
        CollectCompletedSmaaTimings();
        EndAntiAliasingTimer(antiAliasingTimerActive);

        nvrhi::ITexture* displayTexture = antiAliasedTexture;
        if (m_ui.UsesTonemapper())
        {
            BeginRendererStage(RendererTimingStage::ToneMapping);
            m_AgxToneMappingPass->Render(
                m_CommandList, *m_View, antiAliasedTexture);
            EndRendererStage(RendererTimingStage::ToneMapping);

            displayTexture = m_RenderTargets->LdrColor;
        }

        // The tonemapperless renderer intentionally sends forward scene-linear
        // radiance straight to the sRGB swap-chain target. The render-target
        // conversion still applies the display transfer and clamps values to
        // the target's representable range, but AgX output conversion and
        // dithering are absent from this path.
        BeginRendererStage(RendererTimingStage::OutputBlit);
        m_CommonPasses->BlitTexture(
            m_CommandList, framebuffer, displayTexture, &m_BindingCache);
        EndRendererStage(RendererTimingStage::OutputBlit);
        EndRendererStage(RendererTimingStage::CompleteFrame);
        CompleteRendererTimerFrame();

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        CaptureCompletedVisibilityBenchmarkFrame(framebufferTexture);
        if (m_RenderTargets->MotionVectorsEnabled)
            CaptureCurrentViewForMotionVectors();
        if (m_ui.UsesJitteredAntiAliasing())
            ++m_AntiAliasingPhase;

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

    const MiniEngineTemporalAATimings* GetMiniEngineTemporalAATimings() const
    {
        return m_MiniEngineTemporalAAPass
            ? &m_MiniEngineTemporalAAPass->GetTimings()
            : nullptr;
    }

    const SmaaTimings* GetSmaaTimings() const
    {
        return m_SmaaPass
            ? &m_SmaaPass->GetTimings()
            : nullptr;
    }

    const Cmaa2Timings* GetCmaa2Timings() const
    {
        return m_Cmaa2Pass
            ? &m_Cmaa2Pass->GetTimings()
            : nullptr;
    }

    uint32_t GetRasterSampleCount() const
    {
        return m_RenderTargets
            ? m_RenderTargets->GetSampleCount()
            : 1u;
    }

    [[nodiscard]] const RendererTimings& GetRendererTimings() const
    {
        return m_RendererTimings;
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

std::string UvsrSceneViewer::GetActiveAdapterName() const
{
    for (const GpuAdapterChoice& adapter : m_ui.GpuAdapterChoices)
    {
        if (adapter.adapterIndex == m_ui.ActiveGpuAdapterIndex)
            return adapter.name;
    }
    return "Unknown Adapter";
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
            continue;

        nvrhi::ITimerQuery* query =
            m_RendererTimerQueries[stageIndex][slot];
        if (!GetDevice()->pollTimerQuery(query))
        {
            m_RendererTimerFrameWritable = false;
            continue;
        }

        m_RendererTimings.milliseconds[stageIndex] =
            GetDevice()->getTimerQueryTime(query) * 1000.f;
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
    m_RendererTimerActive[stageIndex] = false;
}

void UvsrSceneViewer::CompleteRendererTimerFrame()
{
    if (m_RendererTimerFrameWritable)
        ++m_RendererTimerFrame;
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
        if (!m_SceneFinishedLoading)
        {
            // Command-line benchmarks are queued before Donut completes its
            // asynchronous scene load. Let the queue wait for SceneLoaded,
            // which will either activate the standardized camera or expose a
            // precise unsupported-scene error on the first rendered frame.
        }
        else if (!m_SponzaCameraLocationsAvailable)
        {
            m_VisibilityBenchmarkError =
                "Visibility benchmarks require PBR Sponza Decorated or "
                "PBR Sponza Plain so Benchmark Position 1 can be locked.";
            log::warning("%s", m_VisibilityBenchmarkError.c_str());
            return false;
        }
        else
        {
            m_VisibilityBenchmarkOwnsCameraLock = true;
            m_VisibilityBenchmarkPreviousCameraMode = m_ui.Camera;
            GetDeviceManager()->GetWindowDimensions(
                m_VisibilityBenchmarkPreviousWindowWidth,
                m_VisibilityBenchmarkPreviousWindowHeight);
            ApplySponzaCameraPreset(GetDefaultSponzaCameraPreset());
            m_SponzaCameraLocation =
                SponzaCameraLocation::SimplifiedApproximation;
            m_ui.Camera = CameraMode::Static;
            m_BenchmarkCameraActive = true;

            GLFWwindow* window = GetDeviceManager()->GetWindow();
            const SponzaCameraPreset& preset =
                GetDefaultSponzaCameraPreset();
            glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
            glfwSetWindowSize(
                window,
                int(preset.ReferenceWidth),
                int(preset.ReferenceHeight));
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
        IsVisibilityBenchmarkActive();
    m_VisibilityBenchmarkQueued = false;
    if (m_ScreenSpaceVisibilityPass &&
        m_ScreenSpaceVisibilityPass->IsBenchmarkActive())
    {
        m_ScreenSpaceVisibilityPass->CancelBenchmark();
    }
    m_VisibilityBenchmarkAutoClose = false;
    DiscardVisibilityBenchmarkMeasuredFrame();
    m_ActiveVisibilityBenchmarkArtifact = {};
    ReleaseVisibilityBenchmarkCameraLock();
    if (wasBusy)
    {
        m_VisibilityBenchmarkStatus = "Canceled.";
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
    if (m_VisibilityBenchmarkPreviousWindowWidth > 0 &&
        m_VisibilityBenchmarkPreviousWindowHeight > 0)
    {
        glfwSetWindowSize(
            window,
            m_VisibilityBenchmarkPreviousWindowWidth,
            m_VisibilityBenchmarkPreviousWindowHeight);
    }
    m_VisibilityBenchmarkPreviousWindowWidth = 0;
    m_VisibilityBenchmarkPreviousWindowHeight = 0;
}

void UvsrSceneViewer::FailVisibilityBenchmark(const std::string& message)
{
    const bool closeAfterFailure = m_VisibilityBenchmarkAutoClose;
    m_VisibilityBenchmarkQueued = false;
    if (m_ScreenSpaceVisibilityPass &&
        m_ScreenSpaceVisibilityPass->IsBenchmarkActive())
    {
        m_ScreenSpaceVisibilityPass->CancelBenchmark();
    }
    m_VisibilityBenchmarkAutoClose = false;
    DiscardVisibilityBenchmarkMeasuredFrame();
    m_ActiveVisibilityBenchmarkArtifact = {};
    ReleaseVisibilityBenchmarkCameraLock();
    m_VisibilityBenchmarkStatus = "Failed.";
    m_VisibilityBenchmarkError = message;
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
        if (m_VisibilityBenchmarkQueued &&
            m_SceneFinishedLoading &&
            !m_SponzaCameraLocationsAvailable)
        {
            FailVisibilityBenchmark(
                "Visibility benchmarks require PBR Sponza Decorated or "
                "PBR Sponza Plain so Benchmark Position 1 can be locked.");
            return;
        }
        const SponzaCameraPreset& preset = GetDefaultSponzaCameraPreset();
        const BaseCamera& camera = GetActiveCamera();
        const bool controlledEnvironmentReady =
            m_BenchmarkCameraActive &&
            m_ui.Camera == CameraMode::Static &&
            width == int(preset.ReferenceWidth) &&
            height == int(preset.ReferenceHeight) &&
            IsSponzaCameraAtPreset(
                preset,
                camera.GetPosition(),
                camera.GetDir(),
                camera.GetUp());
        if (m_VisibilityBenchmarkQueued && !controlledEnvironmentReady)
        {
            m_VisibilityBenchmarkStatus =
                "Queued; preparing locked Benchmark Position 1 at "
                "1920 x 1080.";
            return;
        }
        if (!controlledEnvironmentReady)
        {
            FailVisibilityBenchmark(
                "The controlled environment changed. Visibility benchmarks "
                "require locked Benchmark Position 1 at 1920x1080.");
            return;
        }
    }
    const ScreenSpaceVisibilityTimings& timings =
        m_ScreenSpaceVisibilityPass->GetTimings();
    const VisibilityPerformanceWorkload workload =
        GetRenderedVisibilityPerformanceWorkload(
            m_ui.ScreenSpaceVisibility,
            uint32_t(std::max(width, 0)),
            uint32_t(std::max(height, 0)),
            &timings);
    const VisibilityExecutionPlan plan = ResolveVisibilityExecutionPlan(
        GetEffectiveVisibilityPerformanceConfiguration(
            m_ui.ScreenSpaceVisibility),
        workload);

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
            const std::string settingsMismatch =
                FindVisibilityVerificationSettingsMismatch(
                    m_ui.VisibilityVerification,
                    m_ui.ScreenSpaceVisibility,
                    workload);
            if (verification.valid && settingsMismatch.empty() &&
                m_ui.ScreenSpaceVisibility.performanceProfile ==
                    verification.definition.implementationProfile)
            {
                profileName.assign(verification.definition.name);
            }
        }
        if (profileName.empty())
        {
            const VisibilityPerformanceProfileConfiguration configuration =
                GetEffectiveVisibilityPerformanceConfiguration(
                    m_ui.ScreenSpaceVisibility);
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
        m_VisibilityBenchmarkStatus =
            "Running warmup and measured frames.";
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
    struct StatSnapshot
    {
        int width = 0;
        int height = 0;
        double frameTimeSeconds = 0.0;
        GpuPerformanceMetrics gpuMetrics;
        ScreenSpaceVisibilityTimings visibilityTimings;
        MiniEngineTemporalAATimings temporalAATimings;
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
    std::array<std::string, 3> m_VisibilityStatLines;
    std::array<std::string, 2> m_TemporalAAStatLines;
    std::deque<StatSnapshot> m_StatUpdateQueue;
    bool m_HasAppliedStatSnapshot = false;
    bool m_HasGpuStatSnapshot = false;
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

    static std::string_view GetPerformanceProfileUiLabel(
        VisibilityPerformanceProfile profile)
    {
        static const std::array<std::string,
            static_cast<size_t>(VisibilityPerformanceProfile::Count)>
            labels = []()
            {
                std::array<std::string,
                    static_cast<size_t>(
                        VisibilityPerformanceProfile::Count)> result;
                for (uint32_t index = 1u;
                    index < static_cast<uint32_t>(
                        VisibilityPerformanceProfile::Count);
                    ++index)
                {
                    const auto candidate =
                        static_cast<VisibilityPerformanceProfile>(index);
                    std::string provenance = " (Bespoke)";
                    switch (candidate)
                    {
                    case VisibilityPerformanceProfile::Reference:
                        provenance = " (Therrien)";
                        break;
                    case VisibilityPerformanceProfile::ExactFixed8:
                    case VisibilityPerformanceProfile::ExactFixed12:
                    case VisibilityPerformanceProfile::ExactFixed16:
                    case VisibilityPerformanceProfile::ExactFixed20:
                    case VisibilityPerformanceProfile::ExactFixed24:
                    case VisibilityPerformanceProfile::ExactFixed48:
                    case VisibilityPerformanceProfile::ExactFixed64:
                        provenance = " (Therrien) (Bespoke)";
                        break;
                    case VisibilityPerformanceProfile::
                            ExactPackedCurrentFast:
                        provenance = " (Kleber) (Bespoke)";
                        break;
                    case VisibilityPerformanceProfile::
                            ExactFixed8FusedResolveApply:
                        provenance = " (Therrien) (Bespoke)";
                        break;
                    case VisibilityPerformanceProfile::
                            AlgorithmicPackedEdges2x2:
                    case VisibilityPerformanceProfile::
                            AlgorithmicPackedEdgesDepthNormal2x2:
                    case VisibilityPerformanceProfile::
                            AlgorithmicPackedEdgesSlope2x2:
                    case VisibilityPerformanceProfile::
                            AlgorithmicPackedEdgesLeakage2x2:
                        provenance = " (Intel)";
                        break;
                    case VisibilityPerformanceProfile::
                            AlgorithmicFusedPackedEdges2x2:
                        provenance = " (Intel) (Bespoke)";
                        break;
                    default:
                        break;
                    }

                    std::string mutex;
                    const auto configuration =
                        GetVisibilityPerformanceProfileConfiguration(
                            candidate);
                    if (configuration.application ==
                            VisibilityApplicationMode::
                                FusedResolveAndApplyExact ||
                        configuration.application ==
                            VisibilityApplicationMode::
                                FusedResolveAndApplyPackedEdges)
                    {
                        mutex = " (Mutex GI)";
                    }

                    const std::string_view displayName =
                        candidate == VisibilityPerformanceProfile::
                                GenericFallback
                            ? std::string_view("Custom Composite")
                            : GetVisibilityPerformanceProfileDisplayName(
                                candidate);
                    result[index] = std::string(displayName) +
                        provenance + mutex;
                }
                return result;
            }();
        const size_t index = static_cast<size_t>(profile);
        return index < labels.size()
            ? std::string_view(labels[index])
            : std::string_view();
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
            return "Independent Hash Noise (Bespoke)";
        case VisibilityPerformanceScheduler::ToroidalBlueNoiseRankField:
            return "Toroidal Blue Noise (Bespoke)";
        case VisibilityPerformanceScheduler::
                FilterAdaptedSpatiotemporalRankField:
            return "Offline Spacetime Noise (Kleber)";
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
        case VisibilitySampleSpecialization::Fixed20:
            return "Fixed 20 Total, 10 Per Side";
        case VisibilitySampleSpecialization::Fixed24:
            return "Fixed 24 Total, 12 Per Side";
        case VisibilitySampleSpecialization::Fixed48:
            return "Fixed 48 Total, 24 Per Side";
        case VisibilitySampleSpecialization::Fixed64:
            return "Fixed 64 Total, 32 Per Side";
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
            return "Offline Packed Spacetime Noise (Kleber)";
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
        default:
            return "Unknown";
        }
    }

    static const char* GetRawAoStorageLabel(VisibilityRawAoStorage storage)
    {
        switch (storage)
        {
        case VisibilityRawAoStorage::ScalarFloat:
            return "Float (Buffer Drawer)";
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
        case VisibilityReconstructionMode::PackedEdges2x2:
            return "Edge-Guided Reconstruction";
        default:
            return "Unknown";
        }
    }

    static VisibilityPerformanceProfile GetPackedEdgeProfile(
        VisibilityPackedEdgeMode mode)
    {
        switch (mode)
        {
        case VisibilityPackedEdgeMode::Depth:
            return VisibilityPerformanceProfile::
                AlgorithmicPackedEdges2x2;
        case VisibilityPackedEdgeMode::DepthAndNormal:
            return VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesDepthNormal2x2;
        case VisibilityPackedEdgeMode::SlopeAdjustedDepthAndNormal:
            return VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesSlope2x2;
        case VisibilityPackedEdgeMode::ControlledLeakage:
            return VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesLeakage2x2;
        default:
            return VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesDepthNormal2x2;
        }
    }

    static VisibilityPackedEdgeMode GetPackedEdgeMode(
        VisibilityPerformanceProfile profile)
    {
        switch (profile)
        {
        case VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2:
            return VisibilityPackedEdgeMode::Depth;
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesSlope2x2:
            return VisibilityPackedEdgeMode::
                SlopeAdjustedDepthAndNormal;
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesLeakage2x2:
            return VisibilityPackedEdgeMode::ControlledLeakage;
        default:
            return VisibilityPackedEdgeMode::DepthAndNormal;
        }
    }

    static const char* GetIntelReconstructionTooltip(
        VisibilityPerformanceProfile profile)
    {
        switch (profile)
        {
        case VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2:
            return "Rank 1 of 4 separate Intel reconstruction methods: "
                "depth-guided continuity, 2.2467 ms median and 12.04% faster "
                "than Legacy Reference in the controlled Intel run.";
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesDepthNormal2x2:
            return "Rank 2 of 4: depth and surface normals protect more "
                "discontinuities, 2.2976 ms median and 10.05% faster than "
                "Legacy Reference in the controlled Intel run.";
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesSlope2x2:
            return "Rank 3 of 4: slope-aware depth and normal continuity, "
                "2.2993 ms median and 9.99% faster than Legacy Reference in "
                "the controlled Intel run.";
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesLeakage2x2:
            return "Rank 4 of 4: preserves a tiny minimum cross-edge "
                "contribution, 2.3133 ms median and 9.44% faster than Legacy "
                "Reference in the controlled Intel run.";
        default:
            return "Intel edge-guided reconstruction.";
        }
    }

    static const char* GetApplicationLabel(
        VisibilityApplicationMode application)
    {
        switch (application)
        {
        case VisibilityApplicationMode::LegacySeparateComposition:
            return "Separate Composition (Bespoke)";
        case VisibilityApplicationMode::FusedResolveAndApplyExact:
            return "Fused Apply (Bespoke) (Mutex GI)";
        case VisibilityApplicationMode::FusedResolveAndApplyPackedEdges:
            return "Fused Edge Apply (Intel) (Bespoke) (Mutex GI)";
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

    static void DrawSettingsScrollEdgeFades()
    {
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
        bool allowHeightMeasurement = true;
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
        const ImGuiID measurementFreezeKey =
            headerId ^ ImGuiID(0x42B6F19Du);
        const bool open = ImGui::TreeNodeEx(
            label,
            flags | ImGuiTreeNodeFlags_NoTreePushOnOpen);

        const int frame = ImGui::GetFrameCount();
        const int lastFrame = storage->GetInt(frameKey, -2);
        const float measuredHeight =
            storage->GetFloat(measuredHeightKey, 0.f);
        float measurementFreezeUntil =
            storage->GetFloat(measurementFreezeKey, 0.f);
        if (open &&
            measuredHeight > 0.f &&
            (std::abs(ImGui::GetIO().MouseWheel) > 0.001f ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Left)))
        {
            measurementFreezeUntil =
                float(ImGui::GetTime()) + 0.30f;
            storage->SetFloat(
                measurementFreezeKey,
                measurementFreezeUntil);
        }
        const bool allowHeightMeasurement =
            measuredHeight <= 0.f ||
            float(ImGui::GetTime()) >= measurementFreezeUntil;
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
            bodyVisible,
            allowHeightMeasurement
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
            g_NestedDrawerAnimationContext.bodyVisible &&
            g_NestedDrawerAnimationContext.allowHeightMeasurement)
        {
            const float cachedHeight =
                g_NestedDrawerAnimationContext.storage->GetFloat(
                    g_NestedDrawerAnimationContext.measuredHeightKey,
                    0.f);
            float nextHeight = measuredHeight;
            if (cachedHeight > 0.f &&
                std::abs(nextHeight - cachedHeight) > 0.5f)
            {
                const float blend =
                    1.f -
                    std::exp(
                        -18.f *
                        std::max(0.f, ImGui::GetIO().DeltaTime));
                nextHeight +=
                    (cachedHeight - nextHeight) *
                    (1.f - blend);
            }
            g_NestedDrawerAnimationContext.storage->SetFloat(
                g_NestedDrawerAnimationContext.measuredHeightKey,
                nextHeight);
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

    struct UiMutexFadeState
    {
        float linearAmount = 0.f;
        bool targetMutex = false;
        bool initialized = false;
        int lastSeenFrame = -1;
        int transitionFrame = -1;
        int advancedFrame = -1;
    };

    inline static std::unordered_map<ImGuiID, UiMutexFadeState>
        g_UiMutexFadeStates;

    struct PendingAliasingUiAction
    {
        std::function<void()> apply;
        int requestFrame = -1;
    };

    inline static PendingAliasingUiAction
        g_PendingAliasingUiAction;

    static void QueueAliasingUiAction(
        std::function<void()> action)
    {
        g_PendingAliasingUiAction.apply = std::move(action);
        g_PendingAliasingUiAction.requestFrame =
            ImGui::GetFrameCount();
    }

    static void ApplyPendingAliasingUiAction()
    {
        if (!g_PendingAliasingUiAction.apply ||
            ImGui::GetFrameCount() <=
                g_PendingAliasingUiAction.requestFrame)
        {
            return;
        }

        std::function<void()> action =
            std::move(g_PendingAliasingUiAction.apply);
        g_PendingAliasingUiAction = {};
        action();
    }

    static float GetUiMutexFade(
        ImGuiID id,
        bool mutex)
    {
        UiMutexFadeState& state = g_UiMutexFadeStates[id];
        const int frame = ImGui::GetFrameCount();
        const bool submissionWasInterrupted =
            state.lastSeenFrame >= 0 &&
            state.lastSeenFrame < frame - 2;

        if (!state.initialized || submissionWasInterrupted)
        {
            state.linearAmount = mutex ? 1.f : 0.f;
            state.targetMutex = mutex;
            state.initialized = true;
            state.transitionFrame = frame;
        }
        else if (state.targetMutex != mutex)
        {
            state.targetMutex = mutex;
            state.transitionFrame = frame;
        }

        // A queued combo choice is committed only after its popup has closed.
        // Preserve the old endpoint for that complete first new-state frame,
        // then advance once per later rendered frame. This keeps every row in
        // sync and prevents a resource-rebuild hitch from consuming the fade.
        if (frame > state.transitionFrame &&
            state.advancedFrame != frame)
        {
            constexpr float TransitionDuration = 0.30f;
            const float animationDeltaTime = std::min(
                std::max(0.f, ImGui::GetIO().DeltaTime),
                1.f / 30.f);
            const float step =
                animationDeltaTime / TransitionDuration;
            state.linearAmount = state.targetMutex
                ? std::min(1.f, state.linearAmount + step)
                : std::max(0.f, state.linearAmount - step);
            state.advancedFrame = frame;
        }

        state.lastSeenFrame = frame;
        return state.linearAmount *
            state.linearAmount *
            (3.f - 2.f * state.linearAmount);
    }

    static void BeginAnimatedMutex(
        const char* id,
        bool mutex)
    {
        const float amount =
            GetUiMutexFade(ImGui::GetID(id), mutex);
        const ImGuiStyle& style = ImGui::GetStyle();

        // A new mutex disables interaction immediately while opacity moves
        // continuously between the normal and disabled endpoints. Keeping an
        // item disabled for the short fade-out makes BeginDisabled own alpha
        // exactly once, including when this scope is nested inside the
        // motion-test-wide disabled scope.
        const float opacity =
            1.f + (style.DisabledAlpha - 1.f) * amount;
        ImGui::PushStyleVar(
            ImGuiStyleVar_DisabledAlpha,
            opacity);
        ImGui::BeginDisabled(mutex || amount > 0.f);
    }

    static void EndAnimatedMutex()
    {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }

    static void BeginAnimatedMutexPreview(
        const char* id,
        bool mutex)
    {
        const float amount =
            GetUiMutexFade(ImGui::GetID(id), mutex);
        const ImGuiStyle& style = ImGui::GetStyle();
        const bool parentAlreadyDisabled =
            style.Alpha <= style.DisabledAlpha + 1e-4f;
        const float opacity = parentAlreadyDisabled
            ? 1.f
            : 1.f + (style.DisabledAlpha - 1.f) * amount;
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            style.Alpha * opacity);
    }

    static void EndAnimatedMutexPreview()
    {
        ImGui::PopStyleVar();
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

        if (snapshot.hasVisibilityTimings)
        {
            const ScreenSpaceVisibilityTimings* timings =
                &snapshot.visibilityTimings;
            const float traceMilliseconds =
                timings->depthHierarchyMs + timings->samplingMs;
            const float filteringMilliseconds =
                timings->spatialDenoiseMs +
                timings->fusedSpatialDenoiseUpsampleMs +
                timings->requiredUpsampleMs;
            const float otherMilliseconds =
                timings->temporalMs +
                timings->fullResolutionApplyMs +
                timings->compositionMs;
            FormatStatLine(
                m_VisibilityStatLines[0],
                "All %.1f / Trace %.1f / Filter %.1f / Other %.1f ms",
                timings->CompleteEffectMs(),
                traceMilliseconds,
                filteringMilliseconds,
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
            return "Later Bounces";
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
            return "Filtered Spatial Reconstruction";
        case VisibilityBenchmarkStage::RequiredUpsample:
            return "Guide-Aware Upsampling";
        case VisibilityBenchmarkStage::FullResolutionApply:
            return "Fused Apply";
        case VisibilityBenchmarkStage::Composition:
            return "Indirect-Lighting Composition";
        case VisibilityBenchmarkStage::EffectEnvelope:
            return "Complete Visibility Pipeline";
        default:
            return "Unknown Stage";
        }
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
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 crosshairCenter(
            viewport->Pos.x + viewport->Size.x * 0.5f,
            viewport->Pos.y + viewport->Size.y * 0.5f);
        ImGui::GetForegroundDrawList()->AddCircleFilled(
            crosshairCenter,
            2.f,
            IM_COL32(255, 255, 255, 128),
            12);
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

        ApplyReferenceStyle();
        int width, height;
        GetDeviceManager()->GetWindowDimensions(width, height);
        const bool visibilityBenchmarkBusy =
            m_app->IsVisibilityBenchmarkQueued() ||
            m_app->IsVisibilityBenchmarkActive();

        if (visibilityBenchmarkBusy)
        {
            static const char* const benchmarkDots[] = {
                ".",
                "..",
                "..."
            };
            const int benchmarkDotIndex =
                int(ImGui::GetTime() * 2.0) % int(std::size(benchmarkDots));
            const uint32_t benchmarkRequestedFrames =
                m_app->GetVisibilityBenchmarkRequestedFrameCount();
            const uint32_t benchmarkCompletedFrames = std::min(
                m_app->GetVisibilityBenchmarkCompletedFrameCount(),
                benchmarkRequestedFrames);
            char benchmarkLabel[64];
            snprintf(
                benchmarkLabel,
                std::size(benchmarkLabel),
                "Benchmarking%s (%u/%u)",
                benchmarkDots[benchmarkDotIndex],
                benchmarkCompletedFrames,
                benchmarkRequestedFrames);

            ImGui::SetNextWindowPos(
                ImVec2(float(width) - 12.f, 12.f),
                ImGuiCond_Always,
                ImVec2(1.f, 0.f));
            ImGui::SetNextWindowBgAlpha(0.82f);
            ImGui::PushFont(m_Font->GetScaledFont());
            ImGui::Begin(
                "##VisibilityBenchmarkActivity",
                nullptr,
                ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoInputs);
            ImGui::TextUnformatted(benchmarkLabel);
            ImGui::End();
            ImGui::PopFont();
        }

        if (!m_ui.ShowUI)
            return;

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
                for (std::string& value : m_VisibilityStatLines)
                    value.clear();
                for (std::string& value : m_TemporalAAStatLines)
                    value.clear();
                m_HasAppliedStatSnapshot = false;
                m_HasGpuStatSnapshot = false;
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
        const float settingsPanelMarginPixels = std::max(
            1.f,
            std::round(fontSize * 0.6f));
        const float availableWindowWidth =
            std::max(
                1.f,
                float(width) - settingsPanelMarginPixels * 2.f);
        const float settingsWindowWidth = std::min(
            std::max(statusContentWidth, minimumSettingsContentWidth) +
                style.WindowPadding.x * 2.f +
                settingsWidthReadabilityAllowance,
            availableWindowWidth);
        ImGui::SetNextWindowPos(
            ImVec2(
                settingsPanelMarginPixels,
                settingsPanelMarginPixels),
            ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(settingsWindowWidth, 0.f),
            ImGuiCond_Always);
        constexpr float StatusLineSpacing = 2.f;
        const bool hasPerformanceStatus =
            !m_PerformanceStatValues[1].empty();
        const float settingsCollapsedHeight =
            fontSize + style.FramePadding.y * 2.f +
            style.WindowPadding.y +
            fontSize +
            style.ItemSpacing.y +
            1.f +
            (hasPerformanceStatus
                ? StatusLineSpacing + fontSize
                : 0.f);
        ImGui::SetNextSettingsWindowCollapsedHeight(
            settingsCollapsedHeight);
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
        if (hasPerformanceStatus)
        {
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() - style.ItemSpacing.y +
                    StatusLineSpacing);
            ImGui::TextUnformatted(performanceLine.c_str());
            ImGui::SetItemTooltip(
                "Bandwidth is the current theoretical limit. "
                "tflops is current-clock FP32 peak scaled by GPU utilization.");
        }
        if (visibilityBenchmarkBusy)
        {
            ImGui::TextDisabled(
                "Benchmark Environment Locked; Cancel Under Statistics");
        }

        ImGui::Separator();

        const float settingsBodyMaxHeight = std::max(
            1.f,
            float(height) - settingsPanelMarginPixels -
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
        if (visibilityBenchmarkBusy)
            ImGui::EndDisabled();
        EndDrawerBody();
        }
        ImGui::Spacing();

        const bool indirectLightingOpen = DrawCollapsingHeader(
            "Visibility",
            "Configure ambient occlusion, diffuse indirect lighting, "
            "sampling, and reconstruction.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (indirectLightingOpen)
        {
            BeginDrawerBody(
                "##VisibilityBody",
                settingsControlWidth);
            ScreenSpaceVisibilitySettings& visibility = m_ui.ScreenSpaceVisibility;
            const bool visibilityAvailable =
                m_ui.IsScreenSpaceVisibilityAvailable();
            if (!visibilityAvailable)
                ImGui::BeginDisabled();
            if (visibilityBenchmarkBusy)
                ImGui::BeginDisabled();

            const ScreenSpaceVisibilityTimings* visibilityTimings =
                m_app->GetScreenSpaceVisibilityTimings();
            const VisibilityPerformanceWorkload observedWorkload =
                GetRenderedVisibilityPerformanceWorkload(
                    visibility,
                    uint32_t(std::max(width, 0)),
                    uint32_t(std::max(height, 0)),
                    visibilityTimings);
            const VisibilityPerformanceProfileConfiguration
                activeConfiguration =
                    GetEffectiveVisibilityPerformanceConfiguration(
                        visibility);

            auto applyQualityPreset =
                [&](ScreenSpaceVisibilityQuality quality)
                {
                    ApplyScreenSpaceVisibilityQualityPreset(
                        visibility, quality);
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                    m_ui.EnablePbr = true;
                };
            auto switchVisibilityToCustom = [&]()
                {
                    MakeVisibilityPerformanceComposable(visibility);
                    visibility.quality =
                        ScreenSpaceVisibilityQuality::Custom;
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                };
            auto editableConfiguration = [&]()
                -> VisibilityPerformanceProfileConfiguration&
                {
                    switchVisibilityToCustom();
                    return visibility.performance.configuration;
                };
            auto applyTraceCategory =
                [&](VisibilityPerformanceProfile profile)
                {
                    const auto source =
                        GetVisibilityPerformanceProfileConfiguration(profile);
                    auto& target = editableConfiguration();
                    target.trace = source.trace;
                    target.firstBounceSamples =
                        source.firstBounceSamples;
                    target.laterBounceSamples =
                        source.firstBounceSamples;
                    target.bindings = VisibilityBindingStrategy::
                        MinimalConditional;
                    target.estimatorRequirement =
                        VisibilityEstimatorRequirement::Any;
                    target.consumerRequirement =
                        VisibilityConsumerRequirement::Any;
                    target.benchmarkOnly = false;
                    const uint32_t fixedCount =
                        GetVisibilityFixedSampleCount(
                            source.firstBounceSamples);
                    if (fixedCount != 0u)
                    {
                        visibility.sampling.maximumSampleCount =
                            fixedCount;
                        visibility.sampling.stepDistributionExponent =
                            2.f;
                    }
                };
            auto applyNoiseCategory =
                [&](VisibilityPerformanceProfile profile)
                {
                    const auto source =
                        GetVisibilityPerformanceProfileConfiguration(profile);
                    auto& target = editableConfiguration();
                    target.noise = source.noise;
                    target.bindings = VisibilityBindingStrategy::
                        MinimalConditional;
                    if (source.noise ==
                        VisibilityNoiseDelivery::PackedCurrentFast)
                    {
                        visibility.sampling.scheduler =
                            VisibilitySampleScheduler::
                                FilterAdaptedSpatiotemporalRankField;
                    }
                };
            auto applyReconstructionCategory =
                [&](VisibilityPerformanceProfile profile)
                {
                    const auto source =
                        GetVisibilityPerformanceProfileConfiguration(profile);
                    auto& target = editableConfiguration();
                    target.reconstruction = source.reconstruction;
                    target.edgeStorage = source.edgeStorage;
                    if (source.reconstruction ==
                        VisibilityReconstructionMode::PackedEdges2x2)
                    {
                        visibility.reconstruction.spatialEnabled = false;
                    }
                    visibility.performance.packedEdgeMode =
                        GetPackedEdgeMode(profile);
                };
            auto applyApplicationCategory =
                [&](VisibilityPerformanceProfile profile)
                {
                    const auto source =
                        GetVisibilityPerformanceProfileConfiguration(profile);
                    auto& target = editableConfiguration();
                    target.application = source.application;
                    target.explicitHalfRoundtrip =
                        source.explicitHalfRoundtrip;
                    const bool fused =
                        source.application ==
                                VisibilityApplicationMode::
                                    FusedResolveAndApplyExact ||
                        source.application ==
                                VisibilityApplicationMode::
                                    FusedResolveAndApplyPackedEdges;
                    target.consumerRequirement = fused
                        ? VisibilityConsumerRequirement::AmbientOcclusionOnly
                        : VisibilityConsumerRequirement::Any;
                    target.resolutionRequirement = fused
                        ? VisibilityResolutionRequirement::Reduced
                        : VisibilityResolutionRequirement::Any;
                    if (fused)
                    {
                        visibility.indirectDiffuse.enabled = false;
                        visibility.reconstruction.spatialEnabled = false;
                        if (source.reconstruction !=
                            VisibilityReconstructionMode::Legacy)
                        {
                            target.reconstruction =
                                source.reconstruction;
                            target.edgeStorage = source.edgeStorage;
                        }
                    }
                };
            auto keepApplicationCompatibleWithConsumers = [&]()
                {
                    auto& target = editableConfiguration();
                    const bool fused =
                        target.application ==
                                VisibilityApplicationMode::
                                    FusedResolveAndApplyExact ||
                        target.application ==
                                VisibilityApplicationMode::
                                    FusedResolveAndApplyPackedEdges;
                    if (!fused)
                        return;
                    if (visibility.HasActiveAmbientOcclusion() &&
                        !visibility.HasActiveIndirectDiffuse())
                    {
                        return;
                    }

                    // The fused kernels write an AO-composited lighting
                    // target and have no GI output. Preserve every trace,
                    // noise, edge, precision, and reconstruction choice while
                    // changing only the final application stage.
                    target.application =
                        VisibilityApplicationMode::
                            LegacySeparateComposition;
                    target.explicitHalfRoundtrip = false;
                    target.consumerRequirement =
                        VisibilityConsumerRequirement::Any;
                    target.resolutionRequirement =
                        target.reconstruction ==
                                VisibilityReconstructionMode::PackedEdges2x2
                            ? VisibilityResolutionRequirement::Reduced
                            : VisibilityResolutionRequirement::Any;
                };

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

            const std::string_view activeImplementationName =
                GetPerformanceProfileUiLabel(
                    visibility.performanceProfile);
            static const char* qualityPresetLabels[] = {
                "Low", "Medium", "High", "Ultra"
            };
            static const char* qualityPresetDescriptions[] = {
                "Quarter resolution, Uniform Projected Angle, exact 8 "
                    "samples, Offline Packed Spacetime Noise, compact "
                    "depth-normal joint-bilateral upsampling, and one GI "
                    "bounce.",
                "Half resolution, exact 8 samples, Offline Packed Spacetime "
                    "Noise, compact depth-normal joint-bilateral upsampling, "
                    "and one GI bounce.",
                "Factory default: full resolution, exact 20 samples, "
                    "Offline Packed Spacetime Noise, Default Precision "
                    "buffers, and one GI bounce.",
                "Full resolution, exact 48 samples, Offline Packed "
                    "Spacetime Noise, Default Precision buffers, and two "
                    "GI bounces."
            };
            const bool qualityPresetSelected =
                visibility.quality != ScreenSpaceVisibilityQuality::Custom;
            const char* selectedProfileName = qualityPresetSelected
                ? qualityPresetLabels[int(visibility.quality)]
                : activeImplementationName.empty()
                    ? "Custom / Advanced"
                    : activeImplementationName.data();
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo("Profile", selectedProfileName))
            {
                ImGui::TextDisabled("Quality Presets");
                for (int presetIndex = 0;
                    presetIndex < int(std::size(qualityPresetLabels));
                    ++presetIndex)
                {
                    const auto quality =
                        ScreenSpaceVisibilityQuality(presetIndex);
                    const bool selected = visibility.quality == quality;
                    if (ImGui::Selectable(
                            qualityPresetLabels[presetIndex], selected))
                    {
                        applyQualityPreset(quality);
                    }
                    ImGui::SetItemTooltip(
                        "%s", qualityPresetDescriptions[presetIndex]);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose Low, Medium, High, or Ultra. Later edits remain "
                "active and change the label to Custom / Advanced.");

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
                static const char* schedulerLabels[] = {
                    "Independent Hash Noise (Bespoke)",
                    "Toroidal Blue Noise (Bespoke)",
                    "Offline Spacetime Noise (Kleber)"
                };
                const char* noisePatternLabel =
                    activeConfiguration.noise ==
                            VisibilityNoiseDelivery::PackedCurrentFast
                        ? GetNoiseDeliveryLabel(activeConfiguration.noise)
                        : GetSchedulerLabel(observedWorkload.scheduler);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Noise Pattern",
                        noisePatternLabel))
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
                            applyNoiseCategory(
                                VisibilityPerformanceProfile::
                                    GenericFallback);
                            sampling.scheduler = scheduler;
                            samplingChanged = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::Separator();
                    constexpr VisibilityPerformanceProfile packedNoiseProfile =
                        VisibilityPerformanceProfile::ExactPackedCurrentFast;
                    const auto packedNoiseConfiguration =
                        GetVisibilityPerformanceProfileConfiguration(
                            packedNoiseProfile);
                    const bool packedNoiseSelected =
                        activeConfiguration.noise ==
                            packedNoiseConfiguration.noise;
                    if (ImGui::Selectable(
                            GetNoiseDeliveryLabel(
                                packedNoiseConfiguration.noise),
                            packedNoiseSelected))
                    {
                        applyNoiseCategory(packedNoiseProfile);
                    }
                    ImGui::SetItemTooltip(
                        "Use the same offline-computed values as the scalar "
                        "option, prepacked into one RGBA8 lookup. This is an "
                        "exact delivery change for every selectable exact "
                        "sample count.");
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose both the sample sequence and its delivery. The "
                    "packed option is computed offline and fetched as one "
                    "four-channel value.");

                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Exact Sample Count (Therrien) (Bespoke)",
                        GetSampleSpecializationLabel(
                            activeConfiguration.firstBounceSamples)))
                {
                    const VisibilityPerformanceProfile fixedProfiles[] = {
                        VisibilityPerformanceProfile::ExactFixed8,
                        VisibilityPerformanceProfile::ExactFixed12,
                        VisibilityPerformanceProfile::ExactFixed16,
                        VisibilityPerformanceProfile::ExactFixed20,
                        VisibilityPerformanceProfile::ExactFixed24,
                        VisibilityPerformanceProfile::ExactFixed48,
                        VisibilityPerformanceProfile::ExactFixed64
                    };
                    for (VisibilityPerformanceProfile profile : fixedProfiles)
                    {
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                profile);
                        if (ImGui::Selectable(
                                GetPerformanceProfileUiLabel(
                                    profile).data(),
                                activeConfiguration.firstBounceSamples ==
                                    configuration.firstBounceSamples))
                        {
                            applyTraceCategory(profile);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose one compiled exact count. AO and every GI bounce "
                    "use this same total count, split evenly across both "
                    "trace directions.");

                samplingChanged |= DrawSliderFloat(
                    "Radius", &sampling.radius, 0.01f, std::max(m_app->GetSceneDiagonal() * 0.1f, 1.f), "%.3f");
                ImGui::SetItemTooltip("Set how far visibility rays reach.");
                samplingChanged |= DrawSliderFloat(
                    "Thickness", &sampling.thickness, 0.0f, std::max(m_app->GetSceneDiagonal() * 0.02f, 0.5f), "%.3f");
                ImGui::SetItemTooltip("Set the assumed thickness of occluders.");

                samplingChanged |= DrawSliderFloat(
                    "Radial Distribution Exponent",
                    &sampling.stepDistributionExponent,
                    0.5f,
                    4.0f,
                    "%.2f");
                ImGui::SetItemTooltip("Higher values place more samples nearby.");

                if (samplingChanged)
                {
                    visibility.quality = ScreenSpaceVisibilityQuality::Custom;
                    MakeVisibilityPerformanceComposable(visibility);
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Ambient Occlusion",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                AmbientOcclusionSettings& ao = visibility.ambientOcclusion;
                bool aoChanged = ImGui::Checkbox(
                    "Enabled##AmbientVisibility", &ao.enabled);
                ImGui::SetItemTooltip("Enable screen-space ambient occlusion.");
                if (!ao.enabled)
                    ImGui::BeginDisabled();
                aoChanged |= DrawSliderFloat(
                    "Strength", &ao.strength, 0.0f, 2.0f, "%.2f");
                ImGui::SetItemTooltip("Set how strongly AO darkens indirect light.");
                aoChanged |= ImGui::SliderFloat(
                    "Power", &ao.power, 0.1f, 4.0f, "%.2f");
                ImGui::SetItemTooltip(
                    "Shape the AO response. One preserves the traced result; "
                    "higher values deepen occlusion and lower values soften it.");
                if (!ao.enabled)
                    ImGui::EndDisabled();
                if (aoChanged)
                {
                    switchVisibilityToCustom();
                    keepApplicationCompatibleWithConsumers();
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Indirect Diffuse",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                IndirectDiffuseSettings& gi = visibility.indirectDiffuse;
                bool giChanged = ImGui::Checkbox(
                    "Enabled##IndirectDiffuse", &gi.enabled);
                ImGui::SetItemTooltip("Enable screen-space diffuse indirect light.");
                if (!gi.enabled)
                    ImGui::BeginDisabled();
                if (ImGui::Checkbox(
                        "Limit Bounces", &gi.limitBounces))
                {
                    if (!gi.limitBounces)
                    {
                        gi.minimumBounceContribution = std::max(
                            gi.minimumBounceContribution,
                            MinimumContributionTerminatedThreshold);
                    }
                    giChanged = true;
                }
                ImGui::SetItemTooltip(
                    "On: stop at the selected bounce count. Off: continue "
                    "on the GPU while a bounce clears an exponentially "
                    "stricter contribution threshold; a 16-bounce fault "
                    "guard contains malformed or non-converging data.");
                if (!gi.limitBounces)
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
                    giChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Set the explicit diffuse-light bounce count.");
                if (!gi.limitBounces)
                    ImGui::EndDisabled();
                if (!gi.limitBounces || gi.bounceCount > 1u)
                {
                    giChanged |= DrawSliderFloat(
                        gi.limitBounces
                            ? "Bounce Contribution Cutoff"
                            : "Starting Contribution Cutoff",
                        &gi.minimumBounceContribution,
                        gi.limitBounces
                            ? 0.0f
                            : MinimumContributionTerminatedThreshold,
                        MaximumBounceContributionCutoff,
                        "%.5f");
                    ImGui::SetItemTooltip(
                        gi.limitBounces
                            ? "Skip dim higher-bounce light. Zero disables "
                                "the cutoff."
                            : "The continuation bar starts here and becomes "
                                "four times stricter after every bounce.");
                }
                giChanged |= DrawSliderFloat(
                    "Intensity##IndirectDiffuse",
                    &gi.intensity,
                    0.0f,
                    10.0f,
                    "%.2f");
                ImGui::SetItemTooltip("Set screen-space diffuse GI brightness.");
                if (!gi.enabled)
                    ImGui::EndDisabled();
                if (giChanged)
                {
                    switchVisibilityToCustom();
                    keepApplicationCompatibleWithConsumers();
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Reconstruction##VisibilityReconstruction"))
            {
                VisibilityReconstructionSettings& reconstruction =
                    visibility.reconstruction;
                const bool legacyReconstruction =
                    activeConfiguration.reconstruction ==
                        VisibilityReconstructionMode::Legacy;
                const bool fullResolutionWithoutReconstruction =
                    legacyReconstruction &&
                    !reconstruction.spatialEnabled &&
                    visibility.resolution == VisibilityResolution::Full;
                const bool reducedResolutionUpsampling =
                    legacyReconstruction &&
                    !reconstruction.spatialEnabled &&
                    visibility.resolution != VisibilityResolution::Full;
                const char* reconstructionLabel =
                    GetReconstructionLabel(
                        activeConfiguration.reconstruction);
                if (activeConfiguration.reconstruction ==
                    VisibilityReconstructionMode::Legacy)
                {
                    reconstructionLabel = reconstruction.spatialEnabled
                        ? (reconstruction.spatialFilter ==
                                VisibilitySpatialFilter::
                                    GaussianJointBilateral
                            ? "Gaussian Joint Bilateral Reconstruction "
                                "(cdrinmatane)"
                            : "Joint Bilateral Reconstruction (Bespoke)")
                        : (fullResolutionWithoutReconstruction
                            ? "Unreconstructed Full Resolution Input "
                                "(Bespoke)"
                            : "Guide-Aware Upsampling (Bespoke)");
                }
                else
                {
                    reconstructionLabel = GetPerformanceProfileUiLabel(
                        GetPackedEdgeProfile(
                            visibility.performance.packedEdgeMode)).data();
                }
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Reconstruction Method",
                        reconstructionLabel))
                {
                    if (ImGui::Selectable(
                            "Unreconstructed Full Resolution Input (Bespoke)",
                            fullResolutionWithoutReconstruction))
                    {
                        applyReconstructionCategory(
                            VisibilityPerformanceProfile::
                                GenericFallback);
                        applyApplicationCategory(
                            VisibilityPerformanceProfile::
                                GenericFallback);
                        visibility.resolution = VisibilityResolution::Full;
                        reconstruction.spatialEnabled = false;
                    }
                    ImGui::SetItemTooltip(
                        "Trace visibility at the output resolution and bypass "
                        "the spatial reconstruction pass. No upsampling is "
                        "required; filtering remains optional.");
                    if (ImGui::Selectable(
                            "Guide-Aware Upsampling (Bespoke)",
                            reducedResolutionUpsampling))
                    {
                        applyReconstructionCategory(
                            VisibilityPerformanceProfile::
                                GenericFallback);
                        applyApplicationCategory(
                            VisibilityPerformanceProfile::
                                GenericFallback);
                        if (visibility.resolution ==
                            VisibilityResolution::Full)
                        {
                            visibility.resolution =
                                VisibilityResolution::Half;
                        }
                        reconstruction.spatialEnabled = false;
                    }
                    ImGui::SetItemTooltip(
                        "Reconstruct reduced-resolution visibility at output "
                        "resolution with the minimum guide-aware pass and no "
                        "optional denoising.");
                    if (ImGui::Selectable(
                            "Joint Bilateral Reconstruction (Bespoke)",
                            legacyReconstruction &&
                                reconstruction.spatialEnabled &&
                                reconstruction.spatialFilter ==
                                    VisibilitySpatialFilter::JointBilateral))
                    {
                        applyReconstructionCategory(
                            VisibilityPerformanceProfile::
                                GenericFallback);
                        applyApplicationCategory(
                            VisibilityPerformanceProfile::
                                GenericFallback);
                        reconstruction.spatialEnabled = true;
                        reconstruction.spatialFilter =
                            VisibilitySpatialFilter::JointBilateral;
                    }
                    ImGui::SetItemTooltip(
                        "Denoise while reconstructing, using depth and normal "
                        "guides to reduce cross-edge bleeding.");
                    if (ImGui::Selectable(
                            "Gaussian Joint Bilateral Reconstruction "
                                "(cdrinmatane)",
                            legacyReconstruction &&
                                reconstruction.spatialEnabled &&
                                reconstruction.spatialFilter ==
                                    VisibilitySpatialFilter::
                                        GaussianJointBilateral))
                    {
                        applyReconstructionCategory(
                            VisibilityPerformanceProfile::
                                GenericFallback);
                        applyApplicationCategory(
                            VisibilityPerformanceProfile::
                                GenericFallback);
                        reconstruction.spatialEnabled = true;
                        reconstruction.spatialFilter =
                            VisibilitySpatialFilter::
                                GaussianJointBilateral;
                    }
                    ImGui::SetItemTooltip(
                        "Use a wider Gaussian-weighted depth-and-normal "
                        "reconstruction kernel. The radius appears below.");
                    ImGui::Separator();
                    ImGui::TextDisabled("Intel Edge-Guided Reconstruction");
                    const VisibilityPerformanceProfile denoiserProfiles[] = {
                        VisibilityPerformanceProfile::
                            AlgorithmicPackedEdges2x2,
                        VisibilityPerformanceProfile::
                            AlgorithmicPackedEdgesDepthNormal2x2,
                        VisibilityPerformanceProfile::
                            AlgorithmicPackedEdgesSlope2x2,
                        VisibilityPerformanceProfile::
                            AlgorithmicPackedEdgesLeakage2x2
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
                        const bool selected =
                            activeConfiguration.reconstruction ==
                                configuration.reconstruction &&
                            visibility.performance.packedEdgeMode ==
                                GetPackedEdgeMode(profile);
                        if (ImGui::Selectable(
                                profileUiLabel.data(),
                                selected) &&
                            available)
                        {
                            applyReconstructionCategory(profile);
                            applyApplicationCategory(
                                VisibilityPerformanceProfile::
                                    GenericFallback);
                            if (visibility.resolution ==
                                VisibilityResolution::Full)
                            {
                                visibility.resolution =
                                    VisibilityResolution::Half;
                            }
                        }
                        ImGui::SetItemTooltip(
                            "%s", GetIntelReconstructionTooltip(profile));
                        if (!available)
                            ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose whether visibility bypasses reconstruction, is "
                    "upsampled, or is denoised while being reconstructed.");
                if (activeConfiguration.reconstruction ==
                        VisibilityReconstructionMode::Legacy &&
                    reconstruction.spatialEnabled &&
                    reconstruction.spatialFilter ==
                        VisibilitySpatialFilter::GaussianJointBilateral)
                {
                    DrawSliderFloat(
                        "Filter Radius##GaussianRadius",
                        &reconstruction.spatialRadius,
                        1.0f,
                        12.0f,
                        "%.1f");
                    ImGui::SetItemTooltip(
                        "Set how far the Gaussian denoiser reaches.");
                }

                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Final Application",
                        GetApplicationLabel(activeConfiguration.application)))
                {
                    if (ImGui::Selectable(
                            "Separate Composition (Bespoke)",
                            activeConfiguration.application ==
                                VisibilityApplicationMode::
                                    LegacySeparateComposition))
                    {
                        applyApplicationCategory(
                            VisibilityPerformanceProfile::
                                GenericFallback);
                    }
                    struct ApplicationOption
                    {
                        VisibilityPerformanceProfile profile;
                        const char* label;
                    };
                    const ApplicationOption applicationOptions[] = {
                        {
                            VisibilityPerformanceProfile::
                                ExactFusedResolveApply,
                            "Fused Apply (Bespoke) (Mutex GI)"
                        },
                        {
                            VisibilityPerformanceProfile::
                                AlgorithmicFusedPackedEdges2x2,
                            "Fused Edge Apply (Intel) (Bespoke) (Mutex GI)"
                        }
                    };
                    for (const ApplicationOption& option :
                        applicationOptions)
                    {
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                option.profile);
                        if (ImGui::Selectable(
                                option.label,
                                activeConfiguration.application ==
                                    configuration.application))
                        {
                            applyApplicationCategory(option.profile);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose separate composition or a fused resolve-and-"
                    "apply dispatch. Fused work is timed as one pass.");
                EndAnimatedTreeNode();
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
            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool bufferConfigurationOpen = DrawCollapsingHeader(
            "Buffers",
            "Choose visibility-buffer presets or configure each active "
            "AO and GI surface independently.");
        if (bufferConfigurationOpen)
        {
            if (visibilityBenchmarkBusy)
                ImGui::BeginDisabled();

            auto& buffers =
                m_ui.ScreenSpaceVisibility.performance.bufferPrecision;
            using ScalarPrecision = VisibilityScalarBufferPrecision;
            using VectorPrecision = VisibilityVectorBufferPrecision;
            const bool ao16 =
                buffers.rawAmbient == ScalarPrecision::Float16 &&
                buffers.temporalAmbient == ScalarPrecision::Float16 &&
                buffers.finalAmbient == ScalarPrecision::Float16 &&
                buffers.depthHierarchy == ScalarPrecision::Float16;
            const bool ao32 =
                buffers.rawAmbient == ScalarPrecision::Float32 &&
                buffers.temporalAmbient == ScalarPrecision::Float32 &&
                buffers.finalAmbient == ScalarPrecision::Float32 &&
                buffers.depthHierarchy == ScalarPrecision::Float32;
            const bool gi16 =
                buffers.rawIndirect == VectorPrecision::Rgba16Float &&
                buffers.cumulativeIndirect == VectorPrecision::Rgba16Float &&
                buffers.temporalIndirect == VectorPrecision::Rgba16Float &&
                buffers.finalIndirect == VectorPrecision::Rgba16Float;
            const bool gi32 =
                buffers.rawIndirect == VectorPrecision::Rgba32Float &&
                buffers.cumulativeIndirect == VectorPrecision::Rgba32Float &&
                buffers.temporalIndirect == VectorPrecision::Rgba32Float &&
                buffers.finalIndirect == VectorPrecision::Rgba32Float;

            const char* bufferPresetLabel = "Custom";
            if (ao16 && gi16)
                bufferPresetLabel = "Performance Precision";
            else if (ao32 && gi32)
                bufferPresetLabel = "Default Precision";
            else if (ao16 && gi32)
                bufferPresetLabel = "Compact AO";
            else if (ao32 && gi16)
                bufferPresetLabel = "Compact GI";

            const auto applyBufferPreset =
                [&](bool use16BitAo, bool use16BitGi)
                {
                    ApplyVisibilityBufferPrecisionPreset(
                        buffers, use16BitAo, use16BitGi);
                    m_ui.ScreenSpaceVisibility.quality =
                        ScreenSpaceVisibilityQuality::Custom;
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                };

            ImGui::TextUnformatted("Preset");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##BufferPreset", bufferPresetLabel))
            {
                if (ImGui::Selectable(
                        "Performance Precision", ao16 && gi16))
                {
                    applyBufferPreset(true, true);
                }
                ImGui::SetItemTooltip(
                    "Use the measured bandwidth-saving formats for every "
                    "active AO and GI buffer.");
                if (ImGui::Selectable(
                        "Default Precision", ao32 && gi32))
                {
                    applyBufferPreset(false, false);
                }
                ImGui::SetItemTooltip(
                    "Use the full-precision formats selected by High and Ultra.");
                if (ImGui::Selectable(
                        "Compact AO", ao16 && gi32))
                {
                    applyBufferPreset(true, false);
                }
                if (ImGui::Selectable(
                        "Compact GI", ao32 && gi16))
                {
                    applyBufferPreset(false, true);
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Low and Medium begin at Performance Precision; High and "
                "Ultra begin at Default Precision. A later buffer edit keeps "
                "every other setting and changes the Profile label to "
                "Custom / Advanced.");

            static const char* scalarPrecisionLabels[] = {
                "Half Precision (16-bit)",
                "Full Precision (32-bit)"
            };
            static const char* vectorPrecisionLabels[] = {
                "Half Precision RGBA (64-bit)",
                "Full Precision RGBA (128-bit)"
            };
            const auto drawScalarPrecision =
                [&](const char* label,
                    VisibilityScalarBufferPrecision& precision,
                    const char* tooltip)
                {
                    ImGui::TextUnformatted(label);
                    ImGui::PushID(label);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo(
                            "##Precision",
                            scalarPrecisionLabels[
                                static_cast<uint32_t>(precision)]))
                    {
                        for (uint32_t index = 0u;
                            index < std::size(scalarPrecisionLabels);
                            ++index)
                        {
                            const auto candidate =
                                static_cast<
                                    VisibilityScalarBufferPrecision>(index);
                            if (ImGui::Selectable(
                                    scalarPrecisionLabels[index],
                                    precision == candidate))
                            {
                                precision = candidate;
                                m_ui.ScreenSpaceVisibility.quality =
                                    ScreenSpaceVisibilityQuality::Custom;
                                m_ui.VisibilityVerification =
                                    VisibilityVerificationProfile::Unset;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip("%s", tooltip);
                    ImGui::PopID();
                };
            const auto drawVectorPrecision =
                [&](const char* label,
                    VisibilityVectorBufferPrecision& precision,
                    const char* tooltip)
                {
                    ImGui::TextUnformatted(label);
                    ImGui::PushID(label);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::BeginCombo(
                            "##Precision",
                            vectorPrecisionLabels[
                                static_cast<uint32_t>(precision)]))
                    {
                        for (uint32_t index = 0u;
                            index < std::size(vectorPrecisionLabels);
                            ++index)
                        {
                            const auto candidate =
                                static_cast<
                                    VisibilityVectorBufferPrecision>(index);
                            if (ImGui::Selectable(
                                    vectorPrecisionLabels[index],
                                    precision == candidate))
                            {
                                precision = candidate;
                                m_ui.ScreenSpaceVisibility.quality =
                                    ScreenSpaceVisibilityQuality::Custom;
                                m_ui.VisibilityVerification =
                                    VisibilityVerificationProfile::Unset;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip("%s", tooltip);
                    ImGui::PopID();
                };

            ImGui::SeparatorText("Individual Buffers");
            drawScalarPrecision(
                "Trace AO",
                buffers.rawAmbient,
                "Trace-resolution ambient visibility.");
            drawVectorPrecision(
                "Current Bounce GI",
                buffers.rawIndirect,
                "Trace-resolution current indirect-diffuse frontier.");
            drawVectorPrecision(
                "Accumulated GI",
                buffers.cumulativeIndirect,
                "Trace-resolution accumulated multi-bounce GI.");
            drawScalarPrecision(
                "Output AO",
                buffers.finalAmbient,
                "Full-resolution reconstructed ambient visibility.");
            drawVectorPrecision(
                "Output GI",
                buffers.finalIndirect,
                "Full-resolution reconstructed indirect diffuse.");
            drawScalarPrecision(
                "Long-Range Depth",
                buffers.depthHierarchy,
                "Five-mip depth hierarchy used by long AO traces.");
            ImGui::TextDisabled("Edge Metadata: R8");
            ImGui::SetItemTooltip(
                "The packed-edge encoding is fixed by its shader channel "
                "contract.");

            if (visibilityBenchmarkBusy)
                ImGui::EndDisabled();
        }

        const bool visibilityStatisticsOpen = DrawCollapsingHeader(
            "Statistics",
            "Inspect one renderer effect at a time and run benchmarks.");
        if (visibilityStatisticsOpen)
        {
            enum class StatisticsEffect : int
            {
                CompleteRenderer,
                Geometry,
                DirectLighting,
                ScreenSpaceVisibility,
                MaterialPicking,
                ProceduralSky,
                ToneMapping,
                OutputBlit,
                Count
            };
            static int statisticsEffect =
                static_cast<int>(StatisticsEffect::CompleteRenderer);
            static constexpr const char* StatisticsEffectLabels[] = {
                "Complete Renderer",
                "Geometry / G-Buffer",
                "Direct Lighting",
                "Screen-Space Visibility",
                "Material Picking",
                "Procedural Sky",
                "Tone Mapping",
                "Output Blit"
            };
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo(
                    "##StatisticsEffect",
                    StatisticsEffectLabels[statisticsEffect]))
            {
                for (int effectIndex = 0;
                    effectIndex < static_cast<int>(StatisticsEffect::Count);
                    ++effectIndex)
                {
                    const bool selected =
                        effectIndex == statisticsEffect;
                    if (ImGui::Selectable(
                            StatisticsEffectLabels[effectIndex], selected))
                    {
                        statisticsEffect = effectIndex;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the renderer effect whose GPU cost is shown below.");

            if (statisticsEffect != static_cast<int>(
                    StatisticsEffect::ScreenSpaceVisibility))
            {
                const RendererTimings& rendererTimings =
                    m_app->GetRendererTimings();
                const bool deferred = m_ui.UsesDeferredShading();
                const bool visibilityActive = deferred && m_ui.EnablePbr &&
                    m_ui.ScreenSpaceVisibility.HasActiveConsumer();
                const auto stageActive =
                    [&](RendererTimingStage stage)
                    {
                        switch (stage)
                        {
                        case RendererTimingStage::DirectLighting:
                            return deferred;
                        case RendererTimingStage::ScreenSpaceVisibility:
                            return visibilityActive;
                        case RendererTimingStage::MaterialPicking:
                            return m_ui.SelectedMaterial != nullptr ||
                                m_ui.SelectedNode != nullptr;
                        case RendererTimingStage::ProceduralSky:
                            return m_ui.EnableProceduralSky;
                        case RendererTimingStage::ToneMapping:
                            return m_ui.UsesTonemapper();
                        default:
                            return true;
                        }
                    };
                const auto drawRendererTimingRow =
                    [&](const char* label, RendererTimingStage stage)
                    {
                        const bool active = stageActive(stage);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (active)
                            ImGui::TextUnformatted(label);
                        else
                            ImGui::TextDisabled("%s", label);
                        ImGui::TableSetColumnIndex(1);
                        if (active)
                        {
                            ImGui::Text(
                                "%.3f ms", rendererTimings.Get(stage));
                        }
                        else
                        {
                            ImGui::TextDisabled("--");
                        }
                    };
                if (ImGui::BeginTable(
                        "##RendererLiveTimings",
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
                    if (statisticsEffect == static_cast<int>(
                            StatisticsEffect::CompleteRenderer))
                    {
                        drawRendererTimingRow(
                            "Complete Renderer Frame",
                            RendererTimingStage::CompleteFrame);
                        drawRendererTimingRow(
                            "Scene Setup & Clears",
                            RendererTimingStage::SceneSetup);
                        drawRendererTimingRow(
                            deferred ? "G-Buffer Geometry"
                                     : "Forward Geometry & Lighting",
                            RendererTimingStage::Geometry);
                        drawRendererTimingRow(
                            "Deferred Direct Lighting",
                            RendererTimingStage::DirectLighting);
                        drawRendererTimingRow(
                            "Screen-Space Visibility",
                            RendererTimingStage::ScreenSpaceVisibility);
                        drawRendererTimingRow(
                            "Material Picking",
                            RendererTimingStage::MaterialPicking);
                        drawRendererTimingRow(
                            "Procedural Sky",
                            RendererTimingStage::ProceduralSky);
                        drawRendererTimingRow(
                            "Tone Mapping",
                            RendererTimingStage::ToneMapping);
                        drawRendererTimingRow(
                            "Output Blit",
                            RendererTimingStage::OutputBlit);
                    }
                    else
                    {
                        static constexpr RendererTimingStage
                            StatisticsStages[] = {
                                RendererTimingStage::CompleteFrame,
                                RendererTimingStage::Geometry,
                                RendererTimingStage::DirectLighting,
                                RendererTimingStage::ScreenSpaceVisibility,
                                RendererTimingStage::MaterialPicking,
                                RendererTimingStage::ProceduralSky,
                                RendererTimingStage::ToneMapping,
                                RendererTimingStage::OutputBlit
                            };
                        const RendererTimingStage selectedStage =
                            StatisticsStages[statisticsEffect];
                        drawRendererTimingRow(
                            StatisticsEffectLabels[statisticsEffect],
                            selectedStage);
                        drawRendererTimingRow(
                            "Complete Renderer Frame",
                            RendererTimingStage::CompleteFrame);
                    }
                    ImGui::EndTable();
                }
            }
            else
            {
            const ScreenSpaceVisibilitySettings& statsVisibility =
                m_ui.ScreenSpaceVisibility;
            const ScreenSpaceVisibilityTimings* timings =
                m_app->GetScreenSpaceVisibilityTimings();
            const VisibilityPerformanceWorkload statsWorkload =
                GetRenderedVisibilityPerformanceWorkload(
                    statsVisibility,
                    uint32_t(std::max(width, 0)),
                    uint32_t(std::max(height, 0)),
                    timings);
            const VisibilityPerformanceProfileConfiguration statsConfig =
                GetEffectiveVisibilityPerformanceConfiguration(
                    statsVisibility);
            const VisibilityExecutionPlan statsPlan =
                ResolveVisibilityExecutionPlan(
                    statsConfig,
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
                    HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::Composition);

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
                    "Later Bounces",
                    timings->laterTraceMs,
                    laterTraceActive);
                drawTimingRow(
                    "Spatial Denoise",
                    timings->spatialDenoiseMs,
                    dedicatedSpatialDenoise);
                drawTimingRow(
                    "Temporal Reconstruction",
                    timings->temporalMs,
                    temporalActive);
                drawTimingRow(
                    "Filtered Spatial Reconstruction",
                    timings->fusedSpatialDenoiseUpsampleMs,
                    fusedSpatialDenoiseUpsample);
                drawTimingRow(
                    "Guide-Aware Upsampling",
                    timings->requiredUpsampleMs,
                    requiredUpsample);
                drawTimingRow(
                    "Fused Apply",
                    timings->fullResolutionApplyMs,
                    applyActive);
                drawTimingRow(
                    "Indirect-Lighting Composition",
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
                        "  Trace AO",
                        timings->rawAmbientTextureBytes);
                    drawAllocatedMibRow(
                        "  Current Bounce GI",
                        timings->rawIndirectFrontierBytes);
                    drawAllocatedMibRow(
                        "  Additional Bounce Storage",
                        timings->multiBounceIndirectBytes);
                    drawAllocatedMibRow(
                        "  Output AO",
                        timings->finalAmbientTextureBytes);
                    drawAllocatedMibRow(
                        "  Output GI",
                        timings->finalIndirectTextureBytes);
                    drawMibRow(
                        "Working Texture Total (Logical)",
                        timings->workingTextureBytes);
                    drawSectionRow("Shared And History Textures");
                    drawAllocatedMibRow(
                        "  Scheduler Noise Tables",
                        timings->schedulerResourceBytes);
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
                        "  Long-Range Depth",
                        timings->depthHierarchyBytes);
                    drawMibRow(
                        "Optional Candidate Total",
                        timings->optionalTextureBytes);
                    drawSectionRow("Candidate Textures");
                    drawAllocatedMibRow(
                        "  Offline Packed Noise",
                        timings->packedFastNoiseBytes);
                    drawAllocatedMibRow(
                        "  Packed Edge Metadata",
                        timings->packedEdgeMetadataBytes);
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

            ImGui::SeparatorText("Benchmarking");
            static int benchmarkWarmupFrames = 120;
            static int benchmarkMeasuredFrames = 240;
            static std::string benchmarkUiError;

            const bool benchmarkQueued =
                m_app->IsVisibilityBenchmarkQueued();
            const bool benchmarkActive =
                m_app->IsVisibilityBenchmarkActive();
            const bool benchmarkBusy = benchmarkQueued || benchmarkActive;
            const ScreenSpaceVisibilitySettings& benchmarkVisibility =
                m_ui.ScreenSpaceVisibility;
            const ScreenSpaceVisibilityTimings* benchmarkTimings =
                m_app->GetScreenSpaceVisibilityTimings();
            const VisibilityPerformanceWorkload benchmarkWorkload =
                GetRenderedVisibilityPerformanceWorkload(
                    benchmarkVisibility,
                    uint32_t(std::max(width, 0)),
                    uint32_t(std::max(height, 0)),
                    benchmarkTimings);
            const VisibilityExecutionPlan benchmarkPlan =
                ResolveVisibilityExecutionPlan(
                    GetEffectiveVisibilityPerformanceConfiguration(
                        benchmarkVisibility),
                    benchmarkWorkload);
            const bool benchmarkSceneValid =
                m_app->HasSponzaCameraLocations();
            std::string benchmarkBlockedReason;
            if (benchmarkBusy)
            {
                benchmarkBlockedReason =
                    "A visibility benchmark is already running.";
            }
            else if (!m_ui.UsesDeferredShading())
            {
                benchmarkBlockedReason =
                    "Select the deferred renderer before benchmarking.";
            }
            else if (!benchmarkVisibility.enabled ||
                !benchmarkVisibility.HasActiveConsumer())
            {
                benchmarkBlockedReason =
                    "Enable visibility and at least one AO or GI consumer.";
            }
            else if (!benchmarkSceneValid)
            {
                benchmarkBlockedReason =
                    "Load PBR Sponza Decorated or PBR Sponza Plain so the "
                    "standard benchmark camera can be locked.";
            }
            else if (!benchmarkPlan.valid)
            {
                benchmarkBlockedReason =
                    "The current execution plan is invalid: " +
                    benchmarkPlan.errorMessage;
            }
            else if (!benchmarkTimings)
            {
                benchmarkBlockedReason =
                    "Waiting for the renderer's first visibility frame.";
            }
            else if (!benchmarkTimings->profileValid)
            {
                benchmarkBlockedReason =
                    benchmarkTimings->profileError.empty()
                    ? "The renderer rejected the current execution plan."
                    : benchmarkTimings->profileError;
            }
            else if (benchmarkTimings->activePermutation.empty() ||
                benchmarkTimings->activePermutation !=
                    benchmarkPlan.permutationName)
            {
                benchmarkBlockedReason =
                    "Waiting for the current settings to reach the GPU.";
            }
            const bool canRunCurrent = benchmarkBlockedReason.empty();

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

            if (!canRunCurrent)
                ImGui::BeginDisabled();
            if (ImGui::Button(
                    "Run Current",
                    ImVec2(-FLT_MIN, 0.f)) && canRunCurrent)
            {
                benchmarkUiError.clear();
                if (!m_app->QueueVisibilityBenchmark(
                        uint32_t(std::max(benchmarkWarmupFrames, 0)),
                        uint32_t(std::max(benchmarkMeasuredFrames, 1)),
                        {},
                        false))
                {
                    benchmarkUiError =
                        m_app->GetVisibilityBenchmarkError();
                }
            }
            ImGui::SetItemTooltip(
                canRunCurrent
                    ? "Benchmark the settings currently rendered. The "
                        "window and camera lock to the controlled 1920 x 1080 "
                        "Sponza view, then restore afterward."
                    : benchmarkBlockedReason.c_str());
            if (!canRunCurrent)
                ImGui::EndDisabled();
            if (!canRunCurrent && !benchmarkBusy)
                ImGui::TextWrapped("%s", benchmarkBlockedReason.c_str());

            if (!benchmarkBusy)
                ImGui::BeginDisabled();
            if (ImGui::Button(
                    "Cancel",
                    ImVec2(-FLT_MIN, 0.f)) && benchmarkBusy)
            {
                m_app->CancelVisibilityBenchmark();
            }
            ImGui::SetItemTooltip(
                "Cancel without publishing partial measurements.");
            if (!benchmarkBusy)
                ImGui::EndDisabled();
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


        const bool antiAliasingOpen = DrawCollapsingHeader(
            "Aliasing",
            "Choose a complete anti-aliasing pipeline. Technical controls stay in developer overrides.",
            ImGuiTreeNodeFlags_DefaultOpen);
        if (antiAliasingOpen)
        {
            BeginDrawerBody(
                "##AliasingBody",
                settingsControlWidth);
            // CollapsingHeader does not push an ID scope. Keep all Aliasing
            // controls distinct from equally named controls in other Settings
            // drawers (notably Visibility's "Quality" combo).
            ImGui::PushID("AliasingControls");

            AntiAliasingSettings& settings = m_ui.AntiAliasing;
            ApplyPendingAliasingUiAction();
            const bool temporalAAAvailable =
                m_ui.IsTemporalAntiAliasingAvailable();
            const bool motionTestRunning =
                m_app->IsAntiAliasingMotionTestRunning();
            const bool motionTestAvailable =
                m_app->CanStartAntiAliasingMotionTest();

            if (!motionTestAvailable)
                ImGui::BeginDisabled();
            if (DrawCenteredActionButton(
                    motionTestRunning
                        ? "Running 45-Degree Motion Test..."
                        : "Run 45-Degree Motion Test",
                    settingsControlWidth))
            {
                m_app->StartAntiAliasingMotionTest();
            }
            if (!motionTestAvailable)
                ImGui::EndDisabled();
            ImGui::SetItemTooltip(
                motionTestRunning
                    ? "The exact 40 Hz Benchmark Position 1 warm, turn, hold, and return sequence is running."
                    : (m_app->HasSponzaCameraLocations()
                        ? "Run the exact Benchmark Position 1 test used for AA validation: 180 warm frames, 45 degrees right at 0.375 degrees per frame with a 40 Hz target, a 16-frame hold, and the same return."
                        : "The motion test requires a standardized PBR Sponza scene."));
            ImGui::TextWrapped(
                "%s",
                m_app->GetAntiAliasingMotionTestStatus().c_str());

            if (motionTestRunning)
                ImGui::BeginDisabled();

            auto drawEnumOption = [settingsControlWidth](
                const char* label,
                auto& selectedValue,
                uint32_t valueCount,
                auto getLabel,
                const char* tooltip,
                const char* inheritedOrAutoValue = nullptr,
                bool collapseRedundantInheritedValue = false)
            {
                using ValueType =
                    std::decay_t<decltype(selectedValue)>;
                bool changed = false;
                if (collapseRedundantInheritedValue &&
                    inheritedOrAutoValue &&
                    static_cast<uint32_t>(selectedValue) != 0u &&
                    std::string(getLabel(selectedValue)) ==
                        inheritedOrAutoValue)
                {
                    // An explicit value identical to the inherited/Auto value
                    // has no distinct effect. Normalize it so the UI exposes
                    // one effective choice and the preset is not needlessly
                    // marked Custom.
                    selectedValue =
                        static_cast<ValueType>(0u);
                    changed = true;
                }
                const auto makeLabel =
                    [&](ValueType value)
                    {
                        std::string result = getLabel(value);
                        if (static_cast<uint32_t>(value) == 0u &&
                            inheritedOrAutoValue)
                        {
                            result =
                                std::string(inheritedOrAutoValue) +
                                " (" + result;
                            result += ")";
                        }
                        return result;
                    };
                const std::string preview =
                    makeLabel(selectedValue);
                BeginAnimatedMutex(label, false);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(label, preview.c_str()))
                {
                    for (uint32_t index = 0u;
                        index < valueCount;
                        ++index)
                    {
                        const ValueType candidate =
                            static_cast<ValueType>(index);
                        if (collapseRedundantInheritedValue &&
                            index != 0u &&
                            inheritedOrAutoValue &&
                            std::string(getLabel(candidate)) ==
                                inheritedOrAutoValue)
                        {
                            continue;
                        }
                        const bool selected =
                            candidate == selectedValue;
                        const std::string candidateLabel =
                            makeLabel(candidate);
                        if (ImGui::Selectable(
                                candidateLabel.c_str(),
                                selected) &&
                            !selected)
                        {
                            ValueType* selectedValuePointer =
                                &selectedValue;
                            QueueAliasingUiAction(
                                [selectedValuePointer, candidate]()
                                {
                                    *selectedValuePointer = candidate;
                                });
                            changed = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                EndAnimatedMutex();
                ImGui::SetItemTooltip(tooltip);
                return changed;
            };

            auto drawMutexOption = [settingsControlWidth](
                const char* label,
                const char* tooltip)
            {
                BeginAnimatedMutex(label, true);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(label, "(Mutex)"))
                    ImGui::EndCombo();
                EndAnimatedMutex();
                ImGui::SetItemTooltip(tooltip);
            };

            auto drawFixedOption = [settingsControlWidth](
                const char* label,
                const char* value,
                const char* tooltip,
                bool mutex = false)
            {
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (mutex)
                    BeginAnimatedMutex(label, true);
                else
                    ImGui::BeginDisabled();
                if (BeginRoundedCombo(label, value))
                    ImGui::EndCombo();
                if (mutex)
                    EndAnimatedMutex();
                else
                    ImGui::EndDisabled();
                ImGui::SetItemTooltip(tooltip);
            };

            auto drawMutexCheckbox = [](
                const char* label,
                const char* tooltip)
            {
                bool value = false;
                std::string displayedLabel =
                    std::string(label) + " (Mutex)###" + label;
                BeginAnimatedMutex(label, true);
                ImGui::Checkbox(displayedLabel.c_str(), &value);
                EndAnimatedMutex();
                ImGui::SetItemTooltip(tooltip);
            };

            ImGui::Checkbox("Enabled", &settings.enabled);
            ImGui::SetItemTooltip(
                "Bypass anti-aliasing while retaining the selected method, quality, and overrides.");

            const bool temporalMethodSelected =
                settings.method ==
                    AntiAliasingMethod::
                        TemporalSubpixelMorphological;
            const bool temporalSelectionUnavailableBeforeSelection =
                temporalMethodSelected &&
                !temporalAAAvailable;
            std::string methodPreview =
                GetAntiAliasingMethodLabel(settings.method);
            if (temporalSelectionUnavailableBeforeSelection)
                methodPreview += " (Mutex)";
            BeginAnimatedMutexPreview(
                "##MethodMutexPreview",
                temporalSelectionUnavailableBeforeSelection);
            ImGui::SetNextItemWidth(settingsControlWidth);
            const bool methodComboOpen = BeginRoundedCombo(
                "Method",
                methodPreview.c_str());
            EndAnimatedMutexPreview();
            if (methodComboOpen)
            {
                for (uint32_t index = 0u;
                    index <
                        static_cast<uint32_t>(
                            AntiAliasingMethod::Count);
                    ++index)
                {
                    const AntiAliasingMethod candidate =
                        static_cast<AntiAliasingMethod>(index);
                    const bool candidateUnavailable =
                        (candidate ==
                            AntiAliasingMethod::
                                    TemporalSubpixelMorphological) &&
                        !temporalAAAvailable;
                    ImGui::PushID(static_cast<int>(index));
                    BeginAnimatedMutex(
                        "##MethodCandidateMutex",
                        candidateUnavailable);

                    const bool selected =
                        candidate == settings.method;
                    std::string candidateLabel =
                        GetAntiAliasingMethodLabel(candidate);
                    if (candidateUnavailable)
                        candidateLabel += " (Mutex)";
                    candidateLabel += "###MethodCandidate";
                    if (ImGui::Selectable(
                            candidateLabel.c_str(),
                            selected) &&
                        !candidateUnavailable)
                    {
                        AntiAliasingSettings* settingsPointer =
                            &settings;
                        QueueAliasingUiAction(
                            [this, settingsPointer, candidate]()
                            {
                                settingsPointer->method =
                                    candidate;
                                settingsPointer->quality =
                                    SanitizeAntiAliasingQuality(
                                        settingsPointer->method,
                                        settingsPointer->quality);
                                m_ui.MiniEngineTaaVisualization =
                                    MiniEngineTaaDebugView::Off;
                            });
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    EndAnimatedMutex();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose diagnostic spatial SMAA, long-term MiniEngine temporal AA, Intel CMAA2, or hardware MSAA. Temporal AA requires deferred UVSR PBR motion and depth.");

            std::string qualityPreview =
                GetAntiAliasingQualityMenuLabel(
                    settings.method,
                    settings.quality);
            if (IsAntiAliasingPresetCustom(settings))
                qualityPreview += " (Custom)";
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Quality",
                    qualityPreview.c_str()))
            {
                for (uint32_t index = 0u;
                    index <
                        static_cast<uint32_t>(
                            AntiAliasingQuality::Count);
                    ++index)
                {
                    const AntiAliasingQuality candidate =
                        static_cast<AntiAliasingQuality>(index);
                    const bool candidateUnavailable =
                        !IsAntiAliasingQualitySupported(
                            settings.method,
                            candidate);
                    ImGui::PushID(static_cast<int>(index));
                    BeginAnimatedMutex(
                        "##QualityCandidateMutex",
                        candidateUnavailable);
                    const bool selected =
                        candidate == settings.quality;
                    std::string candidateLabel =
                        GetAntiAliasingQualityMenuLabel(
                            settings.method,
                            candidate);
                    if (candidateUnavailable)
                        candidateLabel += " (Mutex)";
                    candidateLabel += "###QualityCandidate";
                    if (ImGui::Selectable(
                            candidateLabel.c_str(),
                            selected) &&
                        !selected &&
                        !candidateUnavailable)
                    {
                        AntiAliasingSettings* settingsPointer =
                            &settings;
                        QueueAliasingUiAction(
                            [this, settingsPointer, candidate]()
                            {
                                settingsPointer->quality =
                                    candidate;
                                m_ui.MiniEngineTaaVisualization =
                                    MiniEngineTaaDebugView::Off;
                            });
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    EndAnimatedMutex();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "SMAA uses the pinned official Low through Ultra presets. Temporal has independent Low through Ultra horizons. Conservative Morphological exposes Low through High. Multisample maps Low, Medium, High, and Ultra to 2x, 4x, 8x, and 16x.");

            const AntiAliasingPreset selectedImplementation =
                GetAntiAliasingImplementation(
                    settings.method,
                    SanitizeAntiAliasingQuality(
                        settings.method,
                        settings.quality));
            const bool longTermTemporalControlsAvailable =
                IsLongTermTemporalPreset(selectedImplementation) &&
                temporalAAAvailable;
            const bool temporalSelectionUnavailable =
                settings.method ==
                    AntiAliasingMethod::
                        TemporalSubpixelMorphological;
            const bool effectiveTemporalSelectionUnavailable =
                temporalSelectionUnavailable &&
                !temporalAAAvailable;
            if (effectiveTemporalSelectionUnavailable)
            {
                ImGui::TextDisabled(
                    m_ui.HasMiniEngineTaaVisibilityConflict()
                        ? "Temporal anti-aliasing is paused until visibility Temporal Reconstruction is disabled."
                        : "Temporal anti-aliasing requires deferred UVSR PBR motion and depth.");
            }

            MiniEngineTaaAlgorithmOverrides& historyOverrides =
                settings.algorithmOverrides;
            AntiAliasingSettings historyPresetSettings = settings;
            historyPresetSettings.algorithmOverrides.historyFrames =
                -1;
            historyPresetSettings.algorithmOverrides.historyStrength =
                -1.f;
            const ResolvedAntiAliasingSettings historyPreset =
                m_ui.GetResolvedAntiAliasingSettings(
                    historyPresetSettings);
            const bool usesConfigurableHistory =
                longTermTemporalControlsAvailable;
            if (usesConfigurableHistory)
            {
                int historyFrames =
                    historyOverrides.historyFrames >= 0
                        ? historyOverrides.historyFrames
                        : static_cast<int>(
                            historyPreset.historyFrames);
                std::string historyFramesLabel =
                    "History Frames";
                if (historyOverrides.historyFrames < 0)
                    historyFramesLabel += " (Preset)";
                historyFramesLabel +=
                    "##ResolvedHistoryFrames";
                BeginAnimatedMutex("History Frames", false);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawSliderInt(
                        historyFramesLabel.c_str(),
                        &historyFrames,
                        1,
                        31,
                        "%d"))
                {
                    historyOverrides.historyFrames =
                        historyFrames ==
                            static_cast<int>(
                                historyPreset.historyFrames)
                            ? -1
                            : historyFrames;
                }
                EndAnimatedMutex();
                ImGui::SetItemTooltip(
                    "Cap the logical number of prior frames that can influence temporal AA. The physical allocation remains two ping-pong textures.");
            }
            else
            {
                drawFixedOption(
                    "History Frames",
                    "No History (Mutex)",
                    "The selected spatial or multisample method does not retain prior frames.",
                    true);
            }

            if (usesConfigurableHistory)
            {
                float historyStrength =
                    100.f *
                    (historyOverrides.historyStrength >= 0.f
                        ? historyOverrides.historyStrength
                        : historyPreset.historyStrength);
                std::string historyStrengthLabel =
                    "History Strength";
                if (historyOverrides.historyStrength < 0.f)
                    historyStrengthLabel += " (Preset)";
                historyStrengthLabel +=
                    "##ResolvedHistoryStrength";
                BeginAnimatedMutex("History Strength", false);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawSliderFloat(
                        historyStrengthLabel.c_str(),
                        &historyStrength,
                        0.f,
                        100.f,
                        "%.0f%%"))
                {
                    historyOverrides.historyStrength =
                        std::clamp(
                            historyStrength * 0.01f,
                            0.f,
                            1.f);
                    if (std::abs(
                            historyOverrides.historyStrength -
                            historyPreset.historyStrength) < 1e-4f)
                    {
                        historyOverrides.historyStrength = -1.f;
                    }
                }
                EndAnimatedMutex();
                ImGui::SetItemTooltip(
                    "Scale accepted history after motion, bounds, reverse-Z depth, disocclusion, rectification, and stable-interior gates. It can only reduce history.");
            }
            else
            {
                float noHistoryStrength = 0.f;
                BeginAnimatedMutex("History Strength", true);
                ImGui::SetNextItemWidth(settingsControlWidth);
                DrawSliderFloat(
                    "History Strength (Mutex)",
                    &noHistoryStrength,
                    0.f,
                    100.f,
                    "%.0f%%");
                EndAnimatedMutex();
                ImGui::SetItemTooltip(
                    "History strength is mutually exclusive with a method that retains no prior frames.");
            }

#if !UVSR_AA_DEVELOPER_OVERRIDES
            if (longTermTemporalControlsAvailable)
            {
                ImGui::Checkbox(
                    "Sharpness###Sharpness",
                    &m_ui.MiniEngineTaaSharpenEnabled);
                ImGui::SetItemTooltip(
                    "Enable or bypass temporal output sharpening while retaining the selected strength.");
                if (!m_ui.MiniEngineTaaSharpenEnabled)
                    ImGui::BeginDisabled();
                ImGui::SetNextItemWidth(settingsControlWidth);
                DrawSliderFloat(
                    "Sharpness Strength",
                    &m_ui.MiniEngineTaaSharpness,
                    MiniEngineTaaMinimumSharpness,
                    MiniEngineTaaMaximumSharpness,
                    "%.2f");
                if (!m_ui.MiniEngineTaaSharpenEnabled)
                    ImGui::EndDisabled();
                ImGui::SetItemTooltip(
                    "Set temporal output sharpness. The stored value is retained while sharpening is disabled.");
            }
#endif

#if UVSR_AA_DEVELOPER_OVERRIDES
            if (BeginAnimatedTreeNode(
                    "Aliasing Algorithm Configuration",
                    ImGuiTreeNodeFlags_DefaultOpen))
            {
                MiniEngineTaaAlgorithmOverrides& overrides =
                    settings.algorithmOverrides;
                AntiAliasingSettings presetLabelSettings =
                    settings;
                presetLabelSettings.algorithmOverrides =
                    MiniEngineTaaAlgorithmOverrides{};
                const ResolvedAntiAliasingSettings resolvedForLabels =
                    m_ui.GetResolvedAntiAliasingSettings(
                        presetLabelSettings);

                const auto drawMorphologyOption = [&]()
                {
                    if (selectedImplementation ==
                        AntiAliasingPreset::Smaa1x)
                    {
                        drawFixedOption(
                            "Subpixel Morphology##Developer",
                            "All Pixels (Preset)",
                            "Diagnostic SMAA 1x always runs the complete full-screen morphology pipeline.");
                    }
                    else if (selectedImplementation ==
                        AntiAliasingPreset::IntelCmaa2)
                    {
                        drawFixedOption(
                            "Subpixel Morphology##Developer",
                            "Conservative Morphological (Preset)",
                            "The standalone Conservative Morphological method always runs Intel CMAA2.");
                    }
                    else if (
                        longTermTemporalControlsAvailable ||
                        selectedImplementation ==
                            AntiAliasingPreset::Msaa2x ||
                        selectedImplementation ==
                            AntiAliasingPreset::Msaa4x ||
                        selectedImplementation ==
                            AntiAliasingPreset::Msaa8x ||
                        selectedImplementation ==
                            AntiAliasingPreset::Msaa16x)
                    {
                        const auto applicationForOverride =
                            [&](SmaaApplicationOverride value)
                            {
                                switch (value)
                                {
                                case SmaaApplicationOverride::Off:
                                    return SmaaApplication::Off;
                                case SmaaApplicationOverride::FullScreen:
                                    return SmaaApplication::FullScreen;
                                case SmaaApplicationOverride::
                                        ConservativeMorphological:
                                    return SmaaApplication::
                                        ConservativeMorphological;
                                case SmaaApplicationOverride::SelectiveShort:
                                    return SmaaApplication::SelectiveShort;
                                case SmaaApplicationOverride::SelectiveLong:
                                    return SmaaApplication::SelectiveLong;
                                default:
                                    return resolvedForLabels
                                        .subpixelMorphology;
                                }
                            };
                        const auto makeMorphologyLabel =
                            [&](SmaaApplicationOverride value)
                            {
                                const SmaaApplication application =
                                    applicationForOverride(value);
                                std::string label =
                                    value ==
                                        SmaaApplicationOverride::FromPreset
                                    ? std::string(
                                          GetSmaaApplicationLabel(
                                              application)) +
                                          " (Preset)"
                                    : GetSmaaApplicationOverrideLabel(value);
                                if (!IsMorphologyApplicationSupported(
                                        selectedImplementation,
                                        application))
                                {
                                    label += " (Mutex)";
                                }
                                return label;
                            };
                        const SmaaApplication selectedApplication =
                            applicationForOverride(
                                overrides.subpixelMorphology);
                        const bool selectedApplicationUnavailable =
                            !IsMorphologyApplicationSupported(
                                selectedImplementation,
                                selectedApplication);
                        const std::string morphologyPreview =
                            makeMorphologyLabel(
                                overrides.subpixelMorphology);
                        BeginAnimatedMutexPreview(
                            "##MorphologyMutexPreview",
                            selectedApplicationUnavailable);
                        ImGui::SetNextItemWidth(settingsControlWidth);
                        const bool morphologyComboOpen =
                            BeginRoundedCombo(
                                "Subpixel Morphology##Developer",
                                morphologyPreview.c_str());
                        EndAnimatedMutexPreview();
                        if (morphologyComboOpen)
                        {
                            for (uint32_t index = 0u;
                                index < static_cast<uint32_t>(
                                    SmaaApplicationOverride::Count);
                                ++index)
                            {
                                const auto candidate =
                                    static_cast<SmaaApplicationOverride>(
                                        index);
                                const SmaaApplication application =
                                    applicationForOverride(candidate);
                                const bool available =
                                    IsMorphologyApplicationSupported(
                                        selectedImplementation,
                                        application);
                                ImGui::PushID(
                                    static_cast<int>(index));
                                BeginAnimatedMutex(
                                    "##MorphologyCandidateMutex",
                                    !available);
                                const bool selected =
                                    candidate ==
                                    overrides.subpixelMorphology;
                                std::string candidateLabel =
                                    makeMorphologyLabel(candidate);
                                candidateLabel +=
                                    "###MorphologyCandidate";
                                if (ImGui::Selectable(
                                        candidateLabel.c_str(),
                                        selected) &&
                                    !selected &&
                                    available)
                                {
                                    MiniEngineTaaAlgorithmOverrides*
                                        overridesPointer =
                                            &overrides;
                                    QueueAliasingUiAction(
                                        [this,
                                            overridesPointer,
                                            candidate]()
                                        {
                                            overridesPointer->
                                                subpixelMorphology =
                                                    candidate;
                                            m_ui.MiniEngineTaaVisualization =
                                                MiniEngineTaaDebugView::Off;
                                        });
                                }
                                if (selected)
                                    ImGui::SetItemDefaultFocus();
                                EndAnimatedMutex();
                                ImGui::PopID();
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SetItemTooltip(
                            "Choose no morphology, full-screen SMAA, or Intel CMAA2 after the resolved Temporal or Multisample image. Selective SMAA additionally requires Temporal's rejection mask.");
                    }
                    else
                    {
                        drawMutexOption(
                            "Subpixel Morphology##Developer",
                            "This standalone method owns its morphology.");
                    }
                };

                if (longTermTemporalControlsAvailable)
                {
                    BeginAnimatedMutex("Sharpness", false);
                    ImGui::Checkbox(
                        "Sharpness###Sharpness",
                        &m_ui.MiniEngineTaaSharpenEnabled);
                    EndAnimatedMutex();
                    ImGui::SetItemTooltip(
                        "Enable or bypass temporal output sharpening while retaining the selected strength.");
                    const bool sharpnessStrengthEnabled =
                        m_ui.MiniEngineTaaSharpenEnabled &&
                        IsSharpnessRelevant(selectedImplementation);
                    if (!sharpnessStrengthEnabled)
                        ImGui::BeginDisabled();
                    ImGui::SetNextItemWidth(
                        settingsControlWidth);
                    DrawSliderFloat(
                        "Sharpness Strength",
                        &m_ui.MiniEngineTaaSharpness,
                        MiniEngineTaaMinimumSharpness,
                        MiniEngineTaaMaximumSharpness,
                        "%.2f");
                    if (!sharpnessStrengthEnabled)
                        ImGui::EndDisabled();
                    ImGui::SetItemTooltip(
                        "Set temporal output sharpness. The stored value is retained while sharpening is disabled.");

                    drawMorphologyOption();
                    drawEnumOption(
                        "Motion Source",
                        overrides.motionSource,
                        static_cast<uint32_t>(
                            MiniEngineTaaMotionSourceOverride::Count),
                        GetMiniEngineTaaMotionSourceOverrideLabel,
                        "Override motion ownership. Changing it resets all temporal state.",
                        GetMiniEngineTaaMotionSourceLabel(
                            resolvedForLabels.temporal.motionSource),
                        true);
                    drawEnumOption(
                        "Current Reconstruction",
                        overrides.currentReconstruction,
                        static_cast<uint32_t>(
                            MiniEngineTaaCurrentReconstructionOverride::Count),
                        GetMiniEngineTaaCurrentReconstructionOverrideLabel,
                        "Override direct or de-jittered current reconstruction.",
                        GetMiniEngineTaaCurrentReconstructionLabel(
                            resolvedForLabels.temporal.currentReconstruction),
                        true);
                    drawEnumOption(
                        "Rectification",
                        overrides.rectification,
                        static_cast<uint32_t>(
                            MiniEngineTaaRectificationOverride::Count),
                        GetMiniEngineTaaRectificationOverrideLabel,
                        "Override pair, per-pixel, or variance-aware history rectification.",
                        GetMiniEngineTaaRectificationLabel(
                            resolvedForLabels.temporal.rectification),
                        true);
                    drawEnumOption(
                        "History Filter",
                        overrides.historyFilter,
                        static_cast<uint32_t>(
                            MiniEngineTaaHistoryFilterOverride::Count),
                        GetMiniEngineTaaHistoryFilterOverrideLabel,
                        "Override the real history sampling filter.",
                        GetMiniEngineTaaHistoryFilterLabel(
                            resolvedForLabels.temporal.historyFilter),
                        true);
                }
                else
                {
                    drawMutexCheckbox(
                        "Sharpness",
                        "Sharpness is mutually exclusive with the current anti-aliasing selection.");
                    drawMorphologyOption();
                    drawMutexOption(
                        "Motion Source",
                        "Temporal algorithm overrides are mutually exclusive with the current anti-aliasing selection.");
                    drawMutexOption(
                        "Current Reconstruction",
                        "Temporal algorithm overrides are mutually exclusive with the current anti-aliasing selection.");
                    drawMutexOption(
                        "Rectification",
                        "Temporal algorithm overrides are mutually exclusive with the current anti-aliasing selection.");
                    drawMutexOption(
                        "History Filter",
                        "Temporal algorithm overrides are mutually exclusive with the current anti-aliasing selection.");
                }

                // Resurrection remains last because it is a recovery policy
                // applied after the primary spatial and temporal choices.
                if (longTermTemporalControlsAvailable)
                {
                    drawEnumOption(
                        "Sample Resurrection",
                        overrides.sampleResurrection,
                        static_cast<uint32_t>(
                            MiniEngineTaaSampleResurrectionOverride::Count),
                        GetMiniEngineTaaSampleResurrectionOverrideLabel,
                        "Reuse one or two older validated samples when immediate history is unreliable.",
                        GetMiniEngineTaaSampleResurrectionLabel(
                            resolvedForLabels.sampleResurrection),
                        true);
                }
                else
                {
                    drawMutexOption(
                        "Sample Resurrection",
                        "Temporal algorithm overrides are mutually exclusive with the current anti-aliasing selection.");
                }

                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Developer Performance Overrides"))
            {
                MiniEngineTaaAlgorithmOverrides& algorithmOverrides =
                    settings.algorithmOverrides;
                if (longTermTemporalControlsAvailable)
                {
                    AntiAliasingSettings stableInteriorPresetSettings =
                        settings;
                    stableInteriorPresetSettings.algorithmOverrides =
                        MiniEngineTaaAlgorithmOverrides{};
                    const bool presetStableInterior =
                        m_ui.GetResolvedAntiAliasingSettings(
                                stableInteriorPresetSettings)
                                .temporal.interiorWeighting ==
                            MiniEngineTaaInteriorWeighting::StableInterior;
                    const bool inherited =
                        algorithmOverrides.stableInterior ==
                        MiniEngineTaaStableInteriorOverride::FromPreset;
                    bool stableInterior = inherited
                        ? presetStableInterior
                        : algorithmOverrides.stableInterior ==
                            MiniEngineTaaStableInteriorOverride::On;
                    std::string stableInteriorLabel = "Stable Interior";
                    if (inherited)
                        stableInteriorLabel += " (Preset)";
                    stableInteriorLabel += "###Stable Interior";
                    BeginAnimatedMutex("Stable Interior", false);
                    if (ImGui::Checkbox(
                            stableInteriorLabel.c_str(),
                            &stableInterior))
                    {
                        if (stableInterior == presetStableInterior)
                        {
                            algorithmOverrides.stableInterior =
                                MiniEngineTaaStableInteriorOverride::
                                    FromPreset;
                        }
                        else
                        {
                            algorithmOverrides.stableInterior =
                                stableInterior
                                ? MiniEngineTaaStableInteriorOverride::On
                                : MiniEngineTaaStableInteriorOverride::Off;
                        }
                    }
                    EndAnimatedMutex();
                    ImGui::SetItemTooltip(
                        "Continuously reduce history toward the controlled clarity floor only in coherent interiors. Preset shows the resolved value.");
                }
                else
                {
                    drawMutexCheckbox(
                        "Stable Interior",
                        "Stable Interior is mutually exclusive with the current anti-aliasing selection.");
                }

                if (const MiniEngineTemporalAATimings* timings =
                        m_app->GetMiniEngineTemporalAATimings())
                {
                    constexpr double BytesPerMiB =
                        1024.0 * 1024.0;
                    const ResolvedAntiAliasingSettings resolved =
                        m_ui.GetResolvedAntiAliasingSettings(
                            settings);
                    ImGui::Text(
                        "History %.1f MiB | %s | Frames %u | Resets %u",
                        double(timings->historyTextureBytes) /
                            BytesPerMiB,
                        timings->historyValid ? "Valid" : "Invalid",
                        timings->accumulationCount,
                        timings->historyResetCount);
                    ImGui::Text(
                        "Permutation %u / %u | Color %u | Moments %u | Depth %uG + %uS",
                        GetMiniEngineTaaBlendPermutationIndex(
                            resolved.temporal) + 1u,
                        MiniEngineTaaBlendPermutationCount,
                        timings->historyColorSamples,
                        timings->historyMomentSamples,
                        timings->historyDepthGathers,
                        timings->historyDepthSamples);
                    ImGui::Text(
                        "TAA %.2f ms | Blend %.2f | Output %.2f | Presentation Sharpen %.2f",
                        timings->CompleteEffectMilliseconds(),
                        timings->blendMilliseconds,
                        timings->outputMilliseconds,
                        timings->presentationSharpenMilliseconds);
                }
                if (const SmaaTimings* timings =
                        m_app->GetSmaaTimings())
                {
                    ImGui::Text(
                        "SMAA %.2f ms | Edge %.2f | Weight %.2f | Blend %.2f",
                        timings->CompleteEffectMilliseconds(),
                        timings->edgeMilliseconds,
                        timings->weightMilliseconds,
                        timings->neighborhoodMilliseconds);
                }
                if (const Cmaa2Timings* timings =
                        m_app->GetCmaa2Timings())
                {
                    ImGui::Text(
                        "CMAA2 %.2f ms | Edge %.2f | Candidates %.2f | Apply %.2f",
                        timings->CompleteEffectMilliseconds(),
                        timings->edgeMilliseconds,
                        timings->candidateMilliseconds,
                        timings->applyMilliseconds);
                }
                if (m_ui.UsesHardwareMsaa())
                {
                    if (m_ui.UsesDeferredShading())
                    {
                        ImGui::Text(
                            "MSAA %ux | Deferred PBR | Per-Sample Lighting",
                            m_app->GetRasterSampleCount());
                    }
                    else
                    {
                        ImGui::Text(
                            "MSAA %ux | Forward PBR | Hardware Resolve",
                            m_app->GetRasterSampleCount());
                    }
                }

                const ResolvedAntiAliasingSettings
                    resolvedForDebugAvailability =
                        m_ui.GetResolvedAntiAliasingSettings(
                            settings);
                const bool smaaDebugAvailable =
                    settings.enabled &&
                    (selectedImplementation ==
                            AntiAliasingPreset::Smaa1x ||
                        resolvedForDebugAvailability
                                .subpixelMorphology ==
                            SmaaApplication::FullScreen ||
                        resolvedForDebugAvailability
                                .subpixelMorphology ==
                            SmaaApplication::SelectiveShort ||
                        resolvedForDebugAvailability
                                .subpixelMorphology ==
                            SmaaApplication::SelectiveLong);
                const auto isDebugViewAvailable =
                    [&](MiniEngineTaaDebugView value)
                    {
                        return value ==
                                MiniEngineTaaDebugView::Off ||
                            (IsMiniEngineTaaDebugVisualization(
                                 value) &&
                                longTermTemporalControlsAvailable) ||
                            (IsSmaaDebugVisualization(value) &&
                                smaaDebugAvailable);
                    };
                std::string debugPreview =
                    GetMiniEngineTaaDebugViewLabel(
                        m_ui.MiniEngineTaaVisualization);
                const bool selectedDebugViewUnavailable =
                    !isDebugViewAvailable(
                        m_ui.MiniEngineTaaVisualization);
                if (selectedDebugViewUnavailable)
                {
                    debugPreview += " (Mutex)";
                }
                BeginAnimatedMutexPreview(
                    "##DebugViewMutexPreview",
                    selectedDebugViewUnavailable);
                ImGui::SetNextItemWidth(settingsControlWidth);
                const bool debugViewComboOpen =
                    BeginRoundedCombo(
                        "Debug View",
                        debugPreview.c_str());
                EndAnimatedMutexPreview();
                if (debugViewComboOpen)
                {
                    for (uint32_t index = 0u;
                        index < MiniEngineTaaDebugViewCount;
                        ++index)
                    {
                        const MiniEngineTaaDebugView candidate =
                            static_cast<MiniEngineTaaDebugView>(
                                index);
                        const bool available =
                            isDebugViewAvailable(candidate);
                        const bool selected =
                            candidate ==
                            m_ui.MiniEngineTaaVisualization;
                        std::string candidateLabel =
                            GetMiniEngineTaaDebugViewLabel(
                                candidate);
                        if (!available)
                        {
                            candidateLabel += " (Mutex)";
                        }
                        ImGui::PushID(static_cast<int>(index));
                        BeginAnimatedMutex(
                            "##DebugViewCandidateMutex",
                            !available);
                        candidateLabel += "###DebugViewCandidate";
                        if (ImGui::Selectable(
                                candidateLabel.c_str(),
                                selected) &&
                            !selected &&
                            available)
                        {
                            QueueAliasingUiAction(
                                [this, candidate]()
                                {
                                    m_ui.MiniEngineTaaVisualization =
                                        candidate;
                                });
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                        EndAnimatedMutex();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Inspect temporal confidence or the active SMAA edge mask, blend weights, and amplified presentation delta. Incompatible views are marked (Mutex).");

                MiniEngineTaaPerformanceOverrides& overrides =
                    settings.performanceOverrides;
                AntiAliasingSettings autoLabelSettings =
                    settings;
                autoLabelSettings.performanceOverrides =
                    MiniEngineTaaPerformanceOverrides{};
                const ResolvedAntiAliasingSettings
                    resolvedForPerformanceLabels =
                        m_ui.GetResolvedAntiAliasingSettings(
                            autoLabelSettings);
                const ResolvedAntiAliasingSettings
                    resolvedForPerformanceAvailability =
                        m_ui.GetResolvedAntiAliasingSettings(
                            settings);
                const bool resurrectionActive =
                    UsesSampleResurrection(
                        resolvedForPerformanceAvailability
                            .sampleResurrection);
                const bool presentationSharpenRequiresFusion =
                    longTermTemporalControlsAvailable &&
                    ShouldSharpenMiniEngineTaa(
                        m_ui.MiniEngineTaaSharpenEnabled,
                        m_ui.MiniEngineTaaSharpness) &&
                    resolvedForPerformanceAvailability
                            .subpixelMorphology !=
                        SmaaApplication::Off &&
                    !IsMiniEngineTaaDebugVisualization(
                        m_ui.MiniEngineTaaVisualization);
                if (longTermTemporalControlsAvailable)
                {
                    drawEnumOption(
                        "Execution Path",
                        overrides.executionPath,
                        static_cast<uint32_t>(
                            MiniEngineTaaExecutionPath::Count),
                        GetMiniEngineTaaExecutionPathLabel,
                        "Auto shows and uses only an adapter-specific validated TAA winner.",
                        GetMiniEngineTaaExecutionPathLabel(
                            resolvedForPerformanceLabels.executionPath),
                        true);
                    drawEnumOption(
                        "Compute Kernel",
                        overrides.computeKernel,
                        static_cast<uint32_t>(
                            MiniEngineTaaComputeKernel::Count),
                        GetMiniEngineTaaComputeKernelLabel,
                        "Compare the TAA 8x8 paired kernel with the 16x8 one-pixel kernel.",
                        GetMiniEngineTaaComputeKernelLabel(
                            resolvedForPerformanceLabels.computeKernel),
                        true);
                    drawEnumOption(
                        "LDS Layout",
                        overrides.ldsLayout,
                        static_cast<uint32_t>(
                            MiniEngineTaaLdsLayout::Count),
                        GetMiniEngineTaaLdsLayoutLabel,
                        "Compare TAA legacy, split, and split-and-packed shared-memory layouts.",
                        GetMiniEngineTaaLdsLayoutLabel(
                            resolvedForPerformanceLabels.ldsLayout),
                        true);
                    drawEnumOption(
                        "Shared-Work Reuse",
                        overrides.sharedWorkReuse,
                        static_cast<uint32_t>(
                            MiniEngineTaaAutoToggle::Count),
                        GetMiniEngineTaaAutoToggleLabel,
                        "Reuse overlapping TAA bounds and reconstruction work.",
                        resolvedForPerformanceLabels.sharedWorkReuse
                            ? "On"
                            : "Off",
                        true);
                    drawEnumOption(
                        "Early History Rejection",
                        overrides.earlyHistoryRejection,
                        static_cast<uint32_t>(
                            MiniEngineTaaAutoToggle::Count),
                        GetMiniEngineTaaAutoToggleLabel,
                        "Skip TAA history work only when confidence is exactly zero.",
                        resolvedForPerformanceLabels.earlyHistoryRejection
                            ? "On"
                            : "Off",
                        true);
                    if (presentationSharpenRequiresFusion)
                    {
                        drawMutexOption(
                            "Pass Fusion",
                            "Presentation sharpening currently requires the compatible TAA topology.");
                    }
                    else
                    {
                        drawEnumOption(
                            "Pass Fusion",
                            overrides.passFusion,
                            static_cast<uint32_t>(
                                MiniEngineTaaPassFusion::Count),
                            GetMiniEngineTaaPassFusionLabel,
                            "Fuse compatible TAA output work when no debug view requires an intermediate.",
                            GetMiniEngineTaaPassFusionLabel(
                                resolvedForPerformanceLabels.passFusion),
                            true);
                    }
                    drawEnumOption(
                        "Cache Blocking",
                        overrides.cacheBlocking,
                        static_cast<uint32_t>(
                            MiniEngineTaaCacheBlocking::Count),
                        GetMiniEngineTaaCacheBlockingLabel,
                        "TAA screen-band experiment; Auto remains Off without repeatable stall evidence.",
                        GetMiniEngineTaaCacheBlockingLabel(
                            resolvedForPerformanceLabels.cacheBlocking),
                        true);
                }
                else
                {
                    ImGui::TextDisabled(
                        "TAA performance overrides are unavailable for this method.");
                }
                EndAnimatedTreeNode();
            }
#endif
            if (motionTestRunning)
                ImGui::EndDisabled();

            ImGui::PopID();
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

        if (visibilityBenchmarkBusy)
            ImGui::EndDisabled();

        // Wheel motion is eased by the UI layer; this viewport fade keeps
        // partially clipped rows from popping into full contrast at either
        // edge while the settings list travels.
        DrawSettingsScrollEdgeFades();
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
            if (visibilityBenchmarkBusy)
                ImGui::BeginDisabled();

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

                    if (previousDomain != material->domain)
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
    AaBenchmarkConfig& aaBenchmark,
    VisibilityBenchmarkLaunchOptions& visibilityBenchmark)
{
    const auto invalidValue = [](
        const char* option,
        const std::string& value,
        const char* expected)
    {
        log::error(
            "Invalid %s value '%s'; expected %s",
            option,
            value.c_str(),
            expected);
        return false;
    };
    const auto missingValue = [](const char* option)
    {
        log::error("Missing value after %s", option);
        return false;
    };

    try
    {
    for (int i = 1; i < argc; i++)
    {
#if !UVSR_AA_DEVELOPER_OVERRIDES
        const bool developerAaOption =
            !strcmp(argv[i], "--aa-execution") ||
            !strcmp(argv[i], "--aa-kernel") ||
            !strcmp(argv[i], "--aa-lds") ||
            !strcmp(argv[i], "--aa-reuse") ||
            !strcmp(argv[i], "--aa-early") ||
            !strcmp(argv[i], "--aa-fusion") ||
            !strcmp(argv[i], "--aa-cache") ||
            !strcmp(argv[i], "--aa-smaa");
        if (developerAaOption)
        {
            log::error(
                "%s requires a build configured with "
                "UVSR_AA_DEVELOPER_OVERRIDES=ON",
                argv[i]);
            return false;
        }
#endif
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
        else if (!strcmp(argv[i], "--aa-benchmark-output") &&
            i + 1 < argc)
        {
            aaBenchmark.enabled = true;
            aaBenchmark.outputPath = argv[++i];
            benchmarkCameraRequested = true;
        }
        else if (!strcmp(argv[i], "--aa-method"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "spatial" ||
                value == "subpixel" ||
                value == "subpixel-morphological")
            {
                aaBenchmark.settings.method =
                    AntiAliasingMethod::SubpixelMorphological;
            }
            else if (value == "temporal" ||
                value == "temporal-subpixel" ||
                value == "temporal-subpixel-morphological")
            {
                aaBenchmark.settings.method =
                    AntiAliasingMethod::TemporalSubpixelMorphological;
            }
            else if (value == "cmaa2" ||
                value == "intel-cmaa2")
            {
                aaBenchmark.settings.method =
                    AntiAliasingMethod::IntelCmaa2;
            }
            else if (value == "msaa")
            {
                aaBenchmark.settings.method =
                    AntiAliasingMethod::Msaa;
            }
            else
            {
                return invalidValue(
                    "--aa-method",
                    value,
                    "spatial|temporal|cmaa2|msaa");
            }
        }
        else if (!strcmp(argv[i], "--aa-quality") ||
            !strcmp(argv[i], "--aa-preset"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "low")
                aaBenchmark.settings.quality = AntiAliasingQuality::Low;
            else if (value == "medium")
                aaBenchmark.settings.quality = AntiAliasingQuality::Medium;
            else if (value == "high")
                aaBenchmark.settings.quality = AntiAliasingQuality::High;
            else if (value == "ultra")
                aaBenchmark.settings.quality = AntiAliasingQuality::Ultra;
            else
                return invalidValue(
                    argv[i - 1],
                    value,
                    "low|medium|high|ultra");
        }
        else if (!strcmp(argv[i], "--aa-enabled"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "on")
                aaBenchmark.settings.enabled = true;
            else if (value == "off")
                aaBenchmark.settings.enabled = false;
            else
                return invalidValue("--aa-enabled", value, "on|off");
        }
        else if (!strcmp(argv[i], "--aa-execution"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "auto")
                aaBenchmark.settings.performanceOverrides.executionPath =
                    MiniEngineTaaExecutionPath::Auto;
            else if (value == "compute")
                aaBenchmark.settings.performanceOverrides.executionPath =
                    MiniEngineTaaExecutionPath::Compute;
            else if (value == "pixel")
                aaBenchmark.settings.performanceOverrides.executionPath =
                    MiniEngineTaaExecutionPath::FullscreenPixelShader;
            else
                return invalidValue(
                    "--aa-execution",
                    value,
                    "auto|compute|pixel");
        }
        else if (!strcmp(argv[i], "--aa-kernel"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "auto")
                aaBenchmark.settings.performanceOverrides.computeKernel =
                    MiniEngineTaaComputeKernel::Auto;
            else if (value == "8x8")
                aaBenchmark.settings.performanceOverrides.computeKernel =
                    MiniEngineTaaComputeKernel::Threads8x8TwoPixels;
            else if (value == "16x8")
                aaBenchmark.settings.performanceOverrides.computeKernel =
                    MiniEngineTaaComputeKernel::Threads16x8OnePixel;
            else
                return invalidValue(
                    "--aa-kernel",
                    value,
                    "auto|8x8|16x8");
        }
        else if (!strcmp(argv[i], "--aa-lds"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "auto")
                aaBenchmark.settings.performanceOverrides.ldsLayout =
                    MiniEngineTaaLdsLayout::Auto;
            else if (value == "legacy")
                aaBenchmark.settings.performanceOverrides.ldsLayout =
                    MiniEngineTaaLdsLayout::Legacy;
            else if (value == "split")
                aaBenchmark.settings.performanceOverrides.ldsLayout =
                    MiniEngineTaaLdsLayout::Split;
            else if (value == "packed" || value == "split-packed")
                aaBenchmark.settings.performanceOverrides.ldsLayout =
                    MiniEngineTaaLdsLayout::SplitAndPacked;
            else
                return invalidValue(
                    "--aa-lds",
                    value,
                    "auto|legacy|split|packed");
        }
        else if (!strcmp(argv[i], "--aa-reuse") ||
            !strcmp(argv[i], "--aa-early"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const bool reuse = !strcmp(argv[i], "--aa-reuse");
            const char* option = argv[i];
            const std::string value = argv[++i];
            MiniEngineTaaAutoToggle parsed;
            if (value == "auto")
                parsed = MiniEngineTaaAutoToggle::Auto;
            else if (value == "off")
                parsed = MiniEngineTaaAutoToggle::Off;
            else if (value == "on")
                parsed = MiniEngineTaaAutoToggle::On;
            else
                return invalidValue(option, value, "auto|off|on");
            if (reuse)
                aaBenchmark.settings.performanceOverrides.sharedWorkReuse =
                    parsed;
            else
                aaBenchmark.settings.performanceOverrides
                    .earlyHistoryRejection = parsed;
        }
        else if (!strcmp(argv[i], "--aa-fusion"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "auto")
                aaBenchmark.settings.performanceOverrides.passFusion =
                    MiniEngineTaaPassFusion::Auto;
            else if (value == "separate")
                aaBenchmark.settings.performanceOverrides.passFusion =
                    MiniEngineTaaPassFusion::Separate;
            else if (value == "fused")
                aaBenchmark.settings.performanceOverrides.passFusion =
                    MiniEngineTaaPassFusion::Fused;
            else
                return invalidValue(
                    "--aa-fusion",
                    value,
                    "auto|separate|fused");
        }
        else if (!strcmp(argv[i], "--aa-cache"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "auto")
                aaBenchmark.settings.performanceOverrides.cacheBlocking =
                    MiniEngineTaaCacheBlocking::Auto;
            else if (value == "off")
                aaBenchmark.settings.performanceOverrides.cacheBlocking =
                    MiniEngineTaaCacheBlocking::Off;
            else if (value == "2")
                aaBenchmark.settings.performanceOverrides.cacheBlocking =
                    MiniEngineTaaCacheBlocking::Bands2;
            else if (value == "3")
                aaBenchmark.settings.performanceOverrides.cacheBlocking =
                    MiniEngineTaaCacheBlocking::Bands3;
            else if (value == "4")
                aaBenchmark.settings.performanceOverrides.cacheBlocking =
                    MiniEngineTaaCacheBlocking::Bands4;
            else
                return invalidValue(
                    "--aa-cache",
                    value,
                    "auto|off|2|3|4");
        }
        else if (!strcmp(argv[i], "--aa-smaa"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "preset" || value == "auto")
                aaBenchmark.settings.algorithmOverrides.subpixelMorphology =
                    SmaaApplicationOverride::FromPreset;
            else if (value == "off" || value == "none")
                aaBenchmark.settings.algorithmOverrides.subpixelMorphology =
                    SmaaApplicationOverride::Off;
            else if (value == "full" || value == "all")
                aaBenchmark.settings.algorithmOverrides.subpixelMorphology =
                    SmaaApplicationOverride::FullScreen;
            else if (
                value == "cmaa" ||
                value == "cmaa2" ||
                value == "conservative")
            {
                aaBenchmark.settings.algorithmOverrides.subpixelMorphology =
                    SmaaApplicationOverride::ConservativeMorphological;
            }
            else if (value == "short")
                aaBenchmark.settings.algorithmOverrides.subpixelMorphology =
                    SmaaApplicationOverride::SelectiveShort;
            else if (value == "long")
                aaBenchmark.settings.algorithmOverrides.subpixelMorphology =
                    SmaaApplicationOverride::SelectiveLong;
            else
                return invalidValue(
                    "--aa-smaa",
                    value,
                    "preset|off|full|cmaa2|short|long");
        }
        else if (!strcmp(argv[i], "--aa-sharpness"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            aaBenchmark.sharpness = std::stof(argv[++i]);
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
                    "displayed Profile name");
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
                    "'. Use a displayed Profile name or its "
                    "hyphenated form.");
                return false;
            }
            visibilityBenchmark.implementationProfileSpecified = true;
        }
        else if (!strcmp(argv[i], "--visibility-benchmark"))
        {
            visibilityBenchmark.benchmarkRequested = true;
        }
        else if (!strcmp(
                argv[i], "--visibility-contribution-terminated-bounces"))
        {
            visibilityBenchmark.contributionTerminatedBounces = true;
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
    }
    catch (const std::exception& exception)
    {
        log::error(
            "Invalid command-line value: %s",
            exception.what());
        return false;
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
    AaBenchmarkConfig aaBenchmark;
    VisibilityBenchmarkLaunchOptions visibilityBenchmark;
    if (!ProcessCommandLine(
            __argc,
            __argv,
            deviceParams,
            sceneName,
            experimentDescription,
            benchmarkCameraRequested,
            aaBenchmark,
            visibilityBenchmark))
    {
        return 1;
    }
    if (visibilityBenchmark.benchmarkRequested)
    {
        // Automated benchmark failures must be visible to the caller instead
        // of being trapped in a modal Windows error box or debugger stream.
        log::ConsoleApplicationMode();
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
        if (aaBenchmark.enabled)
        {
            uiData.AntiAliasing = aaBenchmark.settings;
            uiData.MiniEngineTaaSharpness =
                ClampMiniEngineTaaSharpness(
                    aaBenchmark.sharpness);
        }
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
        if (visibilityBenchmark.contributionTerminatedBounces)
        {
            uiData.ScreenSpaceVisibility.enabled = true;
            uiData.ScreenSpaceVisibility.indirectDiffuse.enabled = true;
            uiData.ScreenSpaceVisibility.indirectDiffuse.limitBounces = false;
            uiData.VisibilityVerification =
                VisibilityVerificationProfile::Unset;
            uiData.EnablePbr = true;
            uiData.RenderMode = RendererMode::Deferred;
        }
        if (visibilityBenchmark.benchmarkRequested)
            uiData.ShowUI = false;

        std::shared_ptr<UvsrSceneViewer> demo = std::make_shared<UvsrSceneViewer>(
            deviceManager,
            uiData,
            sceneName,
            benchmarkCameraRequested,
            aaBenchmark);
        std::shared_ptr<UIRenderer> gui = std::make_shared<UIRenderer>(
            deviceManager,
            demo,
            uiData,
            programLaunchTime);

        gui->Init(demo->GetShaderFactory());
        if (visibilityBenchmark.benchmarkRequested)
        {
            // The ImGui console captures Donut's callback during Init. Restore
            // the default callback for headless runs so diagnostics continue
            // to reach the redirected console streams.
            log::ResetCallback();
        }

        bool runMessageLoop = true;
        if (visibilityBenchmark.benchmarkRequested)
        {
            runMessageLoop = demo->QueueVisibilityBenchmark(
                visibilityBenchmark.warmupFrameCount,
                visibilityBenchmark.measuredFrameCount,
                visibilityBenchmark.outputDirectory,
                visibilityBenchmark.autoClose);
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
