#pragma once

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
#include <fstream>
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
#include <stdexcept>
#include <system_error>
#include <thread>
#include <tuple>
#include <iterator>
#include <utility>
#include <Windows.h>
#include <dwmapi.h>
#include <ShlObj.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <donut/core/vfs/VFS.h>
#include <donut/engine/BindingCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/DescriptorTableManager.h>
#include <donut/engine/Scene.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/ThreadPool.h>
#include <donut/engine/View.h>
#include <donut/render/DeferredLightingPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/GeometryPasses.h>
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
#include "msaa_raster_topology.h"
#include "msaa_visibility_resolve.h"
#include "image_based_lighting_background_pass.h"
#include "image_based_lighting_environment.h"
#include "image_based_lighting_shared.h"
#include "lighting_accumulation_pass.h"
#include "adaptive_sync.h"
#include "agx_tone_mapping_pass.h"
#include "auto_exposure.h"
#include "camera_collision.h"
#include "camera_controllers.h"
#include "command_line_options.h"
#include "denoising_pass.h"
#include "denoising_settings.h"
#include "build_identity.h"
#include "engine_diagnostics.h"
#include "engine_startup.h"
#include "fast_approximate_aa.h"
#include "flashlight.h"
#include "gpu_capabilities.h"
#include "directional_ray_visibility_pass.h"
#include "noise_texture_library.h"
#include "path_tracing_pass.h"
#include "path_tracing_settings.h"
#include "pixel_zoom.h"
#include "ray_traced_flashlight_shadows.h"
#include "ray_traced_sky_visibility.h"
#if defined(UVSR_BUILD_TESTING)
#include "retained_runtime_diagnostic.h"
#endif
#include "renderer_statistics.h"
#include "renderer_geometry_passes.h"
#include "renderer_common_passes.h"
#include "renderer_log.h"
#include "renderer_nvrhi_message_callback.h"
#include "renderer_pixel_readback.h"
#include "renderer_scene_load_worker.h"
#include "renderer_scene_retirement.h"
#include "renderer_shader_factory.h"
#include "renderer_targets.h"
#include "renderer_texture_bmp.h"
#include "scene_catalog.h"
#include "scene_loading.h"
#include "scene_light_names.h"
#include "screen_space_visibility.h"
#include "settings_snapshot.h"
#include "settings_snapshot_decoder.h"
#include "settings_snapshot_transaction.h"
#include "temporal_aa.h"
#include "ui_animation.h"
#include "ui_command_layout.h"
#include "ui_commands.h"
#include "ui_font_family.h"
#include "ui_settings_command_catalog.h"
#include "ui_skin.h"
#include "ui_performance_timing_rows.h"
#include "uvsr_application.h"
#include "uvsr_command_line.h"
#include "uvsr_settings_commands.h"
#include "world_space_representation.h"
#include "windows_executable_path.h"

using namespace donut;
using namespace donut::math;
#include "renderer_gpu_contract.h"
using namespace donut::app;
using namespace donut::vfs;
using namespace donut::engine;
using namespace donut::render;
using namespace uvsr;

inline bool g_RestartRequested = false;
inline int g_RestartAdapterIndex = -1;
inline bool g_StartupSettingsSnapshotFailed = false;
#if defined(UVSR_BUILD_TESTING)
inline bool g_VerifySettingsContractRequested = false;
inline int g_VerifySettingsContractResult = 1;
inline bool g_VerifyRetainedRuntimeRequested = false;
inline int g_VerifyRetainedRuntimeResult = 1;
inline bool g_RuntimeDebugValidationRequested = false;
#endif

#if defined(UVSR_BUILD_TESTING)
struct RetainedRuntimeCameraPose
{
    float3 position = 0.f;
    float3 direction = float3(0.f, 0.f, -1.f);
    float3 up = float3(0.f, 1.f, 0.f);
    float3 right = float3(1.f, 0.f, 0.f);
    float verticalFovDegrees = 60.f;
};
#endif

constexpr float DefaultSunIrradiance = 8.f;
constexpr float DefaultSunAngularSizeDegrees = 0.2f;
constexpr float DefaultFlashlightRayBiasMeters = 0.002f;

inline void HashLightingHistoryBytes(
    uint64_t& signature,
    const void* data,
    size_t size)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t byteIndex = 0; byteIndex < size; ++byteIndex)
    {
        signature ^= uint64_t(bytes[byteIndex]);
        signature *= 1099511628211ull;
    }
}

template<typename T>
inline void HashLightingHistoryValue(
    uint64_t& signature,
    const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    HashLightingHistoryBytes(signature, &value, sizeof(value));
}

inline bool CopyBmpToClipboard(const std::filesystem::path& fileName)
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

