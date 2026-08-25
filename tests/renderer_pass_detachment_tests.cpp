#include "image_based_lighting_environment.h"
#include "renderer_geometry_passes.h"
#include "renderer_light_probe_contract.h"
#include "renderer_light_probe_processing.h"
#include "renderer_targets.h"

#include <DirectXMath.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Renderer pass detachment test failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void TestGeometryInitializationFailures()
    {
        using uvsr::RendererGeometryInitializationContract;
        const RendererGeometryInitializationContract complete = {
            true, true, true, true, true, true,
            true, true, true, true, true, true
        };
        Require(complete.IsComplete(),
            "complete geometry resources were rejected");
        bool RendererGeometryInitializationContract::* const fields[] = {
            &RendererGeometryInitializationContract::device,
            &RendererGeometryInitializationContract::shaderFactory,
            &RendererGeometryInitializationContract::fallbackTexture,
            &RendererGeometryInitializationContract::vertexShader,
            &RendererGeometryInitializationContract::pixelShader,
            &RendererGeometryInitializationContract::alphaTestedPixelShader,
            &RendererGeometryInitializationContract::materialLayout,
            &RendererGeometryInitializationContract::viewLayout,
            &RendererGeometryInitializationContract::inputLayout,
            &RendererGeometryInitializationContract::viewConstantBuffer,
            &RendererGeometryInitializationContract::materialSampler,
            &RendererGeometryInitializationContract::viewBindingSet
        };
        for (auto field : fields)
        {
            RendererGeometryInitializationContract failed = complete;
            failed.*field = false;
            Require(!failed.IsComplete(),
                "injected geometry resource failure was published");
        }
    }

    void TestLightProbeInitializationFailures()
    {
        using uvsr::RendererLightProbeInitializationContract;
        const RendererLightProbeInitializationContract complete = {
            true, true, true, true, true, true, true,
            true, true, true, true, true, true
        };
        Require(complete.IsComplete(),
            "complete light-probe resources were rejected");
        bool RendererLightProbeInitializationContract::* const fields[] = {
            &RendererLightProbeInitializationContract::device,
            &RendererLightProbeInitializationContract::shaderFactory,
            &RendererLightProbeInitializationContract::commonPasses,
            &RendererLightProbeInitializationContract::geometryShader,
            &RendererLightProbeInitializationContract::mipPixelShader,
            &RendererLightProbeInitializationContract::specularPixelShader,
            &RendererLightProbeInitializationContract::environmentBrdfPixelShader,
            &RendererLightProbeInitializationContract::bindingLayout,
            &RendererLightProbeInitializationContract::constantBuffer,
            &RendererLightProbeInitializationContract::intermediateTexture,
            &RendererLightProbeInitializationContract::environmentBrdfTexture,
            &RendererLightProbeInitializationContract::environmentBrdfFramebuffer,
            &RendererLightProbeInitializationContract::environmentBrdfPipeline
        };
        for (auto field : fields)
        {
            RendererLightProbeInitializationContract failed = complete;
            failed.*field = false;
            Require(!failed.IsComplete(),
                "injected light-probe resource failure was published");
        }
    }

    void TestGeometryBehavior()
    {
        using namespace uvsr;
        Require(ClassifyRendererMaterialDomain(
                RendererMaterialDomain::Opaque) ==
                RendererMaterialRasterClass::Opaque,
            "opaque material classification changed");
        Require(ClassifyRendererMaterialDomain(
                RendererMaterialDomain::AlphaTested) ==
                RendererMaterialRasterClass::AlphaTested,
            "alpha-tested material classification changed");
        Require(ClassifyRendererMaterialDomain(
                RendererMaterialDomain::TransmissiveAlphaTested) ==
                RendererMaterialRasterClass::Opaque,
            "retained transmissive material-ID classification changed");
        Require(ClassifyRendererMaterialDomain(
                RendererMaterialDomain::Count) ==
                RendererMaterialRasterClass::Rejected,
            "invalid material domain was accepted");

        nvrhi::DrawArguments pending;
        pending.vertexCount = 12u;
        pending.instanceCount = 2u;
        pending.startIndexLocation = 9u;
        pending.startVertexLocation = 20u;
        pending.startInstanceLocation = 4u;
        RendererGeometryDraw next;
        next.indexCount = 12u;
        next.instanceCount = 1u;
        next.startIndexLocation = 9u;
        next.startVertexLocation = 20u;
        next.startInstanceLocation = 6u;
        Require(CanMergeRendererGeometryDraws(pending, next),
            "contiguous geometry instances did not batch");
        ++next.indexCount;
        Require(!CanMergeRendererGeometryDraws(pending, next),
            "different geometry index counts were merged");
        --next.indexCount;
        ++next.startIndexLocation;
        Require(!CanMergeRendererGeometryDraws(pending, next),
            "different geometry indices were merged");
        --next.startIndexLocation;
        ++next.startVertexLocation;
        Require(!CanMergeRendererGeometryDraws(pending, next),
            "different vertex bases were merged");
    }

    void TestTargetFailurePublication()
    {
        uvsr::RenderTargets targets;
        Require(!targets.IsValid(),
            "default renderer targets were published as valid");
        Require(!targets.Init(
                nullptr,
                DirectX::XMUINT2(1280u, 720u),
                1u,
                DirectX::XMUINT2(1280u, 720u),
                1u,
                true,
                true,
                true,
                true),
            "null device unexpectedly created renderer targets");
        Require(!targets.IsValid(),
            "failed renderer target initialization was published");
        Require(targets.IsUpdateRequired(
                DirectX::XMUINT2(1280u, 720u),
                1u,
                DirectX::XMUINT2(1280u, 720u),
                1u,
                true,
                true,
                true),
            "failed renderer target initialization suppressed retry");
        targets.Clear(nullptr);
    }

    void TestProbeAbiAndActivity()
    {
        static_assert(sizeof(PlanarViewConstants) == 720u);
        static_assert(sizeof(GBufferFillConstants) == 1440u);
        static_assert(sizeof(GBufferPushConstants) == 28u);
        static_assert(offsetof(GBufferPushConstants, tangentOffset) == 24u);
        static_assert(UVSR_GBUFFER_SPACE_MATERIAL == 0);
        static_assert(UVSR_GBUFFER_SPACE_INPUT == 1);
        static_assert(UVSR_GBUFFER_SPACE_VIEW == 2);
        static_assert(UVSR_GBUFFER_BINDING_INSTANCE_BUFFER == 10);
        static_assert(UVSR_GBUFFER_BINDING_VERTEX_BUFFER == 11);
        static_assert(sizeof(LightProbeProcessingConstants) == 16u);
        static_assert(offsetof(
            LightProbeProcessingConstants, inputCubeSize) == 12u);
        static_assert(sizeof(uvsr::ImageBasedLightingHalf4) == 8u);

        using namespace uvsr;
        Require(IsImageBasedLightingProbeActive(
                true, false, false, 1.f, 0.f),
            "diffuse-only probe was inactive");
        Require(IsImageBasedLightingProbeActive(
                false, true, true, 0.f, 1.f),
            "specular-only probe was inactive");
        Require(!IsImageBasedLightingProbeActive(
                false, true, false, 0.f, 1.f),
            "specular probe without BRDF was active");
        Require(!IsImageBasedLightingProbeActive(
                true,
                true,
                true,
                std::numeric_limits<float>::quiet_NaN(),
                0.f),
            "nonfinite/disabled probe lobes were active");

        const LightProbeConstants constants =
            MakeImageBasedLightingProbeConstants(
                2u, 3u, 4.f, 5.f, 9.f);
        Require(constants.diffuseArrayIndex == 2u &&
                constants.specularArrayIndex == 3u &&
                constants.diffuseScale == 4.f &&
                constants.specularScale == 5.f &&
                constants.mipLevels == 9.f,
            "probe constants changed texture/scalar identity");
        for (const auto& plane : constants.frustumPlanes)
        {
            Require(plane.x == 0.f && plane.y == 0.f &&
                    plane.z == 0.f && plane.w == 1.f,
                "global probe bounds no longer accept every point");
        }
    }

    void TestDirectEnvironmentMath()
    {
        constexpr std::uint32_t width = 8u;
        constexpr std::uint32_t height = 4u;
        std::vector<float> pixels(width * height * 3u);
        for (std::size_t offset = 0u; offset < pixels.size(); offset += 3u)
        {
            pixels[offset] = 2.f;
            pixels[offset + 1u] = 1.f;
            pixels[offset + 2u] = 0.5f;
        }
        const auto projection =
            uvsr::ProjectRendererDiffuseEnvironmentLatLongRgb(
                pixels.data(), width, height);
        Require(projection &&
                std::isfinite(projection->averageLuminance) &&
                projection->averageLuminance > 0.f,
            "finite positive environment failed direct SH projection");
        Require(!uvsr::ProjectRendererDiffuseEnvironmentLatLongRgb(
                nullptr, width, height),
            "null environment pixels were accepted");

        for (std::uint32_t face = 0u; face < 6u; ++face)
        {
            const DirectX::XMFLOAT3 direction =
                uvsr::RendererEnvironmentCubeDirection(
                    face, 7u, 11u, 16u);
            const float length = DirectX::XMVectorGetX(
                DirectX::XMVector3Length(
                    DirectX::XMLoadFloat3(&direction)));
            Require(std::isfinite(length) &&
                    std::abs(length - 1.f) < 1e-5f,
                "cubemap direction was not finite and normalized");
        }
    }
}

int main()
{
    TestGeometryInitializationFailures();
    TestLightProbeInitializationFailures();
    TestGeometryBehavior();
    TestTargetFailurePublication();
    TestProbeAbiAndActivity();
    TestDirectEnvironmentMath();
    return EXIT_SUCCESS;
}
