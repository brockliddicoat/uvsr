#pragma once

#include <nvrhi/nvrhi.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace uvsr
{
    inline constexpr std::size_t MsaaVisibilityResolveResourceCount = 7u;
    inline constexpr std::array<std::uint32_t,
        MsaaVisibilityResolveResourceCount>
        MsaaVisibilityResolveBindingSlots = { 0u, 1u, 2u, 3u, 4u, 5u, 6u };

    enum class MsaaVisibilityResolveResource : std::size_t
    {
        Depth,
        Diffuse,
        Material,
        Normals,
        Emissive,
        MaterialAmbientOcclusion,
        MotionVectors,
        Count
    };

    struct MsaaVisibilityResolveInputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* diffuse = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* emissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* motionVectors = nullptr;
    };

    struct MsaaVisibilityResolveOutputs
    {
        nvrhi::ITexture* depth = nullptr;
        nvrhi::ITexture* diffuse = nullptr;
        nvrhi::ITexture* material = nullptr;
        nvrhi::ITexture* normals = nullptr;
        nvrhi::ITexture* emissive = nullptr;
        nvrhi::ITexture* materialAmbientOcclusion = nullptr;
        nvrhi::ITexture* motionVectors = nullptr;
    };

    [[nodiscard]] inline constexpr std::array<nvrhi::ITexture*,
        MsaaVisibilityResolveResourceCount>
    GetMsaaVisibilityResolveInputTextures(
        const MsaaVisibilityResolveInputs& inputs) noexcept
    {
        return {
            inputs.depth,
            inputs.diffuse,
            inputs.material,
            inputs.normals,
            inputs.emissive,
            inputs.materialAmbientOcclusion,
            inputs.motionVectors
        };
    }

    [[nodiscard]] inline constexpr std::array<nvrhi::ITexture*,
        MsaaVisibilityResolveResourceCount>
    GetMsaaVisibilityResolveOutputTextures(
        const MsaaVisibilityResolveOutputs& outputs) noexcept
    {
        return {
            outputs.depth,
            outputs.diffuse,
            outputs.material,
            outputs.normals,
            outputs.emissive,
            outputs.materialAmbientOcclusion,
            outputs.motionVectors
        };
    }

    [[nodiscard]] inline constexpr bool
    IsMsaaVisibilityResolveSampleCountSupported(
        std::uint32_t sampleCount) noexcept
    {
        return sampleCount == 2u || sampleCount == 4u ||
            sampleCount == 8u || sampleCount == 16u;
    }

    [[nodiscard]] inline constexpr bool
    IsMsaaVisibilityResolveDepthFormatSupported(
        nvrhi::Format format) noexcept
    {
        return format == nvrhi::Format::D16 ||
            format == nvrhi::Format::D24S8 ||
            format == nvrhi::Format::D32 ||
            format == nvrhi::Format::D32S8;
    }

    inline constexpr std::array<nvrhi::Format,
        MsaaVisibilityResolveResourceCount - 1u>
        MsaaVisibilityResolveInputFormats = {
            nvrhi::Format::SRGBA8_UNORM,
            nvrhi::Format::RGBA8_UNORM,
            nvrhi::Format::RGBA16_SNORM,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::R8_UNORM,
            nvrhi::Format::RGBA16_FLOAT
        };

    inline constexpr std::array<nvrhi::Format,
        MsaaVisibilityResolveResourceCount>
        MsaaVisibilityResolveOutputFormats = {
            nvrhi::Format::R32_FLOAT,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::RGBA16_FLOAT,
            nvrhi::Format::R16_FLOAT,
            nvrhi::Format::RGBA16_FLOAT
        };

    [[nodiscard]] inline constexpr bool
    AreMsaaVisibilityResolveDescriptorsSupported(
        const std::array<nvrhi::TextureDesc,
            MsaaVisibilityResolveResourceCount>& inputs,
        const std::array<nvrhi::TextureDesc,
            MsaaVisibilityResolveResourceCount>& outputs,
        std::uint32_t sampleCount) noexcept
    {
        if (!IsMsaaVisibilityResolveSampleCountSupported(sampleCount))
            return false;
        const nvrhi::TextureDesc& depth = inputs[0];
        for (std::size_t index = 0u; index < inputs.size(); ++index)
        {
            const nvrhi::TextureDesc& descriptor = inputs[index];
            if (descriptor.width == 0u || descriptor.height == 0u ||
                descriptor.width != depth.width ||
                descriptor.height != depth.height ||
                descriptor.depth != 1u || descriptor.arraySize != 1u ||
                descriptor.mipLevels != 1u ||
                descriptor.sampleCount != sampleCount ||
                descriptor.sampleQuality != depth.sampleQuality ||
                descriptor.dimension !=
                    nvrhi::TextureDimension::Texture2DMS ||
                (index == 0u
                    ? !IsMsaaVisibilityResolveDepthFormatSupported(
                        descriptor.format)
                    : descriptor.format !=
                        MsaaVisibilityResolveInputFormats[index - 1u]))
            {
                return false;
            }
        }

        for (std::size_t index = 0u; index < outputs.size(); ++index)
        {
            const nvrhi::TextureDesc& descriptor = outputs[index];
            if (descriptor.width != depth.width ||
                descriptor.height != depth.height ||
                descriptor.depth != 1u || descriptor.arraySize != 1u ||
                descriptor.mipLevels != 1u ||
                descriptor.sampleCount != 1u ||
                descriptor.sampleQuality != 0u ||
                descriptor.dimension != nvrhi::TextureDimension::Texture2D ||
                descriptor.format != MsaaVisibilityResolveOutputFormats[index] ||
                !descriptor.isUAV)
            {
                return false;
            }
        }
        return true;
    }

    struct MsaaVisibilityResolvePipelineResources
    {
        bool device = false;
        bool shaderFactory = false;
        bool bindingLayout = false;
        bool shader = false;
        bool pipeline = false;

        [[nodiscard]] constexpr bool AreComplete() const noexcept
        {
            return device && shaderFactory && bindingLayout && shader &&
                pipeline;
        }
    };

    struct MsaaVisibilityResolveDispatchState
    {
        bool preparationReady = false;
        bool commandList = false;
        bool descriptorsValid = false;
        bool pipeline = false;
        bool bindingSet = false;
        bool dispatchSubmitted = false;

        [[nodiscard]] constexpr bool CanDispatch() const noexcept
        {
            return preparationReady && commandList && descriptorsValid &&
                pipeline && bindingSet;
        }

        [[nodiscard]] constexpr bool CanPublish() const noexcept
        {
            return CanDispatch() && dispatchSubmitted;
        }
    };

    [[nodiscard]] inline constexpr bool
    PublishMsaaVisibilityResolveOutputs(
        bool dispatchSucceeded,
        const MsaaVisibilityResolveOutputs& candidate,
        MsaaVisibilityResolveOutputs& published) noexcept
    {
        published = {};
        if (!dispatchSucceeded)
            return false;
        for (nvrhi::ITexture* texture :
            GetMsaaVisibilityResolveOutputTextures(candidate))
        {
            if (!texture)
                return false;
        }
        published = candidate;
        return true;
    }
}
