#include "adaptive_msaa_diagnostics_pass.h"
#include "adaptive_msaa_raster_cb.h"

#include <donut/engine/ShaderFactory.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace uvsr::adaptive_msaa;

    inline constexpr uint32_t ExecuteCounters = 1u << 0u;
    inline constexpr uint32_t ExecuteSelectedPixel = 1u << 1u;
    inline constexpr uint32_t ExecuteViewAvailable = 1u << 2u;
    inline constexpr uint32_t ExecuteVisualization = 1u << 3u;
    inline constexpr uint32_t ExecuteReferencePerSample = 1u << 4u;
    inline constexpr uint32_t ErrorExecuteMetrics = 1u << 0u;
    inline constexpr uint32_t ErrorExecuteHistoryWrite = 1u << 1u;
    inline constexpr uint32_t ErrorExecuteHistoryRead = 1u << 2u;

    [[nodiscard]] bool TryMultiply(
        uint64_t first,
        uint64_t second,
        uint64_t& output)
    {
        output = 0u;
        if (first == 0u || second == 0u)
            return true;
        if (first > std::numeric_limits<uint64_t>::max() / second)
            return false;
        output = first * second;
        return true;
    }

    [[nodiscard]] bool IsValidComparisonMode(
        DiagnosticsComparisonMode mode)
    {
        return static_cast<uint32_t>(mode) <
            static_cast<uint32_t>(DiagnosticsComparisonMode::Count);
    }

    [[nodiscard]] bool IsValidDebugView(AdaptiveMsaaDebugView view)
    {
        return static_cast<uint32_t>(view) <
            static_cast<uint32_t>(AdaptiveMsaaDebugView::Count);
    }

    [[nodiscard]] bool IsValidGpuErrorDomain(
        DiagnosticsGpuErrorDomain domain)
    {
        return static_cast<uint32_t>(domain) <
            static_cast<uint32_t>(DiagnosticsGpuErrorDomain::Count);
    }

    [[nodiscard]] bool IsValidReconstructionChannel(
        DiagnosticsReconstructionChannel channel)
    {
        return static_cast<uint32_t>(channel) <
            static_cast<uint32_t>(DiagnosticsReconstructionChannel::Count);
    }

    [[nodiscard]] bool SameHistoryKey(
        const DiagnosticsGpuHistoryKey& first,
        const DiagnosticsGpuHistoryKey& second)
    {
        return first.width == second.width &&
            first.height == second.height &&
            first.sampleCount == second.sampleCount &&
            first.reserved == second.reserved &&
            first.sceneEpoch == second.sceneEpoch &&
            first.modeEpoch == second.modeEpoch;
    }

    [[nodiscard]] bool IsSinglePlaneTexture(
        const DiagnosticsGpuTextureDesc& texture)
    {
        return texture.depth == 1u &&
            texture.arraySize == 1u &&
            texture.mipLevels == 1u;
    }

    [[nodiscard]] DiagnosticsPassStatus ValidateRequiredMsaaTexture(
        const DiagnosticsGpuTextureDesc& texture,
        DiagnosticsGpuTextureFormat format)
    {
        if (!texture.supplied)
            return DiagnosticsPassStatus::MissingRequiredInput;
        if (texture.width == 0u || texture.height == 0u ||
            !IsSinglePlaneTexture(texture))
        {
            return DiagnosticsPassStatus::InvalidDimensions;
        }
        if (texture.dimension !=
                DiagnosticsGpuTextureDimension::Texture2DMS ||
            !texture.shaderResource)
        {
            return DiagnosticsPassStatus::IncompatibleResources;
        }
        if (texture.format != format)
            return DiagnosticsPassStatus::InvalidFormat;
        return DiagnosticsPassStatus::Success;
    }

    [[nodiscard]] DiagnosticsPassStatus ValidateOptionalSingleTexture(
        const DiagnosticsGpuTextureDesc& texture,
        uint32_t width,
        uint32_t height)
    {
        if (!texture.supplied)
            return DiagnosticsPassStatus::Success;
        if (texture.width != width || texture.height != height ||
            !IsSinglePlaneTexture(texture) || texture.sampleCount != 1u ||
            texture.sampleQuality != 0u)
        {
            return DiagnosticsPassStatus::InvalidDimensions;
        }
        if (texture.dimension !=
                DiagnosticsGpuTextureDimension::Texture2D ||
            !texture.shaderResource)
        {
            return DiagnosticsPassStatus::IncompatibleResources;
        }
        if (texture.format != DiagnosticsGpuTextureFormat::Rgba16Float)
            return DiagnosticsPassStatus::InvalidFormat;
        return DiagnosticsPassStatus::Success;
    }

    [[nodiscard]] DiagnosticsPassStatus ValidateOptionalTypedTexture(
        const DiagnosticsGpuTextureDesc& texture,
        uint32_t width,
        uint32_t height,
        DiagnosticsGpuTextureFormat format)
    {
        if (!texture.supplied)
            return DiagnosticsPassStatus::Success;
        if (texture.width != width || texture.height != height ||
            !IsSinglePlaneTexture(texture) || texture.sampleCount != 1u ||
            texture.sampleQuality != 0u)
        {
            return DiagnosticsPassStatus::InvalidDimensions;
        }
        if (texture.dimension !=
                DiagnosticsGpuTextureDimension::Texture2D ||
            !texture.shaderResource)
        {
            return DiagnosticsPassStatus::IncompatibleResources;
        }
        if (texture.format != format)
            return DiagnosticsPassStatus::InvalidFormat;
        return DiagnosticsPassStatus::Success;
    }

    [[nodiscard]] DiagnosticsPassStatus ValidateReconstructionPair(
        const DiagnosticsGpuTextureDesc& reference,
        const DiagnosticsGpuTextureDesc& reconstructed,
        uint32_t width,
        uint32_t height,
        DiagnosticsGpuTextureFormat format)
    {
        if (reference.supplied != reconstructed.supplied)
            return DiagnosticsPassStatus::MissingRequiredInput;
        DiagnosticsPassStatus status = ValidateOptionalTypedTexture(
            reference, width, height, format);
        if (status != DiagnosticsPassStatus::Success)
            return status;
        return ValidateOptionalTypedTexture(
            reconstructed, width, height, format);
    }

    [[nodiscard]] DiagnosticsPassStatus ValidateOptionalSampleStatusTexture(
        const DiagnosticsGpuTextureDesc& texture,
        const DiagnosticsGpuTextureDesc& reference)
    {
        if (!texture.supplied)
            return DiagnosticsPassStatus::Success;
        if (texture.width != reference.width ||
            texture.height != reference.height ||
            texture.depth != 1u ||
            texture.arraySize != reference.sampleCount ||
            texture.mipLevels != 1u ||
            texture.sampleCount != 1u ||
            texture.sampleQuality != 0u)
        {
            return DiagnosticsPassStatus::IncompatibleResources;
        }
        if (texture.dimension !=
                DiagnosticsGpuTextureDimension::Texture2DArray ||
            !texture.shaderResource)
        {
            return DiagnosticsPassStatus::IncompatibleResources;
        }
        if (texture.format != DiagnosticsGpuTextureFormat::R8Uint)
            return DiagnosticsPassStatus::InvalidFormat;
        return DiagnosticsPassStatus::Success;
    }

    [[nodiscard]] DiagnosticsPassStatus ValidateOptionalVisibilityClass(
        const DiagnosticsGpuTextureDesc& texture,
        const DiagnosticsGpuTextureDesc& reference)
    {
        if (!texture.supplied)
            return DiagnosticsPassStatus::Success;
        if (texture.width != reference.width ||
            texture.height != reference.height ||
            texture.depth != reference.depth ||
            texture.arraySize != reference.arraySize ||
            texture.mipLevels != reference.mipLevels ||
            texture.sampleCount != reference.sampleCount ||
            texture.sampleQuality != reference.sampleQuality)
        {
            return DiagnosticsPassStatus::IncompatibleResources;
        }
        if (texture.dimension !=
                DiagnosticsGpuTextureDimension::Texture2DMS ||
            !texture.shaderResource)
        {
            return DiagnosticsPassStatus::IncompatibleResources;
        }
        if (texture.format != DiagnosticsGpuTextureFormat::R8Uint &&
            texture.format != DiagnosticsGpuTextureFormat::R32Uint)
        {
            return DiagnosticsPassStatus::InvalidFormat;
        }
        return DiagnosticsPassStatus::Success;
    }

    [[nodiscard]] DiagnosticsPassStatus ValidateOutputTexture(
        const DiagnosticsGpuTextureDesc& texture,
        uint32_t width,
        uint32_t height)
    {
        if (!texture.supplied)
            return DiagnosticsPassStatus::MissingRequiredInput;
        if (texture.width != width || texture.height != height ||
            !IsSinglePlaneTexture(texture) || texture.sampleCount != 1u ||
            texture.sampleQuality != 0u)
        {
            return DiagnosticsPassStatus::InvalidDimensions;
        }
        if (texture.dimension !=
                DiagnosticsGpuTextureDimension::Texture2D ||
            !texture.unorderedAccess)
        {
            return DiagnosticsPassStatus::IncompatibleResources;
        }
        if (texture.format != DiagnosticsGpuTextureFormat::Rgba16Float)
            return DiagnosticsPassStatus::InvalidFormat;
        return DiagnosticsPassStatus::Success;
    }

    [[nodiscard]] DiagnosticsPassStatus ValidateOptionalBuffer(
        const DiagnosticsGpuBufferDesc& buffer,
        uint32_t expectedStride,
        uint64_t requiredBytes)
    {
        if (!buffer.supplied)
            return DiagnosticsPassStatus::Success;
        if (buffer.structStride != expectedStride)
            return DiagnosticsPassStatus::InvalidBufferStride;
        if (requiredBytes > DiagnosticsGpuMaximumBufferBytes ||
            buffer.byteSize > DiagnosticsGpuMaximumBufferBytes)
            return DiagnosticsPassStatus::CapacityOverflow;
        if (buffer.byteSize < requiredBytes ||
            buffer.byteSize % expectedStride != 0u)
        {
            return DiagnosticsPassStatus::CapacityOverflow;
        }
        return DiagnosticsPassStatus::Success;
    }

    [[nodiscard]] bool IsReferenceView(AdaptiveMsaaDebugView view)
    {
        return view == AdaptiveMsaaDebugView::CompactReconstructionError ||
            view == AdaptiveMsaaDebugView::AdaptiveLightingError;
    }

    [[nodiscard]] DiagnosticsGpuTextureFormat ConvertFormat(
        nvrhi::Format format)
    {
        if (format == nvrhi::Format::RG32_UINT)
            return DiagnosticsGpuTextureFormat::Rg32Uint;
        if (format == nvrhi::Format::RGBA16_FLOAT)
            return DiagnosticsGpuTextureFormat::Rgba16Float;
        if (format == nvrhi::Format::RGBA32_FLOAT)
            return DiagnosticsGpuTextureFormat::Rgba32Float;
        if (format == nvrhi::Format::R8_UINT)
            return DiagnosticsGpuTextureFormat::R8Uint;
        if (format == nvrhi::Format::RGBA8_UNORM)
            return DiagnosticsGpuTextureFormat::Rgba8Unorm;
        if (format == nvrhi::Format::SRGBA8_UNORM)
            return DiagnosticsGpuTextureFormat::Srgba8Unorm;
        if (format == nvrhi::Format::RGBA16_SNORM)
            return DiagnosticsGpuTextureFormat::Rgba16Snorm;
        if (format == nvrhi::Format::R8_UNORM)
            return DiagnosticsGpuTextureFormat::R8Unorm;
        if (format == nvrhi::Format::RG16_FLOAT)
            return DiagnosticsGpuTextureFormat::Rg16Float;
        if (format == nvrhi::Format::R32_FLOAT)
            return DiagnosticsGpuTextureFormat::R32Float;
        if (format == nvrhi::Format::R16_FLOAT)
            return DiagnosticsGpuTextureFormat::R16Float;
        if (format == nvrhi::Format::R32_UINT)
            return DiagnosticsGpuTextureFormat::R32Uint;
        if (format != nvrhi::Format::UNKNOWN &&
            nvrhi::getFormatInfo(format).hasDepth)
        {
            return DiagnosticsGpuTextureFormat::Depth;
        }
        return DiagnosticsGpuTextureFormat::Unknown;
    }

    [[nodiscard]] DiagnosticsGpuTextureDimension ConvertDimension(
        nvrhi::TextureDimension dimension)
    {
        switch (dimension)
        {
        case nvrhi::TextureDimension::Texture2D:
            return DiagnosticsGpuTextureDimension::Texture2D;
        case nvrhi::TextureDimension::Texture2DMS:
            return DiagnosticsGpuTextureDimension::Texture2DMS;
        case nvrhi::TextureDimension::Texture2DArray:
            return DiagnosticsGpuTextureDimension::Texture2DArray;
        default:
            return DiagnosticsGpuTextureDimension::Unknown;
        }
    }

    [[nodiscard]] DiagnosticsGpuTextureDesc DescribeTexture(
        const nvrhi::ITexture* texture)
    {
        DiagnosticsGpuTextureDesc result;
        if (!texture)
            return result;

        const nvrhi::TextureDesc& source = texture->getDesc();
        result.supplied = true;
        result.width = source.width;
        result.height = source.height;
        result.depth = source.depth;
        result.arraySize = source.arraySize;
        result.mipLevels = source.mipLevels;
        result.sampleCount = source.sampleCount;
        result.sampleQuality = source.sampleQuality;
        result.format = ConvertFormat(source.format);
        result.dimension = ConvertDimension(source.dimension);
        result.shaderResource = source.isShaderResource;
        result.unorderedAccess = source.isUAV;
        return result;
    }

    [[nodiscard]] DiagnosticsGpuBufferDesc DescribeBuffer(
        const nvrhi::IBuffer* buffer)
    {
        DiagnosticsGpuBufferDesc result;
        if (!buffer)
            return result;
        const nvrhi::BufferDesc& source = buffer->getDesc();
        result.supplied = true;
        result.byteSize = source.byteSize;
        result.structStride = source.structStride;
        return result;
    }

    [[nodiscard]] AdaptiveMsaaDiagnosticsPassPlanDesc DescribePlan(
        const AdaptiveMsaaDiagnosticsPassInputs& inputs,
        const AdaptiveMsaaDiagnosticsPassRequest& request)
    {
        AdaptiveMsaaDiagnosticsPassPlanDesc result;
        result.settings = request.settings;
        result.surfaceKeys = DescribeTexture(inputs.surfaceKeys);
        result.depth = DescribeTexture(inputs.depth);
        result.visibilityClass = DescribeTexture(inputs.visibilityClass);
        result.output = DescribeTexture(inputs.output);
        result.referenceHdr = DescribeTexture(inputs.referenceHdr);
        result.adaptiveHdr = DescribeTexture(inputs.adaptiveHdr);
        result.referenceAttributes =
            DescribeTexture(inputs.referenceAttributes);
        result.reconstructedAttributes =
            DescribeTexture(inputs.reconstructedAttributes);
        result.referenceDepth = DescribeTexture(inputs.referenceDepth);
        result.reconstructedDepth =
            DescribeTexture(inputs.reconstructedDepth);
        result.referenceDiffuse = DescribeTexture(inputs.referenceDiffuse);
        result.reconstructedDiffuse =
            DescribeTexture(inputs.reconstructedDiffuse);
        result.referenceMaterial = DescribeTexture(inputs.referenceMaterial);
        result.reconstructedMaterial =
            DescribeTexture(inputs.reconstructedMaterial);
        result.referenceNormals = DescribeTexture(inputs.referenceNormals);
        result.reconstructedNormals =
            DescribeTexture(inputs.reconstructedNormals);
        result.referenceEmissive = DescribeTexture(inputs.referenceEmissive);
        result.reconstructedEmissive =
            DescribeTexture(inputs.reconstructedEmissive);
        result.referenceAmbientOcclusion =
            DescribeTexture(inputs.referenceAmbientOcclusion);
        result.reconstructedAmbientOcclusion =
            DescribeTexture(inputs.reconstructedAmbientOcclusion);
        result.referenceMotion = DescribeTexture(inputs.referenceMotion);
        result.reconstructedMotion =
            DescribeTexture(inputs.reconstructedMotion);
        result.sampleStatus = DescribeTexture(inputs.sampleStatus);
        result.rasterSurfaceHits =
            DescribeTexture(inputs.rasterSurfaceHits);
        result.selectedSampleData =
            DescribeBuffer(inputs.selectedSampleData);
        result.pixelMetadata = DescribeBuffer(inputs.pixelMetadata);
        result.producerCapabilityMask =
            inputs.producerCapabilityMask &
            DiagnosticsCounterAllValidityBits;
        result.referencePerSample = inputs.referencePerSample;
        result.frameIndex = request.frameIndex;
        result.visualizedSampleIndex = request.visualizedSampleIndex;
        result.maximumWorkListCapacity = request.maximumWorkListCapacity;
        result.gpuErrorMetricsEnabled = request.gpuErrorMetricsEnabled;
        result.gpuErrorHistoryEnabled = request.gpuErrorHistoryEnabled;
        result.frozenGpuErrorHistoryReadOnly =
            request.frozenGpuErrorHistoryReadOnly;
        result.resetGpuErrorHistory = request.resetGpuErrorHistory;
        result.visualizedErrorDomain = request.visualizedErrorDomain;
        result.reconstructionChannel = request.reconstructionChannel;
        result.historyIdentity = request.historyIdentity;
        return result;
    }

    [[nodiscard]] uint32_t LowWord(uint64_t value)
    {
        return static_cast<uint32_t>(value);
    }

    [[nodiscard]] uint32_t HighWord(uint64_t value)
    {
        return static_cast<uint32_t>(value >> 32u);
    }

    [[nodiscard]] float FloatFromBits(uint32_t bits)
    {
        float result = 0.f;
        static_assert(sizeof(result) == sizeof(bits));
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
}

