#include "adaptive_msaa_validation_plan.h"

#include <donut/core/json.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace
{
    using namespace uvsr::adaptive_msaa;

    constexpr uint64_t MaximumRecipeByteCount = 1024u * 1024u;
    constexpr std::array<double, AdaptiveMsaaValidationPhaseCount>
        ExpectedPhases{{ 0.25, 0.5 }};
    constexpr AdaptiveMsaaValidationPlanCamera ExpectedCamera{
        { 0.0, 0.2, 8.0 },
        { 0.0, 0.0, -1.0 },
        { 0.0, 1.0, 0.0 },
        42.0
    };
    constexpr std::array<AdaptiveMsaaValidationImplementation, 2u>
        CandidateImplementations{{
            AdaptiveMsaaValidationImplementation::AdaptiveGBuffer,
            AdaptiveMsaaValidationImplementation::CompactVisibility
        }};

    [[nodiscard]] AdaptiveMsaaValidationPlanStatus Success() noexcept
    {
        AdaptiveMsaaValidationPlanStatus status;
        status.reason = AdaptiveMsaaValidationPlanReason::None;
        return status;
    }

    [[nodiscard]] AdaptiveMsaaValidationPlanStatus Fail(
        AdaptiveMsaaValidationPlanReason reason,
        size_t itemIndex = 0u,
        uint32_t systemError = 0u) noexcept
    {
        AdaptiveMsaaValidationPlanStatus status;
        status.reason = reason;
        status.itemIndex = itemIndex;
        status.systemError = systemError;
        return status;
    }

    [[nodiscard]] bool IsLowerHexSha256(std::string_view value) noexcept
    {
        if (value.size() != 64u)
            return false;
        bool anyNonzero = false;
        for (char character : value)
        {
            const bool digit = character >= '0' && character <= '9';
            const bool lower = character >= 'a' && character <= 'f';
            if (!digit && !lower)
                return false;
            anyNonzero |= character != '0';
        }
        return anyNonzero;
    }

    [[nodiscard]] bool IsSafeSlug(std::string_view value) noexcept
    {
        if (value.empty() || value.size() > 96u || value.front() == '-' ||
            value.back() == '-')
        {
            return false;
        }
        bool previousHyphen = false;
        for (char character : value)
        {
            const bool lower = character >= 'a' && character <= 'z';
            const bool digit = character >= '0' && character <= '9';
            if (!lower && !digit && character != '-')
                return false;
            if (character == '-' && previousHyphen)
                return false;
            previousHyphen = character == '-';
        }
        return value.find("..") == std::string_view::npos;
    }

    [[nodiscard]] bool IsCanonicalText(std::string_view value) noexcept
    {
        if (value.empty() || value.size() > 256u || value.front() == ' ' ||
            value.back() == ' ')
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

    [[nodiscard]] bool IsSupportedSampleCount(uint32_t sampleCount) noexcept
    {
        return std::find(
                   AdaptiveMsaaValidationSampleCounts.begin(),
                   AdaptiveMsaaValidationSampleCounts.end(),
                   sampleCount) != AdaptiveMsaaValidationSampleCounts.end();
    }

    [[nodiscard]] bool IsCandidateImplementation(
        AdaptiveMsaaValidationImplementation implementation) noexcept
    {
        return implementation ==
                AdaptiveMsaaValidationImplementation::AdaptiveGBuffer ||
            implementation ==
                AdaptiveMsaaValidationImplementation::CompactVisibility;
    }

    [[nodiscard]] bool SameCamera(
        const AdaptiveMsaaValidationPlanCamera& left,
        const AdaptiveMsaaValidationPlanCamera& right) noexcept
    {
        return left.position == right.position &&
            left.direction == right.direction && left.up == right.up &&
            left.verticalFovDegrees == right.verticalFovDegrees;
    }

    [[nodiscard]] bool IsSafeRelativePath(
        const std::filesystem::path& path) noexcept
    {
        try
        {
            if (path.empty() || path.is_absolute() || path.has_root_name() ||
                path.has_root_directory() || path != path.lexically_normal())
            {
                return false;
            }
            for (const std::filesystem::path& component : path)
            {
                if (component.empty() || component == "." ||
                    component == "..")
                {
                    return false;
                }
            }
            const std::string generic = path.generic_string();
            return !generic.empty() && generic.front() != '/' &&
                generic.find('\\') == std::string::npos &&
                generic.find(':') == std::string::npos;
        }
        catch (...)
        {
            return false;
        }
    }

    [[nodiscard]] bool HasExactMembers(
        const Json::Value& value,
        std::initializer_list<const char*> expected)
    {
        if (!value.isObject() || value.size() != expected.size())
            return false;
        for (const char* name : expected)
        {
            if (!value.isMember(name))
                return false;
        }
        return true;
    }

    [[nodiscard]] AdaptiveMsaaValidationPlanStatus ComputeSha256(
        const std::vector<uint8_t>& bytes,
        std::string& output)
    {
        BCRYPT_ALG_HANDLE provider = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        std::vector<uint8_t> state;
        std::array<uint8_t, 32u> digest{};

        auto Close = [&]()
        {
            if (hash)
                BCryptDestroyHash(hash);
            if (provider)
                BCryptCloseAlgorithmProvider(provider, 0u);
        };

        NTSTATUS nativeStatus = BCryptOpenAlgorithmProvider(
            &provider,
            BCRYPT_SHA256_ALGORITHM,
            nullptr,
            0u);
        if (nativeStatus < 0)
        {
            Close();
            return Fail(
                AdaptiveMsaaValidationPlanReason::HashProviderUnavailable,
                0u,
                static_cast<uint32_t>(nativeStatus));
        }

        ULONG stateByteCount = 0u;
        ULONG digestByteCount = 0u;
        ULONG resultByteCount = 0u;
        nativeStatus = BCryptGetProperty(
            provider,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&stateByteCount),
            sizeof(stateByteCount),
            &resultByteCount,
            0u);
        if (nativeStatus < 0 || resultByteCount != sizeof(stateByteCount))
        {
            Close();
            return Fail(
                AdaptiveMsaaValidationPlanReason::HashPropertyQueryFailed,
                0u,
                static_cast<uint32_t>(nativeStatus));
        }
        nativeStatus = BCryptGetProperty(
            provider,
            BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&digestByteCount),
            sizeof(digestByteCount),
            &resultByteCount,
            0u);
        if (nativeStatus < 0 || resultByteCount != sizeof(digestByteCount) ||
            digestByteCount != digest.size())
        {
            Close();
            return Fail(
                AdaptiveMsaaValidationPlanReason::HashPropertyQueryFailed,
                0u,
                static_cast<uint32_t>(nativeStatus));
        }

        try
        {
            state.resize(stateByteCount);
        }
        catch (...)
        {
            Close();
            return Fail(
                AdaptiveMsaaValidationPlanReason::HashStateAllocationFailed);
        }
        nativeStatus = BCryptCreateHash(
            provider,
            &hash,
            state.data(),
            static_cast<ULONG>(state.size()),
            nullptr,
            0u,
            0u);
        if (nativeStatus < 0)
        {
            Close();
            return Fail(
                AdaptiveMsaaValidationPlanReason::HashCreationFailed,
                0u,
                static_cast<uint32_t>(nativeStatus));
        }
        if (bytes.size() > std::numeric_limits<ULONG>::max())
        {
            Close();
            return Fail(AdaptiveMsaaValidationPlanReason::HashDataFailed);
        }
        nativeStatus = BCryptHashData(
            hash,
            const_cast<PUCHAR>(bytes.data()),
            static_cast<ULONG>(bytes.size()),
            0u);
        if (nativeStatus < 0)
        {
            Close();
            return Fail(
                AdaptiveMsaaValidationPlanReason::HashDataFailed,
                0u,
                static_cast<uint32_t>(nativeStatus));
        }
        nativeStatus = BCryptFinishHash(
            hash,
            digest.data(),
            static_cast<ULONG>(digest.size()),
            0u);
        if (nativeStatus < 0)
        {
            Close();
            return Fail(
                AdaptiveMsaaValidationPlanReason::HashFinishFailed,
                0u,
                static_cast<uint32_t>(nativeStatus));
        }
        Close();

        static constexpr char Digits[] = "0123456789abcdef";
        output.assign(digest.size() * 2u, '0');
        for (size_t index = 0u; index < digest.size(); ++index)
        {
            output[index * 2u] = Digits[digest[index] >> 4u];
            output[index * 2u + 1u] = Digits[digest[index] & 0x0fu];
        }
        return Success();
    }

    [[nodiscard]] AdaptiveMsaaValidationPlanStatus ReadRecipeBytes(
        const std::filesystem::path& path,
        std::vector<uint8_t>& bytes)
    {
        if (path.empty())
            return Fail(AdaptiveMsaaValidationPlanReason::PathMissing);
        std::error_code error;
        if (!std::filesystem::exists(path, error))
        {
            return Fail(
                AdaptiveMsaaValidationPlanReason::FileMissing,
                0u,
                static_cast<uint32_t>(error.value()));
        }
        if (!std::filesystem::is_regular_file(path, error))
        {
            return Fail(
                AdaptiveMsaaValidationPlanReason::FileNotRegular,
                0u,
                static_cast<uint32_t>(error.value()));
        }
        const uintmax_t fileSize = std::filesystem::file_size(path, error);
        if (error || fileSize == 0u || fileSize > MaximumRecipeByteCount)
        {
            return Fail(
                AdaptiveMsaaValidationPlanReason::FileSizeInvalid,
                0u,
                static_cast<uint32_t>(error.value()));
        }

        std::ifstream input(path, std::ios::binary);
        if (!input)
            return Fail(AdaptiveMsaaValidationPlanReason::FileOpenFailed);
        try
        {
            bytes.resize(static_cast<size_t>(fileSize));
        }
        catch (...)
        {
            return Fail(AdaptiveMsaaValidationPlanReason::AllocationFailed);
        }
        input.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (!input || static_cast<size_t>(input.gcount()) != bytes.size())
            return Fail(AdaptiveMsaaValidationPlanReason::FileReadFailed);
        if (input.peek() != std::char_traits<char>::eof())
            return Fail(AdaptiveMsaaValidationPlanReason::FileReadFailed);
        return Success();
    }

    [[nodiscard]] bool ReadVector3(
        const Json::Value& value,
        std::array<double, 3u>& output)
    {
        if (!value.isArray() || value.size() != output.size())
            return false;
        for (Json::ArrayIndex index = 0u; index < value.size(); ++index)
        {
            if (!value[index].isNumeric())
                return false;
            output[index] = value[index].asDouble();
            if (!std::isfinite(output[index]))
                return false;
        }
        return true;
    }

    [[nodiscard]] AdaptiveMsaaValidationPlanStatus ReadTargets(
        const Json::Value& value,
        std::vector<std::string>& output,
        size_t caseIndex)
    {
        if (!value.isArray() || value.empty() || value.size() > 32u)
            return Fail(
                AdaptiveMsaaValidationPlanReason::InvalidTargets,
                caseIndex);
        std::set<std::string> unique;
        for (const Json::Value& entry : value)
        {
            if (!entry.isString() || !IsCanonicalText(entry.asString()) ||
                !unique.insert(entry.asString()).second)
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidTargets,
                    caseIndex);
            }
            output.push_back(entry.asString());
        }
        return Success();
    }

    [[nodiscard]] AdaptiveMsaaValidationPlanStatus ParseRecipe(
        const std::vector<uint8_t>& bytes,
        std::string sha256,
        AdaptiveMsaaValidationRecipe& recipe)
    {
        std::string text(bytes.begin(), bytes.end());
        std::istringstream input(text);
        Json::CharReaderBuilder builder;
        builder["allowComments"] = false;
        builder["collectComments"] = false;
        builder["allowTrailingCommas"] = false;
        builder["strictRoot"] = true;
        builder["failIfExtra"] = true;
        builder["rejectDupKeys"] = true;
        builder["allowSpecialFloats"] = false;
        Json::Value root;
        std::string errors;
        if (!Json::parseFromStream(builder, input, &root, &errors) ||
            !root.isObject())
        {
            return Fail(AdaptiveMsaaValidationPlanReason::JsonParseFailed);
        }
        if (!HasExactMembers(
                root,
                { "schema", "suiteId", "width", "height", "phases",
                    "camera", "cases" }))
        {
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidRootFields);
        }
        if (!root["schema"].isUInt() ||
            root["schema"].asUInt() != AdaptiveMsaaValidationRecipeSchema)
        {
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidSchema);
        }
        if (!root["suiteId"].isString() ||
            std::string_view(root["suiteId"].asString()) !=
                AdaptiveMsaaValidationSuiteId)
        {
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidSuite);
        }
        if (!root["width"].isUInt() || !root["height"].isUInt() ||
            root["width"].asUInt() != AdaptiveMsaaValidationWidth ||
            root["height"].asUInt() != AdaptiveMsaaValidationHeight)
        {
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidResolution);
        }

        const Json::Value& phases = root["phases"];
        if (!phases.isArray() ||
            phases.size() != AdaptiveMsaaValidationPhaseCount)
        {
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidPhases);
        }
        std::array<double, AdaptiveMsaaValidationPhaseCount> parsedPhases{};
        for (Json::ArrayIndex index = 0u; index < phases.size(); ++index)
        {
            if (!phases[index].isNumeric())
                return Fail(AdaptiveMsaaValidationPlanReason::InvalidPhases);
            parsedPhases[index] = phases[index].asDouble();
            if (!std::isfinite(parsedPhases[index]) ||
                parsedPhases[index] != ExpectedPhases[index])
            {
                return Fail(AdaptiveMsaaValidationPlanReason::InvalidPhases);
            }
        }
        if (parsedPhases[0u] == parsedPhases[1u])
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidPhases);

        const Json::Value& camera = root["camera"];
        if (!HasExactMembers(
                camera,
                { "position", "direction", "up", "verticalFovDegrees" }))
        {
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidCamera);
        }
        AdaptiveMsaaValidationPlanCamera parsedCamera;
        if (!ReadVector3(camera["position"], parsedCamera.position) ||
            !ReadVector3(camera["direction"], parsedCamera.direction) ||
            !ReadVector3(camera["up"], parsedCamera.up) ||
            !camera["verticalFovDegrees"].isNumeric())
        {
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidCamera);
        }
        parsedCamera.verticalFovDegrees =
            camera["verticalFovDegrees"].asDouble();
        if (!std::isfinite(parsedCamera.verticalFovDegrees) ||
            !SameCamera(parsedCamera, ExpectedCamera))
        {
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidCamera);
        }

        const Json::Value& cases = root["cases"];
        if (!cases.isArray() ||
            cases.size() != AdaptiveMsaaValidationCaseCount)
        {
            return Fail(AdaptiveMsaaValidationPlanReason::InvalidCaseCount);
        }

        AdaptiveMsaaValidationRecipe parsed;
        parsed.schema = root["schema"].asUInt();
        parsed.suiteId = root["suiteId"].asString();
        parsed.width = root["width"].asUInt();
        parsed.height = root["height"].asUInt();
        parsed.phases = parsedPhases;
        parsed.camera = parsedCamera;
        parsed.sha256 = std::move(sha256);
        parsed.cases.reserve(AdaptiveMsaaValidationCaseCount);

        for (Json::ArrayIndex index = 0u; index < cases.size(); ++index)
        {
            const Json::Value& entry = cases[index];
            if (!HasExactMembers(
                    entry,
                    { "id", "roi", "nodes", "materials", "settings" }))
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidCaseFields,
                    index);
            }
            if (!entry["id"].isString() ||
                !IsSafeSlug(entry["id"].asString()))
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidCaseId,
                    index);
            }
            if (std::string_view(entry["id"].asString()) !=
                AdaptiveMsaaValidationRequiredCases[index])
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidCaseOrder,
                    index);
            }

            const Json::Value& roi = entry["roi"];
            if (!HasExactMembers(roi, { "x", "y", "width", "height" }) ||
                !roi["x"].isUInt() || !roi["y"].isUInt() ||
                !roi["width"].isUInt() || !roi["height"].isUInt())
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidRoi,
                    index);
            }
            AdaptiveMsaaValidationPlanCase parsedCase;
            parsedCase.id = entry["id"].asString();
            parsedCase.roi = {
                roi["x"].asUInt(),
                roi["y"].asUInt(),
                roi["width"].asUInt(),
                roi["height"].asUInt()
            };
            const uint64_t roiMaxX =
                uint64_t(parsedCase.roi.x) + parsedCase.roi.width;
            const uint64_t roiMaxY =
                uint64_t(parsedCase.roi.y) + parsedCase.roi.height;
            if (parsedCase.roi.width == 0u ||
                parsedCase.roi.height == 0u ||
                roiMaxX > parsed.width || roiMaxY > parsed.height)
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidRoi,
                    index);
            }

            AdaptiveMsaaValidationPlanStatus status = ReadTargets(
                entry["nodes"],
                parsedCase.nodes,
                index);
            if (!status.Succeeded())
                return status;
            status = ReadTargets(
                entry["materials"],
                parsedCase.materials,
                index);
            if (!status.Succeeded())
                return status;

            const Json::Value& settings = entry["settings"];
            if (!HasExactMembers(
                    settings,
                    { "taa", "flashlight", "flashlightShadows",
                        "directionalShadows", "screenSpaceVisibility" }) ||
                !settings["taa"].isBool() ||
                !settings["flashlight"].isBool() ||
                !settings["flashlightShadows"].isBool() ||
                !settings["directionalShadows"].isBool() ||
                !settings["screenSpaceVisibility"].isBool())
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidSettings,
                    index);
            }
            parsedCase.settings = {
                settings["taa"].asBool(),
                settings["flashlight"].asBool(),
                settings["flashlightShadows"].asBool(),
                settings["directionalShadows"].asBool(),
                settings["screenSpaceVisibility"].asBool()
            };
            parsed.cases.push_back(std::move(parsedCase));
        }

        recipe = std::move(parsed);
        return Success();
    }

    [[nodiscard]] std::string MakeCaptureId(
        std::string_view caseId,
        uint32_t sampleCount,
        AdaptiveMsaaValidationImplementation implementation)
    {
        return "capture--" + std::string(caseId) + "--" +
            std::to_string(sampleCount) + "x--" +
            GetAdaptiveMsaaValidationPlanImplementationName(implementation);
    }

    [[nodiscard]] std::string MakeComparisonId(
        std::string_view caseId,
        uint32_t sampleCount,
        AdaptiveMsaaValidationImplementation implementation)
    {
        return "compare--" + std::string(caseId) + "--" +
            std::to_string(sampleCount) + "x--" +
            GetAdaptiveMsaaValidationPlanImplementationName(implementation);
    }

    [[nodiscard]] AdaptiveMsaaValidationCaptureJob MakeCaptureJob(
        const AdaptiveMsaaValidationRecipe& recipe,
        size_t caseIndex,
        uint32_t sampleCount,
        AdaptiveMsaaValidationImplementation implementation)
    {
        AdaptiveMsaaValidationCaptureJob job;
        job.implementation = implementation;
        job.sampleCount = sampleCount;
        job.caseIndex = caseIndex;
        job.caseId = recipe.cases[caseIndex].id;
        job.id = MakeCaptureId(job.caseId, sampleCount, implementation);
        const std::string sampleDirectory =
            std::to_string(sampleCount) + "x";
        const std::string implementationName =
            GetAdaptiveMsaaValidationPlanImplementationName(implementation);
        for (size_t phaseIndex = 0u;
            phaseIndex < AdaptiveMsaaValidationPhaseCount;
            ++phaseIndex)
        {
            AdaptiveMsaaValidationCaptureFrame& frame = job.frames[phaseIndex];
            frame.phaseIndex = static_cast<uint32_t>(phaseIndex);
            frame.phase = recipe.phases[phaseIndex];
            frame.phaseId = "phase-" + std::to_string(phaseIndex);
            frame.captureId = job.id + "--" + frame.phaseId;
            frame.relativeBmpPath =
                std::filesystem::path("captures") /
                job.caseId /
                sampleDirectory /
                implementationName /
                (frame.phaseId + ".bmp");
            frame.relativeReceiptPath = frame.relativeBmpPath;
            frame.relativeReceiptPath += ".adaptive-msaa.json";
        }
        return job;
    }

    [[nodiscard]] AdaptiveMsaaValidationPlan BuildPlanUnchecked(
        AdaptiveMsaaValidationRecipe recipe)
    {
        AdaptiveMsaaValidationPlan plan;
        plan.recipe = std::move(recipe);
        plan.captureJobs.reserve(AdaptiveMsaaValidationCaptureJobCount);
        plan.comparisons.reserve(AdaptiveMsaaValidationComparisonCount);

        for (size_t caseIndex = 0u;
            caseIndex < AdaptiveMsaaValidationCaseCount;
            ++caseIndex)
        {
            for (uint32_t sampleCount : AdaptiveMsaaValidationSampleCounts)
            {
                plan.captureJobs.push_back(MakeCaptureJob(
                    plan.recipe,
                    caseIndex,
                    sampleCount,
                    AdaptiveMsaaValidationImplementation::Reference));
            }
        }

        for (size_t caseIndex = 0u;
            caseIndex < AdaptiveMsaaValidationCaseCount;
            ++caseIndex)
        {
            for (size_t sampleIndex = 0u;
                sampleIndex < AdaptiveMsaaValidationSampleCounts.size();
                ++sampleIndex)
            {
                const uint32_t sampleCount =
                    AdaptiveMsaaValidationSampleCounts[sampleIndex];
                const size_t referenceIndex =
                    caseIndex * AdaptiveMsaaValidationSampleCounts.size() +
                    sampleIndex;
                for (AdaptiveMsaaValidationImplementation implementation :
                    CandidateImplementations)
                {
                    const size_t candidateIndex = plan.captureJobs.size();
                    plan.captureJobs.push_back(MakeCaptureJob(
                        plan.recipe,
                        caseIndex,
                        sampleCount,
                        implementation));

                    AdaptiveMsaaValidationComparison comparison;
                    comparison.id = MakeComparisonId(
                        plan.recipe.cases[caseIndex].id,
                        sampleCount,
                        implementation);
                    comparison.caseId = plan.recipe.cases[caseIndex].id;
                    comparison.sampleCount = sampleCount;
                    comparison.candidateImplementation = implementation;
                    comparison.referenceCaptureJobIndex = referenceIndex;
                    comparison.candidateCaptureJobIndex = candidateIndex;
                    plan.comparisons.push_back(std::move(comparison));
                }
            }
        }
        return plan;
    }

    [[nodiscard]] bool SameFrame(
        const AdaptiveMsaaValidationCaptureFrame& left,
        const AdaptiveMsaaValidationCaptureFrame& right)
    {
        return left.phaseIndex == right.phaseIndex &&
            left.phase == right.phase && left.phaseId == right.phaseId &&
            left.captureId == right.captureId &&
            left.relativeBmpPath == right.relativeBmpPath &&
            left.relativeReceiptPath == right.relativeReceiptPath;
    }

    [[nodiscard]] bool SameCaptureJob(
        const AdaptiveMsaaValidationCaptureJob& left,
        const AdaptiveMsaaValidationCaptureJob& right)
    {
        if (left.id != right.id ||
            left.implementation != right.implementation ||
            left.sampleCount != right.sampleCount ||
            left.caseIndex != right.caseIndex || left.caseId != right.caseId)
        {
            return false;
        }
        for (size_t index = 0u; index < left.frames.size(); ++index)
        {
            if (!SameFrame(left.frames[index], right.frames[index]))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool SameComparison(
        const AdaptiveMsaaValidationComparison& left,
        const AdaptiveMsaaValidationComparison& right)
    {
        return left.id == right.id && left.caseId == right.caseId &&
            left.sampleCount == right.sampleCount &&
            left.candidateImplementation == right.candidateImplementation &&
            left.referenceCaptureJobIndex ==
                right.referenceCaptureJobIndex &&
            left.candidateCaptureJobIndex ==
                right.candidateCaptureJobIndex;
    }
}

namespace uvsr::adaptive_msaa
{
    const char* GetAdaptiveMsaaValidationPlanReasonName(
        AdaptiveMsaaValidationPlanReason reason) noexcept
    {
#define UVSR_VALIDATION_PLAN_REASON_NAME(name) \
    case AdaptiveMsaaValidationPlanReason::name: return #name
        switch (reason)
        {
        UVSR_VALIDATION_PLAN_REASON_NAME(None);
        UVSR_VALIDATION_PLAN_REASON_NAME(NotInitialized);
        UVSR_VALIDATION_PLAN_REASON_NAME(PathMissing);
        UVSR_VALIDATION_PLAN_REASON_NAME(FileMissing);
        UVSR_VALIDATION_PLAN_REASON_NAME(FileNotRegular);
        UVSR_VALIDATION_PLAN_REASON_NAME(FileOpenFailed);
        UVSR_VALIDATION_PLAN_REASON_NAME(FileSizeInvalid);
        UVSR_VALIDATION_PLAN_REASON_NAME(FileReadFailed);
        UVSR_VALIDATION_PLAN_REASON_NAME(HashProviderUnavailable);
        UVSR_VALIDATION_PLAN_REASON_NAME(HashPropertyQueryFailed);
        UVSR_VALIDATION_PLAN_REASON_NAME(HashStateAllocationFailed);
        UVSR_VALIDATION_PLAN_REASON_NAME(HashCreationFailed);
        UVSR_VALIDATION_PLAN_REASON_NAME(HashDataFailed);
        UVSR_VALIDATION_PLAN_REASON_NAME(HashFinishFailed);
        UVSR_VALIDATION_PLAN_REASON_NAME(ExpectedHashInvalid);
        UVSR_VALIDATION_PLAN_REASON_NAME(ExpectedHashMismatch);
        UVSR_VALIDATION_PLAN_REASON_NAME(JsonParseFailed);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidRootFields);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidSchema);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidSuite);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidResolution);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidPhases);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidCamera);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidCaseCount);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidCaseOrder);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidCaseFields);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidCaseId);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidRoi);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidTargets);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidSettings);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidImplementation);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidSampleCount);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidCaptureJob);
        UVSR_VALIDATION_PLAN_REASON_NAME(InvalidComparison);
        UVSR_VALIDATION_PLAN_REASON_NAME(DuplicateId);
        UVSR_VALIDATION_PLAN_REASON_NAME(UnsafePath);
        UVSR_VALIDATION_PLAN_REASON_NAME(AllocationFailed);
        default:
            return "Unknown";
        }
