#include "engine_startup.h"
#include "renderer_log.h"
#include "renderer_nvrhi_message_callback.h"
#include "renderer_shell_failure_policy.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <crtdbg.h>
#include <process.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "nvrhi-d3d12-diagnostics.h"

namespace
{
    using namespace uvsr;
    constexpr const char* FatalMessage =
        "ExecuteCommandLists Signal failed, HRESULT = 0x887a0005\n"
        "DRED breadcrumb[0]: list='visibility', completed=7/8";

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    size_t CountOccurrences(std::string_view text, std::string_view token)
    {
        size_t count = 0, position = 0;
        while ((position = text.find(token, position)) != std::string_view::npos)
        {
            ++count;
            position += token.size();
        }
        return count;
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        for (size_t position = 0; (position = text.find("\r\n", position)) != std::string::npos;)
            text.erase(position, 1);
        return text;
    }

    struct RecordingCallback final : nvrhi::IMessageCallback
    {
        size_t count = 0;
        nvrhi::MessageSeverity severity = nvrhi::MessageSeverity::Info;
        std::string text;
        void message(nvrhi::MessageSeverity incomingSeverity, const char* incomingText) override
        {
            ++count;
            severity = incomingSeverity;
            text = incomingText ? incomingText : "";
        }
    };

    void FailureReports()
    {
        using namespace nvrhi::d3d12::uvsr_diagnostics;
        for (const HRESULT result : {DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_HUNG,
                DXGI_ERROR_DEVICE_RESET, DXGI_ERROR_DRIVER_INTERNAL_ERROR})
            Require(IsDeviceRemovalFailure(result, S_OK), "device removal classification changed");
        Require(IsDeviceRemovalFailure(E_FAIL, DXGI_ERROR_DEVICE_REMOVED) &&
            !IsDeviceRemovalFailure(E_FAIL, S_OK) && !IsDeviceRemovalFailure(S_OK, S_OK),
            "removed reason or ordinary HRESULT classification changed");
        struct Failure { HRESULT result, reason; bool terminal, dred, fatal; };
        const Failure cases[] = {
            {HRESULT_FROM_WIN32(ERROR_TIMEOUT), S_OK, true, false, true},
            {DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_REMOVED, true, true, true},
            {E_FAIL, DXGI_ERROR_DEVICE_HUNG, false, true, true},
            {E_INVALIDARG, S_OK, false, false, false}
        };
        for (const auto& c : cases)
        {
            RecordingCallback callback;
            std::stringstream message;
            message << "operation failed";
            Require(!ReportFailure(message, c.result, c.reason, nullptr, &callback, c.terminal) &&
                callback.count == 1 && callback.severity ==
                    (c.fatal ? nvrhi::MessageSeverity::Fatal : nvrhi::MessageSeverity::Error) &&
                CountOccurrences(callback.text, "DRED") == size_t(c.dred) &&
                (!c.dred || callback.text.find("DRED device unavailable") != std::string::npos),
                "failure did not emit one correctly classified record with removal-only DRED");
        }
    }

