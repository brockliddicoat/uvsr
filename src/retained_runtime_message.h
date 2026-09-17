#pragma once

#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace uvsr
{
    struct RuntimeOutputEvidence;

    // text remains live through synchronous failure encoding. waits retain only
    // literals and immutable case text; terminal messages may borrow live errors.
    struct RetainedRuntimeMessage
    {
        enum class Format : std::uint8_t
        {
            Parts, PathCount, SnapshotMismatch, BaselineTiming,
            ActionTiming, InvalidOutput, Invalid
        };

        Format format = Format::Parts;
        std::uint8_t partCount = 0;
        bool caseTimeout = false;
        std::string_view parts[4]{};
        // the largest retained output failure contains nine integers and two
        // decimals. action timing uses four decimals. factories own slot order.
        std::uint64_t integers[9]{};
        double decimals[4]{};

        RetainedRuntimeMessage() noexcept = default;
        RetainedRuntimeMessage(std::string_view text) noexcept
            : partCount(1), parts{text} {}
        RetainedRuntimeMessage(const char* text) noexcept
            : RetainedRuntimeMessage(text ? std::string_view(text) : std::string_view())
        {
            if (!text) format = Format::Invalid;
        }
        RetainedRuntimeMessage(std::initializer_list<std::string_view> text) noexcept
        {
            if (text.size() > 4) { format = Format::Invalid; return; }
            for (const auto part : text) parts[partCount++] = part;
        }

        [[nodiscard]] static RetainedRuntimeMessage PathCount(
            std::uint64_t observed, std::uint64_t expected) noexcept;
        [[nodiscard]] static RetainedRuntimeMessage SnapshotMismatch(
            std::string_view expected, std::string_view actual) noexcept;
        [[nodiscard]] static RetainedRuntimeMessage BaselineTiming(double cpu, double gpu) noexcept;
        [[nodiscard]] static RetainedRuntimeMessage ActionTiming(
            double cpu, double cpuLimit, double gpu, double gpuLimit) noexcept;
        [[nodiscard]] static RetainedRuntimeMessage InvalidOutput(const RuntimeOutputEvidence& output) noexcept;
    };
}
