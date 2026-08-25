#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <nvrhi/nvrhi.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "nvrhi-d3d12-diagnostics.h"

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "NVRHI D3D12 diagnostics test failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
            Fail(message);
    }

    size_t CountOccurrences(
        std::string_view text,
        std::string_view token)
    {
        size_t count = 0u;
        size_t position = 0u;
        while ((position = text.find(token, position)) !=
            std::string_view::npos)
        {
            ++count;
            position += token.size();
        }
        return count;
    }

    struct RecordingCallback final : nvrhi::IMessageCallback
    {
        size_t count = 0u;
        nvrhi::MessageSeverity severity = nvrhi::MessageSeverity::Info;
        std::string text;

        void message(
            nvrhi::MessageSeverity incomingSeverity,
            const char* incomingText) override
        {
            ++count;
            severity = incomingSeverity;
            text = incomingText ? incomingText : "";
        }
    };

    struct FakeFence
    {
        uint64_t completedValue = 0u;

        [[nodiscard]] uint64_t GetCompletedValue() const
        {
            return completedValue;
        }
    };

    struct FakeQuery
    {
        bool resolved = false;
        bool failed = false;

        void Poll(const FakeFence& fence, uint64_t target)
        {
            if (failed)
                return;
            const auto completion =
                nvrhi::d3d12::uvsr_diagnostics::ObserveFenceCompletion(
                    fence.GetCompletedValue());
            if (completion.failed)
            {
                failed = true;
                resolved = false;
                return;
            }
            resolved = completion.HasReached(target);
        }

        void Reset()
        {
            resolved = false;
            failed = false;
        }
    };

    struct FakeQueue
    {
        uint64_t lastCompleted = 0u;
        bool failed = false;

        void Update(const FakeFence& fence)
        {
            if (failed)
                return;
            const auto completion =
                nvrhi::d3d12::uvsr_diagnostics::ObserveFenceCompletion(
                    fence.GetCompletedValue());
            if (completion.failed)
            {
                failed = true;
                return;
            }
            lastCompleted = completion.completedValue;
        }

        [[nodiscard]] bool CanRetire(uint64_t submittedValue) const
        {
            return !failed && lastCompleted >= submittedValue;
        }
    };

    void RequireSingleFatal(
        const RecordingCallback& callback,
        bool expectsDred,
        const char* label)
    {
        Require(callback.count == 1u, label);
        Require(
            callback.severity == nvrhi::MessageSeverity::Fatal,
            label);
        Require(
            CountOccurrences(callback.text, "DRED") ==
                (expectsDred ? 1u : 0u),
            label);
    }
}

int main()
{
    using namespace nvrhi::d3d12::uvsr_diagnostics;

    Require(
        FenceWaitTimeoutMilliseconds > 0u &&
            FenceWaitTimeoutMilliseconds < INFINITE,
        "fence wait timeout must be finite and positive");

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

    {
        // This is a primitive state model. The source-contract companion ties
        // the same latches to the staged query, queue, and retirement paths.
        FakeFence fence{ UINT64_MAX };
        FakeQuery query;
        query.Poll(fence, 3u);
        Require(
            query.failed && !query.resolved,
            "UINT64_MAX must fail rather than resolve a query");

        FakeQueue queue{ 2u, false };
        queue.Update(fence);
        Require(
            queue.failed && queue.lastCompleted == 2u &&
                !queue.CanRetire(3u),
            "UINT64_MAX must not advance a queue or retire GPU work");

        fence.completedValue = 3u;
        query.Poll(fence, 3u);
        Require(
            query.failed && !query.resolved,
            "query failure must remain latched until reset");
        queue.Update(fence);
        Require(
            queue.failed && queue.lastCompleted == 2u &&
                !queue.CanRetire(3u),
            "queue failure must permanently block completion and retirement");

        query.Reset();
        query.Poll(fence, 3u);
        Require(
            !query.failed && query.resolved,
            "an explicitly reset query may observe later completion");
    }

    {
        RecordingCallback callback;
        std::stringstream message;
        message << "D3D12 fence wait timed out";
        const bool result = ReportFailure(
            message,
            HRESULT_FROM_WIN32(ERROR_TIMEOUT),
            S_OK,
            nullptr,
            &callback,
            true);
        Require(!result, "timeout must return failure");
        RequireSingleFatal(
            callback, false,
            "timeout must emit exactly one fatal record without DRED");
    }

    {
        RecordingCallback callback;
        std::stringstream message;
        message << "D3D12 fence device removal";
        const bool result = ReportFailure(
            message,
            DXGI_ERROR_DEVICE_REMOVED,
            DXGI_ERROR_DEVICE_REMOVED,
            nullptr,
            &callback,
            true);
        Require(!result, "device removal must return failure");
        RequireSingleFatal(
            callback, true,
            "device removal must emit one DRED-bearing fatal record");
    }

    {
        RecordingCallback callback;
        std::stringstream message;
        message << "CreateGraphicsPipelineState failed";
        const bool result = ReportFailure(
            message,
            E_FAIL,
            DXGI_ERROR_DEVICE_HUNG,
            nullptr,
            &callback,
            false);
        Require(!result, "removed-device graphics PSO must return failure");
        RequireSingleFatal(
            callback, true,
            "removed-device graphics PSO must emit one fatal DRED record");
    }

    {
        RecordingCallback callback;
        std::stringstream message;
        message << "CreateGraphicsPipelineState failed";
        const bool result = ReportFailure(
            message,
            E_INVALIDARG,
            S_OK,
            nullptr,
            &callback,
            false);
        Require(!result, "ordinary graphics PSO failure must return failure");
        Require(
            callback.count == 1u &&
                callback.severity == nvrhi::MessageSeverity::Error &&
                callback.text.find("DRED") == std::string::npos,
            "ordinary graphics PSO failure must remain a single non-DRED error");
    }

    return EXIT_SUCCESS;
}
