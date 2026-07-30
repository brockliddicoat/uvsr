#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace uvsr
{
    struct GpuPerformanceMetrics
    {
        double memoryBandwidthGBps = 0.0;
        double gpuGFlops = 0.0;
        double gpuUtilization = 0.0;
        double telemetryAgeMilliseconds = 0.0;
        uint64_t telemetryGeneration = 0u;
        bool gpuUtilizationValid = false;
        bool valid = false;
    };

    constexpr double GpuTimingNormalizationReferenceTFlops = 42.5;
    constexpr double GpuTimingNormalizationReferenceBandwidthGBps =
        582.464;
    constexpr double GpuTimingNormalizationMaximumFreshAgeMilliseconds =
        600.0;
    constexpr double GpuTimingNormalizationMaximumUsableAgeMilliseconds =
        1000.0;

    struct GpuTimingNormalizationCalibration
    {
        std::string_view id;
        std::string_view exactAdapterName;
        double referenceClockCapacityTFlops = 0.0;
        double referenceMemoryBandwidthGBps = 0.0;
        double memoryBandwidthGradeBTolerance = 0.05;
        double memoryBandwidthDirectionalTolerance = 0.15;
    };

    constexpr GpuTimingNormalizationCalibration
        Rtx4090LaptopGpuTimingNormalizationCalibration = {
            "brock-rtx4090-laptop-v1",
            "NVIDIA GeForce RTX 4090 Laptop GPU",
            GpuTimingNormalizationReferenceTFlops,
            GpuTimingNormalizationReferenceBandwidthGBps,
            0.05,
            0.15
        };

    [[nodiscard]] inline const GpuTimingNormalizationCalibration*
        FindGpuTimingNormalizationCalibration(std::string_view adapterName)
    {
        return adapterName ==
                Rtx4090LaptopGpuTimingNormalizationCalibration.exactAdapterName
            ? &Rtx4090LaptopGpuTimingNormalizationCalibration
            : nullptr;
    }

    enum class GpuTimingNormalizationGrade
    {
        Unavailable,
        A,
        B,
        C,
        Directional
    };

    struct GpuTimingNormalizationEstimate
    {
        double rawMilliseconds = 0.0;
        double currentClockCapacityTFlops = 0.0;
        double utilizedTFlops = 0.0;
        double referenceTFlops = 0.0;
        double referenceMemoryBandwidthGBps = 0.0;
        double memoryBandwidthRatio = 0.0;
        double telemetryAgeMilliseconds = 0.0;
        uint64_t telemetryGeneration = 0u;
        double scaleToReference = 0.0;
        double estimatedMilliseconds = 0.0;
        double workIndexMillisecondsTFlops = 0.0;
        bool utilizationValid = false;
        GpuTimingNormalizationGrade grade =
            GpuTimingNormalizationGrade::Unavailable;
        bool valid = false;
    };

    inline const char* GetGpuTimingNormalizationGradeLabel(
        GpuTimingNormalizationGrade grade)
    {
        switch (grade)
        {
        case GpuTimingNormalizationGrade::A:
            return "A";
        case GpuTimingNormalizationGrade::B:
            return "B";
        case GpuTimingNormalizationGrade::C:
            return "C";
        case GpuTimingNormalizationGrade::Directional:
            return "directional";
        default:
            return "unavailable";
        }
    }

    [[nodiscard]] constexpr GpuTimingNormalizationGrade
        WorseGpuTimingNormalizationGrade(
            GpuTimingNormalizationGrade left,
            GpuTimingNormalizationGrade right)
    {
        if (left == GpuTimingNormalizationGrade::Unavailable)
            return right;
        if (right == GpuTimingNormalizationGrade::Unavailable)
            return left;
        return uint32_t(left) >= uint32_t(right) ? left : right;
    }

    inline GpuTimingNormalizationEstimate NormalizeGpuTimingMilliseconds(
        double rawMilliseconds,
        const GpuPerformanceMetrics& metrics,
        std::string_view adapterName)
    {
        GpuTimingNormalizationEstimate estimate;
        estimate.rawMilliseconds = rawMilliseconds;
        const GpuTimingNormalizationCalibration* calibration =
            FindGpuTimingNormalizationCalibration(adapterName);
        if (!calibration)
            return estimate;

        estimate.referenceTFlops =
            calibration->referenceClockCapacityTFlops;
        estimate.referenceMemoryBandwidthGBps =
            calibration->referenceMemoryBandwidthGBps;
        estimate.telemetryAgeMilliseconds =
            metrics.telemetryAgeMilliseconds;
        estimate.telemetryGeneration = metrics.telemetryGeneration;

        const double currentClockCapacityTFlops =
            metrics.gpuGFlops / 1000.0;
        if (!metrics.valid ||
            !std::isfinite(rawMilliseconds) ||
            rawMilliseconds <= 0.0 ||
            !std::isfinite(currentClockCapacityTFlops) ||
            currentClockCapacityTFlops <= 0.0 ||
            !std::isfinite(calibration->referenceClockCapacityTFlops) ||
            calibration->referenceClockCapacityTFlops <= 0.0 ||
            !std::isfinite(metrics.memoryBandwidthGBps) ||
            metrics.memoryBandwidthGBps <= 0.0 ||
            !std::isfinite(calibration->referenceMemoryBandwidthGBps) ||
            calibration->referenceMemoryBandwidthGBps <= 0.0 ||
            !std::isfinite(metrics.telemetryAgeMilliseconds) ||
            metrics.telemetryAgeMilliseconds < 0.0 ||
            metrics.telemetryGeneration == 0u)
        {
            return estimate;
        }

        estimate.currentClockCapacityTFlops =
            currentClockCapacityTFlops;
        estimate.scaleToReference =
            currentClockCapacityTFlops /
            calibration->referenceClockCapacityTFlops;
        estimate.estimatedMilliseconds =
            rawMilliseconds * estimate.scaleToReference;
        estimate.workIndexMillisecondsTFlops =
            rawMilliseconds * currentClockCapacityTFlops;
        estimate.memoryBandwidthRatio =
            metrics.memoryBandwidthGBps /
            calibration->referenceMemoryBandwidthGBps;
        if (!std::isfinite(estimate.estimatedMilliseconds) ||
            !std::isfinite(estimate.workIndexMillisecondsTFlops) ||
            !std::isfinite(estimate.memoryBandwidthRatio) ||
            estimate.memoryBandwidthRatio <= 0.0)
        {
            return estimate;
        }

        const bool utilizationAvailable =
            metrics.gpuUtilizationValid &&
            std::isfinite(metrics.gpuUtilization) &&
            metrics.gpuUtilization >= 0.0 &&
            metrics.gpuUtilization <= 1.0;
        const double utilization = utilizationAvailable
            ? metrics.gpuUtilization
            : 0.0;
        estimate.utilizationValid = utilizationAvailable;
        estimate.utilizedTFlops =
            currentClockCapacityTFlops * utilization;

        // The estimate deliberately uses current-clock capacity rather than
        // utilization-scaled throughput. Utilization only grades whether this
        // run resembles the calibrated, saturated CSM workload.
        if (utilization >= 0.95 &&
            currentClockCapacityTFlops >= 38.0 &&
            currentClockCapacityTFlops <= 47.0)
        {
            estimate.grade = GpuTimingNormalizationGrade::A;
        }
        else if (utilization >= 0.90 &&
            currentClockCapacityTFlops >= 30.0 &&
            currentClockCapacityTFlops <= 50.0)
        {
            estimate.grade = GpuTimingNormalizationGrade::B;
        }
        else if (currentClockCapacityTFlops >= 25.0)
        {
            estimate.grade = GpuTimingNormalizationGrade::C;
        }
        else
        {
            estimate.grade =
                GpuTimingNormalizationGrade::Directional;
        }

        if (!utilizationAvailable)
        {
            estimate.grade = WorseGpuTimingNormalizationGrade(
                estimate.grade,
                GpuTimingNormalizationGrade::C);
        }

        const double bandwidthDelta =
            std::abs(estimate.memoryBandwidthRatio - 1.0);
        if (bandwidthDelta >
            calibration->memoryBandwidthDirectionalTolerance)
        {
            estimate.grade = GpuTimingNormalizationGrade::Directional;
        }
        else if (bandwidthDelta >
            calibration->memoryBandwidthGradeBTolerance)
        {
            estimate.grade = WorseGpuTimingNormalizationGrade(
                estimate.grade,
                GpuTimingNormalizationGrade::C);
        }

        if (metrics.telemetryAgeMilliseconds >
            GpuTimingNormalizationMaximumUsableAgeMilliseconds)
        {
            estimate.grade = GpuTimingNormalizationGrade::Directional;
        }
        else if (metrics.telemetryAgeMilliseconds >
            GpuTimingNormalizationMaximumFreshAgeMilliseconds)
        {
            estimate.grade = WorseGpuTimingNormalizationGrade(
                estimate.grade,
                GpuTimingNormalizationGrade::C);
        }

        estimate.valid = true;
        return estimate;
    }

    struct GpuTimingNormalizationRunGrade
    {
        size_t sampleCount = 0u;
        size_t validSampleCount = 0u;
        double validFraction = 0.0;
        GpuTimingNormalizationGrade grade =
            GpuTimingNormalizationGrade::Unavailable;
    };

    [[nodiscard]] inline GpuTimingNormalizationRunGrade
        SummarizeGpuTimingNormalizationRunGrade(
            size_t sampleCount,
            const std::array<uint32_t, 5u>& gradeCounts)
    {
        GpuTimingNormalizationRunGrade summary;
        summary.sampleCount = sampleCount;
        summary.validSampleCount =
            size_t(gradeCounts[size_t(GpuTimingNormalizationGrade::A)]) +
            size_t(gradeCounts[size_t(GpuTimingNormalizationGrade::B)]) +
            size_t(gradeCounts[size_t(GpuTimingNormalizationGrade::C)]) +
            size_t(gradeCounts[
                size_t(GpuTimingNormalizationGrade::Directional)]);
        summary.validFraction = sampleCount > 0u
            ? double(summary.validSampleCount) / double(sampleCount)
            : 0.0;
        if (sampleCount == 0u || summary.validSampleCount == 0u)
            return summary;

        const double gradeAFraction =
            double(gradeCounts[size_t(GpuTimingNormalizationGrade::A)]) /
            double(sampleCount);
        const double gradeABFraction =
            double(
                gradeCounts[size_t(GpuTimingNormalizationGrade::A)] +
                gradeCounts[size_t(GpuTimingNormalizationGrade::B)]) /
            double(sampleCount);
        const double gradeABCFraction =
            double(
                gradeCounts[size_t(GpuTimingNormalizationGrade::A)] +
                gradeCounts[size_t(GpuTimingNormalizationGrade::B)] +
                gradeCounts[size_t(GpuTimingNormalizationGrade::C)]) /
            double(sampleCount);
        if (gradeAFraction >= 0.95)
            summary.grade = GpuTimingNormalizationGrade::A;
        else if (gradeABFraction >= 0.95)
            summary.grade = GpuTimingNormalizationGrade::B;
        else if (gradeABCFraction >= 0.95)
            summary.grade = GpuTimingNormalizationGrade::C;
        else
            summary.grade = GpuTimingNormalizationGrade::Directional;
        return summary;
    }

    GpuPerformanceMetrics QueryGpuPerformanceMetrics(const char* rendererName);
}
