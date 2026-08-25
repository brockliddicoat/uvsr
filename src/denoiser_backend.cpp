#include "denoiser_backend.h"
#include "nrd_denoiser_backend.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace
{
    using namespace uvsr;

    constexpr uint32_t kNrdMaximumExtent =
        std::numeric_limits<uint16_t>::max();

    [[nodiscard]] bool IsFinite(float value) noexcept
    {
        return std::isfinite(value);
    }

    [[nodiscard]] bool MatrixIsFinite(const DenoiserMatrix4x4& matrix) noexcept
    {
        return std::all_of(matrix.columnMajor.begin(), matrix.columnMajor.end(),
            [](float value) { return IsFinite(value); });
    }

    [[nodiscard]] const char* SlotName(DenoiserTextureSlot slot) noexcept
    {
        switch (slot)
        {
        case DenoiserTextureSlot::MotionVectors: return "motion vectors";
        case DenoiserTextureSlot::NormalRoughness: return "normal and roughness";
        case DenoiserTextureSlot::ViewZ: return "viewZ";
        case DenoiserTextureSlot::NoisyRadianceHitDistance:
            return "noisy radiance and hit distance";
        case DenoiserTextureSlot::DenoisedRadianceHitDistance:
            return "denoised radiance and hit distance";
        case DenoiserTextureSlot::Penumbra: return "penumbra";
        case DenoiserTextureSlot::Shadow: return "shadow";
        case DenoiserTextureSlot::ValidationOutput: return "validation output";
        default: return "unknown";
        }
    }

    [[nodiscard]] const char* FormatName(nvrhi::Format format) noexcept
    {
        switch (format)
        {
        case nvrhi::Format::R8_UNORM: return "R8_UNORM";
        case nvrhi::Format::R16_FLOAT: return "R16_FLOAT";
        case nvrhi::Format::R32_FLOAT: return "R32_FLOAT";
        case nvrhi::Format::RGBA8_UNORM: return "RGBA8_UNORM";
        case nvrhi::Format::RGBA16_FLOAT: return "RGBA16_FLOAT";
        case nvrhi::Format::RGBA16_SNORM: return "RGBA16_SNORM";
        default: return "the required format";
        }
    }

    [[nodiscard]] DenoiserTextureInfo DescribeTexture(
        nvrhi::ITexture* texture) noexcept
    {
        DenoiserTextureInfo info;
        if (!texture)
            return info;

        const nvrhi::TextureDesc& desc = texture->getDesc();
        info.extent = { desc.width, desc.height };
        info.format = desc.format;
        info.dimension = desc.dimension;
        info.sampleCount = desc.sampleCount;
        info.isUav = desc.isUAV;
        info.present = true;
        return info;
    }

    [[nodiscard]] bool IsRequiredSlot(
        DenoiserMethod method, DenoiserTextureSlot slot) noexcept
    {
        if (slot == DenoiserTextureSlot::MotionVectors ||
            slot == DenoiserTextureSlot::NormalRoughness ||
            slot == DenoiserTextureSlot::ViewZ)
        {
            return true;
        }

        switch (method)
        {
        case DenoiserMethod::ReblurDiffuse:
        case DenoiserMethod::RelaxDiffuse:
            return slot == DenoiserTextureSlot::NoisyRadianceHitDistance ||
                slot == DenoiserTextureSlot::DenoisedRadianceHitDistance;
        case DenoiserMethod::SigmaShadow:
            return slot == DenoiserTextureSlot::Penumbra ||
                slot == DenoiserTextureSlot::Shadow;
        default:
            return false;
        }
    }

    std::atomic<uint64_t> g_NextDenoiserRegistryId{ 1 };

    [[nodiscard]] uint64_t AllocateRegistryId() noexcept
    {
        uint64_t id = g_NextDenoiserRegistryId.fetch_add(
            1, std::memory_order_relaxed);
        if (id == 0)
            id = g_NextDenoiserRegistryId.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

    class DenoiserBackendRegistry final : public IDenoiserBackend
    {
    public:
        explicit DenoiserBackendRegistry(DenoiserSignalBackendFactory factory)
            : m_Factory(std::move(factory))
            , m_RegistryId(AllocateRegistryId())
        {
            m_SeedBackend = CreateSignalBackend();
            if (m_SeedBackend)
                m_Capabilities = m_SeedBackend->GetCapabilities();
            else
                m_Capabilities.backendName = "Invalid denoiser factory";
        }

        ~DenoiserBackendRegistry() override { Shutdown(); }

        [[nodiscard]] const DenoiserBackendCapabilities& GetCapabilities()
            const noexcept override { return m_Capabilities; }

        [[nodiscard]] DenoiserMemoryStats GetMemoryStats() const noexcept override
        {
            DenoiserMemoryStats result;
            for (const auto& entry : m_Signals)
            {
                const DenoiserMemoryStats signal = entry.second->GetMemoryStats();
                result.permanentBytes = SaturatingAdd(
                    result.permanentBytes, signal.permanentBytes);
                result.transientBytes = SaturatingAdd(
                    result.transientBytes, signal.transientBytes);
            }
            return result;
        }

        DenoiserStatus Initialize(
            nvrhi::IDevice* device, uint32_t framesInFlight) override
        {
            if (m_Initialized || m_InitializationAttempted || !m_Signals.empty())
                Shutdown();
            m_InitializationAttempted = true;
            if (!m_SeedBackend)
                m_SeedBackend = CreateSignalBackend();
            if (!m_SeedBackend)
            {
                m_InitializationStatus = DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "denoiser signal factory returned null");
                return m_InitializationStatus;
            }

            m_Capabilities = m_SeedBackend->GetCapabilities();
            m_InitializationStatus =
                m_SeedBackend->Initialize(device, framesInFlight);
            m_Capabilities = m_SeedBackend->GetCapabilities();
            if (!m_InitializationStatus)
                return m_InitializationStatus;

            m_Device = device;
            m_FramesInFlight = framesInFlight;
            m_Initialized = true;
            return DenoiserStatus::Ok();
        }

        DenoiserStatus RegisterSignal(
            const DenoiserSignalDescription& description,
            const DenoiserSignalResources& resources,
            const DenoiserSettings& settings,
            DenoiserSignalHandle& outputHandle) override
        {
            outputHandle = {};
            if (!m_Initialized)
                return NotInitialized();

            std::unique_ptr<IDenoiserSignalBackend> backend;
            if (m_SeedBackend)
            {
                backend = std::move(m_SeedBackend);
            }
            else
            {
                backend = CreateSignalBackend();
                if (!backend)
                {
                    return DenoiserStatus::Error(
                        DenoiserStatusCode::InitializationFailed,
                        "denoiser signal factory returned null");
                }
                DenoiserStatus status =
                    backend->Initialize(m_Device.Get(), m_FramesInFlight);
                if (!status)
                {
                    backend->Shutdown();
                    return status;
                }
            }

            DenoiserStatus status = backend->ConfigureSignal(
                description, resources, settings);
            if (!status)
            {
                backend->Shutdown();
                return status;
            }

            const uint64_t signalId = AllocateSignalId();
            m_Signals.emplace(signalId, std::move(backend));
            outputHandle = { m_RegistryId, signalId };
            return DenoiserStatus::Ok();
        }

        DenoiserStatus ConfigureSignal(
            DenoiserSignalHandle handle,
            const DenoiserSignalDescription& description,
            const DenoiserSignalResources& resources,
            const DenoiserSettings& settings) override
        {
            IDenoiserSignalBackend* backend = FindSignal(handle);
            return backend
                ? backend->ConfigureSignal(description, resources, settings)
                : InvalidHandle("ConfigureSignal");
        }

        [[nodiscard]] bool UnregisterSignal(
            DenoiserSignalHandle handle) noexcept override
        {
            if (handle.registryId != m_RegistryId || handle.signalId == 0)
                return false;
            const auto found = m_Signals.find(handle.signalId);
            if (found == m_Signals.end())
                return false;
            found->second->Shutdown();
            m_Signals.erase(found);
            return true;
        }

        [[nodiscard]] bool RequestHistoryReset(
            DenoiserSignalHandle handle,
            DenoiserHistoryReset reset) noexcept override
        {
            IDenoiserSignalBackend* backend = FindSignal(handle);
            if (!backend)
                return false;
            backend->RequestHistoryReset(reset);
            return true;
        }

        DenoiserStatus Execute(
            DenoiserSignalHandle handle,
            nvrhi::ICommandList* commandList,
            const DenoiserFrameSettings& frame) override
        {
            IDenoiserSignalBackend* backend = FindSignal(handle);
            return backend
                ? backend->Execute(commandList, frame)
                : InvalidHandle("Execute");
        }

        void Shutdown() noexcept override
        {
            for (auto& entry : m_Signals)
                entry.second->Shutdown();
            m_Signals.clear();
            if (m_SeedBackend)
                m_SeedBackend->Shutdown();
            m_SeedBackend.reset();
            m_Device = nullptr;
            m_FramesInFlight = 0;
            m_Initialized = false;
        }

    private:
        DenoiserSignalBackendFactory m_Factory;
        const uint64_t m_RegistryId;
        uint64_t m_NextSignalId = 1;
        nvrhi::DeviceHandle m_Device;
        uint32_t m_FramesInFlight = 0;
        bool m_Initialized = false;
        bool m_InitializationAttempted = false;
        DenoiserStatus m_InitializationStatus;
        DenoiserBackendCapabilities m_Capabilities;
        std::unique_ptr<IDenoiserSignalBackend> m_SeedBackend;
        std::unordered_map<uint64_t,
            std::unique_ptr<IDenoiserSignalBackend>> m_Signals;

        [[nodiscard]] std::unique_ptr<IDenoiserSignalBackend>
            CreateSignalBackend() const
        {
            return m_Factory ? m_Factory() : nullptr;
        }

        [[nodiscard]] IDenoiserSignalBackend* FindSignal(
            DenoiserSignalHandle handle) const noexcept
        {
            if (handle.registryId != m_RegistryId || handle.signalId == 0)
                return nullptr;
            const auto found = m_Signals.find(handle.signalId);
            return found != m_Signals.end() ? found->second.get() : nullptr;
        }

        [[nodiscard]] uint64_t AllocateSignalId() noexcept
        {
            uint64_t id = m_NextSignalId++;
            while (id == 0 || m_Signals.find(id) != m_Signals.end())
                id = m_NextSignalId++;
            return id;
        }

        [[nodiscard]] DenoiserStatus NotInitialized() const
        {
            if (m_InitializationAttempted && !m_InitializationStatus)
                return m_InitializationStatus;
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                "Initialize must succeed before RegisterSignal");
        }

        [[nodiscard]] static DenoiserStatus InvalidHandle(const char* operation)
        {
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                std::string(operation) +
                " requires a live handle from this backend registry");
        }

        [[nodiscard]] static uint64_t SaturatingAdd(
            uint64_t a, uint64_t b) noexcept
        {
            return b > std::numeric_limits<uint64_t>::max() - a
                ? std::numeric_limits<uint64_t>::max() : a + b;
        }
    };
}

