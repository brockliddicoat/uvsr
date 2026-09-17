#pragma once

#include "json_output.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    struct RetainedRuntimeCase;
    struct RetainedRuntimeProvenance;
    struct RetainedRuntimeTelemetry;
    struct RetainedRuntimeSemanticResult;
    struct RetainedRuntimeMessage;

    [[nodiscard]] json::EncodedText EncodeRetainedRuntimeStartJson(
        const RetainedRuntimeProvenance& provenance,
        std::size_t caseCount) noexcept;
    [[nodiscard]] json::EncodedText EncodeRetainedRuntimeFailureJson(
        std::string_view caseName,
        std::string_view message) noexcept;
    [[nodiscard]] json::EncodedText EncodeRetainedRuntimeMessageJson(
        std::string_view caseName, const RetainedRuntimeMessage& message) noexcept;
    [[nodiscard]] json::EncodedText EncodeRetainedRuntimeSemanticFailureJson(
        std::string_view caseName, const RetainedRuntimeSemanticResult& result) noexcept;
    [[nodiscard]] json::EncodedText EncodeRetainedRuntimeCaseJson(
        std::size_t caseIndex,
        const RetainedRuntimeCase& runtimeCase,
        const RetainedRuntimeTelemetry& telemetry) noexcept;
    [[nodiscard]] json::EncodedText EncodeRetainedRuntimeCaptureJson(
        std::size_t caseIndex,
        const RetainedRuntimeCase& runtimeCase,
        std::string_view phase,
        const RetainedRuntimeTelemetry& telemetry) noexcept;
    [[nodiscard]] json::EncodedText EncodeRetainedRuntimeSummaryJson(
        const RetainedRuntimeProvenance& provenance,
        bool passed,
        std::size_t passedCases,
        std::size_t totalCases,
        std::int64_t elapsedMilliseconds) noexcept;
}
