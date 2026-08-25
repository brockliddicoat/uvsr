#include "directional_shadow_settings.h"
#include "renderer_receiver_texture_contract.h"

#include <array>
#include <iostream>

namespace
{
    bool Require(bool condition, const char* message)
    {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    }

    nvrhi::TextureDesc MakeReceiverDescriptor(uint32_t sampleCount)
    {
        nvrhi::TextureDesc descriptor;
        descriptor.width = 640u;
        descriptor.height = 360u;
        descriptor.depth = 1u;
        descriptor.arraySize = 1u;
        descriptor.mipLevels = 1u;
        descriptor.sampleCount = sampleCount;
        descriptor.dimension = sampleCount == 1u
            ? nvrhi::TextureDimension::Texture2D
            : nvrhi::TextureDimension::Texture2DMS;
        return descriptor;
    }

    bool TestReceiverDescriptorContract()
    {
        bool ok = true;
        for (const uint32_t sampleCount : { 1u, 2u, 4u, 8u, 16u })
        {
            const nvrhi::TextureDesc expected =
                MakeReceiverDescriptor(sampleCount);
            ok &= Require(
                uvsr::AreRendererReceiverTextureDescriptorsCompatible(
                    expected,
                    expected,
                    expected),
                "The exact retained receiver descriptor was rejected.");

            const std::array<nvrhi::TextureDimension, 5> wrongDimensions =
                sampleCount == 1u
                ? std::array<nvrhi::TextureDimension, 5>{
                    nvrhi::TextureDimension::Texture2DArray,
                    nvrhi::TextureDimension::TextureCube,
                    nvrhi::TextureDimension::TextureCubeArray,
                    nvrhi::TextureDimension::Texture2DMS,
                    nvrhi::TextureDimension::Texture2DMSArray }
                : std::array<nvrhi::TextureDimension, 5>{
                    nvrhi::TextureDimension::Texture2D,
                    nvrhi::TextureDimension::Texture2DArray,
                    nvrhi::TextureDimension::TextureCube,
                    nvrhi::TextureDimension::TextureCubeArray,
                    nvrhi::TextureDimension::Texture2DMSArray };
            for (const nvrhi::TextureDimension wrong : wrongDimensions)
            {
                for (uint32_t inputIndex = 0u;
                    inputIndex < 3u;
                    ++inputIndex)
                {
                    std::array<nvrhi::TextureDesc, 3> inputs = {
                        expected, expected, expected
                    };
                    inputs[inputIndex].dimension = wrong;
                    ok &= Require(
                        !uvsr::AreRendererReceiverTextureDescriptorsCompatible(
                            inputs[0],
                            inputs[1],
                            inputs[2]),
                        "Directional visibility accepted an array, cube, "
                        "or wrong multisample input dimension.");
                }
            }

            for (uint32_t inputIndex = 0u;
                inputIndex < 3u;
                ++inputIndex)
            {
                std::array<nvrhi::TextureDesc, 3> inputs = {
                    expected, expected, expected
                };
                inputs[inputIndex].arraySize = 2u;
                ok &= Require(
                    !uvsr::AreRendererReceiverTextureDescriptorsCompatible(
                        inputs[0], inputs[1], inputs[2]),
                    "Directional visibility accepted an array receiver.");
                inputs = { expected, expected, expected };
                inputs[inputIndex].mipLevels = 2u;
                ok &= Require(
                    !uvsr::AreRendererReceiverTextureDescriptorsCompatible(
                        inputs[0], inputs[1], inputs[2]),
                    "Directional visibility accepted mipmapped receivers.");
            }
        }

        nvrhi::TextureDesc depth = MakeReceiverDescriptor(4u);
        nvrhi::TextureDesc material = depth;
        nvrhi::TextureDesc normals = depth;
        material.sampleQuality = 1u;
        ok &= Require(
            !uvsr::AreRendererReceiverTextureDescriptorsCompatible(
                depth, material, normals),
            "Directional visibility accepted mismatched sample quality.");
        normals = depth;
        normals.width -= 1u;
        ok &= Require(
            !uvsr::AreRendererReceiverTextureDescriptorsCompatible(
                depth, depth, normals),
            "Directional visibility accepted mismatched receiver extents.");
        return ok;
    }
}

int main()
{
    bool ok = true;
    const uvsr::DirectionalShadowSettings defaults{};
    ok &= Require(
        defaults.enabled &&
            uvsr::IsDirectionalShadowSettingsValid(defaults),
        "Directional ray visibility defaults must be enabled and valid.");
    for (const unsigned count : { 1u, 2u, 4u, 8u, 16u })
    {
        ok &= Require(
            uvsr::IsDirectionalReceiverSampleCountSupported(count),
            "Every retained raster sample count must be supported.");
    }
    ok &= Require(
        !uvsr::IsDirectionalReceiverSampleCountSupported(0u) &&
            !uvsr::IsDirectionalReceiverSampleCountSupported(3u) &&
            !uvsr::IsDirectionalReceiverSampleCountSupported(32u),
        "Unsupported raster sample counts must fail closed.");
    ok &= TestReceiverDescriptorContract();
    uvsr::DirectionalShadowSettings invalid = defaults;
    invalid.rayBias = uvsr::DirectionalShadowMaximumRayBias + 0.001f;
    ok &= Require(
        !uvsr::IsDirectionalShadowSettingsValid(invalid),
        "Out-of-range ray bias must be rejected.");
    return ok ? 0 : 1;
}
