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

#if defined(UVSR_ENGINE_LOG_TEST_HOOKS)
    void DiagnosticLogFailures(const std::filesystem::path& scratch)
    {
        using Failure = EngineDiagnosticLogFailure;
        using Messages = std::vector<std::pair<log::Severity, std::string>>;
        Messages messages;
        const log::Callback downstream{[](void* context, log::Severity severity, const char* message) {
            static_cast<Messages*>(context)->emplace_back(severity, message ? message : "");
        }, &messages};
        log::SetCallback(downstream);
        const auto restored = [&] {
            const auto current = log::GetCallback();
            return current.function == downstream.function && current.context == downstream.context;
        };
        Require(!InitializeEngineDiagnosticLog(nullptr) && !InitializeEngineDiagnosticLog(L"") && restored(),
            "empty log preparation installed or retained a callback");
        unsigned ordinal = 0;
        for (const auto failure : {Failure::Allocation, Failure::Open, Failure::Buffer})
        {
            const auto path = scratch / ("prepare-" + std::to_string(ordinal++)) / "nested/log.txt";
            messages.clear();
            FailEngineDiagnosticLogOnce(failure);
            Require(!InitializeEngineDiagnosticLog(path.c_str()) && restored() && messages.size() == 1 &&
                messages[0].first == log::Severity::Warning,
                "failed log preparation retained state or omitted its error");
            Require(InitializeEngineDiagnosticLog(path.c_str()), "log preparation did not retry with the same path");
            log::error("preparation retry");
            ShutdownEngineDiagnosticLog();
            Require(restored() && ReadText(path).find("[error] preparation retry") != std::string::npos,
                "retried log preparation lost the write or callback");
        }
        const auto blocked = scratch / "parent-is-file";
        { std::ofstream file(blocked); file << "preserve parent bytes"; }
        Require(!InitializeEngineDiagnosticLog((blocked / "child/log.txt").c_str()) && restored() &&
            ReadText(blocked) == "preserve parent bytes", "log parent failure changed a file or retained state");
        const auto denied = scratch / "sharing-denied.log";
        HANDLE reservation = CreateFileW(denied.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(reservation != INVALID_HANDLE_VALUE, "log sharing reservation failed");
        const bool deniedOpen = InitializeEngineDiagnosticLog(denied.c_str());
        CloseHandle(reservation);
        Require(!deniedOpen && restored(), "exclusive file sharing did not reject log initialization");
        Require(InitializeEngineDiagnosticLog(denied.c_str()), "log sharing failure did not retry");
        ShutdownEngineDiagnosticLog();

        const auto root = scratch / "paths";
        const std::filesystem::path paths[] = {
            root / L"\u65e5\u5fd7-\u00e9" / "nested/log.txt",
            root / "made/../dot/log.txt",
            std::filesystem::relative(root / "relative/new/log.txt"),
            std::filesystem::path(L"\\\\?\\" + std::filesystem::absolute(root / "extended/new/log.txt").wstring())
        };
        for (const auto& path : paths)
        {
            Require(InitializeEngineDiagnosticLog(path.c_str()), "a supported native log path was rejected");
            log::error("native path retry");
            ShutdownEngineDiagnosticLog();
            Require(restored() && ReadText(path).find("[error] native path retry") != std::string::npos,
                "native log path did not retain its bytes");
        }

        for (const auto failure : {Failure::Write, Failure::Flush, Failure::Close})
        {
            const auto path = scratch / ("io-" + std::to_string(ordinal++)) / "log.txt";
            Require(InitializeEngineDiagnosticLog(path.c_str()), "I/O failure fixture initialization failed");
            messages.clear();
            FailEngineDiagnosticLogOnce(failure);
            if (failure == Failure::Close) ShutdownEngineDiagnosticLog();
            else log::error("failed file I/O message");
            log::info("downstream remains available");
            ShutdownEngineDiagnosticLog();
            const auto failures = std::count_if(messages.begin(), messages.end(), [](const auto& row) {
                return row.first == log::Severity::Warning && row.second.find("file I/O failed") != std::string::npos;
            });
            Require(failures == 1 && restored() && messages.back().second == "downstream remains available",
                "file I/O failure did not report once and restore downstream delivery");
            Require(InitializeEngineDiagnosticLog(path.c_str()), "file I/O failure did not permit a new logging session");
            log::error("I/O retry complete");
            ShutdownEngineDiagnosticLog();
            Require(ReadText(path).find("[error] I/O retry complete") != std::string::npos && restored(),
                "file I/O retry lost its complete output or callback");
        }
        const auto firstWrite = scratch / "first-write/log.txt";
        FailEngineDiagnosticLogOnce(Failure::Write);
        Require(!InitializeEngineDiagnosticLog(firstWrite.c_str()) && restored(),
            "initial log-line failure was reported as successful initialization");
        Require(InitializeEngineDiagnosticLog(firstWrite.c_str()), "initial write failure did not retry");
        ShutdownEngineDiagnosticLog();

        const auto timing = scratch / "clock/log.txt";
        int64_t now = 100'000'000'000;
        const EngineDiagnosticLogClock clock{[](void* context) noexcept {
            return *static_cast<int64_t*>(context);
        }, &now};
        Require(InitializeEngineDiagnosticLog(timing.c_str(), clock), "clock fixture initialization failed");
        log::info("buffered line before one second");
        now += 999'999'999;
        log::info("buffered line before boundary");
        Require(ReadText(timing).empty(), "ordinary output flushed before the one-second boundary");
        ++now;
        log::info("one-second boundary");
        Require(ReadText(timing).find("[info] one-second boundary") != std::string::npos,
            "ordinary output missed the exact one-second flush boundary");
        log::warning("five-second boundary warning");
        now += 4'999'999'999;
        log::warning("five-second boundary warning");
        ++now;
        log::warning("five-second boundary warning");
        const auto clockText = ReadText(timing);
        Require(CountOccurrences(clockText, "] five-second boundary warning") == 2 &&
            clockText.find("Previous warning repeated 1 additional times") != std::string::npos,
            "the borrowed clock changed the exact five-second coalescing boundary");
        ShutdownEngineDiagnosticLog();

        const auto boundary = scratch / "message-capacity/log.txt";
        Require(InitializeEngineDiagnosticLog(boundary.c_str(), clock), "message boundary initialization failed");
        std::string first(4095, 'x'); first.back() = '1';
        std::string second = first; second.back() = '2';
        log::warning("%s", first.c_str());
        ++now;
        log::warning("%s", second.c_str());
        ++now;
        log::warning("%s", second.c_str());
        log::error("flush full warning keys");
        ShutdownEngineDiagnosticLog();
        const auto boundaryText = ReadText(boundary);
        Require(CountOccurrences(boundaryText, first) == 1 && CountOccurrences(boundaryText, second) == 1 &&
            CountOccurrences(boundaryText, "Previous warning repeated 1 additional times") == 1 && restored(),
            "the fixed warning key truncated content or merged distinct final bytes");
        FailEngineDiagnosticLogOnce(Failure::None);
        log::SetCallback({});
        printf("engine log: six preparation/I/O failures, first-write failure, native paths, exact clocks and full warning keys passed\n");
    }
#endif

    void AppLocalCore(const char* core, const char* executable)
    {
        const auto directory = std::filesystem::absolute(executable).parent_path() / "D3D12";
        const auto packagedCore = directory / "D3D12Core.dll";
        Require(!std::filesystem::exists(directory), "app-local core check requires an isolated executable directory");
        struct ObservedErrors { size_t count = 0u; bool exact = true; } errors;
        const auto previous = log::GetCallback();
        log::SetCallback({[](void* context, log::Severity severity, const char* text)
        {
            auto& observed = *static_cast<ObservedErrors*>(context);
            ++observed.count;
            observed.exact = observed.exact && severity == log::Severity::Error && std::string_view(text) ==
                "App-local D3D12Core.dll is missing or differs from the pinned Direct3D Agility SDK 1.619.5 runtime";
        }, &errors});
        Require(!VerifyAppLocalD3D12Core() && errors.count == 1u && errors.exact,
            "missing app-local runtime lost its diagnostic");
        std::filesystem::create_directory(directory);
        std::filesystem::copy_file(core, packagedCore);
        Require(VerifyAppLocalD3D12Core() && errors.count == 1u,
            "native executable directory did not resolve its exact runtime");
        {
            std::ofstream shortened(packagedCore, std::ios::binary | std::ios::trunc);
            shortened << "short";
            Require(bool(shortened), "cannot prepare short runtime fixture");
        }
        Require(!VerifyAppLocalD3D12Core() && errors.count == 2u && errors.exact,
            "wrong runtime size was accepted or lost its diagnostic");
        std::filesystem::copy_file(core, packagedCore, std::filesystem::copy_options::overwrite_existing);
        Require(VerifyAppLocalD3D12Core() && errors.count == 2u,
            "runtime verification did not release handles or recover after replacement");
        Require(!VerifyD3D12CoreFile(nullptr) && !VerifyD3D12CoreFile(L"") &&
            !VerifyD3D12CoreFile(directory.c_str()), "invalid or directory runtime input was accepted");
        log::SetCallback(previous);
    }

    void DurableLogging(const std::filesystem::path& scratch, const char* core, const char* executable)
    {
        Require(VerifyD3D12CoreFile(std::filesystem::path(core).c_str()), "the pinned D3D12Core bytes were rejected");
        std::filesystem::create_directories(scratch);
        const auto tamperedCore = scratch / "tampered-D3D12Core.dll";
        std::filesystem::copy_file(core, tamperedCore, std::filesystem::copy_options::overwrite_existing);
        {
            std::fstream tamper(tamperedCore, std::ios::binary | std::ios::in | std::ios::out);
            tamper.put('\0');
        }
        Require(!VerifyD3D12CoreFile(tamperedCore.c_str()), "tampered D3D12Core bytes were accepted");
        std::vector<std::pair<log::Severity, std::string>> messages;
        log::SetMinimumSeverity(log::Severity::Info);
        log::SetCallback({[](void* context, log::Severity severity, const char* message)
        {
            using Messages = std::vector<std::pair<log::Severity, std::string>>;
            static_cast<Messages*>(context)->emplace_back(severity, message);
            const log::Callback installed = log::GetCallback();
            log::SetCallback(installed);
        }, &messages});
        log::debug("filtered %d", 1);
        log::info("identity %s %d", "value", 7);
        log::warning("warning");
        Require(messages == std::vector<std::pair<log::Severity, std::string>>{
            {log::Severity::Info, "identity value 7"}, {log::Severity::Warning, "warning"}} && log::GetCallback().function,
            "minimum severity, printf formatting or readable callback changed");
        log::info(nullptr);
        const std::string longMessage(5000, 'x');
        log::info("%s", longMessage.c_str());
        Require(messages.size() == 4 && messages[2].second.empty() &&
            messages[3].second == longMessage.substr(0, 4095) &&
            log::GetCallback().context == &messages,
            "empty formatting, bounded message storage or borrowed callback context changed");
        messages.clear();
        RendererNvrhiMessageCallback callback;
        Require(callback.GetErrorCount() == 0, "new NVRHI callback retained errors");
        const char* detail = "CreateGraphicsPipelineState failed, HRESULT = 0x887a0006\nDRED page-fault VA = 0x1234";
        callback.message(nvrhi::MessageSeverity::Info, "device selected");
        callback.message(nvrhi::MessageSeverity::Warning, "heap pressure");
        Require(callback.GetErrorCount() == 0, "non-error messages invalidated recording");
        callback.message(nvrhi::MessageSeverity::Error, detail);
        Require(callback.GetErrorCount() == 1, "void GPU failure did not invalidate recording");
        Require(messages == std::vector<std::pair<log::Severity, std::string>>{
            {log::Severity::Info, "device selected"}, {log::Severity::Warning, "heap pressure"}, {log::Severity::Error, detail}},
            "NVRHI severity mapping or exact HRESULT/DRED text changed");

        const auto logPath = scratch / "nested" / "uvsr-engine.log";
        auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
        Require(InitializeEngineDiagnosticLog(logPath.c_str(), {[](void* context) noexcept {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                static_cast<std::chrono::steady_clock::time_point*>(context)->time_since_epoch()).count();
        }, &now}) && std::filesystem::is_regular_file(logPath),
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
        Require(log::GetCallback().function != nullptr, "empty callback did not restore the direct default");

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
        Require(uvsr::InitializeEngineDiagnosticLog(std::filesystem::path(argv[2]).c_str()), "fatal child could not initialize its durable log");
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
    AppLocalCore(argv[2], argv[0]);
    DurableLogging(argv[1], argv[2], argv[0]);
#if defined(UVSR_ENGINE_LOG_TEST_HOOKS)
    DiagnosticLogFailures(argv[1]);
#endif
    return EXIT_SUCCESS;
}
