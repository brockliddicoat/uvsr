#pragma once

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace uvsr
{
    enum class DenoiserBackendKind : uint8_t
    {
        Nrd
    };

    enum class DenoiserSignalType : uint8_t
    {
        AmbientOcclusion = 0,
        DiffuseGi = 1,
        SkyVisibility = 2,
        SunShadow = 3,
        FlashlightShadow = 4
    };

    enum class DenoiserMethod : uint8_t
    {
        ReblurDiffuse = 0,
        RelaxDiffuse = 1,
        SigmaShadow = 2
    };

    enum class DenoiserQuality : uint8_t
    {
        Performance,
        Balanced,
        Quality,
        Ultra
    };

    enum class DenoiserHitDistanceReconstruction : uint8_t
    {
        Off,
        Area3x3,
        Area5x5
    };

    enum class DenoiserHistoryReset : uint8_t
    {
        None,
        Restart,
        ClearAndRestart
    };

    enum class DenoiserStatusCode : uint8_t
    {
        Success,
        Unavailable,
        InvalidArgument,
        Unsupported,
        InitializationFailed,
        ExecutionFailed
    };

    struct DenoiserStatus
    {
        DenoiserStatusCode code = DenoiserStatusCode::Success;
        std::string message;

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return code == DenoiserStatusCode::Success;
        }

        [[nodiscard]] static DenoiserStatus Ok();
        [[nodiscard]] static DenoiserStatus Error(
            DenoiserStatusCode code, std::string message);
    };

    struct DenoiserExtent
    {
        uint32_t width = 0;
        uint32_t height = 0;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] bool operator==(const DenoiserExtent& other) const noexcept;
        [[nodiscard]] bool operator!=(const DenoiserExtent& other) const noexcept;
    };

    struct DenoiserMatrix4x4
    {
        std::array<float, 16> columnMajor = {
            1.f, 0.f, 0.f, 0.f,
            0.f, 1.f, 0.f, 0.f,
            0.f, 0.f, 1.f, 0.f,
            0.f, 0.f, 0.f, 1.f };
    };

    struct DenoiserSignalDescription
    {
        DenoiserSignalType type = DenoiserSignalType::DiffuseGi;
        DenoiserMethod method = DenoiserMethod::ReblurDiffuse;
        DenoiserExtent allocationExtent;
        uint64_t viewKey = 0;
        bool hitDistanceAvailable = true;
        bool checkerboard = false;

        [[nodiscard]] bool operator==(
            const DenoiserSignalDescription& other) const noexcept;
        [[nodiscard]] bool operator!=(
            const DenoiserSignalDescription& other) const noexcept;
    };

    struct DenoiserSignalResources
    {
        // Common guides. The motion resource is previous minus current in
        // signal pixels. Normal and roughness are world space XYZ plus linear
        // roughness. ViewZ is positive linear depth.
        nvrhi::ITexture* motionVectors = nullptr;       // RGBA16_FLOAT
        nvrhi::ITexture* normalRoughness = nullptr;     // RGBA16_SNORM
        nvrhi::ITexture* viewZ = nullptr;               // R32_FLOAT

        // ReBLUR and ReLAX diffuse radiance resources.
        nvrhi::ITexture* noisyRadianceHitDistance = nullptr;    // RGBA16_FLOAT
        nvrhi::ITexture* denoisedRadianceHitDistance = nullptr; // RGBA16_FLOAT

        // SIGMA resources. shadow is both the output and stabilization history.
        nvrhi::ITexture* penumbra = nullptr;            // R16_FLOAT
        nvrhi::ITexture* shadow = nullptr;              // R8_UNORM

        nvrhi::ITexture* validationOutput = nullptr;    // RGBA8_UNORM
    };

    enum class DenoiserTextureSlot : uint8_t
    {
        MotionVectors,
        NormalRoughness,
        ViewZ,
        NoisyRadianceHitDistance,
        DenoisedRadianceHitDistance,
        Penumbra,
        Shadow,
        ValidationOutput,
        Count
    };

    struct DenoiserTextureInfo
    {
        DenoiserExtent extent;
        nvrhi::Format format = nvrhi::Format::UNKNOWN;
        nvrhi::TextureDimension dimension = nvrhi::TextureDimension::Unknown;
        uint32_t sampleCount = 0;
        bool isUav = false;
        bool present = false;
    };

    using DenoiserTextureInfoArray = std::array<DenoiserTextureInfo,
        static_cast<size_t>(DenoiserTextureSlot::Count)>;

    struct DenoiserQualityPreset
    {
        uint32_t reblurFastHistory = 5;
        uint32_t reblurHistoryFix = 2;
        uint32_t reblurStabilizationLimit = 12;
        float reblurPrepassRadius = 20.f;
        float reblurMaximumBlurRadius = 25.f;
        bool reblurAntiFirefly = true;

        uint32_t relaxAtrousIterations = 3;
        uint32_t relaxFastHistory = 5;
        uint32_t relaxHistoryFix = 2;
        float relaxPrepassRadius = 20.f;
        bool relaxAntiFirefly = false;

        uint32_t sigmaSunStabilization = 3;
    };

    struct DenoiserSettings
    {
        DenoiserQuality quality = DenoiserQuality::Balanced;
        DenoiserHitDistanceReconstruction hitDistanceReconstruction =
            DenoiserHitDistanceReconstruction::Off;
        uint32_t historyLength = 16;
        float disocclusionThreshold = 0.01f;
        float antiLagStrength = 0.5f;
        // Matches nrd::ReblurHitDistanceParameters. The resource owner uses
        // { ray reach, epsilon, 1 } for bounded AO, GI, and sky visibility.
        std::array<float, 3> hitDistanceParameters = { 3.f, 0.1f, 20.f };
        std::array<float, 3> lightDirectionWorld = { 0.f, 0.f, 0.f };
        bool returnHistoryLength = false;

        [[nodiscard]] bool operator==(const DenoiserSettings& other) const noexcept;
        [[nodiscard]] bool operator!=(const DenoiserSettings& other) const noexcept;
    };

    struct DenoiserFrameSettings
    {
        // NRD consumes nonjittered column major matrices with column vectors.
        DenoiserMatrix4x4 viewToClip;
        DenoiserMatrix4x4 viewToClipPrev;
        DenoiserMatrix4x4 worldToView;
        DenoiserMatrix4x4 worldToViewPrev;
        std::array<float, 2> cameraJitterPixels = { 0.f, 0.f };
        std::array<float, 2> cameraJitterPixelsPrev = { 0.f, 0.f };
        DenoiserExtent rectExtent;
        DenoiserExtent rectExtentPrev;
        float viewZScale = 1.f;
        float timeDeltaMilliseconds = 0.f;
        float denoisingRange = 500000.f;
        uint64_t frameIndex = 0;
        bool enableValidation = false;
    };

    struct DenoiserBackendCapabilities
    {
        std::string backendName;
        bool backendAvailable = false;
        bool supportsAmbientOcclusion = false;
        bool supportsDiffuseGi = false;
        bool supportsSkyVisibility = false;
        bool supportsSunShadow = false;
        bool supportsFlashlightShadow = false;
        bool supportsReblur = false;
        bool supportsRelax = false;
        bool supportsSigma = false;
        bool supportsValidation = false;
        bool supportsDynamicResolution = false;
        bool supportsHitDistanceReconstruction = false;
        bool supportsDisocclusionThreshold = false;
        bool supportsAntiLag = false;
        uint32_t versionMajor = 0;
        uint32_t versionMinor = 0;
        uint32_t versionBuild = 0;
        uint32_t maximumReblurHistoryLength = 0;
        uint32_t maximumRelaxHistoryLength = 0;
        uint32_t maximumSigmaHistoryLength = 0;
        float minimumResolutionScale = 1.f;
        float maximumResolutionScale = 1.f;
        nvrhi::Format normalRoughnessFormat = nvrhi::Format::UNKNOWN;
    };

    struct DenoiserMemoryStats
    {
        uint64_t permanentBytes = 0;
        uint64_t transientBytes = 0;

        [[nodiscard]] uint64_t TotalBytes() const noexcept
        {
            return permanentBytes + transientBytes;
        }
    };

    struct DenoiserSignalHandle
    {
        uint64_t registryId = 0;
        uint64_t signalId = 0;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return registryId != 0 && signalId != 0;
        }
        [[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }
        [[nodiscard]] bool operator==(
            const DenoiserSignalHandle& other) const noexcept
        {
            return registryId == other.registryId && signalId == other.signalId;
        }
        [[nodiscard]] bool operator!=(
            const DenoiserSignalHandle& other) const noexcept
        {
            return !(*this == other);
        }
    };

    [[nodiscard]] DenoiserExtent CalculateDenoiserExtent(
        DenoiserExtent renderExtent, float resolutionScale) noexcept;
    [[nodiscard]] DenoiserMatrix4x4 RowMajorToColumnMajor(
        const std::array<float, 16>& rowMajor) noexcept;
    [[nodiscard]] std::array<float, 2> GetDenoiserCameraJitter(
        const std::array<float, 2>& jitterPixels) noexcept;
    [[nodiscard]] std::array<float, 3> GetDenoiserScreenMotionScale(
        DenoiserExtent extent) noexcept;
    [[nodiscard]] bool IsConsecutiveDenoiserFrame(
        uint64_t previousFrame, uint64_t currentFrame) noexcept;
    [[nodiscard]] DenoiserHistoryReset MergeDenoiserHistoryReset(
        DenoiserHistoryReset current,
        DenoiserHistoryReset requested) noexcept;
    [[nodiscard]] bool IsDenoiserMethodCompatible(
        DenoiserSignalType type, DenoiserMethod method) noexcept;
    [[nodiscard]] DenoiserQualityPreset GetDenoiserQualityPreset(
        DenoiserQuality quality) noexcept;

    [[nodiscard]] DenoiserTextureInfoArray DescribeDenoiserTextures(
        const DenoiserSignalResources& resources) noexcept;
    [[nodiscard]] DenoiserStatus ValidateDenoiserResourceAliases(
        const DenoiserSignalResources& resources);
    [[nodiscard]] DenoiserStatus ValidateDenoiserSignalContract(
        const DenoiserSignalDescription& description,
        const DenoiserTextureInfoArray& textures);
    [[nodiscard]] DenoiserStatus ValidateDenoiserFrameSettings(
        const DenoiserSignalDescription& description,
        const DenoiserFrameSettings& frame);

    class IDenoiserSignalBackend
    {
    public:
        virtual ~IDenoiserSignalBackend() = default;
        [[nodiscard]] virtual const DenoiserBackendCapabilities&
            GetCapabilities() const noexcept = 0;
        [[nodiscard]] virtual DenoiserMemoryStats GetMemoryStats() const noexcept = 0;
        virtual DenoiserStatus Initialize(
            nvrhi::IDevice* device, uint32_t framesInFlight) = 0;
        virtual DenoiserStatus ConfigureSignal(
            const DenoiserSignalDescription& description,
            const DenoiserSignalResources& resources,
            const DenoiserSettings& settings) = 0;
        virtual void RequestHistoryReset(DenoiserHistoryReset reset) noexcept = 0;
        virtual DenoiserStatus Execute(
            nvrhi::ICommandList* commandList,
            const DenoiserFrameSettings& frame) = 0;
        virtual void Shutdown() noexcept = 0;
    };

    using DenoiserSignalBackendFactory =
        std::function<std::unique_ptr<IDenoiserSignalBackend>()>;

    class IDenoiserBackend
    {
    public:
        virtual ~IDenoiserBackend() = default;
        [[nodiscard]] virtual const DenoiserBackendCapabilities&
            GetCapabilities() const noexcept = 0;
        [[nodiscard]] virtual DenoiserMemoryStats GetMemoryStats() const noexcept = 0;
        virtual DenoiserStatus Initialize(
            nvrhi::IDevice* device, uint32_t framesInFlight) = 0;
        virtual DenoiserStatus RegisterSignal(
            const DenoiserSignalDescription& description,
            const DenoiserSignalResources& resources,
            const DenoiserSettings& settings,
            DenoiserSignalHandle& outputHandle) = 0;
        virtual DenoiserStatus ConfigureSignal(
            DenoiserSignalHandle handle,
            const DenoiserSignalDescription& description,
            const DenoiserSignalResources& resources,
            const DenoiserSettings& settings) = 0;
        [[nodiscard]] virtual bool UnregisterSignal(
            DenoiserSignalHandle handle) noexcept = 0;
        [[nodiscard]] virtual bool RequestHistoryReset(
            DenoiserSignalHandle handle,
            DenoiserHistoryReset reset) noexcept = 0;
        virtual DenoiserStatus Execute(
            DenoiserSignalHandle handle,
            nvrhi::ICommandList* commandList,
            const DenoiserFrameSettings& frame) = 0;
        virtual void Shutdown() noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<IDenoiserBackend> CreateDenoiserBackend(
        DenoiserBackendKind kind = DenoiserBackendKind::Nrd);
    [[nodiscard]] std::unique_ptr<IDenoiserBackend>
        CreateDenoiserBackendRegistry(DenoiserSignalBackendFactory factory);
}