inline bool RestartCurrentProcess()
{
    std::wstring commandLine = GetCommandLineW();
    if (g_RestartAdapterIndex >= 0)
    {
        // ParseUvsrCommandLine applies options from left to right, so appending
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
        uvsr::log::error("Failed to restart UVSR (Win32 error %lu)", GetLastError());
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

inline MsaaRasterTopology ResolveSupportedMsaaTopology(
    nvrhi::IDevice* device,
    uint32_t requestedSampleCount)
{
    const MsaaRasterCandidates candidates =
        GetExactMsaaRasterCandidates(requestedSampleCount);
    if (candidates.count == 0u)
        return {};
    if (requestedSampleCount == 1u)
        return candidates.values[0];
    if (!device)
        return {};
    if (device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D12)
        return {};

    ID3D12Device* nativeDevice =
        device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
    if (!nativeDevice)
        return {};

    struct MsaaTopologyCache
    {
        ID3D12Device* device = nullptr;
        uint32_t requestedSampleCount = 0u;
        MsaaRasterTopology topology;
    };
    static MsaaTopologyCache cache;
    if (cache.device == nativeDevice &&
        cache.requestedSampleCount == requestedSampleCount)
    {
        return cache.topology;
    }
    const auto cacheResolution =
        [&](MsaaRasterTopology topology)
    {
        cache.device = nativeDevice;
        cache.requestedSampleCount = requestedSampleCount;
        cache.topology = topology;
        return topology;
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
        uvsr::log::warning(
            "Exact MSAA is unavailable because Donut found no compatible "
            "G-buffer depth format");
        return cacheResolution({});
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

    for (uint32_t index = 0u; index < candidates.count; ++index)
    {
        const MsaaRasterTopology candidate = candidates.values[index];
        if (supportsAllocatedFormats(candidate.rasterSampleCount))
        {
            if (candidate.linearResolutionScale > 1u)
            {
                uvsr::log::warning(
                    "Native %ux MSAA is unavailable; using %ux raster MSAA "
                    "at %ux linear resolution for exactly %u samples",
                    candidate.presentationSampleCount,
                    candidate.rasterSampleCount,
                    candidate.linearResolutionScale,
                    candidate.TotalSampleCount());
            }
            return cacheResolution(candidate);
        }
    }

    uvsr::log::error(
        "The active adapter cannot provide an exact %ux UVSR MSAA topology",
        requestedSampleCount);
    return cacheResolution({});
}

inline void ApplyPbrMaterialParameters(Material& material, float ior = 1.5f)
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

class UvsrSceneViewer : public ApplicationBase
{
private:
    typedef ApplicationBase Super;

    enum class MaterialPickPurpose
    {
        None,
        FocusCameraAtCursor,
        RefreshMaterialDrawerSelection
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
        Denoising,
        TemporalAA,
        TemporalAAPipelines,
        FastApproximateAA,
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
	RendererSceneLoadWorker             m_SceneLoadWorker;
	RendererSceneRetirement             m_SceneRetirement;
    std::shared_ptr<IFileSystem>        m_PendingSceneFileSystem;
    std::filesystem::path               m_PendingSceneFileName;
    bool                                m_SceneRetirementPending = false;
    bool                                m_RendererSceneLoaded = false;
    bool                                m_HasRendererSceneResources = false;
    std::string                         m_SceneLoadFailure;
	std::vector<std::pair<std::shared_ptr<Material>, Material>> m_OriginalMaterials;
    std::shared_ptr<ShaderFactory>      m_ShaderFactory;
    std::shared_ptr<uvsr::RendererShaderFactory>
                                        m_RendererShaderFactory;
    std::shared_ptr<uvsr::RendererCommonPasses>
                                        m_RendererCommonPasses;
    nvrhi::BindingLayoutHandle          m_BindlessLayout;
    std::shared_ptr<DescriptorTableManager>
                                        m_DescriptorTable;
    std::shared_ptr<DirectionalLight>   m_SunLight;
    std::shared_ptr<SpotLight>           m_Flashlight;
    std::shared_ptr<SceneGraphNode>     m_FlashlightNode;
    float                               m_FlashlightTransition = 0.f;
    float                               m_FlashlightSwayTime = 0.f;
    float3                              m_FlashlightAimDirection =
                                            float3(0.f, 0.f, -1.f);
    float3                              m_FlashlightResolvedPosition = 0.f;
    float3                              m_FlashlightResolvedDirection =
                                            float3(0.f, 0.f, -1.f);
    float3                              m_FlashlightResolvedRight =
                                            float3(1.f, 0.f, 0.f);
    float3                              m_FlashlightDesiredPosition = 0.f;
    float                               m_FlashlightCollisionRadius = 0.f;
    bool                                m_FlashlightCollisionInitialized = false;
    bool                                m_FlashlightAimInitialized = false;
    bool                                m_FlashlightPoseValid = false;
    FlashlightMotionSettings            m_FlashlightMotionSettings;
    bool                                m_FlashlightMotionSettingsValid = false;
    float3                              m_FlashlightCameraPosition = 0.f;
    float3                              m_FlashlightCameraDirection =
                                            float3(0.f, 0.f, -1.f);
    float3                              m_FlashlightCameraUp =
                                            float3(0.f, 1.f, 0.f);
    bool                                m_FlashlightCameraPoseValid = false;
    float3                              m_FlashlightSubmittedPosition = 0.f;
    float3                              m_FlashlightSubmittedDirection =
                                            float3(0.f, 0.f, -1.f);
    float3                              m_FlashlightSubmittedRight =
                                            float3(1.f, 0.f, 0.f);
    bool                                m_FlashlightSubmittedPoseValid = false;
    std::vector<std::shared_ptr<Light>> m_SceneLightsWithoutFlashlight;
    std::vector<std::shared_ptr<Light>> m_EditableLights;
    std::shared_ptr<InstancedOpaqueDrawStrategy> m_OpaqueDrawStrategy;
    std::unique_ptr<RenderTargets>      m_RenderTargets;
    std::unique_ptr<RendererGeometryPass>
                                        m_GBufferGeometryPass;
    std::unique_ptr<PbrDeferredLightingPass> m_PbrDeferredLightingPass;
    std::unique_ptr<MsaaVisibilityResolvePass>
        m_MsaaVisibilityResolvePass;
    std::unique_ptr<ImageBasedLightingEnvironment>
                                        m_ImageBasedLightingEnvironment;
    std::unique_ptr<ImageBasedLightingBackgroundPass>
                                        m_ImageBasedLightingBackgroundPass;
    std::unique_ptr<AutoExposurePass>   m_AutoExposurePass;
    std::unique_ptr<AgxToneMappingPass> m_AgxToneMappingPass;
    std::unique_ptr<DirectionalRayVisibilityPass>
                                        m_DirectionalRayVisibilityPass;
    std::unique_ptr<RayTracedFlashlightShadowPass>
                                        m_RayTracedFlashlightShadowPass;
    std::unique_ptr<RayTracedSkyVisibilityPass>
                                        m_RayTracedSkyVisibilityPass;
    std::unique_ptr<WorldSpaceRepresentation>
                                        m_WorldSpaceRepresentation;
    std::unique_ptr<PathTracingPass>     m_PathTracingPass;
    std::unique_ptr<LightingAccumulationPass>
                                        m_LightingAccumulationPass;
    std::unique_ptr<ScreenSpaceVisibilityPass> m_ScreenSpaceVisibilityPass;
    std::unique_ptr<NoiseTextureLibrary> m_NoiseTextureLibrary;
    std::unique_ptr<DenoisingPass>       m_DenoisingPass;
    std::unique_ptr<TemporalAAPass> m_TemporalAAPass;
    std::unique_ptr<FastApproximateAAPass>
                                        m_FastApproximateAAPass;
    std::unique_ptr<RendererGeometryPass>
                                        m_MaterialIdGeometryPass;
    std::unique_ptr<uvsr::RendererPixelReadback>
                                        m_PixelReadback;

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
    BindingCache                        m_BindingCache;
    uint64_t                            m_SubmittedMainViewTriangles = 0u;

    float                               m_CameraVerticalFov = 60.f;
    float                               m_SceneDiagonal = 100.f;
    float                               m_CameraCollisionRadius = 0.1f;
    float                               m_FrameDeltaSeconds = 0.f;
    uint2                               m_PickPosition = 0u;
    MaterialPickPurpose                 m_MaterialPickPurpose =
        MaterialPickPurpose::None;
    const Scene*                        m_MaterialPickScene = nullptr;
    uint64_t                            m_AntiAliasingPhase = 0u;
    uint64_t                            m_LightingHistoryEpoch = 1u;
    uint64_t                            m_LastLightingViewSignature = 0u;
    uint64_t                            m_LastLightingDomainSignature = 0u;
    bool                                m_HasLightingHistorySignatures = false;
    bool                                m_LightingHistoryChangedByViewOnly =
                                            false;
    uint64_t                            m_ScreenSpaceVisibilityPhase = 0u;
#if defined(UVSR_BUILD_TESTING)
    bool                                m_ScreenSpaceVisibilityDispatchedThisFrame =
                                            false;
#endif
    bool                                m_DirectionalRayVisibilityDispatchedThisFrame =
                                            false;
    bool                                m_PathTransportDispatchedThisFrame =
                                             false;
    bool                                m_ReportedPathTransportFailure = false;
    SelectedLightingTransportState      m_SelectedLightingTransportState =
        SelectedLightingTransportState::RayMarching;
    bool                                m_RayTracedFlashlightShadowDispatchedThisFrame =
                                            false;
#if defined(UVSR_BUILD_TESTING)
    bool                                m_FlashlightLightingSubmittedThisFrame =
                                            false;
#endif
    bool                                m_ShadowDenoisingDispatchedThisFrame =
                                            false;
    bool                                m_RayTracedFlashlightShadowContributedLastFrame =
                                            false;
    uint64_t                            m_RayTracedFlashlightShadowPhase = 0u;
    uint64_t                            m_RayTracedSkyVisibilityPhase = 0u;
    bool                                m_RayTracedSkyVisibilityContributedLastFrame =
                                            false;
    bool                                m_RayTracedSkyVisibilityDispatchedThisFrame =
                                            false;
    bool                                m_RayTracedSkyVisibilityDenoisedThisFrame =
                                            false;
    bool                                m_AmbientOcclusionDenoisedThisFrame =
                                            false;
    bool                                m_DiffuseIlluminationDenoisedThisFrame =
                                            false;
    bool                                m_AutoExposureDispatchedThisFrame =
                                             false;
    bool                                m_VisibilityLightingPreparationDispatchedThisFrame =
                                             false;
#if defined(UVSR_BUILD_TESTING)
    bool                                m_LightingAccumulationCommittedThisFrame = false;
    bool                                m_RuntimeOutputCaptureRequested = false;
    std::optional<RuntimeOutputEvidence> m_RuntimeOutputEvidence;
    std::filesystem::path               m_RuntimeOutputCapturePath;
    nvrhi::StagingTextureHandle         m_RuntimeLinearReadback;
    bool                                m_RuntimeLinearReadbackQueued = false;
#endif
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
    UIData&                             m_ui;

    void AdvanceRendererTimers();
    void BeginRendererStage(RendererTimingStage stage);
    void EndRendererStage(RendererTimingStage stage);
    void CompleteRendererTimerFrame();
    void InvalidateRendererStageTiming(RendererTimingStage stage);

    void FailOpenRendererFrame(const char* passName);

    void SubmitPendingRendererPreparationFrame();

public:

    bool ShouldAnimateUnfocused() override;

    bool ShouldRenderUnfocused() override;

    UvsrSceneViewer(
        DeviceManager* deviceManager,
        UIData& ui,
        const std::string& sceneName);

    ~UvsrSceneViewer() override;

	std::shared_ptr<vfs::IFileSystem> GetRootFs() const;

    BaseCamera& GetActiveCamera() const;

    void SetCameraMode(CameraMode mode);


    bool ToggleFlashlight();


    void ResetAntiAliasingState();

    void ApplyCameraPose(
        float3 position,
        float3 direction,
        float3 up,
        float3 right,
        float verticalFovDegrees);

    void ApplySceneInitialCamera(const SceneInitialCamera& preset);

    const std::vector<SceneCatalogEntry>& GetAvailableScenes() const;

    std::filesystem::path const& GetSceneDir() const;

    std::string GetCurrentSceneName() const;

    std::string GetCurrentSceneDisplayName() const;

    [[nodiscard]] bool IsSceneLoading() const;

    [[nodiscard]] bool IsSceneLoaded() const;

    void StartPendingSceneLoad();

    void BeginLoadingScene(
        std::shared_ptr<IFileSystem> fileSystem,
        const std::filesystem::path& sceneFileName) override;

    void SetCurrentSceneName(const std::string& sceneName);

    void RetryCurrentSceneLoad();

    [[nodiscard]] bool HasSceneLoadFailure() const noexcept;

    [[nodiscard]] const std::string& GetSceneLoadFailure() const noexcept;

    void ResetAllRendererSettings();

    void SynchronizeCameraInput();

    static CameraCollisionWorld BuildCameraCollisionWorld(
        const Scene& scene,
        float collisionRadius);

    virtual bool KeyboardUpdate(int key, int scancode, int action, int mods) override;

    virtual bool MousePosUpdate(double xpos, double ypos) override;

    virtual bool MouseButtonUpdate(int button, int action, int mods) override;

    virtual bool MouseScrollUpdate(double xoffset, double yoffset) override;

    void ResetFlashlightMotion();

    static bool SameFlashlightVector(
        const float3& left,
        const float3& right);

    void ApplyFlashlightPresentation();

    void UpdateFlashlightAnimation(float elapsedSeconds);

    static float3 ClampFlashlightAimLag(
        float3 candidate,
        float3 target);

    static float3 InterpolateFlashlightAim(
        float3 current,
        float3 target,
        float blend);

    void UpdateFlashlightMotion(float elapsedSeconds);

    static void SetFlashlightDirectionAndRoll(
        const std::shared_ptr<SpotLight>& light,
        const float3& direction,
        const float3& right);

    void UpdateFlashlightTransform();

    void AttachFlashlightToScene();

    virtual void Animate(float fElapsedTimeSeconds) override;


    virtual void SceneUnloading() override;

    virtual bool LoadScene(std::shared_ptr<IFileSystem> fs, const std::filesystem::path& fileName) override;

    virtual void SceneLoaded() override;

    void CompleteSceneActivation();

    void SetWhiteWorldMode(WhiteWorldMode mode);

    static std::shared_ptr<SceneGraphNode> FindDescendantByName(
        const std::shared_ptr<SceneGraphNode>& node,
        const std::string& name);

    void PointThirdPersonCameraAt(
        const std::shared_ptr<SceneGraphNode>& node,
        float distanceScale = 1.f,
        bool resetOrientation = false);

    std::shared_ptr<TextureCache> GetTextureCache();

    [[nodiscard]] bool IsSceneBusy() const;

    [[nodiscard]] bool IsSceneGpuUploadPending() const;

    std::shared_ptr<Scene> GetScene();

    void SetMaterialDrawerVisible(bool visible);

    const Material* GetOriginalMaterial(
        const std::shared_ptr<Material>& material) const;

    void NotifyMaterialCommandChanged(
        const std::shared_ptr<Material>& material);

    void SynchronizeAntiAliasingSettings();

    bool SetupView();

    void CaptureCurrentViewForMotionVectors();

    [[nodiscard]] nvrhi::ITexture*
        GetPresentationAaInitializationSource() const;

    void CreateFastApproximateAAPass();

    void CreateTemporalAAPass(bool deferPipelineCreation = false);

    [[nodiscard]] std::unique_ptr<RendererGeometryPass>
        CreateGeometryPass(RendererGeometryOutput output);

    [[nodiscard]] bool RenderGeometry(
        RendererGeometryPass& pass,
        nvrhi::IFramebuffer* framebuffer,
        const IView* view,
        const IView* previousView,
        const char* marker);

    void EnsureMsaaVisibilityResolvePass(
        bool deferPipelineCreation = false);

    void RefreshAntiAliasingTargetPasses();

    void BeginRenderPassPreparation(bool waitForIbl);

    bool ProcessRenderPassPreparationStep();

    void CreateRenderPasses();

    void EnsurePathTracingPass();

    void EnsureDirectionalRayVisibilityPass();

    void EnsureRayTracedFlashlightShadowPass();

    void EnsureRayTracedSkyVisibilityPass();

    void UpdateImageBasedLighting(nvrhi::ICommandList* commandList);

    void RecordLoadingPresentationFrame();

    virtual void RenderSplashScreen(nvrhi::IFramebuffer* framebuffer) override;

    bool PrepareLoadingRenderTargets(nvrhi::IFramebuffer* framebuffer);

    void RenderSceneGpuUploadFrame(nvrhi::IFramebuffer* framebuffer);

    void Render(nvrhi::IFramebuffer* framebuffer) override;

    virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override;

    std::shared_ptr<ShaderFactory> GetShaderFactory();

    std::shared_ptr<uvsr::RendererShaderFactory>
    GetRendererShaderFactory();

    std::shared_ptr<uvsr::RendererCommonPasses>
    GetRendererCommonPasses();

    void InvalidateLightingAccumulationHistory();

    void SynchronizeLightingAccumulationHistory(
        uint32_t width,
        uint32_t height,
        const std::vector<std::shared_ptr<Light>>& submittedLights,
        bool worldRepresentationReady,
        bool sceneContentChanged,
        const NoiseSettings& visibilityNoiseSettings,
        const NoiseSettings& skyNoiseSettings,
        const NoiseSettings& flashlightNoiseSettings,
        bool screenSpaceVisibilityRequested,
        bool screenSpaceVisibilityReady,
        bool directionalRayVisibilitySelected,
        bool directionalRayVisibilityReady,
        bool rayTracedFlashlightShadowSelected,
        bool flashlightStochasticRequested,
        bool rayTracedFlashlightShadowReady,
        bool rayTracedSkyVisibilitySelected,
        bool skyVisibilityStochasticRequested,
        bool rayTracedSkyVisibilityReady);

    void ResetImageBasedLightingHistory();

    void ResetNoiseSamplingHistory(
        bool visibility,
        bool shadows,
        bool skyVisibility,
        bool flashlight);

    [[nodiscard]] uint64_t GetNoiseTextureResidentBytes() const;

    const ScreenSpaceVisibilityTimings* GetScreenSpaceVisibilityTimings() const;

    [[nodiscard]] uint32_t GetActiveRasterSampleCount() const;

#if defined(UVSR_BUILD_TESTING)
    void RequestRuntimeOutputEvidence(
        size_t caseIndex,
        std::string_view caseName);

    [[nodiscard]] std::optional<RuntimeOutputEvidence>
        ConsumeRuntimeOutputEvidence();

    void NudgeCameraForRuntimeDiagnostic();

    [[nodiscard]] RetainedRuntimeCameraPose
        CaptureRetainedRuntimeCameraPose() const;

    void RestoreRetainedRuntimeCameraPose(
        const RetainedRuntimeCameraPose& pose);
#endif

    bool HasPrimaryDirectionalLight() const;

    bool HasDirectionalRayVisibilityHardwareSupport() const;

    bool SupportsDirectionalRayVisibility() const;

    bool HasRayTracedFlashlightShadowHardwareSupport() const;

    bool HasRayTracedSkyVisibilityHardwareSupport() const;

    bool SupportsRayTracedSkyVisibility() const;

    const WorldSpaceRepresentationStatus&
        GetWorldSpaceRepresentationStatus() const;

    const PathTracingCapabilities& GetPathTracingCapabilities() const;

    uint64_t GetPathTracingCenterPixelAcceptedSampleCount() const noexcept;

    SelectedLightingTransportState GetSelectedLightingTransportState()
        const noexcept;

    PathTracingSceneDomainStatus GetPathTracingSceneDomainStatus() const;

    void InvalidateWorldSpaceRepresentation(
        WorldSpaceRepresentationInvalidation invalidation);

    std::shared_ptr<DirectionalLight> GetPrimaryDirectionalLight() const;

    const std::vector<std::shared_ptr<Light>>& GetEditableLights() const;

    bool IsFlashlight(const std::shared_ptr<Light>& light) const;

    const TemporalAATimings* GetTemporalAATimings() const;

    [[nodiscard]] uint64_t GetSubmittedMainViewTriangles() const;

    [[nodiscard]] const RendererTimings& GetRendererTimings() const;

    [[nodiscard]] bool DidDispatchDirectionalRayVisibilityThisFrame() const;

#if defined(UVSR_BUILD_TESTING)
    [[nodiscard]] bool DidDispatchScreenSpaceVisibilityThisFrame() const;

    [[nodiscard]] bool DidDispatchRayTracedFlashlightShadowThisFrame() const;

    [[nodiscard]] bool DidSubmitFlashlightLightingThisFrame() const;

    [[nodiscard]] bool DidCommitLightingAccumulationThisFrame() const;

    [[nodiscard]] bool DidDispatchShadowDenoisingThisFrame() const;

    [[nodiscard]] bool DidDispatchSkyDenoisingThisFrame() const;
#endif

    [[nodiscard]] bool DidDispatchRayTracedSkyVisibilityThisFrame() const;

    [[nodiscard]] bool IsRendererStageActiveThisFrame(
        RendererTimingStage stage) const;

};

namespace uvsr_detail
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
        uint32_t cornerMask = UiBackdropCornersAll;
        float2 padding;
    };

    static_assert(sizeof(BackdropBlurConstants) == 80u);

    static std::vector<UiBackdropExclusionRect>
        ResolveBackdropCompositeRegions(
            const UiBackdropExclusionRect& panel,
            const std::vector<UiBackdropExclusionRect>& exclusions)
    {
        std::vector<UiBackdropExclusionRect> regions = { panel };
        for (const UiBackdropExclusionRect& exclusion : exclusions)
        {
            std::vector<UiBackdropExclusionRect> nextRegions;
            nextRegions.reserve(regions.size() * 4u);
            for (const UiBackdropExclusionRect& region : regions)
            {
                const UiBackdropExclusionRect intersection = {
                    std::max(region.minX, exclusion.minX),
                    std::max(region.minY, exclusion.minY),
                    std::min(region.maxX, exclusion.maxX),
                    std::min(region.maxY, exclusion.maxY)
                };
                if (intersection.maxX <= intersection.minX ||
                    intersection.maxY <= intersection.minY)
                {
                    nextRegions.push_back(region);
                    continue;
                }

                const auto appendRegion =
                    [&nextRegions](
                        float minX,
                        float minY,
                        float maxX,
                        float maxY)
                    {
                        if (maxX > minX && maxY > minY)
                        {
                            nextRegions.push_back({
                                minX,
                                minY,
                                maxX,
                                maxY
                            });
                        }
                    };
                appendRegion(
                    region.minX,
                    region.minY,
                    region.maxX,
                    intersection.minY);
                appendRegion(
                    region.minX,
                    intersection.maxY,
                    region.maxX,
                    region.maxY);
                appendRegion(
                    region.minX,
                    intersection.minY,
                    intersection.minX,
                    intersection.maxY);
                appendRegion(
                    intersection.maxX,
                    intersection.minY,
                    region.maxX,
                    intersection.maxY);
            }
            regions = std::move(nextRegions);
            if (regions.empty())
                break;
        }
        return regions;
    }

    class BackdropBlurPass
    {
    private:
        nvrhi::DeviceHandle m_Device;
        std::shared_ptr<uvsr::RendererCommonPasses> m_CommonPasses;
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
                    0, m_CommonPasses->LinearClampSampler()),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_DownsampleTexture)
            };
            m_HorizontalBindingSet = m_Device->createBindingSet(
                bindingSetDesc, m_BindingLayout);

            bindingSetDesc.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    0, m_ConstantBuffer),
                nvrhi::BindingSetItem::Sampler(
                    0, m_CommonPasses->LinearClampSampler()),
                nvrhi::BindingSetItem::Texture_SRV(
                    0, m_HorizontalBlurTexture)
            };
            m_CompositeBindingSet = m_Device->createBindingSet(
                bindingSetDesc, m_BindingLayout);

            nvrhi::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.primType = nvrhi::PrimitiveType::TriangleStrip;
            pipelineDesc.VS =
                m_CommonPasses->FullscreenVertexShader();
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
            const std::shared_ptr<uvsr::RendererShaderFactory>& shaderFactory,
            std::shared_ptr<uvsr::RendererCommonPasses> commonPasses)
            : m_Device(device)
            , m_CommonPasses(std::move(commonPasses))
        {
            m_CommandList = device->createCommandList();

            std::vector<uvsr::RendererShaderMacro> shaderMacros;
            shaderMacros.emplace_back("COMPOSITE", "0");
            m_BlurPixelShader = shaderFactory->CreateShader(
                "uvsr/backdrop_blur_ps.hlsl",
                "main",
                &shaderMacros,
                nvrhi::ShaderType::Pixel);
            shaderMacros[0] =
                uvsr::RendererShaderMacro("COMPOSITE", "1");
            m_CompositePixelShader = shaderFactory->CreateShader(
                "uvsr/backdrop_blur_ps.hlsl",
                "main",
                &shaderMacros,
                nvrhi::ShaderType::Pixel);
            shaderMacros[0] =
                uvsr::RendererShaderMacro("COMPOSITE", "2");
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
                uvsr::RendererMaxConstantBufferVersions;
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

        [[nodiscard]] bool Render(
            nvrhi::IFramebuffer* framebuffer,
            float blurPixels,
            const std::array<
                UiBackdropRect,
                UiBackdropRectCount>& backdropRects)
        {
            const float clampedBlurPixels =
                std::clamp(blurPixels, 0.f, 24.f);
            const bool hasVisibleBackdrop = std::any_of(
                backdropRects.begin(),
                backdropRects.end(),
                [](const UiBackdropRect& rect)
                {
                    return
                        rect.composite &&
                        rect.visible &&
                        rect.maxX > rect.minX &&
                        rect.maxY > rect.minY &&
                        rect.opacity > 0.f;
                });
            const bool renderBackdrop =
                clampedBlurPixels > 0.f &&
                hasVisibleBackdrop;
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
            if (!renderBackdrop && !hasVisibleShadow)
            {
                return true;
            }
            if (!EnsureResources(framebuffer))
            {
                return false;
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
                if (!m_CommonPasses ||
                    !m_CommonPasses->BlitTexture(
                        m_CommandList,
                        m_DownsampleFramebuffer,
                        framebufferTexture) ||
                    m_CommonPasses->HasBlitPipelineFailure())
                {
                    m_CommandList->endMarker();
                    m_CommandList->close();
                    return false;
                }

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
                constants.cornerMask = backdropRect.cornerMask;
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
                    if (!backdropRect.composite ||
                        !backdropRect.visible ||
                        backdropRect.opacity <= 0.f)
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
                    constants.cornerMask = backdropRect.cornerMask;
                    constants.opacity = backdropRect.opacity;
                    constants.shadowBlur = 0.f;
                    constants.shadowOpacity = 0.f;
                    constants.shadowOffsetY = 0.f;
                    m_CommandList->writeBuffer(
                        m_ConstantBuffer,
                        &constants,
                        sizeof(constants));

                    const UiBackdropExclusionRect panel = {
                        minX,
                        minY,
                        maxX,
                        maxY
                    };
                    const std::vector<UiBackdropExclusionRect> regions =
                        ResolveBackdropCompositeRegions(
                            panel,
                            backdropRect.compositeExclusions);
                    for (const UiBackdropExclusionRect& region : regions)
                    {
                        const nvrhi::Viewport regionViewport(
                            region.minX,
                            region.maxX,
                            region.minY,
                            region.maxY,
                            0.f,
                            1.f);
                        nvrhi::GraphicsState compositeState;
                        compositeState.pipeline = m_CompositePipeline;
                        compositeState.framebuffer = framebuffer;
                        compositeState.bindings = {
                            m_CompositeBindingSet
                        };
                        compositeState.viewport.addViewport(
                            regionViewport);
                        compositeState.viewport.addScissorRect(
                            nvrhi::Rect(regionViewport));
                        m_CommandList->setGraphicsState(compositeState);
                        m_CommandList->draw(drawArguments);
                    }
                }
            }

            m_CommandList->endMarker();
            m_CommandList->close();
            m_Device->executeCommandList(m_CommandList);
            return true;
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
        std::shared_ptr<uvsr::RendererCommonPasses> m_CommonPasses;
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
            pipelineDesc.VS =
                m_CommonPasses->FullscreenVertexShader();
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
            const std::shared_ptr<uvsr::RendererShaderFactory>& shaderFactory,
            std::shared_ptr<uvsr::RendererCommonPasses> commonPasses)
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
                uvsr::RendererMaxConstantBufferVersions;
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











