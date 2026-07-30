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
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <limits>
#include <string_view>
#include <system_error>
#include <iterator>
#include <utility>
#include <Windows.h>
#include <bcrypt.h>
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
#include <donut/engine/View.h>
#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/ForwardShadingPass.h>
#include <donut/render/GBuffer.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/GeometryPasses.h>
#include <donut/render/PixelReadbackPass.h>
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
#include "bend_screen_space_shadows.h"
#include "diagnostic_cascaded_shadow_map.h"
#include "diagnostic_csm_benchmark.h"
#include "gpu_performance_monitor.h"
#include "gpu_crash_diagnostics.h"
#include "camera_collision.h"
#include "camera_controllers.h"
#include "cmaa2.h"
#include "command_line_options.h"
#include "experiment_title.h"
#include "pixel_zoom.h"
#include "renderer_statistics.h"
#include "scene_catalog.h"
#include "scene_light_names.h"
#include "screen_space_visibility.h"
#include "sponza_camera_preset.h"
#include "sparse_virtual_shadow_map.h"
#include "svsm_motion_benchmark.h"
#include "taa_miniengine.h"
#include "ui_animation.h"
#include "visibility_perf_capture.h"
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
static VisibilityPerfCapture g_VisibilityPerfCapture(
    LoadVisibilityPerfCaptureOptions());
static bool g_VisibilityPerfDisableRendererTimers =
    g_VisibilityPerfCapture.Enabled() &&
    g_VisibilityPerfCapture.Options().HasVariant("timersoff");
static uint32_t g_VisibilityTaaPrimeFramesRemaining =
    g_VisibilityPerfCapture.Enabled() &&
        g_VisibilityPerfCapture.Options().HasVariant("taaprime")
    ? 120u : 0u;
static constexpr uint32_t MaxVisibilityBenchmarkWarmupFrames = 100000u;
static constexpr uint64_t VisibilityBenchmarkQueryDrainAllowanceFrames = 120u;

static const char* GetLiveProcessPriorityLabel()
{
    switch (GetPriorityClass(GetCurrentProcess()))
    {
    case IDLE_PRIORITY_CLASS: return "Idle";
    case BELOW_NORMAL_PRIORITY_CLASS: return "Below Normal";
    case NORMAL_PRIORITY_CLASS: return "Normal";
    case ABOVE_NORMAL_PRIORITY_CLASS: return "Above Normal";
    case HIGH_PRIORITY_CLASS: return "High";
    case REALTIME_PRIORITY_CLASS: return "Realtime";
    default: return "Unknown";
    }
}

static void ApplyVisibilityPerfProcessPriority()
{
    // This experimental performance build runs at High priority by default.
    // Captures can still request Normal explicitly for controlled A/B tests.
    DWORD requested = HIGH_PRIORITY_CLASS;
    if (g_VisibilityPerfCapture.Enabled() &&
        g_VisibilityPerfCapture.Options().priority ==
        VisibilityPerfPriority::Normal)
    {
        requested = NORMAL_PRIORITY_CLASS;
    }
    else if (g_VisibilityPerfCapture.Enabled() &&
        g_VisibilityPerfCapture.Options().priority ==
        VisibilityPerfPriority::High)
    {
        requested = HIGH_PRIORITY_CLASS;
    }
    if (requested != 0u &&
        !SetPriorityClass(GetCurrentProcess(), requested))
    {
        log::warning(
            "UVSR_PERF could not set process priority (Win32 error %lu)",
            GetLastError());
    }
    log::info(
        "UVSR_PERF live process priority: %s",
        GetLiveProcessPriorityLabel());
}

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
    float opacity = 1.f;
    float shadowBlur = 0.f;
    float shadowOpacity = 0.f;
    float shadowOffsetY = 0.f;
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
    ~AaBenchmarkTimingTagFlag;

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
    uint32_t requestedSampleCount,
    bool enablePbr)
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
        bool enablePbr = false;
        uint32_t resolvedSampleCount = 1u;
    };
    static MsaaSampleCountCache cache;
    if (cache.device == nativeDevice &&
        cache.requestedSampleCount == requestedSampleCount &&
        cache.enablePbr == enablePbr)
    {
        return cache.resolvedSampleCount;
    }
    const auto cacheResolution =
        [&](uint32_t resolvedSampleCount)
    {
        cache.device = nativeDevice;
        cache.requestedSampleCount = requestedSampleCount;
        cache.enablePbr = enablePbr;
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
        // RenderTargets still allocates the deferred attachments while MSAA
        // routes drawing through forward PBR. Check every format that receives
        // the selected sample count rather than only the HDR attachment.
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

        if (enablePbr)
        {
            constexpr DXGI_FORMAT pbrColorFormats[] = {
                DXGI_FORMAT_R8G8B8A8_UNORM,
                DXGI_FORMAT_R8_UNORM
            };
            for (DXGI_FORMAT format : pbrColorFormats)
            {
                if (!supportsFormat(format, sampleCount))
                    return false;
            }
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
    MarkScreenSpaceVisibilityQualityCustom(visibility);
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
    default:
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

    visibility.sampling.maximumSampleCount = 8u;

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
    bool autoClose = false;
    bool contributionTerminatedBounces = false;
};

struct UIData
{
    bool                                ShowUI = false;
    std::array<UiBackdropRect, 3>        BackdropRects;
    PixelZoomMode                       PixelZoom =
        PixelZoomMode::Off;
    std::vector<GpuAdapterChoice>       GpuAdapterChoices;
    int                                 ActiveGpuAdapterIndex = -1;
    bool                                EnablePbr = true;
    RendererMode                        RenderMode = RendererMode::Deferred;
    AntiAliasingSettings                AntiAliasing;
    bool                                MiniEngineTaaSharpenEnabled = false;
    float                               MiniEngineTaaSharpness =
        MiniEngineTaaDefaultSharpness;
    MiniEngineTaaDebugView              MiniEngineTaaVisualization =
        MiniEngineTaaDebugView::Off;
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

    [[nodiscard]] bool UsesCmaa2() const
    {
        return AntiAliasing.enabled &&
            GetResolvedAntiAliasingSettings()
                    .subpixelMorphology ==
                MorphologyApplication::ConservativeMorphological;
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
            observed.indirectDiffuse.includeEmissive !=
                expected.indirectDiffuse.includeEmissive,
            "GI emissive-source state does not match the profile.")).empty())
        return reason;
    if (!(reason = mismatch(
            observed.indirectDiffuse.emissiveGain !=
                expected.indirectDiffuse.emissiveGain,
            "GI emissive-source gain does not match the profile.")).empty())
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

enum class RendererTimingStage : uint32_t
{
    CompleteFrame,
    SceneSetup,
    Geometry,
    DirectLighting,
    ScreenSpaceVisibility,
    MaterialPicking,
    EnvironmentBackground,
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
    uint64_t                            m_SubmittedMainViewTriangles = 0u;

    float                               m_CameraVerticalFov = 60.f;
    float                               m_SceneDiagonal = 100.f;
    float                               m_CameraCollisionRadius = 0.1f;
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
    std::string                         m_AaMotionTestStatus;
    std::vector<AaBenchmarkSample>      m_AaBenchmarkSamples;
    uint64_t                            m_AntiAliasingPhase = 0u;
    bool                                m_HasAppliedAntiAliasingSettings =
        false;
    AntiAliasingSettings                m_AppliedAntiAliasingSettings;
    bool                                m_VisibilityBenchmarkQueued = false;
    uint32_t                            m_VisibilityBenchmarkWarmup = 120u;
    uint32_t                            m_VisibilityBenchmarkFrames = 240u;
    uint64_t                            m_VisibilityBenchmarkRenderedFrames = 0u;
    bool                                m_VisibilityBenchmarkAutoClose = false;
    bool                                m_VisibilityBenchmarkOwnsCameraLock = false;
    CameraMode                          m_VisibilityBenchmarkPreviousCameraMode =
        CameraMode::ThirdPerson;
    int                                 m_VisibilityBenchmarkPreviousWindowWidth = 0;
    int                                 m_VisibilityBenchmarkPreviousWindowHeight = 0;
    bool                                m_HasVisibilityBenchmarkSummary = false;
    VisibilityBenchmarkSummary          m_LastVisibilityBenchmarkSummary;
    std::string                         m_VisibilityBenchmarkStatus;
    std::string                         m_VisibilityBenchmarkError;
    std::string                         m_VisibilityBenchmarkPermutation;
    bool                                m_SceneFinishedLoading = false;
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
        "Idle.";
    bool                                m_SponzaCameraLocationsAvailable = false;
    uint64_t                            m_SvsmSceneStateRevision = 1u;
    uint64_t                            m_SvsmCasterStateRevision = 1u;
    bool                                m_SvsmSceneStateRevisionReliable = true;
    std::vector<const SceneGraphNode*>  m_SvsmDirtyNodeScratch;
    SponzaCameraLocation                m_SponzaCameraLocation =
        SponzaCameraLocation::SimplifiedApproximation;

    UIData&                             m_ui;

    std::string GetActiveAdapterName() const;
    void UpdateVisibilityBenchmarkAfterRender();
    void FailVisibilityBenchmark(const std::string& message);
    void ReleaseVisibilityBenchmarkCameraLock();
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
        const AaBenchmarkConfig& aaBenchmark,
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
        , m_AaBenchmark(aaBenchmark)
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

