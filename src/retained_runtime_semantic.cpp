#include "retained_runtime_semantic.h"
#include "retained_runtime_diagnostic.h"

#include <algorithm>
#include <cmath>

namespace uvsr
{
    RuntimeSemanticSignature BuildRuntimeSemanticSignature(
        const RuntimeOutputEvidence& output) noexcept
    {
        RuntimeSemanticSignature signature;
        signature.width = output.width;
        signature.height = output.height;
        signature.meanLinearLuminance = output.meanLinearLuminance;
        signature.rmsLinearLuminance = output.rmsLinearLuminance;
        signature.meanLinearHorizontalGradient =
            output.meanLinearHorizontalGradient;
        signature.linearLuminanceHistogram =
            output.linearLuminanceHistogram;
        for (const std::uint64_t count : output.linearLuminanceHistogram)
            signature.linearLuminanceSampleCount += count;
        return signature;
    }

    bool RuntimeSemanticSignaturesAreDistinct(
        const RuntimeSemanticSignature& left,
        const RuntimeSemanticSignature& right) noexcept
    {
        if (left.width == 0u || left.height == 0u ||
            left.width != right.width || left.height != right.height)
        {
            return false;
        }
        const auto differs = [](
            double leftValue,
            double rightValue,
            double absoluteTolerance,
            double relativeTolerance)
        {
            if (!std::isfinite(leftValue) || !std::isfinite(rightValue))
                return false;
            const double difference = std::fabs(leftValue - rightValue);
            const double scale = std::max(
                std::fabs(leftValue), std::fabs(rightValue));
            return difference > std::max(
                absoluteTolerance, scale * relativeTolerance);
        };
        double histogramDistance = 0.0;
        if (left.linearLuminanceSampleCount > 0u &&
            right.linearLuminanceSampleCount > 0u)
        {
            for (std::size_t index = 0u;
                index < left.linearLuminanceHistogram.size(); ++index)
            {
                const double leftFrequency =
                    double(left.linearLuminanceHistogram[index]) /
                    double(left.linearLuminanceSampleCount);
                const double rightFrequency =
                    double(right.linearLuminanceHistogram[index]) /
                    double(right.linearLuminanceSampleCount);
                histogramDistance += std::fabs(
                    leftFrequency - rightFrequency);
            }
        }
        return
            differs(left.meanLinearLuminance,
                right.meanLinearLuminance, 0.00005, 0.001) ||
            differs(left.rmsLinearLuminance,
                right.rmsLinearLuminance, 0.00005, 0.001) ||
            differs(left.meanLinearHorizontalGradient,
                right.meanLinearHorizontalGradient, 0.00005, 0.002) ||
            histogramDistance > 0.001;
    }

    namespace
    {
        using SemanticFailure = RetainedRuntimeSemanticFailure;

        [[nodiscard]] bool ValidCases(const RetainedRuntimeCase* cases,
            std::size_t caseCount, std::size_t& expectedCount) noexcept
        {
            expectedCount = 0;
            if ((!cases && caseCount) || caseCount > std::size_t(PTRDIFF_MAX) / sizeof(*cases)) return false;
            for (std::size_t index = 0; index < caseCount; ++index)
            {
                const auto& runtimeCase = cases[index];
                if (!runtimeCase.exerciseRetainedStateChanges) continue;
                if (runtimeCase.name.View().empty()) return false;
                for (std::size_t prior = 0; prior < index; ++prior)
                    if (cases[prior].exerciseRetainedStateChanges &&
                        cases[prior].name.View() == runtimeCase.name.View()) return false;
                ++expectedCount;
            }
            return true;
        }

        [[nodiscard]] bool ValidCapture(const RetainedRuntimeCase& runtimeCase,
            std::string_view sceneToken, const RuntimeSemanticSignature& signature) noexcept
        {
            return !sceneToken.empty() &&
                (sceneToken == runtimeCase.actionBaselineSceneToken.View() ||
                    sceneToken == runtimeCase.expectedSceneToken.View()) &&
                signature.width != 0u && signature.height != 0u;
        }
    }