using namespace uvsr_detail;

class RequiredUiFontStartupError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

class UIRenderer : public ImGui_Renderer
{
private:
    enum class StatisticsEffect : int
    {
        CompleteRenderer,
        SceneSetup,
        Geometry,
        PathTransport,
        DirectLighting,
        Visibility,
        Shadows,
        TemporalReconstructive,
        FastApproximate,
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
        ImVec4 drawerHeaderText = ImVec4(0.94f, 0.95f, 0.98f, 1.f);
        ImVec4 drawerBackground;
        ImVec4 drawerFrame;
        ImVec4 drawerFrameHovered;
        ImVec4 drawerFrameActive;
        ImVec4 outlineTop;
        ImVec4 outlineBottom;
        ImVec4 panelBodySurface;
        ImVec4 colorPickerSurface;
        ImVec4 panelInsetFrame;
        ImVec4 settingsTitleSurface;
        ImVec4 settingsTitleText = ImVec4(0.94f, 0.95f, 0.98f, 1.f);
        ImVec4 actionButton;
        ImVec4 actionButtonHovered;
        ImVec4 actionButtonActive;
        ImVec4 actionButtonText = ImVec4(0.94f, 0.95f, 0.98f, 1.f);
        float controlDisabledAlpha = 0.60f;
        float drawerRounding = 4.f;
        float backdropShadowBlur = UiPanelShadowBlurPixels;
        float backdropShadowOpacity = UiPanelShadowOpacity;
        float backdropShadowOffsetY = UiPanelShadowOffsetYPixels;
        ImVec4 errorText;
        // Match MaterialEditor's explicitly supplied filename accent so
        // success has one deliberate product color everywhere.
        ImVec4 successText;
        bool drawControlOutlines = true;
        bool sceneTranslucentHeaders = false;
    };

