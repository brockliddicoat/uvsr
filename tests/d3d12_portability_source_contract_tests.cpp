#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <nvrhi/nvrhi.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

#include "gpu_capabilities.h"
#include "uvsr-d3d12-diagnostics.h"

namespace
{
    [[noreturn]] void Fail(const char* message)
    {
        std::cerr << "D3D12 portability contract failed: "
                  << message << '\n';
        std::exit(EXIT_FAILURE);
    }

    void Require(bool condition, const char* message)
    {
        if (!condition)
            Fail(message);
    }

    struct RecordingCallback final : nvrhi::IMessageCallback
    {
        uint32_t count = 0u;
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

    [[nodiscard]] size_t CountOccurrences(
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

    void RequireSingleRecord(
        const RecordingCallback& callback,
        nvrhi::MessageSeverity severity,
        size_t dredRecords,
        const char* message)
    {
        Require(callback.count == 1u, message);
        Require(callback.severity == severity, message);
        Require(CountOccurrences(callback.text, "DRED") == dredRecords,
            message);
    }
}

int main()
{
    using namespace nvrhi::d3d12::uvsr_diagnostics;

    static_assert(uvsr::MinimumShaderModel == 0x65u);
    static_assert(uvsr::MinimumD3DFeatureLevel == 0xb000u);
    static_assert(uvsr::MinimumBindlessResourceBindingTier == 2u);
    static_assert(uvsr::MinimumInlineRayTracingTier == 11u);
    static_assert(!uvsr::SupportsRequiredShaderModel(0x64u));
    static_assert(uvsr::SupportsRequiredShaderModel(0x65u));
    static_assert(!uvsr::SupportsRequiredFeatureLevel(0xa100u));
    static_assert(uvsr::SupportsRequiredFeatureLevel(0xb000u));
    static_assert(!uvsr::SupportsBindlessResourceTables(1u));
    static_assert(uvsr::SupportsBindlessResourceTables(2u));
    static_assert(!uvsr::SupportsOptionalRayQueryRendering(2u, 10u));
    static_assert(uvsr::SupportsOptionalRayQueryRendering(2u, 11u));
    static_assert(FenceWaitTimeoutMilliseconds == 30000u);
    static_assert(FenceWaitTimeoutMilliseconds < INFINITE);

    {
        const auto below = ObserveFenceCompletion(40u);
        const auto reached = ObserveFenceCompletion(41u);
        const auto failed = ObserveFenceCompletion(UINT64_MAX);
        Require(!below.failed && !below.HasReached(41u),
            "a value below the target completed the fence");
        Require(!reached.failed && reached.HasReached(41u),
            "the exact target did not complete the fence");
        Require(failed.failed && failed.completedValue == 0u &&
                !failed.HasReached(0u),
            "UINT64_MAX was not preserved as a terminal fence failure");
    }

    {
        std::mutex mutex;
        std::atomic<uint64_t> lastSubmitted{ 9u };
        std::atomic<bool> failed{ false };
        uint64_t signaled = 0u;
        const auto result = TrySignalFence(
            mutex, lastSubmitted, failed,
            [&](uint64_t candidate)
            {
                signaled = candidate;
                return S_OK;
            });
        Require(result.attempted && result.Succeeded() &&
                result.candidateValue == 10u &&
                result.PublishedValue() == 10u && signaled == 10u &&
                lastSubmitted.load() == 10u && !failed.load(),
            "a successful Signal did not atomically publish its value");
    }

    {
        std::mutex mutex;
        std::atomic<uint64_t> lastSubmitted{ 19u };
        std::atomic<bool> failed{ false };
        uint32_t calls = 0u;
        const auto first = TrySignalFence(
            mutex, lastSubmitted, failed,
            [&](uint64_t candidate)
            {
                ++calls;
                Require(candidate == 20u,
                    "Signal received the wrong candidate value");
                return DXGI_ERROR_DEVICE_REMOVED;
            });
        const auto second = TrySignalFence(
            mutex, lastSubmitted, failed,
            [&](uint64_t)
            {
                ++calls;
                return S_OK;
            });
        Require(first.attempted && !first.Succeeded() &&
                first.PublishedValue() == 0u &&
                lastSubmitted.load() == 19u && failed.load(),
            "a failed Signal changed the published value");
        Require(!second.attempted && second.result == E_ABORT &&
                calls == 1u,
            "a latched Signal failure allowed another submission");
    }

    {
        std::mutex mutex;
        std::atomic<uint64_t> lastSubmitted{ UINT64_MAX };
        std::atomic<bool> failed{ false };
        bool invoked = false;
        const auto result = TrySignalFence(
            mutex, lastSubmitted, failed,
            [&](uint64_t)
            {
                invoked = true;
                return S_OK;
            });
        Require(result.attempted && !result.Succeeded() &&
                result.result == E_FAIL && failed.load() && !invoked,
            "fence value overflow invoked Signal or remained recoverable");
    }

    Require(IsDeviceRemovalFailure(
            DXGI_ERROR_DEVICE_REMOVED, S_OK),
        "DXGI_ERROR_DEVICE_REMOVED was not terminal");
    Require(IsDeviceRemovalFailure(DXGI_ERROR_DEVICE_HUNG, S_OK),
        "DXGI_ERROR_DEVICE_HUNG was not terminal");
    Require(IsDeviceRemovalFailure(DXGI_ERROR_DEVICE_RESET, S_OK),
        "DXGI_ERROR_DEVICE_RESET was not terminal");
    Require(IsDeviceRemovalFailure(
            DXGI_ERROR_DRIVER_INTERNAL_ERROR, S_OK),
        "DXGI_ERROR_DRIVER_INTERNAL_ERROR was not terminal");
    Require(IsDeviceRemovalFailure(E_FAIL, DXGI_ERROR_DEVICE_REMOVED),
        "a failed device-removed reason was not terminal");
    Require(!IsDeviceRemovalFailure(E_FAIL, S_OK),
        "an ordinary failure was misclassified as device removal");
    Require(!IsDeviceRemovalFailure(S_OK, S_OK),
        "success was misclassified as device removal");

    {
        RecordingCallback callback;
        std::stringstream message;
        message << "D3D12 fence wait timed out";
        Require(!ReportFailure(
                message, HRESULT_FROM_WIN32(ERROR_TIMEOUT), S_OK,
                nullptr, &callback, true),
            "a terminal timeout returned success");
        RequireSingleRecord(callback, nvrhi::MessageSeverity::Fatal, 0u,
            "a terminal timeout did not emit one fatal record");
    }

    {
        RecordingCallback callback;
        std::stringstream message;
        message << "D3D12 device removal";
        Require(!ReportFailure(
                message, DXGI_ERROR_DEVICE_REMOVED,
                DXGI_ERROR_DEVICE_REMOVED, nullptr, &callback, false),
            "device removal returned success");
        RequireSingleRecord(callback, nvrhi::MessageSeverity::Fatal, 1u,
            "device removal did not emit one DRED-bearing fatal record");
        Require(callback.text.find("DRED device unavailable") !=
                std::string::npos,
            "null-device removal did not explain unavailable DRED");
    }

    {
        RecordingCallback callback;
        std::stringstream message;
        message << "CreateGraphicsPipelineState failed";
        Require(!ReportFailure(
                message, E_FAIL, S_OK, nullptr, &callback, false),
            "an ordinary D3D12 failure returned success");
        RequireSingleRecord(callback, nvrhi::MessageSeverity::Error, 0u,
            "an ordinary D3D12 failure did not remain a single error");
    }

    return EXIT_SUCCESS;
}
