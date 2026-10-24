#include "nrd_denoiser_backend.h"

#include <NRD.h>
#include <NRDSettings.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace uvsr;

    constexpr nrd::Identifier kSignalIdentifier = 1;
    constexpr uint32_t kMaximumDispatchResources = 64;
    constexpr uint32_t kExpectedNrdVersionMajor = 4;
    constexpr uint32_t kExpectedNrdVersionMinor = 17;
    constexpr uint32_t kExpectedNrdVersionBuild = 3;

    [[nodiscard]] const char* NrdResultName(nrd::Result result) noexcept
    {
        switch (result)
        {
        case nrd::Result::SUCCESS: return "SUCCESS";
        case nrd::Result::FAILURE: return "FAILURE";
        case nrd::Result::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case nrd::Result::UNSUPPORTED: return "UNSUPPORTED";
        case nrd::Result::NON_UNIQUE_IDENTIFIER: return "NON_UNIQUE_IDENTIFIER";
        default: return "UNKNOWN";
        }
    }

    [[nodiscard]] DenoiserStatus NrdFailure(
        DenoiserStatusCode code, const char* operation, nrd::Result result)
    {
        return DenoiserStatus::Error(code,
            std::string(operation) + " failed: " + NrdResultName(result));
    }

    [[nodiscard]] uint32_t DivideUp(uint32_t value, uint32_t divisor) noexcept
    {
        return (value + divisor - 1u) / divisor;
    }

    [[nodiscard]] bool ResourcesEqual(
        const DenoiserSignalResources& a,
        const DenoiserSignalResources& b) noexcept
    {
        return a.motionVectors == b.motionVectors &&
            a.normalRoughness == b.normalRoughness &&
            a.viewZ == b.viewZ &&
            a.noisyRadianceHitDistance == b.noisyRadianceHitDistance &&
            a.denoisedRadianceHitDistance == b.denoisedRadianceHitDistance &&
            a.penumbra == b.penumbra &&
            a.shadow == b.shadow &&
            a.validationOutput == b.validationOutput;
    }

    [[nodiscard]] nvrhi::Format ToNvrhiFormat(nrd::Format format) noexcept
    {
        switch (format)
        {
        case nrd::Format::R8_UNORM: return nvrhi::Format::R8_UNORM;
        case nrd::Format::R8_SNORM: return nvrhi::Format::R8_SNORM;
        case nrd::Format::R8_UINT: return nvrhi::Format::R8_UINT;
        case nrd::Format::R8_SINT: return nvrhi::Format::R8_SINT;
        case nrd::Format::RG8_UNORM: return nvrhi::Format::RG8_UNORM;
        case nrd::Format::RG8_SNORM: return nvrhi::Format::RG8_SNORM;
        case nrd::Format::RG8_UINT: return nvrhi::Format::RG8_UINT;
        case nrd::Format::RG8_SINT: return nvrhi::Format::RG8_SINT;
        case nrd::Format::RGBA8_UNORM: return nvrhi::Format::RGBA8_UNORM;
        case nrd::Format::RGBA8_SNORM: return nvrhi::Format::RGBA8_SNORM;
        case nrd::Format::RGBA8_UINT: return nvrhi::Format::RGBA8_UINT;
        case nrd::Format::RGBA8_SINT: return nvrhi::Format::RGBA8_SINT;
        case nrd::Format::RGBA8_SRGB: return nvrhi::Format::SRGBA8_UNORM;
        case nrd::Format::R16_UNORM: return nvrhi::Format::R16_UNORM;
        case nrd::Format::R16_SNORM: return nvrhi::Format::R16_SNORM;
        case nrd::Format::R16_UINT: return nvrhi::Format::R16_UINT;
        case nrd::Format::R16_SINT: return nvrhi::Format::R16_SINT;
        case nrd::Format::R16_SFLOAT: return nvrhi::Format::R16_FLOAT;
        case nrd::Format::RG16_UNORM: return nvrhi::Format::RG16_UNORM;
        case nrd::Format::RG16_SNORM: return nvrhi::Format::RG16_SNORM;
        case nrd::Format::RG16_UINT: return nvrhi::Format::RG16_UINT;
        case nrd::Format::RG16_SINT: return nvrhi::Format::RG16_SINT;
        case nrd::Format::RG16_SFLOAT: return nvrhi::Format::RG16_FLOAT;
        case nrd::Format::RGBA16_UNORM: return nvrhi::Format::RGBA16_UNORM;
        case nrd::Format::RGBA16_SNORM: return nvrhi::Format::RGBA16_SNORM;
        case nrd::Format::RGBA16_UINT: return nvrhi::Format::RGBA16_UINT;
        case nrd::Format::RGBA16_SINT: return nvrhi::Format::RGBA16_SINT;
        case nrd::Format::RGBA16_SFLOAT: return nvrhi::Format::RGBA16_FLOAT;
        case nrd::Format::R32_UINT: return nvrhi::Format::R32_UINT;
        case nrd::Format::R32_SINT: return nvrhi::Format::R32_SINT;
        case nrd::Format::R32_SFLOAT: return nvrhi::Format::R32_FLOAT;
        case nrd::Format::RG32_UINT: return nvrhi::Format::RG32_UINT;
        case nrd::Format::RG32_SINT: return nvrhi::Format::RG32_SINT;
        case nrd::Format::RG32_SFLOAT: return nvrhi::Format::RG32_FLOAT;
        case nrd::Format::RGB32_UINT: return nvrhi::Format::RGB32_UINT;
        case nrd::Format::RGB32_SINT: return nvrhi::Format::RGB32_SINT;
        case nrd::Format::RGB32_SFLOAT: return nvrhi::Format::RGB32_FLOAT;
        case nrd::Format::RGBA32_UINT: return nvrhi::Format::RGBA32_UINT;
        case nrd::Format::RGBA32_SINT: return nvrhi::Format::RGBA32_SINT;
        case nrd::Format::RGBA32_SFLOAT: return nvrhi::Format::RGBA32_FLOAT;
        case nrd::Format::R10_G10_B10_A2_UNORM:
            return nvrhi::Format::R10G10B10A2_UNORM;
        case nrd::Format::R11_G11_B10_UFLOAT:
            return nvrhi::Format::R11G11B10_FLOAT;
        default:
            return nvrhi::Format::UNKNOWN;
        }
    }

    [[nodiscard]] nrd::AccumulationMode ToNrdReset(
        DenoiserHistoryReset reset) noexcept
    {
        switch (reset)
        {
        case DenoiserHistoryReset::ClearAndRestart:
            return nrd::AccumulationMode::CLEAR_AND_RESTART;
        case DenoiserHistoryReset::Restart:
            return nrd::AccumulationMode::RESTART;
        default:
            return nrd::AccumulationMode::CONTINUE;
        }
    }

    [[nodiscard]] nrd::HitDistanceReconstructionMode ToNrdHitDistanceMode(
        DenoiserHitDistanceReconstruction mode) noexcept
    {
        switch (mode)
        {
        case DenoiserHitDistanceReconstruction::Area3x3:
            return nrd::HitDistanceReconstructionMode::AREA_3X3;
        case DenoiserHitDistanceReconstruction::Area5x5:
            return nrd::HitDistanceReconstructionMode::AREA_5X5;
        default:
            return nrd::HitDistanceReconstructionMode::OFF;
        }
    }

    [[nodiscard]] nrd::Denoiser ToNrdDenoiser(DenoiserMethod method) noexcept
    {
        switch (method)
        {
        case DenoiserMethod::RelaxDiffuse:
            return nrd::Denoiser::RELAX_DIFFUSE;
        case DenoiserMethod::SigmaShadow:
            return nrd::Denoiser::SIGMA_SHADOW;
        default:
            return nrd::Denoiser::REBLUR_DIFFUSE;
        }
    }

    [[nodiscard]] bool IsFiniteDirection(
        const std::array<float, 3>& direction) noexcept
    {
        return std::isfinite(direction[0]) &&
            std::isfinite(direction[1]) &&
            std::isfinite(direction[2]);
    }

    [[nodiscard]] bool ValidateSettings(
        const DenoiserSignalDescription& description,
        const DenoiserSettings& settings,
        std::string& error)
    {
        if (settings.quality > DenoiserQuality::Ultra)
        {
            error = "unknown NRD quality preset";
            return false;
        }
        if (settings.hitDistanceReconstruction >
            DenoiserHitDistanceReconstruction::Area5x5)
        {
            error = "unknown hit distance reconstruction mode";
            return false;
        }
        const uint32_t maximumHistory =
            description.method == DenoiserMethod::RelaxDiffuse
            ? nrd::RELAX_MAX_HISTORY_FRAME_NUM
            : nrd::REBLUR_MAX_HISTORY_FRAME_NUM;
        if (settings.historyLength == 0 || settings.historyLength > maximumHistory)
        {
            error = "history length exceeds the selected NRD method limit";
            return false;
        }
        if (!std::isfinite(settings.disocclusionThreshold) ||
            settings.disocclusionThreshold < 0.001f ||
            settings.disocclusionThreshold > 0.1f)
        {
            error = "disocclusion threshold must be in [0.001, 0.1]";
            return false;
        }
        if (!std::isfinite(settings.antiLagStrength) ||
            settings.antiLagStrength < 0.f || settings.antiLagStrength > 1.f)
        {
            error = "anti lag strength must be in [0, 1]";
            return false;
        }
        if (!std::isfinite(settings.hitDistanceParameters[0]) ||
            !std::isfinite(settings.hitDistanceParameters[1]) ||
            !std::isfinite(settings.hitDistanceParameters[2]) ||
            !(settings.hitDistanceParameters[0] > 0.f) ||
            !(settings.hitDistanceParameters[1] > 0.f) ||
            !(settings.hitDistanceParameters[2] >= 1.f))
        {
            error = "ReBLUR hit distance parameters are invalid";
            return false;
        }
        if (!IsFiniteDirection(settings.lightDirectionWorld))
        {
            error = "light direction must contain finite values";
            return false;
        }
        if (description.type == DenoiserSignalType::SunShadow)
        {
            const float lengthSquared =
                settings.lightDirectionWorld[0] * settings.lightDirectionWorld[0] +
                settings.lightDirectionWorld[1] * settings.lightDirectionWorld[1] +
                settings.lightDirectionWorld[2] * settings.lightDirectionWorld[2];
            if (!(lengthSquared > 1e-8f))
            {
                error = "sun SIGMA requires a nonzero light direction";
                return false;
            }
        }
        if (settings.returnHistoryLength &&
            description.method != DenoiserMethod::ReblurDiffuse)
        {
            error = "history length output is only supported by ReBLUR";
            return false;
        }
        return true;
    }

    struct BindingSignatureEntry
    {
        nvrhi::ITexture* texture = nullptr;
        nrd::DescriptorType descriptorType = nrd::DescriptorType::TEXTURE;

        [[nodiscard]] bool operator==(
            const BindingSignatureEntry& other) const noexcept
        {
            return texture == other.texture &&
                descriptorType == other.descriptorType;
        }
    };

    struct BindingSignature
    {
        uint16_t pipelineIndex = 0;
        uint16_t resourceCount = 0;
        std::array<BindingSignatureEntry, kMaximumDispatchResources> resources{};

        [[nodiscard]] bool operator==(const BindingSignature& other) const noexcept
        {
            if (pipelineIndex != other.pipelineIndex ||
                resourceCount != other.resourceCount)
                return false;
            for (uint32_t index = 0; index < resourceCount; ++index)
            {
                if (!(resources[index] == other.resources[index]))
                    return false;
            }
            return true;
        }
    };

    class NrdDenoiserSignalBackend final : public IDenoiserSignalBackend
    {
    public:
        NrdDenoiserSignalBackend()
        {
            m_Capabilities.backendName = "NVIDIA NRD 4.17.3";
            m_Capabilities.backendAvailable = true;
            m_Capabilities.supportsValidation = true;
            m_Capabilities.supportsDynamicResolution = true;
            m_Capabilities.supportsHitDistanceReconstruction = true;
            m_Capabilities.supportsDisocclusionThreshold = true;
            m_Capabilities.supportsAntiLag = true;
            m_Capabilities.versionMajor = kExpectedNrdVersionMajor;
            m_Capabilities.versionMinor = kExpectedNrdVersionMinor;
            m_Capabilities.versionBuild = kExpectedNrdVersionBuild;
            m_Capabilities.maximumReblurHistoryLength =
                nrd::REBLUR_MAX_HISTORY_FRAME_NUM;
            m_Capabilities.maximumRelaxHistoryLength =
                nrd::RELAX_MAX_HISTORY_FRAME_NUM;
            m_Capabilities.maximumSigmaHistoryLength =
                nrd::SIGMA_MAX_HISTORY_FRAME_NUM;
            m_Capabilities.minimumResolutionScale = 0.25f;
            m_Capabilities.maximumResolutionScale = 1.f;
            m_Capabilities.normalRoughnessFormat =
                nvrhi::Format::RGBA16_SNORM;
        }

        ~NrdDenoiserSignalBackend() override { Shutdown(); }

        [[nodiscard]] const DenoiserBackendCapabilities& GetCapabilities()
            const noexcept override { return m_Capabilities; }
        [[nodiscard]] DenoiserMemoryStats GetMemoryStats() const noexcept override
        {
            return m_MemoryStats;
        }

        DenoiserStatus Initialize(
            nvrhi::IDevice* device, uint32_t framesInFlight) override
        {
            if (!device)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                    "NRD initialization requires an NVRHI device");
            }
            if (device->getGraphicsAPI() != nvrhi::GraphicsAPI::D3D12)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::Unsupported,
                    "UVSR supports NRD through NVRHI D3D12 only");
            }
            if (framesInFlight == 0 || framesInFlight > 16)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                    "framesInFlight must be in [1, 16]");
            }

            const std::array<nvrhi::Format, 5> requiredTypedFormats = {
                nvrhi::Format::RGBA16_FLOAT,
                nvrhi::Format::RGBA16_SNORM,
                nvrhi::Format::R32_FLOAT,
                nvrhi::Format::R16_FLOAT,
                nvrhi::Format::R8_UNORM };
            for (nvrhi::Format format : requiredTypedFormats)
            {
                if (!HasNrdTypedUavReadWriteSupport(
                    device->queryFormatSupport(format)))
                {
                    return DenoiserStatus::Error(
                        DenoiserStatusCode::Unsupported,
                        "the adapter lacks a typed UAV load or store required by NRD");
                }
            }
            m_Capabilities.supportsValidation = HasNrdTypedUavReadWriteSupport(
                device->queryFormatSupport(nvrhi::Format::RGBA8_UNORM));

            const nrd::LibraryDesc* library = nrd::GetLibraryDesc();
            if (!library)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "NRD returned no LibraryDesc");
            }
            if (library->versionMajor != kExpectedNrdVersionMajor ||
                library->versionMinor != kExpectedNrdVersionMinor ||
                library->versionBuild != kExpectedNrdVersionBuild)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "NRD runtime does not match pinned v4.17.3");
            }
            if (library->normalEncoding != nrd::NormalEncoding::RGBA16_SNORM ||
                library->roughnessEncoding != nrd::RoughnessEncoding::LINEAR)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "NRD must use RGBA16_SNORM normals and linear roughness");
            }

            const auto supports = [&](nrd::Denoiser denoiser)
            {
                for (uint32_t index = 0;
                    index < library->supportedDenoisersNum; ++index)
                {
                    if (library->supportedDenoisers[index] == denoiser)
                        return true;
                }
                return false;
            };
            const bool reblurDiffuse = supports(nrd::Denoiser::REBLUR_DIFFUSE);
            const bool relaxDiffuse = supports(nrd::Denoiser::RELAX_DIFFUSE);
            const bool sigmaShadow = supports(nrd::Denoiser::SIGMA_SHADOW);
            m_Capabilities.supportsReblur = reblurDiffuse;
            m_Capabilities.supportsRelax = relaxDiffuse;
            m_Capabilities.supportsSigma = sigmaShadow;
            m_Capabilities.supportsAmbientOcclusion = reblurDiffuse;
            m_Capabilities.supportsDiffuseGi = reblurDiffuse || relaxDiffuse;
            m_Capabilities.supportsSkyVisibility = reblurDiffuse || relaxDiffuse;
            m_Capabilities.supportsSunShadow = sigmaShadow;
            m_Capabilities.supportsFlashlightShadow = sigmaShadow;
            if (!reblurDiffuse || !relaxDiffuse || !sigmaShadow)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::Unsupported,
                    "the pinned NRD library omitted a required denoiser");
            }

            Shutdown();
            m_Device = device;
            m_FramesInFlight = framesInFlight;
            m_Initialized = true;
            return DenoiserStatus::Ok();
        }

        DenoiserStatus CreatePoolTexture(
            const nrd::TextureDesc& source,
            bool permanent,
            uint32_t index,
            nvrhi::TextureHandle& output)
        {
            const nvrhi::Format format = ToNvrhiFormat(source.format);
            if (format == nvrhi::Format::UNKNOWN ||
                source.downsampleFactor == 0)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::Unsupported,
                    "NRD requested an unsupported pool texture format");
            }

            nvrhi::TextureDesc desc;
            desc.width = DivideUp(m_Description.allocationExtent.width,
                source.downsampleFactor);
            desc.height = DivideUp(m_Description.allocationExtent.height,
                source.downsampleFactor);
            desc.format = format;
            desc.dimension = nvrhi::TextureDimension::Texture2D;
            desc.mipLevels = 1;
            desc.sampleCount = 1;
            desc.isUAV = true;
            desc.initialState = nvrhi::ResourceStates::ShaderResource;
            desc.keepInitialState = true;
            desc.debugName = std::string(permanent
                ? "NRD/Permanent" : "NRD/Transient") + std::to_string(index);
            output = m_Device->createTexture(desc);
            if (!output)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "failed to allocate an NRD pool texture");
            }

            const uint64_t bytes =
                m_Device->getTextureMemoryRequirements(output).size;
            uint64_t& total = permanent
                ? m_MemoryStats.permanentBytes
                : m_MemoryStats.transientBytes;
            total = bytes > std::numeric_limits<uint64_t>::max() - total
                ? std::numeric_limits<uint64_t>::max()
                : total + bytes;
            return DenoiserStatus::Ok();
        }

        DenoiserStatus ApplySettings()
        {
            nrd::Result result = nrd::Result::INVALID_ARGUMENT;
            if (m_Description.method == DenoiserMethod::ReblurDiffuse)
            {
                nrd::ReblurSettings reblur{};
                reblur.maxAccumulatedFrameNum = m_Settings.historyLength;
                reblur.hitDistanceParameters.A =
                    m_Settings.hitDistanceParameters[0];
                reblur.hitDistanceParameters.B =
                    m_Settings.hitDistanceParameters[1];
                reblur.hitDistanceParameters.C =
                    m_Settings.hitDistanceParameters[2];
                const DenoiserQualityPreset preset =
                    GetDenoiserQualityPreset(m_Settings.quality);
                reblur.maxStabilizedFrameNum = std::min(
                    m_Settings.historyLength,
                    preset.reblurStabilizationLimit);
                reblur.diffusePrepassBlurRadius =
                    preset.reblurPrepassRadius;
                reblur.maxBlurRadius = preset.reblurMaximumBlurRadius;
                reblur.enableAntiFirefly = preset.reblurAntiFirefly;
                reblur.maxFastAccumulatedFrameNum =
                    m_Settings.historyLength > 1
                    ? std::min(preset.reblurFastHistory,
                        m_Settings.historyLength - 1u)
                    : 0u;
                reblur.historyFixFrameNum =
                    reblur.maxFastAccumulatedFrameNum > 1
                    ? std::min(preset.reblurHistoryFix,
                        reblur.maxFastAccumulatedFrameNum - 1u)
                    : 0u;
                reblur.responsiveAccumulationSettings.minAccumulatedFrameNum =
                    std::min(
                        reblur.responsiveAccumulationSettings.minAccumulatedFrameNum,
                        reblur.historyFixFrameNum);
                reblur.hitDistanceReconstructionMode =
                    ToNrdHitDistanceMode(m_Settings.hitDistanceReconstruction);
                reblur.checkerboardMode = nrd::CheckerboardMode::OFF;
                reblur.returnHistoryLengthInsteadOfOcclusion =
                    m_Settings.returnHistoryLength;
                const float antiLag = m_Settings.antiLagStrength;
                reblur.antilagSettings.luminanceSigmaScale =
                    4.f + (2.f - 4.f) * antiLag;
                reblur.antilagSettings.luminanceSensitivity =
                    12.f + (2.f - 12.f) * antiLag;
                result = nrd::SetDenoiserSettings(
                    *m_Instance, kSignalIdentifier, &reblur);
            }
            else if (m_Description.method == DenoiserMethod::RelaxDiffuse)
            {
                nrd::RelaxSettings relax{};
                relax.diffuseMaxAccumulatedFrameNum = m_Settings.historyLength;
                const DenoiserQualityPreset preset =
                    GetDenoiserQualityPreset(m_Settings.quality);
                relax.atrousIterationNum = preset.relaxAtrousIterations;
                relax.diffusePrepassBlurRadius = preset.relaxPrepassRadius;
                relax.enableAntiFirefly = preset.relaxAntiFirefly;
                relax.diffuseMaxFastAccumulatedFrameNum =
                    m_Settings.historyLength > 1
                    ? std::min(preset.relaxFastHistory,
                        m_Settings.historyLength - 1u)
                    : 0u;
                relax.historyFixFrameNum =
                    relax.diffuseMaxFastAccumulatedFrameNum > 1
                    ? std::min(preset.relaxHistoryFix,
                        relax.diffuseMaxFastAccumulatedFrameNum - 1u)
                    : 0u;
                relax.hitDistanceReconstructionMode =
                    ToNrdHitDistanceMode(m_Settings.hitDistanceReconstruction);
                relax.checkerboardMode = nrd::CheckerboardMode::OFF;
                relax.antilagSettings.accelerationAmount =
                    m_Settings.antiLagStrength;
                result = nrd::SetDenoiserSettings(
                    *m_Instance, kSignalIdentifier, &relax);
            }
            else if (m_Description.method == DenoiserMethod::SigmaShadow)
            {
                nrd::SigmaSettings sigma{};
                if (m_Description.type == DenoiserSignalType::SunShadow)
                {
                    const float length = std::sqrt(
                        m_Settings.lightDirectionWorld[0] *
                            m_Settings.lightDirectionWorld[0] +
                        m_Settings.lightDirectionWorld[1] *
                            m_Settings.lightDirectionWorld[1] +
                        m_Settings.lightDirectionWorld[2] *
                            m_Settings.lightDirectionWorld[2]);
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        sigma.lightDirection[axis] =
                            m_Settings.lightDirectionWorld[axis] / length;
                    }
                    sigma.maxStabilizedFrameNum = GetDenoiserQualityPreset(
                        m_Settings.quality).sigmaSunStabilization;
                }
                else
                {
                    // Local light reprojection is intentionally disabled. Its
                    // rapidly changing finite cone remains spatially denoised.
                    sigma.maxStabilizedFrameNum = 0;
                }
                result = nrd::SetDenoiserSettings(
                    *m_Instance, kSignalIdentifier, &sigma);
            }

            return result == nrd::Result::SUCCESS
                ? DenoiserStatus::Ok()
                : NrdFailure(DenoiserStatusCode::InitializationFailed,
                    "nrd::SetDenoiserSettings", result);
        }

        [[nodiscard]] nrd::CommonSettings BuildCommonSettings(
            const DenoiserFrameSettings& frame,
            DenoiserHistoryReset reset,
            bool validation) const noexcept
        {
            nrd::CommonSettings common{};
            std::memcpy(common.viewToClipMatrix,
                frame.viewToClip.columnMajor.data(),
                sizeof(common.viewToClipMatrix));
            std::memcpy(common.viewToClipMatrixPrev,
                frame.viewToClipPrev.columnMajor.data(),
                sizeof(common.viewToClipMatrixPrev));
            std::memcpy(common.worldToViewMatrix,
                frame.worldToView.columnMajor.data(),
                sizeof(common.worldToViewMatrix));
            std::memcpy(common.worldToViewMatrixPrev,
                frame.worldToViewPrev.columnMajor.data(),
                sizeof(common.worldToViewMatrixPrev));

            const DenoiserExtent rect = frame.rectExtent.IsValid()
                ? frame.rectExtent : m_Description.allocationExtent;
            const DenoiserExtent rectPrev = frame.rectExtentPrev.IsValid()
                ? frame.rectExtentPrev : rect;
            const auto motionScale = GetDenoiserScreenMotionScale(rect);
            const auto jitter = GetDenoiserCameraJitter(
                frame.cameraJitterPixels);
            const auto jitterPrev = GetDenoiserCameraJitter(
                frame.cameraJitterPixelsPrev);
            std::copy(motionScale.begin(), motionScale.end(),
                common.motionVectorScale);
            std::copy(jitter.begin(), jitter.end(), common.cameraJitter);
            std::copy(jitterPrev.begin(), jitterPrev.end(),
                common.cameraJitterPrev);

            common.resourceSize[0] =
                static_cast<uint16_t>(m_Description.allocationExtent.width);
            common.resourceSize[1] =
                static_cast<uint16_t>(m_Description.allocationExtent.height);
            common.resourceSizePrev[0] = common.resourceSize[0];
            common.resourceSizePrev[1] = common.resourceSize[1];
            common.rectSize[0] = static_cast<uint16_t>(rect.width);
            common.rectSize[1] = static_cast<uint16_t>(rect.height);
            common.rectSizePrev[0] = static_cast<uint16_t>(rectPrev.width);
            common.rectSizePrev[1] = static_cast<uint16_t>(rectPrev.height);
            common.viewZScale = frame.viewZScale;
            common.timeDeltaBetweenFrames = frame.timeDeltaMilliseconds;
            common.denoisingRange = frame.denoisingRange;
            common.disocclusionThreshold = m_Settings.disocclusionThreshold;
            common.disocclusionThresholdAlternate =
                m_Settings.disocclusionThreshold;
            common.frameIndex = static_cast<uint32_t>(frame.frameIndex);
            common.accumulationMode = ToNrdReset(reset);
            common.isMotionVectorInWorldSpace = false;
            common.isHistoryConfidenceAvailable = false;
            common.isDisocclusionThresholdMixAvailable = false;
            common.enableValidation = validation;
            return common;
        }

        DenoiserStatus PrewarmBindings()
        {
            m_CachedBindings.clear();
            const uint32_t validationModeCount =
                m_Resources.validationOutput ? 2u : 1u;
            for (uint32_t validationMode = 0;
                validationMode < validationModeCount; ++validationMode)
            {
                for (uint32_t phase = 0; phase < 2; ++phase)
                {
                    DenoiserFrameSettings frame;
                    frame.frameIndex = phase;
                    frame.enableValidation = validationMode != 0;
                    const DenoiserHistoryReset reset = phase == 0
                        ? DenoiserHistoryReset::ClearAndRestart
                        : DenoiserHistoryReset::None;
                    nrd::CommonSettings common = BuildCommonSettings(
                        frame, reset, frame.enableValidation);
                    nrd::Result result =
                        nrd::SetCommonSettings(*m_Instance, common);
                    if (result != nrd::Result::SUCCESS)
                    {
                        return NrdFailure(
                            DenoiserStatusCode::InitializationFailed,
                            "nrd::SetCommonSettings during descriptor preparation",
                            result);
                    }

                    const nrd::DispatchDesc* dispatches = nullptr;
                    uint32_t dispatchCount = 0;
                    result = nrd::GetComputeDispatches(*m_Instance,
                        &kSignalIdentifier, 1, dispatches, dispatchCount);
                    if (result != nrd::Result::SUCCESS || !dispatches)
                    {
                        return NrdFailure(
                            DenoiserStatusCode::InitializationFailed,
                            "nrd::GetComputeDispatches during descriptor preparation",
                            result);
                    }
                    for (uint32_t dispatchIndex = 0;
                        dispatchIndex < dispatchCount; ++dispatchIndex)
                    {
                        if (!FindOrCreateBinding(
                            dispatches[dispatchIndex], true))
                        {
                            return DenoiserStatus::Error(
                                DenoiserStatusCode::InitializationFailed,
                                "failed to prepare an NRD descriptor signature");
                        }
                    }
                }
            }
            return DenoiserStatus::Ok();
        }

        [[nodiscard]] nvrhi::ITexture* ResolveResource(
            const nrd::ResourceDesc& resource) const noexcept
        {
            if (resource.type == nrd::ResourceType::PERMANENT_POOL)
            {
                return resource.indexInPool < m_PermanentPool.size()
                    ? m_PermanentPool[resource.indexInPool].Get() : nullptr;
            }
            if (resource.type == nrd::ResourceType::TRANSIENT_POOL)
            {
                return resource.indexInPool < m_TransientPool.size()
                    ? m_TransientPool[resource.indexInPool].Get() : nullptr;
            }
            switch (resource.type)
            {
            case nrd::ResourceType::IN_MV:
                return m_Resources.motionVectors;
            case nrd::ResourceType::IN_NORMAL_ROUGHNESS:
                return m_Resources.normalRoughness;
            case nrd::ResourceType::IN_VIEWZ:
                return m_Resources.viewZ;
            case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:
                return m_Resources.noisyRadianceHitDistance;
            case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:
                return m_Resources.denoisedRadianceHitDistance;
            case nrd::ResourceType::IN_PENUMBRA:
                return m_Resources.penumbra;
            case nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY:
                return m_Resources.shadow;
            case nrd::ResourceType::OUT_VALIDATION:
                return m_Resources.validationOutput;
            default:
                return nullptr;
            }
        }

        [[nodiscard]] bool BuildBindingSignature(
            const nrd::DispatchDesc& dispatch,
            BindingSignature& signature) const noexcept
        {
            if (dispatch.pipelineIndex >= m_Pipelines.size() ||
                dispatch.resourcesNum > kMaximumDispatchResources)
                return false;
            signature = {};
            signature.pipelineIndex = dispatch.pipelineIndex;
            signature.resourceCount =
                static_cast<uint16_t>(dispatch.resourcesNum);
            for (uint32_t index = 0; index < dispatch.resourcesNum; ++index)
            {
                nvrhi::ITexture* texture =
                    ResolveResource(dispatch.resources[index]);
                if (!texture)
                    return false;
                signature.resources[index] = {
                    texture, dispatch.resources[index].descriptorType };
            }
            return true;
        }

        [[nodiscard]] nvrhi::IBindingSet* FindOrCreateBinding(
            const nrd::DispatchDesc& dispatch,
            bool allowCreate)
        {
            BindingSignature signature;
            if (!BuildBindingSignature(dispatch, signature))
                return nullptr;
            for (CachedBinding& cached : m_CachedBindings)
            {
                if (cached.signature == signature)
                    return cached.bindingSet;
            }
            if (!allowCreate)
                return nullptr;

            const nrd::PipelineDesc& pipeline =
                m_InstanceDesc->pipelines[dispatch.pipelineIndex];
            nvrhi::BindingSetDesc bindingDesc;
            uint32_t resourceIndex = 0;
            for (uint32_t rangeIndex = 0;
                rangeIndex < pipeline.resourceRangesNum; ++rangeIndex)
            {
                const nrd::ResourceRangeDesc& range =
                    pipeline.resourceRanges[rangeIndex];
                for (uint32_t descriptorIndex = 0;
                    descriptorIndex < range.descriptorsNum;
                    ++descriptorIndex)
                {
                    if (resourceIndex >= dispatch.resourcesNum)
                        return nullptr;
                    const nrd::ResourceDesc& resource =
                        dispatch.resources[resourceIndex++];
                    if (resource.descriptorType != range.descriptorType)
                        return nullptr;
                    nvrhi::ITexture* texture = ResolveResource(resource);
                    if (!texture)
                        return nullptr;
                    const uint32_t slot =
                        m_InstanceDesc->resourcesBaseRegisterIndex +
                        descriptorIndex;
                    bindingDesc.bindings.push_back(
                        range.descriptorType == nrd::DescriptorType::TEXTURE
                        ? nvrhi::BindingSetItem::Texture_SRV(slot, texture)
                        : nvrhi::BindingSetItem::Texture_UAV(slot, texture));
                }
            }
            if (resourceIndex != dispatch.resourcesNum)
                return nullptr;
            nvrhi::BindingSetHandle binding = m_Device->createBindingSet(
                bindingDesc,
                m_Pipelines[dispatch.pipelineIndex].resourceLayout);
            if (!binding)
                return nullptr;
            CachedBinding& cached = m_CachedBindings.emplace_back();
            cached.signature = signature;
            cached.bindingSet = binding;
            return cached.bindingSet;
        }

        DenoiserStatus ConfigureSignal(
            const DenoiserSignalDescription& description,
            const DenoiserSignalResources& resources,
            const DenoiserSettings& settings) override
        {
            if (!m_Initialized || !m_Device)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                    "Initialize must succeed before ConfigureSignal");
            }
            if (DenoiserStatus status =
                ValidateDenoiserResourceAliases(resources); !status)
                return status;
            if (DenoiserStatus status = ValidateDenoiserSignalContract(
                description, DescribeDenoiserTextures(resources)); !status)
                return status;
            if (resources.validationOutput &&
                !m_Capabilities.supportsValidation)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::Unsupported,
                    "NRD validation requires RGBA8_UNORM typed UAV support");
            }

            std::string settingsError;
            if (!ValidateSettings(description, settings, settingsError))
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InvalidArgument,
                    std::move(settingsError));
            }

            const bool signalUnchanged = m_Instance &&
                description == m_Description &&
                ResourcesEqual(resources, m_Resources);
            if (signalUnchanged && settings == m_Settings)
                return DenoiserStatus::Ok();
            if (signalUnchanged)
            {
                const DenoiserSettings previous = m_Settings;
                m_Settings = settings;
                DenoiserStatus status = ApplySettings();
                if (!status)
                {
                    m_Settings = previous;
                    if (!ApplySettings())
                        ReleaseSignal();
                    return status;
                }
                status = PrewarmBindings();
                if (!status)
                {
                    m_Settings = previous;
                    if (!ApplySettings() || !PrewarmBindings())
                        ReleaseSignal();
                    return status;
                }
                RequestHistoryReset(DenoiserHistoryReset::Restart);
                return DenoiserStatus::Ok();
            }

            ReleaseSignal();
            m_Description = description;
            m_Resources = resources;
            m_Settings = settings;

            const nrd::DenoiserDesc denoiser = {
                kSignalIdentifier, ToNrdDenoiser(description.method) };
            nrd::InstanceCreationDesc creation{};
            creation.denoisers = &denoiser;
            creation.denoisersNum = 1;
            const nrd::Result result = nrd::CreateInstance(creation, m_Instance);
            if (result != nrd::Result::SUCCESS || !m_Instance)
            {
                ReleaseSignal();
                return NrdFailure(DenoiserStatusCode::InitializationFailed,
                    "nrd::CreateInstance", result);
            }
            m_InstanceDesc = nrd::GetInstanceDesc(*m_Instance);
            if (!m_InstanceDesc)
            {
                ReleaseSignal();
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "NRD returned no InstanceDesc");
            }
            if (DenoiserStatus status = CreateGpuObjects(); !status)
            {
                ReleaseSignal();
                return status;
            }
            if (DenoiserStatus status = ApplySettings(); !status)
            {
                ReleaseSignal();
                return status;
            }
            if (DenoiserStatus status = PrewarmBindings(); !status)
            {
                ReleaseSignal();
                return status;
            }
            m_PendingReset = DenoiserHistoryReset::ClearAndRestart;
            m_HasExecutedFrame = false;
            return DenoiserStatus::Ok();
        }

        void RequestHistoryReset(DenoiserHistoryReset reset) noexcept override
        {
            m_PendingReset = MergeDenoiserHistoryReset(m_PendingReset, reset);
        }

        DenoiserStatus Execute(
            nvrhi::ICommandList* commandList,
            const DenoiserFrameSettings& frame) override
        {
            if (!m_Instance || !m_InstanceDesc || !commandList)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                    "NRD Execute requires a configured signal and command list");
            }
            if (DenoiserStatus status =
                ValidateDenoiserFrameSettings(m_Description, frame); !status)
                return status;
            if (frame.enableValidation && !m_Resources.validationOutput)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::InvalidArgument,
                    "NRD validation needs a validation output texture");
            }
            if (frame.enableValidation && !m_Capabilities.supportsValidation)
            {
                return DenoiserStatus::Error(DenoiserStatusCode::Unsupported,
                    "NRD validation is not supported on this adapter");
            }
            if (m_HasExecutedFrame && !IsConsecutiveDenoiserFrame(
                m_PreviousFrameIndex, frame.frameIndex))
            {
                RequestHistoryReset(DenoiserHistoryReset::Restart);
            }

            nrd::CommonSettings common = BuildCommonSettings(
                frame, m_PendingReset, frame.enableValidation);
            nrd::Result result = nrd::SetCommonSettings(*m_Instance, common);
            if (result != nrd::Result::SUCCESS)
            {
                return NrdFailure(DenoiserStatusCode::ExecutionFailed,
                    "nrd::SetCommonSettings", result);
            }

            const nrd::DispatchDesc* dispatches = nullptr;
            uint32_t dispatchCount = 0;
            result = nrd::GetComputeDispatches(*m_Instance,
                &kSignalIdentifier, 1, dispatches, dispatchCount);
            if (result != nrd::Result::SUCCESS ||
                !dispatches || dispatchCount == 0)
            {
                return NrdFailure(DenoiserStatusCode::ExecutionFailed,
                    "nrd::GetComputeDispatches", result);
            }

            uint32_t primedDispatch = std::numeric_limits<uint32_t>::max();
            for (uint32_t index = 0; index < dispatchCount; ++index)
            {
                if (dispatches[index].constantBufferDataSize > 0)
                {
                    commandList->writeBuffer(m_ConstantBuffer,
                        dispatches[index].constantBufferData,
                        dispatches[index].constantBufferDataSize);
                    primedDispatch = index;
                    break;
                }
            }
            if (primedDispatch == std::numeric_limits<uint32_t>::max())
            {
                commandList->writeBuffer(m_ConstantBuffer,
                    m_ZeroConstants.data(), m_ZeroConstants.size());
            }

            commandList->beginMarker("NVIDIA NRD");
            bool constantsInitialized =
                primedDispatch != std::numeric_limits<uint32_t>::max();
            for (uint32_t index = 0; index < dispatchCount; ++index)
            {
                const nrd::DispatchDesc& dispatch = dispatches[index];
                if (dispatch.pipelineIndex >= m_Pipelines.size())
                {
                    commandList->endMarker();
                    return DenoiserStatus::Error(
                        DenoiserStatusCode::ExecutionFailed,
                        "NRD returned an invalid pipeline index");
                }
                if (dispatch.constantBufferDataSize > 0 &&
                    index != primedDispatch &&
                    (!dispatch.constantBufferDataMatchesPreviousDispatch ||
                        !constantsInitialized))
                {
                    commandList->writeBuffer(m_ConstantBuffer,
                        dispatch.constantBufferData,
                        dispatch.constantBufferDataSize);
                    constantsInitialized = true;
                }

                nvrhi::IBindingSet* bindings =
                    FindOrCreateBinding(dispatch, false);
                if (!bindings)
                {
                    commandList->endMarker();
                    return DenoiserStatus::Error(
                        DenoiserStatusCode::ExecutionFailed,
                        "NRD requested a descriptor signature that was not prepared");
                }

                nvrhi::ComputeState state;
                state.pipeline = m_Pipelines[dispatch.pipelineIndex].pipeline;
                state.bindings = { m_CommonBindingSet, bindings };
                commandList->beginMarker(
                    dispatch.name ? dispatch.name : "NRD dispatch");
                commandList->setComputeState(state);
                commandList->dispatch(dispatch.gridWidth, dispatch.gridHeight, 1);
                commandList->endMarker();
            }
            commandList->endMarker();

            m_PendingReset = DenoiserHistoryReset::None;
            m_PreviousFrameIndex = frame.frameIndex;
            m_HasExecutedFrame = true;
            return DenoiserStatus::Ok();
        }

        void Shutdown() noexcept override
        {
            ReleaseSignal();
            m_Device = nullptr;
            m_FramesInFlight = 0;
            m_Initialized = false;
        }

    private:
        struct Pipeline
        {
            nvrhi::ShaderHandle shader;
            nvrhi::BindingLayoutHandle resourceLayout;
            nvrhi::ComputePipelineHandle pipeline;
        };

        struct CachedBinding
        {
            BindingSignature signature;
            nvrhi::BindingSetHandle bindingSet;
        };

        DenoiserBackendCapabilities m_Capabilities;
        DenoiserMemoryStats m_MemoryStats;
        nvrhi::DeviceHandle m_Device;
        uint32_t m_FramesInFlight = 0;
        bool m_Initialized = false;

        nrd::Instance* m_Instance = nullptr;
        const nrd::InstanceDesc* m_InstanceDesc = nullptr;
        DenoiserSignalDescription m_Description;
        DenoiserSignalResources m_Resources;
        DenoiserSettings m_Settings;

        std::vector<nvrhi::TextureHandle> m_PermanentPool;
        std::vector<nvrhi::TextureHandle> m_TransientPool;
        nvrhi::BufferHandle m_ConstantBuffer;
        std::vector<uint8_t> m_ZeroConstants;
        std::vector<nvrhi::SamplerHandle> m_Samplers;
        nvrhi::BindingLayoutHandle m_CommonLayout;
        nvrhi::BindingSetHandle m_CommonBindingSet;
        std::vector<Pipeline> m_Pipelines;
        std::vector<CachedBinding> m_CachedBindings;

        DenoiserHistoryReset m_PendingReset = DenoiserHistoryReset::None;
        uint64_t m_PreviousFrameIndex = 0;
        bool m_HasExecutedFrame = false;

        void ReleaseSignal() noexcept
        {
            m_CachedBindings.clear();
            m_Pipelines.clear();
            m_CommonBindingSet = nullptr;
            m_CommonLayout = nullptr;
            m_Samplers.clear();
            m_ZeroConstants.clear();
            m_ConstantBuffer = nullptr;
            m_TransientPool.clear();
            m_PermanentPool.clear();
            m_MemoryStats = {};
            if (m_Instance)
                nrd::DestroyInstance(*m_Instance);
            m_Instance = nullptr;
            m_InstanceDesc = nullptr;
            m_Description = {};
            m_Resources = {};
            m_Settings = {};
            m_PendingReset = DenoiserHistoryReset::None;
            m_HasExecutedFrame = false;
        }

        DenoiserStatus CreateGpuObjects()
        {
            if (!m_InstanceDesc || !m_Device)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "NRD GPU object creation has no instance or device");
            }
            if (m_InstanceDesc->samplersNum == 0 ||
                m_InstanceDesc->constantBufferMaxDataSize == 0)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "NRD InstanceDesc omitted required samplers or constants");
            }

            m_PermanentPool.resize(m_InstanceDesc->permanentPoolSize);
            for (uint32_t index = 0;
                index < m_InstanceDesc->permanentPoolSize; ++index)
            {
                if (DenoiserStatus status = CreatePoolTexture(
                    m_InstanceDesc->permanentPool[index], true, index,
                    m_PermanentPool[index]); !status)
                    return status;
            }
            m_TransientPool.resize(m_InstanceDesc->transientPoolSize);
            for (uint32_t index = 0;
                index < m_InstanceDesc->transientPoolSize; ++index)
            {
                if (DenoiserStatus status = CreatePoolTexture(
                    m_InstanceDesc->transientPool[index], false, index,
                    m_TransientPool[index]); !status)
                    return status;
            }

            nvrhi::BufferDesc constantDesc;
            constantDesc.byteSize = m_InstanceDesc->constantBufferMaxDataSize;
            constantDesc.isConstantBuffer = true;
            constantDesc.isVolatile = true;
            constantDesc.maxVersions = std::max(16u,
                m_InstanceDesc->descriptorPoolDesc.setsMaxNum * m_FramesInFlight);
            constantDesc.debugName = "NRD/DispatchConstants";
            m_ConstantBuffer = m_Device->createBuffer(constantDesc);
            if (!m_ConstantBuffer)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "failed to create the NRD constant buffer");
            }
            m_ZeroConstants.assign(
                m_InstanceDesc->constantBufferMaxDataSize, 0u);

            m_Samplers.resize(m_InstanceDesc->samplersNum);
            for (uint32_t index = 0; index < m_InstanceDesc->samplersNum; ++index)
            {
                nvrhi::SamplerDesc samplerDesc;
                samplerDesc.setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
                samplerDesc.setAllFilters(
                    m_InstanceDesc->samplers[index] ==
                    nrd::Sampler::LINEAR_CLAMP);
                m_Samplers[index] = m_Device->createSampler(samplerDesc);
                if (!m_Samplers[index])
                {
                    return DenoiserStatus::Error(
                        DenoiserStatusCode::InitializationFailed,
                        "failed to create an NRD sampler");
                }
            }

            nvrhi::BindingLayoutDesc commonLayoutDesc;
            commonLayoutDesc.visibility = nvrhi::ShaderType::Compute;
            commonLayoutDesc.registerSpace =
                m_InstanceDesc->constantBufferAndSamplersSpaceIndex;
            commonLayoutDesc.bindings.push_back(
                nvrhi::BindingLayoutItem::VolatileConstantBuffer(
                    m_InstanceDesc->constantBufferRegisterIndex));
            for (uint32_t index = 0; index < m_InstanceDesc->samplersNum; ++index)
            {
                commonLayoutDesc.bindings.push_back(
                    nvrhi::BindingLayoutItem::Sampler(
                        m_InstanceDesc->samplersBaseRegisterIndex + index));
            }
            m_CommonLayout = m_Device->createBindingLayout(commonLayoutDesc);
            if (!m_CommonLayout)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "failed to create the NRD common binding layout");
            }

            nvrhi::BindingSetDesc commonBindingDesc;
            commonBindingDesc.bindings.push_back(
                nvrhi::BindingSetItem::ConstantBuffer(
                    m_InstanceDesc->constantBufferRegisterIndex,
                    m_ConstantBuffer));
            for (uint32_t index = 0; index < m_Samplers.size(); ++index)
            {
                commonBindingDesc.bindings.push_back(
                    nvrhi::BindingSetItem::Sampler(
                        m_InstanceDesc->samplersBaseRegisterIndex + index,
                        m_Samplers[index]));
            }
            m_CommonBindingSet = m_Device->createBindingSet(
                commonBindingDesc, m_CommonLayout);
            if (!m_CommonBindingSet)
            {
                return DenoiserStatus::Error(
                    DenoiserStatusCode::InitializationFailed,
                    "failed to create the NRD common binding set");
            }

            m_Pipelines.resize(m_InstanceDesc->pipelinesNum);
            for (uint32_t pipelineIndex = 0;
                pipelineIndex < m_InstanceDesc->pipelinesNum; ++pipelineIndex)
            {
                const nrd::PipelineDesc& source =
                    m_InstanceDesc->pipelines[pipelineIndex];
                if (!source.computeShaderDXIL.bytecode ||
                    source.computeShaderDXIL.size == 0)
                {
                    return DenoiserStatus::Error(
                        DenoiserStatusCode::InitializationFailed,
                        "the pinned NRD build did not embed DXIL shaders");
                }

                Pipeline& destination = m_Pipelines[pipelineIndex];
                nvrhi::ShaderDesc shaderDesc;
                shaderDesc.shaderType = nvrhi::ShaderType::Compute;
                shaderDesc.entryName = m_InstanceDesc->shaderEntryPoint;
                shaderDesc.debugName =
                    std::string("NRD/") + source.shaderIdentifier;
                destination.shader = m_Device->createShader(shaderDesc,
                    source.computeShaderDXIL.bytecode,
                    static_cast<size_t>(source.computeShaderDXIL.size));
                if (!destination.shader)
                {
                    return DenoiserStatus::Error(
                        DenoiserStatusCode::InitializationFailed,
                        "failed to create an NRD DXIL compute shader");
                }

                nvrhi::BindingLayoutDesc resourceLayoutDesc;
                resourceLayoutDesc.visibility = nvrhi::ShaderType::Compute;
                resourceLayoutDesc.registerSpace =
                    m_InstanceDesc->resourcesSpaceIndex;
                for (uint32_t rangeIndex = 0;
                    rangeIndex < source.resourceRangesNum; ++rangeIndex)
                {
                    const nrd::ResourceRangeDesc& range =
                        source.resourceRanges[rangeIndex];
                    for (uint32_t descriptorIndex = 0;
                        descriptorIndex < range.descriptorsNum;
                        ++descriptorIndex)
                    {
                        const uint32_t slot =
                            m_InstanceDesc->resourcesBaseRegisterIndex +
                            descriptorIndex;
                        resourceLayoutDesc.bindings.push_back(
                            range.descriptorType == nrd::DescriptorType::TEXTURE
                            ? nvrhi::BindingLayoutItem::Texture_SRV(slot)
                            : nvrhi::BindingLayoutItem::Texture_UAV(slot));
                    }
                }
                destination.resourceLayout =
                    m_Device->createBindingLayout(resourceLayoutDesc);
                if (!destination.resourceLayout)
                {
                    return DenoiserStatus::Error(
                        DenoiserStatusCode::InitializationFailed,
                        "failed to create an NRD resource binding layout");
                }

                nvrhi::ComputePipelineDesc pipelineDesc;
                pipelineDesc.CS = destination.shader;
                pipelineDesc.bindingLayouts = {
                    m_CommonLayout, destination.resourceLayout };
                destination.pipeline =
                    m_Device->createComputePipeline(pipelineDesc);
                if (!destination.pipeline)
                {
                    return DenoiserStatus::Error(
                        DenoiserStatusCode::InitializationFailed,
                        "failed to create an NRD compute pipeline");
                }
            }
            m_CachedBindings.reserve(std::max(16u,
                m_InstanceDesc->descriptorPoolDesc.setsMaxNum * 4u));
            return DenoiserStatus::Ok();
        }
    };
}

namespace uvsr
{
    bool HasNrdTypedUavReadWriteSupport(
        nvrhi::FormatSupport support) noexcept
    {
        const nvrhi::FormatSupport required =
            nvrhi::FormatSupport::Texture |
            nvrhi::FormatSupport::ShaderLoad |
            nvrhi::FormatSupport::ShaderUavLoad |
            nvrhi::FormatSupport::ShaderUavStore;
        return (support & required) == required;
    }

    std::unique_ptr<IDenoiserSignalBackend>
        CreateCompiledNrdDenoiserSignalBackend()
    {
        return std::make_unique<NrdDenoiserSignalBackend>();
    }
}