    struct FrontEllipsisText
    {
        std::string display;
        bool truncated = false;
    };

    [[nodiscard]] static FrontEllipsisText FormatFrontEllipsisUtf8(
        std::string_view source,
        size_t maximumCodePoints);

    struct StatSnapshot
    {
        int width = 0;
        int height = 0;
        uint64_t submittedTriangles = 0u;
        double frameTimeSeconds = 0.0;
        ScreenSpaceVisibilityTimings visibilityTimings;
        TemporalAATimings temporalAATimings;
        bool hasVisibilityTimings = false;
        bool hasTemporalAATimings = false;
    };

    std::shared_ptr<UvsrSceneViewer> m_app;

    struct UiFontFaces
    {
        std::shared_ptr<app::RegisteredFont> regular;
        std::shared_ptr<app::RegisteredFont> body;
        std::shared_ptr<app::RegisteredFont> header;
    };

    std::array<UiFontFaces, UiFontFamilyCount> m_UiFontFaces;
    bool m_RequiredFontsReady = false;
    std::shared_ptr<engine::Light> m_SelectedLight;
    ImGuiID m_AdjustedSpaceFontBakedId = 0;
    float m_BaseSpaceAdvance = 0.f;
    ImGuiID m_AdjustedHeaderSpaceFontBakedId = 0;
    float m_BaseHeaderSpaceAdvance = 0.f;
    double m_DisplayedFrameTime = 0.0;
    double m_StatSnapshotElapsed = 0.0;
    double m_StatFrameTimeSum = 0.0;
    uint32_t m_StatFrameTimeCount = 0;
    std::array<std::string, 4> m_PerformanceStatValues;
    uvsr::PerformanceTimingRowRetention m_PerformanceTimingRows;
    ScreenSpaceVisibilityTimings m_DisplayedVisibilityTimings;
    TemporalAATimings m_DisplayedTemporalAATimings;
    std::deque<StatSnapshot> m_StatUpdateQueue;
    bool m_HasAppliedStatSnapshot = false;
    bool m_HasVisibilityStatSnapshot = false;
    bool m_HasTemporalAAStatSnapshot = false;
    bool m_WasSceneLoading = false;
    bool m_SceneLoadFailed = false;
    std::chrono::steady_clock::time_point m_SceneLoadCounterStart;
    std::string m_SceneLoadHistoryKey;
    SceneLoadTimingHistory m_AllSceneLoadTiming;
    std::unordered_map<std::string, SceneLoadTimingHistory>
        m_SceneLoadTimingByScene;