    void ShellPublication()
    {
        using Operation = RendererShellOperation;
        using Disposition = RendererRenderDisposition;
        Require(RendererShellWaitTimeoutMilliseconds > 0 && RendererShellWaitTimeoutMilliseconds < INFINITE,
            "shell wait must be finite and positive");
        for (const uint32_t outcome : {RendererShellWaitSucceeded, 258u, RendererShellWaitFailed})
        {
            const Operation operation = outcome == RendererShellWaitFailed ? Operation::ShutdownFrameWait : Operation::BeginFrameWait;
            uint32_t observedTimeout = 0, errorCalls = 0;
            const auto result = WaitForRendererShellFence(operation,
                [&](uint32_t timeout) { observedTimeout = timeout; return outcome; },
                [&] { ++errorCalls; return 6u; });
            Require(result.Succeeded() == (outcome == RendererShellWaitSucceeded) &&
                result.operation == (result.Succeeded() ? Operation::None : operation) &&
                result.nativeResult == outcome && result.nativeDetail == (outcome == RendererShellWaitFailed ? 6u : 0u) &&
                errorCalls == uint32_t(outcome == RendererShellWaitFailed) &&
                observedTimeout == RendererShellWaitTimeoutMilliseconds, "shell wait lost its bound or failure attribution");
        }
        const Operation operations[] = {Operation::Present, Operation::ResetFenceEvent, Operation::SetFenceEvent, Operation::SignalFence};
        RendererShellFailureLatch latch;
        for (int failure = 0; failure <= 4; ++failure)
        {
            std::vector<int> calls;
            const auto invoke = [&](int step, uint64_t value = 17)
            {
                Require(value == 17, "present sequence changed its fence value");
                calls.push_back(step);
                return step == failure ? -1 : 0;
            };
            const auto result = PresentRendererShellFrame(17, [&] { return invoke(0); }, [&] { return invoke(1); },
                [&](uint64_t value) { return invoke(2, value); }, [&](uint64_t value) { return invoke(3, value); });
            const std::vector<int> order{0, 1, 2, 3};
            const size_t expectedCalls = size_t(std::min(failure + 1, 4));
            Require(calls == std::vector<int>(order.begin(), order.begin() + expectedCalls) &&
                result.operation == (failure == 4 ? Operation::None : operations[failure]),
                "present failure did not stop callbacks in exact order");
            uint64_t fence = 17;
            Require(AdvanceRendererShellCounter(fence, result) == (failure == 4) &&
                fence == (failure == 4 ? 18u : 17u), "failed present advanced its frame counter");
            Require(latch.Record(result) == (failure == 0) && latch.Failed() && !RendererShellCanRunFrame(latch) &&
                latch.Failure().operation == Operation::Present, "terminal shell cause was replaced or allowed another frame");
        }
        for (auto first : {Disposition::Inactive, Disposition::Pending, Disposition::Failed})
            for (auto second : {Disposition::Inactive, Disposition::Pending, Disposition::Failed})
            {
                const auto merged = MergeRendererRenderDisposition(first, second);
                const auto expected = first == Disposition::Failed || second == Disposition::Failed ? Disposition::Failed :
                    first == Disposition::Pending || second == Disposition::Pending ? Disposition::Pending : Disposition::Inactive;
                Require(merged == expected, "pending or failed disposition was lost");
            }
        for (auto disposition : {Disposition::Inactive, Disposition::Pending, Disposition::Failed})
        {
            unsigned calls = 0;
            Require(ConsumeRendererRenderDisposition(disposition, [&] { ++calls; return true; }) ==
                (disposition != Disposition::Failed) && calls == unsigned(disposition == Disposition::Inactive),
                "pending/failed frame published or inactive frame failed to publish");
        }
        Require(!ConsumeRendererRenderDisposition(Disposition::Inactive, [] { return false; }), "failed publish was accepted");
        RendererShellTerminalDiagnosticLatch diagnostic;
        Require(!diagnostic.ShouldReport(false) && diagnostic.ShouldReport(true) && !diagnostic.ShouldReport(true),
            "ordinary first cause suppressed the single terminal report");
        for (int failure = 0; failure <= 2; ++failure)
        {
            std::vector<int> calls;
            const bool released = ReleaseRendererShellResources(
                [&] { calls.push_back(0); return failure != 0; },
                [&] { calls.push_back(1); return failure != 1; }, [&] { calls.push_back(2); });
            const std::vector<int> order{0, 1, 2};
            Require(released == (failure == 2) && calls == std::vector<int>(order.begin(), order.begin() + failure + 1),
                "resources were released before both waits succeeded");
        }
        Require(!CheckRendererShellBoolean(Operation::MessageLoopDeviceIdle, false).Succeeded(),
            "failed final device idle wait was accepted");
    }

