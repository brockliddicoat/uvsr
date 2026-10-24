#include "adaptive_msaa_validation_capture.h"

#include "adaptive_msaa_texture_qualification.h"

#include <donut/core/json.h>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace uvsr::adaptive_msaa
{
    namespace
    {
        constexpr uint32_t DiagnosticsGroupHistogramValid = 1u << 0u;
        constexpr uint32_t DiagnosticsLightingValid = 1u << 1u;
        constexpr uint32_t DiagnosticsFallbackReasonsValid = 1u << 2u;
        constexpr uint32_t DiagnosticsInvalidKeysValid = 1u << 3u;
        constexpr uint32_t DiagnosticsStaleAndMappingValid = 1u << 4u;
        constexpr uint32_t DiagnosticsWorkListValid = 1u << 5u;
        constexpr uint32_t DiagnosticsAllValidityBits = 0x7fu;
        constexpr size_t MaximumTokenLength = 128u;
        constexpr size_t MaximumSceneNameLength = 512u;

        std::atomic<uint64_t> g_TemporaryCounter{ 1u };

        [[nodiscard]] AdaptiveMsaaValidationCaptureStatus Fail(
            AdaptiveMsaaValidationCaptureReason reason,
            uint32_t systemError = 0u)
        {
            AdaptiveMsaaValidationCaptureStatus status;
            status.reason = reason;
            status.systemError = systemError;
            return status;
        }

        [[nodiscard]] bool IsLowerHex(
            const std::string& value,
            size_t requiredLength)
        {
            if (value.size() != requiredLength)
                return false;
            bool anyNonzero = false;
            for (const char character : value)
            {
                const bool digit = character >= '0' && character <= '9';
                const bool lowerHex = character >= 'a' && character <= 'f';
                if (!digit && !lowerHex)
                    return false;
                anyNonzero |= character != '0';
            }
            return anyNonzero;
        }

        [[nodiscard]] bool IsCanonicalToken(const std::string& value)
        {
            if (value.empty() || value.size() > MaximumTokenLength)
                return false;
            const auto IsAlphaNumeric = [](char character)
            {
                return (character >= 'a' && character <= 'z') ||
                    (character >= '0' && character <= '9');
            };
            if (!IsAlphaNumeric(value.front()) ||
                !IsAlphaNumeric(value.back()))
            {
                return false;
            }
            return std::all_of(
                value.begin(),
                value.end(),
                [&](char character)
                {
                    return IsAlphaNumeric(character) || character == '-' ||
                        character == '_' || character == '.';
                });
        }

        [[nodiscard]] bool HasCanonicalText(
            const std::string& value,
            size_t maximumLength)
        {
            if (value.empty() || value.size() > maximumLength ||
                value.front() == ' ' || value.back() == ' ')
            {
                return false;
            }
            return std::all_of(
                value.begin(),
                value.end(),
                [](unsigned char character)
                {
                    return character >= 0x20u && character != 0x7fu;
                });
        }

        [[nodiscard]] bool IsImplementation(const std::string& value)
        {
            return value == "reference" ||
                value == "adaptive-g-buffer" ||
                value == "compact-visibility";
        }

        [[nodiscard]] bool IsSampleCount(uint32_t value)
        {
            return value == 2u || value == 4u || value == 8u || value == 16u;
        }

        [[nodiscard]] bool AnyHistogramValue(
            const AdaptiveMsaaValidationReceipt& receipt)
        {
            return std::any_of(
                receipt.groupHistogram.begin(),
                receipt.groupHistogram.end(),
                [](uint64_t value) { return value != 0u; });
        }

        [[nodiscard]] bool CompactCountPairValid(
            uint64_t drawCount,
            uint64_t triangleCount)
        {
            return (drawCount == 0u) == (triangleCount == 0u) &&
                triangleCount >= drawCount;
        }

        [[nodiscard]] bool AllDiagnosticPayloadZero(
            const AdaptiveMsaaValidationReceipt& receipt)
        {
            return !AnyHistogramValue(receipt) &&
                receipt.referenceLightingEvaluations == 0u &&
                receipt.performedLightingEvaluations == 0u &&
                receipt.lightingEvaluationsSaved == 0u &&
                std::all_of(
                    receipt.fallbackReasonCounts.begin(),
                    receipt.fallbackReasonCounts.end(),
                    [](uint64_t value) { return value == 0u; }) &&
                receipt.invalidKeyCount == 0u &&
                receipt.staleKeyCount == 0u &&
                receipt.mappingFailureCount == 0u &&
                std::all_of(
                    receipt.errorBudgetRejectionReasonCounts.begin(),
                    receipt.errorBudgetRejectionReasonCounts.end(),
                    [](uint64_t value) { return value == 0u; }) &&
                receipt.errorBudgetRejectedGroupCount == 0u &&
                receipt.errorBudgetRejectedSampleCount == 0u &&
                receipt.workListObservationCount == 0u &&
                receipt.workListCapacityTotal == 0u &&
                receipt.workListOccupancyTotal == 0u &&
                receipt.workListPeakOccupancy == 0u &&
                receipt.workListOverflowCount == 0u;
        }

        [[nodiscard]] std::string LowerHex(
            const TextureQualificationSha256& digest)
        {
            static constexpr char Digits[] = "0123456789abcdef";
            std::string result;
            result.resize(digest.size() * 2u);
            for (size_t index = 0u; index < digest.size(); ++index)
            {
                result[index * 2u] = Digits[digest[index] >> 4u];
                result[index * 2u + 1u] = Digits[digest[index] & 0x0fu];
            }
            return result;
        }

        [[nodiscard]] bool HasBmpExtension(
            const std::filesystem::path& path)
        {
            std::wstring extension = path.extension().wstring();
            std::transform(
                extension.begin(),
                extension.end(),
                extension.begin(),
                [](wchar_t value)
                {
                    return value >= L'A' && value <= L'Z'
                        ? wchar_t(value - L'A' + L'a')
                        : value;
                });
            return extension == L".bmp";
        }

        [[nodiscard]] AdaptiveMsaaValidationCaptureStatus ReadAndHashBmp(
            const std::filesystem::path& path,
            std::string& digest)
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                return Fail(AdaptiveMsaaValidationCaptureReason::ImageReadFailed);
            const std::streamoff end = input.tellg();
            if (end < 2 ||
                static_cast<uint64_t>(end) >
                    static_cast<uint64_t>(
                        std::numeric_limits<size_t>::max()))
            {
                return Fail(AdaptiveMsaaValidationCaptureReason::ImageReadFailed);
            }

            std::vector<uint8_t> bytes;
            try
            {
                bytes.resize(static_cast<size_t>(end));
            }
            catch (...)
            {
                return Fail(AdaptiveMsaaValidationCaptureReason::ImageReadFailed);
            }
            input.seekg(0, std::ios::beg);
            input.read(
                reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input || bytes[0] != uint8_t('B') ||
                bytes[1] != uint8_t('M'))
            {
                return Fail(AdaptiveMsaaValidationCaptureReason::ImageReadFailed);
            }

            TextureQualificationSha256 hash{};
            if (ComputeTextureQualificationSha256(
                    bytes.data(), bytes.size(), hash) !=
                TextureQualificationReason::None)
            {
                return Fail(AdaptiveMsaaValidationCaptureReason::ImageHashFailed);
            }
            digest = LowerHex(hash);
            return {};
        }

        [[nodiscard]] Json::Value UInt64Value(uint64_t value)
        {
            return Json::Value(static_cast<Json::UInt64>(value));
        }

        [[nodiscard]] Json::Value MakeJson(
            const AdaptiveMsaaValidationReceipt& receipt)
        {
            Json::Value root(Json::objectValue);
            root["schema"] = receipt.schema;
            root["kind"] = receipt.kind;
            root["captureId"] = receipt.captureId;
            root["suiteId"] = receipt.suiteId;
            root["case"] = receipt.caseId;
            root["recipeSha256"] = receipt.recipeSha256;
            root["phaseIndex"] = receipt.phaseIndex;
            root["phase"] = receipt.phase;
            root["imagePath"] = receipt.imagePath;
            root["imageSha256"] = receipt.imageSha256;
            root["sourceCommit"] = receipt.sourceCommit;
            root["sourceDiffSha256"] = receipt.sourceDiffSha256;
            root["rendererBuildManifestSha256"] =
                receipt.rendererBuildManifestSha256;
            root["rendererExecutableSha256"] =
                receipt.rendererExecutableSha256;
            root["sceneName"] = receipt.sceneName;
            root["sceneDescriptorSha256"] = receipt.sceneDescriptorSha256;
            root["sceneGltfSha256"] = receipt.sceneGltfSha256;
            root["cameraSha256"] = receipt.cameraSha256;
            root["settingsSha256"] = receipt.settingsSha256;
            root["width"] = receipt.width;
            root["height"] = receipt.height;

            Json::Value viewport(Json::objectValue);
            viewport["minX"] = receipt.viewport.minX;
            viewport["minY"] = receipt.viewport.minY;
            viewport["maxX"] = receipt.viewport.maxX;
            viewport["maxY"] = receipt.viewport.maxY;
            root["viewport"] = std::move(viewport);
            root["frozenSeed"] = UInt64Value(receipt.frozenSeed);
            root["topologyEpoch"] = UInt64Value(receipt.topologyEpoch);
            root["topologyRevision"] = receipt.topologyRevision;
            root["lodRevision"] = receipt.lodRevision;
            root["deformationRevision"] = receipt.deformationRevision;
            root["materialRevision"] = receipt.materialRevision;
            root["requestedImplementation"] = receipt.requestedImplementation;
            root["resolvedImplementation"] = receipt.resolvedImplementation;
            root["appliedImplementation"] = receipt.appliedImplementation;
            root["requestedSampleCount"] = receipt.requestedSampleCount;
            root["resolvedSampleCount"] = receipt.resolvedSampleCount;
            root["appliedSampleCount"] = receipt.appliedSampleCount;
            root["selectedPathSucceeded"] = receipt.selectedPathSucceeded;
            root["submittedMainViewTriangles"] =
                UInt64Value(receipt.submittedMainViewTriangles);
            root["diagnosticsProducerProducedThisFrame"] =
                receipt.diagnosticsProducerProducedThisFrame;
            root["diagnosticsReadbackToken"] =
                UInt64Value(receipt.diagnosticsReadbackToken);
            root["diagnosticsCompletedToken"] =
                UInt64Value(receipt.diagnosticsCompletedToken);
            root["diagnosticsDispatchComplete"] =
                receipt.diagnosticsDispatchComplete;
            root["diagnosticsValidityMask"] = receipt.diagnosticsValidityMask;

            Json::Value histogram(Json::arrayValue);
            for (const uint64_t value : receipt.groupHistogram)
                histogram.append(UInt64Value(value));
            root["groupHistogram"] = std::move(histogram);
            root["referenceLightingEvaluations"] =
                UInt64Value(receipt.referenceLightingEvaluations);
            root["performedLightingEvaluations"] =
                UInt64Value(receipt.performedLightingEvaluations);
            root["lightingEvaluationsSaved"] =
                UInt64Value(receipt.lightingEvaluationsSaved);
            Json::Value fallbackReasons(Json::arrayValue);
            for (const uint64_t value : receipt.fallbackReasonCounts)
                fallbackReasons.append(UInt64Value(value));
            root["fallbackReasonCounts"] = std::move(fallbackReasons);
            root["invalidKeyCount"] = UInt64Value(receipt.invalidKeyCount);
            root["staleKeyCount"] = UInt64Value(receipt.staleKeyCount);
            root["mappingFailureCount"] =
                UInt64Value(receipt.mappingFailureCount);
            Json::Value errorBudgetReasons(Json::arrayValue);
            for (const uint64_t value :
                receipt.errorBudgetRejectionReasonCounts)
            {
                errorBudgetReasons.append(UInt64Value(value));
            }
            root["errorBudgetRejectionReasonCounts"] =
                std::move(errorBudgetReasons);
            root["errorBudgetRejectedGroupCount"] =
                UInt64Value(receipt.errorBudgetRejectedGroupCount);
            root["errorBudgetRejectedSampleCount"] =
                UInt64Value(receipt.errorBudgetRejectedSampleCount);
            root["compactShadeSucceeded"] = receipt.compactShadeSucceeded;
            root["compactShadeDispatchedViewCount"] =
                receipt.compactShadeDispatchedViewCount;
            root["compactReconstructDrawCount"] =
                UInt64Value(receipt.compactReconstructDrawCount);
            root["compactReconstructTriangleCount"] =
                UInt64Value(receipt.compactReconstructTriangleCount);
            root["compactFallbackDrawCount"] =
                UInt64Value(receipt.compactFallbackDrawCount);
            root["compactFallbackTriangleCount"] =
                UInt64Value(receipt.compactFallbackTriangleCount);
            root["preparationTopology"] = receipt.preparationTopology;
            root["workListObservationCount"] =
                UInt64Value(receipt.workListObservationCount);
            root["workListCapacityTotal"] =
                UInt64Value(receipt.workListCapacityTotal);
            root["workListOccupancyTotal"] =
                UInt64Value(receipt.workListOccupancyTotal);
            root["workListPeakOccupancy"] =
                UInt64Value(receipt.workListPeakOccupancy);
            root["workListOverflowCount"] =
                UInt64Value(receipt.workListOverflowCount);

            Json::Value roi(Json::objectValue);
            roi["x"] = receipt.roi.x;
            roi["y"] = receipt.roi.y;
            roi["width"] = receipt.roi.width;
            roi["height"] = receipt.roi.height;
            root["roi"] = std::move(roi);
            return root;
        }

        [[nodiscard]] AdaptiveMsaaValidationCaptureStatus Serialize(
            const AdaptiveMsaaValidationReceipt& receipt,
            std::string& serialized)
        {
            try
            {
                Json::StreamWriterBuilder builder;
                builder["commentStyle"] = "None";
                builder["indentation"] = "";
                builder["enableYAMLCompatibility"] = false;
                builder["dropNullPlaceholders"] = false;
                builder["emitUTF8"] = true;
                serialized = Json::writeString(builder, MakeJson(receipt));
                serialized.push_back('\n');
            }
            catch (...)
            {
                serialized.clear();
                return Fail(
                    AdaptiveMsaaValidationCaptureReason::SerializationFailed);
            }
            return {};
        }

        [[nodiscard]] AdaptiveMsaaValidationCaptureStatus AtomicWrite(
            const std::filesystem::path& destination,
            const std::string& bytes)
        {
            const uint64_t counter = g_TemporaryCounter.fetch_add(
                1u, std::memory_order_relaxed);
            if (counter == 0u)
            {
                return Fail(
                    AdaptiveMsaaValidationCaptureReason::
                        TemporaryNameExhausted);
            }

            std::filesystem::path temporary;
            try
            {
                temporary = destination;
                temporary += L".tmp.";
                temporary += std::to_wstring(GetCurrentProcessId());
                temporary += L".";
                temporary += std::to_wstring(counter);
            }
            catch (...)
            {
                return Fail(
                    AdaptiveMsaaValidationCaptureReason::
                        TemporaryNameExhausted);
            }

            HANDLE file = CreateFileW(
                temporary.c_str(),
                GENERIC_WRITE,
                0u,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
                nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                return Fail(
                    AdaptiveMsaaValidationCaptureReason::AtomicCreateFailed,
                    GetLastError());
            }

            AdaptiveMsaaValidationCaptureStatus status;
            size_t offset = 0u;
            while (offset < bytes.size())
            {
                const size_t remaining = bytes.size() - offset;
                const DWORD request = static_cast<DWORD>(std::min<size_t>(
                    remaining,
                    std::numeric_limits<DWORD>::max()));
                DWORD written = 0u;
                if (!WriteFile(
                        file,
                        bytes.data() + offset,
                        request,
                        &written,
                        nullptr) ||
                    written == 0u)
                {
                    status = Fail(
                        AdaptiveMsaaValidationCaptureReason::AtomicWriteFailed,
                        GetLastError());
                    break;
                }
                offset += written;
            }
            if (status.Succeeded() && !FlushFileBuffers(file))
            {
                status = Fail(
                    AdaptiveMsaaValidationCaptureReason::AtomicFlushFailed,
                    GetLastError());
            }
            if (!CloseHandle(file) && status.Succeeded())
            {
                status = Fail(
                    AdaptiveMsaaValidationCaptureReason::AtomicFlushFailed,
                    GetLastError());
            }
            if (status.Succeeded() && !MoveFileExW(
                    temporary.c_str(),
                    destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                status = Fail(
                    AdaptiveMsaaValidationCaptureReason::AtomicReplaceFailed,
                    GetLastError());
            }
            if (!status.Succeeded())
                DeleteFileW(temporary.c_str());
            else
                status.byteCount = bytes.size();
            return status;
        }
    }

    const char* GetAdaptiveMsaaValidationCaptureReasonName(
        AdaptiveMsaaValidationCaptureReason reason) noexcept
    {
#define UVSR_CAPTURE_REASON_NAME(name) \
    case AdaptiveMsaaValidationCaptureReason::name: return #name
        switch (reason)
        {
        UVSR_CAPTURE_REASON_NAME(None);
        UVSR_CAPTURE_REASON_NAME(PathMissing);
        UVSR_CAPTURE_REASON_NAME(BmpPathRequired);
        UVSR_CAPTURE_REASON_NAME(ImageMissing);
        UVSR_CAPTURE_REASON_NAME(ImageNotRegularFile);
        UVSR_CAPTURE_REASON_NAME(ImageReadFailed);
        UVSR_CAPTURE_REASON_NAME(ImageHashFailed);
        UVSR_CAPTURE_REASON_NAME(InvalidSchema);
        UVSR_CAPTURE_REASON_NAME(InvalidKind);
        UVSR_CAPTURE_REASON_NAME(InvalidToken);
        UVSR_CAPTURE_REASON_NAME(InvalidHash);
        UVSR_CAPTURE_REASON_NAME(InvalidDimensions);
        UVSR_CAPTURE_REASON_NAME(InvalidViewport);
        UVSR_CAPTURE_REASON_NAME(InvalidRoi);
        UVSR_CAPTURE_REASON_NAME(InvalidRevision);
        UVSR_CAPTURE_REASON_NAME(InvalidImplementation);
        UVSR_CAPTURE_REASON_NAME(InvalidSampleCount);
        UVSR_CAPTURE_REASON_NAME(InvalidDiagnostics);
        UVSR_CAPTURE_REASON_NAME(InvalidLightingWork);
        UVSR_CAPTURE_REASON_NAME(InvalidCompactWork);
        UVSR_CAPTURE_REASON_NAME(InvalidPreparationTopology);
        UVSR_CAPTURE_REASON_NAME(InvalidWorkList);
        UVSR_CAPTURE_REASON_NAME(SerializationFailed);
        UVSR_CAPTURE_REASON_NAME(TemporaryNameExhausted);
        UVSR_CAPTURE_REASON_NAME(AtomicCreateFailed);
        UVSR_CAPTURE_REASON_NAME(AtomicWriteFailed);
        UVSR_CAPTURE_REASON_NAME(AtomicFlushFailed);
        UVSR_CAPTURE_REASON_NAME(AtomicReplaceFailed);
        default:
            return "Unknown";
        }
#undef UVSR_CAPTURE_REASON_NAME
    }

    std::filesystem::path DeriveAdaptiveMsaaValidationReceiptPath(
        const std::filesystem::path& bmpPath)
    {
        if (bmpPath.empty())
            return {};
        std::filesystem::path result = bmpPath;
        result += L".adaptive-msaa.json";
        return result;
    }

    AdaptiveMsaaValidationCaptureStatus
        ValidateAdaptiveMsaaValidationReceipt(
            const AdaptiveMsaaValidationReceipt& receipt) noexcept
    {
        if (receipt.schema != AdaptiveMsaaValidationReceiptSchema)
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidSchema);
        if (receipt.kind != AdaptiveMsaaValidationReceiptKind)
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidKind);
        if (!IsCanonicalToken(receipt.captureId) ||
            !IsCanonicalToken(receipt.suiteId) ||
            !IsCanonicalToken(receipt.caseId) ||
            !HasCanonicalText(receipt.sceneName, MaximumSceneNameLength))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidToken);
        }
        if (receipt.phaseIndex > 1u || !std::isfinite(receipt.phase) ||
            receipt.phase < 0.0 || receipt.phase > 1.0)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidToken);
        }
        if (!IsLowerHex(receipt.recipeSha256, 64u) ||
            !IsLowerHex(receipt.imageSha256, 64u) ||
            !IsLowerHex(receipt.sourceCommit, 40u) ||
            !IsLowerHex(receipt.sourceDiffSha256, 64u) ||
            !IsLowerHex(receipt.rendererBuildManifestSha256, 64u) ||
            !IsLowerHex(receipt.rendererExecutableSha256, 64u) ||
            !IsLowerHex(receipt.sceneDescriptorSha256, 64u) ||
            !IsLowerHex(receipt.sceneGltfSha256, 64u) ||
            !IsLowerHex(receipt.cameraSha256, 64u) ||
            !IsLowerHex(receipt.settingsSha256, 64u))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidHash);
        }
        try
        {
            const std::filesystem::path image =
                std::filesystem::u8path(receipt.imagePath);
            if (receipt.imagePath.empty() || !image.is_absolute() ||
                image.lexically_normal().generic_u8string() !=
                    receipt.imagePath)
            {
                return Fail(AdaptiveMsaaValidationCaptureReason::PathMissing);
            }
        }
        catch (...)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::PathMissing);
        }
        if (receipt.width == 0u || receipt.height == 0u)
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidDimensions);
        if (receipt.viewport.minX >= receipt.viewport.maxX ||
            receipt.viewport.minY >= receipt.viewport.maxY ||
            receipt.viewport.maxX > receipt.width ||
            receipt.viewport.maxY > receipt.height)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidViewport);
        }
        const uint64_t roiMaxX = uint64_t(receipt.roi.x) + receipt.roi.width;
        const uint64_t roiMaxY = uint64_t(receipt.roi.y) + receipt.roi.height;
        if (receipt.roi.width == 0u || receipt.roi.height == 0u ||
            receipt.roi.x < receipt.viewport.minX ||
            receipt.roi.y < receipt.viewport.minY ||
            roiMaxX > receipt.viewport.maxX ||
            roiMaxY > receipt.viewport.maxY)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidRoi);
        }
        if (receipt.topologyEpoch == 0u ||
            receipt.topologyRevision == 0u || receipt.lodRevision == 0u ||
            receipt.deformationRevision == 0u ||
            receipt.materialRevision == 0u)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidRevision);
        }
        if (!IsImplementation(receipt.requestedImplementation) ||
            !IsImplementation(receipt.resolvedImplementation) ||
            !IsImplementation(receipt.appliedImplementation))
        {
            return Fail(
                AdaptiveMsaaValidationCaptureReason::InvalidImplementation);
        }
        if (!IsSampleCount(receipt.requestedSampleCount) ||
            !IsSampleCount(receipt.resolvedSampleCount) ||
            !IsSampleCount(receipt.appliedSampleCount) ||
            receipt.resolvedSampleCount > receipt.requestedSampleCount ||
            receipt.appliedSampleCount != receipt.resolvedSampleCount)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidSampleCount);
        }

        if ((receipt.diagnosticsValidityMask &
                ~DiagnosticsAllValidityBits) != 0u)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        }
        if (!receipt.diagnosticsProducerProducedThisFrame)
        {
            if (receipt.diagnosticsReadbackToken != 0u ||
                receipt.diagnosticsCompletedToken != 0u ||
                receipt.diagnosticsDispatchComplete ||
                receipt.diagnosticsValidityMask != 0u ||
                !AllDiagnosticPayloadZero(receipt))
            {
                return Fail(
                    AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
            }
        }
        else if (receipt.diagnosticsReadbackToken == 0u ||
            receipt.diagnosticsCompletedToken >
                receipt.diagnosticsReadbackToken ||
            receipt.diagnosticsDispatchComplete !=
                (receipt.diagnosticsCompletedToken ==
                    receipt.diagnosticsReadbackToken))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        }
        if ((receipt.diagnosticsValidityMask &
                DiagnosticsGroupHistogramValid) == 0u &&
            AnyHistogramValue(receipt))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        }
        if ((receipt.diagnosticsValidityMask & DiagnosticsLightingValid) == 0u &&
            (receipt.referenceLightingEvaluations != 0u ||
                receipt.performedLightingEvaluations != 0u ||
            receipt.lightingEvaluationsSaved != 0u))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        }
        uint64_t fallbackReasonTotal = 0u;
        for (const uint64_t value : receipt.fallbackReasonCounts)
        {
            if (std::numeric_limits<uint64_t>::max() -
                    fallbackReasonTotal < value)
            {
                return Fail(
                    AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
            }
            fallbackReasonTotal += value;
        }
        const bool fallbackReasonsValid =
            (receipt.diagnosticsValidityMask &
                DiagnosticsFallbackReasonsValid) != 0u;
        if ((!fallbackReasonsValid &&
                (receipt.diagnosticsProducerProducedThisFrame ||
                    fallbackReasonTotal != 0u)) ||
            (fallbackReasonsValid &&
                (receipt.fallbackReasonCounts[0u] != 0u ||
                    fallbackReasonTotal >
                        receipt.referenceLightingEvaluations)))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        }
        if ((receipt.diagnosticsValidityMask &
                DiagnosticsInvalidKeysValid) == 0u &&
            receipt.invalidKeyCount != 0u)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        }
        if ((receipt.diagnosticsValidityMask &
                DiagnosticsStaleAndMappingValid) == 0u &&
            (receipt.staleKeyCount != 0u ||
                receipt.mappingFailureCount != 0u))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        }
        if (fallbackReasonsValid)
        {
            const uint64_t expectedInvalidKeys =
                receipt.fallbackReasonCounts[1u] +
                receipt.fallbackReasonCounts[2u] +
                receipt.fallbackReasonCounts[3u] +
                receipt.fallbackReasonCounts[10u];
            const uint64_t expectedStaleKeys =
                receipt.fallbackReasonCounts[4u] +
                receipt.fallbackReasonCounts[5u] +
                receipt.fallbackReasonCounts[6u] +
                receipt.fallbackReasonCounts[7u] +
                receipt.fallbackReasonCounts[8u] +
                receipt.fallbackReasonCounts[11u] +
                receipt.fallbackReasonCounts[12u] +
                receipt.fallbackReasonCounts[22u] +
                receipt.fallbackReasonCounts[23u];
            const uint64_t expectedMappingFailures =
                receipt.fallbackReasonCounts[13u] +
                receipt.fallbackReasonCounts[14u];
            if (((receipt.diagnosticsValidityMask &
                        DiagnosticsInvalidKeysValid) != 0u &&
                    receipt.invalidKeyCount != expectedInvalidKeys) ||
                ((receipt.diagnosticsValidityMask &
                        DiagnosticsStaleAndMappingValid) != 0u &&
                    (receipt.staleKeyCount != expectedStaleKeys ||
                        receipt.mappingFailureCount !=
                            expectedMappingFailures)))
            {
                return Fail(
                    AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
            }
        }
        const bool errorBudgetValid =
            (receipt.diagnosticsValidityMask & (1u << 6u)) != 0u;
        uint64_t rejectedReasonTotal = 0u;
        for (size_t index = 1u;
            index < receipt.errorBudgetRejectionReasonCounts.size();
            ++index)
        {
            if (std::numeric_limits<uint64_t>::max() -
                    rejectedReasonTotal <
                receipt.errorBudgetRejectionReasonCounts[index])
            {
                return Fail(
                    AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
            }
            rejectedReasonTotal +=
                receipt.errorBudgetRejectionReasonCounts[index];
        }
        if ((!errorBudgetValid &&
                (receipt.errorBudgetRejectionReasonCounts[0u] != 0u ||
                    rejectedReasonTotal != 0u ||
                    receipt.errorBudgetRejectedGroupCount != 0u ||
                    receipt.errorBudgetRejectedSampleCount != 0u)) ||
            (errorBudgetValid &&
                (receipt.errorBudgetRejectionReasonCounts[0u] != 0u ||
                    rejectedReasonTotal !=
                        receipt.errorBudgetRejectedGroupCount ||
                    receipt.errorBudgetRejectedSampleCount <
                        receipt.errorBudgetRejectedGroupCount ||
                    receipt.errorBudgetRejectedSampleCount >
                        receipt.referenceLightingEvaluations)))
        {
            return Fail(
                AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        }
        if (receipt.performedLightingEvaluations >
                receipt.referenceLightingEvaluations ||
            receipt.lightingEvaluationsSaved >
                receipt.referenceLightingEvaluations ||
            receipt.performedLightingEvaluations !=
                receipt.referenceLightingEvaluations -
                    receipt.lightingEvaluationsSaved)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidLightingWork);
        }

        if (!CompactCountPairValid(
                receipt.compactReconstructDrawCount,
                receipt.compactReconstructTriangleCount) ||
            !CompactCountPairValid(
                receipt.compactFallbackDrawCount,
                receipt.compactFallbackTriangleCount) ||
            (receipt.compactShadeSucceeded &&
                receipt.compactShadeDispatchedViewCount == 0u) ||
            (!receipt.compactShadeSucceeded &&
                receipt.compactShadeDispatchedViewCount != 0u))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidCompactWork);
        }
        if (receipt.appliedImplementation != "compact-visibility" &&
            (receipt.compactShadeSucceeded ||
                receipt.compactShadeDispatchedViewCount != 0u ||
                receipt.compactReconstructDrawCount != 0u ||
                receipt.compactReconstructTriangleCount != 0u ||
                receipt.compactFallbackDrawCount != 0u ||
                receipt.compactFallbackTriangleCount != 0u))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidCompactWork);
        }

        if (receipt.preparationTopology != "inline" &&
            receipt.preparationTopology != "separated")
        {
            return Fail(
                AdaptiveMsaaValidationCaptureReason::
                    InvalidPreparationTopology);
        }
        const bool anyWorkListValue =
            receipt.workListObservationCount != 0u ||
            receipt.workListCapacityTotal != 0u ||
            receipt.workListOccupancyTotal != 0u ||
            receipt.workListPeakOccupancy != 0u ||
            receipt.workListOverflowCount != 0u;
        if ((receipt.diagnosticsValidityMask & DiagnosticsWorkListValid) == 0u &&
            anyWorkListValue)
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        }
        if ((receipt.workListObservationCount == 0u) != !anyWorkListValue ||
            receipt.workListOverflowCount >
                receipt.workListObservationCount ||
            receipt.workListPeakOccupancy >
                receipt.workListOccupancyTotal ||
            (receipt.workListOverflowCount == 0u &&
                receipt.workListOccupancyTotal >
                    receipt.workListCapacityTotal) ||
            (receipt.preparationTopology == "inline" &&
                (anyWorkListValue ||
                    (receipt.diagnosticsValidityMask &
                        DiagnosticsWorkListValid) != 0u)))
        {
            return Fail(AdaptiveMsaaValidationCaptureReason::InvalidWorkList);
        }
        return {};
    }

    AdaptiveMsaaValidationCaptureWriteResult
        WriteAdaptiveMsaaValidationReceipt(
            const std::filesystem::path& bmpPath,
            const AdaptiveMsaaValidationReceipt& receipt)
    {
        AdaptiveMsaaValidationCaptureWriteResult result;
        if (bmpPath.empty())
        {
            result.status = Fail(
                AdaptiveMsaaValidationCaptureReason::PathMissing);
            return result;
        }
        if (!HasBmpExtension(bmpPath))
        {
            result.status = Fail(
                AdaptiveMsaaValidationCaptureReason::BmpPathRequired);
            return result;
        }

        std::error_code error;
        if (!std::filesystem::exists(bmpPath, error) || error)
        {
            result.status = Fail(
                AdaptiveMsaaValidationCaptureReason::ImageMissing,
                error.value());
            return result;
        }
        if (!std::filesystem::is_regular_file(bmpPath, error) || error)
        {
            result.status = Fail(
                AdaptiveMsaaValidationCaptureReason::ImageNotRegularFile,
                error.value());
            return result;
        }
        const std::filesystem::path canonicalImage =
            std::filesystem::canonical(bmpPath, error);
        if (error || canonicalImage.empty())
        {
            result.status = Fail(
                AdaptiveMsaaValidationCaptureReason::ImageMissing,
                error.value());
            return result;
        }

        AdaptiveMsaaValidationReceipt finalized = receipt;
        try
        {
            finalized.imagePath = canonicalImage.generic_u8string();
        }
        catch (...)
        {
            result.status = Fail(
                AdaptiveMsaaValidationCaptureReason::PathMissing);
            return result;
        }
        result.status = ReadAndHashBmp(
            canonicalImage,
            finalized.imageSha256);
        if (!result.status.Succeeded())
            return result;
        result.status = ValidateAdaptiveMsaaValidationReceipt(finalized);
        if (!result.status.Succeeded())
            return result;

        std::string serialized;
        result.status = Serialize(finalized, serialized);
        if (!result.status.Succeeded())
            return result;
        result.receiptPath =
            DeriveAdaptiveMsaaValidationReceiptPath(canonicalImage);
        result.canonicalImagePath = finalized.imagePath;
        result.imageSha256 = finalized.imageSha256;
        result.status = AtomicWrite(result.receiptPath, serialized);
        if (!result.status.Succeeded())
        {
            result.receiptPath.clear();
            result.canonicalImagePath.clear();
            result.imageSha256.clear();
        }
        return result;
    }
}