    [[nodiscard]] static std::filesystem::path
        GetSceneLoadTimingDatabasePath();

    void LoadSceneLoadTimingDatabase();

    void SaveSceneLoadTimingDatabase() const;
    std::unique_ptr<BackdropBlurPass> m_BackdropBlurPass;
    std::unique_ptr<PixelZoomPass> m_PixelZoomPass;
    uint32_t m_SettingsPanelMarginPixels =
        static_cast<uint32_t>(UiSpacingBasePixels * 4.f);
    float m_UiDisplayScale = 1.f;
    float m_SettingsAppearance = 0.f;
    PixelZoomMode m_RenderedPixelZoom = PixelZoomMode::Off;
    PixelZoomMode m_PendingPixelZoom = PixelZoomMode::Off;
    float m_PixelZoomVisibility = 0.f;
    float m_PixelZoomLevelTransition = 1.f;
    float m_MaterialDrawerAppearance = 0.f;
    bool m_MaterialDrawerPresentationForceClosed = false;
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
    enum class SettingsSnapshotApplyOrigin
    {
        None,
        Startup,
        Command
    };
    SettingsSnapshotController m_SettingsSnapshots;
    std::string m_StartupSettingsSnapshotCode;
    bool m_StartupSettingsSnapshotAttempted = false;
    bool m_SettingsSnapshotRestartHandoffAttempted = false;
    bool m_SettingsSnapshotRestartHandoffOwned = false;
    SettingsSnapshotApplyOrigin m_SettingsSnapshotApplyOrigin =
        SettingsSnapshotApplyOrigin::None;
    std::string m_PendingSettingsSnapshotCode;
#if defined(UVSR_BUILD_TESTING)
    bool m_SettingsContractDiagnosticComplete = false;
    std::unique_ptr<RetainedRuntimeDiagnosticState>
        m_RetainedRuntimeDiagnostic;
    RetainedRuntimeProvenance m_RetainedRuntimeProvenance;
    std::chrono::steady_clock::time_point m_RetainedRuntimeStartup;
    std::optional<RetainedRuntimeCameraPose>
        m_RetainedRuntimeBaselineCamera;
    int m_RetainedRuntimeBaselineWidth = 0;
    int m_RetainedRuntimeBaselineHeight = 0;
    RetainedRuntimeAction m_LastRetainedRuntimeAction =
        RetainedRuntimeAction::None;
    bool m_RetainedRuntimePathReselectionPending = false;
#endif
    std::optional<bool> m_SettingsCollapsedRequest;
    std::optional<bool> m_PerformanceCollapsedRequest;
    bool m_PathingDrawerOpenRequested = false;
    bool m_MaterialRevealRequested = false;
    int m_StatisticsEffect =
        static_cast<int>(StatisticsEffect::CompleteRenderer);

