#include "renderer_shell_failure_policy.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "Renderer shell failure-policy test failed: "
                      << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using namespace uvsr;

    Require(
        RendererShellWaitTimeoutMilliseconds > 0u &&
            RendererShellWaitTimeoutMilliseconds < 0xffffffffu,
        "the shell wait must be finite and positive");

    uint32_t observedTimeout = 0u;
    RendererShellOperationResult result = WaitForRendererShellFence(
        RendererShellOperation::BeginFrameWait,
        [&](uint32_t timeout)
        {
            observedTimeout = timeout;
            return RendererShellWaitSucceeded;
        },
        []() { return 0u; });
    Require(
        result.Succeeded() &&
            observedTimeout == RendererShellWaitTimeoutMilliseconds,
        "a completed begin-frame wait was not accepted with the bound");

    result = WaitForRendererShellFence(
        RendererShellOperation::BeginFrameWait,
        [](uint32_t) { return 258u; },
        []() { return 123u; });
    Require(
        !result.Succeeded() &&
            result.operation == RendererShellOperation::BeginFrameWait &&
            result.nativeResult == 258u && result.nativeDetail == 0u,
        "a begin-frame timeout was not terminally classified");

    result = WaitForRendererShellFence(
        RendererShellOperation::ShutdownFrameWait,
        [](uint32_t) { return RendererShellWaitFailed; },
        []() { return 6u; });
    Require(
        !result.Succeeded() &&
            result.operation == RendererShellOperation::ShutdownFrameWait &&
            result.nativeResult == RendererShellWaitFailed &&
            result.nativeDetail == 6u,
        "a failed shutdown wait lost its Win32 error");

    const auto runPresentCase = [](
        int32_t presentResult,
        int32_t resetResult,
        int32_t eventResult,
        int32_t signalResult)
    {
        std::vector<std::string> calls;
        const RendererShellOperationResult operationResult =
            PresentRendererShellFrame(
                17u,
                [&]()
                {
                    calls.emplace_back("present");
                    return presentResult;
                },
                [&]()
                {
                    calls.emplace_back("reset");
                    return resetResult;
                },
                [&](uint64_t value)
                {
                    Require(value == 17u,
                        "SetEventOnCompletion received the wrong fence value");
                    calls.emplace_back("event");
                    return eventResult;
                },
                [&](uint64_t value)
                {
                    Require(value == 17u,
                        "Signal received the wrong fence value");
                    calls.emplace_back("signal");
                    return signalResult;
                });
        return std::pair{ operationResult, calls };
    };

    auto [presentFailure, presentCalls] = runPresentCase(-1, 0, 0, 0);
    Require(
        presentFailure.operation == RendererShellOperation::Present &&
            presentCalls == std::vector<std::string>{ "present" },
        "failed Present did not stop before fence operations");

    auto [resetFailure, resetCalls] = runPresentCase(0, -2, 0, 0);
    Require(
        resetFailure.operation == RendererShellOperation::ResetFenceEvent &&
            resetCalls == std::vector<std::string>{ "present", "reset" },
        "failed ResetEvent did not stop before SetEventOnCompletion");

    auto [eventFailure, eventCalls] = runPresentCase(0, 0, -2, 0);
    Require(
        eventFailure.operation == RendererShellOperation::SetFenceEvent &&
            eventCalls ==
                std::vector<std::string>{ "present", "reset", "event" },
        "failed SetEventOnCompletion did not stop before Signal");

    auto [signalFailure, signalCalls] = runPresentCase(0, 0, 0, -3);
    Require(
        signalFailure.operation == RendererShellOperation::SignalFence &&
            signalCalls == std::vector<std::string>{
                "present", "reset", "event", "signal" },
        "failed Signal was not terminally classified");

    auto [success, successCalls] = runPresentCase(0, 0, 0, 0);
    Require(
        success.Succeeded() && successCalls == std::vector<std::string>{
            "present", "reset", "event", "signal" },
        "successful present sequence did not execute in exact order");

    uint64_t frameFence = 17u;
    Require(
        !AdvanceRendererShellCounter(frameFence, signalFailure) &&
            frameFence == 17u,
        "a failed present sequence advanced the frame fence");
    Require(
        AdvanceRendererShellCounter(frameFence, success) &&
            frameFence == 18u,
        "a successful present sequence did not advance exactly once");

    Require(
        MergeRendererRenderDisposition(
            RendererRenderDisposition::Inactive,
            RendererRenderDisposition::Pending) ==
                RendererRenderDisposition::Pending &&
        MergeRendererRenderDisposition(
            RendererRenderDisposition::Pending,
            RendererRenderDisposition::Inactive) ==
                RendererRenderDisposition::Pending,
        "a pending render disposition was not retained nonterminally");
    int presentCallbacks = 0;
    const auto publish = [&]()
    {
        ++presentCallbacks;
        return true;
    };
    Require(
        ConsumeRendererRenderDisposition(
            RendererRenderDisposition::Pending,
            publish) &&
        presentCallbacks == 0,
        "a pending frame reached its publish callbacks");
    Require(
        !ConsumeRendererRenderDisposition(
            RendererRenderDisposition::Failed,
            publish) &&
        presentCallbacks == 0,
        "a failed frame reached its publish callbacks");
    Require(
        ConsumeRendererRenderDisposition(
            RendererRenderDisposition::Inactive,
            publish) &&
        presentCallbacks == 1,
        "an inactive disposition did not publish exactly once");
    Require(
        !ConsumeRendererRenderDisposition(
            RendererRenderDisposition::Inactive,
            []() { return false; }),
        "a failed publish was not terminal");

    RendererShellFailureLatch latch;
    Require(
        latch.Record(presentFailure) && latch.Failed(),
        "the first shell failure did not latch");
    int downstreamCallbacks = 0;
    if (RendererShellCanRunFrame(latch))
        ++downstreamCallbacks;
    Require(
        downstreamCallbacks == 0,
        "a latched resize/frame failure allowed downstream callbacks");
    Require(
        MergeRendererRenderDisposition(
            RendererRenderDisposition::Inactive,
            RendererRenderDisposition::Failed) ==
                RendererRenderDisposition::Failed &&
        MergeRendererRenderDisposition(
            RendererRenderDisposition::Pending,
            RendererRenderDisposition::Failed) ==
                RendererRenderDisposition::Failed &&
        MergeRendererRenderDisposition(
            RendererRenderDisposition::Failed,
            RendererRenderDisposition::Pending) ==
                RendererRenderDisposition::Failed,
        "a failed render disposition was not terminal");
    Require(
        !latch.Record(signalFailure) &&
            latch.Failure().operation == RendererShellOperation::Present,
        "a later shell failure replaced the terminal cause");

    RendererShellTerminalDiagnosticLatch terminalDiagnostic;
    Require(
        !terminalDiagnostic.ShouldReport(false) &&
            terminalDiagnostic.ShouldReport(true) &&
            !terminalDiagnostic.ShouldReport(true),
        "an ordinary first cause must not suppress one later terminal report");

    int releaseCount = 0;
    Require(
        !ReleaseRendererShellResources(
            []() { return true; },
            [&]()
            {
                Require(releaseCount == 0,
                    "resources were released while a shell fence was pending");
                return false;
            },
            [&]() { ++releaseCount; }) &&
            releaseCount == 0,
        "a completed internal idle released resources before the shell fence");
    Require(
        ReleaseRendererShellResources(
            []() { return true; },
            [&]()
            {
                Require(releaseCount == 0,
                    "resources were released before shell-fence completion");
                return true;
            },
            [&]() { ++releaseCount; }) &&
            releaseCount == 1,
        "resources were not released after both waits completed");
    Require(
        !CheckRendererShellBoolean(
            RendererShellOperation::MessageLoopDeviceIdle,
            false).Succeeded(),
        "a failed final device-idle wait was accepted");

    return EXIT_SUCCESS;
}
