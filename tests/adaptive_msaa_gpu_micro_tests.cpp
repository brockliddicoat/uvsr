// The headless device and Agility SDK bootstrap follows Donut's
// tests/src/engine/test_float.cpp at pinned Donut commit
// bc1ea24b0486f1c00d89327fe16c0b4dd11c5937 (MIT). The GPU fixture and all
// adaptive-MSAA assertions below are UVSR-native test code.

#if !defined(_WIN32) || !defined(DONUT_WITH_DX12) || !DONUT_WITH_DX12
#error This micro-test requires the UVSR Windows D3D12 build.
#endif

#include <directx/d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

extern "C"
{
    // UVSR stages the matching Agility runtime beside every executable. These
    // exports make the headless test select that exact runtime before main.
    // Activate the stable contract carried by the pinned package. Exporting
    // its preview contract would require Windows developer-mode enablement and
    // can falsely make every real adapter look unavailable on production
    // machines.
    __declspec(dllexport) extern const unsigned int D3D12SDKVersion =
        D3D12_SDK_VERSION;
    __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
}

#include "adaptive_msaa_diagnostics_pass.h"
#include "adaptive_msaa_raster_cb.h"
#include "adaptive_msaa_scene.h"
#include "adaptive_msaa_texture_qualification_runtime.h"

#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/math/float.h>
#include <donut/core/math/math.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/View.h>
#include <nvrhi/nvrhi.h>
#include <nvrhi/utils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace donut;
using namespace donut::math;

#include <donut/shaders/bindless.h>
#include <donut/shaders/gbuffer_cb.h>
#include "pbr_deferred_lighting_cb.h"

namespace
{
    using Microsoft::WRL::ComPtr;
    using namespace uvsr::adaptive_msaa;

    constexpr int NoUsableAdapterExitCode = 77;
    constexpr uint32_t FixtureWidth = 2u;
    constexpr uint32_t FixtureHeight = 2u;
    constexpr uint32_t FixturePixelCount = FixtureWidth * FixtureHeight;
    constexpr uint32_t FixtureGeneration = 1u;
    constexpr uint32_t FixtureSlot = 0u;
    constexpr uint32_t FixtureGeometryInstanceHandle =
        (FixtureGeneration << GeometryInstanceSlotBits) | FixtureSlot;
    constexpr uint32_t CompactFallbackGeometryInstanceHandle =
        (FixtureGeneration << GeometryInstanceSlotBits) | 1u;
    // clearTextureUInt clears every component to the same word. Keeping the
    // primitive index equal to the valid handle still yields a valid exact key
    // and makes this fixture independent of an extra raster pipeline.
    constexpr uint32_t FixturePrimitiveIndex =
        FixtureGeometryInstanceHandle;
    constexpr float FixtureDepth = 0.5f;
    constexpr float FloatTolerance = 1.f / 1024.f;

    constexpr uint32_t CompactFixtureWidth = 8u;
    constexpr uint32_t CompactFixtureHeight = 8u;
    constexpr uint32_t CompactFixturePixelCount =
        CompactFixtureWidth * CompactFixtureHeight;
    constexpr uint32_t CompactFixtureSampleCount = 2u;
    constexpr nvrhi::Format AdaptiveGBufferProductDepthFormat =
        nvrhi::Format::D24S8;
    constexpr DXGI_FORMAT AdaptiveGBufferProductDepthViewFormat =
        DXGI_FORMAT_D24_UNORM_S8_UINT;
    constexpr DXGI_FORMAT AdaptiveGBufferProductDepthShaderViewFormat =
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    // The two production triangles share the x + y == 8 screen-space edge.
    // D3D12's 2x sample locations straddle that edge in this pixel.
    constexpr uint32_t CompactSelectedPixelX = 2u;
    constexpr uint32_t CompactSelectedPixelY = 5u;
    constexpr uint32_t CompactFallbackPixelX = 1u;
    constexpr uint32_t CompactFallbackPixelY = 1u;
    constexpr uint32_t CompactQueueMaximum = 128u;
    constexpr uint32_t CompactQueueOverflowCapacity = 1u;
    constexpr uint32_t CompactBindingDepth = 8u;
    constexpr uint32_t CompactBindingResolvedBackground = 17u;
    constexpr uint32_t CompactBindingDirectionalVisibility = 20u;
    constexpr uint32_t CompactBindingSurfaceOwnership = 21u;
    constexpr uint32_t CompactBindingGeometryInstances = 22u;
    constexpr uint32_t CompactBindingGeometry = 23u;
    constexpr uint32_t CompactBindingInstanceHandles = 24u;
    constexpr uint32_t CompactBindingDiagnosticsBackground = 25u;
    constexpr uint32_t CompactBindingVisibilityClass = 26u;
    constexpr uint32_t CompactBindingFallbackDiffuse = 27u;
    constexpr uint32_t CompactBindingFallbackMaterial = 28u;
    constexpr uint32_t CompactBindingFallbackNormals = 29u;
    constexpr uint32_t CompactBindingFallbackEmissive = 30u;
    constexpr uint32_t CompactBindingFallbackAmbientOcclusion = 31u;
    constexpr uint32_t CompactBindingFallbackMotionVectors = 32u;
    constexpr uint32_t CompactBindingConstants = 1u;
    constexpr uint32_t CompactBindingDiagnosticsConstants = 2u;
    constexpr uint32_t CompactTopologyEpoch = 1u;
    constexpr uint32_t CompactTopologyRevision = 2u;
    constexpr uint32_t CompactLodRevision = 3u;
    constexpr uint32_t CompactDeformationRevision = 4u;
    constexpr uint32_t CompactMaterialRevision = 5u;
    constexpr uint32_t CompactMaterialTextureDescriptorCount = 7u;
    constexpr uint32_t CompactRawInstanceDescriptor =
        CompactMaterialTextureDescriptorCount + 0u;
    constexpr uint32_t CompactRawIndexDescriptor =
        CompactMaterialTextureDescriptorCount + 1u;
    constexpr uint32_t CompactRawVertexDescriptor =
        CompactMaterialTextureDescriptorCount + 2u;
    constexpr uint32_t CompactRawMaterialDescriptor =
        CompactMaterialTextureDescriptorCount + 3u;
    constexpr uint32_t CompactRawDescriptorCount = 4u;
    constexpr uint32_t CompactDescriptorCount =
        CompactMaterialTextureDescriptorCount + CompactRawDescriptorCount;
    constexpr uint32_t CompactMaterialTextureSize = 64u;
    constexpr uint32_t CompactMaterialTextureMipCount = 7u;
    constexpr float CompactEmissiveR = 1.f;
    constexpr float CompactEmissiveG = 0.25f;
    constexpr float CompactEmissiveB = 0.125f;
    constexpr float CompactFallbackEmissiveR = 0.125f;
    constexpr float CompactFallbackEmissiveG = 0.75f;
    constexpr float CompactFallbackEmissiveB = 1.5f;

    struct alignas(16) CompactTestShadeConstants
    {
        GBufferFillConstants gbuffer{};
        CompactSceneGpuConstants scene{};
        uint32_t snapshotBuildSerialLow = 1u;
        uint32_t snapshotBuildSerialHigh = 0u;
        uint32_t sampleCount = CompactFixtureSampleCount;
        uint32_t frontCounterClockwise = 0u;
        uint32_t preparationQueueCapacity = CompactQueueMaximum;
        uint32_t reserved0 = 0u;
        uint32_t reserved1 = 0u;
        uint32_t reserved2 = 0u;
    };

    static_assert(sizeof(CompactTestShadeConstants) % 16u == 0u);
    static_assert(offsetof(CompactTestShadeConstants, scene) ==
        sizeof(GBufferFillConstants));
    static_assert(sizeof(InstanceData) == 7u * 16u);
    static_assert(sizeof(MaterialConstants) == 13u * 16u);

    struct CompactMsaaExtractedSample
    {
        uint32_t geometryInstanceHandle = InvalidVisibilityWord;
        uint32_t geometryLocalPrimitiveIndex = InvalidVisibilityWord;
        uint32_t visibilityClass =
            ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND;
        uint32_t depthBits = 0u;
    };

    struct CompactMsaaExtractCounters
    {
        uint32_t coveredSampleCount = 0u;
        uint32_t backgroundSampleCount = 0u;
        uint32_t nonFiniteDepthCount = 0u;
        uint32_t partialInvalidKeyCount = 0u;
        uint32_t invalidCoveredRecordCount = 0u;
        uint32_t invalidBackgroundRecordCount = 0u;
        uint32_t fatalConfigurationCount = 0u;
        uint32_t recordWriteCount = 0u;
        uint32_t negativeDepthCount = 0u;
    };

    static_assert(sizeof(CompactMsaaExtractedSample) == 16u);
    static_assert(sizeof(CompactMsaaExtractCounters) == 36u);

    enum class AdapterSelectionStatus
    {
        Usable,
        NoUsableAdapter,
        InfrastructureFailure
    };

    struct SelectedAdapterIdentity
    {
        int adapterIndex = -1;
        std::string name;
        uint64_t dedicatedVideoMemory = 0u;
        uint32_t vendorId = 0u;
        uint32_t deviceId = 0u;
        uint32_t subSystemId = 0u;
        uint32_t revision = 0u;
        LUID luid{};
    };

    class ValidationMessageCallback final : public nvrhi::IMessageCallback
    {
    public:
        void message(
            nvrhi::MessageSeverity severity,
            const char* messageText) override
        {
            const char* label = "INFO";
            switch (severity)
            {
            case nvrhi::MessageSeverity::Warning:
                label = "WARNING";
                break;
            case nvrhi::MessageSeverity::Error:
                label = "ERROR";
                m_ErrorCount.fetch_add(1u, std::memory_order_relaxed);
                break;
            case nvrhi::MessageSeverity::Fatal:
                label = "FATAL";
                m_ErrorCount.fetch_add(1u, std::memory_order_relaxed);
                break;
            default:
                break;
            }
            std::cerr << "[NVRHI " << label << "] "
                << (messageText ? messageText : "(null message)") << '\n';
        }

        [[nodiscard]] uint32_t ErrorCount() const
        {
            return m_ErrorCount.load(std::memory_order_relaxed);
        }

    private:
        std::atomic<uint32_t> m_ErrorCount{ 0u };
    };

    struct CompactDiagnosticOutputs
    {
        nvrhi::TextureHandle sampleStatus;
        nvrhi::TextureHandle multisampledBackground;
        nvrhi::BufferHandle selectedSampleData;
        nvrhi::BufferHandle pixelMetadata;
    };

    struct CompactLightingRun
    {
        nvrhi::TextureHandle output;
        nvrhi::TextureHandle visualization;
        nvrhi::StagingTextureHandle outputReadback;
        nvrhi::StagingTextureHandle visualizationReadback;
        CompactDiagnosticOutputs diagnostics;
        nvrhi::FramebufferHandle backgroundMaskFramebuffer;
        nvrhi::BindingSetHandle bindingSet;
    };

    enum class AdaptiveGBufferTestTopology : uint32_t
    {
        Inline = 0u,
        Separated = 1u
    };

    struct AdaptiveGBufferProductionPipelines
    {
        nvrhi::BindingLayoutHandle referenceLayout;
        nvrhi::BindingLayoutHandle adaptiveLayout;
        nvrhi::BindingLayoutHandle adaptiveDiagnosticsLayout;
        nvrhi::ComputePipelineHandle reference;
        std::array<nvrhi::ComputePipelineHandle, 2u> adaptive;
        std::array<nvrhi::ComputePipelineHandle, 2u> adaptiveDiagnostics;
    };

    enum CompactGuideOutput : uint32_t
    {
        CompactGuideDepth = 0u,
        CompactGuideDiffuse,
        CompactGuideMaterial,
        CompactGuideNormals,
        CompactGuideEmissive,
        CompactGuideAmbientOcclusion,
        CompactGuideMotion,
        CompactGuideOutputCount
    };

    enum class CompactMaterialTexturePack : uint32_t
    {
        Uncompressed = 0u,
        BlockCompressedGuide,
        BlockCompressedAlpha,
        Count
    };

    constexpr size_t CompactMaterialTexturePackCount =
        static_cast<size_t>(CompactMaterialTexturePack::Count);

    struct CompactGuideRun
    {
        std::array<nvrhi::TextureHandle, CompactGuideOutputCount> outputs;
        std::array<nvrhi::StagingTextureHandle, CompactGuideOutputCount>
            readbacks;
        nvrhi::BindingSetHandle bindingSet;
    };

    enum class ProductionFixtureScope : uint32_t
    {
        CompactVisibility = 0u,
        AdaptiveGBuffer = 1u
    };

    struct CompactProductionFixture
    {
        ProductionFixtureScope scope =
            ProductionFixtureScope::CompactVisibility;
        uint32_t sampleCount = CompactFixtureSampleCount;
        donut::engine::PlanarView view;
        GBufferFillConstants gbufferConstants{};
        CompactTestShadeConstants compactConstants{};
        PbrDeferredLightingConstants deferredConstants{};
        MaterialConstants primaryFallbackMaterial{};

        nvrhi::TextureHandle surfaceOwnership;
        nvrhi::TextureHandle visibilityClass;
        nvrhi::TextureHandle depth;
        nvrhi::TextureHandle fallbackDiffuse;
        nvrhi::TextureHandle fallbackMaterial;
        nvrhi::TextureHandle fallbackNormals;
        nvrhi::TextureHandle fallbackEmissive;
        nvrhi::TextureHandle fallbackAmbientOcclusion;
        nvrhi::TextureHandle fallbackMotionVectors;
        nvrhi::TextureHandle resolvedBackground;
        nvrhi::TextureHandle shadowMap;
        nvrhi::TextureHandle diffuseEnvironment;
        nvrhi::TextureHandle specularEnvironment;
        nvrhi::TextureHandle environmentBrdf;
        nvrhi::TextureHandle directionalVisibility;
        std::array<nvrhi::TextureHandle,
            CompactMaterialTextureDescriptorCount> materialTextures;
        std::array<nvrhi::TextureHandle,
            CompactMaterialTextureDescriptorCount>
                blockCompressedGuideTextures;
        std::array<nvrhi::TextureHandle,
            CompactMaterialTextureDescriptorCount>
                blockCompressedAlphaTextures;
        nvrhi::FramebufferHandle visibilityFramebuffer;
        nvrhi::FramebufferHandle fallbackFramebuffer;
        nvrhi::FramebufferHandle adaptiveGBufferFramebuffer;

        nvrhi::BufferHandle instanceBuffer;
        nvrhi::BufferHandle indexBuffer;
        nvrhi::BufferHandle vertexBuffer;
        nvrhi::BufferHandle materialBuffer;
        nvrhi::BufferHandle geometryInstanceBuffer;
        nvrhi::BufferHandle geometryBuffer;
        nvrhi::BufferHandle instanceHandleBuffer;
        nvrhi::BufferHandle rasterViewConstantsBuffer;
        nvrhi::BufferHandle compactConstantsBuffer;
        nvrhi::BufferHandle deferredConstantsBuffer;
        nvrhi::BufferHandle diagnosticsConstantsBuffer;
        nvrhi::BufferHandle fallbackMaterialConstantsBuffer;
        nvrhi::BufferHandle secondaryFallbackMaterialConstantsBuffer;

        nvrhi::SamplerHandle materialSampler;
        nvrhi::SamplerHandle shadowSampler;
        nvrhi::SamplerHandle environmentSampler;
        nvrhi::SamplerHandle environmentBrdfSampler;

        nvrhi::BindingLayoutHandle rasterViewLayout;
        nvrhi::BindingLayoutHandle rasterInputLayout;
        nvrhi::BindingLayoutHandle fallbackMaterialLayout;
        nvrhi::BindingLayoutHandle compactGuideLayout;
        nvrhi::BindingLayoutHandle compactFixedLayout;
        nvrhi::BindingLayoutHandle compactBindlessLayout;
        nvrhi::BindingLayoutHandle msaaExtractLayout;
        nvrhi::BindingSetHandle rasterViewBindingSet;
        nvrhi::BindingSetHandle rasterInputBindingSet;
        nvrhi::BindingSetHandle fallbackMaterialBindingSet;
        nvrhi::BindingSetHandle secondaryFallbackMaterialBindingSet;
        nvrhi::BindingSetHandle blockCompressedGuideMaterialBindingSet;
        nvrhi::BindingSetHandle blockCompressedAlphaMaterialBindingSet;
        nvrhi::DescriptorTableHandle compactDescriptorTable;
        nvrhi::DescriptorTableHandle blockCompressedGuideDescriptorTable;
        nvrhi::DescriptorTableHandle blockCompressedAlphaDescriptorTable;
        nvrhi::GraphicsPipelineHandle visibilityPipeline;
        nvrhi::GraphicsPipelineHandle visibilityAlphaPipeline;
        nvrhi::GraphicsPipelineHandle fallbackPipeline;
        nvrhi::GraphicsPipelineHandle fallbackAlphaPipeline;
        nvrhi::GraphicsPipelineHandle adaptiveGBufferPipeline;
        nvrhi::GraphicsPipelineHandle adaptiveGBufferBackgroundMaskPipeline;
        nvrhi::ComputePipelineHandle compactResolvePipeline;
        nvrhi::ComputePipelineHandle compactLightingPipeline;
        nvrhi::ComputePipelineHandle msaaExtractPipeline;
        CompactGuideRun guide;
        std::array<bool, CompactMaterialTexturePackCount>
            materialTexturesUploaded{};
    };

    struct TestCommandLine
    {
        int requestedAdapterIndex = -1;
        std::filesystem::path certificateOutputPath;
        bool writeCompactTextureCertificate = false;
    };

    [[nodiscard]] bool ParseTestCommandLine(
        int argc,
        char** argv,
        TestCommandLine& commandLine)
    {
        commandLine = {};
        for (int argumentIndex = 1;
            argumentIndex < argc;
            ++argumentIndex)
        {
            const char* argument = argv[argumentIndex];
            if (std::strcmp(
                    argument,
                    "--write-compact-texture-certificate") == 0)
            {
                if (commandLine.writeCompactTextureCertificate ||
                    ++argumentIndex >= argc ||
                    !argv[argumentIndex] ||
                    argv[argumentIndex][0] == '\0')
                {
                    std::cerr << argument <<
                        " requires one nonempty output path and may appear "
                        "only once.\n";
                    return false;
                }
                commandLine.certificateOutputPath =
                    std::filesystem::path(argv[argumentIndex]);
                commandLine.writeCompactTextureCertificate = true;
                continue;
            }

            if (std::strcmp(argument, "--adapter") != 0 &&
                std::strcmp(argument, "-adapter") != 0)
            {
                std::cerr << "Unknown argument: " << argument << '\n';
                return false;
            }
            if (++argumentIndex >= argc)
            {
                std::cerr << argument << " requires a non-negative DXGI "
                    "adapter index.\n";
                return false;
            }

            errno = 0;
            char* parseEnd = nullptr;
            const long parsed = std::strtol(
                argv[argumentIndex],
                &parseEnd,
                10);
            if (errno == ERANGE || !parseEnd || *parseEnd != '\0' ||
                parsed < 0 ||
                parsed > std::numeric_limits<int>::max())
            {
                std::cerr << argument << " requires a non-negative DXGI "
                    "adapter index.\n";
                return false;
            }
            commandLine.requestedAdapterIndex = static_cast<int>(parsed);
        }
        return true;
    }

    [[nodiscard]] AdapterSelectionStatus SelectHardwareD3D12Adapter(
        donut::app::DeviceManager* deviceManager,
        donut::app::DeviceCreationParameters& deviceParameters,
        SelectedAdapterIdentity& selectedIdentity)
    {
        if (!deviceManager || !deviceManager->CreateInstance(deviceParameters))
        {
            std::cerr << "D3D12 adapter selection could not initialize "
                "DXGI.\n";
            return AdapterSelectionStatus::InfrastructureFailure;
        }

        std::vector<donut::app::AdapterInfo> adapters;
        if (!deviceManager->EnumerateAdapters(adapters))
        {
            std::cerr << "D3D12 adapter enumeration failed.\n";
            return AdapterSelectionStatus::InfrastructureFailure;
        }
        if (adapters.empty())
            return AdapterSelectionStatus::NoUsableAdapter;

        const bool automaticSelection = deviceParameters.adapterIndex < 0;
        int bestAdapterIndex = -1;
        uint64_t bestDedicatedVideoMemory = 0u;
        std::vector<SelectedAdapterIdentity> usableAdapters;
        usableAdapters.reserve(adapters.size());
        for (size_t adapterIndex = 0u;
            adapterIndex < adapters.size();
            ++adapterIndex)
        {
            const donut::app::AdapterInfo& adapter = adapters[adapterIndex];
            nvrhi::RefCountPtr<IDXGIAdapter1> adapter1;
            DXGI_ADAPTER_DESC1 description{};
            if (FAILED(adapter.dxgiAdapter->QueryInterface(
                    IID_PPV_ARGS(&adapter1))) ||
                FAILED(adapter1->GetDesc1(&description)) ||
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u ||
                FAILED(D3D12CreateDevice(
                    adapter.dxgiAdapter,
                    deviceParameters.featureLevel,
                    __uuidof(ID3D12Device),
                    nullptr)))
            {
                continue;
            }

            usableAdapters.push_back(SelectedAdapterIdentity{
                static_cast<int>(adapterIndex),
                adapter.name,
                adapter.dedicatedVideoMemory,
                description.VendorId,
                description.DeviceId,
                description.SubSysId,
                description.Revision,
                description.AdapterLuid
            });
            if (automaticSelection &&
                (bestAdapterIndex < 0 ||
                    adapter.dedicatedVideoMemory >
                        bestDedicatedVideoMemory))
            {
                bestAdapterIndex = static_cast<int>(adapterIndex);
                bestDedicatedVideoMemory = adapter.dedicatedVideoMemory;
            }
        }

        if (usableAdapters.empty())
            return AdapterSelectionStatus::NoUsableAdapter;
        if (automaticSelection)
            deviceParameters.adapterIndex = bestAdapterIndex;

        const auto selected = std::find_if(
            usableAdapters.begin(),
            usableAdapters.end(),
            [&deviceParameters](const SelectedAdapterIdentity& candidate)
            {
                return candidate.adapterIndex ==
                    deviceParameters.adapterIndex;
            });
        if (selected == usableAdapters.end())
        {
            std::cerr << "Requested DXGI adapter "
                << deviceParameters.adapterIndex
                << " is unavailable or does not support D3D12 feature level "
                << "11_1.\n";
            return AdapterSelectionStatus::InfrastructureFailure;
        }

        selectedIdentity = *selected;
        std::cout << "Selected graphics adapter "
            << selectedIdentity.adapterIndex << ": "
            << selectedIdentity.name << " (PCI "
            << std::hex << std::setw(4) << std::setfill('0')
            << selectedIdentity.vendorId << ':'
            << std::setw(4) << selectedIdentity.deviceId
            << std::dec << std::setfill(' ') << ", "
            << selectedIdentity.dedicatedVideoMemory /
                (1024ull * 1024ull)
            << " MiB dedicated VRAM).\n";
        return AdapterSelectionStatus::Usable;
    }

    [[nodiscard]] bool QueryMultisampleSupport(
        ID3D12Device* device,
        DXGI_FORMAT format,
        uint32_t sampleCount,
        uint32_t& qualityLevelCount)
    {
        qualityLevelCount = 0u;
        if (!device)
            return false;

        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS query{};
        query.Format = format;
        query.SampleCount = sampleCount;
        query.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        if (FAILED(device->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                &query,
                sizeof(query))))
        {
            return false;
        }
        qualityLevelCount = query.NumQualityLevels;
        return qualityLevelCount != 0u;
    }

    [[nodiscard]] bool QueryAdaptiveMultisampleSupport(
        ID3D12Device* device,
        DXGI_FORMAT format,
        uint32_t sampleCount,
        D3D12_FORMAT_SUPPORT1 requiredFormatSupport,
        uint32_t& qualityLevelCount)
    {
        qualityLevelCount = 0u;
        if (!device)
            return false;

        D3D12_FEATURE_DATA_FORMAT_SUPPORT formatQuery{};
        formatQuery.Format = format;
        const bool formatSupported = SUCCEEDED(
                device->CheckFeatureSupport(
                    D3D12_FEATURE_FORMAT_SUPPORT,
                    &formatQuery,
                    sizeof(formatQuery))) &&
            (formatQuery.Support1 & requiredFormatSupport) ==
                requiredFormatSupport;
        // Preserve the quality-level count for capability logging even when
        // a required usage bit is absent; the composed decision requires both
        // predicates exactly like the production resolver.
        const bool sampleCountSupported = QueryMultisampleSupport(
            device,
            format,
            sampleCount,
            qualityLevelCount);
        return formatSupported && sampleCountSupported;
    }

    [[nodiscard]] bool QueryAdaptiveColorMultisampleSupport(
        ID3D12Device* device,
        DXGI_FORMAT format,
        uint32_t sampleCount,
        uint32_t& qualityLevelCount)
    {
        constexpr D3D12_FORMAT_SUPPORT1 RequiredFormatSupport =
            D3D12_FORMAT_SUPPORT1_TEXTURE2D |
            D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
            D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
            D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD |
            D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;
        return QueryAdaptiveMultisampleSupport(
            device,
            format,
            sampleCount,
            RequiredFormatSupport,
            qualityLevelCount);
    }

    [[nodiscard]] bool QueryDepthMultisampleSupport(
        ID3D12Device* device,
        DXGI_FORMAT depthViewFormat,
        DXGI_FORMAT shaderViewFormat,
        uint32_t sampleCount,
        uint32_t& qualityLevelCount)
    {
        qualityLevelCount = 0u;
        if (!device)
            return false;

        constexpr D3D12_FORMAT_SUPPORT1 RequiredDepthViewSupport =
            D3D12_FORMAT_SUPPORT1_TEXTURE2D |
            D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL |
            D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT depthViewQuery{};
        depthViewQuery.Format = depthViewFormat;
        const bool depthViewSupported = SUCCEEDED(
                device->CheckFeatureSupport(
                    D3D12_FEATURE_FORMAT_SUPPORT,
                    &depthViewQuery,
                    sizeof(depthViewQuery))) &&
            (depthViewQuery.Support1 & RequiredDepthViewSupport) ==
                RequiredDepthViewSupport;

        constexpr D3D12_FORMAT_SUPPORT1 RequiredShaderViewSupport =
            D3D12_FORMAT_SUPPORT1_TEXTURE2D |
            D3D12_FORMAT_SUPPORT1_SHADER_LOAD |
            D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD;
        D3D12_FEATURE_DATA_FORMAT_SUPPORT shaderViewQuery{};
        shaderViewQuery.Format = shaderViewFormat;
        const bool shaderViewSupported = SUCCEEDED(
                device->CheckFeatureSupport(
                    D3D12_FEATURE_FORMAT_SUPPORT,
                    &shaderViewQuery,
                    sizeof(shaderViewQuery))) &&
            (shaderViewQuery.Support1 & RequiredShaderViewSupport) ==
                RequiredShaderViewSupport;

        const bool sampleCountSupported = QueryMultisampleSupport(
            device,
            depthViewFormat,
            sampleCount,
            qualityLevelCount);
        return depthViewSupported && shaderViewSupported &&
            sampleCountSupported;
    }

    [[nodiscard]] bool QueryAdaptiveDepthMultisampleSupport(
        ID3D12Device* device,
        uint32_t sampleCount,
        uint32_t& qualityLevelCount)
    {
        // Compact visibility intentionally retains its D32/R32 fixture.
        return QueryDepthMultisampleSupport(
            device,
            DXGI_FORMAT_D32_FLOAT,
            DXGI_FORMAT_R32_FLOAT,
            sampleCount,
            qualityLevelCount);
    }

    [[nodiscard]] bool QueryAdaptiveGBufferDepthMultisampleSupport(
        ID3D12Device* device,
        uint32_t sampleCount,
        uint32_t& qualityLevelCount)
    {
        // Match Donut's first supported UVSR depth choice and its typed SRV.
        return QueryDepthMultisampleSupport(
            device,
            AdaptiveGBufferProductDepthViewFormat,
            AdaptiveGBufferProductDepthShaderViewFormat,
            sampleCount,
            qualityLevelCount);
    }

    [[nodiscard]] uint32_t ResolveUvSrRenderTargetSampleCountForTest(
        nvrhi::IDevice* device,
        ID3D12Device* nativeDevice,
        uint32_t requestedSampleCount)
    {
        if (!device || !nativeDevice || requestedSampleCount <= 1u)
            return 1u;
        constexpr nvrhi::Format depthFormats[] = {
            nvrhi::Format::D24S8,
            nvrhi::Format::D32S8,
            nvrhi::Format::D32,
            nvrhi::Format::D16
        };
        const nvrhi::Format selectedDepthFormat = nvrhi::utils::ChooseFormat(
            device,
            nvrhi::FormatSupport::Texture |
                nvrhi::FormatSupport::DepthStencil |
                nvrhi::FormatSupport::ShaderLoad,
            depthFormats,
            std::size(depthFormats));
        DXGI_FORMAT selectedDepthDxgiFormat = DXGI_FORMAT_UNKNOWN;
        switch (selectedDepthFormat)
        {
        case nvrhi::Format::D24S8:
            selectedDepthDxgiFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
            break;
        case nvrhi::Format::D32S8:
            selectedDepthDxgiFormat =
                DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
            break;
        case nvrhi::Format::D32:
            selectedDepthDxgiFormat = DXGI_FORMAT_D32_FLOAT;
            break;
        case nvrhi::Format::D16:
            selectedDepthDxgiFormat = DXGI_FORMAT_D16_UNORM;
            break;
        default:
            return 1u;
        }

        constexpr DXGI_FORMAT allocatedColorFormats[] = {
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            DXGI_FORMAT_R16G16B16A16_SNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            DXGI_FORMAT_R16G16_FLOAT,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8_UNORM
        };
        const auto SupportsAllocatedFormats = [&](uint32_t sampleCount)
        {
            for (DXGI_FORMAT format : allocatedColorFormats)
            {
                uint32_t qualityLevels = 0u;
                if (!QueryMultisampleSupport(
                        nativeDevice,
                        format,
                        sampleCount,
                        qualityLevels))
                {
                    return false;
                }
            }
            uint32_t depthQualityLevels = 0u;
            return QueryMultisampleSupport(
                nativeDevice,
                selectedDepthDxgiFormat,
                sampleCount,
                depthQualityLevels);
        };
        for (uint32_t candidate : { 16u, 8u, 4u, 2u })
        {
            if (candidate <= requestedSampleCount &&
                SupportsAllocatedFormats(candidate))
            {
                return candidate;
            }
        }
        return 1u;
    }

    [[nodiscard]] const char* AdaptiveImplementationName(
        uvsr::AdaptiveMsaaImplementation implementation)
    {
        switch (implementation)
        {
        case uvsr::AdaptiveMsaaImplementation::Reference:
            return "reference";
        case uvsr::AdaptiveMsaaImplementation::AdaptiveGBuffer:
            return "adaptive-g-buffer";
        case uvsr::AdaptiveMsaaImplementation::CompactVisibility:
            return "compact-visibility";
        case uvsr::AdaptiveMsaaImplementation::Count:
            break;
        }
        return "unknown";
    }

    [[nodiscard]] float FloatFromBits(uint32_t bits)
    {
        float result = 0.f;
        static_assert(sizeof(result) == sizeof(bits));
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    [[nodiscard]] bool Near(float actual, float expected)
    {
        return std::isfinite(actual) &&
            std::abs(actual - expected) <= FloatTolerance;
    }

    [[nodiscard]] bool Check(
        bool condition,
        uint32_t sampleCount,
        const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "[FAIL] " << sampleCount << "x: " << message
                << '\n';
        }
        return condition;
    }

    void ClearD3D12InfoQueue(nvrhi::IDevice* device)
    {
        ID3D12Device* nativeDevice = device
            ? device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device)
            : nullptr;
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (nativeDevice && SUCCEEDED(nativeDevice->QueryInterface(
                IID_PPV_ARGS(&infoQueue))) && infoQueue)
        {
            infoQueue->ClearStoredMessages();
        }
    }

    void DumpD3D12InfoQueue(nvrhi::IDevice* device, uint32_t sampleCount)
    {
        ID3D12Device* nativeDevice = device
            ? device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device)
            : nullptr;
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (!nativeDevice || FAILED(nativeDevice->QueryInterface(
                IID_PPV_ARGS(&infoQueue))) || !infoQueue)
        {
            std::cerr << "[D3D12 INFO QUEUE] " << sampleCount
                << "x unavailable.\n";
            return;
        }

        const uint64_t messageCount =
            infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        std::cerr << "[D3D12 INFO QUEUE] " << sampleCount << "x stored="
            << messageCount << ".\n";
        for (uint64_t index = 0u; index < messageCount; ++index)
        {
            SIZE_T messageBytes = 0u;
            if (FAILED(infoQueue->GetMessage(index, nullptr, &messageBytes)) ||
                messageBytes < sizeof(D3D12_MESSAGE))
            {
                continue;
            }
            std::vector<uint8_t> storage(messageBytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(infoQueue->GetMessage(
                    index, message, &messageBytes)))
            {
                std::cerr << "[D3D12 MESSAGE] category="
                    << static_cast<uint32_t>(message->Category)
                    << " severity="
                    << static_cast<uint32_t>(message->Severity)
                    << " id=" << static_cast<uint32_t>(message->ID)
                    << " "
                    << (message->pDescription
                        ? message->pDescription
                        : "(null message)")
                    << '\n';
            }
        }
    }

    [[nodiscard]] nvrhi::BufferHandle CreateCompactBuffer(
        nvrhi::IDevice* device,
        uint64_t byteSize,
        uint32_t structStride,
        bool rawViews,
        bool unorderedAccess,
        bool constantBuffer,
        const std::string& debugName)
    {
        nvrhi::BufferDesc description;
        description.byteSize = byteSize;
        description.structStride = structStride;
        description.canHaveRawViews = rawViews;
        description.canHaveUAVs = unorderedAccess;
        description.isConstantBuffer = constantBuffer;
        description.isVolatile = constantBuffer;
        description.maxVersions = constantBuffer ? 4u : 0u;
        description.initialState = constantBuffer
            ? nvrhi::ResourceStates::ConstantBuffer
            : unorderedAccess
                ? nvrhi::ResourceStates::UnorderedAccess
                : nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        description.debugName = debugName;
        return device->createBuffer(description);
    }

    [[nodiscard]] nvrhi::TextureHandle CreateCompactTexture(
        nvrhi::IDevice* device,
        nvrhi::Format format,
        uint32_t width,
        uint32_t height,
        nvrhi::TextureDimension dimension,
        uint32_t sampleCount,
        uint32_t arraySize,
        bool renderTarget,
        bool unorderedAccess,
        const std::string& debugName,
        uint32_t mipLevels = 1u)
    {
        nvrhi::TextureDesc description;
        description.width = width;
        description.height = height;
        description.sampleCount = sampleCount;
        description.sampleQuality = 0u;
        description.arraySize = arraySize;
        description.mipLevels = mipLevels;
        description.format = format;
        description.dimension = dimension;
        description.isShaderResource = true;
        description.isRenderTarget = renderTarget;
        description.isUAV = unorderedAccess;
        description.initialState = unorderedAccess
            ? nvrhi::ResourceStates::UnorderedAccess
            : nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        description.debugName = debugName;
        return device->createTexture(description);
    }

    [[nodiscard]] CompactDiagnosticOutputs CreateCompactDiagnostics(
        nvrhi::IDevice* device,
        uint32_t sampleCount,
        const std::string& label)
    {
        CompactDiagnosticOutputs result;
        result.sampleStatus = CreateCompactTexture(
            device,
            nvrhi::Format::R8_UINT,
            CompactFixtureWidth,
            CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DArray,
            1u,
            sampleCount,
            false,
            true,
            label + "/Sample Status");
        result.multisampledBackground = CreateCompactTexture(
            device,
            nvrhi::Format::RGBA16_FLOAT,
            CompactFixtureWidth,
            CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DMS,
            sampleCount,
            1u,
            true,
            false,
            label + "/MSAA Background");
        result.selectedSampleData = CreateCompactBuffer(
            device,
            sizeof(DiagnosticsGpuSampleInputRecord) *
                sampleCount,
            sizeof(DiagnosticsGpuSampleInputRecord),
            false,
            true,
            false,
            label + "/Selected Sample Data");
        result.pixelMetadata = CreateCompactBuffer(
            device,
            sizeof(DiagnosticsGpuPixelMetadataRecord) *
                CompactFixturePixelCount,
            sizeof(DiagnosticsGpuPixelMetadataRecord),
            false,
            true,
            false,
            label + "/Pixel Metadata");
        return result;
    }

    [[nodiscard]] bool CompactDiagnosticsCreated(
        const CompactDiagnosticOutputs& outputs)
    {
        return outputs.sampleStatus && outputs.multisampledBackground &&
            outputs.selectedSampleData && outputs.pixelMetadata;
    }

    void ConfigureCompactProductionView(
        CompactProductionFixture& fixture,
        bool steepPerspective)
    {
        float4x4 reverseDepthProjection = float4x4::identity();
        reverseDepthProjection.m22 = -0.5f;
        if (steepPerspective)
        {
            // Preserve clip w = -world z while keeping the z/w block
            // invertible. Device depth is 0.5 + 0.125 / clipW and is
            // deliberately validated against the full-G-buffer raster
            // result rather than hard-coded to the affine depth.
            reverseDepthProjection.m23 = -1.f;
            reverseDepthProjection.m32 = 0.125f;
            reverseDepthProjection.m33 = 0.f;
        }
        fixture.view.SetMatrices(
            affine3::identity(),
            reverseDepthProjection);
        fixture.view.UpdateCache();
        fixture.view.FillPlanarViewConstants(
            fixture.gbufferConstants.view);
        fixture.gbufferConstants.viewPrev =
            fixture.gbufferConstants.view;
        fixture.compactConstants.gbuffer = fixture.gbufferConstants;
        fixture.view.FillPlanarViewConstants(
            fixture.deferredConstants.deferred.view);
        fixture.compactConstants.frontCounterClockwise =
            fixture.view.IsMirrored() ? 1u : 0u;
    }

    [[nodiscard]] bool CreateCompactProductionFixture(
        nvrhi::IDevice* device,
        donut::engine::ShaderFactory& shaderFactory,
        donut::engine::ShaderFactory& testShaderFactory,
        CompactProductionFixture& fixture,
        uint32_t sampleCount,
        ProductionFixtureScope scope =
            ProductionFixtureScope::CompactVisibility,
        nvrhi::Format visibilityClassFormat = nvrhi::Format::R8_UINT)
    {
        // This target intentionally does not link the renderer application or
        // its scene publisher. Bind the published GPU ABI directly so the
        // staged production visibility and lighting shaders execute without
        // duplicating either shader's grouping, reconstruction, or fallback
        // implementation in test code.
        const bool compactVisibility =
            scope == ProductionFixtureScope::CompactVisibility;
        if (!IsSupportedSampleCount(sampleCount) ||
            (compactVisibility &&
                visibilityClassFormat != nvrhi::Format::R8_UINT &&
                visibilityClassFormat != nvrhi::Format::R32_UINT &&
                visibilityClassFormat != nvrhi::Format::RG32_UINT))
            return false;
        fixture.scope = scope;
        fixture.sampleCount = sampleCount;
        fixture.compactConstants.sampleCount = sampleCount;
        fixture.view.SetViewport(nvrhi::Viewport(
            float(CompactFixtureWidth),
            float(CompactFixtureHeight)));
        fixture.compactConstants.scene.topologyEpochLow =
            CompactTopologyEpoch;
        fixture.compactConstants.scene.topologyRevision =
            CompactTopologyRevision;
        fixture.compactConstants.scene.lodRevision =
            CompactLodRevision;
        fixture.compactConstants.scene.geometryInstanceRecordCount = 2u;
        fixture.compactConstants.scene.geometryRecordCount = 2u;
        fixture.compactConstants.scene.materialRecordCount = 1u;
        fixture.compactConstants.scene.instanceHandleCount = 2u;
        fixture.compactConstants.scene.instanceBufferDescriptor =
            CompactRawInstanceDescriptor;
        fixture.compactConstants.scene.materialBufferDescriptor =
            CompactRawMaterialDescriptor;
        fixture.compactConstants.scene.rawBufferDescriptorCount =
            CompactRawDescriptorCount;
        fixture.compactConstants.scene.textureDescriptorCount =
            CompactMaterialTextureDescriptorCount;
        fixture.compactConstants.scene.deformationRevision =
            CompactDeformationRevision;
        fixture.compactConstants.scene.materialRevision =
            CompactMaterialRevision;
        fixture.compactConstants.scene.descriptorCount =
            CompactDescriptorCount;
        fixture.deferredConstants = {};
        fixture.deferredConstants.deferred.indirectDiffuseScale = 1.f;
        fixture.deferredConstants.directionalVisibilityLightIndex = -1;
        // The baseline affine map stores world z=-1 at reverse depth 0.5
        // while retaining an invertible clip-to-world transform.
        ConfigureCompactProductionView(fixture, false);

        fixture.surfaceOwnership = CreateCompactTexture(
            device,
            nvrhi::Format::RG32_UINT,
            CompactFixtureWidth,
            CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DMS,
            sampleCount,
            1u,
            true,
            false,
            "Adaptive MSAA Compact GPU Test/Surface Ownership");
        if (compactVisibility)
        {
            fixture.visibilityClass = CreateCompactTexture(
                device,
                visibilityClassFormat,
                CompactFixtureWidth,
                CompactFixtureHeight,
                nvrhi::TextureDimension::Texture2DMS,
                sampleCount,
                1u,
                true,
                false,
                "Adaptive MSAA Compact GPU Test/Visibility Class");
        }
        fixture.depth = CreateCompactTexture(
            device,
            compactVisibility
                ? nvrhi::Format::D32
                : AdaptiveGBufferProductDepthFormat,
            CompactFixtureWidth,
            CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DMS,
            sampleCount,
            1u,
            true,
            false,
            compactVisibility
                ? "Adaptive MSAA Compact GPU Test/Depth"
                : "Adaptive GBuffer GPU Test/Product D24S8 Depth");
        fixture.fallbackDiffuse = CreateCompactTexture(
            device, nvrhi::Format::SRGBA8_UNORM,
            CompactFixtureWidth, CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DMS,
            sampleCount, 1u, true, false,
            "Adaptive MSAA Compact GPU Test/Fallback Diffuse");
        fixture.fallbackMaterial = CreateCompactTexture(
            device, nvrhi::Format::RGBA8_UNORM,
            CompactFixtureWidth, CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DMS,
            sampleCount, 1u, true, false,
            "Adaptive MSAA Compact GPU Test/Fallback Material");
        fixture.fallbackNormals = CreateCompactTexture(
            device, nvrhi::Format::RGBA16_SNORM,
            CompactFixtureWidth, CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DMS,
            sampleCount, 1u, true, false,
            "Adaptive MSAA Compact GPU Test/Fallback Normals");
        fixture.fallbackEmissive = CreateCompactTexture(
            device, nvrhi::Format::RGBA16_FLOAT,
            CompactFixtureWidth, CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DMS,
            sampleCount, 1u, true, false,
            "Adaptive MSAA Compact GPU Test/Fallback Emissive");
        fixture.fallbackAmbientOcclusion = CreateCompactTexture(
            device, nvrhi::Format::R8_UNORM,
            CompactFixtureWidth, CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DMS,
            sampleCount, 1u, true, false,
            "Adaptive MSAA Compact GPU Test/Fallback AO");
        fixture.fallbackMotionVectors = CreateCompactTexture(
            device, nvrhi::Format::RGBA16_FLOAT,
            CompactFixtureWidth, CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2DMS,
            sampleCount, 1u, true, false,
            "Adaptive MSAA Compact GPU Test/Fallback Motion");
        fixture.resolvedBackground = CreateCompactTexture(
            device,
            nvrhi::Format::RGBA16_FLOAT,
            CompactFixtureWidth,
            CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2D,
            1u,
            1u,
            true,
            false,
            "Adaptive MSAA Compact GPU Test/Resolved Background");
        fixture.shadowMap = CreateCompactTexture(
            device,
            nvrhi::Format::D32,
            1u,
            1u,
            nvrhi::TextureDimension::Texture2DArray,
            1u,
            1u,
            false,
            false,
            "Adaptive MSAA Compact GPU Test/Shadow Map");
        fixture.diffuseEnvironment = CreateCompactTexture(
            device,
            nvrhi::Format::RGBA8_UNORM,
            1u,
            1u,
            nvrhi::TextureDimension::TextureCubeArray,
            1u,
            6u,
            false,
            false,
            "Adaptive MSAA Compact GPU Test/Diffuse Environment");
        fixture.specularEnvironment = CreateCompactTexture(
            device,
            nvrhi::Format::RGBA8_UNORM,
            1u,
            1u,
            nvrhi::TextureDimension::TextureCubeArray,
            1u,
            6u,
            false,
            false,
            "Adaptive MSAA Compact GPU Test/Specular Environment");
        fixture.environmentBrdf = CreateCompactTexture(
            device,
            nvrhi::Format::RGBA8_UNORM,
            1u,
            1u,
            nvrhi::TextureDimension::Texture2D,
            1u,
            1u,
            false,
            false,
            "Adaptive MSAA Compact GPU Test/Environment BRDF");
        fixture.directionalVisibility = CreateCompactTexture(
            device,
            nvrhi::Format::R8_UNORM,
            CompactFixtureWidth,
            CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2D,
            1u,
            1u,
            false,
            false,
            "Adaptive MSAA Compact GPU Test/Directional Visibility");

        const std::array<nvrhi::Format,
            CompactMaterialTextureDescriptorCount> uncompressedFormats = {
                nvrhi::Format::SRGBA8_UNORM,
                UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC &&
                        sampleCount == 4u
                    ? nvrhi::Format::RGBA8_UNORM
                    : nvrhi::Format::RG8_UNORM,
                nvrhi::Format::SRGBA8_UNORM,
                nvrhi::Format::RGBA8_UNORM,
                nvrhi::Format::R8_UNORM,
                nvrhi::Format::R8_UNORM,
                nvrhi::Format::R8_UNORM
            };
        const std::array<nvrhi::Format,
            CompactMaterialTextureDescriptorCount> compressedGuideFormats = {
                nvrhi::Format::BC1_UNORM_SRGB,
                nvrhi::Format::BC5_UNORM,
                nvrhi::Format::BC7_UNORM_SRGB,
                nvrhi::Format::BC5_UNORM,
                nvrhi::Format::BC4_UNORM,
                nvrhi::Format::BC4_UNORM,
                nvrhi::Format::BC4_UNORM
            };
        auto compressedAlphaFormats = compressedGuideFormats;
        compressedAlphaFormats[0u] = nvrhi::Format::BC3_UNORM_SRGB;
        const auto CreateMaterialPack = [&](const char* packName,
            const std::array<nvrhi::Format,
                CompactMaterialTextureDescriptorCount>& formats,
            std::array<nvrhi::TextureHandle,
                CompactMaterialTextureDescriptorCount>& textures)
        {
            const uint32_t textureSize = compactVisibility
                ? CompactMaterialTextureSize
                : 1u;
            const uint32_t mipLevels = compactVisibility
                ? CompactMaterialTextureMipCount
                : 1u;
            for (uint32_t texture = 0u;
                texture < CompactMaterialTextureDescriptorCount;
                ++texture)
            {
                const bool pocFinalizedMipRenderTarget =
                    UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC &&
                    sampleCount == 4u &&
                    textures.data() == fixture.materialTextures.data();
                textures[texture] = CreateCompactTexture(
                    device,
                    formats[texture],
                    textureSize,
                    textureSize,
                    nvrhi::TextureDimension::Texture2D,
                    1u,
                    1u,
                    pocFinalizedMipRenderTarget,
                    false,
                    std::string("Adaptive MSAA Compact GPU Test/") +
                        packName + "/Texture " +
                        std::to_string(texture),
                    mipLevels);
            }
        };
        CreateMaterialPack(
            "Uncompressed Material",
            uncompressedFormats,
            fixture.materialTextures);
        if (compactVisibility)
        {
            CreateMaterialPack(
                "Block-Compressed Guide Material",
                compressedGuideFormats,
                fixture.blockCompressedGuideTextures);
            CreateMaterialPack(
                "Block-Compressed Alpha Material",
                compressedAlphaFormats,
                fixture.blockCompressedAlphaTextures);
        }

        const std::array<nvrhi::Format, CompactGuideOutputCount>
            guideFormats = {
                nvrhi::Format::R32_FLOAT,
                nvrhi::Format::RGBA16_FLOAT,
                nvrhi::Format::RGBA16_FLOAT,
                nvrhi::Format::RGBA16_FLOAT,
                nvrhi::Format::RGBA16_FLOAT,
                nvrhi::Format::R16_FLOAT,
                nvrhi::Format::RGBA16_FLOAT
            };
        const std::array<const char*, CompactGuideOutputCount>
            guideNames = {
                "Adaptive MSAA Compact GPU Test/Guide Depth",
                "Adaptive MSAA Compact GPU Test/Guide Diffuse",
                "Adaptive MSAA Compact GPU Test/Guide Material",
                "Adaptive MSAA Compact GPU Test/Guide Normals",
                "Adaptive MSAA Compact GPU Test/Guide Emissive",
                "Adaptive MSAA Compact GPU Test/Guide AO",
                "Adaptive MSAA Compact GPU Test/Guide Motion"
            };
        if (compactVisibility)
        {
            for (uint32_t output = 0u;
                output < CompactGuideOutputCount;
                ++output)
            {
                fixture.guide.outputs[output] = CreateCompactTexture(
                    device,
                    guideFormats[output],
                    CompactFixtureWidth,
                    CompactFixtureHeight,
                    nvrhi::TextureDimension::Texture2D,
                    1u,
                    1u,
                    false,
                    true,
                    guideNames[output]);
                if (fixture.guide.outputs[output])
                {
                    fixture.guide.readbacks[output] =
                        device->createStagingTexture(
                            fixture.guide.outputs[output]->getDesc(),
                            nvrhi::CpuAccessMode::Read);
                }
            }
        }

        fixture.instanceBuffer = CreateCompactBuffer(
            device, sizeof(InstanceData), sizeof(InstanceData), true,
            false, false,
            "Adaptive MSAA Compact GPU Test/Instances");
        constexpr uint32_t PositionBytes = sizeof(float) * 3u * 6u;
        constexpr uint32_t TexCoordBytes = sizeof(float) * 2u * 6u;
        constexpr uint32_t PackedNormalBytes = sizeof(uint32_t) * 6u;
        constexpr uint32_t VertexBytes = PositionBytes + TexCoordBytes +
            PackedNormalBytes * 2u;
        fixture.vertexBuffer = CreateCompactBuffer(
            device, VertexBytes, 0u, true,
            false, false,
            "Adaptive MSAA Compact GPU Test/Vertices");
        fixture.instanceHandleBuffer = CreateCompactBuffer(
            device, sizeof(uint32_t) * 2u, sizeof(uint32_t), false,
            false, false,
            "Adaptive MSAA Compact GPU Test/Instance Handles");
        fixture.rasterViewConstantsBuffer = CreateCompactBuffer(
            device, sizeof(GBufferFillConstants), 0u, false,
            false, true,
            "Adaptive MSAA Compact GPU Test/Raster View Constants");
        fixture.deferredConstantsBuffer = CreateCompactBuffer(
            device, sizeof(PbrDeferredLightingConstants), 0u, false,
            false, true,
            "Adaptive MSAA Compact GPU Test/Deferred Constants");
        fixture.diagnosticsConstantsBuffer = CreateCompactBuffer(
            device, sizeof(DiagnosticsProducerGpuConstants), 0u, false,
            false, true,
            "Adaptive MSAA Compact GPU Test/Diagnostics Constants");
        fixture.fallbackMaterialConstantsBuffer = CreateCompactBuffer(
            device, sizeof(MaterialConstants), 0u, false,
            false, true,
            "Adaptive MSAA Compact GPU Test/Fallback Material Constants");
        if (!compactVisibility)
        {
            fixture.secondaryFallbackMaterialConstantsBuffer =
                CreateCompactBuffer(
                    device, sizeof(MaterialConstants), 0u, false,
                    false, true,
                    "Adaptive MSAA Compact GPU Test/Secondary Fallback "
                    "Material Constants");
        }
        if (compactVisibility)
        {
            fixture.indexBuffer = CreateCompactBuffer(
                device, sizeof(uint32_t) * 6u, 0u, true,
                false, false,
                "Adaptive MSAA Compact GPU Test/Indices");
            fixture.materialBuffer = CreateCompactBuffer(
                device, sizeof(MaterialConstants), 0u, true,
                false, false,
                "Adaptive MSAA Compact GPU Test/Materials");
            fixture.geometryInstanceBuffer = CreateCompactBuffer(
                device, sizeof(CompactGeometryInstanceGpuRecord) * 2u,
                sizeof(CompactGeometryInstanceGpuRecord), false,
                false, false,
                "Adaptive MSAA Compact GPU Test/Geometry Instances");
            fixture.geometryBuffer = CreateCompactBuffer(
                device, sizeof(CompactGeometryGpuRecord) * 2u,
                sizeof(CompactGeometryGpuRecord), false,
                false, false,
                "Adaptive MSAA Compact GPU Test/Geometry");
            fixture.compactConstantsBuffer = CreateCompactBuffer(
                device, sizeof(CompactTestShadeConstants), 0u, false,
                false, true,
                "Adaptive MSAA Compact GPU Test/Compact Constants");
        }

        fixture.materialSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap)
                .setMaxAnisotropy(16.f));
        fixture.shadowSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllAddressModes(nvrhi::SamplerAddressMode::Border)
                .setBorderColor(1.f)
                .setReductionType(
                    nvrhi::SamplerReductionType::Comparison));
        fixture.environmentSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Wrap));
        fixture.environmentBrdfSampler = device->createSampler(
            nvrhi::SamplerDesc()
                .setAllFilters(true)
                .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

        if (!fixture.surfaceOwnership ||
            (compactVisibility && !fixture.visibilityClass) ||
            !fixture.depth || !fixture.fallbackDiffuse ||
            !fixture.fallbackMaterial || !fixture.fallbackNormals ||
            !fixture.fallbackEmissive ||
            !fixture.fallbackAmbientOcclusion ||
            !fixture.fallbackMotionVectors ||
            !fixture.resolvedBackground || !fixture.shadowMap ||
            !fixture.diffuseEnvironment || !fixture.specularEnvironment ||
            !fixture.environmentBrdf || !fixture.directionalVisibility ||
            !fixture.instanceBuffer || !fixture.vertexBuffer ||
            !fixture.instanceHandleBuffer ||
            !fixture.rasterViewConstantsBuffer ||
            !fixture.deferredConstantsBuffer ||
            !fixture.diagnosticsConstantsBuffer ||
            !fixture.fallbackMaterialConstantsBuffer ||
            (!compactVisibility &&
                !fixture.secondaryFallbackMaterialConstantsBuffer) ||
            !fixture.materialSampler || !fixture.shadowSampler ||
            !fixture.environmentSampler ||
            !fixture.environmentBrdfSampler)
        {
            return false;
        }
        if (compactVisibility &&
            (!fixture.indexBuffer || !fixture.materialBuffer ||
                !fixture.geometryInstanceBuffer || !fixture.geometryBuffer ||
                !fixture.compactConstantsBuffer))
        {
            return false;
        }
        const auto MaterialPackCreated = [](const auto& textures)
        {
            return std::all_of(
                textures.begin(),
                textures.end(),
                [](const nvrhi::TextureHandle& texture)
                {
                    return bool(texture);
                });
        };
        if (!MaterialPackCreated(fixture.materialTextures) ||
            (compactVisibility &&
                (!MaterialPackCreated(
                    fixture.blockCompressedGuideTextures) ||
                    !MaterialPackCreated(
                        fixture.blockCompressedAlphaTextures))))
        {
            return false;
        }
        if (compactVisibility)
        {
            for (uint32_t output = 0u;
                output < CompactGuideOutputCount;
                ++output)
            {
                if (!fixture.guide.outputs[output] ||
                    !fixture.guide.readbacks[output])
                {
                    return false;
                }
            }
        }

        if (compactVisibility)
        {
            fixture.visibilityFramebuffer = device->createFramebuffer(
                nvrhi::FramebufferDesc()
                    .addColorAttachment(fixture.surfaceOwnership)
                    .addColorAttachment(fixture.visibilityClass)
                    .setDepthAttachment(fixture.depth));
            fixture.fallbackFramebuffer = device->createFramebuffer(
                nvrhi::FramebufferDesc()
                    .addColorAttachment(fixture.fallbackDiffuse)
                    .addColorAttachment(fixture.fallbackMaterial)
                    .addColorAttachment(fixture.fallbackNormals)
                    .addColorAttachment(fixture.fallbackEmissive)
                    .addColorAttachment(fixture.fallbackAmbientOcclusion)
                    .addColorAttachment(fixture.fallbackMotionVectors)
                    .addColorAttachment(fixture.surfaceOwnership)
                    .addColorAttachment(fixture.visibilityClass)
                    .setDepthAttachment(fixture.depth));
        }
        // Match the accepted production Adaptive G-Buffer attachment set
        // exactly: six native G-buffer targets plus the lossless RG32_UINT
        // key. Visibility class is neither attached nor written.
        fixture.adaptiveGBufferFramebuffer = device->createFramebuffer(
            nvrhi::FramebufferDesc()
                .addColorAttachment(fixture.fallbackDiffuse)
                .addColorAttachment(fixture.fallbackMaterial)
                .addColorAttachment(fixture.fallbackNormals)
                .addColorAttachment(fixture.fallbackEmissive)
                .addColorAttachment(fixture.fallbackAmbientOcclusion)
                .addColorAttachment(fixture.fallbackMotionVectors)
                .addColorAttachment(fixture.surfaceOwnership)
                .setDepthAttachment(fixture.depth));
        if (!fixture.adaptiveGBufferFramebuffer ||
            (compactVisibility &&
                (!fixture.visibilityFramebuffer ||
                    !fixture.fallbackFramebuffer)))
            return false;

        fixture.rasterViewLayout = device->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(
                    nvrhi::ShaderType::Vertex |
                    nvrhi::ShaderType::Pixel)
                .setRegisterSpaceAndDescriptorSet(GBUFFER_SPACE_VIEW)
                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    GBUFFER_BINDING_VIEW_CONSTANTS))
                .addItem(nvrhi::BindingLayoutItem::Sampler(
                    GBUFFER_BINDING_MATERIAL_SAMPLER)));
        fixture.rasterInputLayout = device->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(
                    nvrhi::ShaderType::Vertex |
                    nvrhi::ShaderType::Pixel)
                .setRegisterSpaceAndDescriptorSet(GBUFFER_SPACE_INPUT)
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    GBUFFER_BINDING_INSTANCE_BUFFER))
                .addItem(nvrhi::BindingLayoutItem::RawBuffer_SRV(
                    GBUFFER_BINDING_VERTEX_BUFFER))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    ADAPTIVE_MSAA_BINDING_GEOMETRY_INSTANCE_HANDLE_MAP))
                .addItem(nvrhi::BindingLayoutItem::PushConstants(
                    GBUFFER_BINDING_PUSH_CONSTANTS,
                    sizeof(AdaptiveMsaaGBufferPushConstants))));
        fixture.fallbackMaterialLayout = device->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(nvrhi::ShaderType::Pixel)
                .setRegisterSpaceAndDescriptorSet(GBUFFER_SPACE_MATERIAL)
                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    GBUFFER_BINDING_MATERIAL_CONSTANTS))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_SPECULAR_TEXTURE))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_NORMAL_TEXTURE))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_EMISSIVE_TEXTURE))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_OCCLUSION_TEXTURE))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_TRANSMISSION_TEXTURE))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE)));
        if (!fixture.rasterViewLayout || !fixture.rasterInputLayout ||
            !fixture.fallbackMaterialLayout)
            return false;

        fixture.rasterViewBindingSet = device->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::ConstantBuffer(
                    GBUFFER_BINDING_VIEW_CONSTANTS,
                    fixture.rasterViewConstantsBuffer))
                .addItem(nvrhi::BindingSetItem::Sampler(
                    GBUFFER_BINDING_MATERIAL_SAMPLER,
                    fixture.materialSampler)),
            fixture.rasterViewLayout);
        fixture.rasterInputBindingSet = device->createBindingSet(
            nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    GBUFFER_BINDING_INSTANCE_BUFFER,
                    fixture.instanceBuffer))
                .addItem(nvrhi::BindingSetItem::RawBuffer_SRV(
                    GBUFFER_BINDING_VERTEX_BUFFER,
                    fixture.vertexBuffer))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(
                    ADAPTIVE_MSAA_BINDING_GEOMETRY_INSTANCE_HANDLE_MAP,
                    fixture.instanceHandleBuffer))
                .addItem(nvrhi::BindingSetItem::PushConstants(
                    GBUFFER_BINDING_PUSH_CONSTANTS,
                    sizeof(AdaptiveMsaaGBufferPushConstants))),
            fixture.rasterInputLayout);
        const auto CreateMaterialBindingSet = [&](const auto& textures,
            nvrhi::IBuffer* materialConstants)
        {
            nvrhi::BindingSetDesc description;
            description.bindings = {
                nvrhi::BindingSetItem::ConstantBuffer(
                    GBUFFER_BINDING_MATERIAL_CONSTANTS,
                    materialConstants),
                nvrhi::BindingSetItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_DIFFUSE_TEXTURE,
                    textures[0u]),
                nvrhi::BindingSetItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_SPECULAR_TEXTURE,
                    textures[1u]),
                nvrhi::BindingSetItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_NORMAL_TEXTURE,
                    textures[3u]),
                nvrhi::BindingSetItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_EMISSIVE_TEXTURE,
                    textures[2u]),
                nvrhi::BindingSetItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_OCCLUSION_TEXTURE,
                    textures[4u]),
                nvrhi::BindingSetItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_TRANSMISSION_TEXTURE,
                    textures[5u]),
                nvrhi::BindingSetItem::Texture_SRV(
                    GBUFFER_BINDING_MATERIAL_OPACITY_TEXTURE,
                    textures[6u])
            };
            return device->createBindingSet(
                description,
                fixture.fallbackMaterialLayout);
        };
        fixture.fallbackMaterialBindingSet = CreateMaterialBindingSet(
            fixture.materialTextures,
            fixture.fallbackMaterialConstantsBuffer);
        if (!compactVisibility)
        {
            fixture.secondaryFallbackMaterialBindingSet =
                CreateMaterialBindingSet(
                    fixture.materialTextures,
                    fixture.secondaryFallbackMaterialConstantsBuffer);
        }
        if (compactVisibility)
        {
            fixture.blockCompressedGuideMaterialBindingSet =
                CreateMaterialBindingSet(
                    fixture.blockCompressedGuideTextures,
                    fixture.fallbackMaterialConstantsBuffer);
            fixture.blockCompressedAlphaMaterialBindingSet =
                CreateMaterialBindingSet(
                    fixture.blockCompressedAlphaTextures,
                    fixture.fallbackMaterialConstantsBuffer);
        }
        if (!fixture.rasterViewBindingSet ||
            !fixture.rasterInputBindingSet ||
            !fixture.fallbackMaterialBindingSet ||
            (!compactVisibility &&
                !fixture.secondaryFallbackMaterialBindingSet) ||
            (compactVisibility &&
                (!fixture.blockCompressedGuideMaterialBindingSet ||
                    !fixture.blockCompressedAlphaMaterialBindingSet)))
        {
            return false;
        }

        if (compactVisibility)
        {
            std::vector<donut::engine::ShaderMacro> rasterMacros;
            rasterMacros.emplace_back("ALPHA_TESTED", "0");
            const nvrhi::ShaderHandle visibilityVs =
                shaderFactory.CreateShader(
                    "uvsr/adaptive_msaa_compact_visibility_vs.hlsl",
                    "main",
                    &rasterMacros,
                    nvrhi::ShaderType::Vertex);
            const nvrhi::ShaderHandle visibilityPs =
                shaderFactory.CreateShader(
                    "uvsr/adaptive_msaa_compact_visibility_ps.hlsl",
                    "main",
                    &rasterMacros,
                    nvrhi::ShaderType::Pixel);
            std::vector<donut::engine::ShaderMacro> alphaRasterMacros;
            alphaRasterMacros.emplace_back("ALPHA_TESTED", "1");
            const nvrhi::ShaderHandle visibilityAlphaVs =
                shaderFactory.CreateShader(
                    "uvsr/adaptive_msaa_compact_visibility_vs.hlsl",
                    "main",
                    &alphaRasterMacros,
                    nvrhi::ShaderType::Vertex);
            const nvrhi::ShaderHandle visibilityAlphaPs =
                shaderFactory.CreateShader(
                    "uvsr/adaptive_msaa_compact_visibility_ps.hlsl",
                    "main",
                    &alphaRasterMacros,
                    nvrhi::ShaderType::Pixel);
            if (!visibilityVs || !visibilityPs || !visibilityAlphaVs ||
                !visibilityAlphaPs)
                return false;

            nvrhi::GraphicsPipelineDesc visibilityPipelineDesc;
            visibilityPipelineDesc.VS = visibilityVs;
            visibilityPipelineDesc.PS = visibilityPs;
            visibilityPipelineDesc.bindingLayouts = {
                fixture.rasterViewLayout,
                fixture.rasterInputLayout
            };
            visibilityPipelineDesc.renderState.rasterState
                .setFrontCounterClockwise(false)
                .setCullNone();
            visibilityPipelineDesc.renderState.blendState
                .disableAlphaToCoverage();
            visibilityPipelineDesc.renderState.depthStencilState
                .setDepthWriteEnable(true)
                .setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
            fixture.visibilityPipeline = device->createGraphicsPipeline(
                visibilityPipelineDesc,
                fixture.visibilityFramebuffer->getFramebufferInfo());
            nvrhi::GraphicsPipelineDesc visibilityAlphaPipelineDesc =
                visibilityPipelineDesc;
            visibilityAlphaPipelineDesc.VS = visibilityAlphaVs;
            visibilityAlphaPipelineDesc.PS = visibilityAlphaPs;
            visibilityAlphaPipelineDesc.bindingLayouts = {
                fixture.fallbackMaterialLayout,
                fixture.rasterViewLayout,
                fixture.rasterInputLayout
            };
            fixture.visibilityAlphaPipeline = device->createGraphicsPipeline(
                visibilityAlphaPipelineDesc,
                fixture.visibilityFramebuffer->getFramebufferInfo());
            if (!fixture.visibilityPipeline ||
                !fixture.visibilityAlphaPipeline)
                return false;
        }

        std::vector<donut::engine::ShaderMacro> fallbackVsMacros;
        fallbackVsMacros.emplace_back("MOTION_VECTORS", "1");
        std::vector<donut::engine::ShaderMacro> fallbackPsMacros;
        fallbackPsMacros.emplace_back("ALPHA_TESTED", "0");
        fallbackPsMacros.emplace_back("MOTION_VECTORS", "1");
        fallbackPsMacros.emplace_back("WHITE_WORLD", "0");
        fallbackPsMacros.emplace_back("COMPACT_FALLBACK_GBUFFER", "1");
        const nvrhi::ShaderHandle fallbackVs = shaderFactory.CreateShader(
            "uvsr/adaptive_msaa_gbuffer_vs.hlsl",
            "main",
            &fallbackVsMacros,
            nvrhi::ShaderType::Vertex);
        nvrhi::ShaderHandle fallbackPs;
        nvrhi::ShaderHandle fallbackAlphaPs;
        if (compactVisibility)
        {
            fallbackPs = shaderFactory.CreateShader(
                "uvsr/adaptive_msaa_gbuffer_ps.hlsl",
                "main",
                &fallbackPsMacros,
                nvrhi::ShaderType::Pixel);
            std::vector<donut::engine::ShaderMacro> fallbackAlphaPsMacros =
                fallbackPsMacros;
            fallbackAlphaPsMacros[0u] =
                donut::engine::ShaderMacro("ALPHA_TESTED", "1");
            fallbackAlphaPs = shaderFactory.CreateShader(
                "uvsr/adaptive_msaa_gbuffer_ps.hlsl",
                "main",
                &fallbackAlphaPsMacros,
                nvrhi::ShaderType::Pixel);
        }
        std::vector<donut::engine::ShaderMacro> adaptiveGBufferPsMacros =
            fallbackPsMacros;
        adaptiveGBufferPsMacros[3u] = donut::engine::ShaderMacro(
            "COMPACT_FALLBACK_GBUFFER", "0");
        const nvrhi::ShaderHandle adaptiveGBufferPs =
            shaderFactory.CreateShader(
                "uvsr/adaptive_msaa_gbuffer_ps.hlsl",
                "main",
                &adaptiveGBufferPsMacros,
                nvrhi::ShaderType::Pixel);
        if (!fallbackVs || !adaptiveGBufferPs ||
            (compactVisibility && (!fallbackPs || !fallbackAlphaPs)))
            return false;
        nvrhi::GraphicsPipelineDesc gbufferPipelineDesc;
        gbufferPipelineDesc.VS = fallbackVs;
        gbufferPipelineDesc.bindingLayouts = {
            fixture.fallbackMaterialLayout,
            fixture.rasterViewLayout,
            fixture.rasterInputLayout
        };
        gbufferPipelineDesc.renderState.rasterState
            .setFrontCounterClockwise(false)
            .setCullNone();
        gbufferPipelineDesc.renderState.blendState
            .disableAlphaToCoverage();
        gbufferPipelineDesc.renderState.depthStencilState
            .setDepthWriteEnable(true)
            .setDepthFunc(nvrhi::ComparisonFunc::GreaterOrEqual);
        if (compactVisibility)
        {
            nvrhi::GraphicsPipelineDesc fallbackPipelineDesc =
                gbufferPipelineDesc;
            fallbackPipelineDesc.PS = fallbackPs;
            fixture.fallbackPipeline = device->createGraphicsPipeline(
                fallbackPipelineDesc,
                fixture.fallbackFramebuffer->getFramebufferInfo());
            nvrhi::GraphicsPipelineDesc fallbackAlphaPipelineDesc =
                fallbackPipelineDesc;
            fallbackAlphaPipelineDesc.PS = fallbackAlphaPs;
            fixture.fallbackAlphaPipeline = device->createGraphicsPipeline(
                fallbackAlphaPipelineDesc,
                fixture.fallbackFramebuffer->getFramebufferInfo());
        }
        nvrhi::GraphicsPipelineDesc adaptiveGBufferPipelineDesc =
            gbufferPipelineDesc;
        adaptiveGBufferPipelineDesc.PS = adaptiveGBufferPs;
        fixture.adaptiveGBufferPipeline = device->createGraphicsPipeline(
            adaptiveGBufferPipelineDesc,
            fixture.adaptiveGBufferFramebuffer->getFramebufferInfo());
        nvrhi::GraphicsPipelineDesc backgroundMaskPipelineDesc =
            adaptiveGBufferPipelineDesc;
        for (uint32_t target = 0u; target < 7u; ++target)
        {
            backgroundMaskPipelineDesc.renderState.blendState.targets[target]
                .setColorWriteMask(static_cast<nvrhi::ColorMask>(0u));
        }
        backgroundMaskPipelineDesc.renderState.blendState.targets[3u]
            .setColorWriteMask(nvrhi::ColorMask::All)
            .enableBlend()
            .setSrcBlend(nvrhi::BlendFactor::Zero)
            .setDestBlend(nvrhi::BlendFactor::Zero)
            .setSrcBlendAlpha(nvrhi::BlendFactor::Zero)
            .setDestBlendAlpha(nvrhi::BlendFactor::Zero);
        backgroundMaskPipelineDesc.renderState.depthStencilState
            .disableDepthWrite()
            .setDepthFunc(nvrhi::ComparisonFunc::Equal);
        fixture.adaptiveGBufferBackgroundMaskPipeline =
            device->createGraphicsPipeline(
                backgroundMaskPipelineDesc,
                fixture.adaptiveGBufferFramebuffer->getFramebufferInfo());
        if (!fixture.adaptiveGBufferPipeline ||
            (compactVisibility &&
                (!fixture.fallbackPipeline ||
                    !fixture.fallbackAlphaPipeline)) ||
            !fixture.adaptiveGBufferBackgroundMaskPipeline)
        {
            DumpD3D12InfoQueue(device, sampleCount);
            return false;
        }

        if (!compactVisibility)
            return true;

        fixture.compactGuideLayout = device->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(nvrhi::ShaderType::Compute)
                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    CompactBindingConstants))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingDepth))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingSurfaceOwnership))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingGeometryInstances))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingGeometry))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingInstanceHandles))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingVisibilityClass))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackDiffuse))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackMaterial))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackNormals))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackEmissive))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackAmbientOcclusion))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackMotionVectors))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0u))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(1u))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(2u))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(3u))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(4u))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(5u))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(6u))
                .addItem(nvrhi::BindingLayoutItem::Sampler(0u)));
        fixture.compactFixedLayout = device->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(nvrhi::ShaderType::Compute)
                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0u))
                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    CompactBindingConstants))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0u))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1u))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2u))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(3u))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingDepth))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingResolvedBackground))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingDirectionalVisibility))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingSurfaceOwnership))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingGeometryInstances))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingGeometry))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(
                    CompactBindingInstanceHandles))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingVisibilityClass))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackDiffuse))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackMaterial))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackNormals))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackEmissive))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackAmbientOcclusion))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingFallbackMotionVectors))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0u))
                .addItem(nvrhi::BindingLayoutItem::Sampler(0u))
                .addItem(nvrhi::BindingLayoutItem::Sampler(1u))
                .addItem(nvrhi::BindingLayoutItem::Sampler(2u))
                .addItem(nvrhi::BindingLayoutItem::Sampler(3u))
                .addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    CompactBindingDiagnosticsConstants))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactBindingDiagnosticsBackground))
                .addItem(nvrhi::BindingLayoutItem::Texture_UAV(1u))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2u))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3u)));
        fixture.compactBindlessLayout = device->createBindlessLayout(
            nvrhi::BindlessLayoutDesc()
                .setVisibility(nvrhi::ShaderType::All)
                .setFirstSlot(0u)
                .setMaxCapacity(CompactDescriptorCount)
                .setLayoutType(
                    nvrhi::BindlessLayoutDesc::LayoutType::Immutable)
                .addRegisterSpace(nvrhi::BindingLayoutItem::Texture_SRV(
                    CompactTextureRegisterSpace))
                .addRegisterSpace(nvrhi::BindingLayoutItem::RawBuffer_SRV(
                    CompactRawBufferRegisterSpace)));
        fixture.msaaExtractLayout = device->createBindingLayout(
            nvrhi::BindingLayoutDesc()
                .setVisibility(nvrhi::ShaderType::Compute)
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0u))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(1u))
                .addItem(nvrhi::BindingLayoutItem::Texture_SRV(2u))
                .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(
                    0u))
                .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(1u)));
        if (!fixture.compactGuideLayout || !fixture.compactFixedLayout ||
            !fixture.compactBindlessLayout ||
            !fixture.msaaExtractLayout)
        {
            return false;
        }

        const std::array<nvrhi::IBuffer*, CompactRawDescriptorCount>
            rawBuffers = {
                fixture.instanceBuffer.Get(),
                fixture.indexBuffer.Get(),
                fixture.vertexBuffer.Get(),
                fixture.materialBuffer.Get()
            };
        const auto CreateCompactDescriptorTable = [&](const auto& textures)
        {
            nvrhi::DescriptorTableHandle table =
                device->createDescriptorTable(
                    fixture.compactBindlessLayout);
            if (!table)
                return nvrhi::DescriptorTableHandle{};
            device->resizeDescriptorTable(
                table,
                CompactDescriptorCount,
                false);
            if (table->getCapacity() < CompactDescriptorCount)
                return nvrhi::DescriptorTableHandle{};
            for (uint32_t descriptor = 0u;
                descriptor < CompactMaterialTextureDescriptorCount;
                ++descriptor)
            {
                if (!device->writeDescriptorTable(
                        table,
                        nvrhi::BindingSetItem::Texture_SRV(
                            descriptor,
                            textures[descriptor])))
                {
                    return nvrhi::DescriptorTableHandle{};
                }
            }
            for (uint32_t descriptor = 0u;
                descriptor < CompactRawDescriptorCount;
                ++descriptor)
            {
                if (!device->writeDescriptorTable(
                        table,
                        nvrhi::BindingSetItem::RawBuffer_SRV(
                            CompactMaterialTextureDescriptorCount +
                                descriptor,
                            rawBuffers[descriptor])))
                {
                    return nvrhi::DescriptorTableHandle{};
                }
            }
            return table;
        };
        fixture.compactDescriptorTable = CreateCompactDescriptorTable(
            fixture.materialTextures);
        fixture.blockCompressedGuideDescriptorTable =
            CreateCompactDescriptorTable(
                fixture.blockCompressedGuideTextures);
        fixture.blockCompressedAlphaDescriptorTable =
            CreateCompactDescriptorTable(
                fixture.blockCompressedAlphaTextures);
        if (!fixture.compactDescriptorTable ||
            !fixture.blockCompressedGuideDescriptorTable ||
            !fixture.blockCompressedAlphaDescriptorTable)
        {
            return false;
        }

        nvrhi::BindingSetDesc guideBindingSetDesc;
        guideBindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(
                CompactBindingConstants,
                fixture.compactConstantsBuffer),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingDepth,
                fixture.depth),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingSurfaceOwnership,
                fixture.surfaceOwnership),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                CompactBindingGeometryInstances,
                fixture.geometryInstanceBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                CompactBindingGeometry,
                fixture.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                CompactBindingInstanceHandles,
                fixture.instanceHandleBuffer),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingVisibilityClass,
                fixture.visibilityClass),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackDiffuse,
                fixture.fallbackDiffuse),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackMaterial,
                fixture.fallbackMaterial),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackNormals,
                fixture.fallbackNormals),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackEmissive,
                fixture.fallbackEmissive),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackAmbientOcclusion,
                fixture.fallbackAmbientOcclusion),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackMotionVectors,
                fixture.fallbackMotionVectors),
            nvrhi::BindingSetItem::Texture_UAV(
                0u, fixture.guide.outputs[CompactGuideDepth]),
            nvrhi::BindingSetItem::Texture_UAV(
                1u, fixture.guide.outputs[CompactGuideDiffuse]),
            nvrhi::BindingSetItem::Texture_UAV(
                2u, fixture.guide.outputs[CompactGuideMaterial]),
            nvrhi::BindingSetItem::Texture_UAV(
                3u, fixture.guide.outputs[CompactGuideNormals]),
            nvrhi::BindingSetItem::Texture_UAV(
                4u, fixture.guide.outputs[CompactGuideEmissive]),
            nvrhi::BindingSetItem::Texture_UAV(
                5u,
                fixture.guide.outputs[CompactGuideAmbientOcclusion]),
            nvrhi::BindingSetItem::Texture_UAV(
                6u, fixture.guide.outputs[CompactGuideMotion]),
            nvrhi::BindingSetItem::Sampler(0u, fixture.materialSampler)
        };
        fixture.guide.bindingSet = device->createBindingSet(
            guideBindingSetDesc,
            fixture.compactGuideLayout);
        if (!fixture.guide.bindingSet)
            return false;

        std::vector<donut::engine::ShaderMacro> resolveMacros;
        resolveMacros.emplace_back(
            "ADAPTIVE_MSAA_COMPACT_SAMPLES",
            std::to_string(sampleCount));
        resolveMacros.emplace_back("WHITE_WORLD", "0");
        const nvrhi::ShaderHandle resolveShader =
            shaderFactory.CreateShader(
                "uvsr/adaptive_msaa_compact_shade_resolve_cs.hlsl",
                "main",
                &resolveMacros,
                nvrhi::ShaderType::Compute);
        if (!resolveShader)
            return false;
        nvrhi::ComputePipelineDesc resolvePipelineDesc;
        resolvePipelineDesc.CS = resolveShader;
        resolvePipelineDesc.bindingLayouts = {
            fixture.compactGuideLayout,
            fixture.compactBindlessLayout
        };
        fixture.compactResolvePipeline =
            device->createComputePipeline(resolvePipelineDesc);
        if (!fixture.compactResolvePipeline)
        {
            std::cerr << "[FAIL] " << sampleCount
                << "x: compact guide resolve compute pipeline creation "
                    "failed.\n";
            DumpD3D12InfoQueue(device, sampleCount);
            return false;
        }

        std::vector<donut::engine::ShaderMacro> compactMacros;
        compactMacros.emplace_back(
            "ADAPTIVE_MSAA_COMPACT_SAMPLES",
            std::to_string(sampleCount));
        compactMacros.emplace_back(
            "ADAPTIVE_MSAA_COMPACT_VISIBILITY", "0");
        compactMacros.emplace_back("WHITE_WORLD", "0");
        compactMacros.emplace_back(
            "UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER", "1");
        compactMacros.emplace_back(
            "UVSR_ADAPTIVE_MSAA_COMPACT_PREPARATION_TOPOLOGY", "1");
        const nvrhi::ShaderHandle compactShader =
            shaderFactory.CreateShader(
                "uvsr/adaptive_msaa_compact_shade_lighting_cs.hlsl",
                "main",
                &compactMacros,
                nvrhi::ShaderType::Compute);
        if (!compactShader)
            return false;
        nvrhi::ComputePipelineDesc compactPipelineDesc;
        compactPipelineDesc.CS = compactShader;
        compactPipelineDesc.bindingLayouts = {
            fixture.compactFixedLayout,
            fixture.compactBindlessLayout
        };
        fixture.compactLightingPipeline =
            device->createComputePipeline(compactPipelineDesc);
        if (!fixture.compactLightingPipeline)
        {
            std::cerr << "[FAIL] " << sampleCount
                << "x: compact lighting compute pipeline creation failed.\n";
            DumpD3D12InfoQueue(device, sampleCount);
            return false;
        }

        std::vector<donut::engine::ShaderMacro> extractMacros;
        extractMacros.emplace_back(
            "UVSR_TEST_MSAA_SAMPLES",
            std::to_string(sampleCount));
        const nvrhi::ShaderHandle extractShader =
            testShaderFactory.CreateShader(
                "uvsr-tests/adaptive_msaa_msaa_extract_cs.hlsl",
                "main",
                &extractMacros,
                nvrhi::ShaderType::Compute);
        if (!extractShader)
            return false;
        nvrhi::ComputePipelineDesc extractPipelineDesc;
        extractPipelineDesc.CS = extractShader;
        extractPipelineDesc.bindingLayouts = {
            fixture.msaaExtractLayout
        };
        fixture.msaaExtractPipeline =
            device->createComputePipeline(extractPipelineDesc);
        if (!fixture.msaaExtractPipeline)
        {
            std::cerr << "[FAIL] " << sampleCount
                << "x: compact MSAA extract compute pipeline creation "
                    "failed.\n";
            DumpD3D12InfoQueue(device, sampleCount);
        }
        return bool(fixture.msaaExtractPipeline);
    }

    [[nodiscard]] bool CreateAdaptiveGBufferProductionPipelines(
        nvrhi::IDevice* device,
        donut::engine::ShaderFactory& shaderFactory,
        uint32_t sampleCount,
        AdaptiveGBufferProductionPipelines& pipelines)
    {
        pipelines = {};
        const auto CreateLayout = [device](
            bool adaptive,
            bool diagnostics)
        {
            nvrhi::BindingLayoutDesc description;
            description.visibility = nvrhi::ShaderType::Compute;
            description.bindings = {
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(0u),
                nvrhi::BindingLayoutItem::Texture_SRV(0u),
                nvrhi::BindingLayoutItem::Texture_SRV(1u),
                nvrhi::BindingLayoutItem::Texture_SRV(2u),
                nvrhi::BindingLayoutItem::Texture_SRV(3u),
                nvrhi::BindingLayoutItem::Texture_SRV(8u),
                nvrhi::BindingLayoutItem::Texture_SRV(9u),
                nvrhi::BindingLayoutItem::Texture_SRV(10u),
                nvrhi::BindingLayoutItem::Texture_SRV(11u),
                nvrhi::BindingLayoutItem::Texture_SRV(12u),
                nvrhi::BindingLayoutItem::Texture_SRV(14u),
                nvrhi::BindingLayoutItem::Texture_SRV(17u),
                nvrhi::BindingLayoutItem::Texture_SRV(20u),
                nvrhi::BindingLayoutItem::Texture_UAV(0u),
                nvrhi::BindingLayoutItem::Sampler(1u),
                nvrhi::BindingLayoutItem::Sampler(2u),
                nvrhi::BindingLayoutItem::Sampler(3u)
            };
            if (adaptive)
            {
                description.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(21u));
            }
            if (diagnostics)
            {
                description.bindings.push_back(
                    nvrhi::BindingLayoutItem::VolatileConstantBuffer(2u));
                description.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(25u));
                description.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_SRV(26u));
                description.bindings.push_back(
                    nvrhi::BindingLayoutItem::Texture_UAV(1u));
                description.bindings.push_back(
                    nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2u));
                description.bindings.push_back(
                    nvrhi::BindingLayoutItem::StructuredBuffer_UAV(3u));
            }
            return device->createBindingLayout(description);
        };

        pipelines.referenceLayout = CreateLayout(false, false);
        pipelines.adaptiveLayout = CreateLayout(true, false);
        pipelines.adaptiveDiagnosticsLayout = CreateLayout(true, true);
        if (!pipelines.referenceLayout || !pipelines.adaptiveLayout ||
            !pipelines.adaptiveDiagnosticsLayout)
        {
            return false;
        }

        const auto CreatePipeline = [&shaderFactory, device, sampleCount](
            bool adaptive,
            bool diagnostics,
            AdaptiveGBufferTestTopology topology,
            nvrhi::IBindingLayout* layout)
        {
            std::vector<donut::engine::ShaderMacro> macros;
            macros.emplace_back(
                "PBR_DEFERRED_MSAA_SAMPLES",
                std::to_string(sampleCount));
            macros.emplace_back("PBR_DEFERRED_MSAA_VISIBILITY", "0");
            macros.emplace_back(
                "UVSR_ADAPTIVE_MSAA_DIAGNOSTICS_PRODUCER",
                diagnostics ? "1" : "0");
            if (adaptive)
            {
                macros.emplace_back(
                    "UVSR_ADAPTIVE_MSAA_GBUFFER_PREPARATION_TOPOLOGY",
                    topology == AdaptiveGBufferTestTopology::Separated
                        ? "1"
                        : "0");
            }
            const nvrhi::ShaderHandle shader = shaderFactory.CreateShader(
                adaptive
                    ? "uvsr/pbr_deferred_lighting_adaptive_msaa_cs.hlsl"
                    : "uvsr/pbr_deferred_lighting_msaa_cs.hlsl",
                "main",
                &macros,
                nvrhi::ShaderType::Compute);
            if (!shader || !layout)
                return nvrhi::ComputePipelineHandle{};
            nvrhi::ComputePipelineDesc description;
            description.CS = shader;
            description.bindingLayouts = { layout };
            return device->createComputePipeline(description);
        };

        pipelines.reference = CreatePipeline(
            false,
            false,
            AdaptiveGBufferTestTopology::Inline,
            pipelines.referenceLayout);
        for (uint32_t topologyIndex = 0u;
            topologyIndex < pipelines.adaptive.size();
            ++topologyIndex)
        {
            const AdaptiveGBufferTestTopology topology =
                static_cast<AdaptiveGBufferTestTopology>(topologyIndex);
            pipelines.adaptive[topologyIndex] = CreatePipeline(
                true,
                false,
                topology,
                pipelines.adaptiveLayout);
            pipelines.adaptiveDiagnostics[topologyIndex] = CreatePipeline(
                true,
                true,
                topology,
                pipelines.adaptiveDiagnosticsLayout);
        }
        return pipelines.reference &&
            std::all_of(
                pipelines.adaptive.begin(),
                pipelines.adaptive.end(),
                [](const nvrhi::ComputePipelineHandle& pipeline)
                {
                    return bool(pipeline);
                }) &&
            std::all_of(
                pipelines.adaptiveDiagnostics.begin(),
                pipelines.adaptiveDiagnostics.end(),
                [](const nvrhi::ComputePipelineHandle& pipeline)
                {
                    return bool(pipeline);
                });
    }

    [[nodiscard]] bool CreateAdaptiveGBufferLightingRun(
        nvrhi::IDevice* device,
        const CompactProductionFixture& fixture,
        nvrhi::IBindingLayout* layout,
        bool adaptive,
        bool diagnostics,
        const std::string& label,
        CompactLightingRun& run)
    {
        run = {};
        run.output = CreateCompactTexture(
            device,
            nvrhi::Format::RGBA16_FLOAT,
            CompactFixtureWidth,
            CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2D,
            1u,
            1u,
            false,
            true,
            label + "/Lighting Output");
        if (!run.output || !layout)
            return false;
        run.outputReadback = device->createStagingTexture(
            run.output->getDesc(),
            nvrhi::CpuAccessMode::Read);
        if (!run.outputReadback)
            return false;

        if (diagnostics)
        {
            run.visualization = CreateCompactTexture(
                device,
                nvrhi::Format::RGBA16_FLOAT,
                CompactFixtureWidth,
                CompactFixtureHeight,
                nvrhi::TextureDimension::Texture2D,
                1u,
                1u,
                false,
                true,
                label + "/Error Visualization");
            run.diagnostics = CreateCompactDiagnostics(
                device,
                fixture.sampleCount,
                label);
            if (!run.visualization ||
                !CompactDiagnosticsCreated(run.diagnostics))
            {
                return false;
            }
            run.visualizationReadback = device->createStagingTexture(
                run.visualization->getDesc(),
                nvrhi::CpuAccessMode::Read);
            run.backgroundMaskFramebuffer = device->createFramebuffer(
                nvrhi::FramebufferDesc()
                    .addColorAttachment(fixture.fallbackDiffuse)
                    .addColorAttachment(fixture.fallbackMaterial)
                    .addColorAttachment(fixture.fallbackNormals)
                    .addColorAttachment(
                        run.diagnostics.multisampledBackground)
                    .addColorAttachment(
                        fixture.fallbackAmbientOcclusion)
                    .addColorAttachment(fixture.fallbackMotionVectors)
                    .addColorAttachment(fixture.surfaceOwnership)
                    .setDepthAttachment(fixture.depth));
            if (!run.visualizationReadback ||
                !run.backgroundMaskFramebuffer)
                return false;
        }

        nvrhi::BindingSetDesc bindingSetDescription;
        bindingSetDescription.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(
                0u, fixture.deferredConstantsBuffer),
            nvrhi::BindingSetItem::Texture_SRV(0u, fixture.shadowMap),
            nvrhi::BindingSetItem::Texture_SRV(
                1u, fixture.diffuseEnvironment),
            nvrhi::BindingSetItem::Texture_SRV(
                2u, fixture.specularEnvironment),
            nvrhi::BindingSetItem::Texture_SRV(
                3u, fixture.environmentBrdf),
            nvrhi::BindingSetItem::Texture_SRV(8u, fixture.depth),
            nvrhi::BindingSetItem::Texture_SRV(
                9u, fixture.fallbackDiffuse),
            nvrhi::BindingSetItem::Texture_SRV(
                10u, fixture.fallbackMaterial),
            nvrhi::BindingSetItem::Texture_SRV(
                11u, fixture.fallbackNormals),
            nvrhi::BindingSetItem::Texture_SRV(
                12u, fixture.fallbackEmissive),
            nvrhi::BindingSetItem::Texture_SRV(
                14u, fixture.fallbackAmbientOcclusion),
            nvrhi::BindingSetItem::Texture_SRV(
                17u, fixture.resolvedBackground),
            nvrhi::BindingSetItem::Texture_SRV(
                20u, fixture.directionalVisibility),
            nvrhi::BindingSetItem::Texture_UAV(0u, run.output),
            nvrhi::BindingSetItem::Sampler(1u, fixture.shadowSampler),
            nvrhi::BindingSetItem::Sampler(
                2u, fixture.environmentSampler),
            nvrhi::BindingSetItem::Sampler(
                3u, fixture.environmentBrdfSampler)
        };
        if (adaptive)
        {
            bindingSetDescription.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    21u, fixture.surfaceOwnership));
        }
        if (diagnostics)
        {
            bindingSetDescription.bindings.push_back(
                nvrhi::BindingSetItem::ConstantBuffer(
                    2u, fixture.diagnosticsConstantsBuffer));
            bindingSetDescription.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    25u, run.diagnostics.multisampledBackground));
            // The production pass binds the MSAA background at t26 when the
            // optional raster SurfaceHit inspector is absent. Grouping,
            // representative, weight, and radiance fields remain complete;
            // only diagnostic barycentrics/front-face are unavailable.
            bindingSetDescription.bindings.push_back(
                nvrhi::BindingSetItem::Texture_SRV(
                    26u, run.diagnostics.multisampledBackground));
            bindingSetDescription.bindings.push_back(
                nvrhi::BindingSetItem::Texture_UAV(
                    1u, run.diagnostics.sampleStatus));
            bindingSetDescription.bindings.push_back(
                nvrhi::BindingSetItem::StructuredBuffer_UAV(
                    2u, run.diagnostics.selectedSampleData));
            bindingSetDescription.bindings.push_back(
                nvrhi::BindingSetItem::StructuredBuffer_UAV(
                    3u, run.diagnostics.pixelMetadata));
        }
        run.bindingSet = device->createBindingSet(
            bindingSetDescription,
            layout);
        return run.bindingSet != nullptr;
    }

    [[nodiscard]] bool CreateCompactLightingRun(
        nvrhi::IDevice* device,
        const CompactProductionFixture& fixture,
        const std::string& label,
        CompactLightingRun& run)
    {
        run.output = CreateCompactTexture(
            device,
            nvrhi::Format::RGBA16_FLOAT,
            CompactFixtureWidth,
            CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2D,
            1u,
            1u,
            false,
            true,
            label + "/Lighting Output");
        run.visualization = CreateCompactTexture(
            device,
            nvrhi::Format::RGBA16_FLOAT,
            CompactFixtureWidth,
            CompactFixtureHeight,
            nvrhi::TextureDimension::Texture2D,
            1u,
            1u,
            false,
            true,
            label + "/Error Visualization");
        run.diagnostics = CreateCompactDiagnostics(
            device,
            fixture.sampleCount,
            label);
        if (!run.output || !run.visualization ||
            !CompactDiagnosticsCreated(run.diagnostics))
        {
            return false;
        }
        run.outputReadback = device->createStagingTexture(
            run.output->getDesc(),
            nvrhi::CpuAccessMode::Read);
        run.visualizationReadback = device->createStagingTexture(
            run.visualization->getDesc(),
            nvrhi::CpuAccessMode::Read);
        if (!run.outputReadback || !run.visualizationReadback)
            return false;

        nvrhi::BindingSetDesc bindingSetDesc;
        bindingSetDesc.bindings = {
            nvrhi::BindingSetItem::ConstantBuffer(
                0u, fixture.deferredConstantsBuffer),
            nvrhi::BindingSetItem::ConstantBuffer(
                CompactBindingConstants,
                fixture.compactConstantsBuffer),
            nvrhi::BindingSetItem::Texture_SRV(
                0u, fixture.shadowMap),
            nvrhi::BindingSetItem::Texture_SRV(
                1u, fixture.diffuseEnvironment),
            nvrhi::BindingSetItem::Texture_SRV(
                2u, fixture.specularEnvironment),
            nvrhi::BindingSetItem::Texture_SRV(
                3u, fixture.environmentBrdf),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingDepth, fixture.depth),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingResolvedBackground,
                fixture.resolvedBackground),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingDirectionalVisibility,
                fixture.directionalVisibility),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingSurfaceOwnership,
                fixture.surfaceOwnership),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingVisibilityClass,
                fixture.visibilityClass),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackDiffuse,
                fixture.fallbackDiffuse),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackMaterial,
                fixture.fallbackMaterial),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackNormals,
                fixture.fallbackNormals),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackEmissive,
                fixture.fallbackEmissive),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackAmbientOcclusion,
                fixture.fallbackAmbientOcclusion),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingFallbackMotionVectors,
                fixture.fallbackMotionVectors),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                CompactBindingGeometryInstances,
                fixture.geometryInstanceBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                CompactBindingGeometry,
                fixture.geometryBuffer),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                CompactBindingInstanceHandles,
                fixture.instanceHandleBuffer),
            nvrhi::BindingSetItem::Texture_UAV(0u, run.output),
            nvrhi::BindingSetItem::Sampler(
                0u, fixture.materialSampler),
            nvrhi::BindingSetItem::Sampler(
                1u, fixture.shadowSampler),
            nvrhi::BindingSetItem::Sampler(
                2u, fixture.environmentSampler),
            nvrhi::BindingSetItem::Sampler(
                3u, fixture.environmentBrdfSampler),
            nvrhi::BindingSetItem::ConstantBuffer(
                CompactBindingDiagnosticsConstants,
                fixture.diagnosticsConstantsBuffer),
            nvrhi::BindingSetItem::Texture_SRV(
                CompactBindingDiagnosticsBackground,
                run.diagnostics.multisampledBackground),
            nvrhi::BindingSetItem::Texture_UAV(
                1u, run.diagnostics.sampleStatus),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                2u, run.diagnostics.selectedSampleData),
            nvrhi::BindingSetItem::StructuredBuffer_UAV(
                3u, run.diagnostics.pixelMetadata)
        };
        run.bindingSet = device->createBindingSet(
            bindingSetDesc,
            fixture.compactFixedLayout);
        return run.bindingSet != nullptr;
    }

    enum class CompactFixtureRasterMode
    {
        Mixed,
        CompactOnly,
        FullGBufferReference,
        AdaptiveGBuffer,
        AdaptiveGBufferFirstTriangle,
        AdaptiveGBufferAliasedDepth
    };

    struct CompactTextureGradientCase
    {
        const char* label = "";
        float2 texCoordScale = float2(1.f);
        float2 texCoordOffset = float2(0.f);
        bool steepPerspective = false;
        float ndcExtent = 1.f;
        float2 ndcOffset = float2(0.f);
        uint32_t viewportOriginX = 0u;
        uint32_t viewportOriginY = 0u;
        uint32_t viewportWidth = CompactFixtureWidth;
        uint32_t viewportHeight = CompactFixtureHeight;
        bool alphaTested = false;
        bool requireMixedAlphaPopulation = true;
        bool useBaseOrDiffuseTexture = true;
        bool useOpacityTexture = true;
        float scalarOpacity = 1.f;
        float alphaCutoff = 0.5f;
        float texCoordRotationRadians = 0.f;
        CompactMaterialTexturePack texturePack =
            CompactMaterialTexturePack::Uncompressed;
    };

    struct CompactTextureQualificationEvidence
    {
        std::array<
            TextureQualificationSampleRecord,
            TextureQualificationSampleCounts.size()> samples = {{
                { 2u, 0u, 0u },
                { 4u, 0u, 0u },
                { 8u, 0u, 0u }
            }};

        [[nodiscard]] TextureQualificationSampleRecord* Find(
            uint32_t sampleCount)
        {
            for (TextureQualificationSampleRecord& sample : samples)
            {
                if (sample.sampleCount == sampleCount)
                    return &sample;
            }
            return nullptr;
        }
    };

    const auto& GetCompactMaterialTextures(
        const CompactProductionFixture& fixture,
        CompactMaterialTexturePack pack)
    {
        switch (pack)
        {
        case CompactMaterialTexturePack::BlockCompressedGuide:
            return fixture.blockCompressedGuideTextures;
        case CompactMaterialTexturePack::BlockCompressedAlpha:
            return fixture.blockCompressedAlphaTextures;
        case CompactMaterialTexturePack::Uncompressed:
        default:
            return fixture.materialTextures;
        }
    }

    nvrhi::IBindingSet* GetCompactMaterialBindingSet(
        const CompactProductionFixture& fixture,
        CompactMaterialTexturePack pack)
    {
        switch (pack)
        {
        case CompactMaterialTexturePack::BlockCompressedGuide:
            return fixture.blockCompressedGuideMaterialBindingSet;
        case CompactMaterialTexturePack::BlockCompressedAlpha:
            return fixture.blockCompressedAlphaMaterialBindingSet;
        case CompactMaterialTexturePack::Uncompressed:
        default:
            return fixture.fallbackMaterialBindingSet;
        }
    }

    nvrhi::IDescriptorTable* GetCompactDescriptorTable(
        const CompactProductionFixture& fixture,
        CompactMaterialTexturePack pack)
    {
        switch (pack)
        {
        case CompactMaterialTexturePack::BlockCompressedGuide:
            return fixture.blockCompressedGuideDescriptorTable;
        case CompactMaterialTexturePack::BlockCompressedAlpha:
            return fixture.blockCompressedAlphaDescriptorTable;
        case CompactMaterialTexturePack::Uncompressed:
        default:
            return fixture.compactDescriptorTable;
        }
    }

    [[nodiscard]] uint8_t CompactAuthoredAlphaBandValue(
        uint32_t x,
        uint32_t dimension,
        bool reverseLowAndHigh = false)
    {
        if (dimension == 0u)
            return 0u;
        x = std::min(x, dimension - 1u);
        const uint32_t edgeWidth = std::max(1u, dimension / 3u);
        if (x < edgeWidth)
            return reverseLowAndHigh ? 192u : 64u;
        if (x < dimension - edgeWidth)
            return 128u;
        return reverseLowAndHigh ? 64u : 192u;
    }

    void WriteCompactAuthoredBc4AlphaBands(
        uint8_t* block,
        uint32_t blockX,
        uint32_t dimension,
        bool reverseLowAndHigh)
    {
        std::array<uint8_t, 16u> values{};
        std::array<uint8_t, 3u> uniqueValues{};
        uint32_t uniqueValueCount = 0u;
        for (uint32_t y = 0u; y < 4u; ++y)
        {
            for (uint32_t x = 0u; x < 4u; ++x)
            {
                const uint8_t value = CompactAuthoredAlphaBandValue(
                    blockX * 4u + x,
                    dimension,
                    reverseLowAndHigh);
                values[y * 4u + x] = value;
                const bool alreadyObserved = std::find(
                    uniqueValues.begin(),
                    uniqueValues.begin() + uniqueValueCount,
                    value) != uniqueValues.begin() + uniqueValueCount;
                if (!alreadyObserved && uniqueValueCount < uniqueValues.size())
                    uniqueValues[uniqueValueCount++] = value;
            }
        }
        std::sort(
            uniqueValues.begin(),
            uniqueValues.begin() + uniqueValueCount);

        // D3D guarantees BC4 endpoints exactly, but permits implementation
        // tolerance for derived palette entries. Keep every cutoff-critical
        // 64/128/192 value as endpoint 0 or 1 whenever a block contains at
        // most two bands. Only the smallest mip block containing all three
        // bands uses the derived, non-qualification-critical palette.
        const bool exactEndpointBlock = uniqueValueCount <= 2u;
        block[0u] = exactEndpointBlock ? uniqueValues[0u] : 32u;
        block[1u] = exactEndpointBlock
            ? uniqueValues[uniqueValueCount > 1u ? 1u : 0u]
            : 192u;
        uint64_t packedIndices = 0u;
        for (uint32_t y = 0u; y < 4u; ++y)
        {
            for (uint32_t x = 0u; x < 4u; ++x)
            {
                const uint8_t value = values[y * 4u + x];
                const uint64_t index = exactEndpointBlock
                    ? (value == block[0u] ? 0u : 1u)
                    : value == 64u ? 2u : value == 128u ? 4u : 1u;
                packedIndices |= index << ((y * 4u + x) * 3u);
            }
        }
        for (uint32_t byte = 0u; byte < 6u; ++byte)
        {
            block[2u + byte] = static_cast<uint8_t>(
                packedIndices >> (byte * 8u));
        }
    }

    [[nodiscard]] uint32_t CompactAuthoredBc4Index(
        const uint8_t* block,
        uint32_t x,
        uint32_t y)
    {
        uint64_t packedIndices = 0u;
        for (uint32_t byte = 0u; byte < 6u; ++byte)
            packedIndices |= uint64_t(block[2u + byte]) << (byte * 8u);
        return static_cast<uint32_t>(
            (packedIndices >> ((y * 4u + x) * 3u)) & 7u);
    }

    [[nodiscard]] uint8_t DecodeCompactAuthoredBc4Alpha(
        const uint8_t* block,
        uint32_t x,
        uint32_t y)
    {
        const uint32_t index = CompactAuthoredBc4Index(block, x, y);
        if (index == 0u)
            return block[0u];
        if (index == 1u)
            return block[1u];
        if (block[0u] <= block[1u])
        {
            if (index <= 5u)
            {
                const uint32_t numerator =
                    (6u - index) * uint32_t(block[0u]) +
                    (index - 1u) * uint32_t(block[1u]);
                return static_cast<uint8_t>((numerator + 2u) / 5u);
            }
            return index == 6u ? 0u : 255u;
        }
        return 0xffu;
    }

    [[nodiscard]] bool ValidateCompactAuthoredAlphaBands()
    {
        // The alpha rows select approximately LOD 3.2, so trilinear filtering
        // can consume mips 3 and 4. Validate all mips 0..4 shared by the
        // uncompressed, BC3-alpha, and BC4-opacity encoders.
        for (uint32_t dimension : { 64u, 32u, 16u, 8u, 4u })
        {
            std::array<uint32_t, 3u> counts{};
            const uint32_t blocksX = (dimension + 3u) / 4u;
            for (uint32_t blockX = 0u; blockX < blocksX; ++blockX)
            {
                std::array<uint8_t, 8u> block{};
                WriteCompactAuthoredBc4AlphaBands(
                    block.data(),
                    blockX,
                    dimension,
                    false);
                for (uint32_t x = 0u; x < 4u; ++x)
                {
                    const uint32_t globalX = blockX * 4u + x;
                    if (globalX >= dimension)
                        continue;
                    const uint8_t decoded = DecodeCompactAuthoredBc4Alpha(
                        block.data(), x, 0u);
                    const uint8_t expected = CompactAuthoredAlphaBandValue(
                        globalX, dimension, false);
                    if (decoded != expected)
                        return false;
                    if ((dimension == 32u || dimension == 16u) &&
                        expected == 128u &&
                        CompactAuthoredBc4Index(
                            block.data(), x, 0u) > 1u)
                    {
                        return false;
                    }
                    if (decoded == 64u)
                        ++counts[0u];
                    else if (decoded == 128u)
                        ++counts[1u];
                    else if (decoded == 192u)
                        ++counts[2u];
                    else
                        return false;
                }
                std::array<uint8_t, 8u> reversedBlock{};
                WriteCompactAuthoredBc4AlphaBands(
                    reversedBlock.data(),
                    blockX,
                    dimension,
                    true);
                for (uint32_t x = 0u; x < 4u; ++x)
                {
                    const uint32_t globalX = blockX * 4u + x;
                    if (globalX >= dimension)
                        continue;
                    if (DecodeCompactAuthoredBc4Alpha(
                            reversedBlock.data(), x, 0u) !=
                        CompactAuthoredAlphaBandValue(
                            globalX, dimension, true))
                    {
                        return false;
                    }
                }
            }
            if (counts[0u] == 0u || counts[1u] == 0u ||
                counts[2u] == 0u || counts[1u] < 2u)
            {
                return false;
            }
        }
        return true;
    }

    void UploadCompactMaterialTextures(
        nvrhi::ICommandList* commandList,
        CompactProductionFixture& fixture,
        CompactMaterialTexturePack pack)
    {
        const size_t packIndex = static_cast<size_t>(pack);
        if (packIndex >= fixture.materialTexturesUploaded.size() ||
            fixture.materialTexturesUploaded[packIndex])
            return;

        const auto& textures = GetCompactMaterialTextures(fixture, pack);
        const auto Pack565 = [](uint8_t red, uint8_t green, uint8_t blue)
        {
            return static_cast<uint16_t>(
                (uint16_t(red >> 3u) << 11u) |
                (uint16_t(green >> 2u) << 5u) |
                uint16_t(blue >> 3u));
        };
        const auto WriteBc4 = [](uint8_t* block, uint8_t value)
        {
            block[0u] = value;
            block[1u] = value;
            std::fill(block + 2u, block + 8u, uint8_t(0u));
        };
        const auto WriteBc1 = [&](uint8_t* block,
            uint8_t red, uint8_t green, uint8_t blue)
        {
            const uint16_t color = Pack565(red, green, blue);
            std::memcpy(block + 0u, &color, sizeof(color));
            std::memcpy(block + 2u, &color, sizeof(color));
            std::fill(block + 4u, block + 8u, uint8_t(0u));
        };
        const auto WriteBc7 = [](uint8_t* block,
            uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
        {
            std::fill(block, block + 16u, uint8_t(0u));
            uint32_t bitPosition = 0u;
            const auto WriteBits = [&](uint32_t value, uint32_t bitCount)
            {
                for (uint32_t bit = 0u; bit < bitCount; ++bit)
                {
                    if ((value & (1u << bit)) != 0u)
                    {
                        block[bitPosition >> 3u] |= static_cast<uint8_t>(
                            1u << (bitPosition & 7u));
                    }
                    ++bitPosition;
                }
            };
            // BC7 mode 6: unary mode prefix, two seven-bit RGBA endpoints,
            // one p-bit per endpoint, then zero indices. Equal endpoints make
            // a known solid block while every mip/block still uses distinct
            // encoded channel values.
            WriteBits(1u << 6u, 7u);
            for (uint8_t channel : { red, green, blue, alpha })
            {
                WriteBits(channel >> 1u, 7u);
                WriteBits(channel >> 1u, 7u);
            }
            WriteBits(red & 1u, 1u);
            WriteBits(red & 1u, 1u);
        };

        for (uint32_t texture = 0u;
            texture < CompactMaterialTextureDescriptorCount;
            ++texture)
        {
            for (uint32_t mip = 0u;
                mip < CompactMaterialTextureMipCount;
                ++mip)
            {
                const uint32_t dimension = std::max(
                    CompactMaterialTextureSize >> mip, 1u);
                if (pack == CompactMaterialTexturePack::Uncompressed)
                {
                    const bool rgbaMetalRoughnessPoc =
                        UVSR_ADAPTIVE_MSAA_TEXTURED_COMPACT_4X_POC &&
                        fixture.sampleCount == 4u && texture == 1u;
                    const uint32_t bytesPerTexel =
                        texture == 1u && !rgbaMetalRoughnessPoc
                        ? 2u
                        : (rgbaMetalRoughnessPoc || texture == 0u || texture == 2u ||
                            texture == 3u)
                            ? 4u
                            : 1u;
                    std::vector<uint8_t> texels(
                        size_t(dimension) * dimension * bytesPerTexel);
                    for (uint32_t y = 0u; y < dimension; ++y)
                    {
                        for (uint32_t x = 0u; x < dimension; ++x)
                        {
                            const uint32_t checker =
                                ((x ^ y) & 1u) != 0u ? 73u : 0u;
                            const uint32_t hash =
                                x * 37u + y * 67u + mip * 29u +
                                texture * 43u + checker;
                            uint8_t* value = texels.data() +
                                (size_t(y) * dimension + x) *
                                    bytesPerTexel;
                            const uint8_t red = static_cast<uint8_t>(
                                24u + hash % 208u);
                            const uint8_t green = static_cast<uint8_t>(
                                24u + (hash * 3u + 31u) % 208u);
                            const uint8_t blue = static_cast<uint8_t>(
                                24u + (hash * 5u + 17u) % 208u);
                            value[0u] = texture == 6u
                                ? CompactAuthoredAlphaBandValue(
                                    x, dimension, true)
                                : red;
                            if (bytesPerTexel >= 2u)
                                value[1u] = green;
                            if (bytesPerTexel == 4u)
                            {
                                value[2u] = blue;
                                value[3u] = texture == 0u
                                    ? CompactAuthoredAlphaBandValue(
                                        x, dimension, false)
                                    : 255u;
                                if (texture == 3u)
                                {
                                    value[0u] = static_cast<uint8_t>(
                                        96u + hash % 64u);
                                    value[1u] = static_cast<uint8_t>(
                                        96u + (hash * 3u) % 64u);
                                    value[2u] = 255u;
                                }
                            }
                        }
                    }
                    commandList->writeTexture(
                        textures[texture],
                        0u,
                        mip,
                        texels.data(),
                        size_t(dimension) * bytesPerTexel);
                    continue;
                }

                const uint32_t blocksX = std::max(
                    (dimension + 3u) / 4u, 1u);
                const uint32_t blocksY = std::max(
                    (dimension + 3u) / 4u, 1u);
                const bool eightByteBlock = texture == 0u &&
                        pack ==
                            CompactMaterialTexturePack::BlockCompressedGuide ||
                    texture == 4u || texture == 5u || texture == 6u;
                const uint32_t blockBytes = eightByteBlock ? 8u : 16u;
                std::vector<uint8_t> blocks(
                    size_t(blocksX) * blocksY * blockBytes);
                for (uint32_t blockY = 0u; blockY < blocksY; ++blockY)
                {
                    for (uint32_t blockX = 0u;
                        blockX < blocksX;
                        ++blockX)
                    {
                        const uint32_t hash = blockX * 37u +
                            blockY * 67u + mip * 29u + texture * 43u;
                        const uint8_t red = static_cast<uint8_t>(
                            32u + hash % 192u);
                        const uint8_t green = static_cast<uint8_t>(
                            32u + (hash * 3u + 31u) % 192u);
                        const uint8_t blue = static_cast<uint8_t>(
                            32u + (hash * 5u + 17u) % 192u);
                        uint8_t* block = blocks.data() +
                            (size_t(blockY) * blocksX + blockX) *
                                blockBytes;
                        if (texture == 0u)
                        {
                            if (pack == CompactMaterialTexturePack::
                                    BlockCompressedAlpha)
                            {
                                WriteCompactAuthoredBc4AlphaBands(
                                    block,
                                    blockX,
                                    dimension,
                                    false);
                                WriteBc1(block + 8u, red, green, blue);
                            }
                            else
                            {
                                WriteBc1(block, red, green, blue);
                            }
                        }
                        else if (texture == 1u || texture == 3u)
                        {
                            WriteBc4(block, texture == 3u
                                ? static_cast<uint8_t>(
                                    96u + hash % 64u)
                                : red);
                            WriteBc4(block + 8u, texture == 3u
                                ? static_cast<uint8_t>(
                                    96u + (hash * 3u) % 64u)
                                : green);
                        }
                        else if (texture == 2u)
                        {
                            WriteBc7(block, red, green, blue, 255u);
                        }
                        else
                        {
                            if (texture == 6u &&
                                pack == CompactMaterialTexturePack::
                                    BlockCompressedAlpha)
                            {
                                WriteCompactAuthoredBc4AlphaBands(
                                    block,
                                    blockX,
                                    dimension,
                                    true);
                            }
                            else
                            {
                                WriteBc4(block, red);
                            }
                        }
                    }
                }
                commandList->writeTexture(
                    textures[texture],
                    0u,
                    mip,
                    blocks.data(),
                    size_t(blocksX) * blockBytes);
            }
        }
        fixture.materialTexturesUploaded[packIndex] = true;
    }

    void RecordCompactDescriptorReadTransitions(
        nvrhi::ICommandList* commandList,
        const CompactProductionFixture& fixture,
        CompactMaterialTexturePack pack =
            CompactMaterialTexturePack::Uncompressed)
    {
        // Bindless descriptor tables intentionally do not participate in
        // NVRHI's automatic binding-set state inference. Mirror the
        // production CompactSceneSnapshot transition transaction after test
        // uploads and before any compact compute dispatch.
        const auto& materialTextures = GetCompactMaterialTextures(
            fixture,
            pack);
        for (const nvrhi::TextureHandle& texture : materialTextures)
        {
            commandList->setTextureState(
                texture,
                nvrhi::AllSubresources,
                nvrhi::ResourceStates::ShaderResource);
        }
        for (nvrhi::IBuffer* buffer : {
                fixture.instanceBuffer.Get(),
                fixture.indexBuffer.Get(),
                fixture.vertexBuffer.Get(),
                fixture.materialBuffer.Get() })
        {
            commandList->setBufferState(
                buffer,
                nvrhi::ResourceStates::ShaderResource);
        }
    }

    void RecordCompactFixtureRaster(
        nvrhi::ICommandList* commandList,
        CompactProductionFixture& fixture,
        CompactFixtureRasterMode mode = CompactFixtureRasterMode::Mixed,
        const CompactTextureGradientCase* textureCase = nullptr,
        bool useSecondaryAdaptiveMaterial = false)
    {
        const bool compactVisibility =
            fixture.scope == ProductionFixtureScope::CompactVisibility;
        const uint32_t viewportOriginX = textureCase
            ? textureCase->viewportOriginX
            : 0u;
        const uint32_t viewportOriginY = textureCase
            ? textureCase->viewportOriginY
            : 0u;
        const uint32_t viewportWidth = textureCase
            ? textureCase->viewportWidth
            : CompactFixtureWidth;
        const uint32_t viewportHeight = textureCase
            ? textureCase->viewportHeight
            : CompactFixtureHeight;
        fixture.view.SetViewport(nvrhi::Viewport(
            float(viewportOriginX),
            float(viewportOriginX + viewportWidth),
            float(viewportOriginY),
            float(viewportOriginY + viewportHeight),
            0.f,
            1.f));
        ConfigureCompactProductionView(
            fixture,
            textureCase && textureCase->steepPerspective);
        InstanceData instance{};
        instance.firstGeometryInstanceIndex = 0u;
        instance.firstGeometryIndex = 0u;
        instance.numGeometries = 2u;
        instance.transform = float3x4::identity();
        instance.prevTransform = float3x4::identity();

        // These two independently keyed triangles cover the viewport and
        // share the NDC y == x diagonal. The first is compact-reconstructable;
        // the second is rasterized by the production full-G-buffer fallback
        // pipeline while its immutable snapshot payload remains Reconstruct.
        const std::array<uint32_t, 6u> indices = {
            0u, 1u, 2u,
            3u, 4u, 5u
        };
        const std::array<float2, 4u> ndcCorners = {
            float2(-1.f, -1.f),
            float2(1.f, -1.f),
            float2(1.f, 1.f),
            float2(-1.f, 1.f)
        };
        std::array<float, 4u> cornerW = { 1.f, 1.f, 1.f, 1.f };
        if (textureCase && textureCase->steepPerspective)
            cornerW = { 0.75f, 3.25f, 4.f, 1.25f };
        constexpr std::array<uint32_t, 6u> VertexCorners = {
            0u, 1u, 2u, 0u, 2u, 3u
        };
        std::array<float, 18u> positions{};
        for (uint32_t vertex = 0u; vertex < VertexCorners.size(); ++vertex)
        {
            const uint32_t corner = VertexCorners[vertex];
            const float w = cornerW[corner];
            const float extent = textureCase
                ? textureCase->ndcExtent
                : 1.f;
            const float2 origin = textureCase
                ? textureCase->ndcOffset
                : float2(0.f);
            positions[vertex * 3u + 0u] =
                (ndcCorners[corner].x * extent + origin.x) * w;
            positions[vertex * 3u + 1u] =
                (ndcCorners[corner].y * extent + origin.y) * w;
            const bool aliasedDepthSurface =
                mode == CompactFixtureRasterMode::
                    AdaptiveGBufferAliasedDepth &&
                vertex >= 3u;
            positions[vertex * 3u + 2u] =
                (aliasedDepthSurface ? -0.75f : -1.f) * w;
        }
        std::array<float, 12u> texCoords = {
            0.f, 1.f,
            1.f, 1.f,
            1.f, 0.f,
            0.f, 1.f,
            1.f, 0.f,
            0.f, 0.f
        };
        if (textureCase && compactVisibility)
        {
            const float rotationCosine = std::cos(
                textureCase->texCoordRotationRadians);
            const float rotationSine = std::sin(
                textureCase->texCoordRotationRadians);
            for (uint32_t vertex = 0u; vertex < 6u; ++vertex)
            {
                const float scaledU = texCoords[vertex * 2u + 0u] *
                    textureCase->texCoordScale.x;
                const float scaledV = texCoords[vertex * 2u + 1u] *
                    textureCase->texCoordScale.y;
                texCoords[vertex * 2u + 0u] =
                    textureCase->texCoordOffset.x +
                    rotationCosine * scaledU -
                    rotationSine * scaledV;
                texCoords[vertex * 2u + 1u] =
                    textureCase->texCoordOffset.y +
                    rotationSine * scaledU +
                    rotationCosine * scaledV;
            }
            UploadCompactMaterialTextures(
                commandList,
                fixture,
                textureCase->texturePack);
        }
        constexpr uint32_t PackedPositiveZNormal = 0x007f0000u;
        constexpr uint32_t PackedPositiveXTangent = 0x7f00007fu;
        const std::array<uint32_t, 6u> normals = {
            PackedPositiveZNormal,
            PackedPositiveZNormal,
            PackedPositiveZNormal,
            PackedPositiveZNormal,
            PackedPositiveZNormal,
            PackedPositiveZNormal
        };
        const std::array<uint32_t, 6u> tangents = {
            PackedPositiveXTangent,
            PackedPositiveXTangent,
            PackedPositiveXTangent,
            PackedPositiveXTangent,
            PackedPositiveXTangent,
            PackedPositiveXTangent
        };
        constexpr uint32_t PositionOffset = 0u;
        constexpr uint32_t TexCoordOffset =
            static_cast<uint32_t>(sizeof(positions));
        constexpr uint32_t NormalOffset =
            TexCoordOffset + static_cast<uint32_t>(sizeof(texCoords));
        constexpr uint32_t TangentOffset =
            NormalOffset + static_cast<uint32_t>(sizeof(normals));
        std::array<uint8_t,
            sizeof(positions) + sizeof(texCoords) +
                sizeof(normals) + sizeof(tangents)> vertexBytes{};
        std::memcpy(
            vertexBytes.data() + PositionOffset,
            positions.data(),
            sizeof(positions));
        std::memcpy(
            vertexBytes.data() + TexCoordOffset,
            texCoords.data(),
            sizeof(texCoords));
        std::memcpy(
            vertexBytes.data() + NormalOffset,
            normals.data(),
            sizeof(normals));
        std::memcpy(
            vertexBytes.data() + TangentOffset,
            tangents.data(),
            sizeof(tangents));

        MaterialConstants material{};
        material.baseOrDiffuseColor = float3(0.2f, 0.3f, 0.4f);
        material.specularColor = float3(0.f);
        material.emissiveColor = float3(
            CompactEmissiveR,
            CompactEmissiveG,
            CompactEmissiveB);
        material.domain = MaterialDomain_Opaque;
        material.opacity = 1.f;
        material.roughness = 0.5f;
        material.metalness = 0.f;
        material.normalTextureScale = 1.f;
        material.occlusionStrength = 1.f;
        material.baseOrDiffuseTextureIndex = -1;
        material.metalRoughOrSpecularTextureIndex = -1;
        material.emissiveTextureIndex = -1;
        material.normalTextureIndex = -1;
        material.occlusionTextureIndex = -1;
        material.transmissionTextureIndex = -1;
        material.opacityTextureIndex = -1;
        material.normalTextureTransformScale = float2(1.f);

        if (textureCase)
        {
            material.flags |=
                MaterialFlags_UseMetalRoughOrSpecularTexture |
                MaterialFlags_UseEmissiveTexture |
                MaterialFlags_UseNormalTexture |
                MaterialFlags_UseOcclusionTexture |
                MaterialFlags_MetalnessInRedChannel;
            if (textureCase->useBaseOrDiffuseTexture)
            {
                material.flags |=
                    MaterialFlags_UseBaseOrDiffuseTexture;
                material.baseOrDiffuseTextureIndex = 0;
            }
            if (textureCase->useOpacityTexture)
            {
                material.flags |= MaterialFlags_UseOpacityTexture;
                material.opacityTextureIndex = 6;
            }
            material.metalRoughOrSpecularTextureIndex = 1;
            material.emissiveTextureIndex = 2;
            material.normalTextureIndex = 3;
            material.occlusionTextureIndex = 4;
            // Descriptor 5 remains covered by the descriptor planner/table
            // tests. The production guide ABI has no transmission channel, so
            // this image fixture must not claim an unobservable signal.
            material.transmissionTextureIndex = -1;
            material.baseOrDiffuseColor = float3(0.8f, 0.7f, 0.6f);
            material.specularColor = float3(0.04f);
            material.emissiveColor = float3(0.75f, 0.5f, 0.25f);
            material.roughness = 0.8f;
            material.metalness = 0.65f;
            material.occlusionStrength = 0.75f;
            if (textureCase->alphaTested)
            {
                material.domain = MaterialDomain_AlphaTested;
                material.opacity = textureCase->scalarOpacity;
                material.alphaCutoff = textureCase->alphaCutoff;
            }
        }

        MaterialConstants fallbackMaterial = material;
        if (mode == CompactFixtureRasterMode::Mixed)
        {
            fallbackMaterial.emissiveColor = float3(
                CompactFallbackEmissiveR,
                CompactFallbackEmissiveG,
                CompactFallbackEmissiveB);
        }
        MaterialConstants secondaryFallbackMaterial = fallbackMaterial;
        secondaryFallbackMaterial.emissiveColor = float3(
            CompactFallbackEmissiveR,
            CompactFallbackEmissiveG,
            CompactFallbackEmissiveB);
        fixture.primaryFallbackMaterial = fallbackMaterial;

        std::array<CompactGeometryInstanceGpuRecord, 2u>
            geometryInstances{};
        for (uint32_t geometryIndex = 0u;
            geometryIndex < geometryInstances.size();
            ++geometryIndex)
        {
            CompactGeometryInstanceGpuRecord& geometryInstance =
                geometryInstances[geometryIndex];
            geometryInstance.generation = FixtureGeneration;
            geometryInstance.geometryIndex = geometryIndex;
            geometryInstance.instanceIndex = 0u;
            geometryInstance.geometryRecordIndex = geometryIndex;
            geometryInstance.stableInstanceHandleLow = 1u;
            geometryInstance.topologyEpochLow = CompactTopologyEpoch;
            geometryInstance.topologyRevision = CompactTopologyRevision;
            geometryInstance.lodRevision = CompactLodRevision;
            geometryInstance.deformationRevision =
                CompactDeformationRevision;
            geometryInstance.materialRevision = CompactMaterialRevision;
            geometryInstance.primitiveCount = 1u;
            geometryInstance.flags =
                CompactGeometryInstanceFlag_Valid |
                CompactGeometryInstanceFlag_DeformationSupported |
                CompactGeometryInstanceFlag_MaterialSupported;
            geometryInstance.payloadClass = static_cast<uint32_t>(
                CompactGeometryPayloadClass::Reconstruct);
            geometryInstance.payloadFallbackReason =
                static_cast<uint32_t>(FallbackReason::None);
        }

        std::array<CompactGeometryGpuRecord, 2u> geometries{};
        for (uint32_t geometryIndex = 0u;
            geometryIndex < geometries.size();
            ++geometryIndex)
        {
            CompactGeometryGpuRecord& geometry = geometries[geometryIndex];
            geometry.indexBufferDescriptor = CompactRawIndexDescriptor;
            geometry.vertexBufferDescriptor = CompactRawVertexDescriptor;
            geometry.indexOffset = geometryIndex * 3u * sizeof(uint32_t);
            geometry.positionOffset = PositionOffset;
            geometry.previousPositionOffset = PositionOffset;
            geometry.texCoordOffset = TexCoordOffset;
            geometry.normalOffset = NormalOffset;
            geometry.tangentOffset = TangentOffset;
            geometry.indexCount = 3u;
            geometry.vertexCount = 6u;
            geometry.materialIndex = 0u;
            geometry.flags = textureCase && textureCase->alphaTested
                ? CompactGeometryFlag_AlphaTested
                : CompactGeometryFlag_None;
        }
        const std::array<uint32_t, 2u> instanceHandles = {
            FixtureGeometryInstanceHandle,
            CompactFallbackGeometryInstanceHandle
        };

        commandList->writeBuffer(
            fixture.instanceBuffer,
            &instance,
            sizeof(instance));
        commandList->writeBuffer(
            fixture.vertexBuffer,
            vertexBytes.data(),
            vertexBytes.size());
        if (compactVisibility)
        {
            commandList->writeBuffer(
                fixture.indexBuffer,
                indices.data(),
                sizeof(indices));
            commandList->writeBuffer(
                fixture.materialBuffer,
                &material,
                sizeof(material));
            commandList->writeBuffer(
                fixture.geometryInstanceBuffer,
                geometryInstances.data(),
                sizeof(geometryInstances));
            commandList->writeBuffer(
                fixture.geometryBuffer,
                geometries.data(),
                sizeof(geometries));
        }
        commandList->writeBuffer(
            fixture.instanceHandleBuffer,
            instanceHandles.data(),
            sizeof(instanceHandles));
        commandList->writeBuffer(
            fixture.rasterViewConstantsBuffer,
            &fixture.gbufferConstants,
            sizeof(fixture.gbufferConstants));
        commandList->writeBuffer(
            fixture.fallbackMaterialConstantsBuffer,
            &fixture.primaryFallbackMaterial,
            sizeof(fixture.primaryFallbackMaterial));
        if (fixture.secondaryFallbackMaterialConstantsBuffer)
        {
            commandList->writeBuffer(
                fixture.secondaryFallbackMaterialConstantsBuffer,
                &secondaryFallbackMaterial,
                sizeof(secondaryFallbackMaterial));
        }

        commandList->clearTextureUInt(
            fixture.surfaceOwnership,
            nvrhi::AllSubresources,
            ClearedVisibilityWord);
        if (compactVisibility)
        {
            commandList->clearTextureUInt(
                fixture.visibilityClass,
                nvrhi::AllSubresources,
                ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND);
        }
        commandList->clearDepthStencilTexture(
            fixture.depth,
            nvrhi::AllSubresources,
            true,
            0.f,
            false,
            0u);
        commandList->clearTextureFloat(
            fixture.fallbackDiffuse,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            fixture.fallbackMaterial,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            fixture.fallbackNormals,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            fixture.fallbackEmissive,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            fixture.fallbackAmbientOcclusion,
            nvrhi::AllSubresources,
            nvrhi::Color(1.f));
        commandList->clearTextureFloat(
            fixture.fallbackMotionVectors,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearTextureFloat(
            fixture.resolvedBackground,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));

        AdaptiveMsaaGBufferPushConstants push{};
        push.positionOffset = PositionOffset;
        push.prevPositionOffset = PositionOffset;
        push.texCoordOffset = TexCoordOffset;
        push.normalOffset = NormalOffset;
        push.tangentOffset = TangentOffset;
        nvrhi::DrawArguments arguments;
        arguments.vertexCount = 3u;
        arguments.instanceCount = 1u;

        const auto DrawGeometry = [&](uint32_t geometryIndex,
            bool fullGBuffer,
            bool adaptiveGBuffer = false,
            bool aliasFirstGeometryKey = false)
        {
            const bool alphaTested =
                textureCase && textureCase->alphaTested;
            nvrhi::GraphicsState state;
            state.pipeline = adaptiveGBuffer
                ? fixture.adaptiveGBufferPipeline
                : fullGBuffer
                ? (alphaTested
                    ? fixture.fallbackAlphaPipeline
                    : fixture.fallbackPipeline)
                : (alphaTested
                    ? fixture.visibilityAlphaPipeline
                    : fixture.visibilityPipeline);
            state.framebuffer = adaptiveGBuffer
                ? fixture.adaptiveGBufferFramebuffer
                : fullGBuffer
                ? fixture.fallbackFramebuffer
                : fixture.visibilityFramebuffer;
            if (fullGBuffer || adaptiveGBuffer || alphaTested)
            {
                nvrhi::IBindingSet* materialBindingSet =
                    useSecondaryAdaptiveMaterial && adaptiveGBuffer &&
                        geometryIndex == 1u
                    ? fixture.secondaryFallbackMaterialBindingSet.Get()
                    : GetCompactMaterialBindingSet(
                        fixture,
                        textureCase
                            ? textureCase->texturePack
                            : CompactMaterialTexturePack::Uncompressed);
                state.bindings = {
                    materialBindingSet,
                    fixture.rasterViewBindingSet,
                    fixture.rasterInputBindingSet
                };
            }
            else
            {
                state.bindings = {
                    fixture.rasterViewBindingSet,
                    fixture.rasterInputBindingSet
                };
            }
            state.viewport = fixture.view.GetViewportState();
            commandList->setGraphicsState(state);
            push.startVertexLocation = geometryIndex * 3u;
            push.geometryLocalIndex = aliasFirstGeometryKey
                ? 0u
                : geometryIndex;
            push.visibilityClass = fullGBuffer
                ? AdaptiveMsaaEncodeFallbackVisibilityClass(
                    static_cast<uint32_t>(
                        FallbackReason::UnsupportedMaterial),
                    true)
                : ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT;
            commandList->setPushConstants(&push, sizeof(push));
            commandList->draw(arguments);
        };

        switch (mode)
        {
        case CompactFixtureRasterMode::CompactOnly:
            DrawGeometry(0u, false);
            DrawGeometry(1u, false);
            break;
        case CompactFixtureRasterMode::FullGBufferReference:
            DrawGeometry(0u, true);
            DrawGeometry(1u, true);
            break;
        case CompactFixtureRasterMode::AdaptiveGBuffer:
            DrawGeometry(0u, true, true);
            DrawGeometry(1u, true, true);
            break;
        case CompactFixtureRasterMode::AdaptiveGBufferFirstTriangle:
            DrawGeometry(0u, true, true);
            break;
        case CompactFixtureRasterMode::AdaptiveGBufferAliasedDepth:
            DrawGeometry(0u, true, true);
            DrawGeometry(1u, true, true, true);
            break;
        case CompactFixtureRasterMode::Mixed:
        default:
            DrawGeometry(0u, false);
            DrawGeometry(1u, true);
            break;
        }
    }

    struct CompactMsaaExtractionPair
    {
        std::vector<CompactMsaaExtractedSample> reference;
        std::vector<CompactMsaaExtractedSample> compact;
        CompactMsaaExtractCounters referenceCounters{};
        CompactMsaaExtractCounters compactCounters{};
    };

    struct CompactMsaaParitySummary
    {
        uint32_t referenceCoveredSampleCount = 0u;
        uint32_t compactCoveredSampleCount = 0u;
        uint32_t totalSampleCount = 0u;
    };

    [[nodiscard]] bool ExecuteCompactMsaaExtractionPair(
        nvrhi::IDevice* device,
        CompactProductionFixture& fixture,
        const CompactTextureGradientCase& textureCase,
        CompactMsaaExtractionPair& result)
    {
        result = {};
        const uint64_t recordCount =
            uint64_t(CompactFixturePixelCount) * fixture.sampleCount;
        if (recordCount == 0u ||
            recordCount > uint64_t(std::numeric_limits<uint32_t>::max()) /
                sizeof(CompactMsaaExtractedSample))
        {
            return false;
        }
        const uint64_t recordBytes =
            recordCount * sizeof(CompactMsaaExtractedSample);
        const uint64_t counterBytes = sizeof(CompactMsaaExtractCounters);
        const uint64_t readbackBytes = recordBytes * 2u +
            counterBytes * 2u;

        const nvrhi::BufferHandle referenceRecords = CreateCompactBuffer(
            device,
            recordBytes,
            sizeof(CompactMsaaExtractedSample),
            false,
            true,
            false,
            "Adaptive MSAA GPU Test/MSAA Extract Reference Records");
        const nvrhi::BufferHandle compactRecords = CreateCompactBuffer(
            device,
            recordBytes,
            sizeof(CompactMsaaExtractedSample),
            false,
            true,
            false,
            "Adaptive MSAA GPU Test/MSAA Extract Compact Records");
        const nvrhi::BufferHandle referenceCounters = CreateCompactBuffer(
            device,
            counterBytes,
            0u,
            true,
            true,
            false,
            "Adaptive MSAA GPU Test/MSAA Extract Reference Counters");
        const nvrhi::BufferHandle compactCounters = CreateCompactBuffer(
            device,
            counterBytes,
            0u,
            true,
            true,
            false,
            "Adaptive MSAA GPU Test/MSAA Extract Compact Counters");
        nvrhi::BufferDesc readbackDesc;
        readbackDesc.byteSize = readbackBytes;
        readbackDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        readbackDesc.initialState = nvrhi::ResourceStates::CopyDest;
        readbackDesc.keepInitialState = true;
        readbackDesc.debugName =
            "Adaptive MSAA GPU Test/MSAA Extract Paired Readback";
        const nvrhi::BufferHandle readback =
            device->createBuffer(readbackDesc);
        const nvrhi::CommandListHandle commandList =
            device->createCommandList();
        if (!referenceRecords || !compactRecords ||
            !referenceCounters || !compactCounters || !readback ||
            !commandList)
        {
            return false;
        }

        const auto CreateExtractBindingSet = [&](nvrhi::IBuffer* records,
            nvrhi::IBuffer* counters)
        {
            return device->createBindingSet(
                nvrhi::BindingSetDesc()
                    .addItem(nvrhi::BindingSetItem::Texture_SRV(
                        0u,
                        fixture.surfaceOwnership))
                    .addItem(nvrhi::BindingSetItem::Texture_SRV(
                        1u,
                        fixture.visibilityClass))
                    .addItem(nvrhi::BindingSetItem::Texture_SRV(
                        2u,
                        fixture.depth))
                    .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(
                        0u,
                        records))
                    .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(
                        1u,
                        counters)),
                fixture.msaaExtractLayout);
        };
        const nvrhi::BindingSetHandle referenceBindingSet =
            CreateExtractBindingSet(
                referenceRecords,
                referenceCounters);
        const nvrhi::BindingSetHandle compactBindingSet =
            CreateExtractBindingSet(
                compactRecords,
                compactCounters);
        if (!referenceBindingSet || !compactBindingSet)
            return false;

        const auto RecordExtract = [&](nvrhi::IBindingSet* bindingSet,
            nvrhi::IBuffer* records,
            nvrhi::IBuffer* counters)
        {
            commandList->clearBufferUInt(records, 0xffffffffu);
            commandList->clearBufferUInt(counters, 0u);
            nvrhi::ComputeState state;
            state.pipeline = fixture.msaaExtractPipeline;
            state.bindings = { bindingSet };
            commandList->setComputeState(state);
            commandList->dispatch(
                (CompactFixtureWidth + 7u) / 8u,
                (CompactFixtureHeight + 7u) / 8u,
                1u);
        };

        commandList->open();
        RecordCompactFixtureRaster(
            commandList,
            fixture,
            CompactFixtureRasterMode::FullGBufferReference,
            &textureCase);
        RecordExtract(
            referenceBindingSet,
            referenceRecords,
            referenceCounters);
        RecordCompactFixtureRaster(
            commandList,
            fixture,
            CompactFixtureRasterMode::CompactOnly,
            &textureCase);
        RecordExtract(
            compactBindingSet,
            compactRecords,
            compactCounters);

        uint64_t readbackOffset = 0u;
        commandList->copyBuffer(
            readback,
            readbackOffset,
            referenceRecords,
            0u,
            recordBytes);
        readbackOffset += recordBytes;
        commandList->copyBuffer(
            readback,
            readbackOffset,
            compactRecords,
            0u,
            recordBytes);
        readbackOffset += recordBytes;
        commandList->copyBuffer(
            readback,
            readbackOffset,
            referenceCounters,
            0u,
            counterBytes);
        readbackOffset += counterBytes;
        commandList->copyBuffer(
            readback,
            readbackOffset,
            compactCounters,
            0u,
            counterBytes);
        commandList->close();
        device->executeCommandList(commandList);
        if (!device->waitForIdle())
            return false;

        const void* mapped = device->mapBuffer(
            readback,
            nvrhi::CpuAccessMode::Read);
        if (!mapped)
            return false;
        const auto* bytes = static_cast<const uint8_t*>(mapped);
        result.reference.resize(static_cast<size_t>(recordCount));
        result.compact.resize(static_cast<size_t>(recordCount));
        size_t copyOffset = 0u;
        std::memcpy(
            result.reference.data(),
            bytes + copyOffset,
            static_cast<size_t>(recordBytes));
        copyOffset += static_cast<size_t>(recordBytes);
        std::memcpy(
            result.compact.data(),
            bytes + copyOffset,
            static_cast<size_t>(recordBytes));
        copyOffset += static_cast<size_t>(recordBytes);
        std::memcpy(
            &result.referenceCounters,
            bytes + copyOffset,
            sizeof(result.referenceCounters));
        copyOffset += sizeof(result.referenceCounters);
        std::memcpy(
            &result.compactCounters,
            bytes + copyOffset,
            sizeof(result.compactCounters));
        device->unmapBuffer(readback);
        return copyOffset + sizeof(result.compactCounters) ==
            readbackBytes;
    }

    [[nodiscard]] bool ValidateCompactMsaaRasterParity(
        nvrhi::IDevice* device,
        CompactProductionFixture& fixture,
        const CompactTextureGradientCase& textureCase,
        CompactMsaaParitySummary* summary = nullptr,
        uint32_t* evidenceMask = nullptr)
    {
        if (evidenceMask)
            *evidenceMask = 0u;
        CompactMsaaExtractionPair extraction;
        bool passed = Check(
            ExecuteCompactMsaaExtractionPair(
                device,
                fixture,
                textureCase,
                extraction),
            fixture.sampleCount,
            std::string("MSAA extraction failed: ") + textureCase.label);
        if (!passed)
            return false;

        const size_t expectedRecordCount = size_t(
            CompactFixturePixelCount) * fixture.sampleCount;
        passed &= Check(
            extraction.reference.size() == expectedRecordCount &&
                extraction.compact.size() == expectedRecordCount,
            fixture.sampleCount,
            std::string("MSAA extraction record count mismatch: ") +
                textureCase.label);
        if (!passed)
            return false;

        const uint32_t expectedReferenceClass =
            AdaptiveMsaaEncodeFallbackVisibilityClass(
                static_cast<uint32_t>(
                    FallbackReason::UnsupportedMaterial),
                true);
        uint32_t referenceCovered = 0u;
        uint32_t compactCovered = 0u;
        uint32_t coverageMismatches = 0u;
        uint32_t keyMismatches = 0u;
        uint32_t depthMismatches = 0u;
        uint32_t classMismatches = 0u;
        uint32_t sentinelRecords = 0u;
        uint32_t semanticFailures = 0u;
        bool capturedReferenceInvalidUncovered = false;
        bool capturedCompactInvalidUncovered = false;
        size_t referenceInvalidUncoveredIndex = 0u;
        size_t compactInvalidUncoveredIndex = 0u;
        CompactMsaaExtractedSample firstReferenceInvalidUncovered{};
        CompactMsaaExtractedSample firstCompactInvalidUncovered{};
        float minimumPerspectiveDepth =
            std::numeric_limits<float>::infinity();
        float maximumPerspectiveDepth =
            -std::numeric_limits<float>::infinity();
        for (size_t recordIndex = 0u;
            recordIndex < expectedRecordCount;
            ++recordIndex)
        {
            const CompactMsaaExtractedSample& reference =
                extraction.reference[recordIndex];
            const CompactMsaaExtractedSample& compact =
                extraction.compact[recordIndex];
            const auto IsSentinel = [](const CompactMsaaExtractedSample& value)
            {
                return value.geometryInstanceHandle == 0xffffffffu &&
                    value.geometryLocalPrimitiveIndex == 0xffffffffu &&
                    value.visibilityClass == 0xffffffffu &&
                    value.depthBits == 0xffffffffu;
            };
            sentinelRecords += IsSentinel(reference) ? 1u : 0u;
            sentinelRecords += IsSentinel(compact) ? 1u : 0u;

            const float referenceDepth = FloatFromBits(reference.depthBits);
            const float compactDepth = FloatFromBits(compact.depthBits);
            const bool referenceIsCovered =
                std::isfinite(referenceDepth) && referenceDepth > 0.f;
            const bool compactIsCovered =
                std::isfinite(compactDepth) && compactDepth > 0.f;
            referenceCovered += referenceIsCovered ? 1u : 0u;
            compactCovered += compactIsCovered ? 1u : 0u;
            coverageMismatches +=
                referenceIsCovered == compactIsCovered ? 0u : 1u;

            const auto SemanticallyValid = [](
                const CompactMsaaExtractedSample& value,
                bool covered)
            {
                if (!covered)
                {
                    return IsClearedVisibilityKey({
                            value.geometryInstanceHandle,
                            value.geometryLocalPrimitiveIndex }) &&
                        value.visibilityClass ==
                            ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND &&
                        value.depthBits == 0u;
                }
                return ValidateGpuVisibilityKey({
                        value.geometryInstanceHandle,
                        value.geometryLocalPrimitiveIndex }) ==
                        FallbackReason::None &&
                    AdaptiveMsaaDecodeVisibilityClass(
                        value.visibilityClass).valid;
            };
            const bool referenceSemanticallyValid = SemanticallyValid(
                reference, referenceIsCovered);
            const bool compactSemanticallyValid = SemanticallyValid(
                compact, compactIsCovered);
            semanticFailures += referenceSemanticallyValid ? 0u : 1u;
            semanticFailures += compactSemanticallyValid ? 0u : 1u;
            if (!referenceIsCovered && !referenceSemanticallyValid &&
                !capturedReferenceInvalidUncovered)
            {
                capturedReferenceInvalidUncovered = true;
                referenceInvalidUncoveredIndex = recordIndex;
                firstReferenceInvalidUncovered = reference;
            }
            if (!compactIsCovered && !compactSemanticallyValid &&
                !capturedCompactInvalidUncovered)
            {
                capturedCompactInvalidUncovered = true;
                compactInvalidUncoveredIndex = recordIndex;
                firstCompactInvalidUncovered = compact;
            }

            if (!referenceIsCovered || !compactIsCovered)
                continue;
            keyMismatches +=
                reference.geometryInstanceHandle ==
                        compact.geometryInstanceHandle &&
                    reference.geometryLocalPrimitiveIndex ==
                        compact.geometryLocalPrimitiveIndex
                    ? 0u : 1u;
            depthMismatches += reference.depthBits == compact.depthBits
                ? 0u : 1u;
            classMismatches +=
                reference.visibilityClass == expectedReferenceClass &&
                    compact.visibilityClass ==
                        ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT
                ? 0u : 1u;
            minimumPerspectiveDepth = std::min(
                minimumPerspectiveDepth,
                referenceDepth);
            maximumPerspectiveDepth = std::max(
                maximumPerspectiveDepth,
                referenceDepth);
        }

        const auto CountersValid = [expectedRecordCount](
            const CompactMsaaExtractCounters& counters)
        {
            return uint64_t(counters.coveredSampleCount) +
                        counters.backgroundSampleCount ==
                    expectedRecordCount &&
                counters.nonFiniteDepthCount == 0u &&
                counters.partialInvalidKeyCount == 0u &&
                counters.invalidCoveredRecordCount == 0u &&
                counters.invalidBackgroundRecordCount == 0u &&
                counters.fatalConfigurationCount == 0u &&
                counters.recordWriteCount == expectedRecordCount &&
                counters.negativeDepthCount == 0u;
        };
        const bool expectsCompleteCoverage = !textureCase.alphaTested &&
            textureCase.ndcExtent >= 1.f &&
            textureCase.viewportOriginX == 0u &&
            textureCase.viewportOriginY == 0u &&
            textureCase.viewportWidth == CompactFixtureWidth &&
            textureCase.viewportHeight == CompactFixtureHeight;
        const bool coveragePopulationValid = expectsCompleteCoverage
            ? referenceCovered == expectedRecordCount &&
                compactCovered == expectedRecordCount
            : referenceCovered < expectedRecordCount &&
                compactCovered < expectedRecordCount;
        const bool requiresCoveredPopulation =
            !textureCase.alphaTested ||
            textureCase.requireMixedAlphaPopulation;
        passed &= Check(
            sentinelRecords == 0u && semanticFailures == 0u &&
                (!requiresCoveredPopulation ||
                    (referenceCovered > 0u && compactCovered > 0u)) &&
                coveragePopulationValid &&
                coverageMismatches == 0u && keyMismatches == 0u &&
                depthMismatches == 0u && classMismatches == 0u &&
                CountersValid(extraction.referenceCounters) &&
                CountersValid(extraction.compactCounters) &&
                extraction.referenceCounters.coveredSampleCount ==
                    referenceCovered &&
                extraction.compactCounters.coveredSampleCount ==
                    compactCovered,
            fixture.sampleCount,
            std::string("exact MSAA raster parity failed: ") +
                textureCase.label);
        if (textureCase.steepPerspective)
        {
            passed &= Check(
                std::isfinite(minimumPerspectiveDepth) &&
                    std::isfinite(maximumPerspectiveDepth) &&
                    maximumPerspectiveDepth - minimumPerspectiveDepth >=
                        0.1f,
                fixture.sampleCount,
                std::string(
                    "MSAA perspective depth range is below 0.1: ") +
                    textureCase.label);
        }
        if (textureCase.alphaTested &&
            textureCase.requireMixedAlphaPopulation)
        {
            passed &= Check(
                referenceCovered > 0u && compactCovered > 0u &&
                    referenceCovered < expectedRecordCount &&
                    compactCovered < expectedRecordCount,
                fixture.sampleCount,
                std::string(
                    "alpha parity row did not retain both kept and discarded samples: ") +
                    textureCase.label);
        }
        if (summary)
        {
            summary->referenceCoveredSampleCount = referenceCovered;
            summary->compactCoveredSampleCount = compactCovered;
            summary->totalSampleCount = static_cast<uint32_t>(
                expectedRecordCount);
        }
        const auto PrintInvalidUncovered = [&](
            const char* path,
            bool captured,
            size_t recordIndex,
            const CompactMsaaExtractedSample& sample)
        {
            if (!captured)
                return;
            const uint32_t sampleIndex = static_cast<uint32_t>(
                recordIndex % fixture.sampleCount);
            const uint32_t pixelIndex = static_cast<uint32_t>(
                recordIndex / fixture.sampleCount);
            const uint32_t pixelX = pixelIndex % CompactFixtureWidth;
            const uint32_t pixelY = pixelIndex / CompactFixtureWidth;
            const std::ios_base::fmtflags savedFlags = std::cout.flags();
            const char savedFill = std::cout.fill();
            std::cout << "[MSAA INVALID UNCOVERED] " << textureCase.label
                << " path=" << path << " pixel=(" << pixelX << ','
                << pixelY << ") sample=" << sampleIndex << " key=(0x"
                << std::hex << std::setfill('0') << std::setw(8)
                << sample.geometryInstanceHandle << ",0x" << std::setw(8)
                << sample.geometryLocalPrimitiveIndex << ") class=0x"
                << std::setw(8) << sample.visibilityClass
                << " depthBits=0x" << std::setw(8) << sample.depthBits
                << ".\n";
            std::cout.flags(savedFlags);
            std::cout.fill(savedFill);
        };
        PrintInvalidUncovered(
            "reference",
            capturedReferenceInvalidUncovered,
            referenceInvalidUncoveredIndex,
            firstReferenceInvalidUncovered);
        PrintInvalidUncovered(
            "compact",
            capturedCompactInvalidUncovered,
            compactInvalidUncoveredIndex,
            firstCompactInvalidUncovered);
        std::cout << "[MSAA EXTRACT] " << textureCase.label
            << " samples=" << fixture.sampleCount
            << " records=" << expectedRecordCount
            << " reference-covered=" << referenceCovered
            << " compact-covered=" << compactCovered
            << " mismatches=(coverage:" << coverageMismatches
            << ",key:" << keyMismatches
            << ",depth:" << depthMismatches
            << ",class:" << classMismatches << ')'
            << " sentinels=" << sentinelRecords
            << " semantic-failures=" << semanticFailures
            << " reference-errors=("
            << extraction.referenceCounters.nonFiniteDepthCount << ','
            << extraction.referenceCounters.partialInvalidKeyCount << ','
            << extraction.referenceCounters.invalidCoveredRecordCount << ','
            << extraction.referenceCounters.invalidBackgroundRecordCount
            << ',' << extraction.referenceCounters.fatalConfigurationCount
            << ',' << extraction.referenceCounters.negativeDepthCount
            << ") compact-errors=("
            << extraction.compactCounters.nonFiniteDepthCount << ','
            << extraction.compactCounters.partialInvalidKeyCount << ','
            << extraction.compactCounters.invalidCoveredRecordCount << ','
            << extraction.compactCounters.invalidBackgroundRecordCount
            << ',' << extraction.compactCounters.fatalConfigurationCount
            << ',' << extraction.compactCounters.negativeDepthCount
            << ").\n";
        if (passed && evidenceMask)
        {
            *evidenceMask =
                TextureQualificationEvidence_RasterSampleDescriptorParity;
        }
        return passed;
    }

    enum class CompactGuideMutation
    {
        None,
        ZeroFallbackDepth,
        DegenerateFallbackNormal
    };

    using CompactGuidePixel = std::array<
        std::array<float, 4u>, CompactGuideOutputCount>;
    using CompactGuideImage = std::array<
        std::vector<std::array<float, 4u>>,
        CompactGuideOutputCount>;

    [[nodiscard]] bool ReadCompactGuidePixel(
        nvrhi::IDevice* device,
        const CompactGuideRun& run,
        uint32_t pixelX,
        uint32_t pixelY,
        CompactGuidePixel& pixel)
    {
        pixel = {};
        for (uint32_t output = 0u;
            output < CompactGuideOutputCount;
            ++output)
        {
            size_t rowPitch = 0u;
            const auto* mapped = static_cast<const uint8_t*>(
                device->mapStagingTexture(
                    run.readbacks[output],
                    nvrhi::TextureSlice(),
                    nvrhi::CpuAccessMode::Read,
                    &rowPitch));
            if (!mapped)
                return false;
            const uint8_t* row = mapped + size_t(pixelY) * rowPitch;
            if (output == CompactGuideDepth)
            {
                std::memcpy(
                    &pixel[output][0],
                    row + size_t(pixelX) * sizeof(float),
                    sizeof(float));
            }
            else if (output == CompactGuideAmbientOcclusion)
            {
                const auto* texel = reinterpret_cast<const uint16_t*>(row) +
                    pixelX;
                pixel[output][0] = donut::math::Float16ToFloat32(
                    { *texel });
            }
            else
            {
                const auto* texel = reinterpret_cast<const uint16_t*>(row) +
                    pixelX * 4u;
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    pixel[output][channel] =
                        donut::math::Float16ToFloat32(
                            { texel[channel] });
                }
            }
            device->unmapStagingTexture(run.readbacks[output]);
        }
        return true;
    }

    [[nodiscard]] bool ReadCompactGuideImage(
        nvrhi::IDevice* device,
        const CompactGuideRun& run,
        CompactGuideImage& image)
    {
        for (uint32_t output = 0u;
            output < CompactGuideOutputCount;
            ++output)
        {
            image[output].assign(CompactFixturePixelCount, {});
            size_t rowPitch = 0u;
            const auto* mapped = static_cast<const uint8_t*>(
                device->mapStagingTexture(
                    run.readbacks[output],
                    nvrhi::TextureSlice(),
                    nvrhi::CpuAccessMode::Read,
                    &rowPitch));
            if (!mapped)
                return false;
            for (uint32_t y = 0u; y < CompactFixtureHeight; ++y)
            {
                const uint8_t* row = mapped + size_t(y) * rowPitch;
                for (uint32_t x = 0u; x < CompactFixtureWidth; ++x)
                {
                    std::array<float, 4u>& pixel =
                        image[output][y * CompactFixtureWidth + x];
                    if (output == CompactGuideDepth)
                    {
                        std::memcpy(
                            &pixel[0],
                            row + size_t(x) * sizeof(float),
                            sizeof(float));
                    }
                    else if (output == CompactGuideAmbientOcclusion)
                    {
                        const auto* texel =
                            reinterpret_cast<const uint16_t*>(row) + x;
                        pixel[0] = donut::math::Float16ToFloat32(
                            { *texel });
                    }
                    else
                    {
                        const auto* texel =
                            reinterpret_cast<const uint16_t*>(row) +
                            x * 4u;
                        for (uint32_t channel = 0u;
                            channel < 4u;
                            ++channel)
                        {
                            pixel[channel] =
                                donut::math::Float16ToFloat32(
                                    { texel[channel] });
                        }
                    }
                }
            }
            device->unmapStagingTexture(run.readbacks[output]);
        }
        return true;
    }

    [[nodiscard]] bool ExecuteCompactTextureGuideRun(
        nvrhi::IDevice* device,
        CompactProductionFixture& fixture,
        CompactFixtureRasterMode mode,
        const CompactTextureGradientCase& textureCase,
        CompactGuideImage& image)
    {
        const nvrhi::CommandListHandle commandList =
            device->createCommandList();
        if (!commandList)
            return false;
        commandList->open();
        RecordCompactFixtureRaster(
            commandList,
            fixture,
            mode,
            &textureCase);
        RecordCompactDescriptorReadTransitions(
            commandList,
            fixture,
            textureCase.texturePack);
        commandList->writeBuffer(
            fixture.compactConstantsBuffer,
            &fixture.compactConstants,
            sizeof(fixture.compactConstants));
        for (uint32_t output = 0u;
            output < CompactGuideOutputCount;
            ++output)
        {
            commandList->clearTextureFloat(
                fixture.guide.outputs[output],
                nvrhi::AllSubresources,
                output == CompactGuideAmbientOcclusion
                    ? nvrhi::Color(1.f)
                    : nvrhi::Color(0.f));
        }
        nvrhi::ComputeState state;
        state.pipeline = fixture.compactResolvePipeline;
        state.bindings = {
            fixture.guide.bindingSet,
            GetCompactDescriptorTable(fixture, textureCase.texturePack)
        };
        commandList->setComputeState(state);
        commandList->dispatch(1u, 1u, 1u);
        for (uint32_t output = 0u;
            output < CompactGuideOutputCount;
            ++output)
        {
            commandList->copyTexture(
                fixture.guide.readbacks[output],
                nvrhi::TextureSlice(),
                fixture.guide.outputs[output],
                nvrhi::TextureSlice());
        }
        commandList->close();
        device->executeCommandList(commandList);
        return device->waitForIdle() &&
            ReadCompactGuideImage(device, fixture.guide, image);
    }

    [[nodiscard]] bool ValidateCompactTextureGradientCase(
        nvrhi::IDevice* device,
        CompactProductionFixture& fixture,
        const CompactTextureGradientCase& textureCase,
        uint32_t* evidenceMask = nullptr,
        bool* executionCompleted = nullptr)
    {
        if (evidenceMask)
            *evidenceMask = 0u;
        if (executionCompleted)
            *executionCompleted = false;
        CompactGuideImage reference;
        CompactGuideImage compact;
        bool passed = Check(
            ExecuteCompactTextureGuideRun(
                device,
                fixture,
                CompactFixtureRasterMode::FullGBufferReference,
                textureCase,
                reference) &&
            ExecuteCompactTextureGuideRun(
                device,
                fixture,
                CompactFixtureRasterMode::CompactOnly,
                textureCase,
                compact),
            fixture.sampleCount,
            std::string("textured compact/full-G-buffer execution failed: ") +
                textureCase.label);
        if (!passed)
            return false;
        if (executionCompleted)
            *executionCompleted = true;

        constexpr std::array<const char*, CompactGuideOutputCount>
            OutputNames = {
                "depth", "diffuse", "material", "normals",
                "emissive", "ambient-occlusion", "motion"
            };
        constexpr std::array<uint32_t, CompactGuideOutputCount>
            ChannelCounts = { 1u, 4u, 4u, 4u, 4u, 1u, 4u };
        // The full G-buffer side quantizes diffuse/material/AO to its real
        // production formats before the guide copy. These frozen limits admit
        // that reference quantization while rejecting a one-mip or materially
        // different anisotropic-footprint selection.
        constexpr std::array<float, CompactGuideOutputCount>
            Percentile99Limits = {
                1.e-6f,
                1.f / 255.f + 1.f / 2048.f,
                1.f / 255.f + 1.f / 2048.f,
                1.f / 1024.f,
                1.f / 1024.f,
                1.f / 255.f + 1.f / 2048.f,
                1.f / 1024.f
            };
        constexpr std::array<float, CompactGuideOutputCount>
            MaximumLimits = {
                1.e-5f,
                1.f / 128.f,
                1.f / 128.f,
                1.f / 128.f,
                1.f / 128.f,
                1.f / 128.f,
                1.f / 128.f
            };

        std::array<bool, CompactFixturePixelCount> covered{};
        uint32_t coveredPixelCount = 0u;
        uint32_t coverageMismatchCount = 0u;
        float minimumCoveredDepth = std::numeric_limits<float>::infinity();
        float maximumCoveredDepth =
            -std::numeric_limits<float>::infinity();
        for (uint32_t pixel = 0u;
            pixel < CompactFixturePixelCount;
            ++pixel)
        {
            const float referenceDepth =
                reference[CompactGuideDepth][pixel][0];
            const float compactDepth =
                compact[CompactGuideDepth][pixel][0];
            covered[pixel] = textureCase.steepPerspective
                ? std::isfinite(referenceDepth) &&
                    referenceDepth > 0.f && referenceDepth <= 1.f
                : Near(referenceDepth, FixtureDepth);
            coveredPixelCount += covered[pixel] ? 1u : 0u;
            if (covered[pixel])
            {
                minimumCoveredDepth = std::min(
                    minimumCoveredDepth,
                    referenceDepth);
                maximumCoveredDepth = std::max(
                    maximumCoveredDepth,
                    referenceDepth);
            }
            const bool coverageMatches =
                (covered[pixel] && std::isfinite(compactDepth) &&
                    std::abs(compactDepth - referenceDepth) <= 1.e-5f) ||
                (Near(referenceDepth, 0.f) &&
                    Near(compactDepth, 0.f));
            coverageMismatchCount += coverageMatches ? 0u : 1u;
            passed &= Check(
                coverageMatches,
                fixture.sampleCount,
                std::string(
                    "textured compact/reference coverage mismatch: ") +
                    textureCase.label);
        }
        std::cout << "[TEXTURE COVERAGE] " << textureCase.label
            << " reference-covered=" << coveredPixelCount << '/'
            << CompactFixturePixelCount << " mismatches="
            << coverageMismatchCount << ".\n";
        const bool expectsCompleteCoverage = !textureCase.alphaTested &&
            textureCase.ndcExtent >= 1.f &&
            textureCase.viewportOriginX == 0u &&
            textureCase.viewportOriginY == 0u &&
            textureCase.viewportWidth == CompactFixtureWidth &&
            textureCase.viewportHeight == CompactFixtureHeight;
        passed &= Check(
            coveredPixelCount >= 16u &&
                (expectsCompleteCoverage
                    ? coveredPixelCount == CompactFixturePixelCount
                    : coveredPixelCount < CompactFixturePixelCount),
            fixture.sampleCount,
            std::string(
                "inset textured discriminator did not retain a bounded interior: ") +
                textureCase.label);
        if (textureCase.steepPerspective)
        {
            passed &= Check(
                std::isfinite(minimumCoveredDepth) &&
                    std::isfinite(maximumCoveredDepth) &&
                    maximumCoveredDepth - minimumCoveredDepth > 1.e-4f,
                fixture.sampleCount,
                std::string(
                    "perspective discriminator did not produce nonconstant device depth: ") +
                    textureCase.label);
        }
        if (coveredPixelCount == 0u)
            return false;
        const bool coverageEvidencePassed = passed;

        std::array<std::array<float, 4u>, CompactGuideOutputCount>
            referenceMinimum{};
        std::array<std::array<float, 4u>, CompactGuideOutputCount>
            referenceMaximum{};
        for (uint32_t output = 0u;
            output < CompactGuideOutputCount;
            ++output)
        {
            referenceMinimum[output].fill(
                std::numeric_limits<float>::infinity());
            referenceMaximum[output].fill(
                -std::numeric_limits<float>::infinity());
        }
        bool referenceRangesValid = true;
        bool referenceNormalsValid = true;
        const auto InRange = [](float value, float minimum, float maximum)
        {
            return std::isfinite(value) && value >= minimum &&
                value <= maximum;
        };
        for (uint32_t pixel = 0u;
            pixel < CompactFixturePixelCount;
            ++pixel)
        {
            if (!covered[pixel])
                continue;
            for (uint32_t output = CompactGuideDiffuse;
                output <= CompactGuideEmissive;
                ++output)
            {
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    const float value = reference[output][pixel][channel];
                    referenceMinimum[output][channel] = std::min(
                        referenceMinimum[output][channel], value);
                    referenceMaximum[output][channel] = std::max(
                        referenceMaximum[output][channel], value);
                }
            }
            const float ambientOcclusion =
                reference[CompactGuideAmbientOcclusion][pixel][0];
            referenceMinimum[CompactGuideAmbientOcclusion][0] = std::min(
                referenceMinimum[CompactGuideAmbientOcclusion][0],
                ambientOcclusion);
            referenceMaximum[CompactGuideAmbientOcclusion][0] = std::max(
                referenceMaximum[CompactGuideAmbientOcclusion][0],
                ambientOcclusion);

            const auto& diffuse = reference[CompactGuideDiffuse][pixel];
            const auto& normal = reference[CompactGuideNormals][pixel];
            const auto& emissive = reference[CompactGuideEmissive][pixel];
            for (uint32_t channel = 0u; channel < 3u; ++channel)
            {
                referenceRangesValid &= InRange(
                    diffuse[channel], -1.e-5f, 1.00001f);
                referenceRangesValid &= InRange(
                    normal[channel], -1.00001f, 1.00001f);
                referenceRangesValid &= InRange(
                    emissive[channel], -1.e-5f, 1.00001f);
            }
            referenceRangesValid &= InRange(
                diffuse[3u], -1.e-5f, 1.00001f);
            referenceRangesValid &= InRange(
                normal[3u], -1.e-5f, 1.00001f);
            referenceRangesValid &= InRange(
                emissive[3u], -1.e-5f, 1.00001f);
            referenceRangesValid &= InRange(
                ambientOcclusion, -1.e-5f, 1.00001f);
            const float normalLengthSquared = normal[0u] * normal[0u] +
                normal[1u] * normal[1u] + normal[2u] * normal[2u];
            referenceNormalsValid &= std::isfinite(normalLengthSquared) &&
                normalLengthSquared >= 0.25f &&
                normalLengthSquared <= 1.25f;
        }
        const auto Varies = [&](uint32_t output, uint32_t channel)
        {
            return referenceMaximum[output][channel] -
                    referenceMinimum[output][channel] >
                1.e-4f;
        };
        const bool diffuseRgbNonzeroAndVariant =
            referenceMaximum[CompactGuideDiffuse][0u] > 1.e-4f &&
            referenceMaximum[CompactGuideDiffuse][1u] > 1.e-4f &&
            referenceMaximum[CompactGuideDiffuse][2u] > 1.e-4f &&
            Varies(CompactGuideDiffuse, 0u) &&
            Varies(CompactGuideDiffuse, 1u) &&
            Varies(CompactGuideDiffuse, 2u);
        const bool opacityExercised =
            referenceMinimum[CompactGuideDiffuse][3u] >= -1.e-5f &&
            referenceMaximum[CompactGuideDiffuse][3u] > 1.e-4f &&
            referenceMaximum[CompactGuideDiffuse][3u] < 0.99f &&
            Varies(CompactGuideDiffuse, 3u);
        const bool normalAndRoughnessVariant = referenceNormalsValid &&
            Varies(CompactGuideNormals, 0u) &&
            Varies(CompactGuideNormals, 1u) &&
            Varies(CompactGuideNormals, 2u) &&
            Varies(CompactGuideNormals, 3u);
        const bool emissiveRgbAndMetalnessVariant =
            referenceMaximum[CompactGuideEmissive][0u] > 1.e-4f &&
            referenceMaximum[CompactGuideEmissive][1u] > 1.e-4f &&
            referenceMaximum[CompactGuideEmissive][2u] > 1.e-4f &&
            Varies(CompactGuideEmissive, 0u) &&
            Varies(CompactGuideEmissive, 1u) &&
            Varies(CompactGuideEmissive, 2u) &&
            Varies(CompactGuideEmissive, 3u);
        const bool ambientOcclusionNonzeroAndVariant =
            referenceMaximum[CompactGuideAmbientOcclusion][0u] > 1.e-4f &&
            Varies(CompactGuideAmbientOcclusion, 0u);
        const bool referenceSignalPassed =
            referenceRangesValid && diffuseRgbNonzeroAndVariant &&
                opacityExercised && normalAndRoughnessVariant &&
                emissiveRgbAndMetalnessVariant &&
                ambientOcclusionNonzeroAndVariant;
        passed &= Check(
            referenceSignalPassed,
            fixture.sampleCount,
            std::string(
                "full-G-buffer reference texture signals were zero, constant, or out of range: ") +
                textureCase.label);
        std::cout << "[TEXTURE REFERENCE SIGNAL] " << textureCase.label
            << " opacity=["
            << referenceMinimum[CompactGuideDiffuse][3u] << ','
            << referenceMaximum[CompactGuideDiffuse][3u]
            << "] roughness=["
            << referenceMinimum[CompactGuideNormals][3u] << ','
            << referenceMaximum[CompactGuideNormals][3u]
            << "] metalness=["
            << referenceMinimum[CompactGuideEmissive][3u] << ','
            << referenceMaximum[CompactGuideEmissive][3u]
            << "] ao=["
            << referenceMinimum[CompactGuideAmbientOcclusion][0u] << ','
            << referenceMaximum[CompactGuideAmbientOcclusion][0u]
            << "].\n";

        std::array<bool, CompactGuideOutputCount> outputErrorPassed{};
        bool allOutputsFinite = true;
        for (uint32_t output = 0u;
            output < CompactGuideOutputCount;
            ++output)
        {
            std::vector<float> errors;
            errors.reserve(
                CompactFixturePixelCount * ChannelCounts[output]);
            bool finite = true;
            float largestError = -1.f;
            float largestExpected = 0.f;
            float largestActual = 0.f;
            uint32_t largestPixel = 0u;
            uint32_t largestChannel = 0u;
            for (uint32_t pixel = 0u;
                pixel < CompactFixturePixelCount;
                ++pixel)
            {
                if (!covered[pixel])
                    continue;
                for (uint32_t channel = 0u;
                    channel < ChannelCounts[output];
                    ++channel)
                {
                    const float expected =
                        reference[output][pixel][channel];
                    const float actual = compact[output][pixel][channel];
                    finite &= std::isfinite(expected) &&
                        std::isfinite(actual);
                    const float error = std::abs(actual - expected);
                    errors.push_back(error);
                    if (error > largestError)
                    {
                        largestError = error;
                        largestExpected = expected;
                        largestActual = actual;
                        largestPixel = pixel;
                        largestChannel = channel;
                    }
                }
            }
            std::sort(errors.begin(), errors.end());
            const size_t percentileIndex = std::min(
                errors.size() - 1u,
                static_cast<size_t>(std::ceil(
                    0.99 * double(errors.size()))) - 1u);
            const float percentile99 = errors[percentileIndex];
            const float maximum = errors.back();
            std::cout << "[TEXTURE ERROR] " << textureCase.label << ' '
                << OutputNames[output] << " p99=" << percentile99
                << " max=" << maximum << " limits=("
                << Percentile99Limits[output] << ','
                << MaximumLimits[output] << ") max-at=("
                << largestPixel % CompactFixtureWidth << ','
                << largestPixel / CompactFixtureWidth << ",c"
                << largestChannel << ") expected=" << largestExpected
                << " actual=" << largestActual << ".\n";
            outputErrorPassed[output] = finite &&
                percentile99 <= Percentile99Limits[output] &&
                maximum <= MaximumLimits[output];
            allOutputsFinite &= finite;
            passed &= Check(
                outputErrorPassed[output],
                fixture.sampleCount,
                std::string("textured compact guide exceeded frozen ") +
                    OutputNames[output] + " error limits: " +
                    textureCase.label);
        }
        if (passed && evidenceMask)
        {
            constexpr std::array<uint32_t, CompactGuideOutputCount>
                OutputEvidenceBits = {
                    TextureQualificationEvidence_GuideDepthError,
                    TextureQualificationEvidence_GuideDiffuseError,
                    TextureQualificationEvidence_GuideMaterialError,
                    TextureQualificationEvidence_GuideNormalError,
                    TextureQualificationEvidence_GuideEmissiveError,
                    TextureQualificationEvidence_GuideAmbientOcclusionError,
                    TextureQualificationEvidence_GuideMotionError
                };
            uint32_t observed = coverageEvidencePassed
                ? TextureQualificationEvidence_GuideCoverageParity
                : 0u;
            if (referenceSignalPassed)
            {
                observed |=
                    TextureQualificationEvidence_ReferenceSignalDiscrimination;
            }
            if (allOutputsFinite)
                observed |= TextureQualificationEvidence_FiniteOutputs;
            for (uint32_t output = 0u;
                output < CompactGuideOutputCount;
                ++output)
            {
                if (outputErrorPassed[output])
                    observed |= OutputEvidenceBits[output];
            }
            *evidenceMask = observed;
        }
        return passed;
    }

    [[nodiscard]] bool ValidateCompactTextureGradientDiscriminator(
        nvrhi::IDevice* device,
        CompactProductionFixture& fixture,
        bool* executionCompleted = nullptr)
    {
        const CompactTextureGradientCase discriminator = {
            "inset-affine/quad-edge/mip-transition/anisotropic",
            float2(1.875f, 0.022097087f),
            float2(0.1171875f, 0.0703125f),
            false,
            0.75f
        };
        return ValidateCompactTextureGradientCase(
            device, fixture, discriminator, nullptr, executionCompleted);
    }

    [[nodiscard]] bool ValidateCompactTextureMatrix(
        nvrhi::IDevice* device,
        donut::engine::ShaderFactory& shaderFactory,
        donut::engine::ShaderFactory& testShaderFactory,
        CompactProductionFixture& fixture2x,
        CompactTextureQualificationEvidence* evidence = nullptr,
        bool* universalPassed = nullptr)
    {
        if (universalPassed)
            *universalPassed = false;
        CompactProductionFixture fixture4x;
        CompactProductionFixture fixture8x;
        bool passed = Check(
            ValidateCompactAuthoredAlphaBands(),
            2u,
            "authored 64/128/192 uncompressed/BC alpha bands failed their "
            "exact CPU decode proof");
        passed &= Check(
            CreateCompactProductionFixture(
                device,
                shaderFactory,
                testShaderFactory,
                fixture4x,
                4u),
            4u,
            "could not create the 4x compact texture-matrix fixture");
        passed &= Check(
            CreateCompactProductionFixture(
                device,
                shaderFactory,
                testShaderFactory,
                fixture8x,
                8u),
            8u,
            "could not create the 8x compact texture-matrix fixture");
        if (!passed)
            return false;
        bool hardPassed = true;

        const auto EvidenceIndex = [](uint32_t sampleCount)
        {
            for (size_t index = 0u;
                index < TextureQualificationSampleCounts.size();
                ++index)
            {
                if (TextureQualificationSampleCounts[index] == sampleCount)
                    return index;
            }
            return TextureQualificationSampleCounts.size();
        };
        std::array<uint32_t, TextureQualificationSampleCounts.size()>
            guideEvidenceIntersections{};
        guideEvidenceIntersections.fill(0xffffffffu);
        std::array<uint32_t, TextureQualificationSampleCounts.size()>
            guideRowCounts{};
        const auto ValidateGuideRow = [&](
            CompactProductionFixture& fixture,
            const CompactTextureGradientCase& textureCase,
            uint32_t& rowEvidence)
        {
            rowEvidence = 0u;
            std::cout << "[STAGE] compact texture matrix "
                << textureCase.label << ".\n";
            uint32_t rasterEvidence = 0u;
            uint32_t guideEvidence = 0u;
            const bool rasterPassed = ValidateCompactMsaaRasterParity(
                device,
                fixture,
                textureCase,
                nullptr,
                &rasterEvidence);
            bool guideExecutionCompleted = false;
            const bool guidePassed = ValidateCompactTextureGradientCase(
                device,
                fixture,
                textureCase,
                &guideEvidence,
                &guideExecutionCompleted);
            hardPassed &= rasterPassed && guideExecutionCompleted;
            const bool rowPassed = rasterPassed && guidePassed;
            if (rowPassed)
                rowEvidence = rasterEvidence | guideEvidence;
            return rowPassed;
        };
        const auto ValidateRequiredGuideRow = [&] (
            CompactProductionFixture& fixture,
            const CompactTextureGradientCase& textureCase)
        {
            uint32_t rowEvidence = 0u;
            const bool rowPassed = ValidateGuideRow(
                fixture,
                textureCase,
                rowEvidence);
            const size_t index = EvidenceIndex(fixture.sampleCount);
            if (index < guideEvidenceIntersections.size())
            {
                ++guideRowCounts[index];
                guideEvidenceIntersections[index] &= rowPassed
                    ? rowEvidence
                    : 0u;
            }
            return rowPassed;
        };

        CompactTextureGradientCase g1;
        g1.label = "G1/2x/Q00/U/magnification-wrap";
        g1.texCoordScale = float2(0.04f, 0.04f);
        g1.texCoordOffset = float2(0.98f, 0.98f);
        g1.ndcExtent = 0.75f;

        CompactTextureGradientCase g2;
        g2.label = "G2/4x/Q00/U/isotropic-half-lod";
        g2.texCoordScale = float2(0.13258252f, 0.13258252f);
        g2.texCoordOffset = float2(0.30f, 0.0703125f);
        g2.ndcExtent = 0.75f;

        CompactTextureGradientCase g3;
        g3.label = "G3/8x/Q00/BC/rotated-anisotropic-16-plus";
        g3.texCoordScale = float2(1.875f, 0.022097087f);
        g3.texCoordOffset = float2(0.1171875f, 0.0703125f);
        g3.ndcExtent = 0.75f;
        g3.texCoordRotationRadians = 0.78539816339f;
        g3.texturePack =
            CompactMaterialTexturePack::BlockCompressedGuide;

        CompactTextureGradientCase g4;
        g4.label = "G4/4x/triangle-offset-x/U/magnification-wrap";
        g4.texCoordScale = float2(0.04f, 0.04f);
        g4.texCoordOffset = float2(0.98f, 0.98f);
        g4.ndcExtent = 0.65f;
        g4.ndcOffset = float2(0.25f, 0.f);

        CompactTextureGradientCase g5;
        g5.label = "G5/4x/triangle-offset-y/BC/isotropic-half-lod";
        g5.texCoordScale = float2(0.11490485f, 0.11490485f);
        g5.texCoordOffset = float2(0.30f, 0.0703125f);
        g5.ndcExtent = 0.65f;
        g5.ndcOffset = float2(0.f, 0.25f);
        g5.texturePack =
            CompactMaterialTexturePack::BlockCompressedGuide;

        CompactTextureGradientCase g6;
        g6.label = "G6/4x/triangle-offset-xy/U/rotated-anisotropic-16-plus";
        g6.texCoordScale = float2(1.625f, 0.0203125f);
        g6.texCoordOffset = float2(0.1171875f, 0.0703125f);
        g6.ndcExtent = 0.65f;
        g6.ndcOffset = float2(0.25f, 0.25f);
        g6.texCoordRotationRadians = 0.39269908169f;

        CompactTextureGradientCase g7;
        g7.label = "G7/2x/reverse-perspective/U/isotropic-varying-lod";
        g7.texCoordScale = float2(0.75f, 0.75f);
        g7.texCoordOffset = float2(0.1171875f, 0.0703125f);
        g7.steepPerspective = true;
        g7.ndcExtent = 0.75f;

        CompactTextureGradientCase g8;
        g8.label = "G8/8x/reverse-perspective/BC/rotated-anisotropic";
        g8.texCoordScale = float2(1.875f, 0.022097087f);
        g8.texCoordOffset = float2(0.1171875f, 0.0703125f);
        g8.steepPerspective = true;
        g8.ndcExtent = 0.75f;
        g8.texCoordRotationRadians = 0.78539816339f;
        g8.texturePack =
            CompactMaterialTexturePack::BlockCompressedGuide;

        passed &= ValidateRequiredGuideRow(fixture2x, g1);
        passed &= ValidateRequiredGuideRow(fixture4x, g2);
        passed &= ValidateRequiredGuideRow(fixture8x, g3);
        passed &= ValidateRequiredGuideRow(fixture4x, g4);
        passed &= ValidateRequiredGuideRow(fixture4x, g5);
        passed &= ValidateRequiredGuideRow(fixture4x, g6);
        passed &= ValidateRequiredGuideRow(fixture2x, g7);
        passed &= ValidateRequiredGuideRow(fixture8x, g8);

        constexpr uint32_t RequiredGuideEvidenceMask =
            TextureQualificationEvidence_RasterSampleDescriptorParity |
            TextureQualificationEvidence_GuideCoverageParity |
            TextureQualificationEvidence_GuideDepthError |
            TextureQualificationEvidence_GuideDiffuseError |
            TextureQualificationEvidence_GuideMaterialError |
            TextureQualificationEvidence_GuideNormalError |
            TextureQualificationEvidence_GuideEmissiveError |
            TextureQualificationEvidence_GuideAmbientOcclusionError |
            TextureQualificationEvidence_GuideMotionError |
            TextureQualificationEvidence_ReferenceSignalDiscrimination |
            TextureQualificationEvidence_FiniteOutputs;
        static_assert(
            (RequiredGuideEvidenceMask |
                TextureQualificationEvidence_ViewportOriginParity |
                TextureQualificationEvidence_AlphaCoverageParity |
                TextureQualificationEvidence_ValidationClean |
                TextureQualificationEvidence_FeatureFormatSupport) ==
                    TextureQualificationRequiredEvidenceMask,
            "every frozen qualification bit must have one observed evidence producer");
        constexpr std::array<uint32_t, 3u> RequiredGuideRowCounts = {
            2u, 4u, 2u
        };
        for (size_t index = 0u;
            index < guideEvidenceIntersections.size();
            ++index)
        {
            const bool complete =
                guideRowCounts[index] == RequiredGuideRowCounts[index] &&
                (guideEvidenceIntersections[index] &
                    RequiredGuideEvidenceMask) ==
                        RequiredGuideEvidenceMask;
            passed &= Check(
                complete,
                TextureQualificationSampleCounts[index],
                "required compact guide evidence rows were incomplete");
            if (complete && evidence)
            {
                evidence->samples[index].qualificationMask |=
                    guideEvidenceIntersections[index] &
                    RequiredGuideEvidenceMask;
            }
        }

        // Coarse derivative quad formation is target-dependent. The earlier
        // G4-G6 rows move geometry inside an origin-zero viewport and cannot
        // qualify viewport anchoring. Exercise all four actual integer
        // viewport-origin parities at each supported compact texture count,
        // with odd extents on shifted axes and triangles reaching the
        // viewport boundary so helper-lane behavior is part of the proof.
        const std::array<CompactProductionFixture*, 3u> parityFixtures = {
            &fixture2x,
            &fixture4x,
            &fixture8x
        };
        std::array<uint32_t, TextureQualificationSampleCounts.size()>
            observedParityMasks{};
        for (CompactProductionFixture* parityFixture : parityFixtures)
        {
            for (uint32_t parity = 0u; parity < 4u; ++parity)
            {
                CompactTextureGradientCase parityCase;
                const uint32_t originX = parity & 1u;
                const uint32_t originY = (parity >> 1u) & 1u;
                const std::string label =
                    "P/" + std::to_string(parityFixture->sampleCount) +
                    "x/viewport-origin-" + std::to_string(originX) +
                    std::to_string(originY) +
                    "/boundary-half-lod";
                parityCase.label = label.c_str();
                parityCase.texCoordScale =
                    float2(0.13258252f, 0.13258252f);
                parityCase.texCoordOffset =
                    float2(0.30f, 0.0703125f);
                parityCase.ndcExtent = 1.f;
                parityCase.viewportOriginX = originX;
                parityCase.viewportOriginY = originY;
                parityCase.viewportWidth =
                    CompactFixtureWidth - originX;
                parityCase.viewportHeight =
                    CompactFixtureHeight - originY;
                uint32_t rowEvidence = 0u;
                const bool parityPassed = ValidateGuideRow(
                    *parityFixture,
                    parityCase,
                    rowEvidence);
                passed &= parityPassed;
                const size_t index = EvidenceIndex(
                    parityFixture->sampleCount);
                if (parityPassed &&
                    (rowEvidence & RequiredGuideEvidenceMask) ==
                        RequiredGuideEvidenceMask &&
                    index < observedParityMasks.size())
                {
                    observedParityMasks[index] |= 1u << parity;
                }
            }
        }
        for (size_t index = 0u;
            index < observedParityMasks.size();
            ++index)
        {
            const bool complete = observedParityMasks[index] ==
                TextureQualificationViewportParityMask;
            passed &= Check(
                complete,
                TextureQualificationSampleCounts[index],
                "viewport-origin parity evidence was incomplete");
            if (complete && evidence)
            {
                evidence->samples[index].viewportOriginParityMask =
                    observedParityMasks[index];
                evidence->samples[index].qualificationMask |=
                    TextureQualificationEvidence_ViewportOriginParity;
            }
        }

        const std::array<float, 3u> alphaCutoffs = {
            127.f / 255.f,
            128.f / 255.f,
            129.f / 255.f
        };
        const std::array<const char*, 3u> alphaCutoffLabels = {
            "minus-1-lsb",
            "equal",
            "plus-1-lsb"
        };
        const auto ValidateAlphaRows = [&](
            CompactProductionFixture& fixture,
            const char* rowLabel,
            CompactTextureGradientCase alphaCase,
            bool proveConstantCompressedBands = false)
        {
            CompactTextureGradientCase opaqueControl = alphaCase;
            const std::string controlLabel = std::string(rowLabel) +
                "/opaque-control";
            opaqueControl.label = controlLabel.c_str();
            opaqueControl.alphaTested = false;
            CompactMsaaParitySummary controlSummary;
            bool rowsPassed = ValidateCompactMsaaRasterParity(
                device,
                fixture,
                opaqueControl,
                &controlSummary);
            CompactGuideImage controlGuide;
            rowsPassed &= Check(
                ExecuteCompactTextureGuideRun(
                    device,
                    fixture,
                    CompactFixtureRasterMode::FullGBufferReference,
                    opaqueControl,
                    controlGuide),
                fixture.sampleCount,
                std::string("alpha control guide execution failed: ") +
                    rowLabel);
            std::array<uint32_t, 3u> quantizedControlBands{};
            float closestToEquality =
                std::numeric_limits<float>::infinity();
            if (!controlGuide[CompactGuideDepth].empty())
            {
                for (uint32_t pixel = 0u;
                    pixel < CompactFixturePixelCount;
                    ++pixel)
                {
                    if (controlGuide[CompactGuideDepth][pixel][0] <= 0.f)
                        continue;
                    const float opacity =
                        controlGuide[CompactGuideDiffuse][pixel][3u];
                    closestToEquality = std::min(
                        closestToEquality,
                        std::abs(opacity - 128.f / 255.f));
                    for (size_t cutoffIndex = 0u;
                        cutoffIndex < alphaCutoffs.size();
                        ++cutoffIndex)
                    {
                        quantizedControlBands[cutoffIndex] +=
                            opacity >= alphaCutoffs[cutoffIndex] ? 1u : 0u;
                    }
                }
            }
            std::cout << "[ALPHA CONTROL] " << rowLabel
                << " quantized-kept=(" << quantizedControlBands[0u]
                << ',' << quantizedControlBands[1u] << ','
                << quantizedControlBands[2u]
                << ") closest-to-128=" << closestToEquality << ".\n";
            std::array<CompactMsaaParitySummary, 3u> alphaSummaries{};
            for (size_t cutoffIndex = 0u;
                cutoffIndex < alphaCutoffs.size();
                ++cutoffIndex)
            {
                const std::string alphaLabel = std::string(rowLabel) + '/' +
                    alphaCutoffLabels[cutoffIndex];
                alphaCase.label = alphaLabel.c_str();
                alphaCase.alphaTested = true;
                alphaCase.alphaCutoff = alphaCutoffs[cutoffIndex];
                rowsPassed &= ValidateCompactMsaaRasterParity(
                    device,
                    fixture,
                    alphaCase,
                    &alphaSummaries[cutoffIndex]);
                const CompactMsaaParitySummary& alphaSummary =
                    alphaSummaries[cutoffIndex];
                if (!proveConstantCompressedBands)
                {
                    rowsPassed &= Check(
                        alphaSummary.referenceCoveredSampleCount > 0u &&
                            alphaSummary.compactCoveredSampleCount > 0u &&
                            alphaSummary.referenceCoveredSampleCount <
                                controlSummary.referenceCoveredSampleCount &&
                            alphaSummary.compactCoveredSampleCount <
                                controlSummary.compactCoveredSampleCount,
                        fixture.sampleCount,
                        std::string(
                            "alpha row did not prove non-vacuous clipping against its opaque control: ") +
                            alphaCase.label);
                }
            }
            // Exact 64/128/192 bands plus HLSL clip(opacity-cutoff) mean the
            // 128 band survives both 127/255 and 128/255, then is rejected at
            // 129/255. Prove that relation independently on the unchanged
            // full-G-buffer raster and compact-visibility raster.
            if (proveConstantCompressedBands)
            {
                rowsPassed &= Check(
                    alphaSummaries[0u].referenceCoveredSampleCount ==
                            controlSummary.referenceCoveredSampleCount &&
                        alphaSummaries[1u].referenceCoveredSampleCount ==
                            controlSummary.referenceCoveredSampleCount &&
                        alphaSummaries[2u].referenceCoveredSampleCount == 0u &&
                        alphaSummaries[0u].compactCoveredSampleCount ==
                            controlSummary.compactCoveredSampleCount &&
                        alphaSummaries[1u].compactCoveredSampleCount ==
                            controlSummary.compactCoveredSampleCount &&
                        alphaSummaries[2u].compactCoveredSampleCount == 0u,
                    fixture.sampleCount,
                    std::string(
                        "constant compressed alpha row did not prove exact 128 equality semantics: ") +
                        rowLabel);

                const auto ValidateCompressedOverrideBand = [&] (
                    float textureU,
                    const char* bandLabel,
                    bool expectedVisible)
                {
                    CompactTextureGradientCase bandCase = alphaCase;
                    bandCase.texCoordScale = float2(0.f);
                    bandCase.texCoordOffset = float2(textureU, 0.5f);
                    bandCase.alphaTested = true;
                    bandCase.alphaCutoff = 128.f / 255.f;
                    const std::string label = std::string(rowLabel) + '/' +
                        bandLabel;
                    bandCase.label = label.c_str();
                    CompactMsaaParitySummary bandSummary;
                    const bool parityPassed = ValidateCompactMsaaRasterParity(
                        device,
                        fixture,
                        bandCase,
                        &bandSummary);
                    rowsPassed &= parityPassed;
                    const uint32_t expectedReference = expectedVisible
                        ? controlSummary.referenceCoveredSampleCount
                        : 0u;
                    const uint32_t expectedCompact = expectedVisible
                        ? controlSummary.compactCoveredSampleCount
                        : 0u;
                    rowsPassed &= Check(
                        bandSummary.referenceCoveredSampleCount ==
                                expectedReference &&
                            bandSummary.compactCoveredSampleCount ==
                                expectedCompact,
                        fixture.sampleCount,
                        std::string(
                            "compressed opacity override did not select its exact authored band: ") +
                            bandCase.label);
                };
                // The BC3 base alpha bands rise from 64 to 192 while the BC4
                // opacity override bands are reversed. These two rows prove
                // that the override, not contradictory base alpha, controls
                // alpha testing on both sides of the equality band.
                ValidateCompressedOverrideBand(
                    0.125f,
                    "bc4-override-high-base-low",
                    true);
                ValidateCompressedOverrideBand(
                    0.875f,
                    "bc4-override-low-base-high",
                    false);
            }
            else
            {
                rowsPassed &= Check(
                    alphaSummaries[0u].referenceCoveredSampleCount ==
                            alphaSummaries[1u].referenceCoveredSampleCount &&
                        alphaSummaries[1u].referenceCoveredSampleCount >
                            alphaSummaries[2u].referenceCoveredSampleCount &&
                        alphaSummaries[0u].compactCoveredSampleCount ==
                            alphaSummaries[1u].compactCoveredSampleCount &&
                        alphaSummaries[1u].compactCoveredSampleCount >
                            alphaSummaries[2u].compactCoveredSampleCount,
                    fixture.sampleCount,
                    std::string(
                        "alpha cutoff counts did not prove exact equality-band semantics: ") +
                        rowLabel);
            }
            return rowsPassed;
        };

        CompactTextureGradientCase a2;
        a2.texCoordScale = float2(0.75f, 0.75f);
        a2.texCoordOffset = float2(0.03125f, 0.046875f);
        a2.ndcExtent = 0.875f;
        a2.useBaseOrDiffuseTexture = true;
        a2.useOpacityTexture = false;
        a2.texturePack = CompactMaterialTexturePack::Uncompressed;

        CompactTextureGradientCase a4 = a2;
        a4.useOpacityTexture = true;

        CompactTextureGradientCase a8 = a4;
        // Hold the main row at the exact authored 128 plateau. Dedicated
        // constant low/high rows below prove both sides and the contradictory
        // BC3-base/BC4-opacity precedence without relying on filter-transition
        // samples to land on an equality value.
        a8.texCoordScale = float2(0.f);
        a8.texCoordOffset = float2(0.5f, 0.5f);
        a8.requireMixedAlphaPopulation = false;
        a8.texturePack = CompactMaterialTexturePack::BlockCompressedAlpha;

        const bool alpha2Passed = ValidateAlphaRows(
            fixture2x,
            "A2/2x/base-srgba8-alpha",
            a2);
        const bool alpha4Passed = ValidateAlphaRows(
            fixture4x,
            "A4/4x/r8-opacity-override-contradictory-base",
            a4);
        const bool alpha8Passed = ValidateAlphaRows(
            fixture8x,
            "A8/8x/bc3-srgb-base-alpha-bc4-opacity-override",
            a8,
            true);
        const std::array<bool, 3u> alphaEvidence = {
            alpha2Passed, alpha4Passed, alpha8Passed
        };
        for (size_t index = 0u; index < alphaEvidence.size(); ++index)
        {
            hardPassed &= alphaEvidence[index];
            passed &= alphaEvidence[index];
            if (alphaEvidence[index] && evidence)
            {
                evidence->samples[index].qualificationMask |=
                    TextureQualificationEvidence_AlphaCoverageParity;
            }
        }
        if (universalPassed)
            *universalPassed = hardPassed;
        return passed;
    }

    [[nodiscard]] bool ExecuteCompactGuideRun(
        nvrhi::IDevice* device,
        CompactProductionFixture& fixture,
        CompactGuideMutation mutation,
        CompactGuidePixel& pixel)
    {
        const nvrhi::CommandListHandle commandList =
            device->createCommandList();
        if (!commandList)
            return false;
        commandList->open();
        RecordCompactFixtureRaster(commandList, fixture);
        RecordCompactDescriptorReadTransitions(commandList, fixture);
        if (mutation == CompactGuideMutation::ZeroFallbackDepth)
        {
            commandList->clearDepthStencilTexture(
                fixture.depth,
                nvrhi::AllSubresources,
                true,
                0.f,
                false,
                0u);
        }
        else if (mutation ==
            CompactGuideMutation::DegenerateFallbackNormal)
        {
            commandList->clearTextureFloat(
                fixture.fallbackNormals,
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
        }
        commandList->writeBuffer(
            fixture.compactConstantsBuffer,
            &fixture.compactConstants,
            sizeof(fixture.compactConstants));
        for (uint32_t output = 0u;
            output < CompactGuideOutputCount;
            ++output)
        {
            commandList->clearTextureFloat(
                fixture.guide.outputs[output],
                nvrhi::AllSubresources,
                nvrhi::Color(7.f));
        }
        nvrhi::ComputeState state;
        state.pipeline = fixture.compactResolvePipeline;
        state.bindings = {
            fixture.guide.bindingSet,
            fixture.compactDescriptorTable
        };
        commandList->setComputeState(state);
        commandList->dispatch(1u, 1u, 1u);
        for (uint32_t output = 0u;
            output < CompactGuideOutputCount;
            ++output)
        {
            commandList->copyTexture(
                fixture.guide.readbacks[output],
                nvrhi::TextureSlice(),
                fixture.guide.outputs[output],
                nvrhi::TextureSlice());
        }
        commandList->close();
        device->executeCommandList(commandList);
        if (!device->waitForIdle())
            return false;
        return ReadCompactGuidePixel(
            device,
            fixture.guide,
            CompactFallbackPixelX,
            CompactFallbackPixelY,
            pixel);
    }

    [[nodiscard]] bool CompactGuideIsCleared(
        const CompactGuidePixel& pixel)
    {
        if (!Near(pixel[CompactGuideDepth][0], 0.f) ||
            !Near(pixel[CompactGuideAmbientOcclusion][0], 1.f))
        {
            return false;
        }
        for (uint32_t output = CompactGuideDiffuse;
            output < CompactGuideOutputCount;
            ++output)
        {
            if (output == CompactGuideAmbientOcclusion)
                continue;
            for (float channel : pixel[output])
            {
                if (!Near(channel, 0.f))
                    return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool ValidateCompactGuideResolve(
        nvrhi::IDevice* device,
        CompactProductionFixture& fixture)
    {
        CompactGuidePixel valid{};
        CompactGuidePixel zeroDepth{};
        CompactGuidePixel zeroNormal{};
        bool passed = Check(
            ExecuteCompactGuideRun(
                device,
                fixture,
                CompactGuideMutation::None,
                valid),
            CompactFixtureSampleCount,
            "production mixed compact guide resolve/readback failed");
        if (!passed)
            return false;

        const auto Finite4 = [](const std::array<float, 4u>& value)
        {
            return std::all_of(
                value.begin(), value.end(),
                [](float channel) { return std::isfinite(channel); });
        };
        const std::array<float, 4u>& normal =
            valid[CompactGuideNormals];
        passed &= Check(
            Near(valid[CompactGuideDepth][0], FixtureDepth) &&
                Finite4(valid[CompactGuideDiffuse]) &&
                (valid[CompactGuideDiffuse][0] > 0.f ||
                    valid[CompactGuideDiffuse][1] > 0.f ||
                    valid[CompactGuideDiffuse][2] > 0.f) &&
                Finite4(valid[CompactGuideMaterial]) &&
                normal[0] * normal[0] + normal[1] * normal[1] +
                        normal[2] * normal[2] > 1e-12f &&
                Near(
                    valid[CompactGuideEmissive][0],
                    CompactFallbackEmissiveR) &&
                Near(
                    valid[CompactGuideEmissive][1],
                    CompactFallbackEmissiveG) &&
                Near(
                    valid[CompactGuideEmissive][2],
                    CompactFallbackEmissiveB) &&
                Near(valid[CompactGuideAmbientOcclusion][0], 1.f) &&
                Near(valid[CompactGuideMotion][0], 0.f) &&
                Near(valid[CompactGuideMotion][1], 0.f) &&
                Near(valid[CompactGuideMotion][2], 0.f) &&
                Near(valid[CompactGuideMotion][3], 1.f),
            CompactFixtureSampleCount,
            "production guide resolve did not copy the local fallback "
            "G-buffer owner");

        passed &= Check(
            ExecuteCompactGuideRun(
                device,
                fixture,
                CompactGuideMutation::ZeroFallbackDepth,
                zeroDepth) &&
                CompactGuideIsCleared(zeroDepth),
            CompactFixtureSampleCount,
            "depth-zero fallback owner did not clear all seven production "
            "guide outputs");
        passed &= Check(
            ExecuteCompactGuideRun(
                device,
                fixture,
                CompactGuideMutation::DegenerateFallbackNormal,
                zeroNormal) &&
                CompactGuideIsCleared(zeroNormal),
            CompactFixtureSampleCount,
            "degenerate-normal fallback owner did not clear all seven "
            "production guide outputs");
        return passed;
    }

    void RecordCompactLighting(
        nvrhi::ICommandList* commandList,
        CompactProductionFixture& fixture,
        const CompactLightingRun& run,
        uint32_t preparationQueueCapacity,
        uint32_t selectedPixelX,
        uint32_t selectedPixelY,
        CompactMaterialTexturePack texturePack =
            CompactMaterialTexturePack::Uncompressed)
    {
        fixture.compactConstants.preparationQueueCapacity =
            preparationQueueCapacity;
        DiagnosticsProducerGpuConstants diagnosticsConstants{};
        diagnosticsConstants.executionFlags =
            DiagnosticsProducerWriteSampleStatus |
            DiagnosticsProducerWritePixelMetadata |
            DiagnosticsProducerWriteSelectedSamples |
            DiagnosticsProducerValidateErrorBudget;
        diagnosticsConstants.selectedPixelX = selectedPixelX;
        diagnosticsConstants.selectedPixelY = selectedPixelY;
        diagnosticsConstants.producerCapabilityMask =
            DiagnosticsCounterAllValidityBits;
        diagnosticsConstants.viewportWidth = CompactFixtureWidth;
        diagnosticsConstants.viewportHeight = CompactFixtureHeight;

        commandList->clearTextureUInt(
            run.diagnostics.sampleStatus,
            nvrhi::AllSubresources,
            0xffu);
        commandList->clearTextureFloat(
            run.diagnostics.multisampledBackground,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        commandList->clearBufferUInt(
            run.diagnostics.selectedSampleData,
            0xffffffffu);
        commandList->clearBufferUInt(
            run.diagnostics.pixelMetadata,
            0u);
        commandList->writeBuffer(
            fixture.compactConstantsBuffer,
            &fixture.compactConstants,
            sizeof(fixture.compactConstants));
        commandList->writeBuffer(
            fixture.deferredConstantsBuffer,
            &fixture.deferredConstants,
            sizeof(fixture.deferredConstants));
        commandList->writeBuffer(
            fixture.diagnosticsConstantsBuffer,
            &diagnosticsConstants,
            sizeof(diagnosticsConstants));

        nvrhi::ComputeState state;
        state.pipeline = fixture.compactLightingPipeline;
        state.bindings = {
            run.bindingSet,
            GetCompactDescriptorTable(fixture, texturePack)
        };
        commandList->setComputeState(state);
        commandList->dispatch(1u, 1u, 1u);
    }

    [[nodiscard]] bool ExecuteCompactLightingRun(
        nvrhi::IDevice* device,
        AdaptiveMsaaDiagnosticsGpuPass& diagnosticsPass,
        CompactProductionFixture& fixture,
        CompactLightingRun& run,
        nvrhi::ITexture* referenceOutput,
        uint32_t preparationQueueCapacity,
        bool recordRaster,
        DiagnosticsGpuReadbackBlock& readback,
        CompactFixtureRasterMode rasterMode =
            CompactFixtureRasterMode::Mixed,
        const CompactTextureGradientCase* textureCase = nullptr,
        uint32_t selectedPixelX = CompactSelectedPixelX,
        uint32_t selectedPixelY = CompactSelectedPixelY)
    {
        const nvrhi::CommandListHandle commandList =
            device->createCommandList();
        if (!commandList)
            return false;

        AdaptiveMsaaDiagnosticsPassRequest request;
        request.settings.enabled = true;
        request.settings.comparisonMode =
            DiagnosticsComparisonMode::ErrorHeatmap;
        request.settings.debugView =
            AdaptiveMsaaDebugView::AdaptiveLightingError;
        request.settings.selectedPixelCaptureRequested = true;
        request.settings.selectedPixelX = selectedPixelX;
        request.settings.selectedPixelY = selectedPixelY;
        request.settings.selectedPixelCaptureId =
            0x434f4d5000000000ull | preparationQueueCapacity;
        request.settings.readbackEnabled = true;
        request.settings.countersEnabled = true;
        request.settings.dualRenderingEnabled = true;
        request.frameIndex =
            0x5348444500000000ull | preparationQueueCapacity;
        request.visualizedSampleIndex = CompactFixtureSampleCount - 1u;
        request.maximumWorkListCapacity = CompactQueueMaximum;

        AdaptiveMsaaDiagnosticsPassInputs inputs;
        inputs.surfaceKeys = fixture.surfaceOwnership;
        inputs.depth = fixture.depth;
        inputs.visibilityClass = fixture.visibilityClass;
        inputs.output = run.visualization;
        inputs.referenceHdr = referenceOutput
            ? referenceOutput
            : run.output.Get();
        inputs.adaptiveHdr = run.output;
        inputs.sampleStatus = run.diagnostics.sampleStatus;
        inputs.selectedSampleData =
            run.diagnostics.selectedSampleData;
        inputs.pixelMetadata = run.diagnostics.pixelMetadata;
        inputs.producerCapabilityMask =
            DiagnosticsCounterAllValidityBits;

        commandList->open();
        if (recordRaster)
        {
            RecordCompactFixtureRaster(
                commandList,
                fixture,
                rasterMode,
                textureCase);
        }
        const CompactMaterialTexturePack texturePack = textureCase
            ? textureCase->texturePack
            : CompactMaterialTexturePack::Uncompressed;
        RecordCompactDescriptorReadTransitions(
            commandList,
            fixture,
            texturePack);
        RecordCompactLighting(
            commandList,
            fixture,
            run,
            preparationQueueCapacity,
            selectedPixelX,
            selectedPixelY,
            texturePack);
        const AdaptiveMsaaDiagnosticsPassResult execution =
            diagnosticsPass.Execute(commandList, inputs, request);
        const bool verboseReasonCapture =
            std::getenv(
                "UVSR_COMPACT_TEXTURE_REASON_CAPTURE_ONLY") != nullptr;
        if (verboseReasonCapture)
        {
            std::cout << "[TEXTURE REASON PIPELINE] executeStatus="
                << static_cast<uint32_t>(execution.status)
                << " dispatched=" << (execution.dispatched ? 1u : 0u)
                << " viewAvailable="
                << (execution.viewAvailable ? 1u : 0u)
                << " readbackRecorded="
                << (execution.readbackRecorded ? 1u : 0u)
                << " token=" << execution.readbackToken << ".\n";
        }
        if (execution.status != DiagnosticsPassStatus::Success ||
            !execution.dispatched || !execution.viewAvailable ||
            !execution.readbackRecorded || execution.readbackToken == 0u)
        {
            if (execution.readbackToken != 0u)
            {
                (void)diagnosticsPass.CancelUnsubmittedReadback(
                    execution.readbackToken);
            }
            commandList->close();
            return false;
        }
        commandList->copyTexture(
            run.outputReadback,
            nvrhi::TextureSlice(),
            run.output,
            nvrhi::TextureSlice());
        commandList->copyTexture(
            run.visualizationReadback,
            nvrhi::TextureSlice(),
            run.visualization,
            nvrhi::TextureSlice());
        commandList->close();
        device->executeCommandList(commandList);

        const DiagnosticsPassStatus signalStatus =
            diagnosticsPass.SignalReadbackSubmission(
                execution.readbackToken);
        const bool idle = device->waitForIdle();
        const DiagnosticsReadbackPollResult poll =
            diagnosticsPass.PollReadback(
                request.settings,
                readback);
        if (verboseReasonCapture)
        {
            std::cout << "[TEXTURE REASON PIPELINE] signalStatus="
                << static_cast<uint32_t>(signalStatus)
                << " idle=" << (idle ? 1u : 0u)
                << " pollCompleted=" << (poll.completed ? 1u : 0u)
                << " pollStatus="
                << static_cast<uint32_t>(poll.status)
                << " pollToken=" << poll.token << ".\n";
        }
        bool passed = signalStatus == DiagnosticsPassStatus::Success;
        passed &= idle;
        passed &= poll.completed &&
            poll.status == DiagnosticsPassStatus::Success &&
            poll.token == execution.readbackToken;
        return passed;
    }

    [[nodiscard]] bool ReadCompactLighting(
        nvrhi::IDevice* device,
        nvrhi::IStagingTexture* staging,
        std::vector<std::array<float, 4u>>& pixels);

    [[nodiscard]] bool CaptureCompactTextureFailureReason(
        nvrhi::IDevice* device,
        CompactProductionFixture& fixture)
    {
        constexpr uint32_t SelectedX = 3u;
        constexpr uint32_t SelectedY = 3u;
        const CompactTextureGradientCase textureCase = {
            "reason-capture/inset-affine-textured",
            float2(1.875f, 0.022097087f),
            float2(0.1171875f, 0.0703125f),
            false,
            0.75f
        };
        CompactLightingRun run;
        bool passed = Check(
            CreateCompactLightingRun(
                device,
                fixture,
                "Adaptive MSAA Compact GPU Test/Texture Reason Capture",
                run),
            CompactFixtureSampleCount,
            "could not create compact texture reason-capture resources");
        if (!passed)
            return false;

        const nvrhi::StagingTextureHandle statusReadback =
            device->createStagingTexture(
                run.diagnostics.sampleStatus->getDesc(),
                nvrhi::CpuAccessMode::Read);
        nvrhi::BufferDesc selectedReadbackDesc;
        selectedReadbackDesc.byteSize =
            sizeof(DiagnosticsGpuSampleInputRecord) *
            CompactFixtureSampleCount;
        selectedReadbackDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        selectedReadbackDesc.initialState =
            nvrhi::ResourceStates::CopyDest;
        selectedReadbackDesc.keepInitialState = true;
        selectedReadbackDesc.debugName =
            "Adaptive MSAA Compact GPU Test/Texture Reason Selected Readback";
        const nvrhi::BufferHandle selectedReadback =
            device->createBuffer(selectedReadbackDesc);
        const nvrhi::CommandListHandle commandList =
            device->createCommandList();
        passed &= Check(
            statusReadback && selectedReadback && commandList,
            CompactFixtureSampleCount,
            "could not create the direct producer reason readback");
        if (!passed)
            return false;

        commandList->open();
        RecordCompactFixtureRaster(
            commandList,
            fixture,
            CompactFixtureRasterMode::CompactOnly,
            &textureCase);
        RecordCompactDescriptorReadTransitions(commandList, fixture);
        RecordCompactLighting(
            commandList,
            fixture,
            run,
            CompactQueueMaximum,
            SelectedX,
            SelectedY);
        for (uint32_t sampleIndex = 0u;
            sampleIndex < CompactFixtureSampleCount;
            ++sampleIndex)
        {
            const nvrhi::TextureSlice slice =
                nvrhi::TextureSlice().setArraySlice(sampleIndex);
            commandList->copyTexture(
                statusReadback,
                slice,
                run.diagnostics.sampleStatus,
                slice);
        }
        commandList->copyBuffer(
            selectedReadback,
            0u,
            run.diagnostics.selectedSampleData,
            0u,
            selectedReadbackDesc.byteSize);
        commandList->copyTexture(
            run.outputReadback,
            nvrhi::TextureSlice(),
            run.output,
            nvrhi::TextureSlice());
        commandList->close();
        device->executeCommandList(commandList);
        passed &= Check(
            device->waitForIdle(),
            CompactFixtureSampleCount,
            "direct producer reason readback did not reach GPU idle");
        if (!passed)
            return false;

        std::array<uint8_t, CompactFixtureSampleCount> packedStatuses{};
        for (uint32_t sampleIndex = 0u;
            sampleIndex < CompactFixtureSampleCount;
            ++sampleIndex)
        {
            const nvrhi::TextureSlice slice =
                nvrhi::TextureSlice().setArraySlice(sampleIndex);
            size_t rowPitch = 0u;
            const auto* mapped = static_cast<const uint8_t*>(
                device->mapStagingTexture(
                    statusReadback,
                    slice,
                    nvrhi::CpuAccessMode::Read,
                    &rowPitch));
            passed &= Check(
                mapped && rowPitch >= CompactFixtureWidth,
                CompactFixtureSampleCount,
                "could not map a direct R8 sample-status slice");
            if (mapped && rowPitch >= CompactFixtureWidth)
            {
                packedStatuses[sampleIndex] = mapped[
                    size_t(SelectedY) * rowPitch + SelectedX];
            }
            if (mapped)
                device->unmapStagingTexture(statusReadback);
        }

        std::array<DiagnosticsGpuSampleInputRecord,
            CompactFixtureSampleCount> selectedRecords{};
        const void* selectedMapped = device->mapBuffer(
            selectedReadback,
            nvrhi::CpuAccessMode::Read);
        passed &= Check(
            selectedMapped != nullptr,
            CompactFixtureSampleCount,
            "could not map the direct selected-sample producer records");
        if (selectedMapped)
        {
            std::memcpy(
                selectedRecords.data(),
                selectedMapped,
                selectedReadbackDesc.byteSize);
            device->unmapBuffer(selectedReadback);
        }

        std::vector<std::array<float, 4u>> outputPixels;
        passed &= Check(
            ReadCompactLighting(
                device,
                run.outputReadback,
                outputPixels) &&
                outputPixels.size() == CompactFixturePixelCount,
            CompactFixtureSampleCount,
            "could not read the direct compact lighting output");
        if (!passed)
            return false;

        uint32_t capturedReason = DiagnosticsGpuInvalidWord;
        uint32_t reconstructedSamples = 0u;
        for (uint32_t sampleIndex = 0u;
            sampleIndex < CompactFixtureSampleCount;
            ++sampleIndex)
        {
            const DiagnosticsGpuSampleInputRecord& sample =
                selectedRecords[sampleIndex];
            const uint32_t packedReason =
                UnpackDiagnosticsGpuFallbackReason(
                    packedStatuses[sampleIndex]);
            const bool reasonFieldValid =
                (sample.validFields &
                    DiagnosticsSampleInputFallbackReason) != 0u;
            const bool reasonValid = sample.fallbackReason <
                static_cast<uint32_t>(FallbackReason::Count);
            const bool reconstructed =
                (sample.validFields &
                    DiagnosticsSampleInputReconstructedBarycentrics) !=
                0u;
            reconstructedSamples += reconstructed ? 1u : 0u;
            if (sampleIndex == 0u)
                capturedReason = sample.fallbackReason;
            const bool reasonMatches = sample.fallbackReason ==
                capturedReason;
            std::cout << "[TEXTURE PRODUCER REASON] sample=" << sampleIndex
                << " packed=0x" << std::hex
                << uint32_t(packedStatuses[sampleIndex])
                << " validFields=0x" << std::hex << sample.validFields
                << std::dec << " reason=" << sample.fallbackReason
                << " (" << (reasonValid
                    ? GetFallbackReasonName(static_cast<FallbackReason>(
                        sample.fallbackReason))
                    : "invalid") << ")"
                << " reconstructed=" << (reconstructed ? 1u : 0u)
                << " bary=(" << sample.reconstructedBarycentricX
                << ',' << sample.reconstructedBarycentricY << ')'
                << " radiance=(" << sample.radianceR << ','
                << sample.radianceG << ',' << sample.radianceB << ')'
                << ".\n";
            passed &= Check(
                reasonFieldValid && reasonValid && reasonMatches &&
                    packedReason == sample.fallbackReason,
                CompactFixtureSampleCount,
                "the direct producer status and selected fallback record do not agree");
        }

        const std::array<float, 4u>& selectedOutput = outputPixels[
            SelectedY * CompactFixtureWidth + SelectedX];
        const bool outputFinite = std::all_of(
            selectedOutput.begin(),
            selectedOutput.end(),
            [](float value) { return std::isfinite(value); });
        const float outputEnergy = std::abs(selectedOutput[0u]) +
            std::abs(selectedOutput[1u]) +
            std::abs(selectedOutput[2u]);
        std::cout << "[TEXTURE PRODUCER OUTPUT] pixel=(" << SelectedX
            << ',' << SelectedY << ") rgba=(" << selectedOutput[0u]
            << ',' << selectedOutput[1u] << ',' << selectedOutput[2u]
            << ',' << selectedOutput[3u] << ") reconstructed="
            << reconstructedSamples << '/' << CompactFixtureSampleCount
            << ".\n";
        passed &= Check(
            capturedReason < static_cast<uint32_t>(FallbackReason::Count) &&
                outputFinite,
            CompactFixtureSampleCount,
            "the direct producer trace did not expose a valid reason and finite output");
        if (reconstructedSamples == CompactFixtureSampleCount)
        {
            passed &= Check(
                (capturedReason ==
                        static_cast<uint32_t>(FallbackReason::None) ||
                    capturedReason == static_cast<uint32_t>(
                        FallbackReason::UnsafeSharing) ||
                    capturedReason == static_cast<uint32_t>(
                        FallbackReason::WorkListOverflow)) &&
                    outputEnergy > 0.f,
                CompactFixtureSampleCount,
                "a reconstructed producer record did not yield non-background lighting");
        }
        else
        {
            passed &= Check(
                reconstructedSamples == 0u &&
                    capturedReason != static_cast<uint32_t>(
                        FallbackReason::None) &&
                    outputEnergy == 0.f,
                CompactFixtureSampleCount,
                "a contained reconstruction failure did not preserve the cleared background");
        }
        std::cout << "[TEXTURE RAW VISIBILITY] key/depth/class are fixture "
            "assumptions in this direct producer trace; no MSAA resource "
            "was copied to staging.\n";
        return passed;
    }

    [[nodiscard]] nvrhi::TextureHandle CreateMsaaInput(
        nvrhi::IDevice* device,
        nvrhi::Format format,
        uint32_t sampleCount,
        const std::string& debugName)
    {
        nvrhi::TextureDesc description;
        description.width = FixtureWidth;
        description.height = FixtureHeight;
        description.sampleCount = sampleCount;
        description.sampleQuality = 0u;
        description.format = format;
        description.dimension = nvrhi::TextureDimension::Texture2DMS;
        description.isShaderResource = true;
        description.isRenderTarget = true;
        description.initialState = nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        description.debugName = debugName;
        return device->createTexture(description);
    }

    [[nodiscard]] nvrhi::TextureHandle CreateSingleColorTexture(
        nvrhi::IDevice* device,
        bool unorderedAccess,
        const std::string& debugName)
    {
        nvrhi::TextureDesc description;
        description.width = FixtureWidth;
        description.height = FixtureHeight;
        description.format = nvrhi::Format::RGBA16_FLOAT;
        description.dimension = nvrhi::TextureDimension::Texture2D;
        description.isShaderResource = true;
        description.isRenderTarget = !unorderedAccess;
        description.isUAV = unorderedAccess;
        description.initialState = unorderedAccess
            ? nvrhi::ResourceStates::UnorderedAccess
            : nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        description.debugName = debugName;
        return device->createTexture(description);
    }

    [[nodiscard]] nvrhi::TextureHandle CreateSampleStatusTexture(
        nvrhi::IDevice* device,
        uint32_t sampleCount)
    {
        nvrhi::TextureDesc description;
        description.width = FixtureWidth;
        description.height = FixtureHeight;
        description.arraySize = sampleCount;
        description.format = nvrhi::Format::R8_UINT;
        description.dimension = nvrhi::TextureDimension::Texture2DArray;
        description.isShaderResource = true;
        description.isUAV = true;
        description.initialState = nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        description.debugName = "Adaptive MSAA GPU Test/Sample Status";
        return device->createTexture(description);
    }

    template <typename Record>
    [[nodiscard]] nvrhi::BufferHandle CreateStructuredInput(
        nvrhi::IDevice* device,
        uint32_t recordCount,
        const std::string& debugName)
    {
        nvrhi::BufferDesc description;
        description.byteSize = uint64_t(sizeof(Record)) * recordCount;
        description.structStride = sizeof(Record);
        description.initialState = nvrhi::ResourceStates::ShaderResource;
        description.keepInitialState = true;
        description.debugName = debugName;
        return device->createBuffer(description);
    }

    [[nodiscard]] bool ValidateOutput(
        nvrhi::IDevice* device,
        nvrhi::IStagingTexture* staging,
        uint32_t sampleCount)
    {
        size_t rowPitch = 0u;
        const auto* mapped = static_cast<const uint8_t*>(
            device->mapStagingTexture(
                staging,
                nvrhi::TextureSlice(),
                nvrhi::CpuAccessMode::Read,
                &rowPitch));
        if (!Check(mapped != nullptr, sampleCount,
                "could not map RGBA16F visualization readback"))
        {
            return false;
        }

        bool passed = true;
        const size_t minimumRowPitch = FixtureWidth * 4u * sizeof(uint16_t);
        passed &= Check(
            rowPitch >= minimumRowPitch,
            sampleCount,
            "visualization readback row pitch is too small");
        if (rowPitch >= minimumRowPitch)
        {
            for (uint32_t y = 0u; y < FixtureHeight; ++y)
            {
                const auto* row = reinterpret_cast<const uint16_t*>(
                    mapped + size_t(y) * rowPitch);
                for (uint32_t x = 0u; x < FixtureWidth; ++x)
                {
                    const uint16_t* texel = row + x * 4u;
                    const float red = donut::math::Float16ToFloat32(
                        { texel[0] });
                    const float green = donut::math::Float16ToFloat32(
                        { texel[1] });
                    const float blue = donut::math::Float16ToFloat32(
                        { texel[2] });
                    const float alpha = donut::math::Float16ToFloat32(
                        { texel[3] });
                    // Identical reference/adaptive HDR inputs have zero
                    // relative error. ErrorHeat(0) is exact opaque blue.
                    passed &= Check(
                        Near(red, 0.f) && Near(green, 0.f) &&
                            Near(blue, 1.f) && Near(alpha, 1.f),
                        sampleCount,
                        "lighting-error visualization did not produce "
                        "opaque zero-error blue");
                }
            }
        }
        device->unmapStagingTexture(staging);
        return passed;
    }

    [[nodiscard]] bool ValidateCompactVisualization(
        nvrhi::IDevice* device,
        nvrhi::IStagingTexture* staging)
    {
        size_t rowPitch = 0u;
        const auto* mapped = static_cast<const uint8_t*>(
            device->mapStagingTexture(
                staging,
                nvrhi::TextureSlice(),
                nvrhi::CpuAccessMode::Read,
                &rowPitch));
        if (!Check(mapped != nullptr, CompactFixtureSampleCount,
                "could not map compact zero-error visualization"))
        {
            return false;
        }
        bool passed = true;
        for (uint32_t y = 0u; y < CompactFixtureHeight; ++y)
        {
            const auto* row = reinterpret_cast<const uint16_t*>(
                mapped + size_t(y) * rowPitch);
            for (uint32_t x = 0u; x < CompactFixtureWidth; ++x)
            {
                const uint16_t* texel = row + x * 4u;
                const float red = donut::math::Float16ToFloat32(
                    { texel[0] });
                const float green = donut::math::Float16ToFloat32(
                    { texel[1] });
                const float blue = donut::math::Float16ToFloat32(
                    { texel[2] });
                const float alpha = donut::math::Float16ToFloat32(
                    { texel[3] });
                passed &= Check(
                    Near(red, 0.f) && Near(green, 0.f) &&
                        Near(blue, 1.f) && Near(alpha, 1.f),
                    CompactFixtureSampleCount,
                    "queue-overflow fallback differs from the capacity-128 "
                    "production output");
            }
        }
        device->unmapStagingTexture(staging);
        return passed;
    }

    [[nodiscard]] bool ReadCompactLighting(
        nvrhi::IDevice* device,
        nvrhi::IStagingTexture* staging,
        std::vector<std::array<float, 4u>>& pixels)
    {
        size_t rowPitch = 0u;
        const auto* mapped = static_cast<const uint8_t*>(
            device->mapStagingTexture(
                staging,
                nvrhi::TextureSlice(),
                nvrhi::CpuAccessMode::Read,
                &rowPitch));
        if (!mapped)
            return false;
        pixels.resize(CompactFixturePixelCount);
        for (uint32_t y = 0u; y < CompactFixtureHeight; ++y)
        {
            const auto* row = reinterpret_cast<const uint16_t*>(
                mapped + size_t(y) * rowPitch);
            for (uint32_t x = 0u; x < CompactFixtureWidth; ++x)
            {
                const uint16_t* texel = row + x * 4u;
                std::array<float, 4u>& output =
                    pixels[y * CompactFixtureWidth + x];
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    output[channel] =
                        donut::math::Float16ToFloat32(
                            { texel[channel] });
                }
            }
        }
        device->unmapStagingTexture(staging);
        return true;
    }

    [[nodiscard]] bool ValidateCompactLightingOutputs(
        nvrhi::IDevice* device,
        const CompactLightingRun& baseline,
        const CompactLightingRun& overflow,
        uint32_t expectedCoveredPixelCount)
    {
        std::vector<std::array<float, 4u>> baselinePixels;
        std::vector<std::array<float, 4u>> overflowPixels;
        bool passed = Check(
            ReadCompactLighting(
                device,
                baseline.outputReadback,
                baselinePixels) &&
            ReadCompactLighting(
                device,
                overflow.outputReadback,
                overflowPixels),
            CompactFixtureSampleCount,
            "could not map compact production lighting outputs");
        if (!passed)
            return false;

        uint32_t coveredPixelCount = 0u;
        uint32_t fallbackPixelCount = 0u;
        uint32_t reconstructPixelCount = 0u;
        uint32_t mixedPixelCount = 0u;
        for (uint32_t y = 0u; y < CompactFixtureHeight; ++y)
        {
            for (uint32_t x = 0u; x < CompactFixtureWidth; ++x)
            {
                const uint32_t pixel = y * CompactFixtureWidth + x;
                const std::array<float, 4u>& reference =
                    baselinePixels[pixel];
                const std::array<float, 4u>& candidate =
                    overflowPixels[pixel];
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    passed &= Check(
                        Near(candidate[channel], reference[channel]),
                        CompactFixtureSampleCount,
                        "capacity-1 inline fallback changed a production "
                        "lighting texel");
                }

                std::array<float, 3u> expected{};
                if (x + y < CompactFixtureWidth - 1u)
                {
                    ++fallbackPixelCount;
                    expected = {
                        CompactFallbackEmissiveR,
                        CompactFallbackEmissiveG,
                        CompactFallbackEmissiveB
                    };
                }
                else if (x + y > CompactFixtureWidth - 1u)
                {
                    ++reconstructPixelCount;
                    expected = {
                        CompactEmissiveR,
                        CompactEmissiveG,
                        CompactEmissiveB
                    };
                }
                else
                {
                    ++mixedPixelCount;
                    expected = {
                        0.5f * (CompactEmissiveR +
                            CompactFallbackEmissiveR),
                        0.5f * (CompactEmissiveG +
                            CompactFallbackEmissiveG),
                        0.5f * (CompactEmissiveB +
                            CompactFallbackEmissiveB)
                    };
                }
                ++coveredPixelCount;
                passed &= Check(
                    Near(reference[0], expected[0]) &&
                        Near(reference[1], expected[1]) &&
                        Near(reference[2], expected[2]) &&
                        Near(reference[3], 0.f),
                    CompactFixtureSampleCount,
                    "production mixed compact lighting did not retain exact "
                    "reconstructed, local fallback, and half-weighted edge "
                    "radiance");
            }
        }
        passed &= Check(
            coveredPixelCount == expectedCoveredPixelCount &&
                coveredPixelCount == CompactFixturePixelCount &&
                fallbackPixelCount > 0u &&
                reconstructPixelCount > 0u && mixedPixelCount > 0u,
            CompactFixtureSampleCount,
            "production raster coverage did not match compact diagnostics");
        return passed;
    }

    [[nodiscard]] bool ValidateCompactProductionReadbacks(
        const DiagnosticsGpuReadbackBlock& baseline,
        const DiagnosticsGpuReadbackBlock& overflow,
        const D3D12_FEATURE_DATA_D3D12_OPTIONS1& options)
    {
        bool passed = true;
        const DiagnosticsGpuCounterBlock& reference = baseline.counters;
        const DiagnosticsGpuCounterBlock& candidate = overflow.counters;
        passed &= Check(
            reference.width == CompactFixtureWidth &&
                reference.height == CompactFixtureHeight &&
                reference.sampleCount == CompactFixtureSampleCount &&
                reference.pixelCount == CompactFixturePixelCount &&
                candidate.width == reference.width &&
                candidate.height == reference.height &&
                candidate.sampleCount == reference.sampleCount &&
                candidate.pixelCount == reference.pixelCount,
            CompactFixtureSampleCount,
            "compact diagnostics headers do not describe the 8x8 2x "
            "production fixture");
        passed &= Check(
            reference.validityMask == DiagnosticsCounterAllValidityBits &&
                candidate.validityMask == DiagnosticsCounterAllValidityBits,
            CompactFixtureSampleCount,
            "production compact diagnostics did not publish every counter "
            "family");
        passed &= Check(
            reference.groupCountHistogram ==
                    candidate.groupCountHistogram &&
                reference.referenceLightingEvaluations ==
                    candidate.referenceLightingEvaluations &&
                reference.performedLightingEvaluations ==
                    candidate.performedLightingEvaluations &&
                reference.lightingEvaluationsSaved ==
                    candidate.lightingEvaluationsSaved,
            CompactFixtureSampleCount,
            "queue overflow changed grouping or lighting evaluation "
            "accounting");
        passed &= Check(
            reference.invalidKeyCount == 0u &&
                reference.staleKeyCount == 0u &&
                reference.mappingFailureCount == 0u &&
                candidate.invalidKeyCount == 0u &&
                candidate.staleKeyCount == 0u &&
                candidate.mappingFailureCount == 0u,
            CompactFixtureSampleCount,
            "synthetic production scene produced an invalid compact mapping");

        passed &= Check(
            reference.workListObservationCount > 0u &&
                reference.workListCapacityTotal %
                    reference.workListObservationCount == 0u &&
                reference.workListOverflowCount == 0u,
            CompactFixtureSampleCount,
            "capacity-128 production run did not report a valid separated "
            "wave queue");
        const uint32_t actualWaveCapacity =
            reference.workListObservationCount != 0u
            ? reference.workListCapacityTotal /
                reference.workListObservationCount
            : 0u;
        passed &= Check(
            actualWaveCapacity >= options.WaveLaneCountMin &&
                actualWaveCapacity <= options.WaveLaneCountMax &&
                actualWaveCapacity <= CompactQueueMaximum &&
                reference.workListPeakOccupancy <= actualWaveCapacity,
            CompactFixtureSampleCount,
            "diagnostics did not report the hardware's actual live wave "
            "capacity");
        passed &= Check(
            candidate.workListObservationCount ==
                    reference.workListObservationCount &&
                candidate.workListCapacityTotal ==
                    candidate.workListObservationCount *
                        CompactQueueOverflowCapacity &&
                candidate.workListOccupancyTotal ==
                    candidate.workListObservationCount *
                        CompactQueueOverflowCapacity &&
                candidate.workListPeakOccupancy ==
                    CompactQueueOverflowCapacity &&
                reference.workListOccupancyTotal >
                    candidate.workListOccupancyTotal &&
                reference.workListPeakOccupancy >
                    candidate.workListPeakOccupancy &&
                candidate.workListOverflowCount > 0u &&
                candidate.workListOccupancyTotal > 1u,
            CompactFixtureSampleCount,
            "capacity-1 production run did not force and report separated "
            "queue overflow");

        const uint32_t overflowReason =
            static_cast<uint32_t>(FallbackReason::WorkListOverflow);
        const uint32_t unsafeSharingReason =
            static_cast<uint32_t>(FallbackReason::UnsafeSharing);
        const uint32_t localFallbackReason =
            static_cast<uint32_t>(FallbackReason::UnsupportedMaterial);
        passed &= Check(
            overflowReason < candidate.fallbackReasonCounts.size() &&
                candidate.fallbackReasonCounts[overflowReason] >=
                    CompactFixtureSampleCount &&
                reference.fallbackReasonCounts[overflowReason] == 0u &&
                reference.fallbackReasonCounts[localFallbackReason] > 0u,
            CompactFixtureSampleCount,
            "production sample status did not retain local fallback reason "
            "16 or publish WorkListOverflow (21)");
        for (uint32_t reason = 0u;
            reason < candidate.fallbackReasonCounts.size();
            ++reason)
        {
            if (reason != unsafeSharingReason &&
                reason != localFallbackReason)
            {
                passed &= Check(
                    reference.fallbackReasonCounts[reason] == 0u,
                    CompactFixtureSampleCount,
                    "capacity-128 production run reported an unexpected "
                    "fallback reason");
            }
            if (reason != unsafeSharingReason &&
                reason != localFallbackReason &&
                reason != overflowReason)
            {
                passed &= Check(
                    candidate.fallbackReasonCounts[reason] == 0u,
                    CompactFixtureSampleCount,
                    "capacity-1 production run reported a non-overflow "
                    "failure");
            }
        }
        const uint32_t coveredPixelCount = CompactFixturePixelCount -
            reference.groupCountHistogram[0u];
        uint32_t totalGroupCount = 0u;
        for (uint32_t groupCount = 1u;
            groupCount < reference.groupCountHistogram.size();
            ++groupCount)
        {
            totalGroupCount += groupCount *
                reference.groupCountHistogram[groupCount];
        }
        passed &= Check(
            coveredPixelCount > 2u &&
                reference.workListOccupancyTotal > 1u &&
                reference.workListOccupancyTotal <= totalGroupCount &&
                candidate.workListOccupancyTotal <=
                    reference.workListOccupancyTotal &&
                reference.referenceLightingEvaluations >=
                    coveredPixelCount &&
                reference.performedLightingEvaluations <=
                    reference.referenceLightingEvaluations &&
                reference.lightingEvaluationsSaved ==
                    reference.referenceLightingEvaluations -
                        reference.performedLightingEvaluations,
            CompactFixtureSampleCount,
            "production coverage or lighting-evaluation accounting lost a "
            "covered surface");
        passed &= Check(
            reference.errorBudgetRejectedGroupCount == 0u &&
                reference.errorBudgetRejectedSampleCount == 0u &&
            candidate.errorBudgetRejectedGroupCount == 0u &&
                candidate.errorBudgetRejectedSampleCount == 0u,
            CompactFixtureSampleCount,
            "lossless overflow fallback exceeded the production error "
            "budget");

        const DiagnosticsGpuSelectedHeader& selected = overflow.selected;
        passed &= Check(
            selected.validityMask == DiagnosticsSelectedHeaderValid &&
                (selected.flags &
                    DiagnosticsSelectedHeaderDispatchComplete) != 0u &&
                (selected.flags &
                    DiagnosticsSelectedHeaderViewAvailable) != 0u &&
                selected.pixelX == CompactSelectedPixelX &&
                selected.pixelY == CompactSelectedPixelY &&
                selected.sampleCount == CompactFixtureSampleCount &&
                selected.recordCount == CompactFixtureSampleCount,
            CompactFixtureSampleCount,
            "overflow selected-pixel header is incomplete");
        constexpr uint32_t RequiredSelectedFields =
            DiagnosticsSelectedRecordCovered |
            DiagnosticsSelectedRecordKeySupplied |
            DiagnosticsSelectedRecordKeyValid |
            DiagnosticsSelectedRecordDepthValid |
            DiagnosticsSelectedRecordGroupValid |
            DiagnosticsSelectedRecordFallbackReason |
            DiagnosticsSelectedRecordErrorBudgetReason |
            DiagnosticsSelectedRecordRadiance |
            DiagnosticsSelectedRecordWeightValid;
        constexpr uint32_t ReconstructedSelectedFields =
            DiagnosticsSelectedRecordReconstructedBarycentrics |
            DiagnosticsSelectedRecordFrontFaceValid;
        const DiagnosticsGpuSelectedSampleRecord& firstSelected =
            overflow.selectedSamples[0u];
        bool sawReconstruct = false;
        bool sawLocalFallback = false;
        for (uint32_t sampleIndex = 0u;
            sampleIndex < CompactFixtureSampleCount;
            ++sampleIndex)
        {
            const DiagnosticsGpuSelectedSampleRecord& record =
                overflow.selectedSamples[sampleIndex];
            const float barycentricX =
                FloatFromBits(record.reconstructedBarycentricXBits);
            const float barycentricY =
                FloatFromBits(record.reconstructedBarycentricYBits);
            const bool reconstruct = record.geometryInstanceHandle ==
                FixtureGeometryInstanceHandle;
            const bool localFallback = record.geometryInstanceHandle ==
                CompactFallbackGeometryInstanceHandle;
            sawReconstruct |= reconstruct;
            sawLocalFallback |= localFallback;
            const float expectedR = reconstruct
                ? CompactEmissiveR
                : CompactFallbackEmissiveR;
            const float expectedG = reconstruct
                ? CompactEmissiveG
                : CompactFallbackEmissiveG;
            const float expectedB = reconstruct
                ? CompactEmissiveB
                : CompactFallbackEmissiveB;
            const bool reconstructionFieldsValid = reconstruct
                ? (record.validFields & ReconstructedSelectedFields) ==
                    ReconstructedSelectedFields &&
                    std::isfinite(barycentricX) &&
                    std::isfinite(barycentricY) &&
                    barycentricX >= 0.f && barycentricY >= 0.f &&
                    barycentricX + barycentricY <= 1.0001f
                : (record.validFields & ReconstructedSelectedFields) == 0u;
            passed &= Check(
                (reconstruct || localFallback) &&
                    record.geometryLocalPrimitiveIndex == 0u &&
                    (record.validFields & RequiredSelectedFields) ==
                        RequiredSelectedFields &&
                    Near(
                        FloatFromBits(record.deviceDepthBits),
                        FixtureDepth) &&
                    reconstructionFieldsValid &&
                    record.groupIndex <
                        CompactFixtureSampleCount &&
                    record.representativeSampleIndex == sampleIndex &&
                    record.fallbackReason == overflowReason &&
                    record.errorBudgetReason == static_cast<uint32_t>(
                        DiagnosticsErrorBudgetRejectionReason::None) &&
                    Near(
                        FloatFromBits(record.radianceRBits),
                        expectedR) &&
                    Near(
                        FloatFromBits(record.radianceGBits),
                        expectedG) &&
                    Near(
                        FloatFromBits(record.radianceBBits),
                        expectedB) &&
                    Near(FloatFromBits(record.weightBits), 0.5f),
                CompactFixtureSampleCount,
                "selected mixed sample lost exact identity, provenance-aware "
                "reconstruction fields, singleton grouping, reason 21, or "
                "native radiance during overflow fallback");
        }
        const DiagnosticsGpuSelectedSampleRecord& secondSelected =
            overflow.selectedSamples[1u];
        passed &= Check(
            sawReconstruct && sawLocalFallback &&
                firstSelected.groupIndex != secondSelected.groupIndex,
            CompactFixtureSampleCount,
            "selected diagonal pixel did not contain one reconstruct sample "
            "and one exact local full-G-buffer fallback sample");
        if (!passed)
        {
            const auto DumpCounters = [](const char* label,
                const DiagnosticsGpuCounterBlock& counters)
            {
                std::cerr << "[COMPACT COUNTERS] " << label
                    << " observations=" << counters.workListObservationCount
                    << " capacityTotal=" << counters.workListCapacityTotal
                    << " occupancyTotal=" << counters.workListOccupancyTotal
                    << " peak=" << counters.workListPeakOccupancy
                    << " overflows=" << counters.workListOverflowCount
                    << " referenceEvals="
                    << counters.referenceLightingEvaluations
                    << " performedEvals="
                    << counters.performedLightingEvaluations
                    << " saved=" << counters.lightingEvaluationsSaved
                    << " reason16=" << counters.fallbackReasonCounts[
                        static_cast<uint32_t>(
                            FallbackReason::UnsupportedMaterial)]
                    << " reason21=" << counters.fallbackReasonCounts[
                        static_cast<uint32_t>(
                            FallbackReason::WorkListOverflow)]
                    << " histogram=";
                for (uint32_t count : counters.groupCountHistogram)
                    std::cerr << count << ',';
                std::cerr << '\n';
            };
            DumpCounters("capacity128", reference);
            DumpCounters("capacity1", candidate);
            for (uint32_t sampleIndex = 0u;
                sampleIndex < CompactFixtureSampleCount;
                ++sampleIndex)
            {
                const DiagnosticsGpuSelectedSampleRecord& record =
                    overflow.selectedSamples[sampleIndex];
                std::cerr << "[COMPACT SELECTED] sample=" << sampleIndex
                    << " handle=0x" << std::hex
                    << record.geometryInstanceHandle << std::dec
                    << " primitive="
                    << record.geometryLocalPrimitiveIndex
                    << " fields=0x" << std::hex << record.validFields
                    << std::dec
                    << " depth="
                    << FloatFromBits(record.deviceDepthBits)
                    << " group=" << record.groupIndex
                    << " representative="
                    << record.representativeSampleIndex
                    << " reason=" << record.fallbackReason
                    << " weight=" << FloatFromBits(record.weightBits)
                    << " radiance=("
                    << FloatFromBits(record.radianceRBits) << ','
                    << FloatFromBits(record.radianceGBits) << ','
                    << FloatFromBits(record.radianceBBits) << ')'
                    << " bary=("
                    << FloatFromBits(record.reconstructedBarycentricXBits)
                    << ','
                    << FloatFromBits(record.reconstructedBarycentricYBits)
                    << ")\n";
            }
        }
        return passed;
    }

    enum class AdaptiveGBufferProductionCase : uint32_t
    {
        DistinctKeys = 0u,
        ClearBackground,
        PartialBackground,
        UnsafeSharing,
        InvalidKey,
        Count
    };

    [[nodiscard]] const char* AdaptiveGBufferProductionCaseName(
        AdaptiveGBufferProductionCase testCase)
    {
        switch (testCase)
        {
        case AdaptiveGBufferProductionCase::DistinctKeys:
            return "distinct-keys";
        case AdaptiveGBufferProductionCase::ClearBackground:
            return "clear-background";
        case AdaptiveGBufferProductionCase::PartialBackground:
            return "partial-background";
        case AdaptiveGBufferProductionCase::UnsafeSharing:
            return "unsafe-sharing";
        case AdaptiveGBufferProductionCase::InvalidKey:
            return "invalid-key";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] uint32_t AdaptiveGBufferProducerCapabilityMask(
        AdaptiveGBufferTestTopology topology)
    {
        uint32_t capabilities =
            DiagnosticsCounterFallbackReasonsValid |
            DiagnosticsCounterStaleAndMappingValid |
            DiagnosticsCounterGroupHistogramValid |
            DiagnosticsCounterLightingValid |
            DiagnosticsCounterErrorBudgetValid;
        if (topology == AdaptiveGBufferTestTopology::Separated)
            capabilities |= DiagnosticsCounterWorkListValid;
        return capabilities;
    }

    void RecordAdaptiveGBufferBackgroundMask(
        nvrhi::ICommandList* commandList,
        const CompactProductionFixture& fixture,
        const CompactLightingRun& run)
    {
        // Volatile constant buffers are versioned per command list. The mask
        // draw is recorded after the raster list has completed, so publish the
        // exact view and primary material payload again before binding them.
        commandList->writeBuffer(
            fixture.rasterViewConstantsBuffer,
            &fixture.gbufferConstants,
            sizeof(fixture.gbufferConstants));
        commandList->writeBuffer(
            fixture.fallbackMaterialConstantsBuffer,
            &fixture.primaryFallbackMaterial,
            sizeof(fixture.primaryFallbackMaterial));

        nvrhi::GraphicsState state;
        state.pipeline =
            fixture.adaptiveGBufferBackgroundMaskPipeline;
        state.framebuffer = run.backgroundMaskFramebuffer;
        state.bindings = {
            fixture.fallbackMaterialBindingSet,
            fixture.rasterViewBindingSet,
            fixture.rasterInputBindingSet
        };
        state.viewport = fixture.view.GetViewportState();
        commandList->setGraphicsState(state);

        constexpr uint32_t PositionOffset = 0u;
        constexpr uint32_t TexCoordOffset = 18u * sizeof(float);
        constexpr uint32_t NormalOffset =
            TexCoordOffset + 12u * sizeof(float);
        constexpr uint32_t TangentOffset =
            NormalOffset + 6u * sizeof(uint32_t);
        AdaptiveMsaaGBufferPushConstants push{};
        push.positionOffset = PositionOffset;
        push.prevPositionOffset = PositionOffset;
        push.texCoordOffset = TexCoordOffset;
        push.normalOffset = NormalOffset;
        push.tangentOffset = TangentOffset;
        push.startVertexLocation = 0u;
        push.geometryLocalIndex = 0u;
        push.visibilityClass =
            ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT;
        commandList->setPushConstants(&push, sizeof(push));

        nvrhi::DrawArguments arguments;
        arguments.vertexCount = 3u;
        arguments.instanceCount = 1u;
        commandList->draw(arguments);
    }

    void RecordAdaptiveGBufferLighting(
        nvrhi::ICommandList* commandList,
        CompactProductionFixture& fixture,
        nvrhi::IComputePipeline* pipeline,
        const CompactLightingRun& run,
        bool diagnostics,
        AdaptiveGBufferTestTopology topology,
        uint32_t selectedPixelX,
        uint32_t selectedPixelY,
        const nvrhi::Color& diagnosticsBackground,
        bool maskPartialBackground)
    {
        std::cout << "[GPU RECORD DETAIL] lighting clear-output begin.\n";
        commandList->clearTextureFloat(
            run.output,
            nvrhi::AllSubresources,
            nvrhi::Color(0.f));
        std::cout << "[GPU RECORD DETAIL] lighting constants begin.\n";
        commandList->writeBuffer(
            fixture.deferredConstantsBuffer,
            &fixture.deferredConstants,
            sizeof(fixture.deferredConstants));
        std::cout << "[GPU RECORD DETAIL] lighting constants complete.\n";

        if (diagnostics)
        {
            DiagnosticsProducerGpuConstants diagnosticsConstants{};
            diagnosticsConstants.executionFlags =
                DiagnosticsProducerWriteSampleStatus |
                DiagnosticsProducerWritePixelMetadata |
                DiagnosticsProducerWriteSelectedSamples |
                DiagnosticsProducerValidateErrorBudget;
            diagnosticsConstants.selectedPixelX = selectedPixelX;
            diagnosticsConstants.selectedPixelY = selectedPixelY;
            diagnosticsConstants.producerCapabilityMask =
                AdaptiveGBufferProducerCapabilityMask(topology);
            diagnosticsConstants.viewportWidth = CompactFixtureWidth;
            diagnosticsConstants.viewportHeight = CompactFixtureHeight;

            commandList->clearTextureUInt(
                run.diagnostics.sampleStatus,
                nvrhi::AllSubresources,
                0xffu);
            commandList->clearTextureFloat(
                run.diagnostics.multisampledBackground,
                nvrhi::AllSubresources,
                diagnosticsBackground);
            if (maskPartialBackground)
            {
                RecordAdaptiveGBufferBackgroundMask(
                    commandList,
                    fixture,
                    run);
            }
            commandList->clearBufferUInt(
                run.diagnostics.selectedSampleData,
                0xffffffffu);
            commandList->clearBufferUInt(
                run.diagnostics.pixelMetadata,
                0u);
            commandList->writeBuffer(
                fixture.diagnosticsConstantsBuffer,
                &diagnosticsConstants,
                sizeof(diagnosticsConstants));
        }

        nvrhi::ComputeState state;
        state.pipeline = pipeline;
        state.bindings = { run.bindingSet };
        std::cout << "[GPU RECORD DETAIL] lighting compute-state begin.\n";
        commandList->setComputeState(state);
        std::cout << "[GPU RECORD DETAIL] lighting dispatch begin.\n";
        commandList->dispatch(
            (CompactFixtureWidth + 15u) / 16u,
            (CompactFixtureHeight + 15u) / 16u,
            1u);
        std::cout << "[GPU RECORD DETAIL] lighting dispatch complete.\n";
    }

    void RecordAdaptiveGBufferCaseRaster(
        nvrhi::ICommandList* commandList,
        CompactProductionFixture& fixture,
        AdaptiveGBufferProductionCase testCase)
    {
        CompactFixtureRasterMode rasterMode =
            CompactFixtureRasterMode::AdaptiveGBuffer;
        if (testCase ==
            AdaptiveGBufferProductionCase::PartialBackground)
        {
            rasterMode =
                CompactFixtureRasterMode::AdaptiveGBufferFirstTriangle;
        }
        else if (testCase ==
            AdaptiveGBufferProductionCase::UnsafeSharing)
        {
            rasterMode =
                CompactFixtureRasterMode::AdaptiveGBufferAliasedDepth;
        }
        RecordCompactFixtureRaster(
            commandList,
            fixture,
            rasterMode,
            nullptr,
            testCase == AdaptiveGBufferProductionCase::DistinctKeys ||
                testCase == AdaptiveGBufferProductionCase::UnsafeSharing);

        const nvrhi::Color Background(
            0.5f, 1.f, 2.f, 0.f);
        if (testCase == AdaptiveGBufferProductionCase::ClearBackground)
        {
            commandList->clearTextureUInt(
                fixture.surfaceOwnership,
                nvrhi::AllSubresources,
                ClearedVisibilityWord);
            commandList->clearDepthStencilTexture(
                fixture.depth,
                nvrhi::AllSubresources,
                true,
                0.f,
                false,
                0u);
            commandList->clearTextureFloat(
                fixture.fallbackDiffuse,
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
            commandList->clearTextureFloat(
                fixture.fallbackMaterial,
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
            commandList->clearTextureFloat(
                fixture.fallbackNormals,
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
            commandList->clearTextureFloat(
                fixture.fallbackEmissive,
                nvrhi::AllSubresources,
                nvrhi::Color(0.f));
            commandList->clearTextureFloat(
                fixture.fallbackAmbientOcclusion,
                nvrhi::AllSubresources,
                nvrhi::Color(1.f));
            commandList->clearTextureFloat(
                fixture.resolvedBackground,
                nvrhi::AllSubresources,
                Background);
        }
        else if (testCase ==
            AdaptiveGBufferProductionCase::PartialBackground)
        {
            std::array<donut::math::float16_t4,
                CompactFixturePixelCount> resolvedBackground{};
            for (uint32_t y = 0u; y < CompactFixtureHeight; ++y)
            {
                for (uint32_t x = 0u; x < CompactFixtureWidth; ++x)
                {
                    const uint32_t diagonal = x + y;
                    const float uncoveredWeight =
                        diagonal < CompactFixtureWidth - 1u
                            ? 1.f
                            : diagonal == CompactFixtureWidth - 1u
                                ? 0.5f
                                : 0.f;
                    resolvedBackground[
                        y * CompactFixtureWidth + x] =
                        donut::math::Float32ToFloat16x4(float4(
                            0.5f * uncoveredWeight,
                            1.f * uncoveredWeight,
                            2.f * uncoveredWeight,
                            0.f));
                }
            }
            commandList->writeTexture(
                fixture.resolvedBackground,
                0u,
                0u,
                resolvedBackground.data(),
                CompactFixtureWidth *
                    sizeof(donut::math::float16_t4));
        }
        else if (testCase == AdaptiveGBufferProductionCase::InvalidKey)
        {
            commandList->clearTextureUInt(
                fixture.surfaceOwnership,
                nvrhi::AllSubresources,
                InvalidVisibilityWord);
        }
    }

    [[nodiscard]] bool ExecuteAdaptiveGBufferDiagnosticRun(
        nvrhi::IDevice* device,
        AdaptiveMsaaDiagnosticsGpuPass& diagnosticsPass,
        CompactProductionFixture& fixture,
        nvrhi::IComputePipeline* pipeline,
        CompactLightingRun& run,
        nvrhi::ITexture* referenceOutput,
        AdaptiveGBufferTestTopology topology,
        AdaptiveGBufferProductionCase testCase,
        DiagnosticsGpuReadbackBlock& readback)
    {
        const nvrhi::CommandListHandle commandList =
            device->createCommandList();
        if (!commandList || !pipeline || !referenceOutput)
            return false;

        AdaptiveMsaaDiagnosticsPassRequest request;
        request.settings.enabled = true;
        request.settings.comparisonMode =
            DiagnosticsComparisonMode::ErrorHeatmap;
        request.settings.debugView =
            AdaptiveMsaaDebugView::AdaptiveLightingError;
        request.settings.selectedPixelCaptureRequested = true;
        request.settings.selectedPixelX = CompactSelectedPixelX;
        request.settings.selectedPixelY = CompactSelectedPixelY;
        const uint64_t caseBits = static_cast<uint64_t>(testCase);
        const uint64_t topologyBits = static_cast<uint64_t>(topology);
        request.settings.selectedPixelCaptureId =
            0x4742554600000000ull |
            (uint64_t(fixture.sampleCount) << 16u) |
            (caseBits << 8u) | topologyBits;
        request.settings.readbackEnabled = true;
        request.settings.countersEnabled = true;
        request.settings.dualRenderingEnabled = true;
        request.frameIndex =
            0x4742554601000000ull |
            (uint64_t(fixture.sampleCount) << 16u) |
            (caseBits << 8u) | topologyBits;
        request.visualizedSampleIndex = fixture.sampleCount - 1u;
        request.maximumWorkListCapacity = CompactQueueMaximum;

        AdaptiveMsaaDiagnosticsPassInputs inputs;
        inputs.surfaceKeys = fixture.surfaceOwnership;
        inputs.depth = fixture.depth;
        inputs.output = run.visualization;
        inputs.referenceHdr = referenceOutput;
        inputs.adaptiveHdr = run.output;
        inputs.sampleStatus = run.diagnostics.sampleStatus;
        inputs.selectedSampleData =
            run.diagnostics.selectedSampleData;
        inputs.pixelMetadata = run.diagnostics.pixelMetadata;
        inputs.producerCapabilityMask =
            AdaptiveGBufferProducerCapabilityMask(topology);

        const bool nonzeroDiagnosticsBackground =
            testCase == AdaptiveGBufferProductionCase::ClearBackground ||
            testCase == AdaptiveGBufferProductionCase::PartialBackground;
        const nvrhi::Color diagnosticsBackground =
            nonzeroDiagnosticsBackground
                ? nvrhi::Color(0.5f, 1.f, 2.f, 0.f)
                : nvrhi::Color(0.f);
        const char* topologyName =
            topology == AdaptiveGBufferTestTopology::Inline
                ? "inline"
                : "separated";
        std::cout << "[GPU RECORD] adaptive-GBuffer " << fixture.sampleCount
            << "x " << AdaptiveGBufferProductionCaseName(testCase) << ' '
            << topologyName << " diagnostics lighting begin.\n";
        commandList->open();
        RecordAdaptiveGBufferLighting(
            commandList,
            fixture,
            pipeline,
            run,
            true,
            topology,
            CompactSelectedPixelX,
            CompactSelectedPixelY,
            diagnosticsBackground,
            testCase ==
                AdaptiveGBufferProductionCase::PartialBackground);
        std::cout << "[GPU RECORD] adaptive-GBuffer " << fixture.sampleCount
            << "x " << AdaptiveGBufferProductionCaseName(testCase) << ' '
            << topologyName << " diagnostics producer begin.\n";
        const AdaptiveMsaaDiagnosticsPassResult execution =
            diagnosticsPass.Execute(commandList, inputs, request);
        std::cout << "[GPU RECORD] adaptive-GBuffer " << fixture.sampleCount
            << "x " << AdaptiveGBufferProductionCaseName(testCase) << ' '
            << topologyName << " diagnostics producer status="
            << static_cast<uint32_t>(execution.status) << ".\n";
        if (execution.status != DiagnosticsPassStatus::Success ||
            !execution.dispatched || !execution.viewAvailable ||
            !execution.readbackRecorded || execution.readbackToken == 0u)
        {
            if (execution.readbackToken != 0u)
            {
                (void)diagnosticsPass.CancelUnsubmittedReadback(
                    execution.readbackToken);
            }
            commandList->close();
            return false;
        }
        commandList->copyTexture(
            run.outputReadback,
            nvrhi::TextureSlice(),
            run.output,
            nvrhi::TextureSlice());
        commandList->copyTexture(
            run.visualizationReadback,
            nvrhi::TextureSlice(),
            run.visualization,
            nvrhi::TextureSlice());
        commandList->close();
        std::cout << "[GPU SUBMIT] adaptive-GBuffer " << fixture.sampleCount
            << "x " << AdaptiveGBufferProductionCaseName(testCase) << ' '
            << topologyName << " diagnostics command list.\n";
        device->executeCommandList(commandList);

        const DiagnosticsPassStatus signalStatus =
            diagnosticsPass.SignalReadbackSubmission(
                execution.readbackToken);
        std::cout << "[GPU WAIT] adaptive-GBuffer " << fixture.sampleCount
            << "x " << AdaptiveGBufferProductionCaseName(testCase) << ' '
            << topologyName
            << " diagnostics submitted.\n";
        const bool idle = device->waitForIdle();
        std::cout << "[GPU WAIT] adaptive-GBuffer " << fixture.sampleCount
            << "x " << AdaptiveGBufferProductionCaseName(testCase) << ' '
            << topologyName
            << " diagnostics idle=" << (idle ? 1u : 0u) << ".\n";
        const DiagnosticsReadbackPollResult poll =
            diagnosticsPass.PollReadback(
                request.settings,
                readback);
        return signalStatus == DiagnosticsPassStatus::Success && idle &&
            poll.completed &&
            poll.status == DiagnosticsPassStatus::Success &&
            poll.token == execution.readbackToken;
    }

    [[nodiscard]] bool ValidateAdaptiveGBufferZeroErrorVisualization(
        nvrhi::IDevice* device,
        nvrhi::IStagingTexture* staging,
        uint32_t sampleCount)
    {
        size_t rowPitch = 0u;
        const auto* mapped = static_cast<const uint8_t*>(
            device->mapStagingTexture(
                staging,
                nvrhi::TextureSlice(),
                nvrhi::CpuAccessMode::Read,
                &rowPitch));
        if (!Check(
                mapped != nullptr,
                sampleCount,
                "could not map adaptive-GBuffer error visualization"))
        {
            return false;
        }
        bool passed = true;
        for (uint32_t y = 0u; y < CompactFixtureHeight; ++y)
        {
            const auto* row = reinterpret_cast<const uint16_t*>(
                mapped + size_t(y) * rowPitch);
            for (uint32_t x = 0u; x < CompactFixtureWidth; ++x)
            {
                const uint16_t* texel = row + x * 4u;
                const float red = donut::math::Float16ToFloat32(
                    { texel[0u] });
                const float green = donut::math::Float16ToFloat32(
                    { texel[1u] });
                const float blue = donut::math::Float16ToFloat32(
                    { texel[2u] });
                const float alpha = donut::math::Float16ToFloat32(
                    { texel[3u] });
                passed &= Check(
                    Near(red, 0.f) && Near(green, 0.f) &&
                        Near(blue, 1.f) && Near(alpha, 1.f),
                    sampleCount,
                    "adaptive-GBuffer output differs from Reference");
            }
        }
        device->unmapStagingTexture(staging);
        return passed;
    }

    [[nodiscard]] bool ValidateAdaptiveGBufferOutputs(
        nvrhi::IDevice* device,
        const CompactLightingRun& reference,
        const CompactLightingRun& inlineAccepted,
        const CompactLightingRun& separatedAccepted,
        const CompactLightingRun& inlineDiagnostics,
        const CompactLightingRun& separatedDiagnostics,
        AdaptiveGBufferProductionCase testCase,
        uint32_t sampleCount)
    {
        const std::array<const CompactLightingRun*, 5u> runs = {
            &reference,
            &inlineAccepted,
            &separatedAccepted,
            &inlineDiagnostics,
            &separatedDiagnostics
        };
        std::array<std::vector<std::array<float, 4u>>, 5u> images;
        bool passed = true;
        for (size_t runIndex = 0u; runIndex < runs.size(); ++runIndex)
        {
            passed &= Check(
                ReadCompactLighting(
                    device,
                    runs[runIndex]->outputReadback,
                    images[runIndex]) &&
                    images[runIndex].size() == CompactFixturePixelCount,
                sampleCount,
                "could not read an adaptive-GBuffer production output");
        }
        if (!passed)
            return false;

        for (size_t runIndex = 1u; runIndex < runs.size(); ++runIndex)
        {
            for (uint32_t pixel = 0u;
                pixel < CompactFixturePixelCount;
                ++pixel)
            {
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    passed &= Check(
                        Near(
                            images[runIndex][pixel][channel],
                            images[0u][pixel][channel]),
                        sampleCount,
                        "adaptive-GBuffer topology or diagnostic "
                        "permutation changed a Reference texel");
                }
            }
        }

        const std::array<float, 4u> emissive = {
            CompactEmissiveR,
            CompactEmissiveG,
            CompactEmissiveB,
            0.f
        };
        const std::array<float, 4u> secondaryEmissive = {
            CompactFallbackEmissiveR,
            CompactFallbackEmissiveG,
            CompactFallbackEmissiveB,
            0.f
        };
        const std::array<float, 4u> background = {
            0.5f, 1.f, 2.f, 0.f
        };
        if (testCase == AdaptiveGBufferProductionCase::ClearBackground)
        {
            for (const std::array<float, 4u>& pixel : images[0u])
            {
                for (uint32_t channel = 0u; channel < 4u; ++channel)
                {
                    passed &= Check(
                        Near(pixel[channel], background[channel]),
                        sampleCount,
                        "Reference output did not retain the uncovered "
                        "background value");
                }
            }
        }
        else if (testCase ==
            AdaptiveGBufferProductionCase::PartialBackground)
        {
            for (uint32_t y = 0u; y < CompactFixtureHeight; ++y)
            {
                for (uint32_t x = 0u; x < CompactFixtureWidth; ++x)
                {
                    const uint32_t diagonal = x + y;
                    const std::array<float, 4u> expected =
                        diagonal < CompactFixtureWidth - 1u
                            ? background
                            : diagonal > CompactFixtureWidth - 1u
                                ? emissive
                                : std::array<float, 4u>{
                                    0.5f * (emissive[0u] +
                                        background[0u]),
                                    0.5f * (emissive[1u] +
                                        background[1u]),
                                    0.5f * (emissive[2u] +
                                        background[2u]),
                                    0.f
                                };
                    const uint32_t pixelIndex =
                        y * CompactFixtureWidth + x;
                    for (uint32_t channel = 0u;
                        channel < 4u;
                        ++channel)
                    {
                        passed &= Check(
                            Near(
                                images[0u][pixelIndex][channel],
                                expected[channel]),
                            sampleCount,
                            "partial-coverage Reference output did not "
                            "preserve resolved background and weighted "
                            "surface radiance");
                    }
                }
            }
        }
        else
        {
            const bool distinctSurfaceRadiance =
                testCase == AdaptiveGBufferProductionCase::DistinctKeys ||
                testCase == AdaptiveGBufferProductionCase::UnsafeSharing;
            for (uint32_t y = 0u; y < CompactFixtureHeight; ++y)
            {
                for (uint32_t x = 0u; x < CompactFixtureWidth; ++x)
                {
                    const uint32_t diagonal = x + y;
                    std::array<float, 4u> expected = emissive;
                    if (distinctSurfaceRadiance)
                    {
                        expected = diagonal > CompactFixtureWidth - 1u
                            ? emissive
                            : diagonal < CompactFixtureWidth - 1u
                                ? secondaryEmissive
                                : std::array<float, 4u>{
                                    0.5f * (emissive[0u] +
                                        secondaryEmissive[0u]),
                                    0.5f * (emissive[1u] +
                                        secondaryEmissive[1u]),
                                    0.5f * (emissive[2u] +
                                        secondaryEmissive[2u]),
                                    0.f
                                };
                    }
                    const uint32_t pixelIndex =
                        y * CompactFixtureWidth + x;
                    for (uint32_t channel = 0u;
                        channel < 4u;
                        ++channel)
                    {
                        passed &= Check(
                            Near(
                                images[0u][pixelIndex][channel],
                                expected[channel]),
                            sampleCount,
                            diagonal == CompactFixtureWidth - 1u &&
                                    distinctSurfaceRadiance
                                ? "Reference diagonal did not preserve the "
                                    "sample-weighted two-surface radiance"
                                : "Reference output did not retain its "
                                    "surface-owned emissive radiance");
                    }
                }
            }
        }

        passed &= ValidateAdaptiveGBufferZeroErrorVisualization(
            device,
            inlineDiagnostics.visualizationReadback,
            sampleCount);
        passed &= ValidateAdaptiveGBufferZeroErrorVisualization(
            device,
            separatedDiagnostics.visualizationReadback,
            sampleCount);
        return passed;
    }

    [[nodiscard]] bool ValidateAdaptiveGBufferSelectedSamples(
        const DiagnosticsGpuReadbackBlock& inlineReadback,
        const DiagnosticsGpuReadbackBlock& separatedReadback,
        AdaptiveGBufferProductionCase testCase,
        uint32_t sampleCount)
    {
        bool passed = true;
        const DiagnosticsGpuSelectedHeader& header =
            separatedReadback.selected;
        const DiagnosticsGpuSelectedHeader& inlineHeader =
            inlineReadback.selected;
        passed &= Check(
            header.validityMask == DiagnosticsSelectedHeaderValid &&
                (header.flags &
                    DiagnosticsSelectedHeaderDispatchComplete) != 0u &&
                (header.flags &
                    DiagnosticsSelectedHeaderViewAvailable) != 0u &&
                header.pixelX == CompactSelectedPixelX &&
                header.pixelY == CompactSelectedPixelY &&
                header.sampleCount == sampleCount &&
                header.recordCount == sampleCount,
            sampleCount,
            "adaptive-GBuffer selected-pixel header is incomplete");
        passed &= Check(
            inlineHeader.validityMask == header.validityMask &&
                inlineHeader.flags == header.flags &&
                inlineHeader.pixelX == header.pixelX &&
                inlineHeader.pixelY == header.pixelY &&
                inlineHeader.sampleCount == header.sampleCount &&
                inlineHeader.recordCount == header.recordCount &&
                inlineHeader.requestedView == header.requestedView &&
                inlineHeader.visualizedSampleIndex ==
                    header.visualizedSampleIndex,
            sampleCount,
            "inline and separated selected-pixel headers differ");

        uint32_t coveredSampleCount = 0u;
        uint32_t validKeySampleCount = 0u;
        uint32_t groupCount = 0u;
        uint32_t unsafeSharingCount = 0u;
        std::array<bool, MaximumAdaptiveSampleCount> observedGroups{};
        const uint32_t firstDepthBits =
            separatedReadback.selectedSamples[0u].deviceDepthBits;
        uint32_t firstDepthCount = 0u;
        uint32_t otherDepthCount = 0u;
        uint32_t distinctKeyCount = 0u;
        std::array<uint2, MaximumAdaptiveSampleCount> distinctKeys{};

        for (uint32_t sampleIndex = 0u;
            sampleIndex < sampleCount;
            ++sampleIndex)
        {
            const DiagnosticsGpuSelectedSampleRecord& record =
                separatedReadback.selectedSamples[sampleIndex];
            const DiagnosticsGpuSelectedSampleRecord& inlineRecord =
                inlineReadback.selectedSamples[sampleIndex];
            const bool covered = (record.validFields &
                DiagnosticsSelectedRecordCovered) != 0u;
            const bool keyValid = (record.validFields &
                DiagnosticsSelectedRecordKeyValid) != 0u;
            const bool groupValid = (record.validFields &
                DiagnosticsSelectedRecordGroupValid) != 0u;
            coveredSampleCount += covered ? 1u : 0u;
            validKeySampleCount += keyValid ? 1u : 0u;
            unsafeSharingCount += record.fallbackReason ==
                    static_cast<uint32_t>(FallbackReason::UnsafeSharing)
                ? 1u
                : 0u;
            firstDepthCount += record.deviceDepthBits == firstDepthBits
                ? 1u
                : 0u;
            otherDepthCount += record.deviceDepthBits != firstDepthBits
                ? 1u
                : 0u;

            passed &= Check(
                (record.validFields &
                    DiagnosticsSelectedRecordKeySupplied) != 0u &&
                    (record.validFields &
                        DiagnosticsSelectedRecordDepthValid) != 0u &&
                    (record.validFields &
                        DiagnosticsSelectedRecordFallbackReason) != 0u &&
                    (record.validFields &
                        DiagnosticsSelectedRecordRadiance) != 0u &&
                    (record.validFields &
                        DiagnosticsSelectedRecordWeightValid) != 0u,
                sampleCount,
                "adaptive-GBuffer selected sample omitted a required producer field");
            bool expectedIdentity = false;
            switch (testCase)
            {
            case AdaptiveGBufferProductionCase::DistinctKeys:
                expectedIdentity = covered &&
                    record.geometryLocalPrimitiveIndex == 0u &&
                    (record.geometryInstanceHandle ==
                            FixtureGeometryInstanceHandle ||
                        record.geometryInstanceHandle ==
                            CompactFallbackGeometryInstanceHandle);
                break;
            case AdaptiveGBufferProductionCase::ClearBackground:
                expectedIdentity = !covered &&
                    record.geometryInstanceHandle ==
                        ClearedVisibilityWord &&
                    record.geometryLocalPrimitiveIndex ==
                        ClearedVisibilityWord;
                break;
            case AdaptiveGBufferProductionCase::PartialBackground:
                expectedIdentity = covered
                    ? record.geometryInstanceHandle ==
                            FixtureGeometryInstanceHandle &&
                        record.geometryLocalPrimitiveIndex == 0u
                    : record.geometryInstanceHandle ==
                            ClearedVisibilityWord &&
                        record.geometryLocalPrimitiveIndex ==
                            ClearedVisibilityWord;
                break;
            case AdaptiveGBufferProductionCase::UnsafeSharing:
                expectedIdentity = covered &&
                    record.geometryInstanceHandle ==
                        FixtureGeometryInstanceHandle &&
                    record.geometryLocalPrimitiveIndex == 0u;
                break;
            case AdaptiveGBufferProductionCase::InvalidKey:
                expectedIdentity = covered &&
                    record.geometryInstanceHandle ==
                        InvalidVisibilityWord &&
                    record.geometryLocalPrimitiveIndex ==
                        InvalidVisibilityWord;
                break;
            default:
                break;
            }
            passed &= Check(
                expectedIdentity,
                sampleCount,
                "adaptive-GBuffer raster did not preserve the exact final key");
            passed &= Check(
                inlineRecord.geometryInstanceHandle ==
                        record.geometryInstanceHandle &&
                    inlineRecord.geometryLocalPrimitiveIndex ==
                        record.geometryLocalPrimitiveIndex &&
                    inlineRecord.deviceDepthBits == record.deviceDepthBits &&
                    inlineRecord.validFields == record.validFields &&
                    inlineRecord.groupIndex == record.groupIndex &&
                    inlineRecord.representativeSampleIndex ==
                        record.representativeSampleIndex &&
                    inlineRecord.fallbackReason == record.fallbackReason &&
                    inlineRecord.errorBudgetReason ==
                        record.errorBudgetReason &&
                    inlineRecord.weightBits == record.weightBits &&
                    inlineRecord.radianceRBits == record.radianceRBits &&
                    inlineRecord.radianceGBits == record.radianceGBits &&
                    inlineRecord.radianceBBits == record.radianceBBits,
                sampleCount,
                "inline and separated selected-sample records differ");

            if (keyValid)
            {
                const uint2 key = {
                    record.geometryInstanceHandle,
                    record.geometryLocalPrimitiveIndex
                };
                bool found = false;
                for (uint32_t keyIndex = 0u;
                    keyIndex < distinctKeyCount;
                    ++keyIndex)
                {
                    found |= distinctKeys[keyIndex].x == key.x &&
                        distinctKeys[keyIndex].y == key.y;
                }
                if (!found && distinctKeyCount < distinctKeys.size())
                    distinctKeys[distinctKeyCount++] = key;
            }

            if (groupValid)
            {
                passed &= Check(
                    record.groupIndex < sampleCount &&
                        record.representativeSampleIndex < sampleCount,
                    sampleCount,
                    "selected sample published an out-of-range group");
                if (record.groupIndex < observedGroups.size() &&
                    !observedGroups[record.groupIndex])
                {
                    observedGroups[record.groupIndex] = true;
                    ++groupCount;
                }
                uint32_t memberCount = 0u;
                uint32_t minimumMember = sampleCount;
                for (uint32_t memberIndex = 0u;
                    memberIndex < sampleCount;
                    ++memberIndex)
                {
                    const DiagnosticsGpuSelectedSampleRecord& member =
                        separatedReadback.selectedSamples[memberIndex];
                    if ((member.validFields &
                            DiagnosticsSelectedRecordGroupValid) != 0u &&
                        member.groupIndex == record.groupIndex)
                    {
                        ++memberCount;
                        minimumMember = std::min(
                            minimumMember,
                            memberIndex);
                    }
                }
                passed &= Check(
                    memberCount > 0u &&
                        record.representativeSampleIndex == minimumMember &&
                        Near(
                            FloatFromBits(record.weightBits),
                            float(memberCount) / float(sampleCount)),
                    sampleCount,
                    "selected group representative or sample-count weight is not deterministic");
            }
            else
            {
                passed &= Check(
                    !covered &&
                        Near(
                            FloatFromBits(record.weightBits),
                            1.f / float(sampleCount)),
                    sampleCount,
                    "uncovered selected sample claimed a group or invalid weight");
            }

            const bool backgroundSample = !covered;
            const bool nonzeroBackgroundSample =
                testCase == AdaptiveGBufferProductionCase::ClearBackground ||
                (testCase ==
                        AdaptiveGBufferProductionCase::PartialBackground &&
                    backgroundSample);
            const bool secondarySurfaceSample = covered &&
                ((testCase == AdaptiveGBufferProductionCase::DistinctKeys &&
                    record.geometryInstanceHandle ==
                        CompactFallbackGeometryInstanceHandle) ||
                    (testCase ==
                            AdaptiveGBufferProductionCase::UnsafeSharing &&
                        Near(
                            FloatFromBits(record.deviceDepthBits),
                            0.375f)));
            const float expectedRadianceR =
                nonzeroBackgroundSample
                    ? 0.5f
                    : backgroundSample
                        ? 0.f
                        : secondarySurfaceSample
                            ? CompactFallbackEmissiveR
                            : CompactEmissiveR;
            const float expectedRadianceG =
                nonzeroBackgroundSample
                    ? 1.f
                    : backgroundSample
                        ? 0.f
                        : secondarySurfaceSample
                            ? CompactFallbackEmissiveG
                            : CompactEmissiveG;
            const float expectedRadianceB =
                nonzeroBackgroundSample
                    ? 2.f
                    : backgroundSample
                        ? 0.f
                        : secondarySurfaceSample
                            ? CompactFallbackEmissiveB
                            : CompactEmissiveB;
            passed &= Check(
                Near(
                    FloatFromBits(record.radianceRBits),
                    expectedRadianceR) &&
                Near(
                    FloatFromBits(record.radianceGBits),
                    expectedRadianceG) &&
                Near(
                    FloatFromBits(record.radianceBBits),
                    expectedRadianceB),
                sampleCount,
                "selected sample radiance does not match its surface/background owner");
        }

        switch (testCase)
        {
        case AdaptiveGBufferProductionCase::DistinctKeys:
            passed &= Check(
                coveredSampleCount == sampleCount &&
                    validKeySampleCount == sampleCount &&
                    groupCount == 2u && distinctKeyCount == 2u &&
                    unsafeSharingCount == 0u,
                sampleCount,
                "diagonal pixel did not retain two exact-key shading groups");
            break;
        case AdaptiveGBufferProductionCase::ClearBackground:
            passed &= Check(
                coveredSampleCount == 0u &&
                    validKeySampleCount == 0u && groupCount == 0u &&
                    unsafeSharingCount == 0u,
                sampleCount,
                "clear background pixel claimed a covered group");
            break;
        case AdaptiveGBufferProductionCase::PartialBackground:
            passed &= Check(
                coveredSampleCount == sampleCount / 2u &&
                    validKeySampleCount == coveredSampleCount &&
                    groupCount == 1u && distinctKeyCount == 1u &&
                    unsafeSharingCount == 0u,
                sampleCount,
                "partial background pixel did not retain half coverage and one exact group");
            break;
        case AdaptiveGBufferProductionCase::UnsafeSharing:
            passed &= Check(
                coveredSampleCount == sampleCount &&
                    validKeySampleCount == sampleCount &&
                    distinctKeyCount == 1u &&
                    firstDepthCount == sampleCount / 2u &&
                    otherDepthCount == sampleCount / 2u &&
                    groupCount == sampleCount / 2u + 1u &&
                    unsafeSharingCount == sampleCount / 2u,
                sampleCount,
                "equal SurfaceKeys crossed the conservative depth/position sharing boundary");
            break;
        case AdaptiveGBufferProductionCase::InvalidKey:
            passed &= Check(
                coveredSampleCount == sampleCount &&
                    validKeySampleCount == 0u &&
                    groupCount == sampleCount &&
                    unsafeSharingCount == 0u,
                sampleCount,
                "invalid covered keys did not become Reference-equivalent singletons");
            for (uint32_t sampleIndex = 0u;
                sampleIndex < sampleCount;
                ++sampleIndex)
            {
                const DiagnosticsGpuSelectedSampleRecord& record =
                    separatedReadback.selectedSamples[sampleIndex];
                passed &= Check(
                    record.fallbackReason == static_cast<uint32_t>(
                        FallbackReason::InvalidSentinel) &&
                        record.representativeSampleIndex == sampleIndex &&
                        Near(
                            FloatFromBits(record.weightBits),
                            1.f / float(sampleCount)),
                    sampleCount,
                    "invalid key fallback lost singleton identity or weight");
            }
            break;
        default:
            passed = false;
            break;
        }
        return passed;
    }

    [[nodiscard]] bool ValidateAdaptiveGBufferCounters(
        const DiagnosticsGpuReadbackBlock& inlineReadback,
        const DiagnosticsGpuReadbackBlock& separatedReadback,
        const D3D12_FEATURE_DATA_D3D12_OPTIONS1& options,
        AdaptiveGBufferProductionCase testCase,
        uint32_t sampleCount)
    {
        bool passed = true;
        const DiagnosticsGpuCounterBlock& inlineCounters =
            inlineReadback.counters;
        const DiagnosticsGpuCounterBlock& separatedCounters =
            separatedReadback.counters;
        const uint32_t inlineValidity =
            DiagnosticsCounterAllValidityBits &
            ~DiagnosticsCounterWorkListValid;
        passed &= Check(
            inlineCounters.width == CompactFixtureWidth &&
                inlineCounters.height == CompactFixtureHeight &&
                inlineCounters.sampleCount == sampleCount &&
                inlineCounters.pixelCount == CompactFixturePixelCount &&
                separatedCounters.width == inlineCounters.width &&
                separatedCounters.height == inlineCounters.height &&
                separatedCounters.sampleCount == sampleCount &&
                separatedCounters.pixelCount == CompactFixturePixelCount &&
                inlineCounters.validityMask == inlineValidity &&
                separatedCounters.validityMask ==
                    DiagnosticsCounterAllValidityBits,
            sampleCount,
            "adaptive-GBuffer diagnostics header or validity mask mismatch");
        passed &= Check(
            (inlineCounters.flags &
                DiagnosticsCounterDispatchComplete) != 0u &&
                (inlineCounters.flags &
                    DiagnosticsCounterViewAvailable) != 0u &&
                (separatedCounters.flags &
                    DiagnosticsCounterDispatchComplete) != 0u &&
                (separatedCounters.flags &
                    DiagnosticsCounterViewAvailable) != 0u,
            sampleCount,
            "adaptive-GBuffer diagnostics completion/view flags are missing");
        passed &= Check(
            inlineCounters.groupCountHistogram ==
                    separatedCounters.groupCountHistogram &&
                inlineCounters.referenceLightingEvaluations ==
                    separatedCounters.referenceLightingEvaluations &&
                inlineCounters.performedLightingEvaluations ==
                    separatedCounters.performedLightingEvaluations &&
                inlineCounters.lightingEvaluationsSaved ==
                    separatedCounters.lightingEvaluationsSaved &&
                inlineCounters.fallbackReasonCounts ==
                    separatedCounters.fallbackReasonCounts &&
                inlineCounters.invalidKeyCount ==
                    separatedCounters.invalidKeyCount &&
                inlineCounters.staleKeyCount ==
                    separatedCounters.staleKeyCount &&
                inlineCounters.mappingFailureCount ==
                    separatedCounters.mappingFailureCount &&
                inlineCounters.errorBudgetRejectionReasonCounts ==
                    separatedCounters.errorBudgetRejectionReasonCounts &&
                inlineCounters.errorBudgetRejectedGroupCount ==
                    separatedCounters.errorBudgetRejectedGroupCount &&
                inlineCounters.errorBudgetRejectedSampleCount ==
                    separatedCounters.errorBudgetRejectedSampleCount,
            sampleCount,
            "inline and separated adaptive-GBuffer accounting differs");
        passed &= Check(
            inlineCounters.workListObservationCount == 0u &&
                inlineCounters.workListOccupancyTotal == 0u &&
                inlineCounters.workListCapacityTotal == 0u &&
                inlineCounters.workListPeakOccupancy == 0u &&
                inlineCounters.workListOverflowCount == 0u,
            sampleCount,
            "inline adaptive-GBuffer topology published work-list counters");

        const bool allBackground =
            testCase == AdaptiveGBufferProductionCase::ClearBackground;
        if (allBackground)
        {
            passed &= Check(
                separatedCounters.workListObservationCount == 0u &&
                    separatedCounters.workListOccupancyTotal == 0u &&
                    separatedCounters.workListCapacityTotal == 0u &&
                    separatedCounters.workListPeakOccupancy == 0u &&
                    separatedCounters.workListOverflowCount == 0u,
                sampleCount,
                "all-background separated topology fabricated work-list work");
        }
        else
        {
            passed &= Check(
                separatedCounters.workListObservationCount > 0u &&
                    separatedCounters.workListCapacityTotal > 0u &&
                    separatedCounters.workListOccupancyTotal <=
                        separatedCounters.workListCapacityTotal &&
                    separatedCounters.workListPeakOccupancy > 0u &&
                    separatedCounters.workListPeakOccupancy <=
                        options.WaveLaneCountMax &&
                    separatedCounters.workListPeakOccupancy <=
                        CompactQueueMaximum &&
                    separatedCounters.workListOverflowCount == 0u,
                sampleCount,
                "separated adaptive-GBuffer wave queue overflowed or reported an invalid capacity");
        }

        uint32_t histogramPixelCount = 0u;
        uint32_t histogramGroupCount = 0u;
        for (uint32_t groupCount = 0u;
            groupCount < inlineCounters.groupCountHistogram.size();
            ++groupCount)
        {
            histogramPixelCount +=
                inlineCounters.groupCountHistogram[groupCount];
            histogramGroupCount += groupCount *
                inlineCounters.groupCountHistogram[groupCount];
        }
        passed &= Check(
            histogramPixelCount == CompactFixturePixelCount &&
                histogramGroupCount ==
                    inlineCounters.performedLightingEvaluations &&
                inlineCounters.performedLightingEvaluations <=
                    inlineCounters.referenceLightingEvaluations &&
                inlineCounters.lightingEvaluationsSaved ==
                    inlineCounters.referenceLightingEvaluations -
                        inlineCounters.performedLightingEvaluations,
            sampleCount,
            "adaptive-GBuffer group histogram and lighting savings disagree");
        passed &= Check(
            inlineCounters.errorBudgetRejectedGroupCount == 0u &&
                inlineCounters.errorBudgetRejectedSampleCount == 0u &&
                std::all_of(
                    inlineCounters.errorBudgetRejectionReasonCounts.begin(),
                    inlineCounters.errorBudgetRejectionReasonCounts.end(),
                    [](uint32_t count) { return count == 0u; }),
            sampleCount,
            "lossless adaptive-GBuffer fixture exceeded its error budget");

        const uint32_t fullReferenceEvaluations =
            CompactFixturePixelCount * sampleCount;
        switch (testCase)
        {
        case AdaptiveGBufferProductionCase::DistinctKeys:
            passed &= Check(
                inlineCounters.groupCountHistogram[0u] == 0u &&
                    inlineCounters.groupCountHistogram[1u] > 0u &&
                    inlineCounters.groupCountHistogram[2u] > 0u &&
                    inlineCounters.referenceLightingEvaluations ==
                        fullReferenceEvaluations &&
                    inlineCounters.invalidKeyCount == 0u,
                sampleCount,
                "distinct-key production raster did not expose simple and edge groups");
            break;
        case AdaptiveGBufferProductionCase::ClearBackground:
            passed &= Check(
                inlineCounters.groupCountHistogram[0u] ==
                        CompactFixturePixelCount &&
                    inlineCounters.referenceLightingEvaluations == 0u &&
                    inlineCounters.performedLightingEvaluations == 0u &&
                    inlineCounters.invalidKeyCount == 0u,
                sampleCount,
                "clear background produced covered adaptive work");
            break;
        case AdaptiveGBufferProductionCase::PartialBackground:
            passed &= Check(
                inlineCounters.groupCountHistogram[0u] > 0u &&
                    inlineCounters.groupCountHistogram[1u] > 0u &&
                    inlineCounters.referenceLightingEvaluations >
                        inlineCounters.performedLightingEvaluations &&
                    inlineCounters.referenceLightingEvaluations <
                        fullReferenceEvaluations &&
                    inlineCounters.invalidKeyCount == 0u,
                sampleCount,
                "partial background did not preserve uncovered and grouped covered pixels");
            break;
        case AdaptiveGBufferProductionCase::UnsafeSharing:
            passed &= Check(
                inlineCounters.groupCountHistogram[0u] == 0u &&
                    inlineCounters.groupCountHistogram[1u] > 0u &&
                    inlineCounters.referenceLightingEvaluations ==
                        fullReferenceEvaluations &&
                    inlineCounters.fallbackReasonCounts[
                        static_cast<uint32_t>(
                            FallbackReason::UnsafeSharing)] > 0u &&
                    inlineCounters.invalidKeyCount == 0u,
                sampleCount,
                "unsafe equal-key surfaces were incorrectly shared");
            break;
        case AdaptiveGBufferProductionCase::InvalidKey:
            passed &= Check(
                inlineCounters.groupCountHistogram[sampleCount] ==
                        CompactFixturePixelCount &&
                    inlineCounters.referenceLightingEvaluations ==
                        fullReferenceEvaluations &&
                    inlineCounters.performedLightingEvaluations ==
                        fullReferenceEvaluations &&
                    inlineCounters.invalidKeyCount ==
                        fullReferenceEvaluations &&
                    inlineCounters.fallbackReasonCounts[
                        static_cast<uint32_t>(
                            FallbackReason::InvalidSentinel)] ==
                        fullReferenceEvaluations,
                sampleCount,
                "invalid covered keys did not force complete Reference shading");
            break;
        default:
            passed = false;
            break;
        }
        return passed;
    }

    [[nodiscard]] bool RunAdaptiveGBufferProductionCase(
        nvrhi::IDevice* device,
        AdaptiveMsaaDiagnosticsGpuPass& diagnosticsPass,
        CompactProductionFixture& fixture,
        const AdaptiveGBufferProductionPipelines& pipelines,
        CompactLightingRun& reference,
        std::array<CompactLightingRun, 2u>& accepted,
        std::array<CompactLightingRun, 2u>& diagnostic,
        const D3D12_FEATURE_DATA_D3D12_OPTIONS1& options,
        AdaptiveGBufferProductionCase testCase)
    {
        const auto ExecuteRecordedList = [&](const char* label,
            auto&& record)
        {
            const nvrhi::CommandListHandle commandList =
                device->createCommandList();
            if (!commandList)
                return false;
            commandList->open();
            record(commandList);
            commandList->close();
            std::cout << "[GPU WAIT] adaptive-GBuffer "
                << fixture.sampleCount << "x "
                << AdaptiveGBufferProductionCaseName(testCase) << ' '
                << label << " submitted.\n";
            device->executeCommandList(commandList);
            const bool idle = device->waitForIdle();
            std::cout << "[GPU WAIT] adaptive-GBuffer "
                << fixture.sampleCount << "x "
                << AdaptiveGBufferProductionCaseName(testCase) << ' '
                << label << " idle=" << (idle ? 1u : 0u) << ".\n";
            return idle;
        };

        std::cout << "[GPU RECORD] adaptive-GBuffer " << fixture.sampleCount
            << "x " << AdaptiveGBufferProductionCaseName(testCase)
            << " raster begin.\n";
        if (!ExecuteRecordedList(
                "raster",
                [&](nvrhi::ICommandList* commandList)
                {
                    RecordAdaptiveGBufferCaseRaster(
                        commandList,
                        fixture,
                        testCase);
                }))
        {
            return false;
        }

        const auto ExecuteLighting = [&] (
            const char* label,
            nvrhi::IComputePipeline* pipeline,
            CompactLightingRun& run,
            AdaptiveGBufferTestTopology topology)
        {
            return ExecuteRecordedList(
                label,
                [&](nvrhi::ICommandList* commandList)
                {
                    RecordAdaptiveGBufferLighting(
                        commandList,
                        fixture,
                        pipeline,
                        run,
                        false,
                        topology,
                        CompactSelectedPixelX,
                        CompactSelectedPixelY,
                        nvrhi::Color(0.f),
                        false);
                    commandList->copyTexture(
                        run.outputReadback,
                        nvrhi::TextureSlice(),
                        run.output,
                        nvrhi::TextureSlice());
                });
        };
        if (!ExecuteLighting(
                "reference",
                pipelines.reference,
                reference,
                AdaptiveGBufferTestTopology::Inline))
        {
            return false;
        }
        for (uint32_t topologyIndex = 0u;
            topologyIndex < accepted.size();
            ++topologyIndex)
        {
            if (!ExecuteLighting(
                    topologyIndex == 0u
                        ? "inline accepted"
                        : "separated accepted",
                    pipelines.adaptive[topologyIndex],
                    accepted[topologyIndex],
                    static_cast<AdaptiveGBufferTestTopology>(topologyIndex)))
            {
                return false;
            }
        }

        std::array<DiagnosticsGpuReadbackBlock, 2u> readbacks;
        for (uint32_t topologyIndex = 0u;
            topologyIndex < diagnostic.size();
            ++topologyIndex)
        {
            if (!ExecuteAdaptiveGBufferDiagnosticRun(
                    device,
                    diagnosticsPass,
                    fixture,
                    pipelines.adaptiveDiagnostics[topologyIndex],
                    diagnostic[topologyIndex],
                    reference.output,
                    static_cast<AdaptiveGBufferTestTopology>(topologyIndex),
                    testCase,
                    readbacks[topologyIndex]))
            {
                return false;
            }
        }

        bool passed = ValidateAdaptiveGBufferOutputs(
            device,
            reference,
            accepted[0u],
            accepted[1u],
            diagnostic[0u],
            diagnostic[1u],
            testCase,
            fixture.sampleCount);
        passed &= ValidateAdaptiveGBufferCounters(
            readbacks[0u],
            readbacks[1u],
            options,
            testCase,
            fixture.sampleCount);
        passed &= ValidateAdaptiveGBufferSelectedSamples(
            readbacks[0u],
            readbacks[1u],
            testCase,
            fixture.sampleCount);
        passed &= Check(
            diagnosticsPass.OutstandingReadbackCount() == 0u,
            fixture.sampleCount,
            "adaptive-GBuffer diagnostics retained a completed readback token");
        return passed;
    }

    [[nodiscard]] bool RunAdaptiveGBufferProductionShaderTest(
        nvrhi::IDevice* device,
        donut::engine::ShaderFactory& shaderFactory,
        donut::engine::ShaderFactory& testShaderFactory,
        AdaptiveMsaaDiagnosticsGpuPass& diagnosticsPass,
        ValidationMessageCallback& validationMessages,
        const D3D12_FEATURE_DATA_D3D12_OPTIONS1& options,
        uint32_t sampleCount)
    {
        const uint32_t validationErrorsBefore =
            validationMessages.ErrorCount();
        ClearD3D12InfoQueue(device);
        bool passed = Check(
            IsSupportedSampleCount(sampleCount) &&
                options.WaveOps &&
                options.WaveLaneCountMin > 0u &&
                options.WaveLaneCountMax >= options.WaveLaneCountMin,
            sampleCount,
            "adapter lacks the adaptive-GBuffer shader's wave requirement");
        if (!passed)
            return false;

        CompactProductionFixture fixture;
        AdaptiveGBufferProductionPipelines pipelines;
        passed &= Check(
            CreateCompactProductionFixture(
                device,
                shaderFactory,
                testShaderFactory,
                fixture,
                sampleCount,
                ProductionFixtureScope::AdaptiveGBuffer) &&
                CreateAdaptiveGBufferProductionPipelines(
                    device,
                    shaderFactory,
                    sampleCount,
                    pipelines),
            sampleCount,
            "could not create exact adaptive-GBuffer production fixture/pipelines");
        if (!passed)
            return false;

        const auto NoTexturesCreated = [](const auto& textures)
        {
            return std::none_of(
                textures.begin(),
                textures.end(),
                [](const nvrhi::TextureHandle& texture)
                {
                    return bool(texture);
                });
        };
        const auto NoStagingTexturesCreated = [](const auto& textures)
        {
            return std::none_of(
                textures.begin(),
                textures.end(),
                [](const nvrhi::StagingTextureHandle& texture)
                {
                    return bool(texture);
                });
        };
        const nvrhi::FramebufferDesc& adaptiveFramebuffer =
            fixture.adaptiveGBufferFramebuffer->getDesc();
        passed &= Check(
            fixture.scope == ProductionFixtureScope::AdaptiveGBuffer &&
                adaptiveFramebuffer.colorAttachments.size() == 7u &&
                adaptiveFramebuffer.colorAttachments[0u].texture ==
                    fixture.fallbackDiffuse &&
                adaptiveFramebuffer.colorAttachments[1u].texture ==
                    fixture.fallbackMaterial &&
                adaptiveFramebuffer.colorAttachments[2u].texture ==
                    fixture.fallbackNormals &&
                adaptiveFramebuffer.colorAttachments[3u].texture ==
                    fixture.fallbackEmissive &&
                adaptiveFramebuffer.colorAttachments[4u].texture ==
                    fixture.fallbackAmbientOcclusion &&
                adaptiveFramebuffer.colorAttachments[5u].texture ==
                    fixture.fallbackMotionVectors &&
                adaptiveFramebuffer.colorAttachments[6u].texture ==
                    fixture.surfaceOwnership &&
                adaptiveFramebuffer.depthAttachment.texture == fixture.depth &&
                fixture.depth->getDesc().format ==
                    AdaptiveGBufferProductDepthFormat &&
                fixture.adaptiveGBufferFramebuffer->getFramebufferInfo()
                        .depthFormat == AdaptiveGBufferProductDepthFormat,
            sampleCount,
            "adaptive-GBuffer framebuffer is not the exact six native targets plus RG32_UINT key and product D24S8 depth");
        passed &= Check(
            !fixture.visibilityClass && !fixture.visibilityFramebuffer &&
                !fixture.fallbackFramebuffer && !fixture.indexBuffer &&
                !fixture.materialBuffer &&
                !fixture.geometryInstanceBuffer && !fixture.geometryBuffer &&
                !fixture.compactConstantsBuffer &&
                NoTexturesCreated(fixture.blockCompressedGuideTextures) &&
                NoTexturesCreated(fixture.blockCompressedAlphaTextures) &&
                NoTexturesCreated(fixture.guide.outputs) &&
                NoStagingTexturesCreated(fixture.guide.readbacks) &&
                !fixture.guide.bindingSet &&
                !fixture.blockCompressedGuideMaterialBindingSet &&
                !fixture.blockCompressedAlphaMaterialBindingSet &&
                !fixture.compactGuideLayout && !fixture.compactFixedLayout &&
                !fixture.compactBindlessLayout &&
                !fixture.msaaExtractLayout &&
                !fixture.compactDescriptorTable &&
                !fixture.blockCompressedGuideDescriptorTable &&
                !fixture.blockCompressedAlphaDescriptorTable &&
                !fixture.visibilityPipeline &&
                !fixture.visibilityAlphaPipeline &&
                !fixture.fallbackPipeline && !fixture.fallbackAlphaPipeline &&
                !fixture.compactResolvePipeline &&
                !fixture.compactLightingPipeline &&
                !fixture.msaaExtractPipeline,
            sampleCount,
            "adaptive-GBuffer fixture created compact-visibility-only resources");
        if (!passed)
            return false;

        CompactLightingRun reference;
        std::array<CompactLightingRun, 2u> accepted;
        std::array<CompactLightingRun, 2u> diagnostic;
        passed &= Check(
            CreateAdaptiveGBufferLightingRun(
                device,
                fixture,
                pipelines.referenceLayout,
                false,
                false,
                "Adaptive GBuffer GPU Test/Reference",
                reference),
            sampleCount,
            "could not create Reference adaptive-GBuffer run");
        for (uint32_t topologyIndex = 0u;
            topologyIndex < accepted.size();
            ++topologyIndex)
        {
            const std::string topologyName = topologyIndex == 0u
                ? "Inline"
                : "Separated";
            passed &= Check(
                CreateAdaptiveGBufferLightingRun(
                    device,
                    fixture,
                    pipelines.adaptiveLayout,
                    true,
                    false,
                    "Adaptive GBuffer GPU Test/" + topologyName +
                        "/Accepted",
                    accepted[topologyIndex]) &&
                CreateAdaptiveGBufferLightingRun(
                    device,
                    fixture,
                    pipelines.adaptiveDiagnosticsLayout,
                    true,
                    true,
                    "Adaptive GBuffer GPU Test/" + topologyName +
                        "/Diagnostics",
                    diagnostic[topologyIndex]),
                sampleCount,
                "could not create adaptive-GBuffer topology runs");
        }
        if (!passed)
            return false;

        for (uint32_t caseIndex = 0u;
            caseIndex < static_cast<uint32_t>(
                AdaptiveGBufferProductionCase::Count);
            ++caseIndex)
        {
            const AdaptiveGBufferProductionCase testCase =
                static_cast<AdaptiveGBufferProductionCase>(caseIndex);
            std::cout << "[STAGE] production adaptive-GBuffer "
                << sampleCount << "x "
                << AdaptiveGBufferProductionCaseName(testCase)
                << ".\n";
            passed &= Check(
                RunAdaptiveGBufferProductionCase(
                    device,
                    diagnosticsPass,
                    fixture,
                    pipelines,
                    reference,
                    accepted,
                    diagnostic,
                    options,
                    testCase),
                sampleCount,
                std::string("production adaptive-GBuffer case failed: ") +
                    AdaptiveGBufferProductionCaseName(testCase));
            if (!passed)
                return false;
        }

        passed &= Check(
            validationMessages.ErrorCount() == validationErrorsBefore,
            sampleCount,
            "D3D12/NVRHI validation emitted an error during the "
            "production adaptive-GBuffer shader test");
        return passed;
    }

    [[nodiscard]] bool RunCompactProductionShaderTest(
        nvrhi::IDevice* device,
        donut::engine::ShaderFactory& shaderFactory,
        donut::engine::ShaderFactory& testShaderFactory,
        AdaptiveMsaaDiagnosticsGpuPass& diagnosticsPass,
        ValidationMessageCallback& validationMessages,
        const D3D12_FEATURE_DATA_D3D12_OPTIONS1& options,
        nvrhi::SamplerHandle* qualifiedMaterialSampler = nullptr,
        CompactTextureQualificationEvidence* evidence = nullptr,
        bool* targetTextureQualified = nullptr)
    {
        if (targetTextureQualified)
            *targetTextureQualified = false;
        const uint32_t validationErrorsBefore =
            validationMessages.ErrorCount();
        ClearD3D12InfoQueue(device);
        bool passed = Check(
            options.Int64ShaderOps && options.WaveOps &&
                options.WaveLaneCountMin > 0u &&
                options.WaveLaneCountMax >= options.WaveLaneCountMin,
            CompactFixtureSampleCount,
            "adapter lacks the production compact shader's int64/wave "
            "requirements");
        if (!passed)
            return false;

        CompactProductionFixture fixture;
        passed &= Check(
            CreateCompactProductionFixture(
                device,
                shaderFactory,
                testShaderFactory,
                fixture,
                CompactFixtureSampleCount),
            CompactFixtureSampleCount,
            "could not create the production compact visibility/shading "
            "fixture");
        if (!passed)
            return false;
        if (qualifiedMaterialSampler)
            *qualifiedMaterialSampler = fixture.materialSampler;

        if (std::getenv("UVSR_COMPACT_TEXTURE_REASON_CAPTURE_ONLY"))
        {
            std::cout << "[STAGE] production compact texture exact-reason "
                "capture.\n";
            passed &= CaptureCompactTextureFailureReason(
                device,
                fixture);
            passed &= Check(
                validationMessages.ErrorCount() ==
                    validationErrorsBefore,
                CompactFixtureSampleCount,
                "D3D12/NVRHI validation emitted an error during compact "
                "texture reason capture");
            return passed;
        }

        std::cout << "[STAGE] production textured compact/full-G-buffer "
            "gradient discriminator.\n";
        bool discriminatorExecutionCompleted = false;
        const bool discriminatorQualified =
            ValidateCompactTextureGradientDiscriminator(
                device,
                fixture,
                &discriminatorExecutionCompleted);
        const bool discriminatorValidationClean =
            validationMessages.ErrorCount() == validationErrorsBefore;
        passed &= Check(
            discriminatorExecutionCompleted && discriminatorValidationClean,
            CompactFixtureSampleCount,
            "D3D12/NVRHI validation emitted an error during the textured "
            "compact/full-G-buffer discriminator");
        bool textureQualificationPassed = discriminatorQualified &&
            discriminatorValidationClean;
        if (std::getenv("UVSR_COMPACT_TEXTURE_DISCRIMINATOR_ONLY"))
        {
            if (targetTextureQualified)
                *targetTextureQualified = textureQualificationPassed;
            return passed && textureQualificationPassed;
        }
        if (!passed)
            return false;

        std::cout << "[STAGE] production compact 2x/4x/8x texture and "
            "alpha raster matrix.\n";
        bool matrixUniversalPassed = false;
        const bool matrixPassed = ValidateCompactTextureMatrix(
            device,
            shaderFactory,
            testShaderFactory,
            fixture,
            evidence,
            &matrixUniversalPassed);
        passed &= matrixUniversalPassed;
        textureQualificationPassed &= matrixPassed;
        const bool matrixValidationClean =
            validationMessages.ErrorCount() == validationErrorsBefore;
        passed &= Check(
            matrixValidationClean,
            CompactFixtureSampleCount,
            "D3D12/NVRHI validation emitted an error during the compact "
            "texture/alpha matrix");
        textureQualificationPassed &= matrixValidationClean;
        if (matrixPassed && matrixValidationClean && evidence)
        {
            for (TextureQualificationSampleRecord& sample :
                evidence->samples)
            {
                sample.qualificationMask |=
                    TextureQualificationEvidence_ValidationClean;
            }
        }
        if (std::getenv("UVSR_COMPACT_TEXTURE_MATRIX_ONLY") != nullptr)
        {
            if (targetTextureQualified)
                *targetTextureQualified = textureQualificationPassed;
            return passed && textureQualificationPassed;
        }
        if (!passed)
            return false;
        if (!textureQualificationPassed)
        {
            std::cout << "[TARGET NOT QUALIFIED] strict compact texture "
                "reconstruction evidence was rejected; textured materials "
                "remain on the full-G-buffer fallback.\n";
        }

        std::cout << "[STAGE] production mixed compact guide resolve.\n";
        passed &= ValidateCompactGuideResolve(device, fixture);
        if (!passed)
            return false;

        CompactLightingRun baseline;
        CompactLightingRun overflow;
        passed &= Check(
            CreateCompactLightingRun(
                device,
                fixture,
                "Adaptive MSAA Compact GPU Test/Capacity 128",
                baseline) &&
            CreateCompactLightingRun(
                device,
                fixture,
                "Adaptive MSAA Compact GPU Test/Capacity 1",
                overflow),
            CompactFixtureSampleCount,
            "could not create compact baseline/overflow output resources");
        if (!passed)
            return false;

        DiagnosticsGpuReadbackBlock baselineReadback;
        DiagnosticsGpuReadbackBlock overflowReadback;
        std::cout << "[STAGE] production mixed compact lighting "
            "capacity 128.\n";
        passed &= Check(
            ExecuteCompactLightingRun(
                device,
                diagnosticsPass,
                fixture,
                baseline,
                nullptr,
                CompactQueueMaximum,
                true,
                baselineReadback),
            CompactFixtureSampleCount,
            "capacity-128 production compact dispatch/readback failed");
        if (!passed)
            return false;
        std::cout << "[STAGE] production mixed compact lighting "
            "capacity 1.\n";
        passed &= Check(
            ExecuteCompactLightingRun(
                device,
                diagnosticsPass,
                fixture,
                overflow,
                baseline.output,
                CompactQueueOverflowCapacity,
                false,
                overflowReadback),
            CompactFixtureSampleCount,
            "capacity-1 production compact dispatch/readback failed");
        if (!passed)
            return false;

        passed &= ValidateCompactProductionReadbacks(
            baselineReadback,
            overflowReadback,
            options);
        const uint32_t coveredPixelCount = CompactFixturePixelCount -
            overflowReadback.counters.groupCountHistogram[0u];
        passed &= ValidateCompactLightingOutputs(
            device,
            baseline,
            overflow,
            coveredPixelCount);
        passed &= ValidateCompactVisualization(
            device,
            overflow.visualizationReadback);
        passed &= Check(
            diagnosticsPass.OutstandingReadbackCount() == 0u,
            CompactFixtureSampleCount,
            "compact production readback ring retained a completed token");
        passed &= Check(
            validationMessages.ErrorCount() == validationErrorsBefore,
            CompactFixtureSampleCount,
            "D3D12/NVRHI validation emitted an error during the production "
            "compact shader test");
        if (targetTextureQualified)
            *targetTextureQualified = textureQualificationPassed;
        return passed;
    }

    [[nodiscard]] bool ValidateReadback(
        const DiagnosticsGpuReadbackBlock& block,
        uint32_t sampleCount,
        uint64_t captureId,
        uint64_t frameIndex)
    {
        bool passed = true;
        const DiagnosticsGpuCounterBlock& counters = block.counters;
        passed &= Check(
            counters.width == FixtureWidth &&
                counters.height == FixtureHeight &&
                counters.sampleCount == sampleCount &&
                counters.pixelCount == FixturePixelCount,
            sampleCount,
            "counter header does not describe the submitted fixture");
        passed &= Check(
            counters.validityMask == DiagnosticsCounterAllValidityBits,
            sampleCount,
            "all supplied counter families were not marked valid");
        passed &= Check(
            (counters.flags & DiagnosticsCounterDispatchComplete) != 0u &&
                (counters.flags & DiagnosticsCounterViewAvailable) != 0u,
            sampleCount,
            "counter completion/view flags are missing");

        for (uint32_t groupCount = 0u;
            groupCount < counters.groupCountHistogram.size();
            ++groupCount)
        {
            const uint32_t expected = groupCount == 1u
                ? FixturePixelCount
                : 0u;
            passed &= Check(
                counters.groupCountHistogram[groupCount] == expected,
                sampleCount,
                "exact-key group histogram differs from one group per pixel");
        }

        passed &= Check(
            counters.referenceLightingEvaluations ==
                    FixturePixelCount * sampleCount &&
                counters.performedLightingEvaluations == FixturePixelCount &&
                counters.lightingEvaluationsSaved ==
                    FixturePixelCount * (sampleCount - 1u),
            sampleCount,
            "lighting-evaluation accounting differs from the metadata input");
        passed &= Check(
            counters.invalidKeyCount == 0u &&
                counters.staleKeyCount == 0u &&
                counters.mappingFailureCount == 0u,
            sampleCount,
            "valid synthetic keys produced invalid/stale/mapping counters");
        passed &= Check(
            counters.workListObservationCount == FixturePixelCount &&
                counters.workListOccupancyTotal == FixturePixelCount &&
                counters.workListCapacityTotal ==
                    FixturePixelCount * sampleCount &&
                counters.workListPeakOccupancy == 1u &&
                counters.workListOverflowCount == 0u,
            sampleCount,
            "work-list counters differ from the metadata input");
        for (uint32_t count : counters.fallbackReasonCounts)
        {
            passed &= Check(
                count == 0u,
                sampleCount,
                "FallbackReason::None was incorrectly counted");
        }
        for (uint32_t count : counters.errorBudgetRejectionReasonCounts)
        {
            passed &= Check(
                count == 0u,
                sampleCount,
                "zero-error samples produced an error-budget rejection");
        }
        passed &= Check(
            counters.errorBudgetRejectedGroupCount == 0u &&
                counters.errorBudgetRejectedSampleCount == 0u,
            sampleCount,
            "zero-error groups were rejected");

        const DiagnosticsGpuSelectedHeader& selected = block.selected;
        const uint64_t actualCaptureId =
            uint64_t(selected.captureIdLow) |
            (uint64_t(selected.captureIdHigh) << 32u);
        const uint64_t actualFrameIndex =
            uint64_t(selected.frameIndexLow) |
            (uint64_t(selected.frameIndexHigh) << 32u);
        passed &= Check(
            selected.validityMask == DiagnosticsSelectedHeaderValid &&
                (selected.flags &
                    DiagnosticsSelectedHeaderDispatchComplete) != 0u &&
                (selected.flags &
                    DiagnosticsSelectedHeaderViewAvailable) != 0u &&
                actualCaptureId == captureId &&
                actualFrameIndex == frameIndex &&
                selected.pixelX == 0u && selected.pixelY == 0u &&
                selected.sampleCount == sampleCount &&
                selected.recordCount == sampleCount,
            sampleCount,
            "selected-pixel header does not match the one-shot request");

        constexpr uint32_t ExpectedFields =
            DiagnosticsSelectedRecordCovered |
            DiagnosticsSelectedRecordKeySupplied |
            DiagnosticsSelectedRecordKeyValid |
            DiagnosticsSelectedRecordDepthValid |
            DiagnosticsSelectedRecordSuppliedBarycentrics |
            DiagnosticsSelectedRecordReconstructedBarycentrics |
            DiagnosticsSelectedRecordGroupValid |
            DiagnosticsSelectedRecordFallbackReason |
            DiagnosticsSelectedRecordErrorBudgetReason |
            DiagnosticsSelectedRecordRadiance |
            DiagnosticsSelectedRecordFrontFaceValid |
            DiagnosticsSelectedRecordFrontFace |
            DiagnosticsSelectedRecordWeightValid;
        for (uint32_t sampleIndex = 0u;
            sampleIndex < sampleCount;
            ++sampleIndex)
        {
            const DiagnosticsGpuSelectedSampleRecord& record =
                block.selectedSamples[sampleIndex];
            passed &= Check(
                record.geometryInstanceHandle ==
                        FixtureGeometryInstanceHandle &&
                    record.geometryLocalPrimitiveIndex ==
                        FixturePrimitiveIndex &&
                    record.validFields == ExpectedFields &&
                    Near(FloatFromBits(record.deviceDepthBits), FixtureDepth) &&
                    record.groupIndex == 0u &&
                    record.representativeSampleIndex == 0u &&
                    Near(FloatFromBits(record.weightBits), 1.f) &&
                    record.fallbackReason ==
                        static_cast<uint32_t>(FallbackReason::None) &&
                    record.errorBudgetReason == static_cast<uint32_t>(
                        DiagnosticsErrorBudgetRejectionReason::None),
                sampleCount,
                "selected sample did not preserve exact key/depth/group data");
            passed &= Check(
                Near(FloatFromBits(record.suppliedBarycentricXBits), 0.2f) &&
                    Near(FloatFromBits(record.suppliedBarycentricYBits), 0.3f) &&
                    Near(
                        FloatFromBits(
                            record.reconstructedBarycentricXBits),
                        0.2f) &&
                    Near(
                        FloatFromBits(
                            record.reconstructedBarycentricYBits),
                        0.3f) &&
                    Near(FloatFromBits(record.radianceRBits), 0.25f) &&
                    Near(FloatFromBits(record.radianceGBits), 0.5f) &&
                    Near(FloatFromBits(record.radianceBBits), 0.75f),
                sampleCount,
                "selected sample producer fields did not round-trip");
        }
        return passed;
    }

    struct DiagnosticsVisibilityContractCase
    {
        const char* name = nullptr;
        uint32_t keyClear = ClearedVisibilityWord;
        float depthClear = 0.f;
        uint32_t visibilityClassClear =
            ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND;
        uint32_t expectedInvalidSamples = 0u;
        uint32_t expectedReferenceSamples = 0u;
        uint32_t expectedGroupCount = 0u;
        uint32_t expectedReconstructionFailures = 0u;
        bool supplyVisibilityClass = true;
    };

    [[nodiscard]] bool RunDiagnosticsVisibilityContractCase(
        nvrhi::IDevice* device,
        AdaptiveMsaaDiagnosticsGpuPass& pass,
        nvrhi::ITexture* surfaceKeys,
        nvrhi::ITexture* depth,
        nvrhi::ITexture* visibilityClass,
        nvrhi::IBuffer* pixelMetadata,
        uint32_t sampleCount,
        uint32_t caseIndex,
        const DiagnosticsVisibilityContractCase& testCase)
    {
        const nvrhi::CommandListHandle commandList =
            device->createCommandList();
        if (!Check(
                commandList != nullptr,
                sampleCount,
                std::string("could not create diagnostics contract command list for ") +
                    testCase.name))
        {
            return false;
        }

        AdaptiveMsaaDiagnosticsPassRequest request;
        request.settings.enabled = true;
        request.settings.readbackEnabled = true;
        request.settings.countersEnabled = true;
        request.frameIndex = 0x434f4e5400000000ull |
            (uint64_t(sampleCount) << 8u) | caseIndex;
        request.visualizedSampleIndex = 0u;
        request.maximumWorkListCapacity = sampleCount;

        AdaptiveMsaaDiagnosticsPassInputs inputs;
        inputs.surfaceKeys = surfaceKeys;
        inputs.depth = depth;
        inputs.visibilityClass = testCase.supplyVisibilityClass
            ? visibilityClass
            : nullptr;
        inputs.pixelMetadata = pixelMetadata;
        inputs.referencePerSample = true;

        commandList->open();
        std::array<DiagnosticsGpuPixelMetadataRecord, FixturePixelCount>
            metadata{};
        for (DiagnosticsGpuPixelMetadataRecord& record : metadata)
        {
            record.validFields = DiagnosticsPixelMetadataLightingValid;
            record.performedLightingEvaluations =
                testCase.expectedReferenceSamples;
        }
        commandList->writeBuffer(
            pixelMetadata,
            metadata.data(),
            sizeof(metadata));
        commandList->clearTextureUInt(
            surfaceKeys,
            nvrhi::AllSubresources,
            testCase.keyClear);
        commandList->clearDepthStencilTexture(
            depth,
            nvrhi::AllSubresources,
            true,
            testCase.depthClear,
            false,
            0u);
        if (testCase.supplyVisibilityClass)
        {
            commandList->clearTextureUInt(
                visibilityClass,
                nvrhi::AllSubresources,
                testCase.visibilityClassClear);
        }
        const AdaptiveMsaaDiagnosticsPassResult execution = pass.Execute(
            commandList,
            inputs,
            request);
        if (!Check(
                execution.status == DiagnosticsPassStatus::Success &&
                    execution.dispatched && execution.readbackRecorded &&
                    execution.readbackToken != 0u &&
                    !execution.viewAvailable,
                sampleCount,
                std::string("diagnostics contract dispatch failed for ") +
                    testCase.name))
        {
            if (execution.readbackToken != 0u)
            {
                (void)pass.CancelUnsubmittedReadback(
                    execution.readbackToken);
            }
            commandList->close();
            return false;
        }
        commandList->close();
        device->executeCommandList(commandList);

        bool passed = Check(
            pass.SignalReadbackSubmission(execution.readbackToken) ==
                DiagnosticsPassStatus::Success,
            sampleCount,
            std::string("diagnostics contract submission failed for ") +
                testCase.name);
        passed &= Check(
            device->waitForIdle(),
            sampleCount,
            std::string("diagnostics contract queue wait failed for ") +
                testCase.name);

        DiagnosticsGpuReadbackBlock block;
        const DiagnosticsReadbackPollResult poll = pass.PollReadback(
            request.settings,
            block);
        passed &= Check(
            poll.completed &&
                poll.status == DiagnosticsPassStatus::Success &&
                poll.token == execution.readbackToken,
            sampleCount,
            std::string("diagnostics contract readback failed for ") +
                testCase.name);
        if (!poll.completed || poll.status != DiagnosticsPassStatus::Success)
            return false;

        const DiagnosticsGpuCounterBlock& counters = block.counters;
        const uint32_t expectedInvalid =
            testCase.expectedInvalidSamples * FixturePixelCount;
        const uint32_t expectedReference =
            testCase.expectedReferenceSamples * FixturePixelCount;
        const uint32_t expectedReconstructionFailures =
            testCase.expectedReconstructionFailures * FixturePixelCount;
        passed &= Check(
            counters.width == FixtureWidth &&
                counters.height == FixtureHeight &&
                counters.sampleCount == sampleCount &&
                counters.pixelCount == FixturePixelCount &&
                counters.validityMask ==
                    (DiagnosticsCounterInvalidKeysValid |
                        DiagnosticsCounterGroupHistogramValid |
                        DiagnosticsCounterLightingValid |
                        DiagnosticsCounterFallbackReasonsValid) &&
                (counters.flags & DiagnosticsCounterDispatchComplete) != 0u &&
                (counters.flags & DiagnosticsCounterViewAvailable) == 0u,
            sampleCount,
            std::string("diagnostics contract header mismatch for ") +
                testCase.name);
        passed &= Check(
            counters.invalidKeyCount == expectedInvalid &&
                counters.referenceLightingEvaluations == expectedReference &&
                counters.performedLightingEvaluations == expectedReference &&
                counters.lightingEvaluationsSaved == 0u &&
                counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::ReconstructionFailure)] ==
                    expectedReconstructionFailures,
            sampleCount,
            std::string("diagnostics contract counts mismatch for ") +
                testCase.name);
        for (uint32_t reason = 0u;
            reason < counters.fallbackReasonCounts.size();
            ++reason)
        {
            const uint32_t expected =
                reason == static_cast<uint32_t>(
                    FallbackReason::ReconstructionFailure)
                ? expectedReconstructionFailures
                : 0u;
            passed &= Check(
                counters.fallbackReasonCounts[reason] == expected,
                sampleCount,
                std::string("diagnostics contract fallback histogram mismatch for ") +
                    testCase.name);
        }
        for (uint32_t groupCount = 0u;
            groupCount < counters.groupCountHistogram.size();
            ++groupCount)
        {
            const uint32_t expected =
                groupCount == testCase.expectedGroupCount
                ? FixturePixelCount
                : 0u;
            passed &= Check(
                counters.groupCountHistogram[groupCount] == expected,
                sampleCount,
                std::string("diagnostics contract group histogram mismatch for ") +
                    testCase.name);
        }
        return passed;
    }

    [[nodiscard]] bool RunSampleCount(
        nvrhi::IDevice* device,
        AdaptiveMsaaDiagnosticsGpuPass& pass,
        ValidationMessageCallback& validationMessages,
        uint32_t sampleCount,
        nvrhi::Format visibilityClassFormat)
    {
        const uint32_t validationErrorsBefore =
            validationMessages.ErrorCount();
        ClearD3D12InfoQueue(device);

        const nvrhi::TextureHandle surfaceKeys = CreateMsaaInput(
            device,
            nvrhi::Format::RG32_UINT,
            sampleCount,
            "Adaptive MSAA GPU Test/Surface Keys");
        const nvrhi::TextureHandle depth = CreateMsaaInput(
            device,
            nvrhi::Format::D32,
            sampleCount,
            "Adaptive MSAA GPU Test/Depth");
        const bool visibilityClassSupported =
            visibilityClassFormat != nvrhi::Format::UNKNOWN;
        const nvrhi::TextureHandle visibilityClass = visibilityClassSupported
            ? CreateMsaaInput(
                device,
                visibilityClassFormat,
                sampleCount,
                "Adaptive MSAA GPU Test/Visibility Class")
            : nullptr;
        const nvrhi::TextureHandle referenceHdr = CreateSingleColorTexture(
            device,
            false,
            "Adaptive MSAA GPU Test/Reference HDR");
        const nvrhi::TextureHandle adaptiveHdr = CreateSingleColorTexture(
            device,
            false,
            "Adaptive MSAA GPU Test/Adaptive HDR");
        const nvrhi::TextureHandle output = CreateSingleColorTexture(
            device,
            true,
            "Adaptive MSAA GPU Test/Visualization");
        const nvrhi::TextureHandle sampleStatus =
            CreateSampleStatusTexture(device, sampleCount);
        const nvrhi::BufferHandle selectedSampleData =
            CreateStructuredInput<DiagnosticsGpuSampleInputRecord>(
                device,
                sampleCount,
                "Adaptive MSAA GPU Test/Selected Sample Data");
        const nvrhi::BufferHandle pixelMetadata =
            CreateStructuredInput<DiagnosticsGpuPixelMetadataRecord>(
                device,
                FixturePixelCount,
                "Adaptive MSAA GPU Test/Pixel Metadata");
        if (!Check(
                surfaceKeys && depth &&
                    (!visibilityClassSupported || visibilityClass) &&
                    referenceHdr && adaptiveHdr &&
                    output && sampleStatus && selectedSampleData &&
                    pixelMetadata,
                sampleCount,
                "could not create the deterministic fixture resources"))
        {
            return false;
        }
        if (!Check(
                surfaceKeys->getDesc().sampleCount == sampleCount &&
                    depth->getDesc().sampleCount == sampleCount &&
                    (!visibilityClassSupported ||
                        visibilityClass->getDesc().sampleCount == sampleCount),
                sampleCount,
                "D3D12 did not create the requested exact sample count"))
        {
            return false;
        }

        const nvrhi::StagingTextureHandle outputReadback =
            device->createStagingTexture(
                output->getDesc(),
                nvrhi::CpuAccessMode::Read);
        const nvrhi::CommandListHandle commandList =
            device->createCommandList();
        if (!Check(
                outputReadback && commandList,
                sampleCount,
                "could not create command/readback resources"))
        {
            return false;
        }

        std::vector<DiagnosticsGpuSampleInputRecord> sampleInputs(sampleCount);
        for (DiagnosticsGpuSampleInputRecord& record : sampleInputs)
        {
            record.validFields =
                DiagnosticsSampleInputSuppliedBarycentrics |
                DiagnosticsSampleInputReconstructedBarycentrics |
                DiagnosticsSampleInputRadiance |
                DiagnosticsSampleInputFallbackReason |
                DiagnosticsSampleInputErrorBudgetReason |
                DiagnosticsSampleInputFrontFaceValid |
                DiagnosticsSampleInputFrontFace |
                DiagnosticsSampleInputShadingGroup;
            record.suppliedBarycentricX = 0.2f;
            record.suppliedBarycentricY = 0.3f;
            record.reconstructedBarycentricX = 0.2f;
            record.reconstructedBarycentricY = 0.3f;
            record.radianceR = 0.25f;
            record.radianceG = 0.5f;
            record.radianceB = 0.75f;
            record.fallbackReason =
                static_cast<uint32_t>(FallbackReason::None);
            record.errorBudgetReason = static_cast<uint32_t>(
                DiagnosticsErrorBudgetRejectionReason::None);
            record.shadingGroupIndex = 0u;
            record.representativeSampleIndex = 0u;
        }

        std::array<DiagnosticsGpuPixelMetadataRecord, FixturePixelCount>
            metadata{};
        for (DiagnosticsGpuPixelMetadataRecord& record : metadata)
        {
            record.validFields = DiagnosticsPixelMetadataLightingValid |
                DiagnosticsPixelMetadataWorkListValid |
                DiagnosticsPixelMetadataErrorBudgetValid |
                PackDiagnosticsPixelMetadataGroupData(1u, 1u);
            record.performedLightingEvaluations = 1u;
            record.workListOccupancy = 1u;
            record.workListCapacity = sampleCount;
        }

        AdaptiveMsaaDiagnosticsPassRequest request;
        request.settings.enabled = true;
        request.settings.comparisonMode =
            DiagnosticsComparisonMode::ErrorHeatmap;
        request.settings.debugView =
            AdaptiveMsaaDebugView::AdaptiveLightingError;
        request.settings.selectedPixelCaptureRequested = true;
        request.settings.selectedPixelX = 0u;
        request.settings.selectedPixelY = 0u;
        request.settings.selectedPixelCaptureId =
            0x4458000000000000ull | sampleCount;
        request.settings.readbackEnabled = true;
        request.settings.countersEnabled = true;
        request.settings.dualRenderingEnabled = true;
        request.frameIndex = 0x1122000000000000ull | sampleCount;
        request.visualizedSampleIndex = sampleCount - 1u;
        request.maximumWorkListCapacity = sampleCount;

        AdaptiveMsaaDiagnosticsPassInputs inputs;
        inputs.surfaceKeys = surfaceKeys;
        inputs.depth = depth;
        inputs.visibilityClass = visibilityClass;
        inputs.output = output;
        inputs.referenceHdr = referenceHdr;
        inputs.adaptiveHdr = adaptiveHdr;
        inputs.sampleStatus = sampleStatus;
        inputs.selectedSampleData = selectedSampleData;
        inputs.pixelMetadata = pixelMetadata;
        inputs.producerCapabilityMask =
            DiagnosticsCounterAllValidityBits;

        commandList->open();
        commandList->clearTextureUInt(
            surfaceKeys,
            nvrhi::AllSubresources,
            FixtureGeometryInstanceHandle);
        commandList->clearDepthStencilTexture(
            depth,
            nvrhi::AllSubresources,
            true,
            FixtureDepth,
            false,
            0u);
        if (visibilityClass)
        {
            commandList->clearTextureUInt(
                visibilityClass,
                nvrhi::AllSubresources,
                ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT);
        }
        commandList->clearTextureFloat(
            referenceHdr,
            nvrhi::AllSubresources,
            nvrhi::Color(0.25f, 0.5f, 0.75f, 1.f));
        commandList->clearTextureFloat(
            adaptiveHdr,
            nvrhi::AllSubresources,
            nvrhi::Color(0.25f, 0.5f, 0.75f, 1.f));
        commandList->clearTextureUInt(
            sampleStatus,
            nvrhi::AllSubresources,
            PackDiagnosticsGpuSampleStatus(
                static_cast<uint32_t>(FallbackReason::None),
                static_cast<uint32_t>(
                    DiagnosticsErrorBudgetRejectionReason::None)));
        commandList->writeBuffer(
            selectedSampleData,
            sampleInputs.data(),
            sampleInputs.size() * sizeof(sampleInputs.front()));
        commandList->writeBuffer(
            pixelMetadata,
            metadata.data(),
            sizeof(metadata));

        const AdaptiveMsaaDiagnosticsPassResult execution = pass.Execute(
            commandList,
            inputs,
            request);
        if (!Check(
                execution.status == DiagnosticsPassStatus::Success &&
                    execution.dispatched && execution.viewAvailable &&
                    execution.readbackRecorded &&
                    execution.readbackToken != 0u &&
                    execution.dispatchWidth == 1u &&
                    execution.dispatchHeight == 1u,
                sampleCount,
                "diagnostics pass did not record the expected GPU work"))
        {
            DumpD3D12InfoQueue(device, sampleCount);
            if (execution.readbackToken != 0u)
            {
                (void)pass.CancelUnsubmittedReadback(
                    execution.readbackToken);
            }
            commandList->close();
            return false;
        }

        commandList->copyTexture(
            outputReadback,
            nvrhi::TextureSlice(),
            output,
            nvrhi::TextureSlice());
        commandList->close();
        device->executeCommandList(commandList);

        bool passed = Check(
            pass.SignalReadbackSubmission(execution.readbackToken) ==
                DiagnosticsPassStatus::Success,
            sampleCount,
            "readback submission token was rejected");
        passed &= Check(
            device->waitForIdle(),
            sampleCount,
            "D3D12 queue did not become idle (possible device removal)");

        DiagnosticsGpuReadbackBlock block;
        const DiagnosticsReadbackPollResult poll = pass.PollReadback(
            request.settings,
            block);
        passed &= Check(
            poll.completed &&
                poll.status == DiagnosticsPassStatus::Success &&
                poll.token == execution.readbackToken,
            sampleCount,
            "completed GPU readback failed ABI/semantic validation");
        if (poll.completed && poll.status == DiagnosticsPassStatus::Success)
        {
            passed &= ValidateReadback(
                block,
                sampleCount,
                request.settings.selectedPixelCaptureId,
                request.frameIndex);
        }
        passed &= ValidateOutput(device, outputReadback, sampleCount);
        const uint32_t allSamples = sampleCount;
        const uint32_t validHandle = FixtureGeometryInstanceHandle;
        const uint32_t fallbackClass =
            AdaptiveMsaaEncodeFallbackVisibilityClass(
                static_cast<uint32_t>(FallbackReason::UnsupportedMaterial),
                true);
        const std::array<DiagnosticsVisibilityContractCase, 6u>
            contractCases = {
                DiagnosticsVisibilityContractCase{
                    "exact-zero-clear",
                    ClearedVisibilityWord,
                    0.f,
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND,
                    0u,
                    0u,
                    0u,
                    0u },
                DiagnosticsVisibilityContractCase{
                    "malformed-class",
                    validHandle,
                    FixtureDepth,
                    2u,
                    allSamples,
                    allSamples,
                    0u,
                    allSamples },
                DiagnosticsVisibilityContractCase{
                    "reconstruct-zero-depth",
                    validHandle,
                    0.f,
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_RECONSTRUCT,
                    allSamples,
                    allSamples,
                    0u,
                    allSamples },
                DiagnosticsVisibilityContractCase{
                    "fallback-zero-depth",
                    validHandle,
                    0.f,
                    fallbackClass,
                    0u,
                    allSamples,
                    allSamples,
                    0u },
                DiagnosticsVisibilityContractCase{
                    "classless-zero-depth",
                    validHandle,
                    0.f,
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID,
                    0u,
                    allSamples,
                    allSamples,
                    0u,
                    false },
                DiagnosticsVisibilityContractCase{
                    "classless-exact-zero-clear",
                    ClearedVisibilityWord,
                    0.f,
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_INVALID,
                    0u,
                    0u,
                    0u,
                    0u,
                    false }
            };
        for (uint32_t caseIndex = 0u;
            caseIndex < contractCases.size();
            ++caseIndex)
        {
            if (contractCases[caseIndex].supplyVisibilityClass &&
                !visibilityClass)
            {
                continue;
            }
            passed &= RunDiagnosticsVisibilityContractCase(
                device,
                pass,
                surfaceKeys.Get(),
                depth.Get(),
                visibilityClass.Get(),
                pixelMetadata.Get(),
                sampleCount,
                caseIndex,
                contractCases[caseIndex]);
        }
        passed &= Check(
            pass.OutstandingReadbackCount() == 0u,
            sampleCount,
            "readback ring did not release the completed slot");
        passed &= Check(
            validationMessages.ErrorCount() == validationErrorsBefore,
            sampleCount,
            "D3D12/NVRHI validation emitted an error");
        return passed;
    }

    [[nodiscard]] bool GetCurrentExecutablePath(
        std::filesystem::path& executablePath)
    {
        executablePath.clear();
        std::array<wchar_t, 32768u> buffer{};
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0u || length >= buffer.size())
            return false;
        executablePath = std::filesystem::path(std::wstring(
            buffer.data(), length));
        return !executablePath.empty();
    }

    [[nodiscard]] bool ResolveCanonicalCertificateOutputPath(
        const std::filesystem::path& requestedPath,
        std::filesystem::path& canonicalPath)
    {
        canonicalPath.clear();
        if (requestedPath.empty())
            return false;

        std::filesystem::path executablePath;
        if (!GetCurrentExecutablePath(executablePath))
            return false;
        std::error_code error;
        const std::filesystem::path expectedDirectory =
            std::filesystem::weakly_canonical(
                executablePath.parent_path(),
                error);
        if (error || expectedDirectory.empty())
            return false;
        const std::filesystem::path requestedAbsolute =
            std::filesystem::absolute(
                requestedPath,
                error).lexically_normal();
        if (error || requestedAbsolute.empty() ||
            requestedAbsolute.filename().native() !=
                std::wstring(TextureQualificationCertificateFileName))
        {
            return false;
        }
        const std::filesystem::path requestedDirectory =
            std::filesystem::weakly_canonical(requestedAbsolute, error);
        if (error || requestedDirectory.empty())
            return false;

        const std::wstring expectedNative = expectedDirectory.native();
        const std::wstring requestedNative =
            requestedDirectory.parent_path().native();
        if (expectedNative.size() > size_t(std::numeric_limits<int>::max()) ||
            requestedNative.size() > size_t(std::numeric_limits<int>::max()) ||
            CompareStringOrdinal(
                expectedNative.data(),
                static_cast<int>(expectedNative.size()),
                requestedNative.data(),
                static_cast<int>(requestedNative.size()),
                TRUE) != CSTR_EQUAL)
        {
            return false;
        }
        canonicalPath = expectedDirectory /
            TextureQualificationCertificateFileName;
        const DWORD attributes = GetFileAttributesW(canonicalPath.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & (FILE_ATTRIBUTE_REPARSE_POINT |
                FILE_ATTRIBUTE_DIRECTORY)) != 0u)
        {
            canonicalPath.clear();
            return false;
        }
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const DWORD systemError = GetLastError();
            if (systemError != ERROR_FILE_NOT_FOUND &&
                systemError != ERROR_PATH_NOT_FOUND)
            {
                canonicalPath.clear();
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool ValidateCanonicalCertificateOutputPolicy()
    {
        std::filesystem::path executablePath;
        if (!GetCurrentExecutablePath(executablePath))
            return false;
        const std::filesystem::path expected =
            executablePath.parent_path() /
                TextureQualificationCertificateFileName;
        std::filesystem::path canonical;
        if (!ResolveCanonicalCertificateOutputPath(expected, canonical) ||
            canonical.filename().native() !=
                std::wstring(TextureQualificationCertificateFileName))
        {
            return false;
        }

        std::filesystem::path rejected;
        if (ResolveCanonicalCertificateOutputPath(
                executablePath.parent_path() /
                    L"uvsr_compact_texture_qualification.alias.tqc",
                rejected) ||
            ResolveCanonicalCertificateOutputPath(
                executablePath.parent_path() /
                    L"qualification-output" /
                    TextureQualificationCertificateFileName,
                rejected))
        {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool IssueCompactTextureQualificationCertificate(
        ID3D12Device* nativeDevice,
        nvrhi::ISampler* materialSampler,
        const CompactTextureQualificationEvidence& observedEvidence,
        const std::filesystem::path& productionShaderDirectory,
        const std::filesystem::path& testShaderDirectory,
        const std::filesystem::path& outputPath)
    {
        if (!nativeDevice || !materialSampler || outputPath.empty())
            return false;

        std::filesystem::path testExecutablePath;
        if (!GetCurrentExecutablePath(testExecutablePath))
        {
            std::cerr << "Could not resolve the exact qualification-test "
                "executable path.\n";
            return false;
        }
        std::filesystem::path canonicalOutputPath;
        if (!ResolveCanonicalCertificateOutputPath(
                outputPath,
                canonicalOutputPath))
        {
            std::cerr << "The qualification certificate output must be the "
                "canonical sibling of the exact qualification-test "
                "executable.\n";
            return false;
        }

        const std::filesystem::path executableDirectory =
            testExecutablePath.parent_path();
        const std::filesystem::path rendererExecutablePath =
            executableDirectory / "uvsr.exe";
        const std::filesystem::path buildManifestPath =
            executableDirectory /
                TextureQualificationBuildManifestFileName;
        const std::array<std::filesystem::path, 8u> shaderPaths = {
            productionShaderDirectory /
                "adaptive_msaa_compact_shade_lighting_cs.bin",
            productionShaderDirectory /
                "adaptive_msaa_compact_shade_resolve_cs.bin",
            productionShaderDirectory /
                "adaptive_msaa_compact_visibility_ps.bin",
            productionShaderDirectory /
                "adaptive_msaa_compact_visibility_vs.bin",
            productionShaderDirectory /
                "adaptive_msaa_diagnostics_cs.bin",
            productionShaderDirectory / "adaptive_msaa_gbuffer_ps.bin",
            productionShaderDirectory / "adaptive_msaa_gbuffer_vs.bin",
            testShaderDirectory / "adaptive_msaa_msaa_extract_cs.bin"
        };
        const std::array<TextureQualificationLabeledFile, 8u> shaderFiles = {{
            { "production/adaptive_msaa_compact_shade_lighting_cs.bin",
                shaderPaths[0].c_str() },
            { "production/adaptive_msaa_compact_shade_resolve_cs.bin",
                shaderPaths[1].c_str() },
            { "production/adaptive_msaa_compact_visibility_ps.bin",
                shaderPaths[2].c_str() },
            { "production/adaptive_msaa_compact_visibility_vs.bin",
                shaderPaths[3].c_str() },
            { "production/adaptive_msaa_diagnostics_cs.bin",
                shaderPaths[4].c_str() },
            { "production/adaptive_msaa_gbuffer_ps.bin",
                shaderPaths[5].c_str() },
            { "production/adaptive_msaa_gbuffer_vs.bin",
                shaderPaths[6].c_str() },
            { "test/adaptive_msaa_msaa_extract_cs.bin",
                shaderPaths[7].c_str() }
        }};

        TextureQualificationLoadedD3dModules modules;
        TextureQualificationRuntimeStatus runtimeStatus =
            ResolveTextureQualificationLoadedD3dModules(modules);
        if (!runtimeStatus.Succeeded())
        {
            std::cerr << "Could not resolve the exact loaded D3D modules ("
                << GetTextureQualificationRuntimeReasonName(
                    runtimeStatus.reason) << ").\n";
            return false;
        }

        TextureQualificationLiveIdentityInput input;
        input.d3d12Device = nativeDevice;
        input.materialSampler = materialSampler;
        input.loadedModules = modules;
        input.artifacts.rendererExecutablePath =
            rendererExecutablePath.c_str();
        input.artifacts.rendererBuildManifestPath =
            buildManifestPath.c_str();
        input.artifacts.qualificationTestExecutablePath =
            testExecutablePath.c_str();
        input.artifacts.matrixShaderFiles = shaderFiles.data();
        input.artifacts.matrixShaderFileCount = shaderFiles.size();

        TextureQualificationLiveIdentity liveIdentity;
        runtimeStatus = AcquireTextureQualificationLiveIdentity(
            input, liveIdentity);
        if (!runtimeStatus.Succeeded())
        {
            std::cerr << "Could not acquire the exact qualification identity ("
                << GetTextureQualificationRuntimeReasonName(
                    runtimeStatus.reason) << ").\n";
            return false;
        }

        TextureQualificationCertificate certificate;
        certificate.identity = liveIdentity;
        certificate.samples = observedEvidence.samples;

        for (uint32_t sampleCount : TextureQualificationSampleCounts)
        {
            for (uint32_t parity = 0u; parity < 4u; ++parity)
            {
                TextureQualificationValidationRequest request;
                request.requestedSampleCount = sampleCount;
                request.resolvedSampleCount = sampleCount;
                request.viewportOriginXParity = parity & 1u;
                request.viewportOriginYParity = parity >> 1u;
                const TextureQualificationValidationResult validation =
                    ValidateTextureQualificationCertificate(
                        &certificate,
                        liveIdentity,
                        request);
                if (!validation.Succeeded())
                {
                    std::cerr << "In-memory qualification certificate "
                        "validation failed ("
                        << GetTextureQualificationReasonName(
                            validation.reason) << ").\n";
                    return false;
                }
            }
        }

        runtimeStatus = WriteTextureQualificationCertificateFileAtomically(
            canonicalOutputPath.c_str(), certificate);
        if (!runtimeStatus.Succeeded())
        {
            std::cerr << "Could not atomically issue the qualification "
                "certificate ("
                << GetTextureQualificationRuntimeReasonName(
                    runtimeStatus.reason) << ").\n";
            return false;
        }

        const TextureQualificationCertificateReadResult readback =
            ReadTextureQualificationCertificateFile(
                canonicalOutputPath.c_str());
        if (!readback.status.Succeeded())
        {
            std::cerr << "Issued qualification certificate did not parse ("
                << GetTextureQualificationRuntimeReasonName(
                    readback.status.reason) << ").\n";
            return false;
        }
        std::array<uint8_t, TextureQualificationCertificateByteCount>
            expectedBytes{};
        std::array<uint8_t, TextureQualificationCertificateByteCount>
            actualBytes{};
        const TextureQualificationStatus expectedStatus =
            SerializeTextureQualificationCertificate(
                certificate,
                expectedBytes.data(),
                expectedBytes.size());
        const TextureQualificationStatus actualStatus =
            SerializeTextureQualificationCertificate(
                readback.certificate,
                actualBytes.data(),
                actualBytes.size());
        if (!expectedStatus.Succeeded() || !actualStatus.Succeeded() ||
            expectedBytes != actualBytes)
        {
            std::cerr << "Issued qualification certificate did not "
                "round-trip byte-for-byte.\n";
            return false;
        }

        std::cout << "[PASS] issued compact texture qualification "
            "certificate: " << canonicalOutputPath << ".\n";
        return true;
    }
}

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    donut::log::ConsoleApplicationMode();
    donut::log::SetMinSeverity(donut::log::Severity::Warning);

    const TextureQualificationRuntimeStatus dxilPinStatus =
        PinTextureQualificationDxilModule();
    if (!dxilPinStatus.Succeeded())
    {
        std::cerr << "Could not pin the exact staged DXIL validator ("
            << GetTextureQualificationRuntimeReasonName(dxilPinStatus.reason)
            << ", system " << dxilPinStatus.systemError << ").\n";
        return EXIT_FAILURE;
    }

    TestCommandLine commandLine;
    if (!ParseTestCommandLine(
            argc,
            argv,
            commandLine))
    {
        return EXIT_FAILURE;
    }
    if (!ValidateCanonicalCertificateOutputPolicy())
    {
        std::cerr << "Canonical qualification-certificate path policy "
            "self-test failed.\n";
        return EXIT_FAILURE;
    }
    if (commandLine.writeCompactTextureCertificate)
    {
        std::filesystem::path canonicalOutputPath;
        if (!ResolveCanonicalCertificateOutputPath(
                commandLine.certificateOutputPath,
                canonicalOutputPath))
        {
            std::cerr << "--write-compact-texture-certificate requires the "
                "canonical sibling path named "
                << TextureQualificationCertificateFileName << ".\n";
            return EXIT_FAILURE;
        }
        commandLine.certificateOutputPath = canonicalOutputPath;
    }

    ValidationMessageCallback validationMessages;
    std::unique_ptr<donut::app::DeviceManager> deviceManager(
        donut::app::DeviceManager::Create(nvrhi::GraphicsAPI::D3D12));
    if (!deviceManager)
    {
        std::cerr << "Failed to create the D3D12 DeviceManager after a usable "
            << "adapter was found.\n";
        return EXIT_FAILURE;
    }

    donut::app::DeviceCreationParameters deviceParameters;
    deviceParameters.enableDebugRuntime = true;
    deviceParameters.enableGPUValidation = true;
    deviceParameters.enableNvrhiValidationLayer = true;
    deviceParameters.messageCallback = &validationMessages;
    deviceParameters.featureLevel = D3D_FEATURE_LEVEL_11_1;
    deviceParameters.adapterIndex = commandLine.requestedAdapterIndex;
    SelectedAdapterIdentity selectedAdapter;
    const AdapterSelectionStatus adapterSelection =
        SelectHardwareD3D12Adapter(
            deviceManager.get(),
            deviceParameters,
            selectedAdapter);
    if (adapterSelection == AdapterSelectionStatus::NoUsableAdapter)
    {
        std::cout << "SKIP: no usable hardware D3D12 adapter.\n";
        return NoUsableAdapterExitCode;
    }
    if (adapterSelection == AdapterSelectionStatus::InfrastructureFailure)
        return EXIT_FAILURE;

    if (!deviceManager->CreateHeadlessDevice(deviceParameters))
    {
        std::cerr << "Failed to create the validated headless D3D12 device "
            << "after a usable adapter was found.\n";
        return EXIT_FAILURE;
    }

    nvrhi::IDevice* device = deviceManager->GetDevice();
    if (!device || device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D12)
    {
        std::cerr << "DeviceManager did not return a D3D12 NVRHI device.\n";
        return EXIT_FAILURE;
    }
    ID3D12Device* nativeDevice = device->getNativeObject(
        nvrhi::ObjectTypes::D3D12_Device);
    if (!nativeDevice)
    {
        std::cerr << "NVRHI did not expose its native D3D12 device.\n";
        return EXIT_FAILURE;
    }
    const LUID liveAdapterLuid = nativeDevice->GetAdapterLuid();
    if (liveAdapterLuid.HighPart != selectedAdapter.luid.HighPart ||
        liveAdapterLuid.LowPart != selectedAdapter.luid.LowPart)
    {
        std::cerr << "The headless D3D12 device does not match the adapter "
            "selected with UVSR's max-dedicated-VRAM policy.\n";
        return EXIT_FAILURE;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 d3d12Options{};
    if (FAILED(nativeDevice->CheckFeatureSupport(
            D3D12_FEATURE_D3D12_OPTIONS1,
            &d3d12Options,
            sizeof(d3d12Options))))
    {
        std::cerr << "Could not query D3D12 wave/int64 shader support.\n";
        return EXIT_FAILURE;
    }

    const std::filesystem::path executableDirectory =
        donut::app::GetDirectoryWithExecutable();
    const std::filesystem::path shaderDirectory =
        executableDirectory / "shaders" / "uvsr" /
        donut::app::GetShaderTypeName(nvrhi::GraphicsAPI::D3D12);
    const std::filesystem::path testShaderDirectory =
        executableDirectory / "test-shaders" / "uvsr-tests" /
        donut::app::GetShaderTypeName(nvrhi::GraphicsAPI::D3D12);
    const std::filesystem::path extractBundle =
        testShaderDirectory / "adaptive_msaa_msaa_extract_cs.bin";
    const std::filesystem::path diagnosticsBundle =
        shaderDirectory / "adaptive_msaa_diagnostics_cs.bin";
    const std::array<std::filesystem::path, 9u> requiredBundles = {
        diagnosticsBundle,
        shaderDirectory /
            "adaptive_msaa_compact_visibility_vs.bin",
        shaderDirectory /
            "adaptive_msaa_compact_visibility_ps.bin",
        shaderDirectory /
            "adaptive_msaa_gbuffer_vs.bin",
        shaderDirectory /
            "adaptive_msaa_gbuffer_ps.bin",
        shaderDirectory /
            "adaptive_msaa_compact_shade_resolve_cs.bin",
        shaderDirectory /
            "adaptive_msaa_compact_shade_lighting_cs.bin",
        shaderDirectory /
            "pbr_deferred_lighting_msaa_cs.bin",
        shaderDirectory /
            "pbr_deferred_lighting_adaptive_msaa_cs.bin"
    };
    for (const std::filesystem::path& bundle : requiredBundles)
    {
        if (!std::filesystem::is_regular_file(bundle))
        {
            std::cerr << "Staged UVSR production shader bundle is missing: "
                << bundle << '\n';
            return EXIT_FAILURE;
        }
    }
    if (!std::filesystem::is_regular_file(extractBundle))
    {
        std::cerr << "Staged UVSR GPU-test shader bundle is missing: "
            << extractBundle << '\n';
        return EXIT_FAILURE;
    }

    auto rootFileSystem = std::make_shared<donut::vfs::RootFileSystem>();
    rootFileSystem->mount("/shaders/uvsr", shaderDirectory);
    auto shaderFactory = std::make_shared<donut::engine::ShaderFactory>(
        device,
        rootFileSystem,
        "/shaders");
    auto testRootFileSystem =
        std::make_shared<donut::vfs::RootFileSystem>();
    testRootFileSystem->mount(
        "/test-shaders/uvsr-tests",
        testShaderDirectory);
    auto testShaderFactory =
        std::make_shared<donut::engine::ShaderFactory>(
            device,
            testRootFileSystem,
            "/test-shaders");
    AdaptiveMsaaDiagnosticsGpuPass pass(device, shaderFactory);

    bool passed = true;
    const uint32_t resolved16SampleCount =
        ResolveUvSrRenderTargetSampleCountForTest(
            device,
            nativeDevice,
            16u);
    uint32_t resolved16KeyQualityLevels = 0u;
    uint32_t resolved16DepthQualityLevels = 0u;
    uint32_t resolved16ClassR8QualityLevels = 0u;
    uint32_t resolved16ClassR32QualityLevels = 0u;
    const bool resolved16SurfaceKeySupported =
        resolved16SampleCount > 1u &&
        QueryAdaptiveColorMultisampleSupport(
            nativeDevice,
            DXGI_FORMAT_R32G32_UINT,
            resolved16SampleCount,
            resolved16KeyQualityLevels);
    const bool resolved16CompactDepthSupported =
        resolved16SampleCount > 1u &&
        QueryAdaptiveDepthMultisampleSupport(
            nativeDevice,
            resolved16SampleCount,
            resolved16DepthQualityLevels);
    const bool resolved16ClassSupported =
        resolved16SampleCount > 1u &&
        (QueryAdaptiveColorMultisampleSupport(
             nativeDevice,
             DXGI_FORMAT_R8_UINT,
             resolved16SampleCount,
             resolved16ClassR8QualityLevels) ||
            QueryAdaptiveColorMultisampleSupport(
                nativeDevice,
                DXGI_FORMAT_R32_UINT,
                resolved16SampleCount,
                resolved16ClassR32QualityLevels));
    ComposedCapabilityRequest requested16Capability;
    requested16Capability.requestedSampleCount = 16u;
    requested16Capability.resolvedSampleCount = resolved16SampleCount;
    requested16Capability.requestedImplementation =
        uvsr::AdaptiveMsaaImplementation::CompactVisibility;
    requested16Capability.surfaceKeySupported =
        resolved16SurfaceKeySupported;
    requested16Capability.compactDepthSupported =
        resolved16CompactDepthSupported;
    requested16Capability.visibilityClassSupported =
        resolved16ClassSupported;
    const ComposedCapabilityResult requested16Outcome =
        ResolveComposedAdaptiveMsaaCapability(requested16Capability);
    std::cout << "[16X CAPABILITY] requested=16 resolved="
        << requested16Outcome.resolvedSampleCount
        << " implementation="
        << AdaptiveImplementationName(requested16Outcome.implementation)
        << " reason="
        << GetComposedCapabilityReasonName(requested16Outcome.reason)
        << " render-target-downgraded="
        << (requested16Outcome.renderTargetSampleDowngraded ? 1u : 0u)
        << " quality-levels=(surface-key:"
        << resolved16KeyQualityLevels << ",compact-depth:"
        << resolved16DepthQualityLevels << ",class-r8:"
        << resolved16ClassR8QualityLevels << ",class-r32:"
        << resolved16ClassR32QualityLevels << ").\n";
    passed &= Check(
        (resolved16SampleCount < 16u &&
            requested16Outcome.renderTargetSampleDowngraded) ||
            (resolved16SampleCount == 16u &&
                !requested16Outcome.renderTargetSampleDowngraded),
        16u,
        "composed 16x capability did not preserve render-target resolution "
        "provenance");
    const bool resolved16CompactResourcesSupported =
        resolved16SurfaceKeySupported &&
        resolved16CompactDepthSupported &&
        resolved16ClassSupported;
    if (resolved16SampleCount > 1u &&
        resolved16CompactResourcesSupported)
    {
        passed &= Check(
            requested16Outcome.implementation ==
                    uvsr::AdaptiveMsaaImplementation::CompactVisibility &&
                requested16Outcome.reason ==
                    (resolved16SampleCount < 16u
                        ? ComposedCapabilityReason::
                            RenderTargetSampleDowngrade
                        : ComposedCapabilityReason::None),
            16u,
            "supported resolved compact resources must preserve Compact "
            "Visibility after a 16x render-target downgrade");
    }
    const bool compactTextureDiscriminatorOnly =
        std::getenv("UVSR_COMPACT_TEXTURE_DISCRIMINATOR_ONLY") != nullptr;
    const bool compactTextureReasonCaptureOnly =
        std::getenv("UVSR_COMPACT_TEXTURE_REASON_CAPTURE_ONLY") != nullptr;
    const bool compactTextureMatrixOnly =
        std::getenv("UVSR_COMPACT_TEXTURE_MATRIX_ONLY") != nullptr;
    uint32_t adaptiveGBufferSampleOnly = 0u;
    if (const char* value =
            std::getenv("UVSR_ADAPTIVE_GBUFFER_SAMPLE_ONLY"))
    {
        errno = 0;
        char* parseEnd = nullptr;
        const unsigned long parsed = std::strtoul(value, &parseEnd, 10);
        if (errno == ERANGE || !parseEnd || *parseEnd != '\0' ||
            !IsSupportedSampleCount(static_cast<uint32_t>(parsed)))
        {
            std::cerr << "UVSR_ADAPTIVE_GBUFFER_SAMPLE_ONLY requires one of "
                "2, 4, 8, or 16.\n";
            return EXIT_FAILURE;
        }
        adaptiveGBufferSampleOnly = static_cast<uint32_t>(parsed);
    }
    if (commandLine.writeCompactTextureCertificate &&
        (compactTextureDiscriminatorOnly ||
            compactTextureReasonCaptureOnly ||
            compactTextureMatrixOnly ||
            adaptiveGBufferSampleOnly != 0u))
    {
        std::cerr << "Certificate issuance requires the complete ordinary "
            "GPU test; bounded environment modes cannot issue evidence.\n";
        return EXIT_FAILURE;
    }
    bool compactProductionExecuted = false;
    bool compactTextureQualified = false;
    nvrhi::SamplerHandle qualificationMaterialSampler;
    CompactTextureQualificationEvidence qualificationEvidence;
    struct CompactFixtureFormatSupport
    {
        uint32_t sampleCount = 0u;
        uint32_t keyQualityLevels = 0u;
        uint32_t classQualityLevels = 0u;
        uint32_t depthQualityLevels = 0u;
        bool supported = false;
    };
    std::array<CompactFixtureFormatSupport, 3u> compactFormatSupport = {
        CompactFixtureFormatSupport{ 2u },
        CompactFixtureFormatSupport{ 4u },
        CompactFixtureFormatSupport{ 8u }
    };
    bool compactFormatsSupported = true;
    for (CompactFixtureFormatSupport& support : compactFormatSupport)
    {
        const bool keySupported = QueryAdaptiveColorMultisampleSupport(
            nativeDevice,
            DXGI_FORMAT_R32G32_UINT,
            support.sampleCount,
            support.keyQualityLevels);
        const bool classSupported = QueryAdaptiveColorMultisampleSupport(
            nativeDevice,
            DXGI_FORMAT_R8_UINT,
            support.sampleCount,
            support.classQualityLevels);
        const bool depthSupported = QueryAdaptiveDepthMultisampleSupport(
            nativeDevice,
            support.sampleCount,
            support.depthQualityLevels);
        support.supported = keySupported && classSupported && depthSupported;
        compactFormatsSupported &= support.supported;
        if (support.supported && d3d12Options.Int64ShaderOps &&
            d3d12Options.WaveOps &&
            d3d12Options.WaveLaneCountMin > 0u &&
            d3d12Options.WaveLaneCountMax >=
                d3d12Options.WaveLaneCountMin)
        {
            TextureQualificationSampleRecord* sampleEvidence =
                qualificationEvidence.Find(support.sampleCount);
            if (sampleEvidence)
            {
                sampleEvidence->qualificationMask |=
                    TextureQualificationEvidence_FeatureFormatSupport;
            }
        }
        if (!support.supported)
        {
            std::cerr << "[FAIL] " << support.sampleCount
                << "x compact fixture format support is required "
                << "(RG32_UINT=" << support.keyQualityLevels
                << ", R8_UINT=" << support.classQualityLevels
                << ", D32_FLOAT=" << support.depthQualityLevels
                << ").\n";
        }
    }
    if (adaptiveGBufferSampleOnly != 0u)
    {
        std::cout << "[BOUNDED] skipping compact production matrix to isolate "
            << adaptiveGBufferSampleOnly
            << "x production adaptive-GBuffer execution.\n";
    }
    else if (compactFormatsSupported)
    {
        compactProductionExecuted = true;
        const bool compactPassed = RunCompactProductionShaderTest(
            device,
            *shaderFactory,
            *testShaderFactory,
            pass,
            validationMessages,
            d3d12Options,
            std::addressof(qualificationMaterialSampler),
            &qualificationEvidence,
            &compactTextureQualified);
        std::cout << (compactPassed ? "[PASS] " : "[FAIL] ")
            << "2x/4x/8x universal compact raster/alpha, guide, and "
                "separated-overflow GPU test.\n";
        std::cout << (compactTextureQualified
                ? "[TARGET TEXTURE QUALIFIED] "
                : "[TARGET TEXTURE NOT QUALIFIED] ")
            << "strict compact texture reconstruction; unqualified targets "
                "use the full-G-buffer textured-material fallback.\n";
        passed &= compactPassed;
    }
    else
    {
        passed = false;
    }

    if (compactTextureDiscriminatorOnly || compactTextureReasonCaptureOnly ||
        compactTextureMatrixOnly)
    {
        passed &= compactProductionExecuted;
        passed &= device->waitForIdle();
        device->runGarbageCollection();
        if (validationMessages.ErrorCount() != 0u)
            passed = false;
        std::cout << "Adaptive compact texture bounded-run summary: "
            << (passed ? "passed" : "failed") << ".\n";
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    uint32_t executedCount = 0u;
    uint32_t compactContractExecutedCount = 0u;
    uint32_t adaptiveGBufferProductionExecutedCount = 0u;
    uint32_t skippedCount = 0u;
    for (const uint32_t sampleCount : { 2u, 4u, 8u, 16u })
    {
        uint32_t keyQualityLevels = 0u;
        uint32_t compactDepthQualityLevels = 0u;
        uint32_t adaptiveGBufferDepthQualityLevels = 0u;
        uint32_t classR8QualityLevels = 0u;
        uint32_t classR32QualityLevels = 0u;
        const bool keysSupported = QueryAdaptiveColorMultisampleSupport(
            nativeDevice,
            DXGI_FORMAT_R32G32_UINT,
            sampleCount,
            keyQualityLevels);
        const bool compactDepthSupported =
            QueryAdaptiveDepthMultisampleSupport(
                nativeDevice,
                sampleCount,
                compactDepthQualityLevels);
        const bool adaptiveGBufferDepthSupported =
            QueryAdaptiveGBufferDepthMultisampleSupport(
                nativeDevice,
                sampleCount,
                adaptiveGBufferDepthQualityLevels);
        const bool classR8Supported = QueryAdaptiveColorMultisampleSupport(
            nativeDevice,
            DXGI_FORMAT_R8_UINT,
            sampleCount,
            classR8QualityLevels);
        const bool classR32Supported = QueryAdaptiveColorMultisampleSupport(
            nativeDevice,
            DXGI_FORMAT_R32_UINT,
            sampleCount,
            classR32QualityLevels);
        const bool classSupported = classR8Supported || classR32Supported;
        const uint32_t resolvedRenderTargetSampleCount =
            ResolveUvSrRenderTargetSampleCountForTest(
                device,
                nativeDevice,
                sampleCount);
        const bool adaptiveGBufferFixtureSupported =
            keysSupported && adaptiveGBufferDepthSupported &&
            resolvedRenderTargetSampleCount == sampleCount &&
            d3d12Options.WaveOps &&
            d3d12Options.WaveLaneCountMin > 0u &&
            d3d12Options.WaveLaneCountMax >=
                d3d12Options.WaveLaneCountMin;
        const bool selectedAdaptiveGBufferSample =
            adaptiveGBufferSampleOnly == 0u ||
            adaptiveGBufferSampleOnly == sampleCount;
        if (adaptiveGBufferFixtureSupported && selectedAdaptiveGBufferSample)
        {
            ++adaptiveGBufferProductionExecutedCount;
            const bool adaptiveGBufferPassed =
                RunAdaptiveGBufferProductionShaderTest(
                    device,
                    *shaderFactory,
                    *testShaderFactory,
                    pass,
                    validationMessages,
                    d3d12Options,
                    sampleCount);
            std::cout << (adaptiveGBufferPassed ? "[PASS] " : "[FAIL] ")
                << sampleCount
                << "x production adaptive-GBuffer Reference/inline/"
                    "separated GPU fixture.\n";
            passed &= adaptiveGBufferPassed;
        }
        else if (selectedAdaptiveGBufferSample)
        {
            std::cout << "[SKIP] " << sampleCount
                << "x production adaptive-GBuffer fixture: exact resources "
                << "or wave support unavailable (resolved render "
                << "targets=" << resolvedRenderTargetSampleCount
                << ", RG32_UINT=" << keyQualityLevels
                << ", product D24S8 (D24_UNORM_S8_UINT/"
                    "R24_UNORM_X8_TYPELESS) quality levels="
                << adaptiveGBufferDepthQualityLevels
                << ", wave=" << (d3d12Options.WaveOps ? 1u : 0u)
                << ", wave lanes=" << d3d12Options.WaveLaneCountMin
                << '-' << d3d12Options.WaveLaneCountMax
                << ").\n";
        }
        if (adaptiveGBufferSampleOnly != 0u)
            continue;
        if (!keysSupported || !compactDepthSupported)
        {
            ++skippedCount;
            if (sampleCount == 16u)
            {
                std::cout << "[16X DIAGNOSTICS NOT EXECUTED] requested=16 "
                    << "resolved=" << requested16Outcome.resolvedSampleCount
                    << " implementation="
                    << AdaptiveImplementationName(
                        requested16Outcome.implementation)
                    << " reason="
                    << GetComposedCapabilityReasonName(
                        requested16Outcome.reason)
                    << " (diagnostics RG32_UINT=" << keyQualityLevels
                    << ", D32_FLOAT=" << compactDepthQualityLevels
                    << ", class R8/R32=" << classR8QualityLevels << '/'
                    << classR32QualityLevels << ").\n";
            }
            else
            {
                std::cout << "[SKIP] " << sampleCount
                    << "x: unsupported fixture formats (RG32_UINT quality "
                    << "levels=" << keyQualityLevels
                    << ", D32_FLOAT quality levels="
                    << compactDepthQualityLevels
                    << ", class R8/R32 quality levels="
                    << classR8QualityLevels << '/' << classR32QualityLevels
                    << ").\n";
            }
            continue;
        }

        ++executedCount;
        compactContractExecutedCount += classSupported ? 1u : 0u;
        const bool samplePassed = RunSampleCount(
            device,
            pass,
            validationMessages,
            sampleCount,
            classR8Supported
                ? nvrhi::Format::R8_UINT
                : classR32Supported
                    ? nvrhi::Format::R32_UINT
                    : nvrhi::Format::UNKNOWN);
        std::cout << (samplePassed ? "[PASS] " : "[FAIL] ")
            << sampleCount << "x adaptive diagnostics GPU micro-test.\n";
        passed &= samplePassed;
    }

    if (adaptiveGBufferSampleOnly != 0u)
    {
        passed &= device->waitForIdle();
        device->runGarbageCollection();
        passed &= adaptiveGBufferProductionExecutedCount == 1u;
        if (validationMessages.ErrorCount() != 0u)
            passed = false;
        std::cout << "Adaptive GBuffer bounded-run summary: sample-count="
            << adaptiveGBufferSampleOnly << ", executed="
            << adaptiveGBufferProductionExecutedCount << ", result="
            << (passed ? "passed" : "failed") << ".\n";
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    passed &= device->waitForIdle();
    device->runGarbageCollection();
    if (executedCount == 0u)
    {
        std::cerr << "No adaptive MSAA sample count executed on a usable "
            << "D3D12 adapter; refusing to report a passing GPU test.\n";
        return EXIT_FAILURE;
    }
    if (adaptiveGBufferProductionExecutedCount == 0u)
    {
        std::cerr << "No exact production adaptive-GBuffer sample count "
            << "executed on a usable D3D12 adapter; refusing to report a "
            << "passing GPU test.\n";
        return EXIT_FAILURE;
    }
    if (validationMessages.ErrorCount() != 0u)
    {
        std::cerr << validationMessages.ErrorCount()
            << " D3D12/NVRHI validation error(s) were reported.\n";
        passed = false;
    }
    if (commandLine.writeCompactTextureCertificate)
    {
        if (!passed || !compactProductionExecuted ||
            !compactTextureQualified ||
            !qualificationMaterialSampler)
        {
            std::cerr << "Qualification evidence was incomplete; the "
                "certificate was not written.\n";
            passed = false;
        }
        else
        {
            passed = IssueCompactTextureQualificationCertificate(
                nativeDevice,
                qualificationMaterialSampler,
                qualificationEvidence,
                shaderDirectory,
                testShaderDirectory,
                commandLine.certificateOutputPath);
        }
    }

    std::cout << "Adaptive diagnostics GPU micro-test summary: executed="
        << executedCount << ", skipped-unsupported=" << skippedCount
        << ", compact-contract=" << compactContractExecutedCount
        << ", production-adaptive-gbuffer="
        << adaptiveGBufferProductionExecutedCount
        << ", production-compact="
        << (compactProductionExecuted ? "executed" : "skipped")
        << ".\n";
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