    struct LightDefaultState
    {
        int type = UVSR_LIGHT_TYPE_NONE;
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
    inline static std::vector<ImDrawList*>
        g_PerformanceAppearanceDrawLists;
    inline static bool g_PerformanceTableTransitionActive = false;
    inline static UiVisualTokens g_UiVisualTokens;
    inline static UiSpacingTokens g_UiSpacingTokens;

    static void TrackAppearanceDrawList(
        std::vector<ImDrawList*>& drawLists,
        ImDrawList* drawList);

    static void TrackSettingsAppearanceDrawList(ImDrawList* drawList);

    static void TrackPerformanceAppearanceDrawList(ImDrawList* drawList);

    static bool IsSettingsChildLaterInDrawOrder(
        const ImGuiWindow* candidate,
        const ImGuiWindow* current);

    static ImDrawList* ResolveFinalSettingsDecorationDrawList(
        ImGuiWindow* window);

    static void CaptureCurrentWindowBackdrop(
        UiBackdropRect& backdropRect,
        float rounding);

    static void CapturePanelSurfaceBackdrops(
        UiBackdropRect& titleBackdrop,
        UiBackdropRect& bodyBackdrop,
        const ImVec2& windowPosition,
        const ImVec2& windowSize,
        float titleHeight,
        bool expanded,
        float titleRounding,
        float bodyRounding);

    static void ApplyWindowAppearance(
        ImDrawList* drawList,
        const ImVec2& pivot,
        float scale,
        float opacity);

    static void ApplyBackdropAppearance(
        UiBackdropRect& backdropRect,
        const ImVec2& pivot,
        float scale,
        float opacity);

    static ImVec4 CompositeUiColorOver(
        const ImVec4& foreground,
        const ImVec4& background);

    static ImVec4 MakeUiColor(
        const UiRgbaColor& color,
        float alphaMultiplier = 1.f);

    static ImVec4 ScaleUiColor(
        const UiRgbaColor& color,
        float scale,
        float alphaMultiplier = 1.f);

    static ImVec4 OffsetUiColor(
        const UiRgbaColor& color,
        float offset,
        float alphaMultiplier = 1.f);

    static bool IsUltraBrightUiColor(const UiRgbaColor& color);

    static void ApplyUiSkin(
        UiSkin skin,
        const UiAccentSettings& accents,
        bool animationsEnabled,
        float displayScale);

    static void PushPanelBodySurface();

    [[nodiscard]] static ImVec4 GetOpaquePanelBodySurface();

    static void PushOpaquePanelBodySurface();
    inline static constexpr float
        UiLayoutAnimationDurationSeconds = 0.18f;

    static float GetUiLayoutAnimationStep();

    static float GetCommandInterfaceMinimumHeight();

    static float GetCommandInterfaceReservedHeight();

    static float GetPanelTitleHeight(
        const ImGuiStyle& style,
        float fontSize);

    static float GetSettingsCollapsedWindowHeight(
        const ImGuiStyle& style,
        float fontSize);

    static float GetSettingsMinimumExpandedWindowHeight(
        const ImGuiStyle& style,
        float fontSize);

    static float AdvanceUiLayoutAnimation(
        float amount,
        bool targetVisible);

    static float SmoothUiLayoutAnimation(float linearAmount);

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
        float wheelInput = 0.f;
        bool wheelAtTop = false;
        bool wheelAtBottom = false;
        float scrollY = 0.f;
        float viewportTopScreenY = 0.f;
        float retainedViewportHeight = 0.f;
        float lastScrollY = 0.f;
        ImDrawList* rootDrawList = nullptr;
        int rootDrawVertexStart = 0;
        UiDrawerHeightDeltas drawerHeightDeltas;
        std::vector<SettingsScrollAnchorPosition> previousAnchors;
        std::vector<SettingsScrollAnchorPosition> currentAnchors;
        std::vector<ImRect> translucentHeaderSupportRects;
        int lastFrame = -1;
    };

    inline static SettingsScrollStabilityContext
        g_SettingsScrollStabilityContext;

    static void PrepareSettingsScrollStability();

    static float GetSettingsBodyMinimumHeight(
        float maximumHeight);