namespace uvsr
{
    DenoiserStatus DenoiserStatus::Ok() { return {}; }

    DenoiserStatus DenoiserStatus::Error(
        DenoiserStatusCode code, std::string message)
    {
        return { code, std::move(message) };
    }

    bool DenoiserExtent::IsValid() const noexcept
    {
        return width > 0 && height > 0 &&
            width <= kNrdMaximumExtent && height <= kNrdMaximumExtent;
    }

    bool DenoiserExtent::operator==(const DenoiserExtent& other) const noexcept
    {
        return width == other.width && height == other.height;
    }

    bool DenoiserExtent::operator!=(const DenoiserExtent& other) const noexcept
    {
        return !(*this == other);
    }

    bool DenoiserSignalDescription::operator==(
        const DenoiserSignalDescription& other) const noexcept
    {
        return type == other.type && method == other.method &&
            allocationExtent == other.allocationExtent &&
            viewKey == other.viewKey &&
            hitDistanceAvailable == other.hitDistanceAvailable &&
            checkerboard == other.checkerboard;
    }

    bool DenoiserSignalDescription::operator!=(
        const DenoiserSignalDescription& other) const noexcept
    {
        return !(*this == other);
    }

    bool DenoiserSettings::operator==(const DenoiserSettings& other) const noexcept
    {
        return quality == other.quality &&
            hitDistanceReconstruction == other.hitDistanceReconstruction &&
            historyLength == other.historyLength &&
            disocclusionThreshold == other.disocclusionThreshold &&
            antiLagStrength == other.antiLagStrength &&
            hitDistanceParameters == other.hitDistanceParameters &&
            lightDirectionWorld == other.lightDirectionWorld &&
            returnHistoryLength == other.returnHistoryLength;
    }