    void CancelAntiAliasingMotionTest()
    {
        if (!IsAntiAliasingMotionTestRunning())
            return;

        m_AaMotionTestStatus = "Canceled.";
        m_AaBenchmark.enabled = false;
        m_AaBenchmarkStarted = false;
        m_AaBenchmarkPacingActive = false;
        m_AaBenchmarkCurrentTag = AaBenchmarkTimerTag{};
        m_InteractiveAaMotionTest = false;
        m_BenchmarkCameraActive = false;
        SetCameraMode(CameraMode::ThirdPerson);
        m_SponzaCameraLocation = SponzaCameraLocation::Free;
        if (m_AaMotionTestPreviousWidth > 0 &&
            m_AaMotionTestPreviousHeight > 0)
        {
            glfwSetWindowSize(
                GetDeviceManager()->GetWindow(),
                m_AaMotionTestPreviousWidth,
                m_AaMotionTestPreviousHeight);
        }
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
            m_ui.AntiAliasing.enabled;
        m_SvsmMotionBenchmarkPreviousTaaSharpenEnabled =
            m_ui.MiniEngineTaaSharpenEnabled;
        m_SvsmMotionBenchmarkPreviousBendEnabled =
            m_ui.BendScreenSpaceShadows.enabled;
        m_SvsmMotionBenchmarkPreviousCsmEnabled =
            m_ui.DiagnosticCascadedShadowMaps.enabled;
        m_SvsmMotionBenchmarkPreviousScreenSpaceVisibilityEnabled =
            m_ui.ScreenSpaceVisibility.enabled;

        // Keep the benchmark lane independent from other visibility
        // producers and temporal consumers. The previous values are restored
        // after either completion or an abort.
        m_ui.AntiAliasing.enabled = false;
        m_ui.MiniEngineTaaSharpenEnabled = false;
        m_ui.BendScreenSpaceShadows.enabled = false;
        m_ui.DiagnosticCascadedShadowMaps.enabled = false;
        m_ui.ScreenSpaceVisibility.enabled = false;

        m_SvsmMotionBenchmarkStartSettings =
            m_ui.SparseVirtualShadowMaps;
        m_SvsmMotionBenchmarkStartTaaEnabled =
            m_ui.AntiAliasing.enabled;
        m_SvsmMotionBenchmarkStartUsesTaa =
            m_ui.UsesLongTermTemporalAA();
        m_SvsmMotionBenchmarkStartTaaSharpenEnabled =
            m_ui.MiniEngineTaaSharpenEnabled;
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
            m_ui.AntiAliasing.enabled =
                m_SvsmMotionBenchmarkPreviousTaaEnabled;
            m_ui.MiniEngineTaaSharpenEnabled =
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
        m_ui.AntiAliasing.enabled =
            m_SvsmMotionBenchmarkPreviousTaaEnabled;
        m_ui.MiniEngineTaaSharpenEnabled =
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
            << (m_ui.AntiAliasing.enabled ? 1u : 0u) << "\n"
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
        if (m_ui.AntiAliasing.enabled !=
                m_SvsmMotionBenchmarkStartTaaEnabled ||
            m_ui.UsesLongTermTemporalAA() !=
                m_SvsmMotionBenchmarkStartUsesTaa ||
            m_ui.MiniEngineTaaSharpenEnabled !=
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
        m_ui.AntiAliasing = AntiAliasingSettings{};
        m_ui.MiniEngineTaaSharpenEnabled = false;
        m_ui.MiniEngineTaaSharpness = MiniEngineTaaDefaultSharpness;
        m_ui.MiniEngineTaaVisualization = MiniEngineTaaDebugView::Off;
        m_ui.BendScreenSpaceShadows =
            BendScreenSpaceShadowSettings{};
        m_ui.SparseVirtualShadowMaps =
            SparseVirtualShadowMapSettings{};
        m_ui.DiagnosticCascadedShadowMaps =
            DiagnosticCascadedShadowMapSettings{};
        m_ui.ScreenSpaceVisibility = ScreenSpaceVisibilitySettings{};
        m_ui.VisibilityVerification =
            VisibilityVerificationProfile::Unset;
        m_ui.PixelZoom = PixelZoomMode::Off;
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

        if (action == GLFW_PRESS &&
            button == GLFW_MOUSE_BUTTON_MIDDLE)
        {
            m_Pick = true;
        }

        return true;
    }

    virtual bool MouseScrollUpdate(double xoffset, double yoffset) override
    {
        GetActiveCamera().MouseScrollUpdate(xoffset, yoffset);

        return true;
    }

    void AdvanceAntiAliasingTimer()
    {
        if (g_VisibilityPerfDisableRendererTimers)
            return;
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
        if (g_VisibilityPerfDisableRendererTimers)
            return false;
        // The query exists only for the interactive AA benchmark. Issuing an
        // empty timestamp envelope every frame while AA and its benchmark are
        // off adds D3D12 query and resolve work with no consumer.
        if (!m_AaBenchmark.enabled)
            return false;
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
        if (g_VisibilityPerfDisableRendererTimers)
            return;
        if (!m_AaBenchmark.enabled)
            return;
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
        const ResolvedAntiAliasingSettings resolved =
            m_ui.GetResolvedAntiAliasingSettings();
        const bool benchmarkEvidenceValid =
            complete.count == 256u &&
            m_AaBenchmarkIssuedSamples == 256u &&
            m_AaBenchmarkDroppedSamples == 0u &&
            m_AaBenchmarkOutstandingSamples == 0u;
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
            << std::quoted(GetMorphologyApplicationLabel(
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
            << "  }\n"
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
                "(AA %llu/256, AA drops %llu)",
                m_AaBenchmark.outputPath.generic_string().c_str(),
                static_cast<unsigned long long>(complete.count),
                static_cast<unsigned long long>(
                    m_AaBenchmarkDroppedSamples));
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
            m_AaBenchmarkIssuedSamples = 0u;
            m_AaBenchmarkDroppedSamples = 0u;
            m_AaBenchmarkOutstandingSamples = 0u;
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
        if (m_AaBenchmarkFrame >=
                AaBenchmarkMotionEndFrame + MinimumDrainFrames &&
            m_AaBenchmarkOutstandingSamples == 0u)
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
        if (m_DiagnosticCascadedShadowMapPass)
            m_DiagnosticCascadedShadowMapPass->ResetSceneState();
        if (m_SparseVirtualShadowMapPass)
            m_SparseVirtualShadowMapPass->Deactivate();
        m_BindingCache.Clear();
        m_SunLight.reset();
        m_ui.SelectedMaterial = nullptr;
        m_ui.SelectedNode = nullptr;
        m_OriginalMaterials.clear();
        m_PreviousView.reset();
        m_CameraCollisionWorld.Clear();
        m_SubmittedMainViewTriangles = 0u;

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
        const AntiAliasingSettings& applied =
            m_AppliedAntiAliasingSettings;
        const AntiAliasingSettings& requested =
            m_ui.AntiAliasing;
        if (m_HasAppliedAntiAliasingSettings &&
            applied.enabled == requested.enabled &&
            applied.method == requested.method &&
            applied.quality == requested.quality &&
            applied.algorithmOverrides ==
                requested.algorithmOverrides &&
            applied.performanceOverrides ==
                requested.performanceOverrides)
        {
            return;
        }

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

    void CreateMiniEngineTemporalAAPass()
    {
        m_MiniEngineTemporalAAPass.reset();
        if (!m_ui.UsesLongTermTemporalAA())
            return;

        m_MiniEngineTemporalAAPass =
            std::make_unique<MiniEngineTemporalAAPass>(
                GetDevice(),
                m_ShaderFactory,
                m_CommonPasses,
                m_RenderTargets->HdrColor,
                m_RenderTargets->Depth,
                m_RenderTargets->MotionVectors);
    }

    void EnsureMsaaVisibilityResolvePass()
    {
        if (!m_ui.EnablePbr ||
            !m_RenderTargets->VisibilityResourcesEnabled)
        {
            m_MsaaVisibilityResolvePass.reset();
            return;
        }
        if (m_MsaaVisibilityResolvePass)
            return;

        // All four sample-count PSOs are static. Materialize them while the
        // visibility renderer is first created instead of on the first Method
        // change to Multisample Reference.
        m_MsaaVisibilityResolvePass =
            std::make_unique<MsaaVisibilityResolvePass>(GetDevice());
        m_MsaaVisibilityResolvePass->Init(m_ShaderFactory);
    }

    void RefreshAntiAliasingTargetPasses(bool sampleCountChanged)
    {
        // An AA method can change sample count and motion-vector topology
        // without changing the renderer, visibility consumers, or window.
        // Keep those expensive independent passes alive and refresh only the
        // objects whose shader or binding topology actually names a replaced
        // RenderTargets resource.
        if (m_ScreenSpaceVisibilityPass &&
            m_ScreenSpaceVisibilityPass->IsBenchmarkActive())
        {
            FailVisibilityBenchmark(
                "The anti-aliasing render-target topology changed during "
                "the visibility run.");
        }

        m_MiniEngineTemporalAAPass.reset();

        if (sampleCountChanged)
        {
            ForwardShadingPass::CreateParameters forwardParams;
            forwardParams.trackLiveness = false;
            if (m_ui.EnablePbr)
            {
                m_ForwardPass =
                    std::make_shared<PbrForwardShadingPass>(
                        GetDevice(),
                        m_CommonPasses,
                        m_ui.WhiteWorld != WhiteWorldMode::Off);
            }
            else
            {
                m_ForwardPass =
                    std::make_shared<ForwardShadingPass>(
                        GetDevice(), m_CommonPasses);
            }
            m_ForwardPass->Init(*m_ShaderFactory, forwardParams);
        }

        GBufferFillPass::CreateParameters gbufferParams;
        gbufferParams.enableMotionVectors =
            m_RenderTargets->MotionVectorsEnabled;
        if (m_ui.EnablePbr)
        {
            m_GBufferPass = std::make_shared<PbrGBufferFillPass>(
                GetDevice(),
                m_CommonPasses,
                m_ui.WhiteWorld != WhiteWorldMode::Off);
        }
        else
        {
            m_GBufferPass = std::make_shared<GBufferFillPass>(
                GetDevice(), m_CommonPasses);
        }
        m_GBufferPass->Init(*m_ShaderFactory, gbufferParams);

        m_PixelReadbackPass = std::make_unique<PixelReadbackPass>(
            GetDevice(),
            m_ShaderFactory,
            m_RenderTargets->MaterialIDs,
            nvrhi::Format::RGBA32_UINT);

        if (m_DeferredLightingPass)
            m_DeferredLightingPass->ResetBindingCache();
        if (m_PbrDeferredLightingPass)
            m_PbrDeferredLightingPass->ResetBindingCache();
        if (m_ScreenSpaceVisibilityPass)
        {
            m_ScreenSpaceVisibilityPass->ResetBindingCache();
            // The old full pass recreation also discarded visibility history.
            // Preserve that image-correct transition when raster samples,
            // motion-vector topology, or temporal jitter ownership changes.
            m_ScreenSpaceVisibilityPass->ResetHistory();
        }
        EnsureMsaaVisibilityResolvePass();

        CreateMiniEngineTemporalAAPass();
        if (m_Cmaa2Pass)
        {
            // CMAA2 owns only same-sized single-sample intermediates and can
            // safely survive an MSAA/motion-vector target swap. Rebinding its
            // source avoids recreating the large candidate buffers and all 16
            // quality PSOs on every Method change.
            m_Cmaa2Pass->UpdateSourceColor(
                GetResolvedMorphologySource());
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
                    m_RenderTargets->ForwardFramebuffer,
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

    void CreateRenderPasses()
    {
        m_MiniEngineTemporalAAPass.reset();
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
            EnsureMsaaVisibilityResolvePass();
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
            m_ScreenSpaceVisibilityPass =
                std::make_unique<ScreenSpaceVisibilityPass>(
                    GetDevice(),
                    m_ShaderFactory,
                    m_CommonPasses);
        }
        else
        {
            m_PbrDeferredLightingPass.reset();
            m_MsaaVisibilityResolvePass.reset();
            m_BendScreenSpaceShadowPass.reset();
            m_SparseVirtualShadowMapPass.reset();
            m_DiagnosticCascadedShadowMapPass.reset();
            m_ScreenSpaceVisibilityPass.reset();
            m_DeferredLightingPass = std::make_shared<DeferredLightingPass>(GetDevice(), m_CommonPasses);
            m_DeferredLightingPass->Init(m_ShaderFactory);
        }

        CreateMiniEngineTemporalAAPass();

        if (m_ui.UsesCmaa2())
            CreateCmaa2Pass();

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
            !m_ui.AntiAliasing.enabled &&
            !m_ui.UsesLongTermTemporalAA() &&
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
        if (g_VisibilityTaaPrimeFramesRemaining != 0u &&
            --g_VisibilityTaaPrimeFramesRemaining == 0u)
        {
            m_ui.AntiAliasing.enabled = false;
            log::info(
                "UVSR_PERF TAA prime complete; AA disabled before capture");
        }
        UpdateSvsmMotionBenchmark();

        int windowWidth, windowHeight;
        GetDeviceManager()->GetWindowDimensions(windowWidth, windowHeight);
        constexpr int visibilityCaptureWidth = 1896;
        constexpr int visibilityCaptureHeight = 1064;
        if (g_VisibilityPerfCapture.Enabled() &&
            (windowWidth != visibilityCaptureWidth ||
                windowHeight != visibilityCaptureHeight))
        {
            glfwSetWindowSize(
                GetDeviceManager()->GetWindow(),
                visibilityCaptureWidth,
                visibilityCaptureHeight);
        }
        const bool visibilityPerfReady =
            g_VisibilityPerfCapture.Enabled() &&
            m_BenchmarkCameraActive &&
            windowWidth == visibilityCaptureWidth &&
            windowHeight == visibilityCaptureHeight &&
            m_ScreenSpaceVisibilityPass &&
            m_ScreenSpaceVisibilityPass->GetTimings().profileValid &&
            m_ui.EnablePbr &&
            m_ui.UsesDeferredShading() &&
            m_ui.ScreenSpaceVisibility.HasActiveConsumer();
        g_VisibilityPerfCapture.BeginFrame(visibilityPerfReady);
        if (g_VisibilityPerfCapture.ReadyToClose())
        {
            glfwSetWindowShouldClose(
                GetDeviceManager()->GetWindow(), GLFW_TRUE);
        }
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

            const uint sampleCount =
                !m_ui.AntiAliasing.enabled
                    ? 1u
                    : ResolveSupportedMsaaSampleCount(
                        GetDevice(),
                        m_ui.GetResolvedAntiAliasingSettings()
                            .rasterSampleCount,
                        m_ui.EnablePbr);
            const bool screenSpaceVisibilityResourcesRequired =
                m_ui.EnablePbr &&
                m_ui.IsScreenSpaceVisibilityAvailable() &&
                m_ui.HasActiveScreenSpaceVisibilityConsumer();
            // The directional visibility producers consume a single coherent
            // closest surface. Keep the resolve targets allocated for every
            // deferred PBR MSAA topology so toggling Bend, SVSM, or diagnostic
            // CSM does not force an unrelated render-pass rebuild.
            const bool msaaClosestSurfaceResolveResourcesRequired =
                m_ui.EnablePbr &&
                m_ui.UsesDeferredShading() &&
                sampleCount > 1u;
            const bool visibilityResourcesRequired =
                screenSpaceVisibilityResourcesRequired ||
                msaaClosestSurfaceResolveResourcesRequired;
            const bool visibilitySourceRadianceRequired =
                screenSpaceVisibilityResourcesRequired &&
                m_ui.ScreenSpaceVisibility.HasActiveIndirectDiffuse() &&
                (!sceneLights.empty() ||
                    IsImageBasedLightingLobeActive(
                        m_ui.EnableDiffuseIbl,
                        m_ui.DiffuseIblStrength) ||
                    (m_ui.ScreenSpaceVisibility.indirectDiffuse.includeEmissive &&
                        m_ui.ScreenSpaceVisibility.indirectDiffuse.emissiveGain > 0.f));
            const bool temporalAARequired =
                m_ui.UsesLongTermTemporalAA();
            const bool cmaa2Required = m_ui.UsesCmaa2();
            const bool motionVectorsRequired =
                m_ui.RequiresAntiAliasingMotionVectors() ||
                (visibilityResourcesRequired &&
                    (m_ui.ScreenSpaceVisibility
                            .RequiresMotionVectors() ||
                        sampleCount > 1u));

            bool needNewPasses = false;
            bool refreshAntiAliasingTargetPasses = false;
            bool antiAliasingSampleCountChanged = false;

            if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(
                uint2(width, height), sampleCount, m_ui.EnablePbr,
                visibilityResourcesRequired,
                visibilitySourceRadianceRequired,
                motionVectorsRequired))
            {
                const bool hadRenderTargets = bool(m_RenderTargets);
                const bool sameNonAaTopology = hadRenderTargets &&
                    all(m_RenderTargets->GetSize() == uint2(width, height)) &&
                    m_RenderTargets->PbrEnabled == m_ui.EnablePbr &&
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

                m_RenderTargets = nullptr;
                m_BindingCache.Clear();
                m_RenderTargets = std::make_unique<RenderTargets>();
                m_RenderTargets->Init(
                    GetDevice(), uint2(width, height), sampleCount,
                    motionVectorsRequired, true, m_ui.EnablePbr,
                    visibilityResourcesRequired,
                    visibilitySourceRadianceRequired);
                m_PreviousView.reset();

                refreshAntiAliasingTargetPasses =
                    sameNonAaTopology && antiAliasingTopologyChanged;
                needNewPasses = !refreshAntiAliasingTargetPasses;
            }

            const bool refreshTemporalPass =
                temporalAARequired != bool(m_MiniEngineTemporalAAPass);
            if (SetupView())
            {
                needNewPasses = true;
                m_PreviousView.reset();
            }

            if (m_ui.ShaderReloadRequested)
            {
                m_DiagnosticCascadedShadowMapPass.reset();
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
                RefreshAntiAliasingTargetPasses(
                    antiAliasingSampleCountChanged);
            }
            else if (refreshTemporalPass)
            {
                // A method transition does not invalidate forward, G-buffer,
                // lighting, visibility, sky, or output passes while the
                // render-target topology stays unchanged.
                CreateMiniEngineTemporalAAPass();
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
        const bool runScreenSpaceVisibility =
            m_ui.EnablePbr &&
            m_ui.IsScreenSpaceVisibilityAvailable() &&
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
            (!m_ui.ScreenSpaceVisibility.indirectDiffuse.limitBounces ||
                m_ui.ScreenSpaceVisibility.indirectDiffuse.bounceCount >
                    1u);

        m_RenderTargets->Clear(m_CommandList);
        BendScreenSpaceShadowResult bendShadowResult;
        SparseVirtualShadowMapResult sparseVirtualShadowMapResult;
        DiagnosticCascadedShadowMapResult diagnosticCsmResult;
        DirectionalLightVisibilitySet directionalVisibility{};
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

            m_MsaaVisibilityResolvePass->Render(
                m_CommandList,
                resolveInputs,
                closestSurfaceOutputs,
                m_RenderTargets->GetSampleCount());
            closestSurfaceResolved = true;
            return true;
        };

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
        DeferredLightingPass::Inputs deferredMsaaInputs;
        bool deferredMsaaLightingPending = false;
        bool deferredMsaaVisibilityPending = false;
        m_SubmittedMainViewTriangles = 0u;

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
        EndRendererStage(RendererTimingStage::SceneSetup);

        if (m_ui.UsesDeferredShading())
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
                m_ui.BendScreenSpaceShadows.enabled ||
                m_ui.SparseVirtualShadowMaps.enabled ||
                m_ui.DiagnosticCascadedShadowMaps.enabled;
            if (m_RenderTargets->GetSampleCount() > 1u &&
                (runScreenSpaceVisibility ||
                    directionalVisibilityProducerEnabled))
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
            nvrhi::ITexture* visibilityNormals =
                closestSurfaceResolved
                    ? closestSurfaceOutputs.normals
                    : m_RenderTargets->GBufferNormals.Get();

            if (m_ui.EnablePbr &&
                m_BendScreenSpaceShadowPass &&
                (!m_ui.BendScreenSpaceShadows.enabled ||
                    singleSurfaceInputsAvailable))
            {
                bendShadowResult = m_BendScreenSpaceShadowPass->Render(
                    m_CommandList,
                    m_ui.BendScreenSpaceShadows,
                    *m_View,
                    visibilityDepth,
                    m_SunLight.get());
            }

            if (m_ui.EnablePbr &&
                m_SparseVirtualShadowMapPass &&
                (!m_ui.SparseVirtualShadowMaps.enabled ||
                    singleSurfaceInputsAvailable))
            {
                sparseVirtualShadowMapResult =
                    m_SparseVirtualShadowMapPass->Render(
                        m_CommandList,
                        m_ui.SparseVirtualShadowMaps,
                        *m_View,
                        visibilityDepth,
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
            if (m_ui.EnablePbr &&
                m_DiagnosticCascadedShadowMapPass &&
                (!m_ui.DiagnosticCascadedShadowMaps.enabled ||
                    singleSurfaceInputsAvailable))
            {
                diagnosticCsmResult =
                    m_DiagnosticCascadedShadowMapPass->Render(
                        m_CommandList,
                        m_ui.DiagnosticCascadedShadowMaps,
                        *m_View,
                        visibilityDepth,
                        visibilityNormals,
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
            directionalVisibility = {{
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
                    m_PbrDeferredLightingPass->Render(
                        m_CommandList,
                        *m_View,
                        visibilityDeferredInputs,
                        directionalVisibility,
                        globalEnvironment,
                        m_RenderTargets
                            ->DirectDiffuseRadiance,
                        true,
                        writeSourceRadiance,
                        writeBounceMetadata,
                        m_ui.ScreenSpaceVisibility.indirectDiffuse
                            .includeEmissive,
                        m_ui.ScreenSpaceVisibility.indirectDiffuse
                            .emissiveGain,
                        uint32_t(m_ui.LightingDebugView),
                        float2(0.f));

                    ScreenSpaceVisibilityInputs
                        visibilityInputs;
                    visibilityInputs.depth =
                        closestSurfaceOutputs.depth;
                    visibilityInputs.normals =
                        closestSurfaceOutputs.normals;
                    visibilityInputs.motionVectors =
                        closestSurfaceOutputs.motionVectors;
                    visibilityInputs.sourceRadiance =
                        m_RenderTargets
                            ->DirectDiffuseRadiance;
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
                    visibilityInputs.output =
                        m_RenderTargets
                            ->VisibilityComposite;
                    visibilityInputs
                        .knownInactiveLightingSources =
                            knownInactiveLightingSources;
                    BeginRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    m_ScreenSpaceVisibilityPass->Render(
                        m_CommandList,
                        m_ui.ScreenSpaceVisibility,
                        *m_View,
                        visibilityInputs,
                        // The production display path has fixed neutral
                        // exposure while lighting remains under development.
                        1.f,
                        uint32_t(GetFrameIndex()));
                    EndRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    UpdateVisibilityBenchmarkAfterRender();
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
                    BeginRendererStage(
                        RendererTimingStage::ScreenSpaceVisibility);
                    m_ScreenSpaceVisibilityPass->Render(
                        m_CommandList,
                        m_ui.ScreenSpaceVisibility,
                        *m_View,
                        visibilityInputs,
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
            SubmittedTriangleCountingPass geometryPass(
                *m_ForwardPass);
            BeginRendererStage(RendererTimingStage::Geometry);
            RenderCompositeView(m_CommandList,
                m_View.get(), m_View.get(),
                *m_RenderTargets->ForwardFramebuffer,
                m_Scene->GetSceneGraph()->GetRootNode(),
                *m_OpaqueDrawStrategy,
                geometryPass,
                forwardContext,
                "ForwardOpaque",
                false);
            m_SubmittedMainViewTriangles =
                geometryPass.GetSubmittedTriangles();
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

        if (m_ui.ShowEnvironmentBackground &&
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
            // Report the final per-sample material decode and PBR evaluation as
            // the MSAA direct-lighting stage. The earlier closest-surface pass
            // is visibility preparation and cannot share this single timer
            // query without also enclosing unrelated intervening work.
            BeginRendererStage(RendererTimingStage::DirectLighting);
            m_PbrDeferredLightingPass->Render(
                m_CommandList,
                *m_View,
                deferredMsaaInputs,
                directionalVisibility,
                globalEnvironment,
                nullptr,
                deferredMsaaVisibilityPending,
                false,
                false,
                false,
                0.f,
                uint32_t(m_ui.LightingDebugView),
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

        const bool antiAliasingTimerActive =
            BeginAntiAliasingTimer();
        nvrhi::ITexture* antiAliasedTexture =
            sceneColor;
        if (m_ui.AntiAliasing.enabled)
        {
            const ResolvedAntiAliasingSettings antiAliasing =
                m_ui.GetResolvedAntiAliasingSettings();
#if UVSR_AA_DEVELOPER_OVERRIDES
            const MiniEngineTaaDebugView activeAaVisualization =
                m_ui.MiniEngineTaaVisualization;
#else
            // Debug shaders and routing are absent from production. Ignore
            // stale or hostile programmatic state before it can alter the
            // shipping render graph.
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
                m_Cmaa2Pass &&
                IsLongTermTemporalPreset(
                    antiAliasing.implementation) &&
                antiAliasing.subpixelMorphology !=
                    MorphologyApplication::Off &&
                !miniEngineDebugVisualizationActive &&
                temporalSharpenEnabled;
            if (m_MiniEngineTemporalAAPass)
            {
                // Resolve scene-linear radiance before any display transform.
                // The pass intentionally has no exposure, grading, LUT, or
                // transfer dependency, so removing or replacing the display
                // stage does not change its contract.
                antiAliasedTexture =
                    m_MiniEngineTemporalAAPass->Render(
                    m_CommandList,
                    *m_View,
                    m_PreviousView.get(),
                    m_AntiAliasingPhase,
                    antiAliasing,
                    activeAaVisualization,
                    false,
                    temporalSharpenEnabled &&
                        !deferTemporalSharpenToPresentation,
                    deferTemporalSharpenToPresentation,
                    m_ui.MiniEngineTaaSharpness);
            }

            bool cmaa2RenderedThisFrame = false;
            if (m_Cmaa2Pass &&
                antiAliasing.subpixelMorphology ==
                    MorphologyApplication::ConservativeMorphological)
            {
                antiAliasedTexture = m_Cmaa2Pass->Render(
                    m_CommandList,
                    antiAliasedTexture,
                    antiAliasing.morphologyQuality);
                cmaa2RenderedThisFrame = true;
            }
            if (m_Cmaa2Pass && !cmaa2RenderedThisFrame)
                m_Cmaa2Pass->MarkInactiveFrame();

            if (deferTemporalSharpenToPresentation)
            {
                // Apply the same sharpness to the composed presentation
                // result. Blending sharpened temporal against unsharpened
                // spatial current made a changing selective rejection mask
                // modulate edge detail.
                antiAliasedTexture =
                    m_MiniEngineTemporalAAPass
                        ->SharpenPresentation(
                            m_CommandList,
                            antiAliasedTexture);
            }
        }
        EndAntiAliasingTimer(antiAliasingTimerActive);

        nvrhi::ITexture* displayTexture = antiAliasedTexture;
        if (m_ui.UsesTonemapper() &&
            !bendShadowResult.showDebug &&
            !sparseVirtualShadowMapResult.showDebug &&
            !diagnosticCsmResult.showDebug)
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
                m_CommandList,
                framebuffer,
                displayTexture,
                &m_BindingCache);
        }
        EndRendererStage(RendererTimingStage::OutputBlit);
        EndRendererStage(RendererTimingStage::CompleteFrame);
        CompleteRendererTimerFrame();

        m_CommandList->close();
        GetDevice()->executeCommandList(m_CommandList);
        UpdateDiagnosticCsmBenchmarkRecording();
        if (visibilityPerfReady)
        {
            const ScreenSpaceVisibilityTimings& timings =
                m_ScreenSpaceVisibilityPass->GetTimings();
            const VisibilityBufferPrecisionSettings& precision =
                m_ui.ScreenSpaceVisibility.performance.bufferPrecision;
            std::ostringstream settings;
            settings
                << "resolution=full;samples=20;"
                << "estimator=uniform-solid-angle;radius=3;"
                << "thickness=0.5;exponent=2;ao=on;gi=on;"
                << "bounces=1;temporal=off;spatial=off;"
                << "scheduler="
                << (m_ui.ScreenSpaceVisibility.sampling.scheduler ==
                            VisibilitySampleScheduler::
                                ToroidalBlueNoiseRankField
                        ? "toroidal"
                        : "independent-hash")
                << ";trace=runtime"
                << ";ao_precision="
                << (precision.rawAmbient ==
                        VisibilityScalarBufferPrecision::Float16
                    ? "r16f" : "r32f")
                << ";gi_precision="
                << (precision.rawIndirect ==
                        VisibilityVectorBufferPrecision::Rgba16Float
                    ? "rgba16f" : "rgba32f");

            VisibilityPerfCaptureMetadata metadata;
            metadata.build =
                std::string(UVSR_GIT_COMMIT) +
                "-main-fix-runtime-parity";
            metadata.adapter = GetActiveAdapterName();
            metadata.livePriority = GetLiveProcessPriorityLabel();
            metadata.permutation = timings.activePermutation;
            metadata.settings = settings.str();
            metadata.timingProvenance =
                "independently fresh NVRHI trace and composition queries; "
                "their origin frame IDs are recorded because stages can "
                "become readable on different polling cycles; outer effect "
                "envelope is a main-only diagnostic; cross-build matched "
                "total is the sum of separate trace and composition "
                "medians; CPU interval is arrival-frame start-to-start";
            metadata.framebufferWidth = windowWidth;
            metadata.framebufferHeight = windowHeight;
            metadata.outputTextureBytes = timings.outputTextureBytes;
            metadata.workingTextureBytes = timings.workingTextureBytes;
            metadata.rawAmbientTextureBytes =
                timings.rawAmbientTextureBytes;
            metadata.rawIndirectTextureBytes =
                timings.rawIndirectFrontierBytes;
            if (g_VisibilityPerfCapture.Observe(
                    uint64_t(GetFrameIndex()),
                    timings.firstTraceFrameId,
                    timings.compositionFrameId,
                    timings.effectEnvelopeFrameId,
                    timings.firstTraceMs,
                    timings.compositionMs,
                    timings.effectEnvelopeMs,
                    timings.depthHierarchyMs,
                    timings.temporalMs,
                    timings.spatialDenoiseMs +
                        timings.fusedSpatialDenoiseUpsampleMs,
                    metadata))
            {
                glfwSetWindowShouldClose(
                    GetDeviceManager()->GetWindow(), GLFW_TRUE);
            }
        }
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

    [[nodiscard]] uint64_t GetSubmittedMainViewTriangles() const
    {
        return m_SubmittedMainViewTriangles;
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
            const std::array<UiBackdropRect, 3>& backdropRects)
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
        float shadowBlur = 10.f;
        float shadowOpacity = 0.34f;

        float4 outlineTopColor;
        float4 outlineBottomColor;
    };

    static_assert(sizeof(PixelZoomConstants) == 80u);

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
            constants.shadowBlur = 10.f;
            constants.shadowOpacity = 0.34f;
            constants.outlineTopColor =
                float4(0.88f, 0.90f, 0.94f, 0.10f);
            constants.outlineBottomColor =
                float4(0.96f, 0.97f, 1.00f, 0.30f);

            constexpr float ShadowOffsetY = 3.f;
            const float shadowExtent =
                std::ceil(constants.shadowBlur + ShadowOffsetY);
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
    if (g_VisibilityPerfDisableRendererTimers)
    {
        m_RendererTimerFrameWritable = false;
        m_RendererTimerActive.fill(false);
        return;
    }
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
    if (g_VisibilityPerfDisableRendererTimers)
        return;
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
    if (g_VisibilityPerfDisableRendererTimers)
        return;
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
    if (g_VisibilityPerfDisableRendererTimers)
        return;
    if (m_RendererTimerFrameWritable)
        ++m_RendererTimerFrame;
}

bool UvsrSceneViewer::QueueVisibilityBenchmark(
    uint32_t warmupFrameCount,
    uint32_t measuredFrameCount,
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
    m_VisibilityBenchmarkAutoClose = autoClose;
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

    if (m_VisibilityBenchmarkQueued)
    {
        const VisibilityExecutionPlan& plan =
            m_ScreenSpaceVisibilityPass->GetSelectedExecutionPlan();
        const VisibilityPerformanceWorkload& workload = plan.workload;
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
            profileName.assign(plan.configuration.name);
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
                " measured frames complete).");
        }
        return;
    }

    const VisibilityBenchmarkSummary completedSummary =
        m_ScreenSpaceVisibilityPass->GetBenchmarkSummary();
    m_ScreenSpaceVisibilityPass->CancelBenchmark();
    m_VisibilityBenchmarkError.clear();

    m_LastVisibilityBenchmarkSummary = completedSummary;
    m_HasVisibilityBenchmarkSummary = true;
    const bool closeAfterBenchmark = m_VisibilityBenchmarkAutoClose;
    m_VisibilityBenchmarkAutoClose = false;
    m_VisibilityBenchmarkStatus = "Complete.";
    log::info(
        "Visibility benchmark complete (%u complete, %u incomplete frames)",
        completedSummary.completeFrameCount,
        completedSummary.incompleteFrameCount);
    ReleaseVisibilityBenchmarkCameraLock();
    if (closeAfterBenchmark)
    {
        glfwSetWindowShouldClose(
            GetDeviceManager()->GetWindow(), GLFW_TRUE);
    }
}

class UIRenderer : public ImGui_Renderer
{
private:
    struct StatSnapshot
    {
        int width = 0;
        int height = 0;
        uint64_t submittedTriangles = 0u;
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
    ImGuiID m_AdjustedSpaceFontBakedId = 0;
    float m_BaseSpaceAdvance = 0.f;
    double m_DisplayedFrameTime = 0.0;
    double m_DisplayedGpuBandwidthGBps = 0.0;
    double m_DisplayedGpuTFlops = 0.0;
    double m_StatSnapshotElapsed = 0.0;
    double m_StatFrameTimeSum = 0.0;
    uint32_t m_StatFrameTimeCount = 0;
    std::array<std::string, 6> m_PerformanceStatValues;
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
    bool m_WasSceneLoading = false;
    std::unique_ptr<BackdropBlurPass> m_BackdropBlurPass;
    std::unique_ptr<PixelZoomPass> m_PixelZoomPass;
    uint32_t m_SettingsPanelMarginPixels = 10u;
    float m_SettingsAppearance = 0.f;
    PixelZoomMode m_RenderedPixelZoom = PixelZoomMode::Off;
    PixelZoomMode m_PendingPixelZoom = PixelZoomMode::Off;
    float m_PixelZoomVisibility = 0.f;
    float m_PixelZoomLevelTransition = 1.f;

	UIData& m_ui;

    inline static std::vector<ImDrawList*>
        g_SettingsAppearanceDrawLists;

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
        const ImVec2& center,
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
                center.x + (vertex.pos.x - center.x) * clampedScale,
                center.y + (vertex.pos.y - center.y) * clampedScale);
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
                center.x +
                    (command.ClipRect.x - center.x) * clampedScale,
                center.y +
                    (command.ClipRect.y - center.y) * clampedScale,
                center.x +
                    (command.ClipRect.z - center.x) * clampedScale,
                center.y +
                    (command.ClipRect.w - center.y) * clampedScale);
        }
    }

    static void ApplyBackdropAppearance(
        UiBackdropRect& backdropRect,
        const ImVec2& center,
        float scale,
        float opacity)
    {
        const float clampedScale = std::clamp(scale, 0.f, 1.f);
        backdropRect.minX =
            center.x + (backdropRect.minX - center.x) * clampedScale;
        backdropRect.minY =
            center.y + (backdropRect.minY - center.y) * clampedScale;
        backdropRect.maxX =
            center.x + (backdropRect.maxX - center.x) * clampedScale;
        backdropRect.maxY =
            center.y + (backdropRect.maxY - center.y) * clampedScale;
        backdropRect.opacity = std::clamp(opacity, 0.f, 1.f);
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

    static float AdvanceUiLayoutAnimation(
        float amount,
        bool targetVisible)
    {
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

        if (std::abs(scrollDelta) > 0.01f)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            const float requestedScroll =
                window->ScrollTarget.y < FLT_MAX
                    ? window->ScrollTarget.y
                    : window->Scroll.y;
            ImGui::SetScrollY(
                window,
                std::max(0.f, requestedScroll + scrollDelta));
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

    static std::string_view GetPerformanceProfileUiLabel(
        VisibilityPerformanceProfile profile)
    {
        switch (profile)
        {
        case VisibilityPerformanceProfile::Reference:
            return "Standard";
        case VisibilityPerformanceProfile::Runtime:
            return "Custom";
        case VisibilityPerformanceProfile::ExactFusedResolveApply:
            return "Fused";
        case VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2:
            return "Depth Guided";
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesDepthNormal2x2:
            return "Depth Normal";
        case VisibilityPerformanceProfile::AlgorithmicPackedEdgesSlope2x2:
            return "Slope Aware";
        case VisibilityPerformanceProfile::AlgorithmicPackedEdgesLeakage2x2:
            return "Leakage Limited";
        case VisibilityPerformanceProfile::AlgorithmicFusedPackedEdges2x2:
            return "Fused Depth Normal";
        default:
            return {};
        }
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

    static const char* GetMathModeLabel(VisibilityMathMode math)
    {
        switch (math)
        {
        case VisibilityMathMode::ReferenceFp32:
            return "FP32";
        default:
            return "Unknown";
        }
    }

    static const char* GetRawAoStorageLabel(VisibilityRawAoStorage storage)
    {
        switch (storage)
        {
        case VisibilityRawAoStorage::ScalarFloat:
            return "Float";
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
            return "Packed R8";
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
            return "Joint Bilateral";
        case VisibilityReconstructionMode::PackedEdges2x2:
            return "Edge Guided";
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

    static const char* GetEdgeReconstructionTooltip(
        VisibilityPerformanceProfile profile)
    {
        switch (profile)
        {
        case VisibilityPerformanceProfile::AlgorithmicPackedEdges2x2:
            return "Rank 1 of 4 tested reconstruction methods: depth-guided "
                "continuity, 2.2467 ms median and 12.04% faster in the "
                "controlled comparison.";
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesDepthNormal2x2:
            return "Rank 2 of 4: depth and surface normals protect more "
                "discontinuities, 2.2976 ms median and 10.05% faster in the "
                "controlled comparison.";
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesSlope2x2:
            return "Rank 3 of 4: slope-aware depth and normal continuity, "
                "2.2993 ms median and 9.99% faster in the controlled "
                "comparison.";
        case VisibilityPerformanceProfile::
                AlgorithmicPackedEdgesLeakage2x2:
            return "Rank 4 of 4: preserves a tiny minimum cross-edge "
                "contribution, 2.3133 ms median and 9.44% faster in the "
                "controlled comparison.";
        default:
            return "Edge-guided reconstruction.";
        }
    }

    static const char* GetApplicationLabel(
        VisibilityApplicationMode application)
    {
        switch (application)
        {
        case VisibilityApplicationMode::LegacySeparateComposition:
            return "Separate";
        case VisibilityApplicationMode::FusedResolveAndApplyExact:
            return "Fused";
        case VisibilityApplicationMode::FusedResolveAndApplyPackedEdges:
            return "Fused Edge";
        default:
            return "Unknown";
        }
    }

    static const char* GetDepthModeLabel(VisibilityDepthMode depth)
    {
        switch (depth)
        {
        case VisibilityDepthMode::Legacy:
            return "Device Depth";
        default:
            return "Unknown";
        }
    }

    static const char* GetBindingStrategyLabel(
        VisibilityBindingStrategy bindings)
    {
        switch (bindings)
        {
        case VisibilityBindingStrategy::MinimalConditional:
            return "Minimal";
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

    static void DrawDisabledTextWrapped(const char* text)
    {
        ImGui::PushTextWrapPos(0.f);
        ImGui::TextDisabled("%s", text);
        ImGui::PopTextWrapPos();
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
        const ImGuiID measurementValidKey =
            headerId ^ ImGuiID(0x82E4C76Bu);
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
        const float easedAmount = SmoothUiLayoutAnimation(
            g_DrawerAnimationContext.openAmount);
        const float animatedHeight =
            g_DrawerAnimationContext.needsInitialMeasurement
                ? 0.001f
                : std::max(
                    measuredHeight * easedAmount,
                    0.001f);
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
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            style.Alpha *
                (g_DrawerAnimationContext.needsInitialMeasurement
                    ? 0.f
                    : easedAmount));
        ImGuiChildFlags childFlags =
            ImGuiChildFlags_AlwaysUseWindowPadding |
            ImGuiChildFlags_AllowZeroSize;
        ImGuiWindowFlags childWindowFlags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (g_DrawerAnimationContext.needsInitialMeasurement ||
            !g_DrawerAnimationContext.targetOpen ||
            g_DrawerAnimationContext.openAmount < 1.f)
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
        const bool needsInitialMeasurement =
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
            needsInitialMeasurement
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
        if (needsInitialMeasurement ||
            !open ||
            openAmount < 1.f)
        {
            childWindowFlags |= ImGuiWindowFlags_NoInputs;
        }
        bool bodyVisible = ImGui::BeginChild(
            headerId ^ ImGuiID(0xE60792B5u),
            ImVec2(0.f, animatedHeight),
            ImGuiChildFlags_AllowZeroSize,
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

        const bool needsInitialMeasurement =
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
            needsInitialMeasurement
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
        if (needsInitialMeasurement ||
            !state.targetVisible ||
            state.linearAmount < 1.f)
        {
            childWindowFlags |=
                ImGuiWindowFlags_NoNavInputs |
                ImGuiWindowFlags_NoNavFocus;
        }
        bool bodyVisible = ImGui::BeginChild(
            regionId ^ ImGuiID(0x6C3E91B7u),
            ImVec2(0.f, animatedHeight),
            ImGuiChildFlags_AllowZeroSize,
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

    enum class SettingsResetIconPlacement
    {
        Trailing,
        NestedDropdownGutter
    };

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
        const bool pressed = ImGui::InvisibleButton(
            "##PresetReset",
            ImVec2(buttonSize, buttonSize));
        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        const ImVec2 center(
            (minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f);
        const ImU32 background = ImGui::GetColorU32(
            held
                ? ImGuiCol_ButtonActive
                : hovered
                    ? ImGuiCol_ButtonHovered
                    : ImGuiCol_Button);
        drawList->AddRectFilled(
            minimum,
            maximum,
            background,
            style.FrameRounding);

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

    using DeferredAliasingUiPresentation =
        DeferredUiStructuralPresentation<AntiAliasingSettings>;

    inline static DeferredDropdownUiState
        g_DeferredDropdownUiState;
    inline static DeferredAliasingUiPresentation
        g_DeferredAliasingUiPresentation;
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
        g_DeferredAliasingUiPresentation.Cancel();
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
        bool compositionIdle)
    {
        DeferredDropdownUiState& state =
            g_DeferredDropdownUiState;
        if (state.actions.Empty())
            return false;

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
        const char* deferredPreview =
            GetDeferredDropdownPreview(comboId);

        const bool open = ImGui::BeginCombo(
            label,
            deferredPreview ? deferredPreview : previewValue,
            flags | ImGuiComboFlags_NoArrowButton);
        DeferredDropdownUiState& deferredState =
            g_DeferredDropdownUiState;
        if (open && deferredState.transitionComboId == comboId)
        {
            deferredState.transitionComboLastSubmittedFrame =
                ImGui::GetFrameCount();
        }
        g_ActiveRoundedComboId = open ? comboId : 0;

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
        const std::array<std::string, 6>& values)
    {
        return values[0] + " / " +
            values[5] + " / " +
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
        snapshot.submittedTriangles =
            m_app->GetSubmittedMainViewTriangles();
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

    static bool DrawSliderInt(
        const char* label,
        int* value,
        int minimum,
        int maximum,
        const char* format = "%d",
        ImGuiSliderFlags flags = 0)
    {
        const ImGuiID sliderId = ImGui::GetID(label);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID presentationValueKey =
            sliderId ^ ImGuiID(0x41D38B75u);
        const bool freezePresentation =
            FreezeAnimatedToggleVisualValues();
        int presentationValue = storage->GetInt(
            presentationValueKey,
            *value);
        int* submittedValue =
            freezePresentation
                ? &presentationValue
                : value;
        PushPanelSliderTrackStyle();
        const bool changed = ImGui::SliderInt(
            label,
            submittedValue,
            minimum,
            maximum,
            format,
            flags);
        ImGui::PopStyleColor(3);
        if (!freezePresentation)
            storage->SetInt(presentationValueKey, *value);
        return changed && !freezePresentation;
    }

    static bool DrawSvsmSettingsSurface(
        SparseVirtualShadowMapSettings& shadows,
        float settingsControlWidth)
    {
        bool customChanged = false;
        bool resetApplied = false;
        const SparseVirtualShadowMapSettings factoryDefaults{};
        SparseVirtualShadowMapSettings presetDefaults = shadows;
        const SvsmPreset resetPreset =
            shadows.preset == SvsmPreset::Custom
                ? SvsmPreset::Quality
                : shadows.preset;
        ApplySvsmPreset(presetDefaults, resetPreset);
        static constexpr auto reconcileSvsmPreset =
            [](SparseVirtualShadowMapSettings& settings)
            {
                constexpr SvsmPreset Presets[] = {
                    SvsmPreset::Performance,
                    SvsmPreset::Balanced,
                    SvsmPreset::Quality
                };
                SparseVirtualShadowMapSettings current = settings;
                current.preset = SvsmPreset::Custom;
                for (const SvsmPreset preset : Presets)
                {
                    SparseVirtualShadowMapSettings candidate = settings;
                    ApplySvsmPreset(candidate, preset);
                    candidate.preset = SvsmPreset::Custom;
                    if (IsSameSvsmConfiguration(current, candidate))
                    {
                        settings.preset = preset;
                        return;
                    }
                }
                settings.preset = SvsmPreset::Custom;
            };

        const auto drawCheckbox = [&](
            const char* label,
            bool& value,
            bool defaultValue,
            bool available,
            const char* tooltip)
        {
            // SVSM expert values are stored requested configuration. Keep the
            // fixed schema editable across Mode and policy changes; tooltips
            // describe when each requested value becomes renderer-effective.
            (void)available;
            const bool changed = ImGui::Checkbox(label, &value);
            if (tooltip != nullptr)
            {
                if (available)
                    ImGui::SetItemTooltip("%s", tooltip);
                else
                {
                    ImGui::SetItemTooltip(
                        "%s Stored now; becomes effective when its owning "
                        "SVSM mode and requested policies are active.",
                        tooltip);
                }
            }
            if (DrawPresetResetIcon(
                    label,
                    value != defaultValue))
            {
                value = defaultValue;
                resetApplied = true;
            }
            return changed;
        };

        const auto drawCombo = [&](
            const char* label,
            int currentIndex,
            int defaultIndex,
            const char* const* labels,
            int labelCount,
            bool available,
            const char* tooltip,
            bool nestedResetPlacement,
            std::function<void(int)> apply)
        {
            currentIndex = std::clamp(
                currentIndex,
                0,
                labelCount - 1);
            defaultIndex = std::clamp(
                defaultIndex,
                0,
                labelCount - 1);
            (void)available;
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(label, labels[currentIndex]))
            {
                for (int index = 0; index < labelCount; ++index)
                {
                    const bool selected = index == currentIndex;
                    DrawDeferredDropdownOption(
                        labels[index],
                        labels[index],
                        selected,
                        [apply, index]()
                        {
                            apply(index);
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (tooltip != nullptr)
            {
                if (available)
                    ImGui::SetItemTooltip("%s", tooltip);
                else
                {
                    ImGui::SetItemTooltip(
                        "%s Stored now; becomes effective when its owning "
                        "SVSM mode and requested policies are active.",
                        tooltip);
                }
            }
            const bool resetRequested = nestedResetPlacement
                ? DrawNestedDropdownResetIcon(
                      label,
                      currentIndex != defaultIndex)
                : DrawPresetResetIcon(
                      label,
                      currentIndex != defaultIndex);
            if (resetRequested)
            {
                QueueDeferredControlUiAction(
                    [apply, defaultIndex]()
                    {
                        apply(defaultIndex);
                    });
            }
        };

        const auto drawFloat = [&](
            const char* label,
            float& value,
            float defaultValue,
            float minimum,
            float maximum,
            const char* format,
            bool available,
            const char* tooltip)
        {
            (void)available;
            ImGui::SetNextItemWidth(settingsControlWidth);
            const bool changed = DrawSliderFloat(
                label,
                &value,
                minimum,
                maximum,
                format);
            if (tooltip != nullptr)
            {
                if (available)
                    ImGui::SetItemTooltip("%s", tooltip);
                else
                {
                    ImGui::SetItemTooltip(
                        "%s Stored now; becomes effective when its owning "
                        "SVSM mode and requested policies are active.",
                        tooltip);
                }
            }
            if (DrawPresetResetIcon(
                    label,
                    value != defaultValue))
            {
                value = defaultValue;
                resetApplied = true;
            }
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
        drawCombo(
            "Profile##SparseVirtualShadowMaps",
            int(shadows.preset),
            int(factoryDefaults.preset),
            presetLabels,
            int(std::size(presetLabels)),
            true,
            "Choose the speed-to-quality balance. Named profiles reset "
            "Developer and Unabstracted controls while retaining "
            "the scene extent, light-depth range, pool size, and Enabled state.",
            false,
            [settings = &shadows](int selectedPreset)
            {
                ApplySvsmPreset(
                    *settings,
                    SvsmPreset(selectedPreset));
            });

        customChanged |= drawFloat(
            "First Clipmap Extent",
            shadows.firstClipmapExtent,
            factoryDefaults.firstClipmapExtent,
            1.f,
            500.f,
            "%.1f",
            true,
            "Set the world-space width covered by the finest clipmap.");
        customChanged |= drawFloat(
            "Maximum Light Depth",
            shadows.maximumLightDepth,
            factoryDefaults.maximumLightDepth,
            1.f,
            2000.f,
            "%.1f",
            true,
            "Set the caster depth range centered on each clipmap.");

        static const char* filterKernelLabels[] = {
            "Nearest Poisson Reference",
            "Bilinear PCF"
        };
        drawCombo(
            "Filter Kernel",
            int(shadows.filterKernel),
            int(presetDefaults.filterKernel),
            filterKernelLabels,
            int(std::size(filterKernelLabels)),
            sparseMode(),
            sparseMode()
                ? "Choose the deterministic nearest reference or bilinear "
                  "page-safe PCF."
                : "Dense Reference retains its point-load receiver.",
            false,
            [settings = &shadows](int selectedKernel)
            {
                settings->filterKernel =
                    SvsmFilterKernel(selectedKernel);
                reconcileSvsmPreset(*settings);
            });

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
        int defaultTapIndex = 0;
        for (int index = 0; index < int(std::size(tapValues)); ++index)
        {
            if (tapValues[index] == shadows.tapCount)
                currentTapIndex = index;
            if (tapValues[index] == presetDefaults.tapCount)
                defaultTapIndex = index;
        }
        drawCombo(
            "Filter Taps",
            currentTapIndex,
            defaultTapIndex,
            tapLabels,
            int(std::size(tapLabels)),
            true,
            "Select the page-safe 1, 4, 8, or 16-tap receiver.",
            false,
            [settings = &shadows](int selectedTap)
            {
                static constexpr SvsmTapCount TapValues[] = {
                    SvsmTapCount::One,
                    SvsmTapCount::Four,
                    SvsmTapCount::Eight,
                    SvsmTapCount::Sixteen
                };
                settings->tapCount = TapValues[selectedTap];
                reconcileSvsmPreset(*settings);
            });
        customChanged |= drawCheckbox(
            "Adaptive Filtering",
            shadows.adaptiveFiltering,
            presetDefaults.adaptiveFiltering,
            shadows.tapCount != SvsmTapCount::One,
            "Use a page-safe agreement probe set before the selected full "
            "filter. One-tap filtering has no adaptive shortcut.");

        static const char* biasLabels[] = {
            "0",
            "+1 Mip",
            "+2 Mips"
        };
        drawCombo(
            "Resolution Bias",
            int(shadows.resolutionBias),
            int(presetDefaults.resolutionBias),
            biasLabels,
            int(std::size(biasLabels)),
            true,
            "Apply the same clipmap bias to marking, allocation, resolve, "
            "fallback, dense clipmap drawing, and diagnostics.",
            false,
            [settings = &shadows](int selectedBias)
            {
                settings->resolutionBias =
                    SvsmResolutionBias(selectedBias);
                reconcileSvsmPreset(*settings);
            });

        customChanged |=
            drawCheckbox("Receiver Distance Mip Clamp",
                         shadows.receiverDistanceMipClampEnabled,
                         presetDefaults.receiverDistanceMipClampEnabled,
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

            if (BeginAnimatedTreeNode(
                    "Resources and Cache Policy##SparseVirtualShadowMaps",
                    ImGuiTreeNodeFlags_None,
                    "Configure sparse pool capacity, scheduling limits, cache "
                    "mode, paired depth, eviction, and reusable draw lists."))
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
                ImGui::SetItemTooltip(
                    sparseMode()
                        ? "Set the logical size of the shared fixed physical "
                          "page pool."
                        : "Store the logical size of the shared fixed physical "
                          "page pool. It becomes effective in Sparse Uncached "
                          "or Sparse Cached mode.");
                if (DrawPresetResetIcon(
                        "SVSM Physical Pool Pages",
                        shadows.physicalPageCount !=
                            factoryDefaults.physicalPageCount))
                {
                    shadows.physicalPageCount =
                        factoryDefaults.physicalPageCount;
                    if (finitePageBudget())
                    {
                        shadows.pageRenderBudget = std::min(
                            shadows.pageRenderBudget,
                            shadows.physicalPageCount);
                    }
                    resetApplied = true;
                }

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
                    sparseMode()
                        ? "Remove the finite dirty-page scheduling limit. "
                          "Returning to a finite budget restores the last "
                          "finite value."
                        : "Store whether sparse rendering removes the finite "
                          "dirty-page scheduling limit. It becomes effective "
                          "in Sparse Uncached or Sparse Cached mode.");
                if (DrawPresetResetIcon(
                        "SVSM Unlimited Page Render Budget",
                        finitePageBudget()))
                {
                    shadows.pageRenderBudget =
                        std::numeric_limits<uint32_t>::max();
                    unlimitedBudget = true;
                    resetApplied = true;
                }
                if (BeginAnimatedToggleRegion(
                        "##SvsmFinitePageRenderBudget",
                        !unlimitedBudget))
                {
                    int pageBudget = int(std::min(
                        finitePageBudget()
                            ? shadows.pageRenderBudget
                            : rememberedFinitePageBudget,
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
                        sparseMode()
                            ? "Limit dirty-page scheduling across the fine "
                              "clipmaps."
                            : "Store the fine-clipmap dirty-page scheduling "
                              "limit. It becomes effective in Sparse Uncached "
                              "or Sparse Cached mode while Unlimited Page "
                              "Render Budget is off.");
                    constexpr uint32_t DefaultFinitePageBudget = 256u;
                    const uint32_t resetPageBudget = std::min(
                        DefaultFinitePageBudget,
                        shadows.physicalPageCount);
                    if (DrawPresetResetIcon(
                            "SVSM Page Render Budget",
                            pageBudget != int(resetPageBudget)))
                    {
                        shadows.pageRenderBudget = resetPageBudget;
                        rememberedFinitePageBudget = resetPageBudget;
                        resetApplied = true;
                    }
                    EndAnimatedToggleRegion();
                }

                customChanged |= drawCheckbox(
                    "Paired Static/Dynamic Depth",
                    shadows.pairedStaticDynamicDepthEnabled,
                    presetDefaults.pairedStaticDynamicDepthEnabled,
                    cacheReuseAvailable(),
                    "Use one page identity with persistent static depth and a "
                    "receiver-visible merged slice. This doubles physical-depth "
                    "pool memory and therefore requires Sparse Cached mode.");

                ImGui::Separator();

                static const char* modeLabels[] = {
                    "Dense Reference", "Sparse Uncached", "Sparse Cached"};
                drawCombo(
                    "Mode##SparseVirtualShadowMaps",
                    int(shadows.mode),
                    int(presetDefaults.mode),
                    modeLabels,
                    int(std::size(modeLabels)),
                    true,
                    "Select fully backed validation, sparse full redraw, or "
                    "sparse cached page reuse. Cache state follows this mode.",
                    true,
                    [settings = &shadows](int selectedMode)
                    {
                        settings->mode = SvsmMode(selectedMode);
                        settings->cachingEnabled =
                            settings->mode == SvsmMode::SparseCached;
                        reconcileSvsmPreset(*settings);
                    });

                customChanged |= drawCheckbox(
                    "Include Coarsest in Page Budget",
                    shadows.coarsestPageRenderBudgetEnabled,
                    presetDefaults.coarsestPageRenderBudgetEnabled,
                    sparseMode() && finitePageBudget(),
                    "Apply the finite shared render reservation to all six "
                    "clipmaps instead of preserving the complete coarse fallback.");

                static const char* markingLabels[] = {
                    "Per Pixel", "8 by 8 Tile", "16 by 16 Tile"};
                drawCombo(
                    "Page Marking",
                    int(shadows.markingMode),
                    int(presetDefaults.markingMode),
                    markingLabels,
                    int(std::size(markingLabels)),
                    sparseMode(),
                    "Choose exact per-pixel requests or conservative deduplicated "
                    "tile marking.",
                    true,
                    [settings = &shadows](int selectedMarking)
                    {
                        settings->markingMode =
                            SvsmMarkingMode(selectedMarking);
                        reconcileSvsmPreset(*settings);
                    });

                customChanged |=
                    drawCheckbox("Recent Page Eviction Grace",
                                 shadows.recentPageEvictionGraceEnabled,
                                 presetDefaults.recentPageEvictionGraceEnabled,
                                 cacheReuseAvailable(),
                                 "Protect recently used pages from immediate fixed-pool "
                                 "eviction.");
                customChanged |=
                    drawCheckbox("Cached Shadow Draw Lists",
                                 shadows.renderPacketCachingEnabled,
                                 presetDefaults.renderPacketCachingEnabled,
                                 sparseMode(),
                                 "Reuse compatible conservative per-clipmap caster "
                                 "packet lists.");

                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Movement and Invalidation##SparseVirtualShadowMaps",
                    ImGuiTreeNodeFlags_None,
                    "Tune moving-light recovery, receiver-distance clamping, "
                    "and localized caster invalidation policy."))
            {
                static const char* movingBiasLabels[] = {"Off", "+1 Mip", "+2 Mips"};
                const int effectiveMovingBias =
                    shadows.movingLightLodBiasEnabled
                        ? int(shadows.movingLightResolutionBias)
                        : 0;
                const int defaultMovingBias =
                    presetDefaults.movingLightLodBiasEnabled
                        ? int(presetDefaults.movingLightResolutionBias)
                        : 0;
                drawCombo(
                    "Moving-Light Resolution Bias",
                    effectiveMovingBias,
                    defaultMovingBias,
                    movingBiasLabels,
                    int(std::size(movingBiasLabels)),
                    movingLightPolicyAvailable,
                    "Temporarily coarsen moving-light work, then recover. Off is "
                    "the exact no-bias path.",
                    true,
                    [settings = &shadows](int selectedMovingBias)
                    {
                        settings->movingLightResolutionBias =
                            SvsmResolutionBias(selectedMovingBias);
                        settings->movingLightLodBiasEnabled =
                            selectedMovingBias != 0;
                        reconcileSvsmPreset(*settings);
                    });

                int movingLightRecoveryFrames = int(shadows.movingLightLodRecoveryFrames);
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
                ImGui::SetItemTooltip(
                    movingBiasActive
                        ? "Hold the selected moving-light bias, then recover "
                          "after this many successful sparse frames."
                        : "Store the recovery duration used when moving-light "
                          "resolution bias becomes active.");
                if (DrawPresetResetIcon(
                        "SVSM Moving-Light Recovery Frames",
                        shadows.movingLightLodRecoveryFrames !=
                            presetDefaults.movingLightLodRecoveryFrames))
                {
                    shadows.movingLightLodRecoveryFrames =
                        presetDefaults.movingLightLodRecoveryFrames;
                    resetApplied = true;
                }

                if (BeginAnimatedToggleRegion(
                        "##SvsmReceiverDistanceClampTuning",
                        shadows.receiverDistanceMipClampEnabled))
                {
                    if (DrawSliderFloat(
                            "Distance Clamp Start x Extent",
                            &shadows.receiverDistanceMipClampStartScale,
                            0.25f,
                            8.f,
                            "%.2f"))
                    {
                        customChanged = true;
                    }
                    ImGui::SetItemTooltip(
                        receiverDistanceClampAvailable
                            ? "Scale the first receiver-distance threshold by "
                              "the current clipmap extent."
                            : "Store the first receiver-distance threshold "
                              "scale. It becomes effective in Sparse Uncached "
                              "or Sparse Cached mode while Receiver Distance "
                              "Mip Clamp is enabled.");
                    if (DrawPresetResetIcon(
                            "SVSM Distance Clamp Start",
                            shadows.receiverDistanceMipClampStartScale !=
                                presetDefaults
                                    .receiverDistanceMipClampStartScale))
                    {
                        shadows.receiverDistanceMipClampStartScale =
                            presetDefaults
                                .receiverDistanceMipClampStartScale;
                        resetApplied = true;
                    }
                    int receiverDistanceMaximumLevel =
                        int(shadows.receiverDistanceMipClampMaximumLevel);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (DrawSliderInt(
                            "Maximum Distance Clamp Level",
                            &receiverDistanceMaximumLevel,
                            0,
                            int(
                                SvsmMaximumReceiverDistanceMipClampLevel)))
                    {
                        shadows.receiverDistanceMipClampMaximumLevel =
                            uint32_t(receiverDistanceMaximumLevel);
                        customChanged = true;
                    }
                    ImGui::SetItemTooltip(
                        receiverDistanceClampAvailable
                            ? "Set the coarsest fine clipmap that "
                              "receiver-distance clamping may select."
                            : "Store the maximum fine-clipmap clamp level. It "
                              "becomes effective in Sparse Uncached or Sparse "
                              "Cached mode while Receiver Distance Mip Clamp "
                              "is enabled.");
                    if (DrawPresetResetIcon(
                            "SVSM Maximum Distance Clamp Level",
                            shadows.receiverDistanceMipClampMaximumLevel !=
                                presetDefaults
                                    .receiverDistanceMipClampMaximumLevel))
                    {
                        shadows.receiverDistanceMipClampMaximumLevel =
                            presetDefaults
                                .receiverDistanceMipClampMaximumLevel;
                        resetApplied = true;
                    }
                    EndAnimatedToggleRegion();
                }

                const bool adaptiveCasterClassificationAvailable =
                    cacheReuseAvailable() && shadows.localizedInvalidationEnabled &&
                    shadows.pairedStaticDynamicDepthEnabled;
                customChanged |= drawCheckbox(
                    "Adaptive Static Caster Cache",
                    shadows.adaptiveCasterCacheClassificationEnabled,
                    presetDefaults.adaptiveCasterCacheClassificationEnabled,
                    adaptiveCasterClassificationAvailable,
                    "Demote changed rigid opaque casters immediately and promote "
                    "them back to persistent static depth after stabilization.");

                static const char* invalidationModeLabels[] = {
                    "Auto", "Always", "Rigid", "Static"};
                drawCombo(
                    "Object Invalidation Mode",
                    int(shadows.defaultObjectInvalidationMode),
                    int(presetDefaults.defaultObjectInvalidationMode),
                    invalidationModeLabels,
                    int(std::size(invalidationModeLabels)),
                    cacheReuseAvailable() && shadows.localizedInvalidationEnabled,
                    "Choose the default per-object transform and deformation "
                    "invalidation policy.",
                    true,
                    [settings = &shadows](int selectedInvalidationMode)
                    {
                        settings->defaultObjectInvalidationMode =
                            SvsmObjectInvalidationMode(
                                selectedInvalidationMode);
                        reconcileSvsmPreset(*settings);
                    });

                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Culling and Raster##SparseVirtualShadowMaps",
                    ImGuiTreeNodeFlags_None,
                    "Tune packet rejection, scheduled-page masks, static-depth "
                    "occlusion, and dirty-page raster."))
            {
                customChanged |= drawCheckbox(
                    "Packet State Sorting",
                    shadows.packetStateSortingEnabled,
                    presetDefaults.packetStateSortingEnabled,
                    sparseMode() &&
                        shadows.gpuGatedDrawSubmission &&
                        shadows.batchedDrawSubmissionEnabled,
                    "Group compatible packet state before indirect submission.");
                customChanged |= drawCheckbox("Packet Page Culling",
                                              shadows.packetPageCullingEnabled,
                                              presetDefaults.packetPageCullingEnabled,
                                              packetCullingAvailable,
                                              "Intersect each cached caster packet "
                                              "with scheduled dirty work.");
                customChanged |= drawCheckbox(
                    "Hierarchical Scheduled-Page Mask",
                    shadows.hierarchicalScheduledPageMaskEnabled,
                    presetDefaults.hierarchicalScheduledPageMaskEnabled,
                    packetPageCullingActive,
                    "Reject packet rectangles against an exact-validated coarse "
                    "scheduled-page hierarchy.");
                customChanged |= drawCheckbox(
                    "Receiver Subpage Mask Culling",
                    shadows.receiverPageMaskCullingEnabled,
                    presetDefaults.receiverPageMaskCullingEnabled,
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
                    presetDefaults.staticDepthHierarchyCullingEnabled,
                    staticDepthHierarchyAvailable,
                    "Reject only fully occluded dynamic caster/page pairs against "
                    "the complete persistent static page hierarchy.");
                if (BeginAnimatedToggleRegion(
                        "##SvsmStaticDepthHierarchyTuning",
                        shadows.staticDepthHierarchyCullingEnabled))
                {
                    customChanged |=
                        drawFloat("Static HZB Conservative Bias",
                                  shadows.staticDepthHierarchyBias,
                                  presetDefaults.staticDepthHierarchyBias,
                                  0.f,
                                  0.01f,
                                  "%.6f",
                                  true,
                                  staticDepthHierarchyAvailable
                                      ? "Increase the reverse-Z fail-open "
                                        "guard used by static HZB culling."
                                      : "Store the reverse-Z fail-open guard "
                                        "used when static HZB culling becomes "
                                        "effective.");
                    EndAnimatedToggleRegion();
                }

                const bool dirtyScatterRasterChanged = ImGui::Checkbox(
                    "Dirty Page Scatter Raster",
                    &shadows.dirtyPageScatterRasterEnabled);
                ImGui::SetItemTooltip(
                    dirtyScatterAvailable
                        ? "Request one virtual-space draw per intersecting "
                          "packet. The amplification guard is intrinsic and "
                          "falls back to the exact page list when a rectangle "
                          "covers too many holes."
                        : "Store the requested scatter-raster policy. It "
                          "becomes effective in Sparse Uncached or Sparse "
                          "Cached mode when GPU-Gated Draw Submission and "
                          "Packet Page Culling are enabled.");
                if (dirtyScatterRasterChanged)
                {
                    shadows.dirtyPageScatterAmplificationGuardEnabled =
                        shadows.dirtyPageScatterRasterEnabled;
                    customChanged = true;
                }
                if (DrawPresetResetIcon(
                        "SVSM Dirty Page Scatter Raster",
                        shadows.dirtyPageScatterRasterEnabled !=
                                presetDefaults.dirtyPageScatterRasterEnabled ||
                            shadows
                                    .dirtyPageScatterAmplificationGuardEnabled !=
                                presetDefaults
                                    .dirtyPageScatterAmplificationGuardEnabled))
                {
                    shadows.dirtyPageScatterRasterEnabled =
                        presetDefaults.dirtyPageScatterRasterEnabled;
                    shadows.dirtyPageScatterAmplificationGuardEnabled =
                        presetDefaults
                            .dirtyPageScatterAmplificationGuardEnabled;
                    resetApplied = true;
                }
                if (BeginAnimatedToggleRegion(
                        "##SvsmDirtyPageScatterTuning",
                        shadows.dirtyPageScatterRasterEnabled))
                {
                    int scatterMaximumAmplification =
                        int(shadows.dirtyPageScatterMaximumAmplification);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (DrawSliderInt(
                            "Scatter Maximum Page Amplification",
                            &scatterMaximumAmplification,
                            1,
                            int(
                                SvsmMaximumDirtyPageScatterAmplification)))
                    {
                        shadows.dirtyPageScatterMaximumAmplification =
                            uint32_t(scatterMaximumAmplification);
                        customChanged = true;
                    }
                    ImGui::SetItemTooltip(
                        dirtyScatterAvailable
                            ? "Fall back to the exact per-page packet list "
                              "above this rectangle-to-exact-page "
                              "amplification ratio."
                            : "Store the rectangle-to-exact-page "
                              "amplification limit. It becomes effective in "
                              "Sparse Uncached or Sparse Cached mode when "
                              "GPU-Gated Draw Submission, Packet Page "
                              "Culling, and Dirty Page Scatter Raster are "
                              "enabled.");
                    if (DrawPresetResetIcon(
                            "SVSM Scatter Maximum Page Amplification",
                            shadows
                                    .dirtyPageScatterMaximumAmplification !=
                                presetDefaults
                                    .dirtyPageScatterMaximumAmplification))
                    {
                        shadows.dirtyPageScatterMaximumAmplification =
                            presetDefaults
                                .dirtyPageScatterMaximumAmplification;
                        resetApplied = true;
                    }
                    EndAnimatedToggleRegion();
                }

                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Unabstracted##SparseVirtualShadowMaps",
                    ImGuiTreeNodeFlags_None,
                    "Raw independently toggleable optimizations that are proven "
                    "enough to trend toward an abstracted always-on policy, but "
                    "retain their reference paths during validation."))
            {
                customChanged |=
                    drawCheckbox("Allocation Budget Saturation Early-Out",
                                 shadows.allocationBudgetSaturationEarlyOutEnabled,
                                 presetDefaults.allocationBudgetSaturationEarlyOutEnabled,
                                 sparseMode() && finitePageBudget(),
                                 "Bypass redundant reservation atomics after the shared "
                                 "finite budget is saturated.");
                customChanged |= drawCheckbox(
                    "Deduplicate Per-Pixel Requests",
                    shadows.perPixelMarkingDedupeEnabled,
                    presetDefaults.perPixelMarkingDedupeEnabled,
                    sparseMode() && shadows.markingMode == SvsmMarkingMode::PerPixel,
                    "Deduplicate each 8-by-8 group's page requests through a "
                    "bounded shared hash that fails open on collisions.");

                static const char* poissonOrderingLabels[] = {"Legacy Stride Reference",
                                                              "Balanced Progressive"};
                drawCombo(
                    "Poisson Ordering",
                    int(shadows.poissonOrdering),
                    int(presetDefaults.poissonOrdering),
                    poissonOrderingLabels,
                    int(std::size(poissonOrderingLabels)),
                    sparseMode(),
                    "Choose the legacy stride or balanced progressive tap prefix.",
                    true,
                    [settings = &shadows](int selectedPoissonOrdering)
                    {
                        settings->poissonOrdering =
                            SvsmPoissonOrdering(
                                selectedPoissonOrdering);
                        reconcileSvsmPreset(*settings);
                    });

                static const char* filteringLabels[] = {"Manual Page Safe", "Hybrid"};
                drawCombo(
                    "Filtering",
                    int(shadows.filterMode),
                    int(presetDefaults.filterMode),
                    filteringLabels,
                    int(std::size(filteringLabels)),
                    sparseMode(),
                    "Hybrid filtering reuses one translation only when the "
                    "complete footprint stays in one valid page.",
                    true,
                    [settings = &shadows](int selectedFiltering)
                    {
                        settings->filterMode =
                            SvsmFilterMode(selectedFiltering);
                        reconcileSvsmPreset(*settings);
                    });

                customChanged |=
                    drawCheckbox("Precomposed Clipmap Transforms",
                                 shadows.precomposedClipmapTransformsEnabled,
                                 presetDefaults.precomposedClipmapTransformsEnabled,
                                 sparseMode(),
                                 "Transform camera-clip positions directly into each "
                                 "clipmap in marking and resolve.");
                customChanged |=
                    drawCheckbox("Page Translation Cache",
                                 shadows.pageTranslationCachingEnabled,
                                 presetDefaults.pageTranslationCachingEnabled,
                                 sparseMode(),
                                 "Reuse exact page-table translations within one pixel's "
                                 "validated filter footprint.");

                customChanged |= drawCheckbox(
                    "Light-Depth Origin Guard Band",
                    shadows.lightDepthOriginGuardBandEnabled,
                    presetDefaults.lightDepthOriginGuardBandEnabled,
                    cacheReuseAvailable(),
                    "Keep the light-depth origin stable until the configured "
                    "scene-depth guard band is exceeded.");
                if (BeginAnimatedToggleRegion(
                        "##SvsmLightDepthGuardBandTuning",
                        shadows.lightDepthOriginGuardBandEnabled))
                {
                    customChanged |= drawFloat(
                        "Light-Depth Guard Fraction",
                        shadows.lightDepthOriginGuardBandFraction,
                        presetDefaults.lightDepthOriginGuardBandFraction,
                        0.1f,
                        1.f,
                        "%.2f",
                        cacheReuseAvailable(),
                        cacheReuseAvailable()
                            ? "Set the usable fraction of the cached "
                              "light-depth range."
                            : "Store the usable light-depth guard fraction. "
                              "It becomes effective in Sparse Cached mode "
                              "while Light-Depth Origin Guard Band is "
                              "enabled.");
                    EndAnimatedToggleRegion();
                }
                customChanged |=
                    drawCheckbox("Finite-Budget Static Drain",
                                 shadows.finiteBudgetStaticDrainEnabled,
                                 presetDefaults.finiteBudgetStaticDrainEnabled,
                                 cacheReuseAvailable() && finitePageBudget(),
                                 "Drain persistent static-dirty work without starving "
                                 "dynamic fine pages under a finite budget.");
                customChanged |= drawCheckbox(
                    "Static Page Request Reuse",
                    shadows.staticPageRequestReuseEnabled,
                    presetDefaults.staticPageRequestReuseEnabled,
                    cacheReuseAvailable(),
                    "Reuse validated static page requests while camera, light, "
                    "and scene mapping remain compatible.");
                customChanged |=
                    drawCheckbox("Static Visibility Cache",
                                 shadows.staticVisibilityCachingEnabled,
                                 presetDefaults.staticVisibilityCachingEnabled,
                                 cacheReuseAvailable(),
                                 "Reuse the full-resolution visibility result when all "
                                 "receiver and shadow inputs remain compatible.");
                customChanged |=
                    drawCheckbox("Scene State Caching",
                                 shadows.sceneStateCachingEnabled,
                                 presetDefaults.sceneStateCachingEnabled,
                                 true,
                                 "Reuse validated scene revisions until UVSR reports a "
                                 "shadow-relevant scene change.");
                customChanged |=
                    drawCheckbox("Caster-Only Scene Revision",
                                 shadows.casterOnlySceneRevisionEnabled,
                                 presetDefaults.casterOnlySceneRevisionEnabled,
                                 shadows.sceneStateCachingEnabled,
                                 "Distinguish caster changes from light-only transforms "
                                 "using Donut's dirty scene branches.");
                customChanged |= drawCheckbox(
                    "Shared Six-Clipmap Packet Builder",
                    shadows.sharedClipmapPacketBuilderEnabled,
                    presetDefaults.sharedClipmapPacketBuilderEnabled,
                    sparseMode(),
                    "Traverse compatible casters once and classify the shared "
                    "metadata across all six clipmaps.");
                customChanged |= drawCheckbox(
                    "Persistent Caster Source Cache",
                    shadows.persistentCasterSourceCachingEnabled,
                    presetDefaults.persistentCasterSourceCachingEnabled,
                    sparseMode() && shadows.sharedClipmapPacketBuilderEnabled,
                    "Reuse validated caster source references, transforms, "
                    "bounds, topology, materials, and draw arguments.");
                customChanged |= drawCheckbox(
                    "Opaque Raster Specialization",
                    shadows.opaqueRasterSpecializationEnabled,
                    presetDefaults.opaqueRasterSpecializationEnabled,
                    sparseMode(),
                    "Use the position-only, material-free opaque depth path.");
                customChanged |=
                    drawCheckbox("Lean Alpha-Tested Bindings",
                                 shadows.leanAlphaTestedBindingsEnabled,
                                 presetDefaults.leanAlphaTestedBindingsEnabled,
                                 sparseMode(),
                                 "Bind only shadow-relevant alpha material resources.");
                customChanged |= drawCheckbox(
                    "Deferred Static-Depth Merge",
                    shadows.deferredStaticDepthMergeEnabled,
                    presetDefaults.deferredStaticDepthMergeEnabled,
                    cacheReuseAvailable() && shadows.pairedStaticDynamicDepthEnabled,
                    "Merge scheduled static-dirty pages after static raster "
                    "instead of issuing duplicate static depth atomics.");
                customChanged |=
                    drawCheckbox("Moving-Light Uncached Policy",
                                 shadows.movingLightUncachedEnabled,
                                 presetDefaults.movingLightUncachedEnabled,
                                 cachedSparseMode(),
                                 "Use the receiver-visible merged slice while the light "
                                 "moves, then rebuild and resume compatible caching.");
                customChanged |= drawCheckbox(
                    "Preserve Page Mappings on Content Invalidation",
                    shadows.retainPhysicalMappingsOnContentInvalidationEnabled,
                    presetDefaults.retainPhysicalMappingsOnContentInvalidationEnabled,
                    sparseMode(),
                    "Retain validated physical ownership across logical content "
                    "invalidation; resource recreation remains destructive.");
                customChanged |= drawCheckbox(
                    "Continuous Moving-Light Distance Bias",
                    shadows.movingLightContinuousReceiverBiasEnabled,
                    presetDefaults.movingLightContinuousReceiverBiasEnabled,
                    receiverDistanceClampAvailable && movingBiasActive,
                    "Shift distance thresholds continuously during moving-light "
                    "recovery instead of globally dropping a clipmap.");
                customChanged |= drawCheckbox(
                    "Localized Caster Invalidation",
                    shadows.localizedInvalidationEnabled,
                    presetDefaults.localizedInvalidationEnabled,
                    cacheReuseAvailable(),
                    "Dirty only conservative old-plus-new virtual coverage for "
                    "reliable stable caster identities.");
                customChanged |= drawCheckbox(
                    "Tight Localized Bounds",
                    shadows.tightLocalizedInvalidationBoundsEnabled,
                    presetDefaults.tightLocalizedInvalidationBoundsEnabled,
                    cacheReuseAvailable() && shadows.localizedInvalidationEnabled,
                    "Project original object-space bounds directly and fail "
                    "open when identity or bounds are unreliable.");
                customChanged |=
                    drawCheckbox("GPU-Gated Draw Submission",
                                 shadows.gpuGatedDrawSubmission,
                                 presetDefaults.gpuGatedDrawSubmission,
                                 sparseMode(),
                                 "Let compact GPU work determine which prepared caster "
                                 "packets reach raster.");
                customChanged |= drawCheckbox(
                    "Batched Draw Submission",
                    shadows.batchedDrawSubmissionEnabled,
                    presetDefaults.batchedDrawSubmissionEnabled,
                    sparseMode() && shadows.gpuGatedDrawSubmission,
                    "Reuse compatible state and submit compact indirect draw "
                    "batches.");
                customChanged |=
                    drawCheckbox("Per-Level Empty-Work Skip",
                                 shadows.levelEmptyWorkSkipEnabled,
                                 presetDefaults.levelEmptyWorkSkipEnabled,
                                 sparseMode() && shadows.gpuGatedDrawSubmission &&
                                     shadows.batchedDrawSubmissionEnabled,
                                 "Skip parsing indirect commands for clipmap levels with "
                                 "zero dirty work.");
                customChanged |= drawCheckbox(
                    "Packet Rectangle Direct Scan",
                    shadows.packetRectangleDirectScanEnabled,
                    presetDefaults.packetRectangleDirectScanEnabled,
                    packetPageCullingActive,
                    "Probe small wrapped packet rectangles directly instead of "
                    "scanning the full compact dirty-page list.");
                customChanged |= drawCheckbox(
                    "Scatter Alpha-Test Early Reject",
                    shadows.scatterAlphaTestEarlyRejectEnabled,
                    presetDefaults.scatterAlphaTestEarlyRejectEnabled,
                    dirtyScatterAvailable && shadows.dirtyPageScatterRasterEnabled,
                    "Reject unscheduled scatter holes before sampling opacity.");

                EndAnimatedTreeNode();
            }

            EndAnimatedTreeNode();
        }

        if (BeginAnimatedTreeNode(
                "Diagnostics##SparseVirtualShadowMaps",
                ImGuiTreeNodeFlags_None,
                "Inspect SVSM timing detail and full-screen page-state "
                "diagnostics."))
        {
            customChanged |= drawCheckbox(
                "Detailed GPU Stage Timing",
                shadows.detailedGpuTimingEnabled,
                presetDefaults.detailedGpuTimingEnabled,
                true,
                "Measure Mark, Allocate, Clear, Packet, Render, and Filter "
                "separately. Disable for the lowest-overhead total-only "
                "timing path.");

            static constexpr const char* DebugLabels[] = {
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
            drawCombo(
                "Debug View##SparseVirtualShadowMaps",
                int(shadows.debugView),
                int(presetDefaults.debugView),
                DebugLabels,
                int(std::size(DebugLabels)),
                true,
                "Present the selected full-screen diagnostic. Debug views "
                "enable asynchronous page-counter readback and can disable "
                "otherwise reusable visibility work.",
                true,
                [settings = &shadows](int selectedDebugView)
                {
                    settings->debugView =
                        SvsmDebugView(selectedDebugView);
                    reconcileSvsmPreset(*settings);
                });

            EndAnimatedTreeNode();
        }

        if (customChanged || resetApplied)
            reconcileSvsmPreset(shadows);
        return customChanged || resetApplied;
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
        UIData& ui)
        : ImGui_Renderer(deviceManager)
        , m_app(app)
        , m_ui(ui)
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

        buildUI();
        const bool controlledBenchmarkActive =
            m_app->IsVisibilityBenchmarkQueued() ||
            m_app->IsVisibilityBenchmarkActive() ||
            m_app->IsAntiAliasingMotionTestRunning();
        const bool pixelZoomRequested =
            IsPixelZoomEnabled(m_ui.PixelZoom) &&
            !controlledBenchmarkActive;
        const float deltaTime = ImGui::GetIO().DeltaTime;
        if (controlledBenchmarkActive)
        {
            // Controlled runs bypass the visual transition so zoom submits no
            // benchmark GPU work.
            m_PixelZoomVisibility = 0.f;
            m_RenderedPixelZoom = PixelZoomMode::Off;
            m_PendingPixelZoom = PixelZoomMode::Off;
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
        const float pixelZoomOpacity =
            SmoothPixelZoomVisibility(m_PixelZoomVisibility);
        const float pixelZoomLevelTransitionScale =
            ResolvePixelZoomLevelTransitionScale(
                m_PixelZoomLevelTransition);
        const bool pixelZoomPassActive =
            IsPixelZoomEnabled(m_RenderedPixelZoom) &&
            pixelZoomOpacity > 0.f;
        if (pixelZoomRequested && pixelZoomOpacity > 0.f)
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
                    int(std::round(128.f * pixelZoomOpacity))),
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
            ImFont* zoomLabelFont = m_Font
                ? m_Font->GetScaledFont()
                : ImGui::GetFont();
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
        if (pixelZoomPassActive && m_PixelZoomPass)
            m_PixelZoomPass->Capture(framebuffer);
        if (m_BackdropBlurPass)
        {
            m_BackdropBlurPass->Render(
                framebuffer,
                UiBackgroundBlurPixels,
                m_ui.BackdropRects);
        }
        if (pixelZoomPassActive && m_PixelZoomPass)
        {
            m_PixelZoomPass->Composite(
                framebuffer,
                m_RenderedPixelZoom,
                m_SettingsPanelMarginPixels,
                8.f,
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

protected:
    virtual bool KeyboardUpdate(
        int key,
        int scancode,
        int action,
        int mods) override
    {
        const bool captured = ImGui_Renderer::KeyboardUpdate(
            key, scancode, action, mods);
        if (key == GLFW_KEY_ESCAPE &&
            action == GLFW_PRESS &&
            !ImGui::GetIO().WantTextInput)
        {
            m_ui.ShowUI = !m_ui.ShowUI;
            return true;
        }
        const bool plainZoomShortcut =
            (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)) == 0;
        if (key == GLFW_KEY_Z &&
            action == GLFW_PRESS &&
            plainZoomShortcut &&
            !ImGui::GetIO().WantTextInput)
        {
            const bool benchmarkRunning =
                m_app->IsVisibilityBenchmarkQueued() ||
                m_app->IsVisibilityBenchmarkActive() ||
                m_app->IsAntiAliasingMotionTestRunning();
            if (!benchmarkRunning)
            {
                m_ui.PixelZoom =
                    AdvancePixelZoomMode(m_ui.PixelZoom);
            }
            return true;
        }
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
        g_SettingsAppearanceDrawLists.clear();
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
        const ImFont* scaledUiFont = m_Font
            ? m_Font->GetScaledFont()
            : nullptr;
        const float panelReferenceFontSize = scaledUiFont
            ? scaledUiFont->LegacySize
            : ImGui::GetFontSize();
        m_SettingsPanelMarginPixels = static_cast<uint32_t>(
            std::max(
                1.f,
                std::round(panelReferenceFontSize * 0.6f)));
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

        const bool sceneLoading = m_app->IsSceneLoading();
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
                m_SettingsAppearance = 0.f;
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
            static constexpr const char* LoadingDots[] = {
                ".",
                "..",
                "..."
            };
            const size_t loadingDotIndex =
                size_t(ImGui::GetTime() * 2.0) % std::size(LoadingDots);

            char messageBuffer[512];
            const std::string sceneDisplayName =
                m_app->GetCurrentSceneDisplayName();
            snprintf(
                messageBuffer,
                std::size(messageBuffer),
                "Loading scene: %s, please wait%s\n"
                "Objects: %u/%u / Import steps: %llu/%llu / "
                "Textures decoded: %u/%u / GPU ready: %u/%u",
                sceneDisplayName.c_str(),
                LoadingDots[loadingDotIndex],
                objectsLoaded,
                objectsTotal,
                static_cast<unsigned long long>(importStepsCompleted),
                static_cast<unsigned long long>(importStepsTotal),
                texturesDecoded,
                texturesTotal,
                texturesReady,
                texturesTotal);
            DrawScreenCenteredText(messageBuffer);

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
        m_SettingsAppearance = AdvancePixelZoomVisibility(
            m_SettingsAppearance,
            m_ui.ShowUI,
            ImGui::GetIO().DeltaTime);
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
                    g_DeferredAliasingUiPresentation.ReadyForCommit() &&
                    dropdownPopupIdle &&
                    interactionIdle;
            };
        if (!m_ui.ShowUI && m_SettingsAppearance <= 0.f)
        {
            // A hidden owner cannot submit the popup frames needed to finish
            // its roll-up. Close that exact popup before evaluating the same
            // deferred commit barrier used by the visible path.
            FinishDeferredDropdownPopupTransition();
            g_DeferredAliasingUiPresentation.SkipInvisibleAnimation(
                ImGui::GetFrameCount());
            const SettingsScrollStabilityContext& scrollContext =
                g_SettingsScrollStabilityContext;
            const bool recentLayoutAnimation =
                scrollContext.lastFrame >= ImGui::GetFrameCount() - 1 &&
                scrollContext.layoutAnimatingLastFrame;
            TryApplyDeferredDropdownUiActions(
                deferredDropdownCompositionIdle(
                    !recentLayoutAnimation,
                    true));
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
                float(width) - settingsPanelMarginPixels * 2.f);
        const float settingsWindowWidth = std::min(
            fontSize * SettingsWindowWidthInFontHeights,
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
        ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once);
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
                    StatusLineSpacing);
            ImGui::TextUnformatted(performanceLine.c_str());
            ImGui::SetItemTooltip(
                "tris counts frustum-culled triangle instances submitted by "
                "the main geometry pass; occluded, back-facing, and "
                "alpha-discarded triangles can still be included. "
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
                "Choose the GPU. UVSR restarts after a change.");
        }

        ImGui::TextUnformatted("Camera Mode");
        const bool benchmarkCameraActive = m_app->IsBenchmarkCameraActive();
        if (benchmarkCameraActive)
            ImGui::BeginDisabled();
        if (DrawPresetResetIcon(
                "Camera Mode",
                m_ui.Camera != CameraMode::ThirdPerson))
        {
            m_app->SetCameraMode(CameraMode::ThirdPerson);
        }
        if (benchmarkCameraActive)
            ImGui::EndDisabled();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (benchmarkCameraActive)
            ImGui::BeginDisabled();
        const bool cameraComboOpen = BeginRoundedCombo(
            "##Camera", GetCameraModeLabel(m_ui.Camera));
        ImGui::SetItemTooltip(benchmarkCameraActive
            ? "The benchmark camera is Locked."
            : "Choose Freelook or Locked. Space moves up, Shift moves down, "
                "X/C roll, and V levels the roll.");
        if (cameraComboOpen)
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
        if (benchmarkCameraActive)
            ImGui::EndDisabled();

        const bool cameraLocationsAvailable = m_app->HasSponzaCameraLocations();
        if (cameraLocationsAvailable)
        {
            ImGui::TextUnformatted("Camera Location");
            const SponzaCameraLocation selectedCameraLocation =
                m_app->GetSponzaCameraLocation();
            if (benchmarkCameraActive)
                ImGui::BeginDisabled();
            if (DrawPresetResetIcon(
                    "Camera Location",
                    selectedCameraLocation !=
                        SponzaCameraLocation::SimplifiedApproximation))
            {
                m_app->SetSponzaCameraLocation(
                    SponzaCameraLocation::SimplifiedApproximation);
            }
            if (benchmarkCameraActive)
                ImGui::EndDisabled();
            if (benchmarkCameraActive)
                ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(-FLT_MIN);
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
                    DrawDeferredDropdownOption(
                        GetSponzaCameraLocationLabel(location),
                        GetSponzaCameraLocationLabel(location),
                        selected,
                        [this, location]()
                        {
                            m_app->SetSponzaCameraLocation(location);
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (benchmarkCameraActive)
                ImGui::EndDisabled();
        }

        ImGui::TextUnformatted("World Materials");
        if (DrawPresetResetIcon(
                "World Materials",
                selectedWorldMaterial !=
                    WorldMaterialView::WhiteWorldOff))
        {
            m_ui.ScreenSpaceVisibility.showIndirectDiffuseOnly = false;
            m_app->SetWhiteWorldMode(WhiteWorldMode::Off);
        }
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
                const WorldMaterialViewState candidateState =
                    MakeWorldMaterialViewState(view);
                DrawDeferredDropdownOption(
                    GetWorldMaterialViewLabel(view),
                    GetWorldMaterialViewLabel(view),
                    selected,
                    [this, candidateState]()
                    {
                        m_ui.ScreenSpaceVisibility
                            .showIndirectDiffuseOnly = false;
                        m_app->SetWhiteWorldMode(
                            WhiteWorldMode(
                                candidateState.whiteWorldMode));
                        m_ui.ScreenSpaceVisibility
                            .showIndirectDiffuseOnly =
                                candidateState
                                    .showIndirectDiffuseOnly;
                    });
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
            "sampling, and reconstruction.");
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

            const VisibilityPerformanceProfileConfiguration
                activeConfiguration =
                    GetEffectiveVisibilityPerformanceConfiguration(
                        visibility);
            const ScreenSpaceVisibilityQuality visibilityPresetOrigin =
                visibility.quality == ScreenSpaceVisibilityQuality::Custom
                    ? visibility.qualityPresetOrigin
                    : visibility.quality;
            ScreenSpaceVisibilitySettings visibilityPreset;
            ApplyScreenSpaceVisibilityQualityPreset(
                visibilityPreset, visibilityPresetOrigin);
            const VisibilityPerformanceProfileConfiguration
                visibilityPresetConfiguration =
                    GetEffectiveVisibilityPerformanceConfiguration(
                        visibilityPreset);
            ScreenSpaceVisibilitySettings* visibilityPointer =
                &visibility;

            auto applyQualityPreset =
                [this, visibilityPointer](
                    ScreenSpaceVisibilityQuality quality)
                {
                    ApplyScreenSpaceVisibilityQualityPreset(
                        *visibilityPointer, quality);
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                    m_ui.EnablePbr = true;
                };
            auto switchVisibilityToCustom =
                [this, visibilityPointer]()
                {
                    MakeVisibilityPerformanceComposable(
                        *visibilityPointer);
                    MarkScreenSpaceVisibilityQualityCustom(
                        *visibilityPointer);
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                };
            auto finishVisibilityPresetReset =
                [this, visibilityPointer]()
                {
                    ReconcileScreenSpaceVisibilityQualityPreset(
                        *visibilityPointer);
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                    m_ui.EnablePbr = visibilityPointer->enabled;
                };
            auto editableConfiguration =
                [switchVisibilityToCustom, visibilityPointer]()
                -> VisibilityPerformanceProfileConfiguration&
                {
                    switchVisibilityToCustom();
                    return visibilityPointer->performance.configuration;
                };
            auto applyReconstructionCategory =
                [editableConfiguration, visibilityPointer](
                    VisibilityPerformanceProfile profile)
                {
                    const auto source =
                        GetVisibilityPerformanceProfileConfiguration(profile);
                    auto& target = editableConfiguration();
                    target.reconstruction = source.reconstruction;
                    target.edgeStorage = source.edgeStorage;
                    if (source.reconstruction ==
                        VisibilityReconstructionMode::PackedEdges2x2)
                    {
                        visibilityPointer->reconstruction.spatialEnabled =
                            false;
                    }
                    visibilityPointer->performance.packedEdgeMode =
                        GetPackedEdgeMode(profile);
                };
            auto applyApplicationCategory =
                [editableConfiguration, visibilityPointer](
                    VisibilityPerformanceProfile profile)
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
                        visibilityPointer->indirectDiffuse.enabled = false;
                        visibilityPointer->reconstruction.spatialEnabled =
                            false;
                        if (source.reconstruction !=
                            VisibilityReconstructionMode::Legacy)
                        {
                            target.reconstruction =
                                source.reconstruction;
                            target.edgeStorage = source.edgeStorage;
                        }
                    }
                };
            auto keepApplicationCompatibleWithConsumers =
                [editableConfiguration, visibilityPointer]()
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
                    if (visibilityPointer->HasActiveAmbientOcclusion() &&
                        !visibilityPointer->HasActiveIndirectDiffuse())
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
            if (DrawPresetResetIcon(
                    "Visibility Enabled",
                    visibility.enabled != visibilityPreset.enabled))
            {
                visibility.enabled = visibilityPreset.enabled;
                finishVisibilityPresetReset();
            }

            if (BeginAnimatedToggleRegion(
                    "##VisibilityEnabledControls",
                    visibility.enabled))
            {
            static const char* resolutionLabels[] = {
                "Full", "Half", "Quarter"
            };
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Sampling Resolution",
                    resolutionLabels[int(visibility.resolution)]))
            {
                static constexpr std::array<
                    VisibilityResolution, 3> resolutionOrder = {
                        VisibilityResolution::Quarter,
                        VisibilityResolution::Half,
                        VisibilityResolution::Full
                    };
                for (const VisibilityResolution resolution : resolutionOrder)
                {
                    const int index = int(resolution);
                    const bool selected = visibility.resolution == resolution;
                    DrawDeferredDropdownOption(
                        resolutionLabels[index],
                        resolutionLabels[index],
                        selected,
                        [visibilityPointer,
                            switchVisibilityToCustom,
                            resolution]()
                        {
                            visibilityPointer->resolution = resolution;
                            switchVisibilityToCustom();
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the sampling resolution for screen-space visibility.");
            if (DrawPresetResetIcon(
                    "Visibility Sampling Resolution",
                    visibility.resolution != visibilityPreset.resolution))
            {
                visibility.resolution = visibilityPreset.resolution;
                finishVisibilityPresetReset();
            }

            static const char* qualityPresetLabels[] = {
                "Low", "Medium", "High", "Ultra"
            };
            static const char* qualityPresetDescriptions[] = {
                "Quarter resolution, Uniform Projected Angle, 8 samples, "
                    "Toroidal Blue noise, compact "
                    "depth-normal joint-bilateral upsampling, and one GI "
                    "bounce.",
                "Half resolution, 8 samples, Toroidal Blue "
                    "Noise, compact depth-normal joint-bilateral upsampling, "
                    "and one GI bounce.",
                "Factory default: full resolution, 20 samples, Toroidal Blue "
                    "noise, Performance Precision "
                    "buffers, and one GI bounce.",
                "Full resolution, 48 samples, Toroidal Blue "
                    "noise, Default Precision buffers, and two "
                    "GI bounces."
            };
            const bool qualityPresetSelected =
                visibility.quality != ScreenSpaceVisibilityQuality::Custom;
            const int selectedPresetIndex = std::clamp(
                int(qualityPresetSelected
                    ? visibility.quality
                    : visibility.qualityPresetOrigin),
                0,
                int(std::size(qualityPresetLabels)) - 1);
            std::string selectedProfileName =
                qualityPresetLabels[selectedPresetIndex];
            if (!qualityPresetSelected)
                selectedProfileName += " (Custom)";
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Profile",
                    selectedProfileName.c_str()))
            {
                ImGui::TextDisabled("Quality Presets");
                for (int presetIndex = 0;
                    presetIndex < int(std::size(qualityPresetLabels));
                    ++presetIndex)
                {
                    const auto quality =
                        ScreenSpaceVisibilityQuality(presetIndex);
                    const bool selected = visibility.quality == quality;
                    DrawDeferredDropdownOption(
                        qualityPresetLabels[presetIndex],
                        qualityPresetLabels[presetIndex],
                        selected,
                        [applyQualityPreset, quality]()
                        {
                            applyQualityPreset(quality);
                        });
                    ImGui::SetItemTooltip(
                        "%s", qualityPresetDescriptions[presetIndex]);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }

                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose Low, Medium, High, or Ultra. Later edits remain "
                "active and append (Custom) to the originating preset.");
            if (DrawPresetResetIcon(
                    "Visibility Profile",
                    visibility.quality !=
                        ScreenSpaceVisibilityQuality::High,
                    "Reset every Visibility setting to factory High."))
            {
                applyQualityPreset(
                    ScreenSpaceVisibilityQuality::High);
            }

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
                        DrawDeferredDropdownOption(
                            estimatorLabels[estimatorIndex],
                            estimatorLabels[estimatorIndex],
                            selected,
                            [visibilityPointer,
                                switchVisibilityToCustom,
                                estimator]()
                            {
                                visibilityPointer->estimator = estimator;
                                switchVisibilityToCustom();
                            });
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip("Choose how samples spread around each pixel.");
                if (DrawNestedDropdownResetIcon(
                        "Visibility Estimator",
                        visibility.estimator != visibilityPreset.estimator))
                {
                    visibility.estimator = visibilityPreset.estimator;
                    finishVisibilityPresetReset();
                }
                static const char* schedulerLabels[] = {
                    "Independent Hash",
                    "Toroidal Blue"
                };
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Noise Pattern",
                        schedulerLabels[int(sampling.scheduler)]))
                {
                    for (int index = 0;
                        index < int(std::size(schedulerLabels));
                        ++index)
                    {
                        const auto scheduler =
                            VisibilitySampleScheduler(index);
                        const bool selected = sampling.scheduler == scheduler;
                        DrawDeferredDropdownOption(
                            schedulerLabels[index],
                            schedulerLabels[index],
                            selected,
                            [switchVisibilityToCustom,
                                visibilityPointer,
                                scheduler]()
                            {
                                visibilityPointer->sampling.scheduler =
                                    scheduler;
                                switchVisibilityToCustom();
                            });
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose independent hashing or the tiled Toroidal Blue "
                    "rank field used by the quality presets.");
                if (DrawNestedDropdownResetIcon(
                        "Visibility Noise Pattern",
                        sampling.scheduler !=
                                visibilityPreset.sampling.scheduler))
                {
                    sampling.scheduler =
                        visibilityPreset.sampling.scheduler;
                    finishVisibilityPresetReset();
                }

                const ImGuiID sampleSliderId =
                    ImGui::GetID("Samples##VisibilitySamples");
                ImGuiStorage* sampleSliderStorage =
                    ImGui::GetStateStorage();
                const ImGuiID samplePreviewKey =
                    sampleSliderId ^ ImGuiID(0x65D1A903u);
                const ImGuiID samplePendingKey =
                    sampleSliderId ^ ImGuiID(0x49BC2E17u);
                const int committedSliderValue = int(std::clamp(
                    sampling.maximumSampleCount, 1u, 64u));
                if (!sampleSliderStorage->GetBool(
                        samplePendingKey, false))
                {
                    sampleSliderStorage->SetInt(
                        samplePreviewKey,
                        committedSliderValue);
                }
                int sampleSliderValue =
                    sampleSliderStorage->GetInt(
                        samplePreviewKey,
                        committedSliderValue);
                sampleSliderValue = std::clamp(
                    sampleSliderValue,
                    1,
                    64);
                const bool sampleSliderChanged = DrawSliderInt(
                        "Samples##VisibilitySamples",
                        &sampleSliderValue,
                        1,
                        64,
                        "%d",
                        ImGuiSliderFlags_AlwaysClamp);
                if (sampleSliderChanged)
                {
                    sampleSliderStorage->SetInt(
                        samplePreviewKey,
                        sampleSliderValue);
                    sampleSliderStorage->SetBool(
                        samplePendingKey,
                        true);
                }
                const bool commitSampleSlider =
                    sampleSliderStorage->GetBool(
                        samplePendingKey, false) &&
                    !ImGui::IsItemActive();
                if (commitSampleSlider)
                {
                    const int committedPreview =
                        sampleSliderStorage->GetInt(
                            samplePreviewKey,
                            committedSliderValue);
                    sampling.maximumSampleCount =
                        uint32_t(std::clamp(committedPreview, 1, 64));
                    sampleSliderStorage->SetBool(
                        samplePendingKey,
                        false);
                    samplingChanged = true;
                }
                ImGui::SetItemTooltip(
                    "Set the 1-64 radial-sample budget used by AO and every "
                    "GI bounce. Runtime validates the count and selects a "
                    "compact even, odd, or guarded loop.");
                if (DrawPresetResetIcon(
                        "Visibility Samples",
                        sampling.maximumSampleCount !=
                            visibilityPreset.sampling.maximumSampleCount))
                {
                    sampleSliderStorage->SetBool(
                        samplePendingKey,
                        false);
                    sampling.maximumSampleCount =
                        visibilityPreset.sampling.maximumSampleCount;
                    finishVisibilityPresetReset();
                }

                samplingChanged |= DrawSliderFloat(
                    "Radius", &sampling.radius, 0.01f, std::max(m_app->GetSceneDiagonal() * 0.1f, 1.f), "%.3f");
                ImGui::SetItemTooltip("Set how far visibility rays reach.");
                if (DrawPresetResetIcon(
                        "Visibility Radius",
                        sampling.radius != visibilityPreset.sampling.radius))
                {
                    sampling.radius = visibilityPreset.sampling.radius;
                    finishVisibilityPresetReset();
                }
                samplingChanged |= DrawSliderFloat(
                    "Thickness", &sampling.thickness, 0.0f, std::max(m_app->GetSceneDiagonal() * 0.02f, 0.5f), "%.3f");
                ImGui::SetItemTooltip("Set the assumed thickness of occluders.");
                if (DrawPresetResetIcon(
                        "Visibility Thickness",
                        sampling.thickness !=
                            visibilityPreset.sampling.thickness))
                {
                    sampling.thickness =
                        visibilityPreset.sampling.thickness;
                    finishVisibilityPresetReset();
                }
                samplingChanged |= DrawSliderFloat(
                    "Distribution",
                    &sampling.stepDistributionExponent,
                    0.5f,
                    4.0f,
                    "%.2f");
                ImGui::SetItemTooltip(
                    "Increase to place more visibility samples nearby.");
                if (DrawPresetResetIcon(
                        "Visibility Distribution Exponent",
                        sampling.stepDistributionExponent !=
                            visibilityPreset.sampling
                                .stepDistributionExponent))
                {
                    sampling.stepDistributionExponent =
                        visibilityPreset.sampling
                            .stepDistributionExponent;
                    finishVisibilityPresetReset();
                }

                if (samplingChanged)
                {
                    MarkScreenSpaceVisibilityQualityCustom(visibility);
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
                if (DrawPresetResetIcon(
                        "Visibility Ambient Occlusion Enabled",
                        ao.enabled !=
                            visibilityPreset.ambientOcclusion.enabled))
                {
                    ao.enabled =
                        visibilityPreset.ambientOcclusion.enabled;
                    finishVisibilityPresetReset();
                }
                if (BeginAnimatedToggleRegion(
                        "##AmbientVisibilityControls",
                        ao.enabled))
                {
                    aoChanged |= DrawSliderFloat(
                        "Strength", &ao.strength, 0.0f, 2.0f, "%.2f");
                    ImGui::SetItemTooltip(
                        "Set how strongly AO darkens indirect light.");
                    if (DrawPresetResetIcon(
                            "Visibility Ambient Occlusion Strength",
                            ao.strength !=
                                visibilityPreset.ambientOcclusion.strength))
                    {
                        ao.strength =
                            visibilityPreset.ambientOcclusion.strength;
                        finishVisibilityPresetReset();
                    }
                    aoChanged |= DrawSliderFloat(
                        "Power", &ao.power, 0.1f, 4.0f, "%.2f");
                    ImGui::SetItemTooltip(
                        "Shape the AO response. One preserves the traced "
                        "result; higher values deepen occlusion and lower "
                        "values soften it.");
                    if (DrawPresetResetIcon(
                            "Visibility Ambient Occlusion Power",
                            ao.power !=
                                visibilityPreset.ambientOcclusion.power))
                    {
                        ao.power =
                            visibilityPreset.ambientOcclusion.power;
                        finishVisibilityPresetReset();
                    }
                    EndAnimatedToggleRegion();
                }
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
                if (DrawPresetResetIcon(
                        "Visibility Indirect Diffuse Enabled",
                        gi.enabled !=
                            visibilityPreset.indirectDiffuse.enabled))
                {
                    gi.enabled =
                        visibilityPreset.indirectDiffuse.enabled;
                    finishVisibilityPresetReset();
                }
                if (BeginAnimatedToggleRegion(
                        "##IndirectDiffuseControls",
                        gi.enabled))
                {
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
                    if (DrawPresetResetIcon(
                            "Visibility Limit Bounces",
                            gi.limitBounces !=
                                visibilityPreset.indirectDiffuse
                                    .limitBounces))
                    {
                        gi.limitBounces =
                            visibilityPreset.indirectDiffuse.limitBounces;
                        gi.minimumBounceContribution =
                            visibilityPreset.indirectDiffuse
                                .minimumBounceContribution;
                        finishVisibilityPresetReset();
                    }
                    if (BeginAnimatedToggleRegion(
                            "##BounceLimitControls",
                            gi.limitBounces))
                    {
                        int bounceCount = int(std::clamp(
                            gi.bounceCount,
                            1u,
                            MaxIndirectDiffuseBounceCount));
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
                        if (DrawPresetResetIcon(
                                "Visibility Bounce Count",
                                gi.bounceCount !=
                                    visibilityPreset.indirectDiffuse
                                        .bounceCount))
                        {
                            gi.bounceCount =
                                visibilityPreset.indirectDiffuse.bounceCount;
                            finishVisibilityPresetReset();
                        }
                        EndAnimatedToggleRegion();
                    }
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
                        if (DrawPresetResetIcon(
                                "Visibility Bounce Contribution",
                                gi.minimumBounceContribution !=
                                    visibilityPreset.indirectDiffuse
                                        .minimumBounceContribution))
                        {
                            gi.minimumBounceContribution =
                                visibilityPreset.indirectDiffuse
                                    .minimumBounceContribution;
                            finishVisibilityPresetReset();
                        }
                    }
                    giChanged |= DrawSliderFloat(
                        "Intensity##IndirectDiffuse",
                        &gi.intensity,
                        0.0f,
                        10.0f,
                        "%.2f");
                    ImGui::SetItemTooltip(
                        "Set screen-space diffuse GI brightness.");
                    if (DrawPresetResetIcon(
                            "Visibility Indirect Diffuse Intensity",
                            gi.intensity !=
                                visibilityPreset.indirectDiffuse.intensity))
                    {
                        gi.intensity =
                            visibilityPreset.indirectDiffuse.intensity;
                        finishVisibilityPresetReset();
                    }
                    giChanged |= ImGui::Checkbox(
                        "Include Emissive Sources",
                        &gi.includeEmissive);
                    ImGui::SetItemTooltip(
                        "Let visible emissive surfaces light the scene.");
                    if (DrawPresetResetIcon(
                            "Visibility Include Emissive Sources",
                            gi.includeEmissive !=
                                visibilityPreset.indirectDiffuse
                                    .includeEmissive))
                    {
                        gi.includeEmissive =
                            visibilityPreset.indirectDiffuse.includeEmissive;
                        finishVisibilityPresetReset();
                    }
                    if (BeginAnimatedToggleRegion(
                            "##EmissiveSourceControls",
                            gi.includeEmissive))
                    {
                        giChanged |= DrawSliderFloat(
                            "Emissive Source Gain",
                            &gi.emissiveGain,
                            0.0f,
                            10.0f,
                            "%.2f");
                        ImGui::SetItemTooltip(
                            "Set emissive surfaces' GI strength.");
                        if (DrawPresetResetIcon(
                                "Visibility Emissive Source Gain",
                                gi.emissiveGain !=
                                    visibilityPreset.indirectDiffuse
                                        .emissiveGain))
                        {
                            gi.emissiveGain =
                                visibilityPreset.indirectDiffuse.emissiveGain;
                            finishVisibilityPresetReset();
                        }
                        EndAnimatedToggleRegion();
                    }
                    EndAnimatedToggleRegion();
                }
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
                VisibilityReconstructionSettings*
                    reconstructionPointer = &reconstruction;
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
                            ? "Gaussian Bilateral"
                            : "Joint Bilateral")
                        : (fullResolutionWithoutReconstruction
                            ? "Full Resolution"
                            : "Guide-Aware Upsampling");
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
                    DrawDeferredDropdownOption(
                        "Full Resolution",
                        "Full Resolution",
                        fullResolutionWithoutReconstruction,
                        [applyReconstructionCategory,
                            applyApplicationCategory,
                            visibilityPointer,
                            reconstructionPointer]()
                        {
                            applyReconstructionCategory(
                                VisibilityPerformanceProfile::
                                    Runtime);
                            applyApplicationCategory(
                                VisibilityPerformanceProfile::
                                    Runtime);
                            visibilityPointer->resolution =
                                VisibilityResolution::Full;
                            reconstructionPointer->spatialEnabled = false;
                        });
                    ImGui::SetItemTooltip(
                        "Trace visibility at the output resolution and bypass "
                        "the spatial reconstruction pass. No upsampling is "
                        "required; filtering remains optional.");
                    DrawDeferredDropdownOption(
                        "Guide-Aware Upsampling",
                        "Guide-Aware Upsampling",
                        reducedResolutionUpsampling,
                        [applyReconstructionCategory,
                            applyApplicationCategory,
                            visibilityPointer,
                            reconstructionPointer]()
                        {
                            applyReconstructionCategory(
                                VisibilityPerformanceProfile::
                                    Runtime);
                            applyApplicationCategory(
                                VisibilityPerformanceProfile::
                                    Runtime);
                            if (visibilityPointer->resolution ==
                                VisibilityResolution::Full)
                            {
                                visibilityPointer->resolution =
                                    VisibilityResolution::Half;
                            }
                            reconstructionPointer->spatialEnabled = false;
                        });
                    ImGui::SetItemTooltip(
                        "Reconstruct reduced-resolution visibility at output "
                        "resolution with the minimum guide-aware pass and no "
                        "optional denoising.");
                    const bool jointBilateralSelected =
                        legacyReconstruction &&
                        reconstruction.spatialEnabled &&
                        reconstruction.spatialFilter ==
                            VisibilitySpatialFilter::JointBilateral;
                    DrawDeferredDropdownOption(
                        "Joint Bilateral",
                        "Joint Bilateral",
                        jointBilateralSelected,
                        [applyReconstructionCategory,
                            applyApplicationCategory,
                            reconstructionPointer]()
                        {
                            applyReconstructionCategory(
                                VisibilityPerformanceProfile::
                                    Runtime);
                            applyApplicationCategory(
                                VisibilityPerformanceProfile::
                                    Runtime);
                            reconstructionPointer->spatialEnabled = true;
                            reconstructionPointer->spatialFilter =
                                VisibilitySpatialFilter::JointBilateral;
                        });
                    ImGui::SetItemTooltip(
                        "Denoise while reconstructing, using depth and normal "
                        "guides to reduce cross-edge bleeding.");
                    const bool gaussianBilateralSelected =
                        legacyReconstruction &&
                        reconstruction.spatialEnabled &&
                        reconstruction.spatialFilter ==
                            VisibilitySpatialFilter::
                                GaussianJointBilateral;
                    DrawDeferredDropdownOption(
                        "Gaussian Bilateral",
                        "Gaussian Bilateral",
                        gaussianBilateralSelected,
                        [applyReconstructionCategory,
                            applyApplicationCategory,
                            reconstructionPointer]()
                        {
                            applyReconstructionCategory(
                                VisibilityPerformanceProfile::
                                    Runtime);
                            applyApplicationCategory(
                                VisibilityPerformanceProfile::
                                    Runtime);
                            reconstructionPointer->spatialEnabled = true;
                            reconstructionPointer->spatialFilter =
                                VisibilitySpatialFilter::
                                    GaussianJointBilateral;
                        });
                    ImGui::SetItemTooltip(
                        "Use a wider Gaussian-weighted depth-and-normal "
                        "reconstruction kernel. The radius appears below.");
                    ImGui::Separator();
                    ImGui::TextDisabled("Edge-Guided Reconstruction");
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
                        if (available)
                        {
                            DrawDeferredDropdownOption(
                                profileUiLabel.data(),
                                profileUiLabel.data(),
                                selected,
                                [applyReconstructionCategory,
                                    applyApplicationCategory,
                                    visibilityPointer,
                                    profile]()
                                {
                                    applyReconstructionCategory(profile);
                                    applyApplicationCategory(
                                        VisibilityPerformanceProfile::
                                            Runtime);
                                    if (visibilityPointer->resolution ==
                                        VisibilityResolution::Full)
                                    {
                                        visibilityPointer->resolution =
                                            VisibilityResolution::Half;
                                    }
                                });
                        }
                        else
                        {
                            ImGui::Selectable(
                                profileUiLabel.data(),
                                selected);
                        }
                        ImGui::SetItemTooltip(
                            "%s", GetEdgeReconstructionTooltip(profile));
                        if (!available)
                            ImGui::EndDisabled();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose whether visibility bypasses reconstruction, is "
                    "upsampled, or is denoised while being reconstructed.");
                if (DrawNestedDropdownResetIcon(
                        "Visibility Reconstruction Method",
                        activeConfiguration.reconstruction !=
                                visibilityPresetConfiguration
                                    .reconstruction ||
                            activeConfiguration.edgeStorage !=
                                visibilityPresetConfiguration.edgeStorage ||
                            reconstruction.spatialEnabled !=
                                visibilityPreset.reconstruction
                                    .spatialEnabled ||
                            reconstruction.spatialFilter !=
                                visibilityPreset.reconstruction
                                    .spatialFilter ||
                            visibility.performance.packedEdgeMode !=
                                visibilityPreset.performance
                                    .packedEdgeMode))
                {
                    auto& target = editableConfiguration();
                    target.reconstruction =
                        visibilityPresetConfiguration.reconstruction;
                    target.edgeStorage =
                        visibilityPresetConfiguration.edgeStorage;
                    reconstruction.spatialEnabled =
                        visibilityPreset.reconstruction.spatialEnabled;
                    reconstruction.spatialFilter =
                        visibilityPreset.reconstruction.spatialFilter;
                    visibility.performance.packedEdgeMode =
                        visibilityPreset.performance.packedEdgeMode;
                    finishVisibilityPresetReset();
                }
                if (activeConfiguration.reconstruction ==
                        VisibilityReconstructionMode::Legacy &&
                    reconstruction.spatialEnabled &&
                    reconstruction.spatialFilter ==
                        VisibilitySpatialFilter::GaussianJointBilateral)
                {
                    const bool filterRadiusChanged = DrawSliderFloat(
                        "Filter Radius##GaussianRadius",
                        &reconstruction.spatialRadius,
                        1.0f,
                        12.0f,
                        "%.1f");
                    ImGui::SetItemTooltip(
                        "Set how far the Gaussian denoiser reaches.");
                    if (filterRadiusChanged)
                        switchVisibilityToCustom();
                    if (DrawPresetResetIcon(
                            "Visibility Reconstruction Filter Radius",
                            reconstruction.spatialRadius !=
                                visibilityPreset.reconstruction
                                    .spatialRadius))
                    {
                        reconstruction.spatialRadius =
                            visibilityPreset.reconstruction.spatialRadius;
                        finishVisibilityPresetReset();
                    }
                }

                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(
                        "Final Application",
                        GetApplicationLabel(activeConfiguration.application)))
                {
                    const bool separateApplicationSelected =
                        activeConfiguration.application ==
                            VisibilityApplicationMode::
                                LegacySeparateComposition;
                    DrawDeferredDropdownOption(
                        "Separate",
                        "Separate",
                        separateApplicationSelected,
                        [applyApplicationCategory]()
                        {
                            applyApplicationCategory(
                                VisibilityPerformanceProfile::
                                    Runtime);
                        });
                    struct ApplicationOption
                    {
                        VisibilityPerformanceProfile profile;
                        const char* label;
                    };
                    const ApplicationOption applicationOptions[] = {
                        {
                            VisibilityPerformanceProfile::
                                ExactFusedResolveApply,
                            "Fused"
                        },
                        {
                            VisibilityPerformanceProfile::
                                AlgorithmicFusedPackedEdges2x2,
                            "Fused Edge"
                        }
                    };
                    for (const ApplicationOption& option :
                        applicationOptions)
                    {
                        const auto configuration =
                            GetVisibilityPerformanceProfileConfiguration(
                                option.profile);
                        const bool selected =
                            activeConfiguration.application ==
                                configuration.application;
                        DrawDeferredDropdownOption(
                            option.label,
                            option.label,
                            selected,
                            [applyApplicationCategory,
                                profile = option.profile]()
                            {
                                applyApplicationCategory(profile);
                            });
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(
                    "Choose separate composition or a fused resolve-and-"
                    "apply dispatch. Fused work is timed as one pass.");
                if (DrawNestedDropdownResetIcon(
                        "Visibility Final Application",
                        activeConfiguration.application !=
                                visibilityPresetConfiguration.application ||
                            activeConfiguration.explicitHalfRoundtrip !=
                                visibilityPresetConfiguration
                                    .explicitHalfRoundtrip ||
                            activeConfiguration.consumerRequirement !=
                                visibilityPresetConfiguration
                                    .consumerRequirement ||
                            activeConfiguration.resolutionRequirement !=
                                visibilityPresetConfiguration
                                    .resolutionRequirement))
                {
                    auto& target = editableConfiguration();
                    target.application =
                        visibilityPresetConfiguration.application;
                    target.explicitHalfRoundtrip =
                        visibilityPresetConfiguration
                            .explicitHalfRoundtrip;
                    target.consumerRequirement =
                        visibilityPresetConfiguration
                            .consumerRequirement;
                    target.resolutionRequirement =
                        visibilityPresetConfiguration
                            .resolutionRequirement;
                    finishVisibilityPresetReset();
                }
                EndAnimatedTreeNode();
            }

                EndAnimatedToggleRegion();
            }
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
            BeginDrawerBody(
                "##BuffersBody",
                settingsControlWidth);
            ImGui::PushID("BufferControls");
            if (visibilityBenchmarkBusy)
                ImGui::BeginDisabled();

            ScreenSpaceVisibilitySettings& bufferVisibility =
                m_ui.ScreenSpaceVisibility;
            ScreenSpaceVisibilitySettings* bufferVisibilityPointer =
                &bufferVisibility;
            auto& buffers =
                bufferVisibility.performance.bufferPrecision;
            VisibilityBufferPrecisionSettings* buffersPointer =
                &buffers;
            const ScreenSpaceVisibilityQuality bufferPresetOrigin =
                bufferVisibility.quality ==
                        ScreenSpaceVisibilityQuality::Custom
                    ? bufferVisibility.qualityPresetOrigin
                    : bufferVisibility.quality;
            ScreenSpaceVisibilitySettings bufferVisibilityPreset;
            ApplyScreenSpaceVisibilityQualityPreset(
                bufferVisibilityPreset, bufferPresetOrigin);
            const VisibilityBufferPrecisionSettings& presetBuffers =
                bufferVisibilityPreset.performance.bufferPrecision;
            using ScalarPrecision = VisibilityScalarBufferPrecision;
            using VectorPrecision = VisibilityVectorBufferPrecision;
            const auto buffersMatch =
                [](const VisibilityBufferPrecisionSettings& left,
                    const VisibilityBufferPrecisionSettings& right)
                {
                    return left.rawAmbient == right.rawAmbient &&
                        left.rawIndirect == right.rawIndirect &&
                        left.cumulativeIndirect ==
                            right.cumulativeIndirect &&
                        left.temporalAmbient == right.temporalAmbient &&
                        left.temporalIndirect == right.temporalIndirect &&
                        left.temporalDepth == right.temporalDepth &&
                        left.finalAmbient == right.finalAmbient &&
                        left.finalIndirect == right.finalIndirect &&
                        left.depthHierarchy == right.depthHierarchy;
                };
            const auto finishBufferPresetEdit =
                [this, bufferVisibilityPointer]()
                {
                    MarkScreenSpaceVisibilityQualityCustom(
                        *bufferVisibilityPointer);
                    ReconcileScreenSpaceVisibilityQualityPreset(
                        *bufferVisibilityPointer);
                    m_ui.VisibilityVerification =
                        VisibilityVerificationProfile::Unset;
                };
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
                [buffersPointer, finishBufferPresetEdit](
                    bool use16BitAo,
                    bool use16BitGi)
                {
                    ApplyVisibilityBufferPrecisionPreset(
                        *buffersPointer,
                        use16BitAo,
                        use16BitGi);
                    finishBufferPresetEdit();
                };

            ImGui::TextUnformatted("Preset");
            if (DrawPresetResetIcon(
                    "Visibility Buffer Preset",
                    !buffersMatch(buffers, presetBuffers),
                    "Reset every buffer format to the Visibility preset."))
            {
                buffers = presetBuffers;
                finishBufferPresetEdit();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (BeginRoundedCombo("##BufferPreset", bufferPresetLabel))
            {
                DrawDeferredDropdownOption(
                    "Performance Precision",
                    "Performance Precision",
                    ao16 && gi16,
                    [applyBufferPreset]()
                    {
                        applyBufferPreset(true, true);
                    });
                ImGui::SetItemTooltip(
                    "Use the measured bandwidth-saving formats for every "
                    "active AO and GI buffer.");
                DrawDeferredDropdownOption(
                    "Default Precision",
                    "Default Precision",
                    ao32 && gi32,
                    [applyBufferPreset]()
                    {
                        applyBufferPreset(false, false);
                    });
                ImGui::SetItemTooltip(
                    "Use the full-precision formats selected by High and Ultra.");
                DrawDeferredDropdownOption(
                    "Compact AO",
                    "Compact AO",
                    ao16 && gi32,
                    [applyBufferPreset]()
                    {
                        applyBufferPreset(true, false);
                    });
                DrawDeferredDropdownOption(
                    "Compact GI",
                    "Compact GI",
                    ao32 && gi16,
                    [applyBufferPreset]()
                    {
                        applyBufferPreset(false, true);
                    });
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Low, Medium, and High begin at Performance Precision; Ultra "
                "begins at Default Precision. A later buffer edit keeps every "
                "other setting and appends (Custom) to the originating Profile "
                "label.");

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
                    VisibilityScalarBufferPrecision presetPrecision,
                    const char* tooltip)
                {
                    VisibilityScalarBufferPrecision* precisionPointer =
                        &precision;
                    ImGui::TextUnformatted(label);
                    ImGui::PushID(label);
                    if (DrawPresetResetIcon(
                            "Buffer Precision",
                            precision != presetPrecision))
                    {
                        precision = presetPrecision;
                        finishBufferPresetEdit();
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (BeginRoundedCombo(
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
                            const bool selected =
                                precision == candidate;
                            DrawDeferredDropdownOption(
                                scalarPrecisionLabels[index],
                                scalarPrecisionLabels[index],
                                selected,
                                [precisionPointer,
                                    finishBufferPresetEdit,
                                    candidate]()
                                {
                                    *precisionPointer = candidate;
                                    finishBufferPresetEdit();
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip("%s", tooltip);
                    ImGui::PopID();
                };
            const auto drawVectorPrecision =
                [&](const char* label,
                    VisibilityVectorBufferPrecision& precision,
                    VisibilityVectorBufferPrecision presetPrecision,
                    const char* tooltip)
                {
                    VisibilityVectorBufferPrecision* precisionPointer =
                        &precision;
                    ImGui::TextUnformatted(label);
                    ImGui::PushID(label);
                    if (DrawPresetResetIcon(
                            "Buffer Precision",
                            precision != presetPrecision))
                    {
                        precision = presetPrecision;
                        finishBufferPresetEdit();
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (BeginRoundedCombo(
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
                            const bool selected =
                                precision == candidate;
                            DrawDeferredDropdownOption(
                                vectorPrecisionLabels[index],
                                vectorPrecisionLabels[index],
                                selected,
                                [precisionPointer,
                                    finishBufferPresetEdit,
                                    candidate]()
                                {
                                    *precisionPointer = candidate;
                                    finishBufferPresetEdit();
                                });
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
                presetBuffers.rawAmbient,
                "Trace-resolution ambient visibility.");
            drawVectorPrecision(
                "Current Bounce GI",
                buffers.rawIndirect,
                presetBuffers.rawIndirect,
                "Trace-resolution current indirect-diffuse frontier.");
            drawVectorPrecision(
                "Accumulated GI",
                buffers.cumulativeIndirect,
                presetBuffers.cumulativeIndirect,
                "Trace-resolution accumulated multi-bounce GI.");
            drawScalarPrecision(
                "Output AO",
                buffers.finalAmbient,
                presetBuffers.finalAmbient,
                "Full-resolution reconstructed ambient visibility.");
            drawVectorPrecision(
                "Output GI",
                buffers.finalIndirect,
                presetBuffers.finalIndirect,
                "Full-resolution reconstructed indirect diffuse.");
            drawScalarPrecision(
                "Long-Range Depth",
                buffers.depthHierarchy,
                presetBuffers.depthHierarchy,
                "Five-mip depth hierarchy used by long AO traces.");
            ImGui::TextDisabled("Edge Metadata: R8");
            ImGui::SetItemTooltip(
                "The packed-edge encoding is fixed by its shader channel "
                "contract.");

            if (visibilityBenchmarkBusy)
                ImGui::EndDisabled();
            ImGui::PopID();
            EndDrawerBody();
        }
        ImGui::Spacing();

        const bool visibilityStatisticsOpen = DrawCollapsingHeader(
            "Statistics",
            "Inspect one renderer effect at a time and run benchmarks.");
        if (visibilityStatisticsOpen)
        {
            BeginDrawerBody(
                "##StatisticsBody",
                settingsControlWidth);
            ImGui::PushID("StatisticsControls");
            enum class StatisticsEffect : int
            {
                CompleteRenderer,
                Geometry,
                DirectLighting,
                ScreenSpaceVisibility,
                AntiAliasing,
                BendShadows,
                SparseVirtualShadowMaps,
                DiagnosticCascadedShadowMaps,
                MaterialPicking,
                EnvironmentBackground,
                ToneMapping,
                OutputBlit,
                Count
            };
            static constexpr int DefaultStatisticsEffect =
                static_cast<int>(StatisticsEffect::CompleteRenderer);
            static int statisticsEffect =
                DefaultStatisticsEffect;
            statisticsEffect = std::clamp(
                statisticsEffect,
                0,
                static_cast<int>(StatisticsEffect::Count) - 1);
            static constexpr const char* StatisticsEffectLabels[] = {
                "Complete Renderer",
                "Geometry",
                "Direct Lighting",
                "Screen-Space Visibility",
                "Anti-Aliasing",
                "Bend Shadows",
                "Sparse Virtual Shadow Maps",
                "Diagnostic Cascaded Shadow Maps",
                "Material Picking",
                "Environment Background",
                "Tone Mapping",
                "Output Blit"
            };
            ImGui::TextUnformatted("Effect");
            if (DrawPresetResetIcon(
                    "Statistics Effect",
                    statisticsEffect != DefaultStatisticsEffect))
            {
                statisticsEffect = DefaultStatisticsEffect;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (BeginRoundedCombo(
                    "##StatisticsEffect",
                    StatisticsEffectLabels[statisticsEffect]))
            {
                for (int effectIndex = 0;
                    effectIndex < static_cast<int>(StatisticsEffect::Count);
                    ++effectIndex)
                {
                    const bool selected =
                        effectIndex == statisticsEffect;
                    DrawDeferredDropdownOption(
                        StatisticsEffectLabels[effectIndex],
                        StatisticsEffectLabels[effectIndex],
                        selected,
                        [statisticsEffectPointer = &statisticsEffect,
                            effectIndex]()
                        {
                            *statisticsEffectPointer = effectIndex;
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the renderer effect whose GPU cost is shown below.");

            if (statisticsEffect == static_cast<int>(
                    StatisticsEffect::AntiAliasing))
            {
                const AntiAliasingSettings& statisticsAliasing =
                    g_DeferredAliasingUiPresentation.
                        PresentStructuralBody(m_ui.AntiAliasing);
                const ResolvedAntiAliasingSettings statisticsResolved =
                    m_ui.GetResolvedAntiAliasingSettings(
                        statisticsAliasing);
                using AliasingPresentationPhase =
                    DeferredAliasingUiPresentation::Phase;
                const bool statisticsRendererReady =
                    !g_DeferredAliasingUiPresentation.HasPending() ||
                    g_DeferredAliasingUiPresentation.GetPhase() ==
                        AliasingPresentationPhase::AwaitPopupRollUp ||
                    g_DeferredAliasingUiPresentation.GetPhase() ==
                        AliasingPresentationPhase::CollapseCommitted;
                const bool temporalStatisticsActive =
                    statisticsAliasing.enabled &&
                    IsLongTermTemporalPreset(
                        statisticsResolved.implementation) &&
                    m_ui.IsTemporalAntiAliasingAvailable();
                const bool cmaa2StatisticsActive =
                    statisticsAliasing.enabled &&
                    statisticsResolved.subpixelMorphology ==
                        MorphologyApplication::ConservativeMorphological;
                const bool multisampleStatisticsActive =
                    statisticsAliasing.enabled &&
                    statisticsResolved.rasterSampleCount > 1u;
                const bool showAliasingStatistics =
                    g_DeferredAliasingUiPresentation.
                        ShowStructuralBody();
                if (BeginAnimatedToggleRegion(
                        "##StatisticsAliasingMethodBreakdown",
                        showAliasingStatistics))
                {
                    if (ImGui::BeginTable(
                            "##AntiAliasingLiveStatistics",
                            2,
                            ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn(
                            "Anti-Aliasing Metric",
                            ImGuiTableColumnFlags_WidthStretch,
                            3.f);
                        ImGui::TableSetupColumn(
                            "Current",
                            ImGuiTableColumnFlags_WidthStretch,
                            2.f);
                        ImGui::TableHeadersRow();
                        const auto beginAntiAliasingStatisticsRow =
                            [](const char* label)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(label);
                                ImGui::TableSetColumnIndex(1);
                            };

                        bool hasAntiAliasingStatistics = false;
                        const MiniEngineTemporalAATimings* temporalTimings =
                            statisticsRendererReady &&
                                temporalStatisticsActive
                                ? m_app->GetMiniEngineTemporalAATimings()
                                : nullptr;
                        if (temporalStatisticsActive)
                        {
                            constexpr double BytesPerMiB =
                                1024.0 * 1024.0;
                            const auto drawPendingTemporalValue = [&]()
                            {
                                if (!temporalTimings)
                                    ImGui::TextDisabled("--");
                            };

                            beginAntiAliasingStatisticsRow(
                                "Temporal AA Total");
                            if (temporalTimings)
                            {
                                ImGui::Text(
                                    "%.3f ms",
                                    temporalTimings->
                                        CompleteEffectMilliseconds());
                            }
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow("Temporal Blend");
                            if (temporalTimings)
                                ImGui::Text("%.3f ms", temporalTimings->blendMilliseconds);
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow("Temporal Output");
                            if (temporalTimings)
                                ImGui::Text("%.3f ms", temporalTimings->outputMilliseconds);
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow(
                                "Presentation Sharpen");
                            if (temporalTimings)
                                ImGui::Text("%.3f ms", temporalTimings->presentationSharpenMilliseconds);
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow("History Memory");
                            if (temporalTimings)
                            {
                                ImGui::Text(
                                    "%.1f MiB",
                                    double(temporalTimings->historyTextureBytes) /
                                        BytesPerMiB);
                            }
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow("History Status");
                            if (temporalTimings)
                            {
                                ImGui::TextUnformatted(
                                    temporalTimings->historyValid
                                        ? "Valid" : "Invalid");
                            }
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow(
                                "Accumulated Frames");
                            if (temporalTimings)
                                ImGui::Text("%u", temporalTimings->accumulationCount);
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow("History Resets");
                            if (temporalTimings)
                                ImGui::Text("%u", temporalTimings->historyResetCount);
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow(
                                "Shader Permutation");
                            if (temporalTimings)
                            {
                                ImGui::Text(
                                    "%u / %u",
                                    GetMiniEngineTaaBlendPermutationIndex(
                                        statisticsResolved.temporal) + 1u,
                                    MiniEngineTaaBlendPermutationCount);
                            }
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow(
                                "History Color Samples");
                            if (temporalTimings)
                                ImGui::Text("%u", temporalTimings->historyColorSamples);
                            else drawPendingTemporalValue();
                            beginAntiAliasingStatisticsRow(
                                "History Depth Access");
                            if (temporalTimings)
                            {
                                ImGui::Text(
                                    "%u gathers + %u samples",
                                    temporalTimings->historyDepthGathers,
                                    temporalTimings->historyDepthSamples);
                            }
                            else drawPendingTemporalValue();
                            hasAntiAliasingStatistics = true;
                        }

                        const Cmaa2Timings* cmaa2Timings =
                            statisticsRendererReady && cmaa2StatisticsActive
                                ? m_app->GetCmaa2Timings()
                                : nullptr;
                        if (cmaa2StatisticsActive)
                        {
                            const auto drawCmaa2Value = [&](double value)
                            {
                                if (cmaa2Timings)
                                    ImGui::Text("%.3f ms", value);
                                else
                                    ImGui::TextDisabled("--");
                            };
                            beginAntiAliasingStatisticsRow("CMAA2 Total");
                            drawCmaa2Value(cmaa2Timings
                                ? cmaa2Timings->CompleteEffectMilliseconds()
                                : 0.0);
                            beginAntiAliasingStatisticsRow("CMAA2 Edges");
                            drawCmaa2Value(cmaa2Timings
                                ? cmaa2Timings->edgeMilliseconds : 0.0);
                            beginAntiAliasingStatisticsRow(
                                "CMAA2 Candidates");
                            drawCmaa2Value(cmaa2Timings
                                ? cmaa2Timings->candidateMilliseconds : 0.0);
                            beginAntiAliasingStatisticsRow("CMAA2 Apply");
                            drawCmaa2Value(cmaa2Timings
                                ? cmaa2Timings->applyMilliseconds : 0.0);
                            hasAntiAliasingStatistics = true;
                        }

                        if (multisampleStatisticsActive)
                        {
                            beginAntiAliasingStatisticsRow("MSAA Samples");
                            if (statisticsRendererReady)
                            {
                                ImGui::Text(
                                    "%ux",
                                    m_app->GetRasterSampleCount());
                            }
                            else
                            {
                                ImGui::TextDisabled("--");
                            }
                            beginAntiAliasingStatisticsRow(
                                "MSAA Lighting Path");
                            if (statisticsRendererReady)
                            {
                                ImGui::TextUnformatted(
                                    m_ui.UsesDeferredShading()
                                        ? "Deferred Per-Sample"
                                        : "Forward Hardware Resolve");
                            }
                            else
                            {
                                ImGui::TextDisabled("--");
                            }
                            hasAntiAliasingStatistics = true;
                        }
                        if (!hasAntiAliasingStatistics)
                        {
                            beginAntiAliasingStatisticsRow("Status");
                            ImGui::TextDisabled("Inactive");
                        }
                        ImGui::EndTable();
                    }
                    EndAnimatedToggleRegion();
                }
            }
            else if (statisticsEffect == static_cast<int>(
                    StatisticsEffect::BendShadows) ||
                statisticsEffect == static_cast<int>(
                    StatisticsEffect::SparseVirtualShadowMaps) ||
                statisticsEffect == static_cast<int>(
                    StatisticsEffect::DiagnosticCascadedShadowMaps))
            {
                static constexpr const char* BendShadowStatLabels[] = {
                    "Trace Work",
                    "Sampling and Output"
                };
                static constexpr const char* SparseShadowStatLabels[] = {
                    "GPU Timing",
                    "Packet Timing",
                    "Page Residency",
                    "Page Rendering",
                    "Resource Memory",
                    "Fallback Pixels",
                    "Packet Pages",
                    "Scheduled Tile Mask",
                    "Culling Availability",
                    "Culling Counters",
                    "Static Reuse",
                    "Packet Culling",
                    "Draw Submission",
                    "Light and Cache State",
                    "CPU Timing",
                    "Last GPU Work",
                    "GPU Work History"
                };
                static constexpr const char* DiagnosticCsmStatLabels[] = {
                    "GPU Timing",
                    "CPU Timing",
                    "Output and Maps",
                    "Cascade Work",
                    "Caster Work",
                    "Texel Work",
                    "Resource Memory",
                    "Coverage",
                    "Optimization State",
                    "SVSM Comparison",
                    "Normalized Estimate"
                };
                const auto drawStatLines = [](
                    const char* tableId,
                    const auto& labels,
                    const auto& lines,
                    bool available)
                {
                    if (ImGui::BeginTable(
                            tableId,
                            2,
                            ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchProp))
                    {
                        ImGui::TableSetupColumn(
                            "Metric",
                            ImGuiTableColumnFlags_WidthStretch,
                            3.f);
                        ImGui::TableSetupColumn(
                            "Current",
                            ImGuiTableColumnFlags_WidthStretch,
                            1.f);
                        ImGui::TableHeadersRow();
                        if (!available)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted("Status");
                            ImGui::TableSetColumnIndex(1);
                            ImGui::TextDisabled("Inactive");
                        }
                        else
                        {
                            using LabelArray = std::remove_cv_t<
                                std::remove_reference_t<decltype(labels)>>;
                            using LineArray = std::remove_cv_t<
                                std::remove_reference_t<decltype(lines)>>;
                            static_assert(
                                std::extent_v<LabelArray> ==
                                std::tuple_size_v<LineArray>);
                            for (size_t index = 0;
                                index < std::size(lines);
                                ++index)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(labels[index]);
                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextWrapped(
                                    "%s",
                                    lines[index].c_str());
                            }
                        }
                        ImGui::EndTable();
                    }
                };

                if (statisticsEffect == static_cast<int>(
                        StatisticsEffect::BendShadows))
                {
                    drawStatLines(
                        "##BendShadowStatistics",
                        BendShadowStatLabels,
                        m_BendShadowStatLines,
                        m_HasBendShadowStatSnapshot);
                }
                else if (statisticsEffect == static_cast<int>(
                        StatisticsEffect::SparseVirtualShadowMaps))
                {
                    drawStatLines(
                        "##SparseVirtualShadowMapStatistics",
                        SparseShadowStatLabels,
                        m_SparseShadowStatLines,
                        m_HasSparseShadowStatSnapshot);

                    const bool motionTestRunning =
                        m_app->IsSvsmMotionBenchmarkRunning();
                    if (!motionTestRunning)
                    {
                        const bool canStart =
                            m_app->CanStartSvsmMotionBenchmark();
                        if (!canStart)
                            ImGui::BeginDisabled();
                        if (DrawCenteredActionButton(
                                "Run Camera Motion Test",
                                settingsControlWidth))
                        {
                            m_app->StartSvsmMotionBenchmark();
                        }
                        ImGui::SetItemTooltip(
                            "Run the fixed 45-degree Position 1 motion test.");
                        if (DrawCenteredActionButton(
                                "Run Sun Motion Test",
                                settingsControlWidth))
                        {
                            m_app->StartSvsmSunMotionBenchmark();
                        }
                        ImGui::SetItemTooltip(
                            "Run the fixed slow-sun cache-recovery test.");
                        if (!canStart)
                            ImGui::EndDisabled();
                    }
                    if (BeginAnimatedToggleRegion(
                            "##SvsmBenchmarkCancel",
                            motionTestRunning))
                    {
                        if (DrawCenteredActionButton(
                                "Cancel Motion Test",
                                settingsControlWidth))
                        {
                            m_app->AbortSvsmMotionBenchmark(
                                "cancelled from Statistics");
                        }
                        ImGui::TextWrapped(
                            "%s",
                            m_app->GetSvsmMotionBenchmarkStatus().c_str());
                        EndAnimatedToggleRegion();
                    }
                }
                else
                {
                    drawStatLines(
                        "##DiagnosticCascadedShadowMapStatistics",
                        DiagnosticCsmStatLabels,
                        m_DiagnosticCsmStatLines,
                        m_HasDiagnosticCsmStatSnapshot);
                }
            }
            else if (statisticsEffect != static_cast<int>(
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
                        case RendererTimingStage::EnvironmentBackground:
                            return m_ui.ShowEnvironmentBackground;
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
                    const bool environmentStatistics =
                        statisticsEffect == static_cast<int>(
                            StatisticsEffect::EnvironmentBackground);
                    ImGui::TableSetupColumn(
                        environmentStatistics
                            ? "Metric"
                            : "GPU Stage",
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
                            "Environment Background",
                            RendererTimingStage::EnvironmentBackground);
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
                                RendererTimingStage::CompleteFrame,
                                RendererTimingStage::CompleteFrame,
                                RendererTimingStage::CompleteFrame,
                                RendererTimingStage::CompleteFrame,
                                RendererTimingStage::MaterialPicking,
                                RendererTimingStage::EnvironmentBackground,
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
                    if (environmentStatistics)
                    {
                        const float sourceMean =
                            m_app->
                                GetImageBasedLightingSourceAverageLuminance();
                        const float commonMean =
                            sourceMean *
                            m_app->GetImageBasedLightingRadianceScale();
                        const float diffuseMean =
                            IsImageBasedLightingLobeActive(
                                m_ui.EnableDiffuseIbl,
                                m_ui.DiffuseIblStrength)
                                ? commonMean * m_ui.DiffuseIblStrength
                                : 0.f;
                        const float specularMean =
                            IsImageBasedLightingLobeActive(
                                m_ui.EnableSpecularIbl,
                                m_ui.SpecularIblStrength)
                                ? commonMean * m_ui.SpecularIblStrength
                                : 0.f;
                        const auto drawRadianceRow =
                            [](const char* label, float value)
                            {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(label);
                                ImGui::TableSetColumnIndex(1);
                                ImGui::Text("%.4f", value);
                            };
                        drawRadianceRow(
                            "Source Mean Radiance",
                            sourceMean);
                        drawRadianceRow(
                            "Exposed Mean Radiance",
                            commonMean);
                        drawRadianceRow(
                            "Diffuse IBL Mean Radiance",
                            diffuseMean);
                        drawRadianceRow(
                            "Specular IBL Mean Radiance",
                            specularMean);
                        ImGui::SetItemTooltip(
                            "Scene-linear mean radiance after exposure and "
                            "each lobe strength.");
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
                    HasVisibilityExecutionPass(
                        statsPlan.passMask,
                        VisibilityExecutionPass::RuntimeLaterBounceTrace);
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

            if (timings)
            {
                const bool resourceFootprintOpen =
                    BeginAnimatedTreeNode("Resource Footprint");
                ImGui::SetItemTooltip(
                    "Payload and traffic values are logical models, not "
                    "measured memory bandwidth.");
                if (resourceFootprintOpen)
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
                        drawSectionRow("Shared and History Textures");
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
                    EndAnimatedTreeNode();
                }
            }
        }

            ImGui::SeparatorText("Benchmarking");
            static constexpr int DefaultBenchmarkWarmupFrames = 120;
            static constexpr int DefaultBenchmarkMeasuredFrames = 240;
            static int benchmarkWarmupFrames =
                DefaultBenchmarkWarmupFrames;
            static int benchmarkMeasuredFrames =
                DefaultBenchmarkMeasuredFrames;
            static std::string benchmarkUiError;
            const bool motionTestRunning =
                m_app->IsAntiAliasingMotionTestRunning();
            const bool motionTestAvailable =
                m_app->CanStartAntiAliasingMotionTest();

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
            if (motionTestRunning)
            {
                benchmarkBlockedReason =
                    "The current configuration is already running with motion.";
            }
            else if (benchmarkBusy)
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

            DrawSliderInt(
                "Warmup Frames",
                &benchmarkWarmupFrames,
                0,
                600,
                "%d",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip(
                "Frames discarded after history reset before measurement.");
            if (DrawPresetResetIcon(
                    "Benchmark Warmup Frames",
                    benchmarkWarmupFrames !=
                        DefaultBenchmarkWarmupFrames))
            {
                benchmarkWarmupFrames =
                    DefaultBenchmarkWarmupFrames;
            }
            DrawSliderInt(
                "Measured Frames",
                &benchmarkMeasuredFrames,
                1,
                2000,
                "%d",
                ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip(
                "Complete, frame-correlated GPU samples retained per run.");
            if (DrawPresetResetIcon(
                    "Benchmark Measured Frames",
                    benchmarkMeasuredFrames !=
                        DefaultBenchmarkMeasuredFrames))
            {
                benchmarkMeasuredFrames =
                    DefaultBenchmarkMeasuredFrames;
            }

            if (!canRunCurrent)
                ImGui::BeginDisabled();
            if (DrawCenteredActionButton(
                    "Run Current",
                    std::max(1.f, ImGui::GetContentRegionAvail().x)) &&
                canRunCurrent)
            {
                benchmarkUiError.clear();
                if (!m_app->QueueVisibilityBenchmark(
                        uint32_t(std::max(benchmarkWarmupFrames, 0)),
                        uint32_t(std::max(benchmarkMeasuredFrames, 1)),
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

            const bool canRunWithMotion =
                motionTestAvailable && !benchmarkBusy;
            if (!canRunWithMotion)
                ImGui::BeginDisabled();
            if (DrawCenteredActionButton(
                    "Run Current With Motion",
                    std::max(1.f, ImGui::GetContentRegionAvail().x)) &&
                canRunWithMotion)
            {
                m_app->StartAntiAliasingMotionTest();
            }
            ImGui::SetItemTooltip(
                motionTestRunning
                    ? "The exact 40 Hz Benchmark Position 1 warm, turn, hold, "
                        "and return sequence is running."
                    : (m_app->HasSponzaCameraLocations()
                        ? "Run the current AA configuration through the exact "
                            "Benchmark Position 1 test: 180 warm frames, 45 "
                            "degrees right at 0.375 degrees per frame with a "
                            "40 Hz target, a 16-frame hold, and the same return."
                        : "The motion test requires a standardized PBR Sponza "
                            "scene."));
            if (!canRunWithMotion)
                ImGui::EndDisabled();
            const std::string motionTestStatus =
                m_app->GetAntiAliasingMotionTestStatus();
            if (!motionTestStatus.empty())
                ImGui::TextWrapped("%s", motionTestStatus.c_str());

            if (!canRunCurrent && !benchmarkBusy)
            {
                DrawDisabledTextWrapped(benchmarkBlockedReason.c_str());
            }

            const bool testRunning =
                benchmarkBusy || motionTestRunning;
            if (BeginAnimatedToggleRegion(
                    "##BenchmarkCancelControls",
                    testRunning))
            {
                if (DrawCenteredActionButton(
                        "Cancel",
                        std::max(
                            1.f,
                            ImGui::GetContentRegionAvail().x)))
                {
                    if (motionTestRunning)
                        m_app->CancelAntiAliasingMotionTest();
                    else
                        m_app->CancelVisibilityBenchmark();
                }
                ImGui::SetItemTooltip(
                    "Cancel the active test without retaining partial "
                    "measurements.");
                EndAnimatedToggleRegion();
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
            ImGui::PopID();
            EndDrawerBody();
        }
        ImGui::Spacing();

        if (visibilityBenchmarkBusy)
            ImGui::BeginDisabled();


        const bool antiAliasingOpen = DrawCollapsingHeader(
            "Aliasing",
            "Choose a complete anti-aliasing pipeline. Technical controls stay in developer overrides.");
        if (antiAliasingOpen)
        {
            BeginDrawerBody(
                "##AliasingBody",
                settingsControlWidth);
            // CollapsingHeader does not push an ID scope. Keep all Aliasing
            // controls distinct from equally named controls in other Settings
            // drawers (notably Visibility's "Quality" combo).
            ImGui::PushID("AliasingControls");

            // Method and quality changes can rebuild render targets, passes,
            // and first-use pipelines. Their UI-only snapshot drives every
            // Aliasing label and layout predicate while the renderer keeps
            // consuming the committed settings. Commit copies this exact
            // snapshot into renderer state, so the UI target cannot change a
            // second time on the expensive frame.
            AntiAliasingSettings& selectorSettings =
                g_DeferredAliasingUiPresentation.PresentSelectors(
                    m_ui.AntiAliasing);
            const auto commitDeferredAliasingPresentation =
                [this]()
                {
                    if (g_DeferredAliasingUiPresentation.CommitTo(
                            m_ui.AntiAliasing))
                    {
                        m_ui.MiniEngineTaaVisualization =
                            MiniEngineTaaDebugView::Off;
                    }
                };
            const AntiAliasingSettings defaultAliasingSettings;
            const bool temporalAAAvailable =
                m_ui.IsTemporalAntiAliasingAvailable();
            const bool motionTestRunning =
                m_app->IsAntiAliasingMotionTestRunning();

            if (motionTestRunning)
                ImGui::BeginDisabled();

            auto drawEnumOption = [settingsControlWidth](
                const char* label,
                auto& selectedValue,
                const auto& orderedValues,
                auto getLabel,
                const char* tooltip,
                const char* inheritedOrAutoValue = nullptr)
            {
                using ValueType =
                    std::decay_t<decltype(selectedValue)>;
                bool changed = false;
                const bool inherited =
                    inheritedOrAutoValue &&
                    static_cast<uint32_t>(selectedValue) == 0u;
                const std::string preview = inherited
                    ? inheritedOrAutoValue
                    : getLabel(selectedValue);
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (BeginRoundedCombo(label, preview.c_str()))
                {
                    // Show only concrete choices in their fixed
                    // least-expensive-to-most-expensive order. When the
                    // setting is inherited, select its resolved concrete row.
                    for (const ValueType candidate : orderedValues)
                    {
                        const std::string candidateLabel =
                            getLabel(candidate);
                        const bool candidateRepresentsInherited =
                            inherited &&
                            candidateLabel == inheritedOrAutoValue;
                        const bool selected =
                            candidateRepresentsInherited ||
                            candidate == selectedValue;
                        const ValueType committedValue =
                            candidateRepresentsInherited
                            ? static_cast<ValueType>(0u)
                            : candidate;
                        ValueType* selectedValuePointer =
                            &selectedValue;
                        if (DrawDeferredDropdownOption(
                                candidateLabel.c_str(),
                                candidateLabel.c_str(),
                                selected,
                                [selectedValuePointer, committedValue]()
                                {
                                    *selectedValuePointer = committedValue;
                                }))
                        {
                            changed = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SetItemTooltip(tooltip);
                if (DrawNestedDropdownResetIcon(
                        label,
                        !inherited))
                {
                    ValueType* selectedValuePointer = &selectedValue;
                    QueueDeferredControlUiAction(
                        [selectedValuePointer]()
                        {
                            *selectedValuePointer =
                                static_cast<ValueType>(0u);
                        });
                    changed = true;
                }
                return changed;
            };

            auto drawFixedOption = [settingsControlWidth](
                const char* label,
                const char* value,
                const char* tooltip)
            {
                ImGui::SetNextItemWidth(settingsControlWidth);
                ImGui::BeginDisabled();
                if (BeginRoundedCombo(label, value))
                    ImGui::EndCombo();
                ImGui::EndDisabled();
                ImGui::SetItemTooltip(tooltip);
            };

            ImGui::Checkbox("Enabled", &selectorSettings.enabled);
            ImGui::SetItemTooltip(
                "Bypass anti-aliasing while retaining the selected method, quality, and overrides.");
            if (DrawPresetResetIcon(
                    "Aliasing Enabled",
                    !selectorSettings.enabled))
            {
                selectorSettings.enabled = true;
            }

            const bool temporalMethodSelected =
                selectorSettings.method ==
                    AntiAliasingMethod::
                        TemporalSubpixelMorphological;
            const bool temporalSelectionUnavailableBeforeSelection =
                temporalMethodSelected &&
                !temporalAAAvailable;
            std::string methodPreview =
                GetAntiAliasingMethodLabel(selectorSettings.method);
            if (temporalSelectionUnavailableBeforeSelection)
                methodPreview += " (Mutex)";
            ImGui::SetNextItemWidth(settingsControlWidth);
            const bool methodComboOpen = BeginRoundedCombo(
                "Method",
                methodPreview.c_str());
            if (methodComboOpen)
            {
                static constexpr std::array<
                    AntiAliasingMethod, 3> methodOrder = {
                        AntiAliasingMethod::IntelCmaa2,
                        AntiAliasingMethod::
                            TemporalSubpixelMorphological,
                        AntiAliasingMethod::Msaa
                    };
                for (const AntiAliasingMethod candidate : methodOrder)
                {
                    const uint32_t index =
                        static_cast<uint32_t>(candidate);
                    const bool candidateMutex =
                        (candidate ==
                            AntiAliasingMethod::
                                    TemporalSubpixelMorphological) &&
                        !temporalAAAvailable;
                    ImGui::PushID(static_cast<int>(index));

                    const bool selected =
                        candidate == selectorSettings.method;
                    std::string candidatePreview =
                        GetAntiAliasingMethodLabel(candidate);
                    if (candidateMutex)
                        candidatePreview += " (Mutex)";
                    std::string candidateLabel = candidatePreview;
                    candidateLabel += "##MethodCandidate";
                    if (DrawDeferredDropdownOption(
                        candidateLabel.c_str(),
                        candidatePreview.c_str(),
                        selected,
                        commitDeferredAliasingPresentation))
                    {
                        g_DeferredAliasingUiPresentation.Stage(
                            m_ui.AntiAliasing,
                            true,
                            ImGui::GetFrameCount(),
                            [candidate](AntiAliasingSettings& staged)
                            {
                                NormalizeRedundantAntiAliasingOverrides(
                                    staged);
                                staged.method = candidate;
                                staged.quality =
                                    SanitizeAntiAliasingQuality(
                                        staged.method,
                                        staged.quality);
                                NormalizeRedundantAntiAliasingOverrides(
                                    staged);
                            });
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose temporal reconstructive, conservative morphological, "
                "or multisample reference anti-aliasing. Temporal AA requires "
                "deferred UVSR PBR motion and depth.");
            if (DrawPresetResetIcon(
                    "Aliasing Method",
                    selectorSettings.method !=
                        defaultAliasingSettings.method))
            {
                const AntiAliasingMethod defaultMethod =
                    defaultAliasingSettings.method;
                QueueDeferredControlUiAction(
                    commitDeferredAliasingPresentation);
                g_DeferredAliasingUiPresentation.Stage(
                    m_ui.AntiAliasing,
                    true,
                    ImGui::GetFrameCount(),
                    [defaultMethod](AntiAliasingSettings& staged)
                    {
                        NormalizeRedundantAntiAliasingOverrides(staged);
                        staged.method = defaultMethod;
                        staged.quality =
                            SanitizeAntiAliasingQuality(
                                staged.method,
                                staged.quality);
                        NormalizeRedundantAntiAliasingOverrides(staged);
                    });
            }

            std::string qualityPreview =
                GetAntiAliasingQualityMenuLabel(
                    selectorSettings.method,
                    selectorSettings.quality);
            if (IsAntiAliasingPresetCustom(selectorSettings))
                qualityPreview += " (Custom)";
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "Quality",
                    qualityPreview.c_str()))
            {
                static constexpr std::array<
                    AntiAliasingQuality, 4> qualityOrder = {
                        AntiAliasingQuality::Low,
                        AntiAliasingQuality::Medium,
                        AntiAliasingQuality::High,
                        AntiAliasingQuality::Ultra
                    };
                for (const AntiAliasingQuality candidate : qualityOrder)
                {
                    const uint32_t index =
                        static_cast<uint32_t>(candidate);
                    const bool candidateUnavailable =
                        !IsAntiAliasingQualitySupported(
                            selectorSettings.method,
                            candidate);
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::BeginDisabled(candidateUnavailable);
                    const bool selected =
                        candidate == selectorSettings.quality;
                    std::string candidatePreview =
                        GetAntiAliasingQualityMenuLabel(
                            selectorSettings.method,
                            candidate);
                    if (candidateUnavailable)
                        candidatePreview += " (Mutex)";
                    std::string candidateLabel = candidatePreview;
                    candidateLabel += "##QualityCandidate";
                    if (DrawDeferredDropdownOption(
                        candidateLabel.c_str(),
                        candidatePreview.c_str(),
                        selected,
                        commitDeferredAliasingPresentation))
                    {
                        g_DeferredAliasingUiPresentation.Stage(
                            m_ui.AntiAliasing,
                            false,
                            ImGui::GetFrameCount(),
                            [candidate](AntiAliasingSettings& staged)
                            {
                                NormalizeRedundantAntiAliasingOverrides(
                                    staged);
                                staged.quality = candidate;
                                NormalizeRedundantAntiAliasingOverrides(
                                    staged);
                            });
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Temporal Reconstructive and Conservative Morphological expose "
                "Low through Ultra. Multisample Reference maps Low, Medium, "
                "High, and Ultra to 2x, 4x, 8x, and 16x.");
            if (DrawPresetResetIcon(
                    "Aliasing Quality",
                    selectorSettings.quality !=
                        defaultAliasingSettings.quality))
            {
                const AntiAliasingQuality defaultQuality =
                    defaultAliasingSettings.quality;
                QueueDeferredControlUiAction(
                    commitDeferredAliasingPresentation);
                g_DeferredAliasingUiPresentation.Stage(
                    m_ui.AntiAliasing,
                    false,
                    ImGui::GetFrameCount(),
                    [defaultQuality](AntiAliasingSettings& staged)
                    {
                        NormalizeRedundantAntiAliasingOverrides(staged);
                        staged.quality = defaultQuality;
                        NormalizeRedundantAntiAliasingOverrides(staged);
                    });
            }

            AntiAliasingSettings& settings =
                g_DeferredAliasingUiPresentation.
                    PresentStructuralBody(m_ui.AntiAliasing);
            const bool showAliasingMethodDependentBody =
                g_DeferredAliasingUiPresentation.
                    ShowStructuralBody();
            if (BeginAnimatedToggleRegion(
                    "##AliasingMethodDependentControls",
                    showAliasingMethodDependentBody))
            {
            const AntiAliasingPreset selectedImplementation =
                GetAntiAliasingImplementation(
                    settings.method,
                    SanitizeAntiAliasingQuality(
                        settings.method,
                        settings.quality));
            const bool longTermTemporalControlsAvailable =
                IsLongTermTemporalPreset(selectedImplementation);
            const bool temporalSelectionUnavailable =
                settings.method ==
                    AntiAliasingMethod::
                        TemporalSubpixelMorphological;
            const bool effectiveTemporalSelectionUnavailable =
                temporalSelectionUnavailable &&
                !temporalAAAvailable;
            if (effectiveTemporalSelectionUnavailable)
            {
                DrawDisabledTextWrapped(
                    m_ui.HasMiniEngineTaaVisibilityConflict()
                        ? "Temporal anti-aliasing is paused until visibility\n"
                            "Temporal Reconstruction is disabled."
                        : "Temporal anti-aliasing requires deferred UVSR PBR\n"
                            "motion and depth.");
            }

            MiniEngineTaaAlgorithmOverrides& historyOverrides =
                settings.algorithmOverrides;
            AntiAliasingSettings historyPresetSettings = settings;
            // The enabled toggle controls execution, not the retained preset
            // presentation. Resolve inherited history from the active method
            // while its controls collapse so 6 / 100% never flashes to 0.
            historyPresetSettings.enabled = true;
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
                historyFramesLabel +=
                    "##ResolvedHistoryFrames";
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawSliderInt(
                        historyFramesLabel.c_str(),
                        &historyFrames,
                        1,
                        32,
                        "%d"))
                {
                    historyOverrides.historyFrames =
                        historyFrames ==
                            static_cast<int>(
                                historyPreset.historyFrames)
                            ? -1
                            : historyFrames;
                }
                ImGui::SetItemTooltip(
                    "Cap the logical number of prior frames that can influence temporal AA. The physical allocation remains two ping-pong textures.");
                if (DrawPresetResetIcon(
                        "Aliasing History Frames",
                        historyOverrides.historyFrames >= 0))
                {
                    historyOverrides.historyFrames = -1;
                }
                float historyStrength =
                    100.f *
                    (historyOverrides.historyStrength >= 0.f
                        ? historyOverrides.historyStrength
                        : historyPreset.historyStrength);
                std::string historyStrengthLabel =
                    "History Strength";
                historyStrengthLabel +=
                    "##ResolvedHistoryStrength";
                ImGui::SetNextItemWidth(settingsControlWidth);
                if (DrawSliderFloat(
                        historyStrengthLabel.c_str(),
                        &historyStrength,
                        0.f,
                        200.f,
                        "%.0f%%"))
                {
                    historyOverrides.historyStrength =
                        std::clamp(
                            historyStrength * 0.01f,
                            0.f,
                            2.f);
                    if (std::abs(
                            historyOverrides.historyStrength -
                            historyPreset.historyStrength) < 1e-4f)
                    {
                        historyOverrides.historyStrength = -1.f;
                    }
                }
                ImGui::SetItemTooltip(
                    "%s",
                    "Scale accepted history after motion, bounds, reverse-Z "
                    "depth, disocclusion, and rectification gates. Values "
                    "above 100% strengthen only accepted "
                    "history, remain capped by the selected frame horizon, "
                    "and cannot revive a rejected sample.");
                if (DrawPresetResetIcon(
                        "Aliasing History Strength",
                        historyOverrides.historyStrength >= 0.f))
                {
                    historyOverrides.historyStrength = -1.f;
                }
            }

            const auto drawDejitterControl = [&]()
                {
                    MiniEngineTaaAlgorithmOverrides& algorithmOverrides =
                        settings.algorithmOverrides;
                    AntiAliasingSettings dejitterPresetSettings =
                        settings;
                    dejitterPresetSettings.algorithmOverrides =
                        MiniEngineTaaAlgorithmOverrides{};
                    const bool presetDejitter =
                        m_ui.GetResolvedAntiAliasingSettings(
                                dejitterPresetSettings)
                                .temporal.currentReconstruction ==
                            MiniEngineTaaCurrentReconstruction::DeJittered;
                    const bool inherited =
                        algorithmOverrides.currentReconstruction ==
                        MiniEngineTaaCurrentReconstructionOverride::
                            FromPreset;
                    bool dejitter = inherited
                        ? presetDejitter
                        : algorithmOverrides.currentReconstruction ==
                            MiniEngineTaaCurrentReconstructionOverride::
                                DeJittered;
                    if (ImGui::Checkbox(
                            "Dejitter##AliasingDejitter",
                            &dejitter))
                    {
                        if (dejitter == presetDejitter)
                        {
                            algorithmOverrides.currentReconstruction =
                                MiniEngineTaaCurrentReconstructionOverride::
                                    FromPreset;
                        }
                        else
                        {
                            algorithmOverrides.currentReconstruction =
                                dejitter
                                ? MiniEngineTaaCurrentReconstructionOverride::
                                    DeJittered
                                : MiniEngineTaaCurrentReconstructionOverride::
                                    Direct;
                        }
                    }
                    ImGui::SetItemTooltip(
                        "Reconstruct the current frame at the unjittered "
                        "pixel center. Ultra enables this by default; Low, "
                        "Medium, and High leave it off.");
                    if (DrawPresetResetIcon(
                            "Aliasing Dejitter",
                            !inherited))
                    {
                        algorithmOverrides.currentReconstruction =
                            MiniEngineTaaCurrentReconstructionOverride::
                                FromPreset;
                    }
                };

            const auto drawRectificationControl = [&]()
                {
                    MiniEngineTaaAlgorithmOverrides& algorithmOverrides =
                        settings.algorithmOverrides;
                    AntiAliasingSettings rectificationPresetSettings =
                        settings;
                    rectificationPresetSettings.algorithmOverrides =
                        MiniEngineTaaAlgorithmOverrides{};
                    const ResolvedAntiAliasingSettings resolvedForLabels =
                        m_ui.GetResolvedAntiAliasingSettings(
                            rectificationPresetSettings);
                    static constexpr std::array<
                        MiniEngineTaaRectificationOverride, 2>
                        rectificationOrder = {
                            MiniEngineTaaRectificationOverride::PairRgb,
                            MiniEngineTaaRectificationOverride::VarianceYCoCg
                        };
                    drawEnumOption(
                        "Rectification",
                        algorithmOverrides.rectification,
                        rectificationOrder,
                        GetMiniEngineTaaRectificationOverrideLabel,
                        "Choose pair-clamp RGB or variance-aware YCoCg history "
                        "rectification.",
                        GetMiniEngineTaaRectificationLabel(
                            resolvedForLabels.temporal.rectification));
                };

            if (longTermTemporalControlsAvailable)
            {
                drawDejitterControl();
                ImGui::Checkbox(
                    "Sharpness###Sharpness",
                    &m_ui.MiniEngineTaaSharpenEnabled);
                ImGui::SetItemTooltip(
                    "Enable or bypass temporal output sharpening while retaining the selected strength.");
                if (DrawPresetResetIcon(
                        "Aliasing Sharpness Enabled",
                        m_ui.MiniEngineTaaSharpenEnabled))
                {
                    m_ui.MiniEngineTaaSharpenEnabled = false;
                }
                if (BeginAnimatedToggleRegion(
                        "##SharpnessStrengthControls",
                        m_ui.MiniEngineTaaSharpenEnabled))
                {
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    DrawSliderFloat(
                        "Sharpness Strength",
                        &m_ui.MiniEngineTaaSharpness,
                        MiniEngineTaaMinimumSharpness,
                        MiniEngineTaaMaximumSharpness,
                        "%.2f");
                    ImGui::SetItemTooltip(
                        "Set temporal output sharpness. The stored value is "
                        "retained while sharpening is disabled.");
                    if (DrawPresetResetIcon(
                            "Aliasing Sharpness Strength",
                            std::abs(
                                m_ui.MiniEngineTaaSharpness -
                                MiniEngineTaaDefaultSharpness) > 1e-4f))
                    {
                        m_ui.MiniEngineTaaSharpness =
                            MiniEngineTaaDefaultSharpness;
                    }
                    EndAnimatedToggleRegion();
                }
            }

            const bool algorithmConfigurationAvailable =
                longTermTemporalControlsAvailable ||
                selectedImplementation == AntiAliasingPreset::IntelCmaa2 ||
                selectedImplementation == AntiAliasingPreset::Msaa2x ||
                selectedImplementation == AntiAliasingPreset::Msaa4x ||
                selectedImplementation == AntiAliasingPreset::Msaa8x ||
                selectedImplementation == AntiAliasingPreset::Msaa16x;
            if (algorithmConfigurationAvailable &&
                BeginAnimatedTreeNode(
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
                const ResolvedAntiAliasingSettings resolvedCurrent =
                    m_ui.GetResolvedAntiAliasingSettings(settings);

                const auto drawMorphologyOption = [&]()
                {
                    if (selectedImplementation ==
                        AntiAliasingPreset::IntelCmaa2)
                    {
                        const std::string fixedLabel =
                            std::string("Conservative ") +
                            GetAntiAliasingQualityLabel(
                                resolvedForLabels.morphologyQuality);
                        drawFixedOption(
                            "Subpixel Morphology##Developer",
                            fixedLabel.c_str(),
                            "The Conservative Morphological method uses the "
                            "strength selected by Quality.");
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
                        const bool morphologyInherited =
                            overrides.subpixelMorphology ==
                                MorphologyApplicationOverride::FromPreset &&
                            overrides.morphologyQuality < 0;
                        const bool morphologyOff =
                            resolvedCurrent.subpixelMorphology ==
                                MorphologyApplication::Off;
                        const AntiAliasingQuality morphologyQuality =
                            resolvedCurrent.morphologyQuality;
                        const std::string morphologyPreview =
                            morphologyOff
                            ? "Off"
                            : std::string("Conservative ") +
                                GetAntiAliasingQualityLabel(
                                    morphologyQuality);
                        ImGui::SetNextItemWidth(settingsControlWidth);
                        const bool morphologyComboOpen =
                            BeginRoundedCombo(
                                "Subpixel Morphology##Developer",
                                morphologyPreview.c_str());
                        if (morphologyComboOpen)
                        {
                            constexpr const char* morphologyLabels[] = {
                                "Conservative Low",
                                "Conservative Medium",
                                "Conservative High",
                                "Conservative Ultra"
                            };
                            static constexpr std::array<
                                AntiAliasingQuality, 4>
                                morphologyQualityOrder = {
                                    AntiAliasingQuality::Low,
                                    AntiAliasingQuality::Medium,
                                    AntiAliasingQuality::High,
                                    AntiAliasingQuality::Ultra
                                };
                            MiniEngineTaaAlgorithmOverrides*
                                overridesPointer = &overrides;
                            const bool offRepresentsInherited =
                                morphologyInherited && morphologyOff;
                            DrawDeferredDropdownOption(
                                "Off##MorphologyCandidate",
                                "Off",
                                morphologyOff,
                                [this,
                                    overridesPointer,
                                    offRepresentsInherited]()
                                {
                                    overridesPointer->subpixelMorphology =
                                        offRepresentsInherited
                                        ? MorphologyApplicationOverride::
                                            FromPreset
                                        : MorphologyApplicationOverride::Off;
                                    overridesPointer->morphologyQuality = -1;
                                    m_ui.MiniEngineTaaVisualization =
                                        MiniEngineTaaDebugView::Off;
                                });
                            for (const AntiAliasingQuality candidateQuality :
                                morphologyQualityOrder)
                            {
                                const uint32_t index =
                                    static_cast<uint32_t>(
                                        candidateQuality);
                                const bool selected =
                                    !morphologyOff &&
                                    morphologyQuality == candidateQuality;
                                const bool candidateRepresentsInherited =
                                    morphologyInherited && selected;
                                ImGui::PushID(static_cast<int>(index));
                                std::string candidateLabel =
                                    morphologyLabels[index];
                                candidateLabel += "##MorphologyCandidate";
                                MiniEngineTaaAlgorithmOverrides*
                                    overridesPointer = &overrides;
                                DrawDeferredDropdownOption(
                                    candidateLabel.c_str(),
                                    morphologyLabels[index],
                                    selected,
                                    [this,
                                        overridesPointer,
                                        candidateQuality,
                                        candidateRepresentsInherited]()
                                    {
                                        overridesPointer->
                                            subpixelMorphology =
                                                candidateRepresentsInherited
                                                ? MorphologyApplicationOverride::
                                                    FromPreset
                                                : MorphologyApplicationOverride::
                                                    ConservativeMorphological;
                                        overridesPointer->morphologyQuality =
                                            candidateRepresentsInherited
                                            ? -1
                                            : int32_t(candidateQuality);
                                        m_ui.MiniEngineTaaVisualization =
                                            MiniEngineTaaDebugView::Off;
                                    });
                                if (selected)
                                    ImGui::SetItemDefaultFocus();
                                ImGui::PopID();
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SetItemTooltip(
                            "Choose the CMAA2 strength applied after the "
                            "resolved Temporal or Multisample image.");
                        if (DrawNestedDropdownResetIcon(
                                "Aliasing Subpixel Morphology",
                                overrides.subpixelMorphology !=
                                    MorphologyApplicationOverride::
                                        FromPreset ||
                                    overrides.morphologyQuality >= 0))
                        {
                            MiniEngineTaaAlgorithmOverrides*
                                overridesPointer = &overrides;
                            QueueDeferredControlUiAction(
                                [this, overridesPointer]()
                                {
                                    overridesPointer->subpixelMorphology =
                                        MorphologyApplicationOverride::
                                            FromPreset;
                                    overridesPointer->morphologyQuality = -1;
                                    m_ui.MiniEngineTaaVisualization =
                                        MiniEngineTaaDebugView::Off;
                                });
                        }
                    }
                };

                static constexpr std::array<
                    MiniEngineTaaMotionSourceOverride, 3>
                    motionSourceOrder = {
                        MiniEngineTaaMotionSourceOverride::Center,
                        MiniEngineTaaMotionSourceOverride::ClosestCross,
                        MiniEngineTaaMotionSourceOverride::
                            CenterFirstEdgeDilation
                    };
                static constexpr std::array<
                    MiniEngineTaaHistoryFilterOverride, 4>
                    reconstructionOrder = {
                        MiniEngineTaaHistoryFilterOverride::Bilinear,
                        MiniEngineTaaHistoryFilterOverride::
                            OneSampleBicubic,
                        MiniEngineTaaHistoryFilterOverride::
                            FiveTapCatmullRom,
                        MiniEngineTaaHistoryFilterOverride::
                            NineTapCatmullRom
                    };
#if UVSR_AA_DEVELOPER_OVERRIDES
                static constexpr std::array<
                    MiniEngineTaaSampleResurrectionOverride, 3>
                    sampleResurrectionOrder = {
                        MiniEngineTaaSampleResurrectionOverride::Off,
                        MiniEngineTaaSampleResurrectionOverride::
                            OneOlderFrame,
                        MiniEngineTaaSampleResurrectionOverride::
                            TwoOlderFrames
                    };
#endif

                if (longTermTemporalControlsAvailable)
                {
                    drawMorphologyOption();
                    drawEnumOption(
                        "Motion Source",
                        overrides.motionSource,
                        motionSourceOrder,
                        GetMiniEngineTaaMotionSourceOverrideLabel,
                        "Override motion ownership. Changing it resets all temporal state.",
                        GetMiniEngineTaaMotionSourceLabel(
                            resolvedForLabels.temporal.motionSource));
                    drawEnumOption(
                        "Reconstruction",
                        overrides.historyFilter,
                        reconstructionOrder,
                        GetMiniEngineTaaHistoryFilterOverrideLabel,
                        "Choose the real history reconstruction filter, "
                        "including the full nine-tap Catmull-Rom option.",
                        GetMiniEngineTaaHistoryFilterLabel(
                            resolvedForLabels.temporal.historyFilter));
                    drawRectificationControl();
                }
                else
                {
                    drawMorphologyOption();
                }

                // Resurrection remains last because it is a recovery policy
                // applied after the primary spatial and temporal choices.
#if UVSR_AA_DEVELOPER_OVERRIDES
                if (longTermTemporalControlsAvailable)
                {
                    drawEnumOption(
                        "Sample Resurrection",
                        overrides.sampleResurrection,
                        sampleResurrectionOrder,
                        GetMiniEngineTaaSampleResurrectionOverrideLabel,
                        "Reuse one or two older validated samples when immediate history is unreliable.",
                        GetMiniEngineTaaSampleResurrectionLabel(
                            resolvedForLabels.sampleResurrection));
                }
#endif

                EndAnimatedTreeNode();
            }
            EndAnimatedToggleRegion();
            }
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
                                m_app->ResetImageBasedLightingHistory(true);
                            }
                        });
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Choose the imported radiance source used by IBL and the "
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
                        m_app->ResetImageBasedLightingHistory(true);
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
                    "%+.2f EV"))
            {
                m_app->ResetImageBasedLightingHistory(true);
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
                m_app->ResetImageBasedLightingHistory(true);
            }

            if (ImGui::Checkbox(
                    "Diffuse IBL",
                    &m_ui.EnableDiffuseIbl))
            {
                m_app->ResetImageBasedLightingHistory(true);
            }
            ImGui::SetItemTooltip(
                "Use the selected environment for diffuse lighting.");
            if (DrawPresetResetIcon(
                    "Diffuse IBL Enabled",
                    !m_ui.EnableDiffuseIbl))
            {
                m_ui.EnableDiffuseIbl = true;
                m_app->ResetImageBasedLightingHistory(true);
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
                    m_app->ResetImageBasedLightingHistory(true);
                }
                ImGui::SetItemTooltip(
                    "Scale diffuse environment lighting after exposure.");
                if (DrawPresetResetIcon(
                        "Diffuse IBL Strength",
                        m_ui.DiffuseIblStrength != 1.f))
                {
                    m_ui.DiffuseIblStrength = 1.f;
                    m_app->ResetImageBasedLightingHistory(true);
                }
                EndAnimatedToggleRegion();
            }

            if (ImGui::Checkbox(
                    "Specular IBL",
                    &m_ui.EnableSpecularIbl))
            {
                m_app->ResetImageBasedLightingHistory(false);
            }
            ImGui::SetItemTooltip(
                "Use the selected environment for specular reflections.");
            if (DrawPresetResetIcon(
                    "Specular IBL Enabled",
                    !m_ui.EnableSpecularIbl))
            {
                m_ui.EnableSpecularIbl = true;
                m_app->ResetImageBasedLightingHistory(false);
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
                    m_app->ResetImageBasedLightingHistory(false);
                }
                ImGui::SetItemTooltip(
                    "Scale specular environment lighting after exposure.");
                if (DrawPresetResetIcon(
                        "Specular IBL Strength",
                        m_ui.SpecularIblStrength != 1.f))
                {
                    m_ui.SpecularIblStrength = 1.f;
                    m_app->ResetImageBasedLightingHistory(false);
                }
                EndAnimatedToggleRegion();
            }

            if (ImGui::Checkbox(
                    "Show Environment Background",
                    &m_ui.ShowEnvironmentBackground))
            {
                m_app->ResetImageBasedLightingHistory(false);
            }
            ImGui::SetItemTooltip(
                "Show the same environment used for lighting.");
            if (DrawPresetResetIcon(
                    "Environment Background Enabled",
                    !m_ui.ShowEnvironmentBackground))
            {
                m_ui.ShowEnvironmentBackground = true;
                m_app->ResetImageBasedLightingHistory(false);
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
            constexpr uint32_t DefaultLightingDebugView =
                uint32_t(PbrLightingDebugView::None);
            const uint32_t lightingDebugIndex = std::min(
                uint32_t(m_ui.LightingDebugView),
                uint32_t(std::size(PbrLightingDebugLabels) - 1u));
            ImGui::TextUnformatted("PBR Lighting Debug");
            ImGui::SetNextItemWidth(settingsControlWidth);
            if (BeginRoundedCombo(
                    "##PbrLightingDebug",
                    PbrLightingDebugLabels[lightingDebugIndex]))
            {
                for (uint32_t index = 0u;
                    index < std::size(PbrLightingDebugLabels);
                    ++index)
                {
                    const PbrLightingDebugView view =
                        PbrLightingDebugView(index);
                    const bool selected =
                        view == m_ui.LightingDebugView;
                    DrawDeferredDropdownOption(
                        PbrLightingDebugLabels[index],
                        PbrLightingDebugLabels[index],
                        selected,
                        [this, view]()
                        {
                            m_ui.LightingDebugView = view;
                            m_app->ResetImageBasedLightingHistory(true);
                        });
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip(
                "Inspect decoded normals, diffuse and specular IBL stages, the "
                "split-sum LUT response, occlusion, and roughness-selected mip.");
            if (DrawPresetResetIcon(
                    "PBR Lighting Debug",
                    lightingDebugIndex != DefaultLightingDebugView))
            {
                QueueDeferredControlUiAction(
                    [this]()
                    {
                        m_ui.LightingDebugView =
                            PbrLightingDebugView::None;
                        m_app->ResetImageBasedLightingHistory(true);
                    });
            }
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
                        m_SelectedLight != lights.front(),
                        "Select the scene's first light."))
                {
                    m_SelectedLight = lights.front();
                }

                if (m_SelectedLight)
                {
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
                    static std::unordered_map<
                        std::string,
                        LightDefaultState> lightDefaults;
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
                        lightDefaults.try_emplace(
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
                        DrawSliderFloat(
                            "Angular Size",
                            &light.angularSize,
                            0.1f,
                            20.f);
                        ImGui::SetItemTooltip(
                            "Set the directional light's angular size.");
                        if (DrawPresetResetIcon(
                                "Light Angular Size",
                                floatChanged(
                                    light.angularSize,
                                    defaultLight.angularSize)))
                        {
                            light.angularSize =
                                defaultLight.angularSize;
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

            const bool directionalVisibilityAvailable =
                m_ui.EnablePbr &&
                m_ui.UsesDeferredShading() &&
                m_app->HasPrimaryDirectionalLight();

            if (BeginAnimatedTreeNode(
                    "Bend Screen-Space Shadows##Lights"))
            {
                BendScreenSpaceShadowSettings& shadows =
                    m_ui.BendScreenSpaceShadows;
                const BendScreenSpaceShadowSettings bendDefaults{};
                static constexpr auto isSameBendConfiguration =
                    [](const BendScreenSpaceShadowSettings& left,
                        const BendScreenSpaceShadowSettings& right)
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
                            left.useEarlyOut == right.useEarlyOut &&
                            left.debugView == right.debugView;
                    };
                static constexpr auto reconcileBendPreset =
                    [](BendScreenSpaceShadowSettings& settings)
                    {
                        constexpr BendShadowPreset Presets[] = {
                            BendShadowPreset::BendExact,
                            BendShadowPreset::Long,
                            BendShadowPreset::MaximumValidation
                        };
                        for (const BendShadowPreset preset : Presets)
                        {
                            BendScreenSpaceShadowSettings presetSettings =
                                settings;
                            ApplyBendShadowPreset(
                                presetSettings,
                                preset);
                            if (isSameBendConfiguration(
                                    settings,
                                    presetSettings))
                            {
                                settings.preset = preset;
                                return;
                            }
                        }
                        settings.preset = BendShadowPreset::Custom;
                    };
                if (!directionalVisibilityAvailable)
                    ImGui::BeginDisabled();
                ImGui::Checkbox("Enabled##BendShadows", &shadows.enabled);
                ImGui::SetItemTooltip(
                    "Trace the existing depth buffer for the primary "
                    "directional light.");
                if (DrawPresetResetIcon(
                        "Bend Shadows Enabled",
                        shadows.enabled))
                {
                    shadows.enabled = false;
                }
                if (BeginAnimatedToggleRegion(
                        "##BendShadowControls",
                        shadows.enabled))
                {
                    bool bendCustomChanged = false;
                    bool bendResetApplied = false;
                    static constexpr const char* PresetLabels[] = {
                        "Bend Exact",
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
                            "Profile##BendShadows",
                            PresetLabels[presetIndex]))
                    {
                        for (int index = 0;
                            index < int(std::size(PresetLabels));
                            ++index)
                        {
                            const BendShadowPreset preset =
                                BendShadowPreset(index);
                            DrawDeferredDropdownOption(
                                PresetLabels[index],
                                PresetLabels[index],
                                shadows.preset == preset,
                                [settings = &shadows, preset]()
                                {
                                    ApplyBendShadowPreset(
                                        *settings,
                                        preset);
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Trade trace reach and cost: 60 pixels for Bend "
                        "Exact, 240 for Long, or 960 for Maximum Validation.");
                    BendScreenSpaceShadowSettings bendExactSettings =
                        shadows;
                    ApplyBendShadowPreset(
                        bendExactSettings,
                        BendShadowPreset::BendExact);
                    if (DrawNestedDropdownResetIcon(
                            "Bend Shadow Profile",
                            shadows.preset !=
                                    BendShadowPreset::BendExact ||
                                !isSameBendConfiguration(
                                    shadows,
                                    bendExactSettings),
                            "Reset every Bend shadow setting to Bend Exact."))
                    {
                        QueueDeferredControlUiAction(
                            [settings = &shadows]()
                            {
                                ApplyBendShadowPreset(
                                    *settings,
                                    BendShadowPreset::BendExact);
                            });
                    }

                    static constexpr const char* LengthLabels[] = {
                        "60 px", "120 px", "240 px", "480 px", "960 px"
                    };
                    int lengthIndex = FindBendShadowCompiledValue(
                        BendShadowSampleCounts,
                        GetBendShadowSampleCount(shadows.length));
                    lengthIndex = std::clamp(
                        lengthIndex,
                        0,
                        int(std::size(LengthLabels)) - 1);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Length##BendShadows",
                            LengthLabels[lengthIndex]))
                    {
                        for (int index = 0;
                            index < int(std::size(LengthLabels));
                            ++index)
                        {
                            const BendShadowLength length =
                                BendShadowLength(
                                    BendShadowSampleCounts[size_t(index)]);
                            DrawDeferredDropdownOption(
                                LengthLabels[index],
                                LengthLabels[index],
                                shadows.length == length,
                                [settings = &shadows, length]()
                                {
                                    settings->length = length;
                                    settings->preset =
                                        BendShadowPreset::Custom;
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Select a precompiled SAMPLE_COUNT shadow length.");
                    if (DrawNestedDropdownResetIcon(
                            "Bend Shadow Length",
                            shadows.length != bendDefaults.length))
                    {
                        const BendShadowLength defaultLength =
                            bendDefaults.length;
                        QueueDeferredControlUiAction(
                            [settings = &shadows, defaultLength]()
                            {
                                settings->length = defaultLength;
                                reconcileBendPreset(*settings);
                            });
                    }

                    bendCustomChanged |= DrawSliderFloat(
                        "Surface Thickness##BendShadows",
                        &shadows.surfaceThickness,
                        0.f,
                        0.05f,
                        "%.4f");
                    ImGui::SetItemTooltip(
                        "Set Bend's nonlinear-depth occluder thickness.");
                    if (DrawPresetResetIcon(
                            "Bend Shadow Surface Thickness",
                            shadows.surfaceThickness !=
                                bendDefaults.surfaceThickness))
                    {
                        shadows.surfaceThickness =
                            bendDefaults.surfaceThickness;
                        bendResetApplied = true;
                    }
                    bendCustomChanged |= DrawSliderFloat(
                        "Bilinear Threshold##BendShadows",
                        &shadows.bilinearThreshold,
                        0.f,
                        0.1f,
                        "%.3f");
                    ImGui::SetItemTooltip(
                        "Set the relative depth discontinuity that disables "
                        "interpolation.");
                    if (DrawPresetResetIcon(
                            "Bend Shadow Bilinear Threshold",
                            shadows.bilinearThreshold !=
                                bendDefaults.bilinearThreshold))
                    {
                        shadows.bilinearThreshold =
                            bendDefaults.bilinearThreshold;
                        bendResetApplied = true;
                    }
                    bendCustomChanged |= DrawSliderFloat(
                        "Shadow Contrast##BendShadows",
                        &shadows.shadowContrast,
                        1.f,
                        16.f,
                        "%.1f");
                    ImGui::SetItemTooltip(
                        "Set Bend's visibility transition contrast.");
                    if (DrawPresetResetIcon(
                            "Bend Shadow Contrast",
                            shadows.shadowContrast !=
                                bendDefaults.shadowContrast))
                    {
                        shadows.shadowContrast =
                            bendDefaults.shadowContrast;
                        bendResetApplied = true;
                    }

                    static constexpr const char* HardSampleLabels[] = {
                        "0", "4", "8"
                    };
                    const int selectedHard =
                        FindBendShadowCompiledValue(
                            BendShadowHardSampleCounts,
                            shadows.hardShadowSamples);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Hard Shadow Samples##BendShadows",
                            selectedHard >= 0
                                ? HardSampleLabels[selectedHard]
                                : "Unsupported"))
                    {
                        for (int index = 0;
                            index < int(std::size(HardSampleLabels));
                            ++index)
                        {
                            const uint32_t value =
                                BendShadowHardSampleCounts[size_t(index)];
                            DrawDeferredDropdownOption(
                                HardSampleLabels[index],
                                HardSampleLabels[index],
                                shadows.hardShadowSamples == value,
                                [settings = &shadows, value]()
                                {
                                    settings->hardShadowSamples = value;
                                    settings->preset =
                                        BendShadowPreset::Custom;
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Select the compiled count of fully hard contact "
                        "samples.");
                    if (DrawNestedDropdownResetIcon(
                            "Bend Shadow Hard Samples",
                            shadows.hardShadowSamples !=
                                bendDefaults.hardShadowSamples))
                    {
                        const uint32_t defaultHardShadowSamples =
                            bendDefaults.hardShadowSamples;
                        QueueDeferredControlUiAction(
                            [settings = &shadows,
                                defaultHardShadowSamples]()
                            {
                                settings->hardShadowSamples =
                                    defaultHardShadowSamples;
                                reconcileBendPreset(*settings);
                            });
                    }

                    static constexpr const char* FadeSampleLabels[] = {
                        "0", "8", "16"
                    };
                    const int selectedFade =
                        FindBendShadowCompiledValue(
                            BendShadowFadeSampleCounts,
                            shadows.fadeOutSamples);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Fade-Out Samples##BendShadows",
                            selectedFade >= 0
                                ? FadeSampleLabels[selectedFade]
                                : "Unsupported"))
                    {
                        for (int index = 0;
                            index < int(std::size(FadeSampleLabels));
                            ++index)
                        {
                            const uint32_t value =
                                BendShadowFadeSampleCounts[size_t(index)];
                            DrawDeferredDropdownOption(
                                FadeSampleLabels[index],
                                FadeSampleLabels[index],
                                shadows.fadeOutSamples == value,
                                [settings = &shadows, value]()
                                {
                                    settings->fadeOutSamples = value;
                                    settings->preset =
                                        BendShadowPreset::Custom;
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Select the compiled count of samples that soften "
                        "the trace endpoint.");
                    if (DrawNestedDropdownResetIcon(
                            "Bend Shadow Fade-Out Samples",
                            shadows.fadeOutSamples !=
                                bendDefaults.fadeOutSamples))
                    {
                        const uint32_t defaultFadeOutSamples =
                            bendDefaults.fadeOutSamples;
                        QueueDeferredControlUiAction(
                            [settings = &shadows,
                                defaultFadeOutSamples]()
                            {
                                settings->fadeOutSamples =
                                    defaultFadeOutSamples;
                                reconcileBendPreset(*settings);
                            });
                    }

                    bendCustomChanged |= ImGui::Checkbox(
                        "Ignore Edge Pixels##BendShadows",
                        &shadows.ignoreEdgePixels);
                    ImGui::SetItemTooltip(
                        "Prevent detected depth-edge pixels from casting "
                        "shadows.");
                    if (DrawPresetResetIcon(
                            "Bend Shadow Ignore Edge Pixels",
                            shadows.ignoreEdgePixels !=
                                bendDefaults.ignoreEdgePixels))
                    {
                        shadows.ignoreEdgePixels =
                            bendDefaults.ignoreEdgePixels;
                        bendResetApplied = true;
                    }
                    bendCustomChanged |= ImGui::Checkbox(
                        "Precision Offset##BendShadows",
                        &shadows.usePrecisionOffset);
                    ImGui::SetItemTooltip(
                        "Apply Bend's optional depth precision offset.");
                    if (DrawPresetResetIcon(
                            "Bend Shadow Precision Offset",
                            shadows.usePrecisionOffset !=
                                bendDefaults.usePrecisionOffset))
                    {
                        shadows.usePrecisionOffset =
                            bendDefaults.usePrecisionOffset;
                        bendResetApplied = true;
                    }
                    bendCustomChanged |= ImGui::Checkbox(
                        "Bilinear Offset Mode##BendShadows",
                        &shadows.bilinearSamplingOffsetMode);
                    ImGui::SetItemTooltip(
                        "Offset bilinear samples onto the shared wavefront "
                        "ray.");
                    if (DrawPresetResetIcon(
                            "Bend Shadow Bilinear Offset Mode",
                            shadows.bilinearSamplingOffsetMode !=
                                bendDefaults.bilinearSamplingOffsetMode))
                    {
                        shadows.bilinearSamplingOffsetMode =
                            bendDefaults.bilinearSamplingOffsetMode;
                        bendResetApplied = true;
                    }
                    bendCustomChanged |= ImGui::Checkbox(
                        "Early Out##BendShadows",
                        &shadows.useEarlyOut);
                    ImGui::SetItemTooltip(
                        shadows.debugView == BendShadowDebugView::None
                            ? "Skip pixels outside Bend's directional depth "
                                "bounds."
                            : "Bend suppresses effective early-out while a "
                                "debug view is active.");
                    if (DrawPresetResetIcon(
                            "Bend Shadow Early Out",
                            shadows.useEarlyOut !=
                                bendDefaults.useEarlyOut))
                    {
                        shadows.useEarlyOut =
                            bendDefaults.useEarlyOut;
                        bendResetApplied = true;
                    }

                    static constexpr const char* DebugLabels[] = {
                        "Off", "Edge", "Thread", "Wave"
                    };
                    const int debugIndex = std::clamp(
                        int(shadows.debugView),
                        0,
                        int(std::size(DebugLabels)) - 1);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Debug View##BendShadows",
                            DebugLabels[debugIndex]))
                    {
                        for (int index = 0;
                            index < int(std::size(DebugLabels));
                            ++index)
                        {
                            const BendShadowDebugView view =
                                BendShadowDebugView(index);
                            DrawDeferredDropdownOption(
                                DebugLabels[index],
                                DebugLabels[index],
                                shadows.debugView == view,
                                [settings = &shadows, view]()
                                {
                                    settings->debugView = view;
                                    settings->preset =
                                        BendShadowPreset::Custom;
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Show the raw R8 edge, thread, or wave diagnostic "
                        "output.");
                    if (DrawNestedDropdownResetIcon(
                            "Bend Shadow Debug View",
                            shadows.debugView != bendDefaults.debugView))
                    {
                        const BendShadowDebugView defaultDebugView =
                            bendDefaults.debugView;
                        QueueDeferredControlUiAction(
                            [settings = &shadows, defaultDebugView]()
                            {
                                settings->debugView = defaultDebugView;
                                reconcileBendPreset(*settings);
                            });
                    }
                    if (bendCustomChanged)
                        shadows.preset = BendShadowPreset::Custom;
                    else if (bendResetApplied)
                        reconcileBendPreset(shadows);
                    EndAnimatedToggleRegion();
                }
                if (!directionalVisibilityAvailable)
                {
                    ImGui::EndDisabled();
                    ImGui::TextDisabled(
                        "Requires deferred PBR and a directional light.");
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Sparse Virtual Shadow Maps##Lights"))
            {
                SparseVirtualShadowMapSettings& shadows =
                    m_ui.SparseVirtualShadowMaps;
                const bool motionTestRunning =
                    m_app->IsSvsmMotionBenchmarkRunning();
                if (!directionalVisibilityAvailable ||
                    motionTestRunning)
                {
                    ImGui::BeginDisabled();
                }
                ImGui::Checkbox("Enabled##Svsm", &shadows.enabled);
                ImGui::SetItemTooltip(
                    "Resolve cached virtual shadow-map visibility for the "
                    "primary directional light.");
                if (DrawPresetResetIcon(
                        "SVSM Enabled",
                        shadows.enabled))
                {
                    shadows.enabled = false;
                }
                if (BeginAnimatedToggleRegion(
                        "##SvsmControls",
                        shadows.enabled))
                {
                    DrawSvsmSettingsSurface(
                        shadows,
                        settingsControlWidth);
                    EndAnimatedToggleRegion();
                }
                if (!directionalVisibilityAvailable ||
                    motionTestRunning)
                {
                    ImGui::EndDisabled();
                }
                if (!directionalVisibilityAvailable)
                {
                    ImGui::TextDisabled(
                        "Requires deferred PBR and a directional light.");
                }
                EndAnimatedTreeNode();
            }

            if (BeginAnimatedTreeNode(
                    "Diagnostic Cascaded Shadow Maps##Lights"))
            {
                DiagnosticCascadedShadowMapSettings& shadows =
                    m_ui.DiagnosticCascadedShadowMaps;
                const DiagnosticCascadedShadowMapSettings csmDefaults{};
                static constexpr auto reconcileCsmProfile =
                    [](DiagnosticCascadedShadowMapSettings& settings)
                    {
                        constexpr DiagnosticCsmProfile Profiles[] = {
                            DiagnosticCsmProfile::SingleMapReference,
                            DiagnosticCsmProfile::LowCostCsm,
                            DiagnosticCsmProfile::Ue5CsmReference,
                            DiagnosticCsmProfile::CachedSingleShadow,
                            DiagnosticCsmProfile::
                                OptimizedCachedSingleShadow,
                            DiagnosticCsmProfile::OptimizedCachedCsm
                        };
                        for (const DiagnosticCsmProfile profile : Profiles)
                        {
                            const DiagnosticCascadedShadowMapSettings
                                profileSettings =
                                    ApplyDiagnosticCsmProfile(
                                        settings,
                                        profile);
                            if (IsSameDiagnosticCsmTimingConfiguration(
                                    settings,
                                    profileSettings))
                            {
                                settings.profile = profile;
                                return;
                            }
                        }
                        settings.profile = DiagnosticCsmProfile::Custom;
                    };
                if (!directionalVisibilityAvailable)
                    ImGui::BeginDisabled();
                ImGui::Checkbox(
                    "Enabled##DiagnosticCsm",
                    &shadows.enabled);
                ImGui::SetItemTooltip(
                    "Resolve the independent cascaded-shadow diagnostic.");
                if (DrawPresetResetIcon(
                        "Diagnostic CSM Enabled",
                        shadows.enabled))
                {
                    shadows.enabled = false;
                }
                if (BeginAnimatedToggleRegion(
                        "##DiagnosticCsmControls",
                        shadows.enabled))
                {
                    bool csmCustomChanged = false;
                    bool csmResetApplied = false;
                    const auto drawCsmCheckbox =
                        [&csmCustomChanged, &csmResetApplied](
                            const char* label,
                            const char* resetId,
                            bool& value,
                            bool defaultValue,
                            const char* tooltip,
                            bool available)
                        {
                            if (!available)
                                ImGui::BeginDisabled();
                            const bool changed =
                                ImGui::Checkbox(label, &value);
                            ImGui::SetItemTooltip("%s", tooltip);
                            if (!available)
                                ImGui::EndDisabled();
                            if (DrawPresetResetIcon(
                                    resetId,
                                    value != defaultValue))
                            {
                                value = defaultValue;
                                csmResetApplied = true;
                            }
                            csmCustomChanged |= changed;
                        };
                    const auto drawCsmFloat =
                        [&csmCustomChanged, &csmResetApplied](
                            const char* label,
                            const char* resetId,
                            float& value,
                            float defaultValue,
                            float minimum,
                            float maximum,
                            const char* format,
                            const char* tooltip,
                            bool available)
                        {
                            if (!available)
                                ImGui::BeginDisabled();
                            const bool changed = DrawSliderFloat(
                                label,
                                &value,
                                minimum,
                                maximum,
                                format);
                            ImGui::SetItemTooltip("%s", tooltip);
                            if (!available)
                                ImGui::EndDisabled();
                            if (DrawPresetResetIcon(
                                    resetId,
                                    value != defaultValue))
                            {
                                value = defaultValue;
                                csmResetApplied = true;
                            }
                            csmCustomChanged |= changed;
                        };
                    const auto drawCsmUint =
                        [&csmCustomChanged, &csmResetApplied](
                            const char* label,
                            const char* resetId,
                            uint32_t& value,
                            uint32_t defaultValue,
                            int minimum,
                            int maximum,
                            ImGuiSliderFlags flags,
                            const char* tooltip)
                        {
                            int sliderValue = int(std::clamp(
                                value,
                                uint32_t(minimum),
                                uint32_t(maximum)));
                            if (DrawSliderInt(
                                    label,
                                    &sliderValue,
                                    minimum,
                                    maximum,
                                    "%d",
                                    flags))
                            {
                                value = uint32_t(sliderValue);
                                csmCustomChanged = true;
                            }
                            ImGui::SetItemTooltip("%s", tooltip);
                            if (DrawPresetResetIcon(
                                    resetId,
                                    value != defaultValue))
                            {
                                value = defaultValue;
                                csmResetApplied = true;
                            }
                        };

                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Profile##DiagnosticCsm",
                            GetDiagnosticCsmProfileLabel(
                                shadows.profile)))
                    {
                        for (uint32_t index = 0u;
                            index < uint32_t(
                                DiagnosticCsmProfile::Count);
                            ++index)
                        {
                            const DiagnosticCsmProfile profile =
                                DiagnosticCsmProfile(index);
                            const char* label =
                                GetDiagnosticCsmProfileLabel(profile);
                            DrawDeferredDropdownOption(
                                label,
                                label,
                                shadows.profile == profile,
                                [settings = &shadows, profile]()
                                {
                                    if (profile ==
                                        DiagnosticCsmProfile::Custom)
                                    {
                                        settings->profile = profile;
                                    }
                                    else
                                    {
                                        *settings =
                                            ApplyDiagnosticCsmProfile(
                                                *settings,
                                                profile);
                                    }
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Select a reference or cache-policy diagnostic. "
                        "Custom retains every edited value.");
                    const DiagnosticCascadedShadowMapSettings
                        csmUe5Profile =
                            ApplyDiagnosticCsmProfile(
                                shadows,
                                DiagnosticCsmProfile::Ue5CsmReference);
                    if (DrawNestedDropdownResetIcon(
                            "Diagnostic CSM Profile",
                            shadows.profile !=
                                    DiagnosticCsmProfile::
                                        Ue5CsmReference ||
                                !IsSameDiagnosticCsmTimingConfiguration(
                                    shadows,
                                    csmUe5Profile),
                            "Reset profile-owned CSM settings to UE5 CSM "
                            "Reference."))
                    {
                        QueueDeferredControlUiAction(
                            [settings = &shadows]()
                            {
                                *settings =
                                    ApplyDiagnosticCsmProfile(
                                        *settings,
                                        DiagnosticCsmProfile::
                                            Ue5CsmReference);
                            });
                    }

                    drawCsmUint(
                        "Cascades##DiagnosticCsm",
                        "Diagnostic CSM Cascades",
                        shadows.cascadeCount,
                        csmDefaults.cascadeCount,
                        1,
                        int(DiagnosticCsmMaximumCascades),
                        ImGuiSliderFlags_None,
                        "Use one to four directional cascades.");
                    drawCsmUint(
                        "Resolution per Cascade##DiagnosticCsm",
                        "Diagnostic CSM Resolution per Cascade",
                        shadows.shadowMapResolution,
                        csmDefaults.shadowMapResolution,
                        128,
                        8192,
                        ImGuiSliderFlags_Logarithmic |
                            ImGuiSliderFlags_AlwaysClamp,
                        "Set the persistent square resolution of every "
                        "cascade. Prefer D16 and fall back to sampleable D32 "
                        "when D16 is unavailable.");
                    drawCsmFloat(
                        "Maximum Shadow Distance##DiagnosticCsm",
                        "Diagnostic CSM Maximum Shadow Distance",
                        shadows.maximumShadowDistance,
                        csmDefaults.maximumShadowDistance,
                        1.f,
                        5000.f,
                        "%.1f",
                        "Set the camera-space range covered by all cascades.",
                        true);
                    drawCsmFloat(
                        "Maximum Light Depth##DiagnosticCsm",
                        "Diagnostic CSM Maximum Light Depth",
                        shadows.maximumLightDepth,
                        csmDefaults.maximumLightDepth,
                        1.f,
                        10000.f,
                        "%.1f",
                        "Set the saved conservative caster depth range. UE "
                        "Minimum Light Depth raises the effective range to at "
                        "least 10,000 units.",
                        true);

                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Filter##DiagnosticCsm",
                            GetDiagnosticCsmFilterLabel(
                                shadows.filter)))
                    {
                        for (uint32_t index = 0u;
                            index < uint32_t(
                                DiagnosticCsmFilter::Count);
                            ++index)
                        {
                            const DiagnosticCsmFilter filter =
                                DiagnosticCsmFilter(index);
                            const char* label =
                                GetDiagnosticCsmFilterLabel(filter);
                            DrawDeferredDropdownOption(
                                label,
                                label,
                                shadows.filter == filter,
                                [settings = &shadows, filter]()
                                {
                                    settings->filter = filter;
                                    settings->profile =
                                        DiagnosticCsmProfile::Custom;
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Use UE's manual 5x5 PCF or the SVSM-matched fixed "
                        "point-load Poisson footprint.");
                    if (DrawNestedDropdownResetIcon(
                            "Diagnostic CSM Filter",
                            shadows.filter != csmDefaults.filter))
                    {
                        QueueDeferredControlUiAction(
                            [settings = &shadows]()
                            {
                                settings->filter =
                                    DiagnosticCsmFilter::Ue5Pcf5x5;
                                reconcileCsmProfile(*settings);
                            });
                    }

                    const bool poissonFilterActive =
                        shadows.filter == DiagnosticCsmFilter::Poisson;
                    if (!poissonFilterActive)
                        ImGui::BeginDisabled();
                    constexpr uint32_t PoissonTapCounts[] = {
                        1u, 4u, 8u, 16u
                    };
                    const uint32_t normalizedTapCount =
                        NormalizeDiagnosticCsmTapCount(
                            shadows.poissonTapCount);
                    char tapPreview[16];
                    snprintf(
                        tapPreview,
                        std::size(tapPreview),
                        "%u taps",
                        normalizedTapCount);
                    ImGui::SetNextItemWidth(settingsControlWidth);
                    if (BeginRoundedCombo(
                            "Filter Taps##DiagnosticCsm",
                            tapPreview))
                    {
                        for (const uint32_t tapCount : PoissonTapCounts)
                        {
                            char label[16];
                            snprintf(
                                label,
                                std::size(label),
                                "%u taps",
                                tapCount);
                            DrawDeferredDropdownOption(
                                label,
                                label,
                                normalizedTapCount == tapCount,
                                [settings = &shadows, tapCount]()
                                {
                                    settings->poissonTapCount = tapCount;
                                    settings->profile =
                                        DiagnosticCsmProfile::Custom;
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Select the matched 1, 4, 8, or 16-tap Poisson "
                        "receiver.");
                    if (!poissonFilterActive)
                        ImGui::EndDisabled();
                    if (DrawNestedDropdownResetIcon(
                            "Diagnostic CSM Filter Taps",
                            shadows.poissonTapCount !=
                                csmDefaults.poissonTapCount))
                    {
                        QueueDeferredControlUiAction(
                            [settings = &shadows]()
                            {
                                settings->poissonTapCount = 16u;
                                reconcileCsmProfile(*settings);
                            });
                    }
                    drawCsmFloat(
                        "Filter Radius##DiagnosticCsm",
                        "Diagnostic CSM Filter Radius",
                        shadows.filterRadiusTexels,
                        csmDefaults.filterRadiusTexels,
                        0.f,
                        float(DiagnosticCsmMaximumFilterRadiusTexels),
                        "%.2f texels",
                        poissonFilterActive
                            ? "Set the fixed point-load Poisson footprint and "
                                "dirty-rectangle safety halo in shadow texels."
                            : "UE's manual PCF retains its fixed 5x5 "
                                "footprint; this saved Poisson radius is "
                                "retained.",
                        poissonFilterActive);

                    if (BeginAnimatedTreeNode(
                            "Developer Options##DiagnosticCsm"))
                    {
                        if (BeginAnimatedTreeNode(
                                "Projection and Bias##DiagnosticCsm"))
                        {
                            drawCsmCheckbox(
                                "UE Minimum Light Depth##DiagnosticCsm",
                                "Diagnostic CSM UE Minimum Light Depth",
                                shadows.enforceUeMinimumLightDepth,
                                csmDefaults.enforceUeMinimumLightDepth,
                                "Enforce UE's conventional 10,000-unit "
                                "minimum directional-shadow subject depth "
                                "span.",
                                true);
                            drawCsmFloat(
                                "Split Distribution Exponent##DiagnosticCsm",
                                "Diagnostic CSM Split Distribution Exponent",
                                shadows.cascadeDistributionExponent,
                                csmDefaults.cascadeDistributionExponent,
                                1.f,
                                8.f,
                                "%.2f",
                                "Set UE-style geometric cascade split "
                                "weighting.",
                                true);
                            drawCsmFloat(
                                "Cascade Transition Fraction##DiagnosticCsm",
                                "Diagnostic CSM Cascade Transition Fraction",
                                shadows.cascadeTransitionFraction,
                                csmDefaults.cascadeTransitionFraction,
                                0.f,
                                0.5f,
                                "%.3f",
                                "Extend and cross-fade adjacent cascades over "
                                "this fraction of each split.",
                                true);
                            drawCsmFloat(
                                "Shadow Distance Fade Fraction##DiagnosticCsm",
                                "Diagnostic CSM Shadow Distance Fade Fraction",
                                shadows.shadowDistanceFadeoutFraction,
                                csmDefaults.shadowDistanceFadeoutFraction,
                                0.f,
                                0.5f,
                                "%.3f",
                                "Fade the last cascade to unshadowed "
                                "visibility at the maximum distance.",
                                true);
                            drawCsmUint(
                                "Projection Snap Multiple##DiagnosticCsm",
                                "Diagnostic CSM Projection Snap Multiple",
                                shadows.projectionSnapTexelMultiple,
                                csmDefaults.projectionSnapTexelMultiple,
                                1,
                                16,
                                ImGuiSliderFlags_None,
                                "Snap stable cascade centers in this many "
                                "shadow texels.");
                            drawCsmFloat(
                                "Depth Bias##DiagnosticCsm",
                                "Diagnostic CSM Depth Bias",
                                shadows.depthBias,
                                csmDefaults.depthBias,
                                0.f,
                                100.f,
                                "%.2f",
                                "Set UE's CSM depth-bias base value.",
                                true);
                            drawCsmFloat(
                                "Slope-Scaled Depth Bias##DiagnosticCsm",
                                "Diagnostic CSM Slope-Scaled Depth Bias",
                                shadows.slopeScaledDepthBias,
                                csmDefaults.slopeScaledDepthBias,
                                0.f,
                                10.f,
                                "%.2f",
                                "Set UE's CSM slope-scaled depth-bias base "
                                "value.",
                                true);
                            drawCsmFloat(
                                "Directional-Light Shadow Bias##DiagnosticCsm",
                                "Diagnostic CSM Directional-Light Shadow Bias",
                                shadows.directionalLightShadowBias,
                                csmDefaults.directionalLightShadowBias,
                                0.f,
                                1.f,
                                "%.2f",
                                "Match the directional-light Shadow Bias "
                                "multiplier.",
                                true);
                            drawCsmFloat(
                                "Directional-Light Slope Bias##DiagnosticCsm",
                                "Diagnostic CSM Directional-Light Slope Bias",
                                shadows.directionalLightShadowSlopeBias,
                                csmDefaults.directionalLightShadowSlopeBias,
                                0.f,
                                1.f,
                                "%.2f",
                                "Match the directional-light Shadow Slope "
                                "Bias multiplier.",
                                true);
                            drawCsmFloat(
                                "Receiver Depth Bias##DiagnosticCsm",
                                "Diagnostic CSM Receiver Depth Bias",
                                shadows.receiverDepthBias,
                                csmDefaults.receiverDepthBias,
                                0.f,
                                1.f,
                                "%.3f",
                                "Widen the soft comparison transition at "
                                "grazing receiver angles.",
                                true);
                            EndAnimatedTreeNode();
                        }

                        if (BeginAnimatedTreeNode(
                                "Cache Update Policy##DiagnosticCsm"))
                        {
                            const bool cachePolicyActive =
                                HasAnyDiagnosticCsmCachePolicy(shadows);
                            drawCsmCheckbox(
                                "Cached Shadow Draw Lists##DiagnosticCsm",
                                "Diagnostic CSM Cached Shadow Draw Lists",
                                shadows.cachedShadowDrawListsEnabled,
                                csmDefaults.cachedShadowDrawListsEnabled,
                                "Reuse exact final sorted caster lists for "
                                "repeating full-redraw configurations.",
                                !cachePolicyActive);
                            drawCsmCheckbox(
                                "Whole-Map Reuse##DiagnosticCsm",
                                "Diagnostic CSM Whole-Map Reuse",
                                shadows.wholeMapReuseEnabled,
                                csmDefaults.wholeMapReuseEnabled,
                                "Reuse all cascades when every projection and "
                                "the scene revision are unchanged.",
                                true);
                            drawCsmCheckbox(
                                "Whole-Cascade Reuse##DiagnosticCsm",
                                "Diagnostic CSM Whole-Cascade Reuse",
                                shadows.wholeCascadeReuseEnabled,
                                csmDefaults.wholeCascadeReuseEnabled,
                                "Classify and reuse each cascade "
                                "independently.",
                                true);
                            const bool cascadeReuseActive =
                                shadows.wholeCascadeReuseEnabled;
                            drawCsmCheckbox(
                                "Dirty Rectangles##DiagnosticCsm",
                                "Diagnostic CSM Dirty Rectangles",
                                shadows.dirtyRectanglesEnabled,
                                csmDefaults.dirtyRectanglesEnabled,
                                "Clear old and new changed bounds and rerender "
                                "every overlapping caster.",
                                cascadeReuseActive);
                            drawCsmCheckbox(
                                "Scrolling##DiagnosticCsm",
                                "Diagnostic CSM Scrolling",
                                shadows.scrollingEnabled,
                                csmDefaults.scrollingEnabled,
                                "Reuse exactly compatible integer-shifted "
                                "texels, then update exposed regions.",
                                cascadeReuseActive);
                            drawCsmFloat(
                                "Minimum Scroll Overlap##DiagnosticCsm",
                                "Diagnostic CSM Minimum Scroll Overlap",
                                shadows.minimumScrollOverlap,
                                csmDefaults.minimumScrollOverlap,
                                0.5f,
                                1.f,
                                "%.2f",
                                "Require at least this compatible texel "
                                "overlap before accepting a scroll update.",
                                cascadeReuseActive &&
                                    shadows.scrollingEnabled);
                            EndAnimatedTreeNode();
                        }

                        if (BeginAnimatedTreeNode(
                                "Culling and Raster##DiagnosticCsm"))
                        {
                            const bool viewDependentCullingAvailable =
                                CanUseDiagnosticCsmViewDependentCasterCulling(
                                    shadows);
                            drawCsmCheckbox(
                                "Input-Assembler Caster Fetch##DiagnosticCsm",
                                "Diagnostic CSM Input-Assembler Caster Fetch",
                                shadows.inputAssemblerCasterFetchEnabled,
                                csmDefaults.
                                    inputAssemblerCasterFetchEnabled,
                                "Route eligible non-deforming casters "
                                "through the CSM-local input-assembler path.",
                                true);
                            drawCsmCheckbox(
                                "Receiver Raster Scissor##DiagnosticCsm",
                                "Diagnostic CSM Receiver Raster Scissor",
                                shadows.receiverRasterScissorEnabled,
                                csmDefaults.receiverRasterScissorEnabled,
                                "Conservatively scissor uncached full-redraw "
                                "raster to the snapped receiver footprint.",
                                viewDependentCullingAvailable);
                            drawCsmCheckbox(
                                "Accurate Caster Hull Culling##DiagnosticCsm",
                                "Diagnostic CSM Accurate Caster Hull Culling",
                                shadows.accurateCasterCullingEnabled,
                                csmDefaults.accurateCasterCullingEnabled,
                                "Reject reliable bounds outside the "
                                "light-extruded receiver hull during full "
                                "redraws.",
                                viewDependentCullingAvailable);
                            drawCsmCheckbox(
                                "UE Caster Radius Threshold##DiagnosticCsm",
                                "Diagnostic CSM UE Caster Radius Threshold",
                                shadows.ueCasterRadiusThresholdEnabled,
                                csmDefaults.
                                    ueCasterRadiusThresholdEnabled,
                                "Apply UE's camera-projected caster-size "
                                "rejection when no map-cache policy is active.",
                                viewDependentCullingAvailable);
                            drawCsmFloat(
                                "Caster Radius Threshold##DiagnosticCsm",
                                "Diagnostic CSM Caster Radius Threshold",
                                shadows.casterRadiusThreshold,
                                csmDefaults.casterRadiusThreshold,
                                0.f,
                                0.05f,
                                "%.3f",
                                "Cull reliable casters below this "
                                "camera-distance-relative radius.",
                                viewDependentCullingAvailable &&
                                    shadows.
                                        ueCasterRadiusThresholdEnabled);
                            EndAnimatedTreeNode();
                        }

                        if (BeginAnimatedTreeNode(
                                "Unabstracted##DiagnosticCsm"))
                        {
                            drawCsmCheckbox(
                                "16-Bit Shadow Depth##DiagnosticCsm",
                                "Diagnostic CSM 16-Bit Shadow Depth",
                                shadows.use16BitDepthEnabled,
                                csmDefaults.use16BitDepthEnabled,
                                "Prefer D16 shadow depth and fall back to "
                                "sampleable D32 when D16 is unsupported.",
                                true);
                            drawCsmCheckbox(
                                "Opaque Depth State Merging##DiagnosticCsm",
                                "Diagnostic CSM Opaque Depth State Merging",
                                shadows.opaqueDepthStateMergingEnabled,
                                csmDefaults.
                                    opaqueDepthStateMergingEnabled,
                                "Canonicalize eligible opaque depth-only "
                                "materials while retaining distinct "
                                "alpha-tested states.",
                                true);
                            drawCsmCheckbox(
                                "Position-Only Opaque Casters##DiagnosticCsm",
                                "Diagnostic CSM Position-Only Opaque Casters",
                                shadows.positionOnlyOpaqueEnabled,
                                csmDefaults.positionOnlyOpaqueEnabled,
                                "Use the CSM-local position-only permutation "
                                "for eligible opaque casters.",
                                true);
                            drawCsmCheckbox(
                                "Translation-Only Caster Transforms"
                                "##DiagnosticCsm",
                                "Diagnostic CSM Translation-Only Caster "
                                "Transforms",
                                shadows.
                                    translationOnlyCasterTransformEnabled,
                                csmDefaults.
                                    translationOnlyCasterTransformEnabled,
                                "Use finite translation-only constants for "
                                "eligible single-instance casters.",
                                true);
                            drawCsmCheckbox(
                                "Precomputed Depth-Axis Normalization"
                                "##DiagnosticCsm",
                                "Diagnostic CSM Precomputed Depth-Axis "
                                "Normalization",
                                shadows.
                                    precomputedDepthAxisInverseLengthEnabled,
                                csmDefaults.
                                    precomputedDepthAxisInverseLengthEnabled,
                                "Normalize the directional depth axis once "
                                "per cascade.",
                                true);
                            const bool depthAxisPrecomputed =
                                shadows.
                                    precomputedDepthAxisInverseLengthEnabled;
                            drawCsmCheckbox(
                                "Conservative Saturated-Slope Shortcut"
                                "##DiagnosticCsm",
                                "Diagnostic CSM Conservative Saturated-Slope "
                                "Shortcut",
                                shadows.conservativeSaturatedSlopeEnabled,
                                csmDefaults.
                                    conservativeSaturatedSlopeEnabled,
                                "Use the conservative shortcut at UE's "
                                "clamped slope boundary.",
                                depthAxisPrecomputed);
                            drawCsmCheckbox(
                                "Algebraic Slow-Slope Reduction"
                                "##DiagnosticCsm",
                                "Diagnostic CSM Algebraic Slow-Slope "
                                "Reduction",
                                shadows.algebraicSlowSlopeEnabled,
                                csmDefaults.algebraicSlowSlopeEnabled,
                                "Evaluate the unsaturated UE slope with the "
                                "equivalent algebraic reduction.",
                                depthAxisPrecomputed);
                            drawCsmCheckbox(
                                "Pre-Normalized Receiver Light Direction"
                                "##DiagnosticCsm",
                                "Diagnostic CSM Pre-Normalized Receiver "
                                "Light Direction",
                                shadows.
                                    preNormalizedReceiverLightDirectionEnabled,
                                csmDefaults.
                                    preNormalizedReceiverLightDirectionEnabled,
                                "Reuse the finite CPU-normalized directional "
                                "light vector in the receiver.",
                                true);
                            drawCsmCheckbox(
                                "Precomposed Clip-to-Shadow Transform"
                                "##DiagnosticCsm",
                                "Diagnostic CSM Precomposed Clip-to-Shadow "
                                "Transform",
                                shadows.precomposedClipToShadowEnabled,
                                csmDefaults.precomposedClipToShadowEnabled,
                                "Precompose each cascade's clip-to-shadow "
                                "transform on the CPU.",
                                true);
                            drawCsmCheckbox(
                                "One-Pass Cascade Classification"
                                "##DiagnosticCsm",
                                "Diagnostic CSM One-Pass Cascade "
                                "Classification",
                                shadows.
                                    singleTraversalCasterClassificationEnabled,
                                csmDefaults.
                                    singleTraversalCasterClassificationEnabled,
                                "Traverse the scene once and classify each "
                                "caster across redrawn cascades.",
                                true);
                            const bool receiverHullAxesAvailable =
                                CanUseDiagnosticCsmViewDependentCasterCulling(
                                    shadows) &&
                                shadows.accurateCasterCullingEnabled;
                            drawCsmCheckbox(
                                "Precomputed Receiver Hull Axes"
                                "##DiagnosticCsm",
                                "Diagnostic CSM Precomputed Receiver Hull "
                                "Axes",
                                shadows.precomputedReceiverHullAxesEnabled,
                                csmDefaults.
                                    precomputedReceiverHullAxesEnabled,
                                "Precompute normalized receiver-hull axes and "
                                "intervals once per cascade.",
                                receiverHullAxesAvailable);
                            drawCsmCheckbox(
                                "Shared Caster Light Projection"
                                "##DiagnosticCsm",
                                "Diagnostic CSM Shared Caster Light "
                                "Projection",
                                shadows.sharedCasterLightProjectionEnabled,
                                csmDefaults.
                                    sharedCasterLightProjectionEnabled,
                                "Project reliable caster bounds once into a "
                                "shared directional-light basis.",
                                true);
                            drawCsmCheckbox(
                                "Direct Caster Submission##DiagnosticCsm",
                                "Diagnostic CSM Direct Caster Submission",
                                shadows.directCasterSubmissionEnabled,
                                csmDefaults.directCasterSubmissionEnabled,
                                "Submit sorted caster records directly "
                                "without rebuilding a scratch vector.",
                                true);
                            drawCsmCheckbox(
                                "Batched Full-Redraw Clear##DiagnosticCsm",
                                "Diagnostic CSM Batched Full-Redraw Clear",
                                shadows.batchedFullRedrawClearEnabled,
                                csmDefaults.
                                    batchedFullRedrawClearEnabled,
                                "Clear contiguous full-redraw cascade slices "
                                "with one depth clear.",
                                true);
                            EndAnimatedTreeNode();
                        }
                        EndAnimatedTreeNode();
                    }

                    if (BeginAnimatedTreeNode(
                            "Diagnostics##DiagnosticCsm"))
                    {
                        drawCsmCheckbox(
                            "Detailed GPU Stage Timing##DiagnosticCsm",
                            "Diagnostic CSM Detailed GPU Stage Timing",
                            shadows.detailedGpuTimingEnabled,
                            csmDefaults.detailedGpuTimingEnabled,
                            "Measure clear/update, raster, and "
                            "full-resolution sampling separately.",
                            true);

                        ImGui::SetNextItemWidth(settingsControlWidth);
                        if (BeginRoundedCombo(
                                "Debug View##DiagnosticCsm",
                                GetDiagnosticCsmDebugViewLabel(
                                    shadows.debugView)))
                        {
                            for (uint32_t index = 0u;
                                index < uint32_t(
                                    DiagnosticCsmDebugView::Count);
                                ++index)
                            {
                                const DiagnosticCsmDebugView view =
                                    DiagnosticCsmDebugView(index);
                                const char* label =
                                    GetDiagnosticCsmDebugViewLabel(view);
                                DrawDeferredDropdownOption(
                                    label,
                                    label,
                                    shadows.debugView == view,
                                    [settings = &shadows, view]()
                                    {
                                        settings->debugView = view;
                                        settings->profile =
                                            DiagnosticCsmProfile::Custom;
                                    });
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SetItemTooltip(
                            "Present CSM visibility, cascade selection, or "
                            "cache action without changing the lighting "
                            "input.");
                        if (DrawNestedDropdownResetIcon(
                                "Diagnostic CSM Debug View",
                                shadows.debugView !=
                                    csmDefaults.debugView))
                        {
                            QueueDeferredControlUiAction(
                                [settings = &shadows]()
                                {
                                    settings->debugView =
                                        DiagnosticCsmDebugView::None;
                                    reconcileCsmProfile(*settings);
                                });
                        }
                        EndAnimatedTreeNode();
                    }

                    if (csmCustomChanged)
                    {
                        shadows.profile =
                            DiagnosticCsmProfile::Custom;
                    }
                    else if (csmResetApplied)
                    {
                        reconcileCsmProfile(shadows);
                    }
                    EndAnimatedToggleRegion();
                }
                if (!directionalVisibilityAvailable)
                    ImGui::EndDisabled();
                if (!directionalVisibilityAvailable)
                {
                    ImGui::TextDisabled(
                        "Requires deferred PBR and a directional light.");
                }
                EndAnimatedTreeNode();
            }
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
        const bool zoomSuspendedForMotionTest =
            m_app->IsAntiAliasingMotionTestRunning();
        if (zoomSuspendedForMotionTest)
            ImGui::BeginDisabled();
        if (DrawCenteredActionButton(
                GetPixelZoomButtonLabel(m_ui.PixelZoom),
                actionButtonWidth))
        {
            m_ui.PixelZoom =
                AdvancePixelZoomMode(m_ui.PixelZoom);
        }
        ImGui::SetItemTooltip(
            zoomSuspendedForMotionTest
                ? "Pixel zoom is suspended while the motion benchmark runs."
                : "Cycle exact Off, 2x, 3x, 4x, and 5x pixel zoom. Z uses the "
                    "same cycle.");
        if (zoomSuspendedForMotionTest)
            ImGui::EndDisabled();

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
        g_DeferredAliasingUiPresentation.Advance(
            ImGui::GetFrameCount(),
            settingsLayoutIdle && settingsScrollIdle,
            !IsDeferredDropdownPopupTransitionActive());
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
        const float settingsTitleHeight =
            fontSize + style.FramePadding.y * 2.f;
        if (settingsCollapsed)
        {
            UiBackdropRect& titleBackdrop =
                m_ui.BackdropRects[0];
            titleBackdrop.minX = settingsWindowPosition.x;
            titleBackdrop.minY = settingsWindowPosition.y;
            titleBackdrop.maxX =
                settingsWindowPosition.x + settingsWindowSize.x;
            titleBackdrop.maxY =
                settingsWindowPosition.y + settingsTitleHeight;
            titleBackdrop.rounding = style.WindowRounding;
            titleBackdrop.visible = true;

            UiBackdropRect& statusBackdrop =
                m_ui.BackdropRects[1];
            statusBackdrop.minX = settingsWindowPosition.x;
            statusBackdrop.minY =
                settingsWindowPosition.y + settingsTitleHeight - 1.f;
            statusBackdrop.maxX =
                settingsWindowPosition.x + settingsWindowSize.x;
            statusBackdrop.maxY =
                settingsWindowPosition.y + settingsWindowSize.y;
            statusBackdrop.rounding = std::min(
                style.WindowRounding,
                std::max(
                    0.f,
                    (statusBackdrop.maxY -
                        statusBackdrop.minY) * 0.15f));
            statusBackdrop.visible =
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
            titleBackdrop.rounding = style.WindowRounding;
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
            backdrop.shadowBlur = 10.f;
            backdrop.shadowOpacity = 0.34f;
            backdrop.shadowOffsetY = 3.f;
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
        ImGui::PopStyleColor(3);

        if (m_ui.ShowMaterialEditor)
        {
            ImGui::SetNextWindowPos(ImVec2(float(width) - fontSize * 0.6f, fontSize * 0.6f), 0, ImVec2(1.f, 0.f));
            const bool materialEditorVisible = ImGui::Begin(
                "Material Editor",
                &m_ui.ShowMaterialEditor,
                ImGuiWindowFlags_AlwaysAutoResize);
            CaptureCurrentWindowBackdrop(
                m_ui.BackdropRects[2],
                style.WindowRounding);
            if (visibilityBenchmarkBusy)
                ImGui::BeginDisabled();
            const bool deferredMaterialInputBlocked =
                HasDeferredDropdownUiActions();
            if (deferredMaterialInputBlocked)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 1.f);
                ImGui::BeginDisabled();
            }

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
                    ImGui::SetNextItemWidth(-FLT_MIN);
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
                            const std::shared_ptr<Scene> scene =
                                m_app->GetScene();
                            DrawDeferredDropdownOption(
                                MaterialDomainLabels[index],
                                MaterialDomainLabels[index],
                                material->domain == candidate,
                                [material, scene, candidate]()
                                {
                                    material->domain = candidate;
                                    material->dirty = true;
                                    if (scene)
                                    {
                                        scene->GetSceneGraph()
                                            ->GetRootNode()
                                            ->InvalidateContent();
                                    }
                                });
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SetItemTooltip(
                        "Choose how the selected surface is rendered.");
                    ImGui::PopID();

                    material->dirty |=
                        donut::app::MaterialEditor(
                            material.get(),
                            false,
                            false);
                }
                else
                {
                    ImGui::TextDisabled(
                        "Click a scene surface to select a material.");
                }
            }

            if (deferredMaterialInputBlocked)
            {
                ImGui::EndDisabled();
                ImGui::PopStyleVar();
            }
            if (visibilityBenchmarkBusy)
                ImGui::EndDisabled();

            ImGui::End();
        }

        // Commit only after every UI window has finished composing. Any
        // synchronous renderer work then holds a previously presented stable
        // frame instead of interrupting popup, drawer, scroll, Settings, or
        // magnifier motion.
        FinishUnsubmittedDeferredDropdownPopupTransition();
        TryApplyDeferredDropdownUiActions(
            deferredDropdownCompositionIdle(
                settingsLayoutIdle,
                settingsScrollIdle));
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
    VisibilityBenchmarkLaunchOptions& visibilityBenchmark,
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
            !strcmp(argv[i], "--aa-cache");
        if (developerAaOption)
        {
            log::error(
                "%s requires a build configured with "
                "UVSR_AA_DEVELOPER_OVERRIDES=ON",
                argv[i]);
            return false;
        }
#endif
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
            if (value == "temporal" ||
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
                    "temporal|cmaa2|msaa");
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
        else if (!strcmp(argv[i], "--aa-rectification"))
        {
            if (i + 1 >= argc)
                return missingValue(argv[i]);
            const std::string value = argv[++i];
            if (value == "preset")
            {
                aaBenchmark.settings.algorithmOverrides.rectification =
                    MiniEngineTaaRectificationOverride::FromPreset;
            }
            else if (value == "pair" || value == "pair-rgb")
            {
                aaBenchmark.settings.algorithmOverrides.rectification =
                    MiniEngineTaaRectificationOverride::PairRgb;
            }
            else if (value == "variance" ||
                value == "variance-ycocg")
            {
                aaBenchmark.settings.algorithmOverrides.rectification =
                    MiniEngineTaaRectificationOverride::VarianceYCoCg;
            }
            else
            {
                return invalidValue(
                    "--aa-rectification",
                    value,
                    "preset|pair-rgb|variance-ycocg");
            }
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
        else if (!strcmp(argv[i], "--benchmark-auto-close"))
        {
            visibilityBenchmark.autoClose = true;
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

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    ApplyVisibilityPerfProcessPriority();
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
    AaBenchmarkConfig aaBenchmark;
    VisibilityBenchmarkLaunchOptions visibilityBenchmark;
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
            aaBenchmark,
            visibilityBenchmark,
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
    const uint32_t benchmarkFamilyCount =
        uint32_t(aaBenchmark.enabled) +
        uint32_t(visibilityBenchmark.benchmarkRequested) +
        uint32_t(g_VisibilityPerfCapture.Enabled()) +
        uint32_t(diagnosticCsmBenchmarkRequested) +
        uint32_t(svsmMotionBenchmarkRequested);
    if (benchmarkFamilyCount > 1u)
    {
        ReportCommandLineError(
            "Select only one AA, visibility, diagnostic CSM, or SVSM "
            "benchmark mode per launch.");
        return 1;
    }
    if (g_VisibilityPerfCapture.Enabled())
    {
        log::ConsoleApplicationMode();
        benchmarkCameraRequested = true;
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
        !deviceParams.startMaximized &&
        !benchmarkCameraRequested)
    {
        CenterWindowInMonitorWorkArea(
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
        if (g_VisibilityPerfCapture.Enabled())
        {
            uiData.ShowUI = false;
            uiData.EnablePbr = true;
            uiData.RenderMode = RendererMode::Deferred;
            if (!g_VisibilityPerfCapture.Options().HasVariant("taaprime"))
                uiData.AntiAliasing.enabled = false;
            else
                log::info(
                    "UVSR_PERF applied variant token: taaprime");
            uiData.MiniEngineTaaSharpenEnabled = false;

            ScreenSpaceVisibilitySettings& visibility =
                uiData.ScreenSpaceVisibility;
            ApplyScreenSpaceVisibilityQualityPreset(
                visibility, ScreenSpaceVisibilityQuality::High);
            visibility.enabled = true;
            visibility.estimator = VisibilityEstimator::UniformSolidAngle;
            visibility.resolution = VisibilityResolution::Full;
            visibility.sampling.maximumSampleCount = 20u;
            visibility.sampling.radius = 3.f;
            visibility.sampling.thickness = 0.5f;
            visibility.sampling.stepDistributionExponent = 2.f;
            visibility.ambientOcclusion.enabled = true;
            visibility.ambientOcclusion.strength = 1.f;
            visibility.ambientOcclusion.power = 1.f;
            visibility.indirectDiffuse.enabled = true;
            visibility.indirectDiffuse.limitBounces = true;
            visibility.indirectDiffuse.bounceCount = 1u;
            visibility.indirectDiffuse.intensity = 1.f;
            visibility.reconstruction.temporalEnabled = false;
            visibility.reconstruction.spatialEnabled = false;

            const VisibilityPerfCaptureOptions& perf =
                g_VisibilityPerfCapture.Options();
            if (perf.HasVariant("performance16"))
            {
                ApplyVisibilityBufferPrecisionPreset(
                    visibility.performance.bufferPrecision, true, true);
                log::info(
                    "UVSR_PERF applied variant token: performance16");
            }
            if (perf.HasVariant("performance32"))
            {
                ApplyVisibilityBufferPrecisionPreset(
                    visibility.performance.bufferPrecision, false, false);
                log::info(
                    "UVSR_PERF applied variant token: performance32");
            }
            if (perf.HasVariant("toroidal"))
            {
                visibility.sampling.scheduler =
                    VisibilitySampleScheduler::ToroidalBlueNoiseRankField;
                log::info("UVSR_PERF applied variant token: toroidal");
            }
            if (perf.HasVariant("timersoff"))
            {
                log::info("UVSR_PERF applied variant token: timersoff");
            }
            if (perf.HasVariant("factory"))
            {
                log::info("UVSR_PERF applied variant token: factory");
            }
            if (perf.isolateVisibility)
            {
                uiData.ShowEnvironmentBackground = false;
                uiData.EnableDiffuseIbl = false;
                uiData.EnableSpecularIbl = false;
            }
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
        if (diagnosticCsmBenchmarkRequested)
        {
            // Apply the complete matched CSM comparison state before the first
            // rendered frame. This avoids spending the laptop's short thermal
            // window configuring unrelated renderer features through the UI.
            uiData.EnablePbr = true;
            uiData.RenderMode = RendererMode::Deferred;
            uiData.AntiAliasing.enabled = false;
            uiData.MiniEngineTaaSharpenEnabled = false;
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
            uiData.AntiAliasing.enabled = false;
            uiData.MiniEngineTaaSharpenEnabled = false;
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
            aaBenchmark,
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
            uiData);

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
                visibilityBenchmark.autoClose);
            if (!runMessageLoop)
                g_VisibilityBenchmarkFailed = true;
        }

        deviceManager->AddRenderPassToBack(demo.get());
        deviceManager->AddRenderPassToBack(gui.get());

        if (runMessageLoop)
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

    if (g_VisibilityBenchmarkFailed)
        return 1;
    if (g_VisibilityPerfCapture.Enabled() &&
        (!g_VisibilityPerfCapture.Complete() ||
            g_VisibilityPerfCapture.Failed()))
    {
        return 1;
    }
	
	return 0;
}