    static void MarkSettingsLayoutAnimationActive();

    static void EnsureAnimatedChildLayoutSubmission(
        bool& bodySubmitted);

    static void BeginSettingsScrollStability();

    static void TrackSettingsScrollAnchor(
        ImGuiID id,
        float screenY);

    static void TrackSettingsDrawerHeight(
        ImGuiStorage* storage,
        ImGuiID headerId,
        float bodyTop,
        float displayedHeight);

    static void EndSettingsScrollStability();

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

    bool DrawCollapsingHeader(
        const char* label,
        const char* tooltip,
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None,
        bool forceClosedPresentation = false);

    static void BeginDrawerBody(
        const char* id,
        float controlWidth,
        float maximumHeight = 0.f);

    static ImVec4 LerpUiColor(
        const ImVec4& normal,
        const ImVec4& interaction,
        float amount);

    static void DrawDrawerBodyOutline(
        ImDrawList* drawList,
        const ImVec2& minimum,
        const ImVec2& maximum,
        float rounding,
        float topGap,
        bool intersectClipRect);

    static float ResolveRoundedRectRadius(
        const ImRect& rectangle,
        float requestedRadius);

    static void DrawFilledRoundedInsetFrame(
        ImDrawList* drawList,
        const ImRect& outerRect,
        const ImRect& innerRect,
        float rounding);

    static void DrawOpaqueRootPanelRetainedContent(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& retainedContentRect,
        float rounding);

    static void DrawRootPanelBodySurface(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& contentRect,
        const ImRect& retainedContentRect,
        float rounding);

    static void DrawRootPanelBodyOutlines(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& contentRect,
        float rounding);

    static void DrawRootPanelBodyChrome(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& contentRect,
        const ImRect& retainedContentRect,
        float rounding);

    static ImRect DrawCompactRootPanelBody(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        const ImRect& contentRect,
        float rounding,
        const char* text);

