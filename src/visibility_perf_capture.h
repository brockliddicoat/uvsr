#pragma once

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace uvsr
{
    enum class VisibilityPerfPriority
    {
        Native,
        Normal,
        High
    };

    struct VisibilityPerfCaptureOptions
    {
        std::filesystem::path outputBase;
        uint32_t warmupFrames = 120u;
        uint32_t measuredFrames = 600u;
        VisibilityPerfPriority priority = VisibilityPerfPriority::Native;
        std::string variants = "factory";
        std::filesystem::path readyMarker;
        double minimumMeasuredSeconds = 0.0;
        bool isolateVisibility = false;

        [[nodiscard]] bool Enabled() const
        {
            return !outputBase.empty() && measuredFrames > 0u;
        }

        [[nodiscard]] bool HasVariant(std::string_view requested) const
        {
            size_t begin = 0u;
            while (begin < variants.size())
            {
                const size_t end = variants.find(',', begin);
                size_t tokenBegin = begin;
                size_t tokenEnd =
                    end == std::string::npos ? variants.size() : end;
                while (tokenBegin < tokenEnd &&
                    std::isspace(static_cast<unsigned char>(
                        variants[tokenBegin])))
                {
                    ++tokenBegin;
                }
                while (tokenEnd > tokenBegin &&
                    std::isspace(static_cast<unsigned char>(
                        variants[tokenEnd - 1u])))
                {
                    --tokenEnd;
                }
                if (std::string_view(variants).substr(
                        tokenBegin, tokenEnd - tokenBegin) == requested)
                {
                    return true;
                }
                if (end == std::string::npos)
                    break;
                begin = end + 1u;
            }
            return false;
        }
    };

    inline bool VisibilityPerfEnvironmentFlag(const char* name)
    {
        const char* value = std::getenv(name);
        if (!value)
            return false;
        const std::string text(value);
        return text == "1" || text == "true" || text == "on" ||
            text == "yes";
    }

    inline uint32_t VisibilityPerfEnvironmentUint(
        const char* name,
        uint32_t fallback,
        uint32_t minimum,
        uint32_t maximum)
    {
        const char* value = std::getenv(name);
        if (!value || value[0] == '\0')
            return fallback;
        uint32_t parsed = 0u;
        const char* end = value + std::char_traits<char>::length(value);
        const auto result = std::from_chars(value, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end ||
            parsed < minimum || parsed > maximum)
        {
            return fallback;
        }
        return parsed;
    }

    inline VisibilityPerfCaptureOptions
        LoadVisibilityPerfCaptureOptions()
    {
        VisibilityPerfCaptureOptions options;
        if (const char* output = std::getenv("UVSR_PERF_OUTPUT");
            output && output[0] != '\0')
        {
            options.outputBase = output;
        }
        options.warmupFrames = VisibilityPerfEnvironmentUint(
            "UVSR_PERF_WARMUP", 120u, 0u, 100000u);
        options.measuredFrames = VisibilityPerfEnvironmentUint(
            "UVSR_PERF_FRAMES", 600u, 1u, 100000u);
        if (const char* variants = std::getenv("UVSR_PERF_VARIANT");
            variants && variants[0] != '\0')
        {
            options.variants = variants;
            std::transform(
                options.variants.begin(),
                options.variants.end(),
                options.variants.begin(),
                [](unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
        }
        if (const char* priority = std::getenv("UVSR_PERF_PRIORITY");
            priority && priority[0] != '\0')
        {
            const std::string value(priority);
            if (value == "normal")
                options.priority = VisibilityPerfPriority::Normal;
            else if (value == "high")
                options.priority = VisibilityPerfPriority::High;
        }
        options.isolateVisibility =
            VisibilityPerfEnvironmentFlag("UVSR_PERF_ISOLATE_VISIBILITY");
        if (const char* marker = std::getenv("UVSR_PERF_READY_MARKER");
            marker && marker[0] != '\0')
        {
            options.readyMarker = marker;
        }
        if (const char* seconds = std::getenv("UVSR_PERF_MIN_SECONDS");
            seconds && seconds[0] != '\0')
        {
            char* parseEnd = nullptr;
            const double parsed = std::strtod(seconds, &parseEnd);
            if (parseEnd && parseEnd[0] == '\0' &&
                std::isfinite(parsed) && parsed >= 0.0 &&
                parsed <= 3600.0)
            {
                options.minimumMeasuredSeconds = parsed;
            }
        }
        return options;
    }

    struct VisibilityPerfCaptureMetadata
    {
        std::string build;
        std::string adapter;
        std::string livePriority;
        std::string permutation;
        std::string settings;
        std::string timingProvenance;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        uint64_t outputTextureBytes = 0u;
        uint64_t workingTextureBytes = 0u;
        uint64_t rawAmbientTextureBytes = 0u;
        uint64_t rawIndirectTextureBytes = 0u;
    };

    struct VisibilityPerfCaptureSample
    {
        uint32_t sampleIndex = 0u;
        uint64_t arrivalFrameId = 0u;
        uint64_t traceFrameId = 0u;
        uint64_t compositionFrameId = 0u;
        uint64_t effectFrameId = 0u;
        bool traceCompositionAligned = false;
        double frameStartToStartMs = 0.0;
        double traceMs = 0.0;
        double compositionMs = 0.0;
        double effectMs = 0.0;
        double depthMs = 0.0;
        double temporalMs = 0.0;
        double spatialMs = 0.0;
    };

    struct VisibilityPerfDistribution
    {
        double mean = 0.0;
        double median = 0.0;
        double p95 = 0.0;
        double minimum = 0.0;
        double maximum = 0.0;
    };

    inline VisibilityPerfDistribution SummarizeVisibilityPerfValues(
        std::vector<double> values)
    {
        VisibilityPerfDistribution result;
        if (values.empty())
            return result;
        std::sort(values.begin(), values.end());
        result.mean = std::accumulate(
            values.begin(), values.end(), 0.0) / double(values.size());
        result.minimum = values.front();
        result.maximum = values.back();
        const auto percentile = [&values](double probability)
        {
            const double rank =
                probability * double(values.size() - 1u);
            const size_t low = static_cast<size_t>(std::floor(rank));
            const size_t high = static_cast<size_t>(std::ceil(rank));
            const double fraction = rank - double(low);
            return values[low] * (1.0 - fraction) +
                values[high] * fraction;
        };
        result.median = percentile(0.5);
        result.p95 = percentile(0.95);
        return result;
    }

    inline std::string EscapeVisibilityPerfJson(std::string_view value)
    {
        std::string result;
        result.reserve(value.size() + 8u);
        for (const char character : value)
        {
            switch (character)
            {
            case '\\': result += "\\\\"; break;
            case '"': result += "\\\""; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(character); break;
            }
        }
        return result;
    }

    class VisibilityPerfCapture
    {
    public:
        explicit VisibilityPerfCapture(
            VisibilityPerfCaptureOptions options)
            : m_Options(std::move(options))
        {
        }

        [[nodiscard]] const VisibilityPerfCaptureOptions& Options() const
        {
            return m_Options;
        }

        [[nodiscard]] bool Enabled() const
        {
            return m_Options.Enabled();
        }

        [[nodiscard]] bool Complete() const
        {
            return m_Complete;
        }

        [[nodiscard]] bool Failed() const
        {
            return m_Failed;
        }

        [[nodiscard]] bool ReadyToClose() const
        {
            return m_Complete || m_Failed;
        }

        void BeginFrame(bool ready)
        {
            if (!Enabled() || m_Complete)
                return;
            const auto now = std::chrono::steady_clock::now();
            PollMarker(now);
            if (m_Failed)
                return;
            if (m_ThresholdsMet &&
                (m_Options.readyMarker.empty() || m_MarkerComplete))
            {
                Finalize();
                return;
            }
            const bool markerAllowsMeasurement =
                m_Options.readyMarker.empty() || m_MarkerReady;
            if (!ready || !markerAllowsMeasurement || m_ThresholdsMet)
            {
                m_HasPreviousFrameStart = false;
                m_PendingFrameMilliseconds = 0.0;
                return;
            }
            if (m_HasPreviousFrameStart)
            {
                m_PendingFrameMilliseconds =
                    std::chrono::duration<double, std::milli>(
                        now - m_PreviousFrameStart).count();
            }
            else
            {
                m_PendingFrameMilliseconds = 0.0;
                m_HasPreviousFrameStart = true;
            }
            m_PreviousFrameStart = now;
        }

        bool Observe(
            uint64_t arrivalFrameId,
            uint64_t traceFrameId,
            uint64_t compositionFrameId,
            uint64_t effectFrameId,
            double traceMs,
            double compositionMs,
            double effectMs,
            double depthMs,
            double temporalMs,
            double spatialMs,
            const VisibilityPerfCaptureMetadata& metadata)
        {
            if (!Enabled() || m_Complete || m_Failed ||
                m_ThresholdsMet ||
                !(m_PendingFrameMilliseconds > 0.0) ||
                (m_HasLastTimerFrames &&
                    (traceFrameId == m_LastTraceFrame ||
                        compositionFrameId == m_LastCompositionFrame)) ||
                !std::isfinite(traceMs) || !(traceMs > 0.0) ||
                !std::isfinite(compositionMs) || compositionMs < 0.0 ||
                !std::isfinite(effectMs) || !(effectMs > 0.0))
            {
                return false;
            }

            m_HasLastTimerFrames = true;
            m_LastTraceFrame = traceFrameId;
            m_LastCompositionFrame = compositionFrameId;
            m_Metadata = metadata;
            if (m_ObservedFreshFrames++ < m_Options.warmupFrames)
            {
                if (m_ObservedFreshFrames == m_Options.warmupFrames)
                {
                    std::fprintf(
                        stdout,
                        "UVSR_PERF warmup complete (%u frames)\n",
                        m_Options.warmupFrames);
                    std::fflush(stdout);
                }
                return false;
            }

            VisibilityPerfCaptureSample sample;
            sample.sampleIndex = static_cast<uint32_t>(m_Samples.size());
            sample.arrivalFrameId = arrivalFrameId;
            sample.traceFrameId = traceFrameId;
            sample.compositionFrameId = compositionFrameId;
            sample.effectFrameId = effectFrameId;
            sample.traceCompositionAligned =
                traceFrameId == compositionFrameId;
            sample.frameStartToStartMs = m_PendingFrameMilliseconds;
            sample.traceMs = traceMs;
            sample.compositionMs = compositionMs;
            sample.effectMs = effectMs;
            sample.depthMs = depthMs;
            sample.temporalMs = temporalMs;
            sample.spatialMs = spatialMs;
            if (m_Samples.empty())
                m_MeasurementStart = std::chrono::steady_clock::now();
            m_Samples.push_back(sample);

            if ((m_Samples.size() % 120u) == 0u ||
                m_Samples.size() == m_Options.measuredFrames)
            {
                std::fprintf(
                    stdout,
                    "UVSR_PERF measured %zu/%u fresh frames\n",
                    m_Samples.size(),
                    m_Options.measuredFrames);
                std::fflush(stdout);
            }

            const auto now = std::chrono::steady_clock::now();
            m_CollectionElapsedSeconds =
                std::chrono::duration<double>(
                    now - m_MeasurementStart).count();
            if (m_Samples.size() < m_Options.measuredFrames ||
                m_CollectionElapsedSeconds <
                    m_Options.minimumMeasuredSeconds)
            {
                return false;
            }

            m_ThresholdsMet = true;
            if (m_Options.readyMarker.empty() || m_MarkerComplete)
            {
                Finalize();
            }
            else
            {
                std::fprintf(
                    stdout,
                    "UVSR_PERF thresholds met; waiting for marker "
                    "state=complete\n");
                std::fflush(stdout);
            }
            return ReadyToClose();
        }

    private:
        void PollMarker(
            const std::chrono::steady_clock::time_point& now)
        {
            if (m_Options.readyMarker.empty() ||
                (m_HasMarkerPollDeadline && now < m_NextMarkerPoll))
            {
                return;
            }
            m_HasMarkerPollDeadline = true;
            m_NextMarkerPoll = now + std::chrono::milliseconds(100);

            std::ifstream marker(
                m_Options.readyMarker, std::ios::binary);
            if (!marker)
                return;
            std::ostringstream contents;
            contents << marker.rdbuf();
            const std::string state = contents.str();
            if (state.find("state=contaminated") != std::string::npos)
            {
                m_Failed = true;
                std::fprintf(
                    stderr,
                    "UVSR_PERF marker reported state=contaminated\n");
                std::fflush(stderr);
                return;
            }
            if (state.find("state=complete") != std::string::npos)
            {
                m_MarkerReady = true;
                m_MarkerComplete = true;
            }
            else if (state.find("state=ready") != std::string::npos)
            {
                m_MarkerReady = true;
            }
        }

        void Finalize()
        {
            if (m_Complete || m_Failed)
                return;
            if (!WriteResults())
            {
                m_Failed = true;
                std::fprintf(
                    stderr,
                    "UVSR_PERF failed to write capture output\n");
                std::fflush(stderr);
                return;
            }
            m_Complete = true;
        }

        static void WriteDistribution(
            std::ostream& output,
            const char* key,
            const VisibilityPerfDistribution& value,
            bool trailingComma)
        {
            output << "    \"" << key << "\": {"
                << "\"mean\": " << value.mean
                << ", \"median\": " << value.median
                << ", \"p95\": " << value.p95
                << ", \"min\": " << value.minimum
                << ", \"max\": " << value.maximum << "}"
                << (trailingComma ? "," : "") << "\n";
        }

        bool WriteResults()
        {
            std::filesystem::path base = m_Options.outputBase;
            if (base.extension() == ".json" || base.extension() == ".csv")
                base.replace_extension();
            std::filesystem::path jsonPath = base;
            std::filesystem::path csvPath = base;
            jsonPath += ".json";
            csvPath += ".csv";

            std::error_code directoryError;
            if (!jsonPath.parent_path().empty())
            {
                std::filesystem::create_directories(
                    jsonPath.parent_path(), directoryError);
            }
            if (directoryError)
                return false;

            std::ofstream csv(csvPath, std::ios::binary | std::ios::trunc);
            if (!csv)
                return false;
            csv << "sample_index,arrival_frame_id,trace_frame_id,"
                "composition_frame_id,effect_frame_id,"
                "trace_composition_aligned,"
                "frame_start_to_start_ms,instantaneous_fps,trace_ms,"
                "composition_ms,effect_ms,depth_ms,temporal_ms,spatial_ms\n";
            csv << std::fixed << std::setprecision(9);
            for (const VisibilityPerfCaptureSample& sample : m_Samples)
            {
                csv << sample.sampleIndex << ','
                    << sample.arrivalFrameId << ','
                    << sample.traceFrameId << ','
                    << sample.compositionFrameId << ','
                    << sample.effectFrameId << ','
                    << (sample.traceCompositionAligned ? 1 : 0) << ','
                    << sample.frameStartToStartMs << ','
                    << (1000.0 / sample.frameStartToStartMs) << ','
                    << sample.traceMs << ','
                    << sample.compositionMs << ','
                    << sample.effectMs << ','
                    << sample.depthMs << ','
                    << sample.temporalMs << ','
                    << sample.spatialMs << '\n';
            }
            csv.close();
            if (!csv)
                return false;

            std::vector<double> frameValues;
            std::vector<double> traceValues;
            std::vector<double> compositionValues;
            std::vector<double> traceCompositionValues;
            std::vector<double> effectValues;
            size_t alignedTraceCompositionSamples = 0u;
            frameValues.reserve(m_Samples.size());
            traceValues.reserve(m_Samples.size());
            compositionValues.reserve(m_Samples.size());
            traceCompositionValues.reserve(m_Samples.size());
            effectValues.reserve(m_Samples.size());
            for (const VisibilityPerfCaptureSample& sample : m_Samples)
            {
                frameValues.push_back(sample.frameStartToStartMs);
                traceValues.push_back(sample.traceMs);
                compositionValues.push_back(sample.compositionMs);
                traceCompositionValues.push_back(
                    sample.traceMs + sample.compositionMs);
                effectValues.push_back(sample.effectMs);
                if (sample.traceCompositionAligned)
                    ++alignedTraceCompositionSamples;
            }
            const VisibilityPerfDistribution frame =
                SummarizeVisibilityPerfValues(frameValues);
            const VisibilityPerfDistribution trace =
                SummarizeVisibilityPerfValues(traceValues);
            const VisibilityPerfDistribution composition =
                SummarizeVisibilityPerfValues(compositionValues);
            const VisibilityPerfDistribution traceComposition =
                SummarizeVisibilityPerfValues(traceCompositionValues);
            const VisibilityPerfDistribution effect =
                SummarizeVisibilityPerfValues(effectValues);

            std::ofstream json(jsonPath, std::ios::binary | std::ios::trunc);
            if (!json)
                return false;
            json << std::fixed << std::setprecision(9)
                << "{\n"
                << "  \"schema_version\": 1,\n"
                << "  \"build\": \"" <<
                    EscapeVisibilityPerfJson(m_Metadata.build) << "\",\n"
                << "  \"adapter\": \"" <<
                    EscapeVisibilityPerfJson(m_Metadata.adapter) << "\",\n"
                << "  \"live_priority\": \"" <<
                    EscapeVisibilityPerfJson(m_Metadata.livePriority) << "\",\n"
                << "  \"variants\": \"" <<
                    EscapeVisibilityPerfJson(m_Options.variants) << "\",\n"
                << "  \"isolate_visibility\": " <<
                    (m_Options.isolateVisibility ? "true" : "false") << ",\n"
                << "  \"framebuffer_width\": " <<
                    m_Metadata.framebufferWidth << ",\n"
                << "  \"framebuffer_height\": " <<
                    m_Metadata.framebufferHeight << ",\n"
                << "  \"warmup_frames\": " << m_Options.warmupFrames << ",\n"
                << "  \"measured_frames\": " << m_Samples.size() << ",\n"
                << "  \"minimum_measured_seconds\": " <<
                    m_Options.minimumMeasuredSeconds << ",\n"
                << "  \"collection_elapsed_seconds\": " <<
                    m_CollectionElapsedSeconds << ",\n"
                << "  \"trace_composition_aligned_samples\": " <<
                    alignedTraceCompositionSamples << ",\n"
                << "  \"trace_composition_alignment_fraction\": " <<
                    (m_Samples.empty()
                        ? 0.0
                        : double(alignedTraceCompositionSamples) /
                            double(m_Samples.size())) << ",\n"
                << "  \"trace_plus_composition_of_stage_medians_ms\": " <<
                    (trace.median + composition.median) << ",\n"
                << "  \"permutation\": \"" <<
                    EscapeVisibilityPerfJson(m_Metadata.permutation) << "\",\n"
                << "  \"settings\": \"" <<
                    EscapeVisibilityPerfJson(m_Metadata.settings) << "\",\n"
                << "  \"timing_provenance\": \"" <<
                    EscapeVisibilityPerfJson(
                        m_Metadata.timingProvenance) << "\",\n"
                << "  \"resources\": {\n"
                << "    \"output_texture_bytes\": " <<
                    m_Metadata.outputTextureBytes << ",\n"
                << "    \"working_texture_bytes\": " <<
                    m_Metadata.workingTextureBytes << ",\n"
                << "    \"raw_ambient_texture_bytes\": " <<
                    m_Metadata.rawAmbientTextureBytes << ",\n"
                << "    \"raw_indirect_texture_bytes\": " <<
                    m_Metadata.rawIndirectTextureBytes << "\n"
                << "  },\n"
                << "  \"aggregate_fps\": " <<
                    (frame.mean > 0.0 ? 1000.0 / frame.mean : 0.0) << ",\n"
                << "  \"distributions_ms\": {\n";
            WriteDistribution(json, "frame_start_to_start", frame, true);
            WriteDistribution(json, "trace", trace, true);
            WriteDistribution(json, "composition", composition, true);
            WriteDistribution(
                json, "trace_plus_composition", traceComposition, true);
            WriteDistribution(json, "effect", effect, false);
            json << "  },\n"
                << "  \"raw_csv\": \"" <<
                    EscapeVisibilityPerfJson(csvPath.string()) << "\"\n"
                << "}\n";
            json.close();
            if (!json)
                return false;

            std::fprintf(
                stdout,
                "UVSR_PERF complete: %.3f aggregate FPS, "
                "%.6f ms frame median, %.6f ms trace median; %s\n",
                frame.mean > 0.0 ? 1000.0 / frame.mean : 0.0,
                frame.median,
                trace.median,
                jsonPath.string().c_str());
            std::fflush(stdout);
            return true;
        }

        VisibilityPerfCaptureOptions m_Options;
        VisibilityPerfCaptureMetadata m_Metadata;
        std::vector<VisibilityPerfCaptureSample> m_Samples;
        std::chrono::steady_clock::time_point m_PreviousFrameStart{};
        std::chrono::steady_clock::time_point m_MeasurementStart{};
        std::chrono::steady_clock::time_point m_NextMarkerPoll{};
        double m_PendingFrameMilliseconds = 0.0;
        double m_CollectionElapsedSeconds = 0.0;
        uint32_t m_ObservedFreshFrames = 0u;
        uint64_t m_LastTraceFrame = 0u;
        uint64_t m_LastCompositionFrame = 0u;
        bool m_HasPreviousFrameStart = false;
        bool m_HasLastTimerFrames = false;
        bool m_HasMarkerPollDeadline = false;
        bool m_MarkerReady = false;
        bool m_MarkerComplete = false;
        bool m_ThresholdsMet = false;
        bool m_Complete = false;
        bool m_Failed = false;
    };
}