    void DurableLogging(const std::filesystem::path& scratch, const char* core, const char* executable)
    {
        Require(VerifyD3D12CoreFile(core), "the pinned D3D12Core bytes were rejected");
        std::filesystem::create_directories(scratch);
        const auto tamperedCore = scratch / "tampered-D3D12Core.dll";
        std::filesystem::copy_file(core, tamperedCore, std::filesystem::copy_options::overwrite_existing);
        {
            std::fstream tamper(tamperedCore, std::ios::binary | std::ios::in | std::ios::out);
            tamper.put('\0');
        }
        Require(!VerifyD3D12CoreFile(tamperedCore), "tampered D3D12Core bytes were accepted");
        std::vector<std::pair<log::Severity, std::string>> messages;
        log::SetMinimumSeverity(log::Severity::Info);
        log::SetCallback([&](log::Severity severity, const char* message) { messages.emplace_back(severity, message); });
        log::debug("filtered %d", 1);
        log::info("identity %s %d", "value", 7);
        log::warning("warning");
        Require(messages == std::vector<std::pair<log::Severity, std::string>>{
            {log::Severity::Info, "identity value 7"}, {log::Severity::Warning, "warning"}} && bool(log::GetCallback()),
            "minimum severity, printf formatting or readable callback changed");
        messages.clear();
        RendererNvrhiMessageCallback callback;
        const char* detail = "CreateGraphicsPipelineState failed, HRESULT = 0x887a0006\nDRED page-fault VA = 0x1234";
        callback.message(nvrhi::MessageSeverity::Info, "device selected");
        callback.message(nvrhi::MessageSeverity::Warning, "heap pressure");
        callback.message(nvrhi::MessageSeverity::Error, detail);
        Require(messages == std::vector<std::pair<log::Severity, std::string>>{
            {log::Severity::Info, "device selected"}, {log::Severity::Warning, "heap pressure"}, {log::Severity::Error, detail}},
            "NVRHI severity mapping or exact HRESULT/DRED text changed");

        const auto logPath = scratch / "nested" / "uvsr-engine.log";
        auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
        Require(InitializeEngineDiagnosticLog(logPath, [&] { return now; }) && std::filesystem::is_regular_file(logPath),
            "diagnostic initialization did not create its nested directory and file");
        log::error("urgent flush known answer");
        Require(ReadText(logPath).find("[error] urgent flush known answer") != std::string::npos,
            "error severity was not flushed before shutdown");
        for (int seconds : {0, 1, 2, 5})
        {
            now = std::chrono::steady_clock::time_point(std::chrono::seconds(100 + seconds));
            log::warning("coalesced warning");
        }
        auto written = ReadText(logPath);
        Require(CountOccurrences(written, "] coalesced warning") == 2 &&
            written.find("Previous warning repeated 2 additional times") != std::string::npos,
            "five-second repeat window failed to flush its summary");
        now += std::chrono::seconds(1);
        log::warning("coalesced warning");
        ShutdownEngineDiagnosticLog();
        written = ReadText(logPath);
        Require(written.find("Previous warning repeated 1 additional times") != std::string::npos,
            "shutdown did not flush its pending repeat summary");
        log::info("restored downstream");
        Require(ReadText(logPath) == written && messages.back().second == "restored downstream",
            "shutdown failed to stop file writes or restore the prior callback");
        log::SetCallback({});
        Require(bool(log::GetCallback()), "empty callback did not restore the direct default");

        const auto fatalLog = scratch / "fatal-nvrhi.log";
        std::filesystem::remove(fatalLog);
        const auto logArgument = fatalLog.string();
        const char* arguments[] = {executable, "--fatal-child", logArgument.c_str(), nullptr};
        const intptr_t status = _spawnv(_P_WAIT, executable, arguments);
        Require(status != -1 && status != 0, "fatal callback child failed to start or exited successfully");
        Require(ReadText(fatalLog).find("[fatal] " + std::string(FatalMessage)) != std::string::npos,
            "fatal HRESULT/DRED text was not flushed before termination");
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--fatal-child")
    {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0u, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        Require(uvsr::InitializeEngineDiagnosticLog(argv[2]), "fatal child could not initialize its durable log");
        uvsr::RendererNvrhiMessageCallback{}.message(nvrhi::MessageSeverity::Fatal, FatalMessage);
        return EXIT_SUCCESS;
    }
    Require(argc == 3, "expected scratch-directory and exact D3D12Core arguments");
    using namespace nvrhi::d3d12::uvsr_diagnostics;

    Require(
        FenceWaitTimeoutMilliseconds > 0u &&
            FenceWaitTimeoutMilliseconds < INFINITE,
        "fence wait timeout must be finite and positive");

    {
        const FenceCompletionObservation below =
            ObserveFenceCompletion(40u);
        const FenceCompletionObservation reached =
            ObserveFenceCompletion(41u);
        const FenceCompletionObservation failed =
            ObserveFenceCompletion(UINT64_MAX);
        Require(!below.failed && !below.HasReached(41u),
            "a value below the target completed the fence");
        Require(!reached.failed && reached.HasReached(41u),
            "the exact target did not complete the fence");
        Require(failed.failed && failed.completedValue == 0u &&
                !failed.HasReached(0u),
            "UINT64_MAX was not preserved as terminal fence failure");
    }

    {
        std::mutex submissionMutex;
        std::atomic<uint64_t> lastSubmitted{ 41u };
        std::atomic<bool> completionFailed{ false };
        uint64_t signaledValue = 0u;
        const FenceSubmissionObservation submission = TrySignalFence(
            submissionMutex,
            lastSubmitted,
            completionFailed,
            [&](uint64_t candidate)
            {
                signaledValue = candidate;
                return S_OK;
            });
        Require(
            submission.Succeeded() && submission.candidateValue == 42u &&
                submission.PublishedValue() == 42u && signaledValue == 42u &&
                submission.attempted && lastSubmitted.load() == 42u &&
                !completionFailed.load(),
            "successful Signal did not publish its exact candidate value");
    }

    {
        std::mutex submissionMutex;
        std::atomic<uint64_t> lastSubmitted{ 42u };
        std::atomic<bool> completionFailed{ false };
        uint64_t attemptedValue = 0u;
        const FenceSubmissionObservation submission = TrySignalFence(
            submissionMutex,
            lastSubmitted,
            completionFailed,
            [&](uint64_t candidate)
            {
                attemptedValue = candidate;
                return DXGI_ERROR_DEVICE_REMOVED;
            });
        Require(
            !submission.Succeeded() &&
                submission.candidateValue == 43u &&
                submission.PublishedValue() == 0u &&
                submission.attempted && attemptedValue == 43u &&
                lastSubmitted.load() == 42u && completionFailed.load(),
            "failed Signal published or changed its candidate value");
    }

    {
        std::mutex submissionMutex;
        std::atomic<uint64_t> lastSubmitted{ 19u };
        std::atomic<bool> completionFailed{ false };
        uint32_t calls = 0u;
        const FenceSubmissionObservation first = TrySignalFence(
            submissionMutex,
            lastSubmitted,
            completionFailed,
            [&](uint64_t candidate)
            {
                ++calls;
                Require(candidate == 20u,
                    "Signal received the wrong candidate value");
                return DXGI_ERROR_DEVICE_REMOVED;
            });
        const FenceSubmissionObservation second = TrySignalFence(
            submissionMutex,
            lastSubmitted,
            completionFailed,
            [&](uint64_t)
            {
                ++calls;
                return S_OK;
            });
        Require(first.attempted && !first.Succeeded() &&
                first.PublishedValue() == 0u &&
                lastSubmitted.load() == 19u && completionFailed.load() &&
                !second.attempted && second.result == E_ABORT && calls == 1u,
            "a failed Signal did not latch or blocked submission ran");
    }

    {
        std::mutex submissionMutex;
        std::atomic<uint64_t> lastSubmitted{ UINT64_MAX };
        std::atomic<bool> completionFailed{ false };
        bool invoked = false;
        const FenceSubmissionObservation submission = TrySignalFence(
            submissionMutex,
            lastSubmitted,
            completionFailed,
            [&](uint64_t)
            {
                invoked = true;
                return S_OK;
            });
        Require(submission.attempted && !submission.Succeeded() &&
                submission.result == E_FAIL && completionFailed.load() &&
                !invoked,
            "fence value overflow invoked Signal or remained recoverable");
    }

    {
        std::mutex submissionMutex;
        std::atomic<uint64_t> lastSubmitted{ 100u };
        std::atomic<bool> completionFailed{ false };
        std::vector<uint64_t> signaledValues;
        std::mutex valuesMutex;
        const auto submit = [&]()
        {
            const FenceSubmissionObservation submission = TrySignalFence(
                submissionMutex,
                lastSubmitted,
                completionFailed,
                [&](uint64_t candidate)
                {
                    const std::lock_guard<std::mutex> lock(valuesMutex);
                    signaledValues.push_back(candidate);
                    return S_OK;
                });
            Require(submission.Succeeded(),
                "a concurrent injected Signal unexpectedly failed");
        };
        std::thread first(submit);
        std::thread second(submit);
        first.join();
        second.join();
        std::sort(signaledValues.begin(), signaledValues.end());
        Require(
            signaledValues == std::vector<uint64_t>{ 101u, 102u } &&
                lastSubmitted.load() == 102u &&
                !completionFailed.load(),
            "concurrent Signal calls did not publish unique monotonic IDs");
    }

    FailureReports();
    ShellPublication();
    DurableLogging(argv[1], argv[2], argv[0]);
    return EXIT_SUCCESS;
}