    static void EndDrawerBody();

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
        const char* tooltip = nullptr);

    static void EndAnimatedTreeNode();

    struct UiToggleRegionAnimationState
    {
        float linearAmount = 0.f;
        float disabledPresentationLinearAmount = 0.f;
        UiExpandedMeasurementState measurement;
        bool targetVisible = false;
        bool initialized = false;
        int lastSeenFrame = -1;
        int transitionFrame = -1;
        int advancedFrame = -1;
        int disabledPresentationAdvancedFrame = -1;
    };

    enum class UiToggleRegionOwner
    {
        Settings,
        Performance
    };

    enum class UiToggleRegionVisualMode
    {
        FadeWithHeight,
        ClipDuringCollapse
    };

    struct UiToggleRegionAnimationContext
    {
        ImGuiID id = 0;
        bool bodyVisible = false;
        bool ownsDisabledPresentationScope = false;
    };

    struct UiDisabledPresentationState
    {
        float linearAmount = 0.f;
        bool initialized = false;
        int lastSeenFrame = -1;
        int advancedFrame = -1;
    };

    inline static std::unordered_map<
        ImGuiID,
        UiToggleRegionAnimationState>
        g_UiToggleRegionAnimationStates;
    inline static std::vector<UiToggleRegionAnimationContext>
        g_UiToggleRegionAnimationContexts;
    inline static std::unordered_map<
        ImGuiID,
        UiDisabledPresentationState>
        g_UiDisabledPresentationStates;
    inline static int g_UiVisualDisabledScopeDepth = 0;

    static float ResolveDisabledPresentationAmount(
        UiDisabledPresentationState& state,
        bool disabled);

    static bool BeginVisuallyDisabledUiScope(
        const char* id,
        bool disabled);

    static void EndVisuallyDisabledUiScope(bool manualAlphaApplied);

    static bool BeginAnimatedToggleRegion(
        const char* id,
        bool visible,
        UiToggleRegionOwner owner = UiToggleRegionOwner::Settings,
        UiToggleRegionVisualMode visualMode =
            UiToggleRegionVisualMode::FadeWithHeight);

    static void EndAnimatedToggleRegion();

    static bool BeginMaterialEditorConditionalRegion(
        const char* id,
        bool visible);

    static void EndMaterialEditorConditionalRegion();

    static void DrawMaterialEditorTextureFilename(
        const char* filename,
        const float4& color);

    enum class UvsrColorEditChannels
    {
        Rgb,
        Rgba
    };

    static bool DrawUvsrColorEdit(
        const char* label,
        float* color,
        UvsrColorEditChannels channels);

    static bool DrawMaterialEditorColorEdit3(
        const char* label,
        float* color);

    static float GetUiHighlightFade(
        ImGuiID id,
        bool highlighted,
        float speed = 24.f);

    enum class SettingsResetIconPlacement
    {
        Trailing,
        NestedDropdownGutter
    };

    static void SetNextLabeledControlWidth(
        const char* label,
        float preferredWidth);

    static bool DrawPresetResetIconAtPlacement(
        const char* id,
        bool modified,
        const char* tooltip,
        SettingsResetIconPlacement placement);

    static bool DrawPresetResetIcon(
        const char* id,
        bool modified,
        const char* tooltip = "Reset this setting to its default value.");

    static bool DrawNestedDropdownResetIcon(
        const char* id,
        bool modified,
        const char* tooltip = "Reset this setting to its default value.");

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

    static bool HasDeferredDropdownUiActions();

    static void CancelDeferredDropdownUiActions();

    static bool IsDeferredDropdownPopupTransitionActive();

    static void FinishUnsubmittedDeferredDropdownPopupTransition();

    static void DrawTranslucentHeaderPanelBodySurface(
        ImDrawList* drawList,
        const ImRect& bodyRect,
        float rounding);

    static const char* GetDeferredDropdownPreview(ImGuiID comboId);

    static void QueueDeferredUiAction(
        ImGuiID controlId,
        ImGuiID transitionComboId,
        const char* previewValue,
        std::function<void()> action);

    static void QueueDeferredControlUiAction(
        std::function<void()> action);

    static void QueueDeferredDropdownUiAction(
        const char* previewValue,
        std::function<void()> action);

    static bool TryApplyDeferredDropdownUiActions(
        bool compositionIdle);

    static bool BeginRoundedCombo(
        const char* label,
        const char* previewValue,
        ImGuiComboFlags flags = ImGuiComboFlags_None);

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
        bool expanded);

    static std::filesystem::path GetWindowsFontsDirectory();

    UiFontFaces& GetUiFontFaces(UiFontFamily family);

    const UiFontFaces& GetUiFontFaces(UiFontFamily family) const;

    bool IsUiFontFamilyAvailable(UiFontFamily family) const;

    static std::string GetUiFontFamilyUnavailableReason(
        UiFontFamily family);

    void RequireUiFontFamily(UiFontFamily family) const;

    ImFont* GetActiveUiFont();

    ImFont* GetActiveUiHeaderFont();

    void ApplyActiveUiWordSpacing();

    void RestoreActiveUiWordSpacing();

    void ApplyActiveUiHeaderWordSpacing();

    void RestoreActiveUiHeaderWordSpacing();

    static std::string FormatCommandFloat3(const float3& value);

    static bool ApplyCommandFloat3(
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string_view path,
        float3& current,
        const float3& defaultValue,
        float minimum,
        float maximum,
        std::string& value,
        std::string& error);

    void SetCommandResult(
        std::string result,
        bool error = false);

    bool IsCommandRuntimeMutationLocked(
        const UiSettingsCommandDefinition& definition) const;

    bool CheckCommandMutationAllowed(
        const UiSettingsCommandDefinition& definition,
        std::string& error) const;

    bool DispatchUiCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    void RequestMaterialDrawerVisible(bool visible);

    const GpuAdapterChoice* GetActiveGpuAdapterChoice() const;

    AdaptiveSyncMode GetDefaultAdaptiveSyncMode() const;

    bool IsAdaptiveSyncModeAvailableForActiveAdapter(
        AdaptiveSyncMode mode) const;

    void ApplyAdaptiveSyncMode(AdaptiveSyncMode mode);

    void ApplyLightingSolution(LightingSolution solution);

    void ResetAllSettingsToFactoryDefaults();

    bool DispatchGeneralCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    bool DispatchRepresentationCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    bool DispatchNoiseCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);


    bool DispatchVisibilityCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    bool DispatchAliasingCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    bool DispatchDebugCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    bool DispatchSkyCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    bool DispatchDenoisingCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    std::shared_ptr<Light> GetDefaultCommandLight() const;

    std::shared_ptr<Light> EnsureCommandSelectedLight();

    const LightDefaultState& GetCommandLightDefaults(
        const std::shared_ptr<Light>& light);

    static std::pair<float, float> GetCommandLightAngles(
        const double3& storedDirection,
        bool directional);

    static double3 MakeCommandLightDirection(
        float azimuthDegrees,
        float elevationDegrees,
        bool directional);

    struct FlashlightFloatCommandBinding
    {
        std::string_view path;
        float FlashlightSettings::* member = nullptr;
        float minimum = 0.f;
        float maximum = 1.f;
    };

    inline static constexpr std::array<
        FlashlightFloatCommandBinding, 12> FlashlightFloatCommandBindings = {{
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
                "light.selected.flashlight.angular-size",
                &FlashlightSettings::angularSizeDegrees,
                FlashlightMinimumAngularSizeDegrees,
                FlashlightMaximumAngularSizeDegrees
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
                "light.selected.flashlight.horizontal-offset",
                &FlashlightSettings::cameraHorizontalOffsetMeters,
                FlashlightMinimumCameraHorizontalOffsetMeters,
                FlashlightMaximumCameraHorizontalOffsetMeters
            },
            {
                "light.selected.flashlight.vertical-offset",
                &FlashlightSettings::cameraVerticalOffsetMeters,
                FlashlightMinimumCameraVerticalOffsetMeters,
                FlashlightMaximumCameraVerticalOffsetMeters
            }
        }};

    bool DispatchFlashlightCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    bool DispatchLightCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    bool DispatchDirectionalShadowCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    static bool IsCommandMaterialTransmissive(MaterialDomain domain);

    static bool IsCommandMaterialAlphaTested(MaterialDomain domain);

    static bool IsCommandMaterialAlphaBlended(MaterialDomain domain);

    bool DispatchMaterialCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    bool DispatchCommandValue(
        const UiSettingsCommandDefinition& definition,
        CommandValueOperation operation,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    [[nodiscard]] SettingsSnapshotRuntimeAccess
    MakeSettingsSnapshotRuntimeAccess();

    void RefreshSettingsSnapshot();

    void CopySettingsSnapshot();

    [[nodiscard]] bool ApplyCanonicalSettingsSnapshot(
        std::string_view canonical,
        std::size_t& changedValueCount,
        std::string& error);

    [[nodiscard]] bool BuildSettingsSnapshotRestartHandoffCode(
        const SettingsSnapshotRestartHandoff& handoff,
        std::string& code,
        std::string& error) const;

    void FailStartupSettingsSnapshot(
        std::string_view code,
        std::string_view error);

    void HandleStagedSettingsSnapshotStep(
        const SettingsSnapshotTransactionStep& step);

    void TryApplyStartupSettingsSnapshot();

    bool DispatchCommandAction(
        const UiSettingsCommandDefinition& definition,
        const std::vector<std::string>& arguments,
        std::string& value,
        std::string& error);

    void AppendDynamicCommandValues(
        std::string_view path,
        std::vector<std::string>& values) const;

    std::vector<std::string> GetCommandCompletionCandidates(
        const UiCommandCompletionToken& completion) const;

    void ExecuteUiCommand(const UiCommand& command);

    void CompleteCommandInput(ImGuiInputTextCallbackData* data);

    void RecallCommandHistory(
        ImGuiInputTextCallbackData* data,
        bool previous);

    static int CommandInputCallback(
        ImGuiInputTextCallbackData* data);

    void SubmitCommandInput();

    void DrawCommandInterface();

    void DrawPerformancePanelContents(
        float settingsControlWidth,
        const std::string& performanceLine);

    void DrawGeneralDrawer(float settingsControlWidth);

    void DrawPathingDrawer(float settingsControlWidth);

    void DrawMaterialDrawer(float settingsControlWidth);

    void DrawInterfaceDrawer(float settingsControlWidth);

    static std::string BuildPerformanceLine(
        const std::array<std::string, 4>& values);

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

    void QueueStatSnapshot(int width, int height);

    void ApplyQueuedStatSnapshot();

    bool DrawBoundedSliderFloat(
        const char* label,
        float* value,
        float logicalMinimum,
        float logicalMaximum,
        float travelMinimum,
        float travelMaximum,
        const char* format = "%.3f",
        ImGuiSliderFlags flags = 0);

    bool DrawSliderFloat(
        const char* label,
        float* value,
        float minimum,
        float maximum,
        const char* format = "%.3f",
        ImGuiSliderFlags flags = 0);

    bool DrawBoundedSliderInt(
        const char* label,
        int* value,
        int logicalMinimum,
        int logicalMaximum,
        int travelMinimum,
        int travelMaximum,
        const char* format = "%d",
        ImGuiSliderFlags flags = 0);

    bool DrawSliderInt(
        const char* label,
        int* value,
        int minimum,
        int maximum,
        const char* format = "%d",
        ImGuiSliderFlags flags = 0);

    bool DrawLightDirectionSliders(
        double3& direction,
        bool directional);

    static bool DrawCenteredActionButton(const char* label, float width);

public:
    UIRenderer(
        DeviceManager* deviceManager,
        std::shared_ptr<UvsrSceneViewer> app,
        UIData& ui,
        std::string startupSettingsSnapshotCode);

    void Animate(float elapsedTimeSeconds) override;

    bool ShouldSuppressFullscreenShortcut() const override;

    bool Init(std::shared_ptr<ShaderFactory> shaderFactory);

#if defined(UVSR_BUILD_TESTING)
    [[nodiscard]] bool ApplyRuntimeSetting(
        std::string_view name,
        std::string_view requestedValue,
        std::string& error);

    [[nodiscard]] bool ChangeRuntimeDiagnosticMaterial(
        std::string& error);

    [[nodiscard]] bool ChangeRuntimeDiagnosticLight(
        std::string& error);

    [[nodiscard]] bool ToggleRuntimeDiagnosticFlashlight(
        std::string& error);

    void DriveRetainedRuntimeDiagnostic();
    int VerifyCanonicalSettingsContract();

#endif

    virtual void Render(nvrhi::IFramebuffer* framebuffer) override;

    virtual void BackBufferResizing() override;

    virtual void DisplayScaleChanged(
        float scaleX,
        float scaleY) override;

protected:
    virtual bool KeyboardUpdate(
        int key,
        int scancode,
        int action,
        int mods) override;

    virtual bool KeyboardCharInput(
        unsigned int unicode,
        int mods) override;

    virtual void buildUI(void) override;
};
