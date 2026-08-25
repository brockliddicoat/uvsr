#include "denoising_settings.h"
#include "image_based_lighting_background_pass.h"
#include "image_based_lighting_environment.h"
#include "pbr_deferred_dispatch_contract.h"
#include "screen_space_visibility.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void TestVisibilityInstrumentationClosesEveryExitExactlyOnce()
    {
        std::vector<int> closeOrder;
        {
            uvsr::ScreenSpaceVisibilityInstrumentationScope scope(
                [&]() { closeOrder.push_back(1); },
                [&]() { closeOrder.push_back(2); });
        }
        Require(
            closeOrder == std::vector<int>({ 1, 2 }),
            "visibility failure must close marker before timer");

        closeOrder.clear();
        {
            uvsr::ScreenSpaceVisibilityInstrumentationScope scope(
                [&]() { closeOrder.push_back(1); },
                [&]() { closeOrder.push_back(2); });
            scope.Close();
            scope.Close();
        }
        Require(
            closeOrder == std::vector<int>({ 1, 2 }),
            "visibility success and destructor must not double-close instrumentation");

        uvsr::ScreenSpaceVisibilityResult result;
        Require(
            result.status ==
                uvsr::ScreenSpaceVisibilityRenderStatus::Inactive &&
                !result.HasFailed(),
            "default visibility result must mean inactive, not failed");
        result.status = uvsr::ScreenSpaceVisibilityRenderStatus::Failed;
        Require(
            result.HasFailed() && !result.dispatched,
            "visibility failure must be observable independently of dispatch");
    }

    void TestPbrDispatchTransactionProtectsOutput()
    {
        int outputWrites = 0;
        uvsr::PbrDeferredLightingRenderTransaction failed(1u);
        const bool failedDispatch = uvsr::ExecutePbrDeferredLightingView(
            failed,
            false,
            [&]() { ++outputWrites; });
        const uvsr::PbrDeferredLightingRenderResult failedResult =
            failed.Finish();
        Require(
            !failedDispatch && outputWrites == 0 &&
                !failedResult.Succeeded() &&
                failedResult.dispatchedViewCount == 0u,
            "failed PBR binding must not execute the output dispatch");

        uvsr::PbrDeferredLightingRenderTransaction complete(2u);
        Require(
            uvsr::ExecutePbrDeferredLightingView(
                complete, true, [&]() { ++outputWrites; }) &&
                uvsr::ExecutePbrDeferredLightingView(
                    complete, true, [&]() { ++outputWrites; }),
            "ready PBR views must dispatch");
        const uvsr::PbrDeferredLightingRenderResult completeResult =
            complete.Finish();
        Require(
            completeResult.Succeeded() &&
                completeResult.dispatchedViewCount == 2u &&
                outputWrites == 2,
            "PBR success must require every expected view output");

        uvsr::PbrDeferredLightingRenderTransaction partial(2u);
        Require(
            uvsr::ExecutePbrDeferredLightingView(
                partial, true, [&]() { ++outputWrites; }),
            "first PBR view must dispatch before injected later failure");
        Require(
            !uvsr::ExecutePbrDeferredLightingView(
                partial, false, [&]() { ++outputWrites; }) &&
                !partial.Finish().Succeeded() && outputWrites == 3,
            "partial PBR output must remain a terminal failed result");
    }

    void TestIblPreparationDistinguishesIdleReadyAndFailure()
    {
        uvsr::ImageBasedLightingPreparationState state;
        Require(
            state.Get() == uvsr::ImageBasedLightingPreparationStatus::Idle &&
                !state.IsReady() && !state.HasFailed(),
            "IBL idle must not mean ready");
        uvsr::ImageBasedLightingPreparationState invalidCompletion;
        invalidCompletion.Complete();
        Require(
            invalidCompletion.HasFailed(),
            "IBL completion without a started preparation must fail closed");
        state.Begin();
        Require(
            state.Get() ==
                uvsr::ImageBasedLightingPreparationStatus::Preparing &&
                !state.IsReady(),
            "IBL upload start must be observable");
        state.Fail();
        Require(
            state.HasFailed() && !state.IsReady(),
            "IBL upload failure must never collapse to ready");
        state.Begin();
        state.Complete();
        Require(
            state.IsReady() && !state.HasFailed(),
            "a new successful IBL request must replace the prior failure");
    }

    void TestIblBackgroundDispatchProtectsOutput()
    {
        int outputWrites = 0;
        auto result = uvsr::ExecuteImageBasedLightingBackgroundViews(
            false,
            1u,
            [&](std::uint32_t)
            {
                ++outputWrites;
                return true;
            });
        Require(
            !result.Succeeded() && outputWrites == 0,
            "invalid IBL background pass must not touch output");

        result = uvsr::ExecuteImageBasedLightingBackgroundViews(
            true,
            3u,
            [&](std::uint32_t viewIndex)
            {
                if (viewIndex == 1u)
                    return false;
                ++outputWrites;
                return true;
            });
        Require(
            !result.Succeeded() &&
                result.dispatchedViewCount == 1u &&
                outputWrites == 1,
            "invalid IBL background view must report partial failure");

        outputWrites = 0;
        result = uvsr::ExecuteImageBasedLightingBackgroundViews(
            true,
            3u,
            [&](std::uint32_t)
            {
                ++outputWrites;
                return true;
            });
        Require(
            result.Succeeded() &&
                result.dispatchedViewCount == 3u &&
                outputWrites == 3,
            "IBL background success must include every output view");
    }

    void TestSelectedDenoiserAvailabilityCannotCollapseToInactive()
    {
        using uvsr::DenoisingMethodChoice;
        using uvsr::IsDenoisingMethodRuntimeAvailable;

        Require(
            IsDenoisingMethodRuntimeAvailable(
                DenoisingMethodChoice::None, false, false),
            "disabled denoising must remain an available no-op");
        for (const DenoisingMethodChoice method : {
                DenoisingMethodChoice::JointBilateral,
                DenoisingMethodChoice::GaussianBilateral })
        {
            Require(
                !IsDenoisingMethodRuntimeAvailable(
                    method, false, true) &&
                IsDenoisingMethodRuntimeAvailable(
                    method, true, false),
                "selected spatial denoising must require its runtime owner");
        }
        for (const DenoisingMethodChoice method : {
                DenoisingMethodChoice::Reblur,
                DenoisingMethodChoice::Relax,
                DenoisingMethodChoice::Sigma })
        {
            Require(
                !IsDenoisingMethodRuntimeAvailable(
                    method, true, false) &&
                IsDenoisingMethodRuntimeAvailable(
                    method, false, true),
                "selected third-party denoising must require its runtime owner");
        }
        Require(
            !IsDenoisingMethodRuntimeAvailable(
                static_cast<DenoisingMethodChoice>(0xff), true, true),
            "corrupt denoising selection must fail closed");
    }
}

int main()
{
    TestVisibilityInstrumentationClosesEveryExitExactlyOnce();
    TestPbrDispatchTransactionProtectsOutput();
    TestIblPreparationDistinguishesIdleReadyAndFailure();
    TestIblBackgroundDispatchProtectsOutput();
    TestSelectedDenoiserAvailabilityCannotCollapseToInactive();
    return EXIT_SUCCESS;
}