    bool DenoiserSettings::operator!=(const DenoiserSettings& other) const noexcept
    {
        return !(*this == other);
    }

    DenoiserExtent CalculateDenoiserExtent(
        DenoiserExtent renderExtent, float resolutionScale) noexcept
    {
        if (renderExtent.width == 0 || renderExtent.height == 0 ||
            !IsFinite(resolutionScale) ||
            resolutionScale < 0.25f || resolutionScale > 1.f)
        {
            return {};
        }
        const double width = std::ceil(
            static_cast<double>(renderExtent.width) * resolutionScale);
        const double height = std::ceil(
            static_cast<double>(renderExtent.height) * resolutionScale);
        if (width > kNrdMaximumExtent || height > kNrdMaximumExtent)
            return {};
        return { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
    }

    DenoiserMatrix4x4 RowMajorToColumnMajor(
        const std::array<float, 16>& rowMajor) noexcept
    {
        DenoiserMatrix4x4 result;
        // The byte layout of a Donut row vector matrix equals the column major
        // layout of its transpose, which represents the same transform in NRD.
        result.columnMajor = rowMajor;
        return result;
    }

    std::array<float, 2> GetDenoiserCameraJitter(
        const std::array<float, 2>& jitterPixels) noexcept
    {
        if (!IsFinite(jitterPixels[0]) || !IsFinite(jitterPixels[1]) ||
            std::abs(jitterPixels[0]) > 0.5f ||
            std::abs(jitterPixels[1]) > 0.5f)
        {
            return { 0.f, 0.f };
        }
        return jitterPixels;
    }

    std::array<float, 3> GetDenoiserScreenMotionScale(
        DenoiserExtent extent) noexcept
    {
        if (!extent.IsValid())
            return { 0.f, 0.f, 0.f };
        return {
            1.f / static_cast<float>(extent.width),
            1.f / static_cast<float>(extent.height),
            0.f };
    }

    bool IsConsecutiveDenoiserFrame(
        uint64_t previousFrame, uint64_t currentFrame) noexcept
    {
        return static_cast<uint32_t>(currentFrame) ==
            static_cast<uint32_t>(previousFrame) + 1u;
    }

    DenoiserHistoryReset MergeDenoiserHistoryReset(
        DenoiserHistoryReset current,
        DenoiserHistoryReset requested) noexcept
    {
        return static_cast<uint8_t>(requested) > static_cast<uint8_t>(current)
            ? requested : current;
    }

    bool IsDenoiserMethodCompatible(
        DenoiserSignalType type, DenoiserMethod method) noexcept
    {
        switch (type)
        {
        case DenoiserSignalType::AmbientOcclusion:
            return method == DenoiserMethod::ReblurDiffuse;
        case DenoiserSignalType::DiffuseGi:
        case DenoiserSignalType::SkyVisibility:
            return method == DenoiserMethod::ReblurDiffuse ||
                method == DenoiserMethod::RelaxDiffuse;
        case DenoiserSignalType::SunShadow:
        case DenoiserSignalType::FlashlightShadow:
            return method == DenoiserMethod::SigmaShadow;
        default:
            return false;
        }
    }

    DenoiserQualityPreset GetDenoiserQualityPreset(
        DenoiserQuality quality) noexcept
    {
        DenoiserQualityPreset preset;
        switch (quality)
        {
        case DenoiserQuality::Performance:
            preset.reblurFastHistory = 3;
            preset.reblurHistoryFix = 1;
            preset.reblurStabilizationLimit = 0;
            preset.reblurPrepassRadius = 10.f;
            preset.reblurMaximumBlurRadius = 20.f;
            preset.reblurAntiFirefly = false;
            preset.relaxAtrousIterations = 2;
            preset.relaxFastHistory = 3;
            preset.relaxHistoryFix = 1;
            preset.relaxPrepassRadius = 10.f;
            preset.relaxAntiFirefly = false;
            preset.sigmaSunStabilization = 0;
            break;
        case DenoiserQuality::Quality:
            preset.reblurFastHistory = 6;
            preset.reblurHistoryFix = 3;
            preset.reblurStabilizationLimit =
                std::numeric_limits<uint32_t>::max();
            preset.reblurPrepassRadius = 30.f;
            preset.reblurMaximumBlurRadius = 30.f;
            preset.reblurAntiFirefly = true;
            preset.relaxAtrousIterations = 5;
            preset.relaxFastHistory = 6;
            preset.relaxHistoryFix = 3;
            preset.relaxPrepassRadius = 30.f;
            preset.relaxAntiFirefly = false;
            preset.sigmaSunStabilization = 5;
            break;
        case DenoiserQuality::Ultra:
            preset.reblurFastHistory = 8;
            preset.reblurHistoryFix = 3;
            preset.reblurStabilizationLimit =
                std::numeric_limits<uint32_t>::max();
            preset.reblurPrepassRadius = 30.f;
            preset.reblurMaximumBlurRadius = 40.f;
            preset.reblurAntiFirefly = true;
            preset.relaxAtrousIterations = 8;
            preset.relaxFastHistory = 8;
            preset.relaxHistoryFix = 3;
            preset.relaxPrepassRadius = 30.f;
            preset.relaxAntiFirefly = true;
            preset.sigmaSunStabilization = 7;
            break;
        default:
            break;
        }
        return preset;
    }

    DenoiserTextureInfoArray DescribeDenoiserTextures(
        const DenoiserSignalResources& resources) noexcept
    {
        DenoiserTextureInfoArray result{};
        result[size_t(DenoiserTextureSlot::MotionVectors)] =
            DescribeTexture(resources.motionVectors);
        result[size_t(DenoiserTextureSlot::NormalRoughness)] =
            DescribeTexture(resources.normalRoughness);
        result[size_t(DenoiserTextureSlot::ViewZ)] =
            DescribeTexture(resources.viewZ);
        result[size_t(DenoiserTextureSlot::NoisyRadianceHitDistance)] =
            DescribeTexture(resources.noisyRadianceHitDistance);
        result[size_t(DenoiserTextureSlot::DenoisedRadianceHitDistance)] =
            DescribeTexture(resources.denoisedRadianceHitDistance);
        result[size_t(DenoiserTextureSlot::Penumbra)] =
            DescribeTexture(resources.penumbra);
        result[size_t(DenoiserTextureSlot::Shadow)] =
            DescribeTexture(resources.shadow);
        result[size_t(DenoiserTextureSlot::ValidationOutput)] =
            DescribeTexture(resources.validationOutput);
        return result;
    }

    DenoiserStatus ValidateDenoiserResourceAliases(
        const DenoiserSignalResources& resources)
    {
        const std::array<nvrhi::ITexture*,
            size_t(DenoiserTextureSlot::Count)> textures = {
            resources.motionVectors,
            resources.normalRoughness,
            resources.viewZ,
            resources.noisyRadianceHitDistance,
            resources.denoisedRadianceHitDistance,
            resources.penumbra,
            resources.shadow,
            resources.validationOutput };

        for (size_t first = 0; first < textures.size(); ++first)
        {
            if (!textures[first])
                continue;
            for (size_t second = first + 1; second < textures.size(); ++second)
            {
                if (textures[first] != textures[second])
                    continue;
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InvalidArgument,
                    std::string(SlotName(DenoiserTextureSlot(first))) + " and " +
                    SlotName(DenoiserTextureSlot(second)) +
                    " must use distinct textures");
            }
        }
        return DenoiserStatus::Ok();
    }