namespace uvsr::adaptive_msaa
{
    DiagnosticsVisibilitySampleValidation
        ValidateDiagnosticsVisibilitySample(
            const GpuVisibilityKey& key,
            float deviceDepth,
            bool visibilityClassAvailable,
            uint32_t visibilityClass)
    {
        DiagnosticsVisibilitySampleValidation result;
        const bool clearedKeyDepth = IsClearedVisibilityKey(key) &&
            std::isfinite(deviceDepth) && deviceDepth == 0.f &&
            !std::signbit(deviceDepth);
        result.background = clearedKeyDepth &&
            (!visibilityClassAvailable ||
                visibilityClass ==
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND);
        if (result.background)
            return result;

        result.reason = ValidateGpuVisibilityKey(key);
        if (result.reason != FallbackReason::None)
            return result;

        const AdaptiveMsaaVisibilityClassDecodeResult decoded =
            AdaptiveMsaaDecodeVisibilityClass(visibilityClass);
        const bool validClass = !visibilityClassAvailable || decoded.valid;
        const bool requirePositiveDepth =
            visibilityClassAvailable && decoded.reconstruct;
        if (!std::isfinite(deviceDepth) || deviceDepth < 0.f ||
            deviceDepth > 1.f ||
            (requirePositiveDepth && deviceDepth == 0.f) ||
            !validClass ||
            (visibilityClassAvailable &&
                visibilityClass ==
                    ADAPTIVE_MSAA_VISIBILITY_CLASS_BACKGROUND))
        {
            result.reason = FallbackReason::ReconstructionFailure;
        }
        return result;
    }

