#include "renderer_scene_retirement.h"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Renderer scene-retirement test failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    uvsr::RendererSceneRetirement retirement(nullptr);
    Require(!retirement.IsValid(), "null device was accepted");
    Require(!retirement.Begin(), "null device armed retirement");
    Require(
        retirement.Poll() ==
            uvsr::RendererSceneRetirementStatus::Idle,
        "idle retirement changed state");
    Require(!retirement.Consume(), "idle retirement was consumed");
    Require(!retirement.UsedBlockingFallback(),
        "idle retirement reported a blocking fallback");

    int armCount = 0;
    int pollCount = 0;
    int waitCount = 0;
    uvsr::RendererSceneRetirementOperations queryOperations;
    queryOperations.armQuery = [&]()
    {
        ++armCount;
        return true;
    };
    queryOperations.pollQuery = [&]()
    {
        ++pollCount;
        return pollCount == 1
            ? uvsr::RendererSceneQueryStatus::Pending
            : uvsr::RendererSceneQueryStatus::Complete;
    };
    queryOperations.waitForIdle = [&]()
    {
        ++waitCount;
        return true;
    };
    uvsr::RendererSceneRetirement queryRetirement(
        std::move(queryOperations));
    Require(queryRetirement.Begin(), "query retirement did not arm");
    Require(queryRetirement.Poll() ==
            uvsr::RendererSceneRetirementStatus::Pending &&
            armCount == 1 && pollCount == 0,
        "query was not armed before polling");
    Require(queryRetirement.Poll() ==
            uvsr::RendererSceneRetirementStatus::Pending &&
            pollCount == 1,
        "pending query published scene retirement");
    Require(queryRetirement.Poll() ==
            uvsr::RendererSceneRetirementStatus::Ready &&
            pollCount == 2 && waitCount == 0,
        "completed query did not publish retirement exactly once");
    Require(queryRetirement.Consume(),
        "completed query retirement was not consumable");

    uvsr::RendererSceneRetirementOperations fallbackOperations;
    fallbackOperations.armQuery = []() { return false; };
    fallbackOperations.pollQuery = []()
    {
        return uvsr::RendererSceneQueryStatus::Failed;
    };
    fallbackOperations.waitForIdle = []() { return true; };
    uvsr::RendererSceneRetirement fallbackRetirement(
        std::move(fallbackOperations));
    Require(fallbackRetirement.Begin(), "fallback retirement did not arm");
    Require(fallbackRetirement.Poll() ==
            uvsr::RendererSceneRetirementStatus::Ready &&
            fallbackRetirement.UsedBlockingFallback(),
        "successful blocking fallback did not publish readiness");

    uvsr::RendererSceneRetirementOperations failingOperations;
    failingOperations.armQuery = []() { return false; };
    failingOperations.pollQuery = []()
    {
        return uvsr::RendererSceneQueryStatus::Failed;
    };
    failingOperations.waitForIdle = []() { return false; };
    uvsr::RendererSceneRetirement failingRetirement(
        std::move(failingOperations));
    Require(failingRetirement.Begin(), "failing retirement did not arm");
    Require(failingRetirement.Poll() ==
            uvsr::RendererSceneRetirementStatus::Failed,
        "failed idle wait published scene retirement readiness");
    Require(!failingRetirement.Consume(),
        "failed retirement released scene ownership");

    uvsr::RendererSceneRetirementOperations pollFailureOperations;
    pollFailureOperations.armQuery = []() { return true; };
    pollFailureOperations.pollQuery = []()
    {
        return uvsr::RendererSceneQueryStatus::Failed;
    };
    pollFailureOperations.waitForIdle = []() { return true; };
    uvsr::RendererSceneRetirement pollFailureRetirement(
        std::move(pollFailureOperations));
    Require(pollFailureRetirement.Begin(),
        "poll-failure retirement did not arm");
    Require(pollFailureRetirement.Poll() ==
            uvsr::RendererSceneRetirementStatus::Pending,
        "armed query skipped its pending publication boundary");
    Require(pollFailureRetirement.Poll() ==
            uvsr::RendererSceneRetirementStatus::Failed,
        "poll device failure published scene retirement readiness");
    Require(!pollFailureRetirement.Consume(),
        "poll device failure released scene ownership");
    return EXIT_SUCCESS;
}