    void RetainedRuntimeSemanticSummary::BeginCase() noexcept
    {
        m_CaseCaptureCount = 0;
        m_FirstSceneToken = {};
        m_PairIsDistinct = false;
    }

    void RetainedRuntimeSemanticSummary::Record(const RetainedRuntimeCase& runtimeCase,
        std::string_view sceneToken, const RuntimeSemanticSignature& signature) noexcept
    {
        ++m_CaptureCount;
        if (!ValidCapture(runtimeCase, sceneToken, signature)) m_CaptureIdentityDrifted = true;
        if (m_CaseCaptureCount == 0)
        {
            m_FirstSignature = signature;
            m_FirstSceneToken = sceneToken;
        }
        else if (m_CaseCaptureCount == 1)
        {
            m_PairIsDistinct = m_FirstSceneToken != sceneToken &&
                RuntimeSemanticSignaturesAreDistinct(m_FirstSignature, signature);
        }
        ++m_CaseCaptureCount;
    }

    void RetainedRuntimeSemanticSummary::CompleteCase(const RetainedRuntimeCase& runtimeCase) noexcept
    {
        if (!runtimeCase.exerciseRetainedStateChanges) return;
        if (m_CaseCaptureCount) ++m_RepresentedCases;
        if ((m_CaseCaptureCount != 2u || !m_PairIsDistinct) &&
            (!m_FirstFailedCase || runtimeCase.name.View() < m_FirstFailedCase->name.View()))
            m_FirstFailedCase = &runtimeCase;
    }

    RetainedRuntimeSemanticResult RetainedRuntimeSemanticSummary::Validate(
        const RetainedRuntimeCase* cases, std::size_t caseCount) const noexcept
    {
        std::size_t expectedCount = 0;
        if (!ValidCases(cases, caseCount, expectedCount)) return {SemanticFailure::CaseIdentity};
        if (m_CaptureIdentityDrifted) return {SemanticFailure::CaptureIdentity};
        if (m_RepresentedCases != expectedCount || m_CaptureCount != expectedCount * 2u)
            return {SemanticFailure::Coverage};
        if (m_FirstFailedCase) return {SemanticFailure::NotDistinct, m_FirstFailedCase};
        return {};
    }

    RetainedRuntimeSemanticResult CheckRetainedRuntimeSemanticCaptures(
        const RetainedRuntimeCase* cases, std::size_t caseCount,
        const RetainedRuntimeSemanticCaptureView* captures, std::size_t captureCount) noexcept
    {
        std::size_t expectedCount = 0;
        if (!ValidCases(cases, caseCount, expectedCount)) return {SemanticFailure::CaseIdentity};
        if ((!captures && captureCount) || captureCount > std::size_t(PTRDIFF_MAX) / sizeof(*captures))
            return {SemanticFailure::CaptureIdentity};
        for (std::size_t index = 0; index < captureCount; ++index)
        {
            const auto& capture = captures[index];
            const RetainedRuntimeCase* runtimeCase = nullptr;
            for (std::size_t candidate = 0; candidate < caseCount; ++candidate)
            {
                if (cases[candidate].exerciseRetainedStateChanges &&
                    cases[candidate].name.View() == capture.caseName)
                {
                    runtimeCase = &cases[candidate];
                    break;
                }
            }
            if (!runtimeCase || !ValidCapture(*runtimeCase, capture.sceneToken, capture.signature))
                return {SemanticFailure::CaptureIdentity};
        }
        if (captureCount != expectedCount * 2u) return {SemanticFailure::Coverage};

        RetainedRuntimeSemanticSummary summary;
        for (std::size_t index = 0; index < caseCount; ++index)
        {
            const auto& runtimeCase = cases[index];
            if (!runtimeCase.exerciseRetainedStateChanges) continue;
            summary.BeginCase();
            for (std::size_t capture = 0; capture < captureCount; ++capture)
                if (captures[capture].caseName == runtimeCase.name.View())
                    summary.Record(runtimeCase, captures[capture].sceneToken, captures[capture].signature);
            summary.CompleteCase(runtimeCase);
        }
        return summary.Validate(cases, caseCount);
    }
}
