#include "msaa_visibility_resolve_contract.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "MSAA visibility resolve contract failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    std::array<nvrhi::TextureDesc,
        uvsr::MsaaVisibilityResolveResourceCount>
    MakeInputDescriptors(std::uint32_t sampleCount)
    {
        std::array<nvrhi::TextureDesc,
            uvsr::MsaaVisibilityResolveResourceCount> descriptors;
        for (std::size_t index = 0u; index < descriptors.size(); ++index)
        {
            nvrhi::TextureDesc& descriptor = descriptors[index];
            descriptor.width = 640u;
            descriptor.height = 360u;
            descriptor.depth = 1u;
            descriptor.arraySize = 1u;
            descriptor.mipLevels = 1u;
            descriptor.sampleCount = sampleCount;
            descriptor.sampleQuality = 0u;
            descriptor.dimension = nvrhi::TextureDimension::Texture2DMS;
            descriptor.format = index == 0u
                ? nvrhi::Format::D32
                : uvsr::MsaaVisibilityResolveInputFormats[index - 1u];
        }
        return descriptors;
    }

    std::array<nvrhi::TextureDesc,
        uvsr::MsaaVisibilityResolveResourceCount>
    MakeOutputDescriptors()
    {
        std::array<nvrhi::TextureDesc,
            uvsr::MsaaVisibilityResolveResourceCount> descriptors;
        for (std::size_t index = 0u; index < descriptors.size(); ++index)
        {
            nvrhi::TextureDesc& descriptor = descriptors[index];
            descriptor.width = 640u;
            descriptor.height = 360u;
            descriptor.depth = 1u;
            descriptor.arraySize = 1u;
            descriptor.mipLevels = 1u;
            descriptor.sampleCount = 1u;
            descriptor.sampleQuality = 0u;
            descriptor.dimension = nvrhi::TextureDimension::Texture2D;
            descriptor.format =
                uvsr::MsaaVisibilityResolveOutputFormats[index];
            descriptor.isUAV = true;
        }
        return descriptors;
    }

    void TestExactResourceOrder()
    {
        static_assert(
            static_cast<std::size_t>(
                uvsr::MsaaVisibilityResolveResource::Depth) == 0u);
        static_assert(
            static_cast<std::size_t>(
                uvsr::MsaaVisibilityResolveResource::Diffuse) == 1u);
        static_assert(
            static_cast<std::size_t>(
                uvsr::MsaaVisibilityResolveResource::Material) == 2u);
        static_assert(
            static_cast<std::size_t>(
                uvsr::MsaaVisibilityResolveResource::Normals) == 3u);
        static_assert(
            static_cast<std::size_t>(
                uvsr::MsaaVisibilityResolveResource::Emissive) == 4u);
        static_assert(
            static_cast<std::size_t>(uvsr::
                MsaaVisibilityResolveResource::MaterialAmbientOcclusion) ==
                5u);
        static_assert(
            static_cast<std::size_t>(
                uvsr::MsaaVisibilityResolveResource::MotionVectors) == 6u);
        static_assert(
            static_cast<std::size_t>(
                uvsr::MsaaVisibilityResolveResource::Count) == 7u);
        static_assert(uvsr::MsaaVisibilityResolveBindingSlots[0] == 0u);
        static_assert(uvsr::MsaaVisibilityResolveBindingSlots[1] == 1u);
        static_assert(uvsr::MsaaVisibilityResolveBindingSlots[2] == 2u);
        static_assert(uvsr::MsaaVisibilityResolveBindingSlots[3] == 3u);
        static_assert(uvsr::MsaaVisibilityResolveBindingSlots[4] == 4u);
        static_assert(uvsr::MsaaVisibilityResolveBindingSlots[5] == 5u);
        static_assert(uvsr::MsaaVisibilityResolveBindingSlots[6] == 6u);

        std::array<std::max_align_t, 7> storage{};
        std::array<nvrhi::ITexture*, 7> textures{};
        for (std::size_t index = 0u; index < textures.size(); ++index)
        {
            textures[index] = reinterpret_cast<nvrhi::ITexture*>(
                &storage[index]);
        }
        const uvsr::MsaaVisibilityResolveInputs inputs = {
            textures[0], textures[1], textures[2], textures[3],
            textures[4], textures[5], textures[6]
        };
        const uvsr::MsaaVisibilityResolveOutputs outputs = {
            textures[0], textures[1], textures[2], textures[3],
            textures[4], textures[5], textures[6]
        };
        Require(
            uvsr::GetMsaaVisibilityResolveInputTextures(inputs) == textures,
            "input field order drifted from t0..t6");
        Require(
            uvsr::GetMsaaVisibilityResolveOutputTextures(outputs) == textures,
            "output field order drifted from u0..u6");
    }

    void TestDescriptorValidation()
    {
        for (const std::uint32_t sampleCount : { 2u, 4u, 8u, 16u })
        {
            const auto inputs = MakeInputDescriptors(sampleCount);
            const auto outputs = MakeOutputDescriptors();
            Require(
                uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    inputs, outputs, sampleCount),
                "valid 7-in/7-out topology was rejected");

            for (std::size_t index = 0u; index < inputs.size(); ++index)
            {
                auto invalid = inputs;
                invalid[index].arraySize = 2u;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    invalid, outputs, sampleCount),
                    "array input topology was accepted");
                invalid = inputs;
                invalid[index].mipLevels = 2u;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    invalid, outputs, sampleCount),
                    "mipmapped input topology was accepted");
                invalid = inputs;
                invalid[index].dimension =
                    nvrhi::TextureDimension::Texture2DMSArray;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    invalid, outputs, sampleCount),
                    "multisample-array input was accepted");
                invalid = inputs;
                invalid[index].width -= 1u;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    invalid, outputs, sampleCount),
                    "mismatched input extent was accepted");
                invalid = inputs;
                invalid[index].format = nvrhi::Format::R32_FLOAT;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    invalid, outputs, sampleCount),
                    "wrong input semantic format was accepted");
            }

            for (std::size_t index = 0u; index < outputs.size(); ++index)
            {
                auto invalid = outputs;
                invalid[index].arraySize = 2u;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    inputs, invalid, sampleCount),
                    "array output topology was accepted");
                invalid = outputs;
                invalid[index].mipLevels = 2u;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    inputs, invalid, sampleCount),
                    "mipmapped output topology was accepted");
                invalid = outputs;
                invalid[index].dimension =
                    nvrhi::TextureDimension::Texture2DArray;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    inputs, invalid, sampleCount),
                    "array output dimension was accepted");
                invalid = outputs;
                invalid[index].format = nvrhi::Format::R8_UINT;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    inputs, invalid, sampleCount),
                    "wrong output semantic format was accepted");
                invalid = outputs;
                invalid[index].isUAV = false;
                Require(!uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                    inputs, invalid, sampleCount),
                    "non-UAV output was accepted");
            }
        }

        Require(
            !uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                MakeInputDescriptors(2u), MakeOutputDescriptors(), 1u) &&
            !uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                MakeInputDescriptors(2u), MakeOutputDescriptors(), 3u) &&
            !uvsr::AreMsaaVisibilityResolveDescriptorsSupported(
                MakeInputDescriptors(2u), MakeOutputDescriptors(), 32u),
            "unsupported sample count was accepted");
    }

    void TestInjectedPreparationAndDispatchFailures()
    {
        const uvsr::MsaaVisibilityResolvePipelineResources complete = {
            true, true, true, true, true
        };
        Require(complete.AreComplete(),
            "complete pipeline preparation was rejected");
        bool uvsr::MsaaVisibilityResolvePipelineResources::* const stages[] = {
            &uvsr::MsaaVisibilityResolvePipelineResources::device,
            &uvsr::MsaaVisibilityResolvePipelineResources::shaderFactory,
            &uvsr::MsaaVisibilityResolvePipelineResources::bindingLayout,
            &uvsr::MsaaVisibilityResolvePipelineResources::shader,
            &uvsr::MsaaVisibilityResolvePipelineResources::pipeline
        };
        for (auto stage : stages)
        {
            uvsr::MsaaVisibilityResolvePipelineResources failed = complete;
            failed.*stage = false;
            Require(!failed.AreComplete(),
                "injected pipeline creation failure was published ready");
        }

        const uvsr::MsaaVisibilityResolveDispatchState submitted = {
            true, true, true, true, true, true
        };
        Require(submitted.CanDispatch() && submitted.CanPublish(),
            "complete dispatch was rejected");
        bool uvsr::MsaaVisibilityResolveDispatchState::* const dispatchStages[] = {
            &uvsr::MsaaVisibilityResolveDispatchState::preparationReady,
            &uvsr::MsaaVisibilityResolveDispatchState::commandList,
            &uvsr::MsaaVisibilityResolveDispatchState::descriptorsValid,
            &uvsr::MsaaVisibilityResolveDispatchState::pipeline,
            &uvsr::MsaaVisibilityResolveDispatchState::bindingSet,
            &uvsr::MsaaVisibilityResolveDispatchState::dispatchSubmitted
        };
        for (auto stage : dispatchStages)
        {
            uvsr::MsaaVisibilityResolveDispatchState failed = submitted;
            failed.*stage = false;
            Require(!failed.CanPublish(),
                "injected preparation/binding/dispatch failure published");
        }
    }

    void TestAtomicOutputPublication()
    {
        std::array<std::max_align_t, 14> storage{};
        const auto pointer = [&storage](std::size_t index)
        {
            return reinterpret_cast<nvrhi::ITexture*>(&storage[index]);
        };
        const uvsr::MsaaVisibilityResolveOutputs candidate = {
            pointer(0u), pointer(1u), pointer(2u), pointer(3u),
            pointer(4u), pointer(5u), pointer(6u)
        };
        uvsr::MsaaVisibilityResolveOutputs published = {
            pointer(7u), pointer(8u), pointer(9u), pointer(10u),
            pointer(11u), pointer(12u), pointer(13u)
        };
        Require(!uvsr::PublishMsaaVisibilityResolveOutputs(
                false, candidate, published) &&
            uvsr::GetMsaaVisibilityResolveOutputTextures(published) ==
                std::array<nvrhi::ITexture*, 7>{},
            "failed dispatch retained stale resolved outputs");

        uvsr::MsaaVisibilityResolveOutputs incomplete = candidate;
        incomplete.normals = nullptr;
        Require(!uvsr::PublishMsaaVisibilityResolveOutputs(
                true, incomplete, published) &&
            uvsr::GetMsaaVisibilityResolveOutputTextures(published) ==
                std::array<nvrhi::ITexture*, 7>{},
            "partial output set was published");
        Require(uvsr::PublishMsaaVisibilityResolveOutputs(
                true, candidate, published) &&
            uvsr::GetMsaaVisibilityResolveOutputTextures(published) ==
                uvsr::GetMsaaVisibilityResolveOutputTextures(candidate),
            "successful dispatch did not publish all seven outputs");
    }
}

int main()
{
    TestExactResourceOrder();
    TestDescriptorValidation();
    TestInjectedPreparationAndDispatchFailures();
    TestAtomicOutputPublication();
    return EXIT_SUCCESS;
}