    DenoiserStatus ValidateDenoiserSignalContract(
        const DenoiserSignalDescription& description,
        const DenoiserTextureInfoArray& textures)
    {
        if (!IsDenoiserMethodCompatible(description.type, description.method))
        {
            return DenoiserStatus::Error(DenoiserStatusCode::Unsupported,
                "the selected NRD method is not compatible with this signal");
        }
        if (!description.allocationExtent.IsValid())
        {
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                "denoiser allocation dimensions must be in [1, 65535]");
        }
        if (!description.hitDistanceAvailable)
        {
            return DenoiserStatus::Error(DenoiserStatusCode::Unsupported,
                "the selected NRD method requires physical hit distance data");
        }
        if (description.checkerboard)
        {
            return DenoiserStatus::Error(DenoiserStatusCode::Unsupported,
                "checkerboard input is not enabled by the UVSR NRD build");
        }

        for (uint32_t index = 0;
            index < uint32_t(DenoiserTextureSlot::Count); ++index)
        {
            const auto slot = DenoiserTextureSlot(index);
            const DenoiserTextureInfo& texture = textures[index];
            const bool required = IsRequiredSlot(description.method, slot);
            if (slot == DenoiserTextureSlot::ValidationOutput && !texture.present)
                continue;
            if (!required && slot != DenoiserTextureSlot::ValidationOutput)
                continue;
            if (!texture.present)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                    std::string("missing ") + SlotName(slot) + " texture");
            }
            if (texture.extent != description.allocationExtent ||
                texture.dimension != nvrhi::TextureDimension::Texture2D ||
                texture.sampleCount != 1)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                    std::string(SlotName(slot)) +
                    " must be a single sample Texture2D matching the allocation");
            }
        }

        const auto requireFormat = [&](DenoiserTextureSlot slot,
            nvrhi::Format format, bool requireUav) -> DenoiserStatus
        {
            const DenoiserTextureInfo& texture = textures[size_t(slot)];
            if (!texture.present || texture.format != format ||
                (requireUav && !texture.isUav))
            {
                return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                    std::string(SlotName(slot)) + " must be " +
                    (requireUav ? "UAV capable " : "") +
                    FormatName(format));
            }
            return DenoiserStatus::Ok();
        };

        if (DenoiserStatus status = requireFormat(
            DenoiserTextureSlot::MotionVectors,
            nvrhi::Format::RGBA16_FLOAT, true); !status) return status;
        if (DenoiserStatus status = requireFormat(
            DenoiserTextureSlot::NormalRoughness,
            nvrhi::Format::RGBA16_SNORM, false); !status) return status;
        if (DenoiserStatus status = requireFormat(
            DenoiserTextureSlot::ViewZ,
            nvrhi::Format::R32_FLOAT, false); !status) return status;

        switch (description.method)
        {
        case DenoiserMethod::ReblurDiffuse:
        case DenoiserMethod::RelaxDiffuse:
            if (DenoiserStatus status = requireFormat(
                DenoiserTextureSlot::NoisyRadianceHitDistance,
                nvrhi::Format::RGBA16_FLOAT, false); !status) return status;
            if (DenoiserStatus status = requireFormat(
                DenoiserTextureSlot::DenoisedRadianceHitDistance,
                nvrhi::Format::RGBA16_FLOAT, true); !status) return status;
            break;
        case DenoiserMethod::SigmaShadow:
            if (DenoiserStatus status = requireFormat(
                DenoiserTextureSlot::Penumbra,
                nvrhi::Format::R16_FLOAT, false); !status) return status;
            if (DenoiserStatus status = requireFormat(
                DenoiserTextureSlot::Shadow,
                nvrhi::Format::R8_UNORM, true); !status) return status;
            break;
        }

        const DenoiserTextureInfo& validation =
            textures[size_t(DenoiserTextureSlot::ValidationOutput)];
        if (validation.present &&
            (validation.format != nvrhi::Format::RGBA8_UNORM ||
                !validation.isUav))
        {
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                "validation output must be UAV capable RGBA8_UNORM");
        }
        return DenoiserStatus::Ok();
    }

    DenoiserStatus ValidateDenoiserFrameSettings(
        const DenoiserSignalDescription& description,
        const DenoiserFrameSettings& frame)
    {
        if (!description.allocationExtent.IsValid())
        {
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                "cannot validate a frame without a valid allocation");
        }
        if (!MatrixIsFinite(frame.viewToClip) ||
            !MatrixIsFinite(frame.viewToClipPrev) ||
            !MatrixIsFinite(frame.worldToView) ||
            !MatrixIsFinite(frame.worldToViewPrev))
        {
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                "denoiser matrices must contain finite values");
        }
        if (!IsFinite(frame.cameraJitterPixels[0]) ||
            !IsFinite(frame.cameraJitterPixels[1]) ||
            !IsFinite(frame.cameraJitterPixelsPrev[0]) ||
            !IsFinite(frame.cameraJitterPixelsPrev[1]) ||
            std::abs(frame.cameraJitterPixels[0]) > 0.5f ||
            std::abs(frame.cameraJitterPixels[1]) > 0.5f ||
            std::abs(frame.cameraJitterPixelsPrev[0]) > 0.5f ||
            std::abs(frame.cameraJitterPixelsPrev[1]) > 0.5f)
        {
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                "denoiser jitter must be finite pixel offsets in [-0.5, 0.5]");
        }
        const auto isZero = [](DenoiserExtent extent)
        {
            return extent.width == 0 && extent.height == 0;
        };
        if ((!isZero(frame.rectExtent) && !frame.rectExtent.IsValid()) ||
            (!isZero(frame.rectExtentPrev) && !frame.rectExtentPrev.IsValid()))
        {
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                "dynamic resolution rectangles must be zero or valid on both axes");
        }
        const DenoiserExtent rect = frame.rectExtent.IsValid()
            ? frame.rectExtent : description.allocationExtent;
        const DenoiserExtent rectPrev = frame.rectExtentPrev.IsValid()
            ? frame.rectExtentPrev : rect;
        if (rect.width > description.allocationExtent.width ||
            rect.height > description.allocationExtent.height ||
            rectPrev.width > description.allocationExtent.width ||
            rectPrev.height > description.allocationExtent.height)
        {
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                "dynamic resolution rectangles must fit inside the allocation");
        }
        if (!IsFinite(frame.viewZScale) || frame.viewZScale <= 0.f ||
            !IsFinite(frame.denoisingRange) || frame.denoisingRange <= 0.f ||
            !IsFinite(frame.timeDeltaMilliseconds) ||
            frame.timeDeltaMilliseconds < 0.f)
        {
            return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                "viewZ scale, denoising range, or frame delta is invalid");
        }
        return DenoiserStatus::Ok();
    }

    std::unique_ptr<IDenoiserBackend> CreateDenoiserBackendRegistry(
        DenoiserSignalBackendFactory factory)
    {
        return std::make_unique<DenoiserBackendRegistry>(std::move(factory));
    }

    std::unique_ptr<IDenoiserBackend> CreateDenoiserBackend()
    {
        return CreateDenoiserBackendRegistry([]
        {
            return CreateCompiledNrdDenoiserSignalBackend();
        });
    }
}