    DiagnosticsPassStatus EvaluateDiagnosticsGpuHistoryTransition(
        bool enabled,
        bool explicitReset,
        const DiagnosticsGpuHistoryKey& requestedKey,
        uint64_t frameIndex,
        const DiagnosticsGpuHistoryState& currentState,
        DiagnosticsGpuHistoryTransition& transition)
    {
        transition = DiagnosticsGpuHistoryTransition::Disabled;
        if (!enabled)
            return DiagnosticsPassStatus::Success;
        if (requestedKey.width == 0u || requestedKey.height == 0u ||
            !IsSupportedSampleCount(requestedKey.sampleCount) ||
            requestedKey.reserved != 0u ||
            requestedKey.sceneEpoch == 0u ||
            requestedKey.modeEpoch == 0u)
        {
            return DiagnosticsPassStatus::InvalidHistoryIdentity;
        }
        if (!currentState.valid)
        {
            transition = DiagnosticsGpuHistoryTransition::Initialize;
            return DiagnosticsPassStatus::Success;
        }
        if (explicitReset ||
            !SameHistoryKey(requestedKey, currentState.key) ||
            frameIndex == 0u ||
            frameIndex != currentState.frameIndex + 1u)
        {
            transition = DiagnosticsGpuHistoryTransition::Reset;
            return DiagnosticsPassStatus::Success;
        }
        transition = DiagnosticsGpuHistoryTransition::Continue;
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsPassStatus ValidateDiagnosticsGpuFrozenHistoryRead(
        const DiagnosticsGpuHistoryKey& requestedKey,
        const DiagnosticsGpuHistoryState& currentState)
    {
        if (requestedKey.width == 0u || requestedKey.height == 0u ||
            !IsSupportedSampleCount(requestedKey.sampleCount) ||
            requestedKey.reserved != 0u ||
            requestedKey.sceneEpoch == 0u ||
            requestedKey.modeEpoch == 0u ||
            !currentState.valid ||
            !SameHistoryKey(requestedKey, currentState.key))
        {
            return DiagnosticsPassStatus::InvalidHistoryIdentity;
        }
        return DiagnosticsPassStatus::Success;
    }

    AdaptiveMsaaDiagnosticsPassPlan BuildAdaptiveMsaaDiagnosticsPassPlan(
        const AdaptiveMsaaDiagnosticsPassPlanDesc& description)
    {
        AdaptiveMsaaDiagnosticsPassPlan plan;

        AdaptiveMsaaDiagnosticsSettings sanitized;
        if (SanitizeDiagnosticsSettings(
                description.settings,
                sanitized) != DiagnosticsInputStatus::Success ||
            !IsValidComparisonMode(description.settings.comparisonMode) ||
            !IsValidDebugView(description.settings.debugView) ||
            !IsValidGpuErrorDomain(description.visualizedErrorDomain) ||
            !IsValidReconstructionChannel(
                description.reconstructionChannel) ||
            (description.gpuErrorHistoryEnabled &&
                !description.gpuErrorMetricsEnabled))
        {
            plan.status = DiagnosticsPassStatus::InvalidSettings;
            return plan;
        }
        if (!sanitized.enabled)
        {
            plan.status = DiagnosticsPassStatus::Disabled;
            return plan;
        }

        if (description.frozenGpuErrorHistoryReadOnly &&
            (!description.gpuErrorHistoryEnabled ||
                description.resetGpuErrorHistory ||
                description.visualizedErrorDomain !=
                    DiagnosticsGpuErrorDomain::FrameToFrameTemporal))
        {
            plan.status = DiagnosticsPassStatus::InvalidSettings;
            return plan;
        }

        const AdaptiveMsaaDebugView shaderRequestedView =
            sanitized.debugView == AdaptiveMsaaDebugView::Disabled &&
                RequestsFinalLightingComparison(sanitized)
            ? AdaptiveMsaaDebugView::AdaptiveLightingError
            : sanitized.debugView;
        const bool visualizationRequested =
            sanitized.debugView != AdaptiveMsaaDebugView::Disabled ||
            sanitized.comparisonMode !=
                DiagnosticsComparisonMode::Disabled;
        const bool counterReadbackRequested = sanitized.countersEnabled;
        const bool selectedReadbackRequested =
            sanitized.selectedPixelCaptureRequested;
        const bool anyWork = visualizationRequested ||
            counterReadbackRequested || selectedReadbackRequested ||
            description.gpuErrorMetricsEnabled;
        if (!anyWork)
        {
            plan.status = DiagnosticsPassStatus::NoWork;
            return plan;
        }
        if ((counterReadbackRequested || selectedReadbackRequested) &&
            !sanitized.readbackEnabled)
        {
            plan.status = DiagnosticsPassStatus::ReadbackRequired;
            return plan;
        }

        DiagnosticsPassStatus status = ValidateRequiredMsaaTexture(
            description.surfaceKeys,
            DiagnosticsGpuTextureFormat::Rg32Uint);
        if (status != DiagnosticsPassStatus::Success)
        {
            plan.status = status;
            return plan;
        }
        status = ValidateRequiredMsaaTexture(
            description.depth,
            DiagnosticsGpuTextureFormat::Depth);
        if (status != DiagnosticsPassStatus::Success)
        {
            plan.status = status;
            return plan;
        }

        const DiagnosticsGpuTextureDesc& keys = description.surfaceKeys;
        const DiagnosticsGpuTextureDesc& depth = description.depth;
        if (!IsSupportedSampleCount(keys.sampleCount))
        {
            plan.status = DiagnosticsPassStatus::UnsupportedSampleCount;
            return plan;
        }
        if (keys.width != depth.width || keys.height != depth.height ||
            keys.depth != depth.depth || keys.arraySize != depth.arraySize ||
            keys.mipLevels != depth.mipLevels ||
            keys.sampleCount != depth.sampleCount ||
            keys.sampleQuality != depth.sampleQuality)
        {
            plan.status = DiagnosticsPassStatus::IncompatibleResources;
            return plan;
        }
        status = ValidateOptionalVisibilityClass(
            description.visibilityClass,
            keys);
        if (status != DiagnosticsPassStatus::Success)
        {
            plan.status = status;
            return plan;
        }
        if (description.visualizedSampleIndex >= keys.sampleCount)
        {
            plan.status = DiagnosticsPassStatus::InvalidSampleIndex;
            return plan;
        }
        if (selectedReadbackRequested &&
            (sanitized.selectedPixelX >= keys.width ||
                sanitized.selectedPixelY >= keys.height))
        {
            plan.status = DiagnosticsPassStatus::InvalidSampleIndex;
            return plan;
        }

        if (visualizationRequested)
        {
            status = ValidateOutputTexture(
                description.output,
                keys.width,
                keys.height);
            if (status != DiagnosticsPassStatus::Success)
            {
                plan.status = status;
                return plan;
            }
        }

        const DiagnosticsGpuTextureDesc optionalTextures[] = {
            description.referenceHdr,
            description.adaptiveHdr,
            description.referenceAttributes,
            description.reconstructedAttributes
        };
        for (const DiagnosticsGpuTextureDesc& texture : optionalTextures)
        {
            status = ValidateOptionalSingleTexture(
                texture,
                keys.width,
                keys.height);
            if (status != DiagnosticsPassStatus::Success)
            {
                plan.status = status;
                return plan;
            }
        }

        const struct
        {
            const DiagnosticsGpuTextureDesc* reference;
            const DiagnosticsGpuTextureDesc* reconstructed;
            DiagnosticsGpuTextureFormat format;
        } reconstructionPairs[] = {
            { &description.referenceDepth,
                &description.reconstructedDepth,
                DiagnosticsGpuTextureFormat::R32Float },
            { &description.referenceDiffuse,
                &description.reconstructedDiffuse,
                DiagnosticsGpuTextureFormat::Rgba16Float },
            { &description.referenceMaterial,
                &description.reconstructedMaterial,
                DiagnosticsGpuTextureFormat::Rgba16Float },
            { &description.referenceNormals,
                &description.reconstructedNormals,
                DiagnosticsGpuTextureFormat::Rgba16Float },
            { &description.referenceEmissive,
                &description.reconstructedEmissive,
                DiagnosticsGpuTextureFormat::Rgba16Float },
            { &description.referenceAmbientOcclusion,
                &description.reconstructedAmbientOcclusion,
                DiagnosticsGpuTextureFormat::R16Float },
            { &description.referenceMotion,
                &description.reconstructedMotion,
                DiagnosticsGpuTextureFormat::Rgba16Float }
        };
        bool anyReconstructionInput = false;
        bool completeReconstructionInput = true;
        for (const auto& pair : reconstructionPairs)
        {
            anyReconstructionInput = anyReconstructionInput ||
                pair.reference->supplied || pair.reconstructed->supplied;
            completeReconstructionInput = completeReconstructionInput &&
                pair.reference->supplied && pair.reconstructed->supplied;
            status = ValidateReconstructionPair(
                *pair.reference,
                *pair.reconstructed,
                keys.width,
                keys.height,
                pair.format);
            if (status != DiagnosticsPassStatus::Success)
            {
                plan.status = status;
                return plan;
            }
        }
        if (anyReconstructionInput && !completeReconstructionInput)
        {
            plan.status = DiagnosticsPassStatus::MissingRequiredInput;
            return plan;
        }

        const bool completeReconstructionAvailable =
            sanitized.dualRenderingEnabled && completeReconstructionInput;
        const bool lightingComparisonAvailable =
            sanitized.dualRenderingEnabled &&
            description.referenceHdr.supplied &&
            description.adaptiveHdr.supplied;
        if (description.gpuErrorMetricsEnabled &&
            !completeReconstructionAvailable &&
            !lightingComparisonAvailable)
        {
            plan.status = DiagnosticsPassStatus::MissingRequiredInput;
            return plan;
        }
        if (shaderRequestedView ==
                AdaptiveMsaaDebugView::AdaptiveLightingError &&
            sanitized.comparisonMode ==
                DiagnosticsComparisonMode::ErrorHeatmap &&
            description.visualizedErrorDomain ==
                DiagnosticsGpuErrorDomain::FrameToFrameTemporal &&
            !description.gpuErrorHistoryEnabled)
        {
            plan.status = DiagnosticsPassStatus::InvalidSettings;
            return plan;
        }
        if (description.gpuErrorHistoryEnabled)
        {
            if (!lightingComparisonAvailable)
            {
                plan.status = DiagnosticsPassStatus::MissingRequiredInput;
                return plan;
            }
            DiagnosticsGpuHistoryTransition ignored;
            const DiagnosticsGpuHistoryKey key = {
                keys.width,
                keys.height,
                keys.sampleCount,
                0u,
                description.historyIdentity.sceneEpoch,
                description.historyIdentity.modeEpoch
            };
            const DiagnosticsPassStatus historyStatus =
                EvaluateDiagnosticsGpuHistoryTransition(
                    true,
                    description.resetGpuErrorHistory,
                    key,
                    description.frameIndex,
                    {},
                    ignored);
            if (historyStatus != DiagnosticsPassStatus::Success)
            {
                plan.status = historyStatus;
                return plan;
            }
        }

        uint64_t pixelCount = 0u;
        uint64_t frameSampleCount = 0u;
        uint64_t sampleStatusFootprintBytes = 0u;
        uint64_t requiredSelectedSampleBytes = 0u;
        uint64_t requiredMetadataBytes = 0u;
        uint64_t maximumWorkListTotal = 0u;
        if (!TryMultiply(keys.width, keys.height, pixelCount) ||
            !TryMultiply(pixelCount, keys.sampleCount, frameSampleCount) ||
            !TryMultiply(
                frameSampleCount,
                DiagnosticsGpuSampleStatusBytesPerSample,
                sampleStatusFootprintBytes) ||
            !TryMultiply(
                keys.sampleCount,
                sizeof(DiagnosticsGpuSampleInputRecord),
                requiredSelectedSampleBytes) ||
            !TryMultiply(
                pixelCount,
                sizeof(DiagnosticsGpuPixelMetadataRecord),
                requiredMetadataBytes) ||
            description.maximumWorkListCapacity == 0u ||
            !TryMultiply(
                pixelCount,
                description.maximumWorkListCapacity,
                maximumWorkListTotal) ||
            pixelCount > std::numeric_limits<uint32_t>::max() ||
            frameSampleCount > std::numeric_limits<uint32_t>::max() ||
            requiredSelectedSampleBytes > DiagnosticsGpuMaximumBufferBytes)
        {
            plan.status = DiagnosticsPassStatus::CapacityOverflow;
            return plan;
        }

        status = ValidateOptionalSampleStatusTexture(
            description.sampleStatus,
            keys);
        if (status != DiagnosticsPassStatus::Success)
        {
            plan.status = status;
            return plan;
        }
        if (description.rasterSurfaceHits.supplied)
        {
            status = ValidateRequiredMsaaTexture(
                description.rasterSurfaceHits,
                DiagnosticsGpuTextureFormat::Rgba32Float);
            if (status != DiagnosticsPassStatus::Success)
            {
                plan.status = status;
                return plan;
            }
            const DiagnosticsGpuTextureDesc& hits =
                description.rasterSurfaceHits;
            if (hits.width != keys.width || hits.height != keys.height ||
                hits.depth != keys.depth ||
                hits.arraySize != keys.arraySize ||
                hits.mipLevels != keys.mipLevels ||
                hits.sampleCount != keys.sampleCount ||
                hits.sampleQuality != keys.sampleQuality)
            {
                plan.status = DiagnosticsPassStatus::IncompatibleResources;
                return plan;
            }
        }
        if (description.selectedSampleData.supplied &&
            description.selectedSampleData.byteSize >
                MaximumAdaptiveSampleCount *
                    sizeof(DiagnosticsGpuSampleInputRecord))
        {
            plan.status = DiagnosticsPassStatus::CapacityOverflow;
            return plan;
        }
        status = ValidateOptionalBuffer(
            description.selectedSampleData,
            sizeof(DiagnosticsGpuSampleInputRecord),
            requiredSelectedSampleBytes);
        if (status != DiagnosticsPassStatus::Success)
        {
            plan.status = status;
            return plan;
        }
        status = ValidateOptionalBuffer(
            description.pixelMetadata,
            sizeof(DiagnosticsGpuPixelMetadataRecord),
            requiredMetadataBytes);
        if (status != DiagnosticsPassStatus::Success)
        {
            plan.status = status;
            return plan;
        }
        if (description.pixelMetadata.supplied &&
            maximumWorkListTotal > std::numeric_limits<uint32_t>::max())
        {
            plan.status = DiagnosticsPassStatus::CapacityOverflow;
            return plan;
        }

        // Producer absence is not a mode signal. Adaptive diagnostic work
        // that needs final groups or reasons must have current-frame producer
        // records. Only the explicit unchanged Reference contract may
        // synthesize deterministic per-sample groups in the consumer.
        if (!description.referencePerSample)
        {
            const bool groupViewRequested = sanitized.debugView ==
                AdaptiveMsaaDebugView::UniqueGroupsAndRepresentatives;
            const bool fallbackViewRequested = sanitized.debugView ==
                AdaptiveMsaaDebugView::FallbackReasons;
            if ((selectedReadbackRequested &&
                    !description.selectedSampleData.supplied) ||
                (groupViewRequested &&
                    (!description.pixelMetadata.supplied ||
                        (description.producerCapabilityMask &
                            DiagnosticsCounterGroupHistogramValid) == 0u)) ||
                (fallbackViewRequested &&
                    (!description.sampleStatus.supplied ||
                        (description.producerCapabilityMask &
                            DiagnosticsCounterFallbackReasonsValid) == 0u)) ||
                (counterReadbackRequested &&
                    (!description.sampleStatus.supplied ||
                        !description.pixelMetadata.supplied)))
            {
                plan.status = DiagnosticsPassStatus::MissingRequiredInput;
                return plan;
            }
        }
        else if (selectedReadbackRequested &&
            !description.rasterSurfaceHits.supplied)
        {
            plan.status = DiagnosticsPassStatus::MissingRequiredInput;
            return plan;
        }

        plan.status = DiagnosticsPassStatus::Success;
        plan.operations.allocate = true;
        plan.operations.dispatch = true;
        plan.readbackRequested =
            sanitized.readbackEnabled &&
            (counterReadbackRequested || selectedReadbackRequested);
        plan.operations.copy = plan.readbackRequested;
        plan.operations.query = plan.readbackRequested;
        plan.operations.timerQuery = sanitized.passTimingsEnabled;
        plan.operations.map = false;
        plan.operations.retainDualRenderTargets = false;
        plan.width = keys.width;
        plan.height = keys.height;
        plan.sampleCount = keys.sampleCount;
        plan.sampleQuality = keys.sampleQuality;
        plan.dispatchWidth =
            (keys.width + DiagnosticsGpuThreadGroupWidth - 1u) /
            DiagnosticsGpuThreadGroupWidth;
        plan.dispatchHeight =
            (keys.height + DiagnosticsGpuThreadGroupHeight - 1u) /
            DiagnosticsGpuThreadGroupHeight;
        plan.pixelCount = pixelCount;
        plan.frameSampleCount = frameSampleCount;
        plan.sampleStatusFootprintBytes = sampleStatusFootprintBytes;
        plan.requiredSelectedSampleDataBytes =
            requiredSelectedSampleBytes;
        plan.requiredPixelMetadataBytes = requiredMetadataBytes;
        plan.maximumWorkListCapacity =
            description.maximumWorkListCapacity;
        plan.referencePerSample = description.referencePerSample;
        plan.completeReconstructionAvailable =
            completeReconstructionAvailable;
        plan.lightingComparisonAvailable = lightingComparisonAvailable;
        plan.shaderRequestedView = shaderRequestedView;
        plan.gpuErrorMetricsEnabled =
            description.gpuErrorMetricsEnabled;
        plan.gpuErrorHistoryEnabled =
            description.gpuErrorHistoryEnabled;
        plan.frozenGpuErrorHistoryReadOnly =
            description.frozenGpuErrorHistoryReadOnly;
        plan.resetGpuErrorHistory = description.resetGpuErrorHistory;
        plan.visualizedErrorDomain = description.visualizedErrorDomain;
        plan.reconstructionChannel = description.reconstructionChannel;
        plan.historyIdentity = description.historyIdentity;

        if (sanitized.dualRenderingEnabled &&
            description.referenceHdr.supplied)
        {
            plan.inputAvailabilityMask |= DiagnosticsInputReferenceHdr;
        }
        if (sanitized.dualRenderingEnabled &&
            description.adaptiveHdr.supplied)
        {
            plan.inputAvailabilityMask |= DiagnosticsInputAdaptiveHdr;
        }
        if (sanitized.dualRenderingEnabled &&
            description.referenceAttributes.supplied)
        {
            plan.inputAvailabilityMask |=
                DiagnosticsInputReferenceAttributes;
        }
        if (sanitized.dualRenderingEnabled &&
            description.reconstructedAttributes.supplied)
        {
            plan.inputAvailabilityMask |=
                DiagnosticsInputReconstructedAttributes;
        }
        if (completeReconstructionAvailable)
        {
            plan.inputAvailabilityMask |=
                DiagnosticsInputCompleteReconstruction;
        }
        if (description.sampleStatus.supplied)
            plan.inputAvailabilityMask |= DiagnosticsInputSampleStatus;
        if (description.selectedSampleData.supplied)
        {
            plan.inputAvailabilityMask |=
                DiagnosticsInputSelectedSampleData;
        }
        if (description.pixelMetadata.supplied)
            plan.inputAvailabilityMask |= DiagnosticsInputPixelMetadata;
        if (description.visibilityClass.supplied)
        {
            plan.inputAvailabilityMask |=
                DiagnosticsInputVisibilityClass;
        }
        if (description.rasterSurfaceHits.supplied)
        {
            plan.inputAvailabilityMask |=
                DiagnosticsInputRasterSurfaceHits;
        }

        switch (plan.shaderRequestedView)
        {
        case AdaptiveMsaaDebugView::Disabled:
            plan.viewAvailable = false;
            break;
        case AdaptiveMsaaDebugView::SurfaceOwnershipKeys:
            plan.viewAvailable = true;
            break;
        case AdaptiveMsaaDebugView::UniqueGroupsAndRepresentatives:
            plan.viewAvailable = description.referencePerSample ||
                (plan.inputAvailabilityMask &
                    DiagnosticsInputPixelMetadata) != 0u &&
                (description.producerCapabilityMask &
                    DiagnosticsCounterGroupHistogramValid) != 0u;
            break;
        case AdaptiveMsaaDebugView::FallbackReasons:
            plan.viewAvailable = description.referencePerSample ||
                (plan.inputAvailabilityMask & DiagnosticsInputSampleStatus) !=
                    0u &&
                (description.producerCapabilityMask &
                    DiagnosticsCounterFallbackReasonsValid) != 0u;
            break;
        case AdaptiveMsaaDebugView::CompactReconstructionError:
            plan.viewAvailable = IsReferenceView(
                    plan.shaderRequestedView) &&
                sanitized.comparisonMode !=
                    DiagnosticsComparisonMode::Disabled &&
                plan.completeReconstructionAvailable;
            break;
        case AdaptiveMsaaDebugView::AdaptiveLightingError:
            plan.viewAvailable = IsReferenceView(
                    plan.shaderRequestedView) &&
                sanitized.comparisonMode !=
                    DiagnosticsComparisonMode::Disabled &&
                plan.lightingComparisonAvailable &&
                (sanitized.comparisonMode !=
                        DiagnosticsComparisonMode::ErrorHeatmap ||
                    plan.visualizedErrorDomain ==
                        DiagnosticsGpuErrorDomain::CurrentFrameSpatial ||
                    plan.gpuErrorHistoryEnabled);
            break;
        default:
            plan.viewAvailable = false;
            break;
        }

        if (counterReadbackRequested)
        {
            // Exact key syntax is always observable from the required key
            // attachment. Every producer-derived family additionally needs an
            // explicit current-frame capability bit; resource presence alone
            // does not prove that a shader populated meaningful records. The
            // automatic group gate is the one exception: its extended pixel
            // record carries an in-band validity bit, and the GPU consumer
            // promotes that readback family only after consuming such a
            // record.
            plan.counterValidityMask =
                DiagnosticsCounterInvalidKeysValid;
            if (description.referencePerSample)
            {
                plan.counterValidityMask |=
                    DiagnosticsCounterGroupHistogramValid |
                    DiagnosticsCounterLightingValid |
                    DiagnosticsCounterFallbackReasonsValid;
            }
            if ((plan.inputAvailabilityMask & DiagnosticsInputSampleStatus) !=
                0u)
            {
                plan.counterValidityMask |=
                    description.producerCapabilityMask &
                    (DiagnosticsCounterFallbackReasonsValid |
                        DiagnosticsCounterStaleAndMappingValid);
            }
            if ((plan.inputAvailabilityMask & DiagnosticsInputPixelMetadata) !=
                0u)
            {
                plan.counterValidityMask |=
                    description.producerCapabilityMask &
                    (DiagnosticsCounterGroupHistogramValid |
                        DiagnosticsCounterLightingValid |
                        DiagnosticsCounterWorkListValid);
            }
        }
        return plan;
    }

    DiagnosticsGpuReadbackBlock MakeInitialDiagnosticsGpuReadbackBlock(
        const AdaptiveMsaaDiagnosticsPassPlan& plan,
        const AdaptiveMsaaDiagnosticsPassPlanDesc& description)
    {
        DiagnosticsGpuReadbackBlock block{};
        block.counters.validityMask = plan.counterValidityMask;
        block.counters.flags = plan.viewAvailable
            ? DiagnosticsCounterViewAvailable
            : 0u;
        block.counters.width = plan.width;
        block.counters.height = plan.height;
        block.counters.sampleCount = plan.sampleCount;
        block.counters.pixelCount = static_cast<uint32_t>(plan.pixelCount);

        block.selected.flags = plan.viewAvailable
            ? DiagnosticsSelectedHeaderViewAvailable
            : 0u;
        block.selected.requestedView =
            static_cast<uint32_t>(plan.shaderRequestedView);
        block.selected.visualizedSampleIndex =
            description.visualizedSampleIndex;
        if (description.settings.selectedPixelCaptureRequested &&
            plan.status == DiagnosticsPassStatus::Success)
        {
            block.selected.validityMask = DiagnosticsSelectedHeaderValid;
            block.selected.captureIdLow = LowWord(
                description.settings.selectedPixelCaptureId);
            block.selected.captureIdHigh = HighWord(
                description.settings.selectedPixelCaptureId);
            block.selected.frameIndexLow = LowWord(description.frameIndex);
            block.selected.frameIndexHigh = HighWord(description.frameIndex);
            block.selected.pixelX = description.settings.selectedPixelX;
            block.selected.pixelY = description.settings.selectedPixelY;
            block.selected.sampleCount = plan.sampleCount;
            block.selected.recordCount = plan.sampleCount;
        }
        return block;
    }

    DiagnosticsPassStatus ValidateDiagnosticsGpuReadbackBlock(
        const DiagnosticsGpuReadbackBlock& block)
    {
        if (block.counters.abiVersion != DiagnosticsGpuAbiVersion ||
            block.counters.byteSize != sizeof(DiagnosticsGpuCounterBlock) ||
            block.selected.abiVersion != DiagnosticsGpuAbiVersion ||
            block.selected.byteSize !=
                sizeof(DiagnosticsGpuSelectedHeader) +
                    sizeof(block.selectedSamples))
        {
            return DiagnosticsPassStatus::AbiMismatch;
        }
        if (!IsSupportedSampleCount(block.counters.sampleCount) ||
            block.counters.width == 0u || block.counters.height == 0u ||
            block.counters.pixelCount !=
                uint64_t(block.counters.width) * block.counters.height ||
            (block.counters.validityMask &
                ~DiagnosticsCounterAllValidityBits) != 0u ||
            (block.counters.flags & ~DiagnosticsCounterAllFlagBits) != 0u ||
            (block.selected.validityMask &
                ~DiagnosticsSelectedHeaderAllValidityBits) != 0u ||
            (block.selected.flags &
                ~DiagnosticsSelectedHeaderAllFlagBits) != 0u ||
            ((block.counters.flags & DiagnosticsCounterViewAvailable) != 0u) !=
                ((block.selected.flags &
                    DiagnosticsSelectedHeaderViewAvailable) != 0u) ||
            static_cast<uint32_t>(block.selected.requestedView) >=
                static_cast<uint32_t>(AdaptiveMsaaDebugView::Count) ||
            block.selected.visualizedSampleIndex >=
                block.counters.sampleCount ||
            block.selected.reserved0 != 0u ||
            block.selected.reserved1 != 0u)
        {
            return DiagnosticsPassStatus::AbiMismatch;
        }
        if ((block.counters.validityMask != 0u &&
                (block.counters.flags &
                    DiagnosticsCounterDispatchComplete) == 0u) ||
            ((block.selected.validityMask &
                    DiagnosticsSelectedHeaderValid) != 0u &&
                (block.selected.flags &
                    DiagnosticsSelectedHeaderDispatchComplete) == 0u))
        {
            return DiagnosticsPassStatus::AbiMismatch;
        }
        if ((block.selected.validityMask & DiagnosticsSelectedHeaderValid) !=
            0u)
        {
            if (!IsSupportedSampleCount(block.selected.sampleCount) ||
                block.selected.sampleCount != block.counters.sampleCount ||
                block.selected.recordCount != block.selected.sampleCount ||
                block.selected.recordCount > MaximumAdaptiveSampleCount ||
                block.selected.pixelX >= block.counters.width ||
                block.selected.pixelY >= block.counters.height)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
        }
        else if (block.selected.sampleCount != 0u ||
            block.selected.recordCount != 0u ||
            block.selected.pixelX != DiagnosticsGpuInvalidWord ||
            block.selected.pixelY != DiagnosticsGpuInvalidWord)
        {
            return DiagnosticsPassStatus::AbiMismatch;
        }

        const uint64_t frameSampleCount =
            uint64_t(block.counters.pixelCount) *
            block.counters.sampleCount;
        const bool lightingCountersValid =
            (block.counters.validityMask &
                DiagnosticsCounterLightingValid) != 0u;
        if (lightingCountersValid
                ? (block.counters.referenceLightingEvaluations >
                        frameSampleCount ||
                    block.counters.performedLightingEvaluations >
                        block.counters.referenceLightingEvaluations ||
                    block.counters.lightingEvaluationsSaved !=
                        block.counters.referenceLightingEvaluations -
                            block.counters.performedLightingEvaluations)
                : (block.counters.referenceLightingEvaluations != 0u ||
                    block.counters.performedLightingEvaluations != 0u ||
                    block.counters.lightingEvaluationsSaved != 0u))
        {
            return DiagnosticsPassStatus::AbiMismatch;
        }
        const bool invalidKeysValid =
            (block.counters.validityMask &
                DiagnosticsCounterInvalidKeysValid) != 0u;
        if (invalidKeysValid
                ? block.counters.invalidKeyCount > frameSampleCount
                : block.counters.invalidKeyCount != 0u)
            return DiagnosticsPassStatus::AbiMismatch;
        uint64_t observedPixels = 0u;
        for (uint32_t count : block.counters.groupCountHistogram)
            observedPixels += count;
        if ((block.counters.validityMask &
                DiagnosticsCounterGroupHistogramValid) != 0u
                ? observedPixels != block.counters.pixelCount
                : observedPixels != 0u)
        {
            return DiagnosticsPassStatus::AbiMismatch;
        }
        uint64_t fallbackCount = 0u;
        for (uint32_t count : block.counters.fallbackReasonCounts)
            fallbackCount += count;
        const bool fallbackCountersValid =
            (block.counters.validityMask &
                DiagnosticsCounterFallbackReasonsValid) != 0u;
        if (fallbackCountersValid
                ? (block.counters.fallbackReasonCounts[
                        static_cast<size_t>(FallbackReason::None)] != 0u ||
                    fallbackCount > frameSampleCount)
                : fallbackCount != 0u)
        {
            return DiagnosticsPassStatus::AbiMismatch;
        }
        if ((block.counters.validityMask &
                DiagnosticsCounterStaleAndMappingValid) != 0u)
        {
            if ((block.counters.validityMask &
                    DiagnosticsCounterFallbackReasonsValid) == 0u)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
            const uint64_t expectedStale =
                uint64_t(block.counters.fallbackReasonCounts[
                    static_cast<size_t>(FallbackReason::GenerationMismatch)]) +
                block.counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::TopologyEpochMismatch)] +
                block.counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::LodRevisionMismatch)] +
                block.counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::DeformationRevisionMismatch)] +
                block.counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::MaterialRevisionMismatch)] +
                block.counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::GenerationExhausted)] +
                block.counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::RegistryResetRequired)] +
                block.counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::TextureContentRevisionMismatch)] +
                block.counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::TextureQualificationMismatch)];
            const uint64_t expectedMapping =
                uint64_t(block.counters.fallbackReasonCounts[
                    static_cast<size_t>(
                        FallbackReason::TransientMappingCollision)]) +
                block.counters.fallbackReasonCounts[static_cast<size_t>(
                    FallbackReason::TransientMappingFailure)];
            if (block.counters.staleKeyCount != expectedStale ||
                block.counters.mappingFailureCount != expectedMapping)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
        }
        else if (block.counters.staleKeyCount != 0u ||
            block.counters.mappingFailureCount != 0u)
        {
            return DiagnosticsPassStatus::AbiMismatch;
        }
        const bool workListValid =
            (block.counters.validityMask &
                DiagnosticsCounterWorkListValid) != 0u;
        if (workListValid
                ? (block.counters.workListOccupancyTotal >
                        block.counters.workListCapacityTotal ||
                    block.counters.workListPeakOccupancy >
                        block.counters.workListCapacityTotal ||
                    block.counters.workListOverflowCount >
                        block.counters.workListObservationCount ||
                    block.counters.workListObservationCount >
                        block.counters.pixelCount)
                : (block.counters.workListObservationCount != 0u ||
                    block.counters.workListOccupancyTotal != 0u ||
                    block.counters.workListCapacityTotal != 0u ||
                    block.counters.workListPeakOccupancy != 0u ||
                    block.counters.workListOverflowCount != 0u))
        {
            return DiagnosticsPassStatus::AbiMismatch;
        }
        const bool errorBudgetValid =
            (block.counters.validityMask &
                DiagnosticsCounterErrorBudgetValid) != 0u;
        if (errorBudgetValid)
        {
            uint64_t rejectedGroups = 0u;
            for (size_t reason = 1u;
                reason < DiagnosticsErrorBudgetRejectionReasonBinCount;
                ++reason)
            {
                rejectedGroups +=
                    block.counters.errorBudgetRejectionReasonCounts[reason];
            }
            if (block.counters.errorBudgetRejectionReasonCounts[
                    static_cast<size_t>(
                        DiagnosticsErrorBudgetRejectionReason::None)] != 0u ||
                rejectedGroups !=
                    block.counters.errorBudgetRejectedGroupCount ||
                block.counters.errorBudgetRejectedSampleCount <
                    block.counters.errorBudgetRejectedGroupCount ||
                block.counters.errorBudgetRejectedSampleCount >
                    frameSampleCount)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
        }
        else
        {
            uint64_t unavailableErrorBudgetCount = 0u;
            for (uint32_t count :
                block.counters.errorBudgetRejectionReasonCounts)
            {
                unavailableErrorBudgetCount += count;
            }
            if (unavailableErrorBudgetCount != 0u ||
                block.counters.errorBudgetRejectedGroupCount != 0u ||
                block.counters.errorBudgetRejectedSampleCount != 0u)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
        }

        const uint32_t inspectedCount = block.selected.recordCount;
        for (uint32_t sampleIndex = 0u;
            sampleIndex < MaximumAdaptiveSampleCount;
            ++sampleIndex)
        {
            const DiagnosticsGpuSelectedSampleRecord& record =
                block.selectedSamples[sampleIndex];
            if (sampleIndex >= inspectedCount && record.validFields != 0u)
                return DiagnosticsPassStatus::AbiMismatch;
            if (sampleIndex < inspectedCount &&
                (record.validFields &
                    DiagnosticsSelectedRecordKeySupplied) == 0u)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
            if ((record.validFields &
                    ~DiagnosticsSelectedRecordAllValidityBits) != 0u ||
                ((record.validFields & DiagnosticsSelectedRecordFrontFace) !=
                    0u &&
                    (record.validFields &
                        DiagnosticsSelectedRecordFrontFaceValid) == 0u) ||
                ((record.validFields & DiagnosticsSelectedRecordKeyValid) !=
                    0u &&
                    (record.validFields &
                        DiagnosticsSelectedRecordKeySupplied) == 0u) ||
                ((record.validFields & DiagnosticsSelectedRecordGroupValid) !=
                    0u &&
                    (record.validFields & DiagnosticsSelectedRecordCovered) ==
                        0u) ||
                ((record.validFields & DiagnosticsSelectedRecordKeyValid) !=
                    0u &&
                    (record.validFields & DiagnosticsSelectedRecordCovered) ==
                        0u))
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }

            const auto unavailableFloat = [](uint32_t bits)
            {
                return bits == DiagnosticsGpuInvalidFloatBits;
            };
            if ((record.validFields &
                    DiagnosticsSelectedRecordKeySupplied) == 0u &&
                (record.geometryInstanceHandle != DiagnosticsGpuInvalidWord ||
                    record.geometryLocalPrimitiveIndex !=
                        DiagnosticsGpuInvalidWord))
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
            if ((record.validFields & DiagnosticsSelectedRecordKeyValid) != 0u)
            {
                const GpuVisibilityKey key = {
                    record.geometryInstanceHandle,
                    record.geometryLocalPrimitiveIndex
                };
                if (ValidateGpuVisibilityKey(key) != FallbackReason::None)
                    return DiagnosticsPassStatus::AbiMismatch;
            }
            if ((record.validFields & DiagnosticsSelectedRecordDepthValid) ==
                0u)
            {
                if (!unavailableFloat(record.deviceDepthBits))
                    return DiagnosticsPassStatus::AbiMismatch;
            }
            else if (!std::isfinite(FloatFromBits(record.deviceDepthBits)))
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }

            const bool suppliedBarycentrics =
                (record.validFields &
                    DiagnosticsSelectedRecordSuppliedBarycentrics) != 0u;
            if (suppliedBarycentrics !=
                    (!unavailableFloat(record.suppliedBarycentricXBits) &&
                        !unavailableFloat(
                            record.suppliedBarycentricYBits)) ||
                (suppliedBarycentrics &&
                    (!std::isfinite(FloatFromBits(
                            record.suppliedBarycentricXBits)) ||
                        !std::isfinite(FloatFromBits(
                            record.suppliedBarycentricYBits)))))
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
            const bool reconstructedBarycentrics =
                (record.validFields &
                    DiagnosticsSelectedRecordReconstructedBarycentrics) != 0u;
            if (reconstructedBarycentrics !=
                    (!unavailableFloat(
                            record.reconstructedBarycentricXBits) &&
                        !unavailableFloat(
                            record.reconstructedBarycentricYBits)) ||
                (reconstructedBarycentrics &&
                    (!std::isfinite(FloatFromBits(
                            record.reconstructedBarycentricXBits)) ||
                        !std::isfinite(FloatFromBits(
                            record.reconstructedBarycentricYBits)))))
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }

            const bool weightValid =
                (record.validFields &
                    DiagnosticsSelectedRecordWeightValid) != 0u;
            const float weight = FloatFromBits(record.weightBits);
            if (sampleIndex < inspectedCount &&
                (!weightValid || !std::isfinite(weight) ||
                    weight <= 0.f || weight > 1.f))
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
            if (sampleIndex >= inspectedCount &&
                (weightValid || !unavailableFloat(record.weightBits)))
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }

            if ((record.validFields & DiagnosticsSelectedRecordGroupValid) ==
                0u)
            {
                if (record.groupIndex != DiagnosticsGpuInvalidWord ||
                    record.representativeSampleIndex !=
                        DiagnosticsGpuInvalidWord)
                {
                    return DiagnosticsPassStatus::AbiMismatch;
                }
            }
            else
            {
                if (sampleIndex >= inspectedCount ||
                    (record.validFields &
                        DiagnosticsSelectedRecordCovered) == 0u ||
                    record.groupIndex >= inspectedCount ||
                    record.representativeSampleIndex >= inspectedCount ||
                    !weightValid)
                {
                    return DiagnosticsPassStatus::AbiMismatch;
                }
            }

            const bool fallbackValid =
                (record.validFields &
                    DiagnosticsSelectedRecordFallbackReason) != 0u;
            if (fallbackValid
                    ? record.fallbackReason >=
                        static_cast<uint32_t>(FallbackReason::Count)
                    : record.fallbackReason != DiagnosticsGpuInvalidWord)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
            const bool coveredWithoutGroup =
                (record.validFields & DiagnosticsSelectedRecordCovered) != 0u &&
                (record.validFields &
                    DiagnosticsSelectedRecordGroupValid) == 0u;
            const bool coveredInvalidKey =
                (record.validFields & DiagnosticsSelectedRecordCovered) != 0u &&
                (record.validFields &
                    DiagnosticsSelectedRecordKeyValid) == 0u;
            if ((coveredWithoutGroup || coveredInvalidKey) &&
                (!fallbackValid || record.fallbackReason ==
                    static_cast<uint32_t>(FallbackReason::None)))
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
            const bool errorBudgetValid =
                (record.validFields &
                    DiagnosticsSelectedRecordErrorBudgetReason) != 0u;
            if (errorBudgetValid
                    ? record.errorBudgetReason >= static_cast<uint32_t>(
                        DiagnosticsErrorBudgetRejectionReason::Count)
                    : record.errorBudgetReason != DiagnosticsGpuInvalidWord)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
            const bool radianceValid =
                (record.validFields & DiagnosticsSelectedRecordRadiance) != 0u;
            if (radianceValid !=
                    (!unavailableFloat(record.radianceRBits) &&
                        !unavailableFloat(record.radianceGBits) &&
                        !unavailableFloat(record.radianceBBits)) ||
                (radianceValid &&
                    (!std::isfinite(FloatFromBits(record.radianceRBits)) ||
                        !std::isfinite(FloatFromBits(record.radianceGBits)) ||
                        !std::isfinite(FloatFromBits(record.radianceBBits)))))
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
        }

        uint32_t observedGroupMask = 0u;
        uint32_t groupCount = 0u;
        for (uint32_t sampleIndex = 0u;
            sampleIndex < inspectedCount;
            ++sampleIndex)
        {
            const DiagnosticsGpuSelectedSampleRecord& record =
                block.selectedSamples[sampleIndex];
            if ((record.validFields &
                    DiagnosticsSelectedRecordGroupValid) == 0u)
            {
                continue;
            }
            observedGroupMask |= 1u << record.groupIndex;
            groupCount = std::max(groupCount, record.groupIndex + 1u);
        }
        const uint32_t contiguousGroupMask = groupCount == 0u
            ? 0u
            : (1u << groupCount) - 1u;
        if (observedGroupMask != contiguousGroupMask)
            return DiagnosticsPassStatus::AbiMismatch;

        // SameSurface and CanShareShading are intentionally independent.
        // Validate the producer's final groups rather than rebuilding groups
        // from SurfaceKey: one exact key may conservatively split into several
        // groups, while every member of one final group must retain one key.
        for (uint32_t groupIndex = 0u;
            groupIndex < groupCount;
            ++groupIndex)
        {
            uint32_t expectedRepresentative = DiagnosticsGpuInvalidWord;
            uint32_t expectedGroupSize = 0u;
            uint32_t groupGeometryInstanceHandle = DiagnosticsGpuInvalidWord;
            uint32_t groupGeometryLocalPrimitiveIndex =
                DiagnosticsGpuInvalidWord;

            for (uint32_t sampleIndex = 0u;
                sampleIndex < inspectedCount;
                ++sampleIndex)
            {
                const DiagnosticsGpuSelectedSampleRecord& record =
                    block.selectedSamples[sampleIndex];
                if ((record.validFields &
                        DiagnosticsSelectedRecordGroupValid) == 0u ||
                    record.groupIndex != groupIndex)
                {
                    continue;
                }

                if (expectedGroupSize == 0u)
                {
                    groupGeometryInstanceHandle =
                        record.geometryInstanceHandle;
                    groupGeometryLocalPrimitiveIndex =
                        record.geometryLocalPrimitiveIndex;
                    expectedRepresentative = sampleIndex;
                }
                else if (record.geometryInstanceHandle !=
                        groupGeometryInstanceHandle ||
                    record.geometryLocalPrimitiveIndex !=
                        groupGeometryLocalPrimitiveIndex)
                {
                    return DiagnosticsPassStatus::AbiMismatch;
                }
                expectedRepresentative =
                    std::min(expectedRepresentative, sampleIndex);
                ++expectedGroupSize;
            }

            if (expectedGroupSize == 0u)
                return DiagnosticsPassStatus::AbiMismatch;
            const DiagnosticsGpuSelectedSampleRecord& representativeRecord =
                block.selectedSamples[expectedRepresentative];
            if ((representativeRecord.validFields &
                    DiagnosticsSelectedRecordKeyValid) == 0u &&
                expectedGroupSize != 1u)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
            const float expectedWeight =
                float(expectedGroupSize) / float(block.selected.sampleCount);
            for (uint32_t sampleIndex = 0u;
                sampleIndex < inspectedCount;
                ++sampleIndex)
            {
                const DiagnosticsGpuSelectedSampleRecord& record =
                    block.selectedSamples[sampleIndex];
                if ((record.validFields &
                        DiagnosticsSelectedRecordGroupValid) == 0u ||
                    record.groupIndex != groupIndex)
                {
                    continue;
                }
                if (record.representativeSampleIndex !=
                        expectedRepresentative ||
                    std::abs(FloatFromBits(record.weightBits) -
                        expectedWeight) > 1e-6f)
                {
                    return DiagnosticsPassStatus::AbiMismatch;
                }
            }
        }
        const float ungroupedSampleWeight =
            1.f / float(block.selected.sampleCount);
        for (uint32_t sampleIndex = 0u;
            sampleIndex < inspectedCount;
            ++sampleIndex)
        {
            const DiagnosticsGpuSelectedSampleRecord& record =
                block.selectedSamples[sampleIndex];
            if ((record.validFields &
                    DiagnosticsSelectedRecordGroupValid) == 0u &&
                std::abs(FloatFromBits(record.weightBits) -
                    ungroupedSampleWeight) > 1e-6f)
            {
                return DiagnosticsPassStatus::AbiMismatch;
            }
        }
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsReadbackTicket DiagnosticsReadbackRingState::Acquire()
    {
        for (uint32_t offset = 0u;
            offset < DiagnosticsReadbackRingCapacity;
            ++offset)
        {
            const uint32_t slotIndex =
                (m_NextSlot + offset) % DiagnosticsReadbackRingCapacity;
            Slot& slot = m_Slots[slotIndex];
            if (slot.state != DiagnosticsReadbackSlotState::Available)
                continue;

            uint64_t token = m_NextToken++;
            if (token == 0u)
            {
                token = m_NextToken++;
                if (token == 0u)
                    token = 1u;
            }
            while (FindSlot(token) != DiagnosticsGpuInvalidWord)
            {
                token = m_NextToken++;
                if (token == 0u)
                    token = 1u;
            }

            slot.state = DiagnosticsReadbackSlotState::Recorded;
            slot.token = token;
            m_NextSlot =
                (slotIndex + 1u) % DiagnosticsReadbackRingCapacity;
            return { DiagnosticsPassStatus::Success, token, slotIndex };
        }
        return {};
    }

    DiagnosticsPassStatus DiagnosticsReadbackRingState::MarkSubmitted(
        uint64_t token)
    {
        const uint32_t slotIndex = FindSlot(token);
        if (slotIndex == DiagnosticsGpuInvalidWord || token == 0u)
            return DiagnosticsPassStatus::InvalidReadbackToken;
        Slot& slot = m_Slots[slotIndex];
        if (slot.state != DiagnosticsReadbackSlotState::Recorded)
            return DiagnosticsPassStatus::InvalidReadbackState;
        if (m_SubmittedCount >= DiagnosticsReadbackRingCapacity)
            return DiagnosticsPassStatus::InvalidReadbackState;
        slot.state = DiagnosticsReadbackSlotState::Submitted;
        const uint32_t queueIndex =
            (m_SubmittedHead + m_SubmittedCount) %
            DiagnosticsReadbackRingCapacity;
        m_SubmittedSlots[queueIndex] = slotIndex;
        ++m_SubmittedCount;
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsPassStatus DiagnosticsReadbackRingState::CancelRecorded(
        uint64_t token)
    {
        const uint32_t slotIndex = FindSlot(token);
        if (slotIndex == DiagnosticsGpuInvalidWord || token == 0u)
            return DiagnosticsPassStatus::InvalidReadbackToken;
        Slot& slot = m_Slots[slotIndex];
        if (slot.state != DiagnosticsReadbackSlotState::Recorded)
            return DiagnosticsPassStatus::InvalidReadbackState;
        slot = {};
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsPassStatus DiagnosticsReadbackRingState::MarkCompleted(
        uint64_t token)
    {
        const uint32_t slotIndex = FindSlot(token);
        if (slotIndex == DiagnosticsGpuInvalidWord || token == 0u)
            return DiagnosticsPassStatus::InvalidReadbackToken;
        Slot& slot = m_Slots[slotIndex];
        if (slot.state != DiagnosticsReadbackSlotState::Submitted)
            return DiagnosticsPassStatus::InvalidReadbackState;

        uint32_t submittedOffset = DiagnosticsGpuInvalidWord;
        for (uint32_t offset = 0u; offset < m_SubmittedCount; ++offset)
        {
            if (m_SubmittedSlots[
                    (m_SubmittedHead + offset) %
                        DiagnosticsReadbackRingCapacity] == slotIndex)
            {
                submittedOffset = offset;
                break;
            }
        }
        if (submittedOffset == DiagnosticsGpuInvalidWord)
            return DiagnosticsPassStatus::InvalidReadbackState;
        for (uint32_t offset = submittedOffset;
            offset + 1u < m_SubmittedCount;
            ++offset)
        {
            m_SubmittedSlots[
                (m_SubmittedHead + offset) %
                    DiagnosticsReadbackRingCapacity] =
                m_SubmittedSlots[
                    (m_SubmittedHead + offset + 1u) %
                        DiagnosticsReadbackRingCapacity];
        }
        m_SubmittedSlots[
            (m_SubmittedHead + m_SubmittedCount - 1u) %
                DiagnosticsReadbackRingCapacity] =
            DiagnosticsGpuInvalidWord;
        --m_SubmittedCount;
        if (m_SubmittedCount == 0u)
            m_SubmittedHead = 0u;
        slot = {};
        return DiagnosticsPassStatus::Success;
    }

    uint32_t DiagnosticsReadbackRingState::FindSlot(uint64_t token) const
    {
        if (token == 0u)
            return DiagnosticsGpuInvalidWord;
        for (uint32_t slotIndex = 0u;
            slotIndex < DiagnosticsReadbackRingCapacity;
            ++slotIndex)
        {
            if (m_Slots[slotIndex].token == token &&
                m_Slots[slotIndex].state !=
                    DiagnosticsReadbackSlotState::Available)
            {
                return slotIndex;
            }
        }
        return DiagnosticsGpuInvalidWord;
    }

    DiagnosticsReadbackSlotState DiagnosticsReadbackRingState::SlotState(
        uint32_t slotIndex) const
    {
        return slotIndex < DiagnosticsReadbackRingCapacity
            ? m_Slots[slotIndex].state
            : DiagnosticsReadbackSlotState::Count;
    }

    uint64_t DiagnosticsReadbackRingState::SlotToken(
        uint32_t slotIndex) const
    {
        return slotIndex < DiagnosticsReadbackRingCapacity
            ? m_Slots[slotIndex].token
            : 0u;
    }

    uint32_t DiagnosticsReadbackRingState::OldestSubmittedSlot() const
    {
        if (m_SubmittedCount == 0u)
            return DiagnosticsGpuInvalidWord;
        const uint32_t slotIndex = m_SubmittedSlots[m_SubmittedHead];
        return slotIndex < DiagnosticsReadbackRingCapacity &&
                m_Slots[slotIndex].state ==
                    DiagnosticsReadbackSlotState::Submitted
            ? slotIndex
            : DiagnosticsGpuInvalidWord;
    }

    uint32_t DiagnosticsReadbackRingState::OutstandingCount() const
    {
        uint32_t result = 0u;
        for (const Slot& slot : m_Slots)
        {
            if (slot.state != DiagnosticsReadbackSlotState::Available)
                ++result;
        }
        return result;
    }

    uint32_t DiagnosticsReadbackRingState::SubmittedCount() const
    {
        return m_SubmittedCount;
    }

    AdaptiveMsaaDiagnosticsGpuPass::AdaptiveMsaaDiagnosticsGpuPass(
        nvrhi::IDevice* device,
        std::shared_ptr<donut::engine::ShaderFactory> shaderFactory)
        : m_Device(device)
        , m_ShaderFactory(std::move(shaderFactory))
    {
    }

    AdaptiveMsaaDiagnosticsGpuPass::~AdaptiveMsaaDiagnosticsGpuPass() =
        default;

    uint32_t AdaptiveMsaaDiagnosticsGpuPass::PipelineIndex(
        uint32_t sampleCount)
    {
        switch (sampleCount)
        {
        case 2u: return 0u;
        case 4u: return 1u;
        case 8u: return 2u;
        case 16u: return 3u;
        default: return DiagnosticsGpuInvalidWord;
        }
    }

    DiagnosticsPassStatus AdaptiveMsaaDiagnosticsGpuPass::EnsureCoreResources(
        uint32_t sampleCount)
    {
        if (!m_Device || !m_ShaderFactory)
            return DiagnosticsPassStatus::ResourceCreationFailed;

        if (!m_CoreResourcesReady)
        {
            nvrhi::BindingLayoutDesc layout;
            layout.visibility = nvrhi::ShaderType::Compute;
            layout.bindings = {
                nvrhi::BindingLayoutItem::PushConstants(
                    0,
                    sizeof(GpuConstants)),
                nvrhi::BindingLayoutItem::Texture_SRV(0),
                nvrhi::BindingLayoutItem::Texture_SRV(1),
                nvrhi::BindingLayoutItem::Texture_SRV(2),
                nvrhi::BindingLayoutItem::Texture_SRV(3),
                nvrhi::BindingLayoutItem::Texture_SRV(4),
                nvrhi::BindingLayoutItem::Texture_SRV(5),
                nvrhi::BindingLayoutItem::Texture_SRV(6),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(7),
                nvrhi::BindingLayoutItem::StructuredBuffer_SRV(8),
                nvrhi::BindingLayoutItem::Texture_SRV(9),
                nvrhi::BindingLayoutItem::Texture_SRV(10),
                nvrhi::BindingLayoutItem::Texture_SRV(11),
                nvrhi::BindingLayoutItem::Texture_SRV(12),
                nvrhi::BindingLayoutItem::Texture_SRV(13),
                nvrhi::BindingLayoutItem::Texture_SRV(14),
                nvrhi::BindingLayoutItem::Texture_SRV(15),
                nvrhi::BindingLayoutItem::Texture_SRV(16),
                nvrhi::BindingLayoutItem::Texture_SRV(17),
                nvrhi::BindingLayoutItem::Texture_SRV(18),
                nvrhi::BindingLayoutItem::Texture_SRV(19),
                nvrhi::BindingLayoutItem::Texture_SRV(20),
                nvrhi::BindingLayoutItem::Texture_SRV(21),
                nvrhi::BindingLayoutItem::Texture_SRV(22),
                nvrhi::BindingLayoutItem::Texture_SRV(23),
                nvrhi::BindingLayoutItem::Texture_SRV(24),
                nvrhi::BindingLayoutItem::Texture_SRV(25),
                nvrhi::BindingLayoutItem::Texture_SRV(26),
                nvrhi::BindingLayoutItem::Texture_UAV(0),
                nvrhi::BindingLayoutItem::RawBuffer_UAV(1),
                nvrhi::BindingLayoutItem::Texture_UAV(2),
                nvrhi::BindingLayoutItem::Texture_UAV(3),
                nvrhi::BindingLayoutItem::Texture_UAV(4),
                nvrhi::BindingLayoutItem::Texture_UAV(5),
                nvrhi::BindingLayoutItem::Texture_UAV(6),
                nvrhi::BindingLayoutItem::Texture_UAV(7)
            };
            m_BindingLayout = m_Device->createBindingLayout(layout);

            nvrhi::BufferDesc gpuReadbackDesc;
            gpuReadbackDesc.byteSize = sizeof(DiagnosticsGpuReadbackBlock);
            gpuReadbackDesc.canHaveUAVs = true;
            gpuReadbackDesc.canHaveRawViews = true;
            gpuReadbackDesc.initialState = nvrhi::ResourceStates::Common;
            gpuReadbackDesc.keepInitialState = true;
            gpuReadbackDesc.debugName =
                "Adaptive MSAA Diagnostics/GPU Readback";
            m_GpuReadback = m_Device->createBuffer(gpuReadbackDesc);

            nvrhi::BufferDesc dummySampleDesc;
            dummySampleDesc.byteSize =
                sizeof(DiagnosticsGpuSampleInputRecord);
            dummySampleDesc.structStride =
                sizeof(DiagnosticsGpuSampleInputRecord);
            dummySampleDesc.initialState =
                nvrhi::ResourceStates::ShaderResource;
            dummySampleDesc.keepInitialState = true;
            dummySampleDesc.debugName =
                "Adaptive MSAA Diagnostics/Dummy Selected Sample Data";
            m_DummySelectedSampleData =
                m_Device->createBuffer(dummySampleDesc);

            nvrhi::BufferDesc dummyMetadataDesc;
            dummyMetadataDesc.byteSize =
                sizeof(DiagnosticsGpuPixelMetadataRecord);
            dummyMetadataDesc.structStride =
                sizeof(DiagnosticsGpuPixelMetadataRecord);
            dummyMetadataDesc.initialState =
                nvrhi::ResourceStates::ShaderResource;
            dummyMetadataDesc.keepInitialState = true;
            dummyMetadataDesc.debugName =
                "Adaptive MSAA Diagnostics/Dummy Pixel Metadata";
            m_DummyPixelMetadata =
                m_Device->createBuffer(dummyMetadataDesc);

            nvrhi::TextureDesc dummyInputDesc;
            dummyInputDesc.width = 1u;
            dummyInputDesc.height = 1u;
            dummyInputDesc.format = nvrhi::Format::RGBA16_FLOAT;
            dummyInputDesc.dimension = nvrhi::TextureDimension::Texture2D;
            dummyInputDesc.isShaderResource = true;
            dummyInputDesc.initialState =
                nvrhi::ResourceStates::ShaderResource;
            dummyInputDesc.keepInitialState = true;
            dummyInputDesc.debugName =
                "Adaptive MSAA Diagnostics/Dummy Color Input";
            m_DummyColorInput = m_Device->createTexture(dummyInputDesc);

            nvrhi::TextureDesc dummyScalarInputDesc = dummyInputDesc;
            dummyScalarInputDesc.format = nvrhi::Format::R32_FLOAT;
            dummyScalarInputDesc.debugName =
                "Adaptive MSAA Diagnostics/Dummy Scalar Input";
            m_DummyScalarInput =
                m_Device->createTexture(dummyScalarInputDesc);

            nvrhi::TextureDesc dummyStatusDesc;
            dummyStatusDesc.width = 1u;
            dummyStatusDesc.height = 1u;
            dummyStatusDesc.arraySize = MaximumAdaptiveSampleCount;
            dummyStatusDesc.format = nvrhi::Format::R8_UINT;
            dummyStatusDesc.dimension =
                nvrhi::TextureDimension::Texture2DArray;
            dummyStatusDesc.isShaderResource = true;
            dummyStatusDesc.initialState =
                nvrhi::ResourceStates::ShaderResource;
            dummyStatusDesc.keepInitialState = true;
            dummyStatusDesc.debugName =
                "Adaptive MSAA Diagnostics/Dummy Sample Status";
            m_DummySampleStatus =
                m_Device->createTexture(dummyStatusDesc);

            nvrhi::TextureDesc dummyOutputDesc = dummyInputDesc;
            // NVRHI's D3D12 backend requires a UAV texture to retain shader
            // resource creation support even when this dummy is bound only as
            // a UAV in the diagnostics layout.
            dummyOutputDesc.isShaderResource = true;
            dummyOutputDesc.isUAV = true;
            dummyOutputDesc.initialState =
                nvrhi::ResourceStates::UnorderedAccess;
            dummyOutputDesc.debugName =
                "Adaptive MSAA Diagnostics/Dummy Output";
            m_DummyOutput = m_Device->createTexture(dummyOutputDesc);

            nvrhi::TextureDesc dummyUintOutputDesc = dummyOutputDesc;
            dummyUintOutputDesc.format = nvrhi::Format::R8_UINT;
            dummyUintOutputDesc.debugName =
                "Adaptive MSAA Diagnostics/Dummy Uint Output";
            m_DummyUintOutput =
                m_Device->createTexture(dummyUintOutputDesc);

            if (!m_BindingLayout || !m_GpuReadback ||
                !m_DummySelectedSampleData || !m_DummyPixelMetadata ||
                !m_DummyColorInput || !m_DummyScalarInput ||
                !m_DummySampleStatus ||
                !m_DummyOutput || !m_DummyUintOutput)
            {
                return DiagnosticsPassStatus::ResourceCreationFailed;
            }
            m_CoreResourcesReady = true;
        }

        const uint32_t pipelineIndex = PipelineIndex(sampleCount);
        if (pipelineIndex == DiagnosticsGpuInvalidWord)
            return DiagnosticsPassStatus::UnsupportedSampleCount;
        if (m_Pipelines[pipelineIndex])
            return DiagnosticsPassStatus::Success;

        std::vector<donut::engine::ShaderMacro> macros;
        macros.emplace_back(
            "UVSR_DIAGNOSTICS_MSAA_SAMPLES",
            std::to_string(sampleCount));
        m_Shaders[pipelineIndex] = m_ShaderFactory->CreateShader(
            "uvsr/adaptive_msaa_diagnostics_cs.hlsl",
            "main",
            &macros,
            nvrhi::ShaderType::Compute);
        if (!m_Shaders[pipelineIndex])
            return DiagnosticsPassStatus::PipelineUnavailable;

        nvrhi::ComputePipelineDesc pipelineDesc;
        pipelineDesc.bindingLayouts = { m_BindingLayout };
        pipelineDesc.CS = m_Shaders[pipelineIndex];
        m_Pipelines[pipelineIndex] =
            m_Device->createComputePipeline(pipelineDesc);
        return m_Pipelines[pipelineIndex]
            ? DiagnosticsPassStatus::Success
            : DiagnosticsPassStatus::PipelineUnavailable;
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::EnsureErrorMetricResources(
        uint32_t width,
        uint32_t height,
        bool historyEnabled)
    {
        if (!m_Device || width == 0u || height == 0u)
            return DiagnosticsPassStatus::ResourceCreationFailed;
        const bool extentChanged = m_ErrorResourceWidth != width ||
            m_ErrorResourceHeight != height;
        if (!m_ErrorMetricResourcesReady || extentChanged)
        {
            nvrhi::TextureDesc metricDesc;
            metricDesc.width = width;
            metricDesc.height = height;
            metricDesc.format = nvrhi::Format::RGBA32_FLOAT;
            metricDesc.dimension = nvrhi::TextureDimension::Texture2D;
            metricDesc.isShaderResource = true;
            metricDesc.isUAV = true;
            metricDesc.initialState = nvrhi::ResourceStates::Common;
            metricDesc.keepInitialState = true;

            metricDesc.debugName =
                "Adaptive MSAA Diagnostics/Reconstruction Error 0";
            nvrhi::TextureHandle reconstruction0 =
                m_Device->createTexture(metricDesc);
            metricDesc.debugName =
                "Adaptive MSAA Diagnostics/Reconstruction Error 1";
            nvrhi::TextureHandle reconstruction1 =
                m_Device->createTexture(metricDesc);
            metricDesc.debugName =
                "Adaptive MSAA Diagnostics/Lighting Error";
            nvrhi::TextureHandle lighting =
                m_Device->createTexture(metricDesc);

            nvrhi::TextureDesc validityDesc = metricDesc;
            validityDesc.format = nvrhi::Format::R8_UINT;
            validityDesc.debugName =
                "Adaptive MSAA Diagnostics/Error Validity";
            nvrhi::TextureHandle validity =
                m_Device->createTexture(validityDesc);
            if (!reconstruction0 || !reconstruction1 || !lighting ||
                !validity)
            {
                return DiagnosticsPassStatus::ResourceCreationFailed;
            }

            m_ReconstructionError0 = std::move(reconstruction0);
            m_ReconstructionError1 = std::move(reconstruction1);
            m_LightingError = std::move(lighting);
            m_ErrorValidity = std::move(validity);
            m_ErrorResourceWidth = width;
            m_ErrorResourceHeight = height;
            m_ErrorMetricResourcesReady = true;
            m_ReferenceHistory = {};
            m_AdaptiveHistory = {};
            m_ErrorHistoryResourcesReady = false;
            m_HistoryState = {};
            m_HistoryReadIndex = 0u;
        }

        if (historyEnabled && !m_ErrorHistoryResourcesReady)
        {
            nvrhi::TextureDesc historyDesc;
            historyDesc.width = width;
            historyDesc.height = height;
            historyDesc.format = nvrhi::Format::RGBA16_FLOAT;
            historyDesc.dimension = nvrhi::TextureDimension::Texture2D;
            historyDesc.isShaderResource = true;
            historyDesc.isUAV = true;
            historyDesc.initialState = nvrhi::ResourceStates::Common;
            historyDesc.keepInitialState = true;

            std::array<nvrhi::TextureHandle, 2u> reference{};
            std::array<nvrhi::TextureHandle, 2u> adaptive{};
            for (uint32_t index = 0u; index < 2u; ++index)
            {
                historyDesc.debugName =
                    "Adaptive MSAA Diagnostics/Reference History " +
                    std::to_string(index);
                reference[index] = m_Device->createTexture(historyDesc);
                historyDesc.debugName =
                    "Adaptive MSAA Diagnostics/Adaptive History " +
                    std::to_string(index);
                adaptive[index] = m_Device->createTexture(historyDesc);
                if (!reference[index] || !adaptive[index])
                    return DiagnosticsPassStatus::ResourceCreationFailed;
            }
            m_ReferenceHistory = std::move(reference);
            m_AdaptiveHistory = std::move(adaptive);
            m_ErrorHistoryResourcesReady = true;
            m_HistoryState = {};
            m_HistoryReadIndex = 0u;
        }
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::EnsureReadbackResources()
    {
        if (m_ReadbackResourcesReady)
            return DiagnosticsPassStatus::Success;
        if (!m_Device || !m_GpuReadback)
            return DiagnosticsPassStatus::ResourceCreationFailed;

        std::array<ReadbackSlot, DiagnosticsReadbackRingCapacity> created{};
        for (uint32_t slotIndex = 0u;
            slotIndex < DiagnosticsReadbackRingCapacity;
            ++slotIndex)
        {
            nvrhi::BufferDesc readbackDesc;
            readbackDesc.byteSize = sizeof(DiagnosticsGpuReadbackBlock);
            readbackDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
            readbackDesc.initialState = nvrhi::ResourceStates::CopyDest;
            readbackDesc.keepInitialState = true;
            readbackDesc.debugName =
                "Adaptive MSAA Diagnostics/Readback " +
                std::to_string(slotIndex);
            created[slotIndex].buffer =
                m_Device->createBuffer(readbackDesc);
            created[slotIndex].eventQuery = m_Device->createEventQuery();
            if (!created[slotIndex].buffer ||
                !created[slotIndex].eventQuery)
            {
                return DiagnosticsPassStatus::ResourceCreationFailed;
            }
        }
        m_ReadbackSlots = std::move(created);
        m_ReadbackResourcesReady = true;
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::EnsureTimerResources()
    {
        if (m_TimerResourcesReady)
            return DiagnosticsPassStatus::Success;
        if (!m_Device)
            return DiagnosticsPassStatus::ResourceCreationFailed;

        std::array<nvrhi::TimerQueryHandle,
            DiagnosticsGpuTimerRingCapacity> created{};
        for (nvrhi::TimerQueryHandle& query : created)
        {
            query = m_Device->createTimerQuery();
            if (!query)
                return DiagnosticsPassStatus::ResourceCreationFailed;
        }
        m_TimerQueries = std::move(created);
        m_TimerResourcesReady = true;
        return DiagnosticsPassStatus::Success;
    }

    void AdaptiveMsaaDiagnosticsGpuPass::AdvanceTimerQueries()
    {
        if (!m_TimerResourcesReady || !m_Device)
            return;

        for (uint32_t slotIndex = 0u;
            slotIndex < DiagnosticsGpuTimerRingCapacity;
            ++slotIndex)
        {
            if (!m_TimerPending[slotIndex])
                continue;
            nvrhi::ITimerQuery* query = m_TimerQueries[slotIndex];
            if (!query || !m_Device->pollTimerQuery(query))
                continue;

            const float gpuMilliseconds =
                m_Device->getTimerQueryTime(query) * 1000.f;
            m_Device->resetTimerQuery(query);
            m_TimerPending[slotIndex] = false;
            if (std::isfinite(gpuMilliseconds) &&
                gpuMilliseconds >= 0.f &&
                (!m_LatestGpuTiming.available ||
                    m_TimerFrameIndices[slotIndex] >=
                        m_LatestGpuTiming.frameIndex))
            {
                m_LatestGpuTiming.gpuMilliseconds = gpuMilliseconds;
                m_LatestGpuTiming.frameIndex =
                    m_TimerFrameIndices[slotIndex];
                m_LatestGpuTiming.available = true;
            }
        }
    }

    bool AdaptiveMsaaDiagnosticsGpuPass::BeginTimerQuery(
        nvrhi::ICommandList* commandList,
        uint64_t frameIndex,
        uint32_t& slotIndex)
    {
        slotIndex = DiagnosticsGpuInvalidWord;
        if (!commandList || !m_TimerResourcesReady)
            return false;

        for (uint32_t offset = 0u;
            offset < DiagnosticsGpuTimerRingCapacity;
            ++offset)
        {
            const uint32_t candidate =
                (m_NextTimerSlot + offset) %
                DiagnosticsGpuTimerRingCapacity;
            if (m_TimerPending[candidate] || !m_TimerQueries[candidate])
                continue;

            commandList->beginTimerQuery(m_TimerQueries[candidate]);
            m_TimerFrameIndices[candidate] = frameIndex;
            m_NextTimerSlot =
                (candidate + 1u) % DiagnosticsGpuTimerRingCapacity;
            slotIndex = candidate;
            return true;
        }
        return false;
    }

    void AdaptiveMsaaDiagnosticsGpuPass::EndTimerQuery(
        nvrhi::ICommandList* commandList,
        uint32_t slotIndex)
    {
        if (!commandList ||
            slotIndex >= DiagnosticsGpuTimerRingCapacity ||
            !m_TimerQueries[slotIndex])
        {
            return;
        }
        commandList->endTimerQuery(m_TimerQueries[slotIndex]);
        m_TimerPending[slotIndex] = true;
    }

    nvrhi::BindingSetHandle AdaptiveMsaaDiagnosticsGpuPass::CreateBindingSet(
        const AdaptiveMsaaDiagnosticsPassInputs& inputs,
        const AdaptiveMsaaDiagnosticsPassPlan& plan,
        uint32_t historyReadIndex,
        uint32_t historyWriteIndex) const
    {
        if (!m_Device || !m_BindingLayout)
            return nullptr;

        const auto colorInput = [this](
            nvrhi::ITexture* input,
            uint32_t availability,
            uint32_t mask) -> nvrhi::ITexture*
        {
            return (availability & mask) != 0u
                ? input
                : m_DummyColorInput.Get();
        };

        nvrhi::BindingSetDesc set;
        set.bindings = {
            nvrhi::BindingSetItem::PushConstants(0, sizeof(GpuConstants)),
            nvrhi::BindingSetItem::Texture_SRV(0, inputs.surfaceKeys),
            nvrhi::BindingSetItem::Texture_SRV(1, inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(
                2,
                colorInput(
                    inputs.referenceHdr,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReferenceHdr)),
            nvrhi::BindingSetItem::Texture_SRV(
                3,
                colorInput(
                    inputs.adaptiveHdr,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputAdaptiveHdr)),
            nvrhi::BindingSetItem::Texture_SRV(
                4,
                colorInput(
                    inputs.referenceAttributes,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReferenceAttributes)),
            nvrhi::BindingSetItem::Texture_SRV(
                5,
                colorInput(
                    inputs.reconstructedAttributes,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReconstructedAttributes)),
            nvrhi::BindingSetItem::Texture_SRV(
                6,
                (plan.inputAvailabilityMask & DiagnosticsInputSampleStatus) !=
                        0u
                    ? inputs.sampleStatus
                    : m_DummySampleStatus.Get()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                7,
                (plan.inputAvailabilityMask &
                    DiagnosticsInputSelectedSampleData) != 0u
                    ? inputs.selectedSampleData
                    : m_DummySelectedSampleData.Get()),
            nvrhi::BindingSetItem::StructuredBuffer_SRV(
                8,
                (plan.inputAvailabilityMask &
                    DiagnosticsInputPixelMetadata) != 0u
                    ? inputs.pixelMetadata
                    : m_DummyPixelMetadata.Get()),
            nvrhi::BindingSetItem::Texture_SRV(
                9,
                (plan.inputAvailabilityMask &
                    DiagnosticsInputRasterSurfaceHits) != 0u
                    ? inputs.rasterSurfaceHits
                    // The shader never loads t9 without the availability bit.
                    // Reuse the exact-sample-count depth SRV as a typed dummy
                    // so no benchmark-only allocation is retained.
                    : inputs.depth),
            nvrhi::BindingSetItem::Texture_SRV(
                10,
                (plan.inputAvailabilityMask &
                    DiagnosticsInputReferenceDepth) != 0u
                    ? inputs.referenceDepth
                    : m_DummyScalarInput.Get()),
            nvrhi::BindingSetItem::Texture_SRV(
                11,
                (plan.inputAvailabilityMask &
                    DiagnosticsInputReconstructedDepth) != 0u
                    ? inputs.reconstructedDepth
                    : m_DummyScalarInput.Get()),
            nvrhi::BindingSetItem::Texture_SRV(
                12,
                colorInput(inputs.referenceDiffuse,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReferenceDiffuse)),
            nvrhi::BindingSetItem::Texture_SRV(
                13,
                colorInput(inputs.reconstructedDiffuse,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReconstructedDiffuse)),
            nvrhi::BindingSetItem::Texture_SRV(
                14,
                colorInput(inputs.referenceMaterial,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReferenceMaterial)),
            nvrhi::BindingSetItem::Texture_SRV(
                15,
                colorInput(inputs.reconstructedMaterial,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReconstructedMaterial)),
            nvrhi::BindingSetItem::Texture_SRV(
                16,
                colorInput(inputs.referenceNormals,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReferenceNormals)),
            nvrhi::BindingSetItem::Texture_SRV(
                17,
                colorInput(inputs.reconstructedNormals,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReconstructedNormals)),
            nvrhi::BindingSetItem::Texture_SRV(
                18,
                colorInput(inputs.referenceEmissive,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReferenceEmissive)),
            nvrhi::BindingSetItem::Texture_SRV(
                19,
                colorInput(inputs.reconstructedEmissive,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReconstructedEmissive)),
            nvrhi::BindingSetItem::Texture_SRV(
                20,
                (plan.inputAvailabilityMask &
                    DiagnosticsInputReferenceAmbientOcclusion) != 0u
                    ? inputs.referenceAmbientOcclusion
                    : m_DummyScalarInput.Get()),
            nvrhi::BindingSetItem::Texture_SRV(
                21,
                (plan.inputAvailabilityMask &
                    DiagnosticsInputReconstructedAmbientOcclusion) != 0u
                    ? inputs.reconstructedAmbientOcclusion
                    : m_DummyScalarInput.Get()),
            nvrhi::BindingSetItem::Texture_SRV(
                22,
                colorInput(inputs.referenceMotion,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReferenceMotion)),
            nvrhi::BindingSetItem::Texture_SRV(
                23,
                colorInput(inputs.reconstructedMotion,
                    plan.inputAvailabilityMask,
                    DiagnosticsInputReconstructedMotion)),
            nvrhi::BindingSetItem::Texture_SRV(
                24,
                plan.gpuErrorHistoryEnabled &&
                        historyReadIndex < m_ReferenceHistory.size()
                    ? m_ReferenceHistory[historyReadIndex].Get()
                    : m_DummyColorInput.Get()),
            nvrhi::BindingSetItem::Texture_SRV(
                25,
                plan.gpuErrorHistoryEnabled &&
                        historyReadIndex < m_AdaptiveHistory.size()
                    ? m_AdaptiveHistory[historyReadIndex].Get()
                    : m_DummyColorInput.Get()),
            nvrhi::BindingSetItem::Texture_SRV(
                26,
                (plan.inputAvailabilityMask &
                    DiagnosticsInputVisibilityClass) != 0u
                    ? inputs.visibilityClass
                    // The shader never loads t26 without the availability
                    // bit. Reuse the matching MSAA key resource as a
                    // descriptor-only dummy and retain no benchmark resource.
                    : inputs.surfaceKeys),
            nvrhi::BindingSetItem::Texture_UAV(
                0,
                inputs.output ? inputs.output : m_DummyOutput.Get()),
            nvrhi::BindingSetItem::RawBuffer_UAV(1, m_GpuReadback),
            nvrhi::BindingSetItem::Texture_UAV(
                2,
                plan.gpuErrorMetricsEnabled
                    ? m_ReconstructionError0.Get()
                    : m_DummyOutput.Get()),
            nvrhi::BindingSetItem::Texture_UAV(
                3,
                plan.gpuErrorMetricsEnabled
                    ? m_ReconstructionError1.Get()
                    : m_DummyOutput.Get()),
            nvrhi::BindingSetItem::Texture_UAV(
                4,
                plan.gpuErrorMetricsEnabled
                    ? m_LightingError.Get()
                    : m_DummyOutput.Get()),
            nvrhi::BindingSetItem::Texture_UAV(
                5,
                plan.gpuErrorMetricsEnabled
                    ? m_ErrorValidity.Get()
                    : m_DummyUintOutput.Get()),
            nvrhi::BindingSetItem::Texture_UAV(
                6,
                plan.gpuErrorHistoryEnabled &&
                        !plan.frozenGpuErrorHistoryReadOnly &&
                        historyWriteIndex < m_ReferenceHistory.size()
                    ? m_ReferenceHistory[historyWriteIndex].Get()
                    : m_DummyOutput.Get()),
            nvrhi::BindingSetItem::Texture_UAV(
                7,
                plan.gpuErrorHistoryEnabled &&
                        !plan.frozenGpuErrorHistoryReadOnly &&
                        historyWriteIndex < m_AdaptiveHistory.size()
                    ? m_AdaptiveHistory[historyWriteIndex].Get()
                    : m_DummyOutput.Get())
        };
        return m_Device->createBindingSet(set, m_BindingLayout);
    }

    AdaptiveMsaaDiagnosticsPassResult
        AdaptiveMsaaDiagnosticsGpuPass::Execute(
        nvrhi::ICommandList* commandList,
        const AdaptiveMsaaDiagnosticsPassInputs& inputs,
        const AdaptiveMsaaDiagnosticsPassRequest& request)
    {
        AdaptiveMsaaDiagnosticsPassResult result;

        // Resolve the zero-work gate before inspecting any external resource.
        AdaptiveMsaaDiagnosticsPassPlanDesc preliminaryDescription;
        preliminaryDescription.settings = request.settings;
        preliminaryDescription.gpuErrorMetricsEnabled =
            request.gpuErrorMetricsEnabled;
        preliminaryDescription.gpuErrorHistoryEnabled =
            request.gpuErrorHistoryEnabled;
        preliminaryDescription.frozenGpuErrorHistoryReadOnly =
            request.frozenGpuErrorHistoryReadOnly;
        preliminaryDescription.resetGpuErrorHistory =
            request.resetGpuErrorHistory;
        preliminaryDescription.visualizedErrorDomain =
            request.visualizedErrorDomain;
        preliminaryDescription.reconstructionChannel =
            request.reconstructionChannel;
        preliminaryDescription.historyIdentity = request.historyIdentity;
        const AdaptiveMsaaDiagnosticsPassPlan preliminary =
            BuildAdaptiveMsaaDiagnosticsPassPlan(preliminaryDescription);
        if (preliminary.status == DiagnosticsPassStatus::Disabled ||
            preliminary.status == DiagnosticsPassStatus::NoWork ||
            preliminary.status == DiagnosticsPassStatus::InvalidSettings ||
            preliminary.status == DiagnosticsPassStatus::ReadbackRequired)
        {
            result.status = preliminary.status;
            return result;
        }

        AdaptiveMsaaDiagnosticsPassPlanDesc description =
            DescribePlan(inputs, request);
        const AdaptiveMsaaDiagnosticsPassPlan plan =
            BuildAdaptiveMsaaDiagnosticsPassPlan(description);
        result.status = plan.status;
        result.viewAvailable = plan.viewAvailable;
        result.dispatchWidth = plan.dispatchWidth;
        result.dispatchHeight = plan.dispatchHeight;
        if (plan.status != DiagnosticsPassStatus::Success)
            return result;
        if (!commandList)
        {
            result.status = DiagnosticsPassStatus::MissingRequiredInput;
            return result;
        }
        const nvrhi::ITexture* comparedInputs[] = {
            inputs.surfaceKeys,
            inputs.depth,
            inputs.visibilityClass,
            inputs.referenceHdr,
            inputs.adaptiveHdr,
            inputs.referenceAttributes,
            inputs.reconstructedAttributes,
            inputs.referenceDepth,
            inputs.reconstructedDepth,
            inputs.referenceDiffuse,
            inputs.reconstructedDiffuse,
            inputs.referenceMaterial,
            inputs.reconstructedMaterial,
            inputs.referenceNormals,
            inputs.reconstructedNormals,
            inputs.referenceEmissive,
            inputs.reconstructedEmissive,
            inputs.referenceAmbientOcclusion,
            inputs.reconstructedAmbientOcclusion,
            inputs.referenceMotion,
            inputs.reconstructedMotion,
            inputs.sampleStatus
        };
        if (inputs.output && std::find(
                std::begin(comparedInputs),
                std::end(comparedInputs),
                inputs.output) != std::end(comparedInputs))
        {
            result.status = DiagnosticsPassStatus::IncompatibleResources;
            return result;
        }
        if (inputs.sampleStatus && inputs.sampleStatus == inputs.surfaceKeys)
        {
            result.status = DiagnosticsPassStatus::IncompatibleResources;
            return result;
        }
        if (inputs.visibilityClass &&
            (inputs.visibilityClass == inputs.surfaceKeys ||
                inputs.visibilityClass == inputs.depth))
        {
            result.status = DiagnosticsPassStatus::IncompatibleResources;
            return result;
        }
        if (plan.readbackRequested &&
            m_ReadbackState.OutstandingCount() >=
                DiagnosticsReadbackRingCapacity)
        {
            result.status = DiagnosticsPassStatus::ReadbackRingFull;
            return result;
        }
        if (m_PendingHistoryWrite.valid)
        {
            result.status = DiagnosticsPassStatus::HistorySubmissionPending;
            return result;
        }

        DiagnosticsPassStatus status = EnsureCoreResources(plan.sampleCount);
        if (status != DiagnosticsPassStatus::Success)
        {
            result.status = status;
            return result;
        }
        if (plan.readbackRequested)
        {
            status = EnsureReadbackResources();
            if (status != DiagnosticsPassStatus::Success)
            {
                result.status = status;
                return result;
            }
        }
        if (plan.operations.timerQuery)
        {
            status = EnsureTimerResources();
            if (status != DiagnosticsPassStatus::Success)
            {
                result.status = status;
                return result;
            }
            // Polling remains nonblocking and is reached only through the
            // explicit pass-timing gate. Disabled or untimed diagnostics
            // never create, poll, reset, begin, or end timer queries.
            AdvanceTimerQueries();
            result.timing = m_LatestGpuTiming;
        }

        DiagnosticsGpuHistoryTransition historyTransition =
            DiagnosticsGpuHistoryTransition::Disabled;
        uint32_t historyReadIndex = m_HistoryReadIndex;
        uint32_t historyWriteIndex = 1u - m_HistoryReadIndex;
        if (plan.gpuErrorMetricsEnabled)
        {
            status = EnsureErrorMetricResources(
                plan.width,
                plan.height,
                plan.gpuErrorHistoryEnabled);
            if (status != DiagnosticsPassStatus::Success)
            {
                result.status = status;
                return result;
            }
        }
        if (plan.gpuErrorHistoryEnabled)
        {
            const DiagnosticsGpuHistoryKey key = {
                plan.width,
                plan.height,
                plan.sampleCount,
                0u,
                plan.historyIdentity.sceneEpoch,
                plan.historyIdentity.modeEpoch
            };
            if (plan.frozenGpuErrorHistoryReadOnly)
            {
                status = ValidateDiagnosticsGpuFrozenHistoryRead(
                    key,
                    m_HistoryState);
                if (status == DiagnosticsPassStatus::Success)
                {
                    historyTransition =
                        DiagnosticsGpuHistoryTransition::Continue;
                }
            }
            else
            {
                status = EvaluateDiagnosticsGpuHistoryTransition(
                    true,
                    plan.resetGpuErrorHistory,
                    key,
                    request.frameIndex,
                    m_HistoryState,
                    historyTransition);
            }
            if (status != DiagnosticsPassStatus::Success)
            {
                result.status = status;
                return result;
            }
            historyReadIndex = m_HistoryReadIndex;
            historyWriteIndex = 1u - historyReadIndex;
        }

        DiagnosticsReadbackTicket ticket;
        if (plan.readbackRequested)
        {
            ticket = m_ReadbackState.Acquire();
            if (!ticket.Succeeded())
            {
                result.status = ticket.status;
                return result;
            }
        }

        const uint32_t pipelineIndex = PipelineIndex(plan.sampleCount);
        nvrhi::BindingSetHandle bindingSet = CreateBindingSet(
            inputs,
            plan,
            historyReadIndex,
            historyWriteIndex);
        if (!bindingSet || pipelineIndex == DiagnosticsGpuInvalidWord ||
            !m_Pipelines[pipelineIndex])
        {
            if (ticket.Succeeded())
                (void)m_ReadbackState.CancelRecorded(ticket.token);
            result.status = DiagnosticsPassStatus::ResourceCreationFailed;
            return result;
        }

        if (plan.readbackRequested)
        {
            const DiagnosticsGpuReadbackBlock initial =
                MakeInitialDiagnosticsGpuReadbackBlock(plan, description);
            commandList->writeBuffer(
                m_GpuReadback,
                &initial,
                sizeof(initial));
        }

        GpuConstants constants;
        constants.width = plan.width;
        constants.height = plan.height;
        constants.sampleCount = plan.sampleCount;
        constants.requestedView =
            static_cast<uint32_t>(plan.shaderRequestedView);
        constants.comparisonMode =
            static_cast<uint32_t>(request.settings.comparisonMode);
        constants.visualizedSampleIndex = request.visualizedSampleIndex;
        constants.inputAvailabilityMask = plan.inputAvailabilityMask;
        if (request.settings.countersEnabled)
            constants.executionFlags |= ExecuteCounters;
        if (request.settings.selectedPixelCaptureRequested)
            constants.executionFlags |= ExecuteSelectedPixel;
        if (plan.viewAvailable)
            constants.executionFlags |= ExecuteViewAvailable;
        if (plan.shaderRequestedView !=
            AdaptiveMsaaDebugView::Disabled)
        {
            constants.executionFlags |= ExecuteVisualization;
        }
        if (plan.referencePerSample)
            constants.executionFlags |= ExecuteReferencePerSample;
        constants.selectedPixelX = request.settings.selectedPixelX;
        constants.selectedPixelY = request.settings.selectedPixelY;
        constants.captureIdLow =
            LowWord(request.settings.selectedPixelCaptureId);
        constants.captureIdHigh =
            HighWord(request.settings.selectedPixelCaptureId);
        constants.frameIndexLow = LowWord(request.frameIndex);
        constants.frameIndexHigh = HighWord(request.frameIndex);
        constants.counterValidityMask = plan.counterValidityMask;
        constants.maximumWorkListCapacity = plan.maximumWorkListCapacity;
        constants.errorDomain =
            static_cast<uint32_t>(plan.visualizedErrorDomain);
        constants.reconstructionChannel =
            static_cast<uint32_t>(plan.reconstructionChannel);
        if (plan.gpuErrorMetricsEnabled)
            constants.errorExecutionFlags |= ErrorExecuteMetrics;
        if (plan.gpuErrorHistoryEnabled &&
            !plan.frozenGpuErrorHistoryReadOnly)
        {
            constants.errorExecutionFlags |= ErrorExecuteHistoryWrite;
        }
        if (historyTransition ==
            DiagnosticsGpuHistoryTransition::Continue)
        {
            constants.errorExecutionFlags |= ErrorExecuteHistoryRead;
            constants.historyValid = 1u;
        }

        uint32_t timerSlot = DiagnosticsGpuInvalidWord;
        if (plan.operations.timerQuery)
        {
            result.timerQueryRecorded = BeginTimerQuery(
                commandList,
                request.frameIndex,
                timerSlot);
        }
        commandList->beginMarker("Adaptive MSAA Diagnostics");
        nvrhi::ComputeState computeState;
        computeState.pipeline = m_Pipelines[pipelineIndex];
        computeState.bindings = { bindingSet };
        commandList->setComputeState(computeState);
        commandList->setPushConstants(&constants, sizeof(constants));
        commandList->dispatch(
            plan.dispatchWidth,
            plan.dispatchHeight,
            1u);

        if (plan.readbackRequested)
        {
            ReadbackSlot& slot = m_ReadbackSlots[ticket.slotIndex];
            commandList->setBufferState(
                m_GpuReadback,
                nvrhi::ResourceStates::CopySource);
            commandList->setBufferState(
                slot.buffer,
                nvrhi::ResourceStates::CopyDest);
            commandList->commitBarriers();
            commandList->copyBuffer(
                slot.buffer,
                0u,
                m_GpuReadback,
                0u,
                sizeof(DiagnosticsGpuReadbackBlock));
        }
        commandList->endMarker();
        if (result.timerQueryRecorded)
            EndTimerQuery(commandList, timerSlot);

        result.status = DiagnosticsPassStatus::Success;
        result.dispatched = true;
        result.readbackRecorded = plan.readbackRequested;
        result.readbackToken = plan.readbackRequested ? ticket.token : 0u;
        if (plan.gpuErrorMetricsEnabled)
        {
            result.errorOutputs.reconstruction0 =
                m_ReconstructionError0.Get();
            result.errorOutputs.reconstruction1 =
                m_ReconstructionError1.Get();
            result.errorOutputs.lighting = m_LightingError.Get();
            result.errorOutputs.validity = m_ErrorValidity.Get();
            result.errorOutputs.currentFrameRecorded = true;
            result.errorOutputs.temporalHistoryRead =
                historyTransition ==
                    DiagnosticsGpuHistoryTransition::Continue;
            result.errorOutputs.frozenHistoryReadOnly =
                result.errorOutputs.temporalHistoryRead &&
                plan.frozenGpuErrorHistoryReadOnly;
            result.errorOutputs.historyTransition = historyTransition;
        }
        if (plan.gpuErrorHistoryEnabled &&
            !plan.frozenGpuErrorHistoryReadOnly)
        {
            uint64_t historyToken = m_NextHistoryToken++;
            if (historyToken == 0u)
            {
                historyToken = m_NextHistoryToken++;
            }
            m_PendingHistoryWrite.valid = true;
            m_PendingHistoryWrite.token = historyToken;
            m_PendingHistoryWrite.frameIndex = request.frameIndex;
            m_PendingHistoryWrite.writeIndex = historyWriteIndex;
            m_PendingHistoryWrite.key = {
                plan.width,
                plan.height,
                plan.sampleCount,
                0u,
                plan.historyIdentity.sceneEpoch,
                plan.historyIdentity.modeEpoch
            };
            result.historyWriteRecorded = true;
            result.historyToken = historyToken;
        }
        return result;
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::SignalReadbackSubmission(
            uint64_t token)
    {
        const uint32_t slotIndex = m_ReadbackState.FindSlot(token);
        if (slotIndex == DiagnosticsGpuInvalidWord || token == 0u)
            return DiagnosticsPassStatus::InvalidReadbackToken;
        if (m_ReadbackState.SlotState(slotIndex) !=
            DiagnosticsReadbackSlotState::Recorded)
        {
            return DiagnosticsPassStatus::InvalidReadbackState;
        }
        if (!m_Device || !m_ReadbackResourcesReady ||
            !m_ReadbackSlots[slotIndex].eventQuery)
        {
            return DiagnosticsPassStatus::ResourceCreationFailed;
        }

        const DiagnosticsPassStatus status =
            m_ReadbackState.MarkSubmitted(token);
        if (status != DiagnosticsPassStatus::Success)
            return status;
        m_Device->setEventQuery(
            m_ReadbackSlots[slotIndex].eventQuery,
            nvrhi::CommandQueue::Graphics);
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::CancelUnsubmittedReadback(
            uint64_t token)
    {
        return m_ReadbackState.CancelRecorded(token);
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::ConfirmErrorHistorySubmission(
        uint64_t token)
    {
        if (!m_PendingHistoryWrite.valid || token == 0u ||
            token != m_PendingHistoryWrite.token)
        {
            return DiagnosticsPassStatus::InvalidHistoryToken;
        }
        m_HistoryReadIndex = m_PendingHistoryWrite.writeIndex;
        m_HistoryState.valid = true;
        m_HistoryState.key = m_PendingHistoryWrite.key;
        m_HistoryState.frameIndex = m_PendingHistoryWrite.frameIndex;
        m_PendingHistoryWrite = {};
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::CancelErrorHistorySubmission(
        uint64_t token)
    {
        if (!m_PendingHistoryWrite.valid || token == 0u ||
            token != m_PendingHistoryWrite.token)
        {
            return DiagnosticsPassStatus::InvalidHistoryToken;
        }
        m_PendingHistoryWrite = {};
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::InvalidateErrorHistory()
    {
        if (m_PendingHistoryWrite.valid)
            return DiagnosticsPassStatus::HistorySubmissionPending;
        m_HistoryState = {};
        return DiagnosticsPassStatus::Success;
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::ReleaseReadbackAfterQueueIdle(
            uint64_t token)
    {
        const uint32_t slotIndex = m_ReadbackState.FindSlot(token);
        if (slotIndex == DiagnosticsGpuInvalidWord || token == 0u)
            return DiagnosticsPassStatus::InvalidReadbackToken;
        const DiagnosticsReadbackSlotState state =
            m_ReadbackState.SlotState(slotIndex);
        if (state == DiagnosticsReadbackSlotState::Recorded)
            return m_ReadbackState.CancelRecorded(token);
        if (state != DiagnosticsReadbackSlotState::Submitted)
            return DiagnosticsPassStatus::InvalidReadbackState;
        if (m_ReadbackSlots[slotIndex].eventQuery && m_Device)
            m_Device->resetEventQuery(
                m_ReadbackSlots[slotIndex].eventQuery);
        return m_ReadbackState.MarkCompleted(token);
    }

    void AdaptiveMsaaDiagnosticsGpuPass::
        ReleaseAllReadbacksAfterQueueIdle()
    {
        for (uint32_t slotIndex = 0u;
            slotIndex < DiagnosticsReadbackRingCapacity;
            ++slotIndex)
        {
            const uint64_t token =
                m_ReadbackState.SlotToken(slotIndex);
            if (token != 0u)
                (void)ReleaseReadbackAfterQueueIdle(token);
        }
    }

    DiagnosticsReadbackPollResult
        AdaptiveMsaaDiagnosticsGpuPass::PollReadback(
        const AdaptiveMsaaDiagnosticsSettings& settings,
        DiagnosticsGpuReadbackBlock& output)
    {
        output = {};
        AdaptiveMsaaDiagnosticsSettings sanitized;
        if (SanitizeDiagnosticsSettings(settings, sanitized) !=
            DiagnosticsInputStatus::Success)
        {
            return { DiagnosticsPassStatus::InvalidSettings, false, 0u };
        }
        if (!sanitized.enabled)
            return { DiagnosticsPassStatus::Disabled, false, 0u };
        if (!m_ReadbackResourcesReady ||
            m_ReadbackState.OutstandingCount() == 0u)
        {
            return { DiagnosticsPassStatus::ReadbackEmpty, false, 0u };
        }

        const uint32_t slotIndex =
            m_ReadbackState.OldestSubmittedSlot();
        if (slotIndex != DiagnosticsGpuInvalidWord)
        {
            ReadbackSlot& slot = m_ReadbackSlots[slotIndex];
            if (!m_Device->pollEventQuery(slot.eventQuery))
            {
                return {
                    DiagnosticsPassStatus::ReadbackNotReady,
                    false,
                    0u
                };
            }

            const uint64_t token = m_ReadbackState.SlotToken(slotIndex);
            void* mapped = m_Device->mapBuffer(
                slot.buffer,
                nvrhi::CpuAccessMode::Read);
            if (!mapped)
            {
                m_Device->resetEventQuery(slot.eventQuery);
                (void)m_ReadbackState.MarkCompleted(token);
                return {
                    DiagnosticsPassStatus::ReadbackMapFailed,
                    true,
                    token
                };
            }
            std::memcpy(&output, mapped, sizeof(output));
            m_Device->unmapBuffer(slot.buffer);
            m_Device->resetEventQuery(slot.eventQuery);
            (void)m_ReadbackState.MarkCompleted(token);

            const DiagnosticsPassStatus validation =
                ValidateDiagnosticsGpuReadbackBlock(output);
            return { validation, true, token };
        }

        return {
            DiagnosticsPassStatus::ReadbackNotReady,
            false,
            0u
        };
    }

    DiagnosticsPassStatus
        AdaptiveMsaaDiagnosticsGpuPass::RetireCompletedReadbacks()
    {
        if (!m_ReadbackResourcesReady || !m_Device)
            return DiagnosticsPassStatus::ResourceCreationFailed;
        if (m_ReadbackState.OutstandingCount() == 0u)
            return DiagnosticsPassStatus::ReadbackEmpty;

        bool retiredAny = false;
        while (true)
        {
            const uint32_t slotIndex =
                m_ReadbackState.OldestSubmittedSlot();
            if (slotIndex == DiagnosticsGpuInvalidWord)
            {
                return retiredAny
                    ? DiagnosticsPassStatus::Success
                    : DiagnosticsPassStatus::InvalidReadbackState;
            }
            ReadbackSlot& slot = m_ReadbackSlots[slotIndex];
            if (!slot.eventQuery ||
                !m_Device->pollEventQuery(slot.eventQuery))
            {
                return retiredAny
                    ? DiagnosticsPassStatus::Success
                    : DiagnosticsPassStatus::ReadbackNotReady;
            }

            const uint64_t token =
                m_ReadbackState.SlotToken(slotIndex);
            m_Device->resetEventQuery(slot.eventQuery);
            const DiagnosticsPassStatus completion =
                m_ReadbackState.MarkCompleted(token);
            if (completion != DiagnosticsPassStatus::Success)
                return completion;
            retiredAny = true;
            if (m_ReadbackState.OutstandingCount() == 0u)
                return DiagnosticsPassStatus::Success;
        }
    }

    uint32_t
        AdaptiveMsaaDiagnosticsGpuPass::OutstandingReadbackCount() const
    {
        return m_ReadbackState.OutstandingCount();
    }

    bool AdaptiveMsaaDiagnosticsGpuPass::HasAllocatedGpuResources() const
    {
        if (m_CoreResourcesReady || m_ReadbackResourcesReady ||
            m_TimerResourcesReady ||
            m_BindingLayout || m_GpuReadback ||
            m_DummySelectedSampleData || m_DummyPixelMetadata ||
            m_DummyColorInput || m_DummyScalarInput ||
            m_DummySampleStatus || m_DummyOutput ||
            m_DummyUintOutput || m_ReconstructionError0 ||
            m_ReconstructionError1 || m_LightingError ||
            m_ErrorValidity || m_ErrorMetricResourcesReady ||
            m_ErrorHistoryResourcesReady)
        {
            return true;
        }
        for (const nvrhi::ShaderHandle& shader : m_Shaders)
        {
            if (shader)
                return true;
        }
        for (const nvrhi::ComputePipelineHandle& pipeline : m_Pipelines)
        {
            if (pipeline)
                return true;
        }
        for (const ReadbackSlot& slot : m_ReadbackSlots)
        {
            if (slot.buffer || slot.eventQuery)
                return true;
        }
        for (const nvrhi::TimerQueryHandle& query : m_TimerQueries)
        {
            if (query)
                return true;
        }
        for (const nvrhi::TextureHandle& texture : m_ReferenceHistory)
        {
            if (texture)
                return true;
        }
        for (const nvrhi::TextureHandle& texture : m_AdaptiveHistory)
        {
            if (texture)
                return true;
        }
        return false;
    }
}