#undef UVSR_VALIDATION_PLAN_REASON_NAME
    }

    const char* GetAdaptiveMsaaValidationPlanImplementationName(
        AdaptiveMsaaValidationImplementation implementation) noexcept
    {
        switch (implementation)
        {
        case AdaptiveMsaaValidationImplementation::Reference:
            return "reference";
        case AdaptiveMsaaValidationImplementation::AdaptiveGBuffer:
            return "adaptive-g-buffer";
        case AdaptiveMsaaValidationImplementation::CompactVisibility:
            return "compact-visibility";
        default:
            return "invalid";
        }
    }

    AdaptiveMsaaValidationPlanStatus ValidateAdaptiveMsaaValidationPlan(
        const AdaptiveMsaaValidationPlan& plan) noexcept
    {
        try
        {
            const AdaptiveMsaaValidationRecipe& recipe = plan.recipe;
            if (recipe.schema != AdaptiveMsaaValidationRecipeSchema)
                return Fail(AdaptiveMsaaValidationPlanReason::InvalidSchema);
            if (std::string_view(recipe.suiteId) !=
                AdaptiveMsaaValidationSuiteId)
                return Fail(AdaptiveMsaaValidationPlanReason::InvalidSuite);
            if (recipe.width != AdaptiveMsaaValidationWidth ||
                recipe.height != AdaptiveMsaaValidationHeight)
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidResolution);
            }
            if (recipe.phases != ExpectedPhases ||
                recipe.phases[0u] == recipe.phases[1u])
            {
                return Fail(AdaptiveMsaaValidationPlanReason::InvalidPhases);
            }
            if (!SameCamera(recipe.camera, ExpectedCamera))
                return Fail(AdaptiveMsaaValidationPlanReason::InvalidCamera);
            if (!IsLowerHexSha256(recipe.sha256))
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::ExpectedHashInvalid);
            }
            if (recipe.cases.size() != AdaptiveMsaaValidationCaseCount)
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidCaseCount);
            }
            for (size_t index = 0u; index < recipe.cases.size(); ++index)
            {
                const AdaptiveMsaaValidationPlanCase& entry =
                    recipe.cases[index];
                if (std::string_view(entry.id) !=
                    AdaptiveMsaaValidationRequiredCases[index])
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidCaseOrder,
                        index);
                }
                const uint64_t roiMaxX = uint64_t(entry.roi.x) +
                    entry.roi.width;
                const uint64_t roiMaxY = uint64_t(entry.roi.y) +
                    entry.roi.height;
                if (entry.roi.width == 0u || entry.roi.height == 0u ||
                    roiMaxX > recipe.width || roiMaxY > recipe.height)
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidRoi,
                        index);
                }
                if (entry.nodes.empty() || entry.nodes.size() > 32u ||
                    entry.materials.empty() ||
                    entry.materials.size() > 32u)
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidTargets,
                        index);
                }
                std::set<std::string> nodes;
                std::set<std::string> materials;
                for (const std::string& node : entry.nodes)
                {
                    if (!IsCanonicalText(node) || !nodes.insert(node).second)
                    {
                        return Fail(
                            AdaptiveMsaaValidationPlanReason::InvalidTargets,
                            index);
                    }
                }
                for (const std::string& material : entry.materials)
                {
                    if (!IsCanonicalText(material) ||
                        !materials.insert(material).second)
                    {
                        return Fail(
                            AdaptiveMsaaValidationPlanReason::InvalidTargets,
                            index);
                    }
                }
            }

            if (plan.captureJobs.size() !=
                AdaptiveMsaaValidationCaptureJobCount)
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidCaptureJob);
            }
            if (plan.comparisons.size() !=
                AdaptiveMsaaValidationComparisonCount)
            {
                return Fail(
                    AdaptiveMsaaValidationPlanReason::InvalidComparison);
            }

            std::set<std::string> allIds;
            std::set<std::string> paths;
            for (size_t index = 0u; index < plan.captureJobs.size(); ++index)
            {
                const AdaptiveMsaaValidationCaptureJob& job =
                    plan.captureJobs[index];
                if (!allIds.insert(job.id).second)
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::DuplicateId,
                        index);
                }
                if (!IsSupportedSampleCount(job.sampleCount))
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidSampleCount,
                        index);
                }
                const bool referenceRange =
                    index < AdaptiveMsaaValidationReferenceJobCount;
                if ((referenceRange && job.implementation !=
                        AdaptiveMsaaValidationImplementation::Reference) ||
                    (!referenceRange &&
                        !IsCandidateImplementation(job.implementation)))
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidImplementation,
                        index);
                }
                for (const AdaptiveMsaaValidationCaptureFrame& frame :
                    job.frames)
                {
                    if (!allIds.insert(frame.captureId).second)
                    {
                        return Fail(
                            AdaptiveMsaaValidationPlanReason::DuplicateId,
                            index);
                    }
                    if (!IsSafeRelativePath(frame.relativeBmpPath) ||
                        !IsSafeRelativePath(frame.relativeReceiptPath))
                    {
                        return Fail(
                            AdaptiveMsaaValidationPlanReason::UnsafePath,
                            index);
                    }
                    if (!paths.insert(
                            frame.relativeBmpPath.generic_string()).second ||
                        !paths.insert(
                            frame.relativeReceiptPath.generic_string()).second)
                    {
                        return Fail(
                            AdaptiveMsaaValidationPlanReason::DuplicateId,
                            index);
                    }
                }
            }
            for (size_t index = 0u; index < plan.comparisons.size(); ++index)
            {
                const AdaptiveMsaaValidationComparison& comparison =
                    plan.comparisons[index];
                if (!allIds.insert(comparison.id).second)
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::DuplicateId,
                        index);
                }
                if (!IsSupportedSampleCount(comparison.sampleCount))
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidSampleCount,
                        index);
                }
                if (!IsCandidateImplementation(
                        comparison.candidateImplementation))
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidImplementation,
                        index);
                }
                if (comparison.referenceCaptureJobIndex >=
                        plan.captureJobs.size() ||
                    comparison.candidateCaptureJobIndex >=
                        plan.captureJobs.size())
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidComparison,
                        index);
                }
                const AdaptiveMsaaValidationCaptureJob& reference =
                    plan.captureJobs[
                        comparison.referenceCaptureJobIndex];
                const AdaptiveMsaaValidationCaptureJob& candidate =
                    plan.captureJobs[
                        comparison.candidateCaptureJobIndex];
                if (reference.implementation !=
                        AdaptiveMsaaValidationImplementation::Reference ||
                    candidate.implementation !=
                        comparison.candidateImplementation ||
                    reference.caseId != comparison.caseId ||
                    candidate.caseId != comparison.caseId ||
                    reference.sampleCount != comparison.sampleCount ||
                    candidate.sampleCount != comparison.sampleCount)
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidComparison,
                        index);
                }
            }

            const AdaptiveMsaaValidationPlan expected =
                BuildPlanUnchecked(recipe);
            for (size_t index = 0u; index < plan.captureJobs.size(); ++index)
            {
                if (!SameCaptureJob(
                        plan.captureJobs[index],
                        expected.captureJobs[index]))
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidCaptureJob,
                        index);
                }
            }
            for (size_t index = 0u; index < plan.comparisons.size(); ++index)
            {
                if (!SameComparison(
                        plan.comparisons[index],
                        expected.comparisons[index]))
                {
                    return Fail(
                        AdaptiveMsaaValidationPlanReason::InvalidComparison,
                        index);
                }
            }
            return Success();
        }
        catch (...)
        {
            return Fail(AdaptiveMsaaValidationPlanReason::AllocationFailed);
        }
    }

    AdaptiveMsaaValidationPlanResult LoadAdaptiveMsaaValidationPlan(
        const std::filesystem::path& recipePath,
        std::string_view expectedSha256)
    {
        AdaptiveMsaaValidationPlanResult result;
        try
        {
            if (!expectedSha256.empty() &&
                !IsLowerHexSha256(expectedSha256))
            {
                result.status = Fail(
                    AdaptiveMsaaValidationPlanReason::ExpectedHashInvalid);
                return result;
            }

            std::vector<uint8_t> bytes;
            result.status = ReadRecipeBytes(recipePath, bytes);
            if (!result.Succeeded())
                return result;

            std::string sha256;
            result.status = ComputeSha256(bytes, sha256);
            if (!result.Succeeded())
                return result;
            if (!expectedSha256.empty() &&
                std::string_view(sha256) != expectedSha256)
            {
                result.status = Fail(
                    AdaptiveMsaaValidationPlanReason::ExpectedHashMismatch);
                return result;
            }

            AdaptiveMsaaValidationRecipe recipe;
            result.status = ParseRecipe(bytes, std::move(sha256), recipe);
            if (!result.Succeeded())
                return result;
            result.plan = BuildPlanUnchecked(std::move(recipe));
            result.status = ValidateAdaptiveMsaaValidationPlan(result.plan);
            if (!result.Succeeded())
                result.plan = {};
            return result;
        }
        catch (...)
        {
            result.plan = {};
            result.status = Fail(
                AdaptiveMsaaValidationPlanReason::AllocationFailed);
            return result;
        }
    }
}
