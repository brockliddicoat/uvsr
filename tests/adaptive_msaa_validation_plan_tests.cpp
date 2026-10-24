#include "adaptive_msaa_validation_plan.h"

#include <donut/core/json.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace uvsr::adaptive_msaa;

namespace
{
    void Require(bool condition, std::string_view message)
    {
        if (!condition)
            throw std::runtime_error(std::string(message));
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const uint64_t seed = static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            for (uint32_t attempt = 0u; attempt < 64u; ++attempt)
            {
                std::error_code error;
                const std::filesystem::path candidate =
                    std::filesystem::temp_directory_path() /
                    ("uvsr-adaptive-msaa-plan-" + std::to_string(seed) + "-" +
                        std::to_string(attempt));
                if (std::filesystem::create_directory(candidate, error))
                {
                    m_Path = candidate;
                    return;
                }
            }
            throw std::runtime_error(
                "could not create adaptive-MSAA plan test directory");
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_Path, ignored);
        }

        [[nodiscard]] const std::filesystem::path& Path() const noexcept
        {
            return m_Path;
        }

    private:
        std::filesystem::path m_Path;
    };

    [[nodiscard]] Json::Value ReadJson(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(bool(input), "could not open validation recipe");
        Json::CharReaderBuilder builder;
        builder["collectComments"] = false;
        builder["rejectDupKeys"] = true;
        Json::Value root;
        std::string errors;
        Require(
            Json::parseFromStream(builder, input, &root, &errors) &&
                root.isObject(),
            "could not parse validation recipe fixture");
        return root;
    }

    void WriteText(
        const std::filesystem::path& path,
        const std::string& text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        Require(bool(output), "could not create mutated recipe");
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        Require(bool(output), "could not write mutated recipe");
    }

    [[nodiscard]] std::filesystem::path WriteJson(
        const TemporaryDirectory& temporary,
        const Json::Value& root,
        size_t index)
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        const std::filesystem::path path =
            temporary.Path() / ("recipe-" + std::to_string(index) + ".json");
        WriteText(path, Json::writeString(builder, root));
        return path;
    }

    void ExpectRecipeFailure(
        const TemporaryDirectory& temporary,
        const Json::Value& root,
        size_t& sequence,
        AdaptiveMsaaValidationPlanReason reason)
    {
        const AdaptiveMsaaValidationPlanResult result =
            LoadAdaptiveMsaaValidationPlan(
                WriteJson(temporary, root, sequence++));
        Require(!result.Succeeded(), "mutated recipe unexpectedly passed");
        Require(
            result.status.reason == reason,
            std::string("mutated recipe failed with ") +
                GetAdaptiveMsaaValidationPlanReasonName(result.status.reason) +
                " instead of " +
                GetAdaptiveMsaaValidationPlanReasonName(reason));
        Require(
            result.plan.captureJobs.empty() &&
                result.plan.comparisons.empty(),
            "failed recipe retained an executable plan");
    }

    void ExpectPlanFailure(
        const AdaptiveMsaaValidationPlan& plan,
        AdaptiveMsaaValidationPlanReason reason)
    {
        const AdaptiveMsaaValidationPlanStatus status =
            ValidateAdaptiveMsaaValidationPlan(plan);
        Require(!status.Succeeded(), "mutated plan unexpectedly passed");
        Require(
            status.reason == reason,
            std::string("mutated plan failed with ") +
                GetAdaptiveMsaaValidationPlanReasonName(status.reason) +
                " instead of " +
                GetAdaptiveMsaaValidationPlanReasonName(reason));
    }

    [[nodiscard]] std::string PlanFingerprint(
        const AdaptiveMsaaValidationPlan& plan)
    {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(17)
            << plan.recipe.schema << '|'
            << plan.recipe.suiteId << '|'
            << plan.recipe.width << 'x' << plan.recipe.height << '|'
            << plan.recipe.sha256 << '|';
        for (double phase : plan.recipe.phases)
            output << phase << ',';
        for (double value : plan.recipe.camera.position)
            output << value << ',';
        for (double value : plan.recipe.camera.direction)
            output << value << ',';
        for (double value : plan.recipe.camera.up)
            output << value << ',';
        output << plan.recipe.camera.verticalFovDegrees << '|';
        for (const AdaptiveMsaaValidationPlanCase& entry : plan.recipe.cases)
        {
            output << entry.id << ':' << entry.roi.x << ',' << entry.roi.y
                << ',' << entry.roi.width << ',' << entry.roi.height << ':';
            for (const std::string& node : entry.nodes)
                output << node << ',';
            output << ':';
            for (const std::string& material : entry.materials)
                output << material << ',';
            output << ':' << entry.settings.taa << entry.settings.flashlight
                << entry.settings.flashlightShadows
                << entry.settings.directionalShadows
                << entry.settings.screenSpaceVisibility << '|';
        }
        for (const AdaptiveMsaaValidationCaptureJob& job : plan.captureJobs)
        {
            output << job.id << ':'
                << GetAdaptiveMsaaValidationPlanImplementationName(
                    job.implementation)
                << ':' << job.sampleCount << ':' << job.caseIndex << ':'
                << job.caseId;
            for (const AdaptiveMsaaValidationCaptureFrame& frame : job.frames)
            {
                output << ':' << frame.phaseIndex << ':' << frame.phase << ':'
                    << frame.phaseId << ':' << frame.captureId << ':'
                    << frame.relativeBmpPath.generic_string() << ':'
                    << frame.relativeReceiptPath.generic_string();
            }
            output << '|';
        }
        for (const AdaptiveMsaaValidationComparison& comparison :
            plan.comparisons)
        {
            output << comparison.id << ':' << comparison.caseId << ':'
                << comparison.sampleCount << ':'
                << GetAdaptiveMsaaValidationPlanImplementationName(
                    comparison.candidateImplementation)
                << ':' << comparison.referenceCaptureJobIndex << ':'
                << comparison.candidateCaptureJobIndex << '|';
        }
        return output.str();
    }

    void ValidateGeneratedGraph(const AdaptiveMsaaValidationPlan& plan)
    {
        Require(
            ValidateAdaptiveMsaaValidationPlan(plan).Succeeded(),
            "generated validation plan did not self-validate");
        Require(
            plan.captureJobs.size() ==
                AdaptiveMsaaValidationCaptureJobCount,
            "validation plan did not contain 180 capture jobs");
        Require(
            plan.comparisons.size() ==
                AdaptiveMsaaValidationComparisonCount,
            "validation plan did not contain 120 comparisons");
        Require(
            plan.recipe.cases.size() == AdaptiveMsaaValidationCaseCount,
            "validation recipe did not retain all cases");
        Require(
            plan.recipe.camera.position ==
                std::array<double, 3u>{ 0.0, 0.2, 8.0 } &&
                plan.recipe.camera.direction ==
                    std::array<double, 3u>{ 0.0, 0.0, -1.0 } &&
                plan.recipe.camera.up ==
                    std::array<double, 3u>{ 0.0, 1.0, 0.0 } &&
                plan.recipe.camera.verticalFovDegrees == 42.0,
            "validation plan camera differs from the recipe contract");

        size_t referenceCount = 0u;
        size_t adaptiveGBufferCount = 0u;
        size_t compactVisibilityCount = 0u;
        std::set<std::string> ids;
        std::set<std::string> paths;
        for (size_t index = 0u; index < plan.captureJobs.size(); ++index)
        {
            const AdaptiveMsaaValidationCaptureJob& job =
                plan.captureJobs[index];
            Require(ids.insert(job.id).second, "capture ID is not unique");
            switch (job.implementation)
            {
            case AdaptiveMsaaValidationImplementation::Reference:
                ++referenceCount;
                Require(
                    index < AdaptiveMsaaValidationReferenceJobCount,
                    "Reference capture escaped the leading job block");
                break;
            case AdaptiveMsaaValidationImplementation::AdaptiveGBuffer:
                ++adaptiveGBufferCount;
                Require(
                    index >= AdaptiveMsaaValidationReferenceJobCount,
                    "candidate capture entered the Reference block");
                break;
            case AdaptiveMsaaValidationImplementation::CompactVisibility:
                ++compactVisibilityCount;
                Require(
                    index >= AdaptiveMsaaValidationReferenceJobCount,
                    "candidate capture entered the Reference block");
                break;
            default:
                throw std::runtime_error(
                    "generated plan contains an invalid implementation");
            }
            Require(job.frames.size() == 2u, "capture is not two-phase");
            for (size_t phaseIndex = 0u; phaseIndex < job.frames.size();
                ++phaseIndex)
            {
                const AdaptiveMsaaValidationCaptureFrame& frame =
                    job.frames[phaseIndex];
                Require(
                    frame.phaseIndex == phaseIndex &&
                        frame.phase == plan.recipe.phases[phaseIndex] &&
                        frame.phaseId ==
                            "phase-" + std::to_string(phaseIndex),
                    "capture phase binding differs");
                Require(
                    frame.captureId == job.id + "--" + frame.phaseId &&
                        ids.insert(frame.captureId).second,
                    "per-frame receipt capture ID is not unique");
                Require(
                    paths.insert(frame.relativeBmpPath.generic_string()).second,
                    "BMP path is not unique");
                Require(
                    paths.insert(
                        frame.relativeReceiptPath.generic_string()).second,
                    "receipt path is not unique");
                Require(
                    frame.relativeReceiptPath.generic_string() ==
                        frame.relativeBmpPath.generic_string() +
                            ".adaptive-msaa.json",
                    "receipt is not the BMP sibling expected by the writer");
            }
        }
        Require(
            referenceCount == AdaptiveMsaaValidationReferenceJobCount &&
                adaptiveGBufferCount == 60u &&
                compactVisibilityCount == 60u,
            "capture implementation distribution differs");

        const AdaptiveMsaaValidationCaptureJob& first = plan.captureJobs[0u];
        Require(
            first.id == "capture--crossing-triangles--2x--reference" &&
                first.caseIndex == 0u && first.sampleCount == 2u &&
                first.frames[0u].captureId ==
                    "capture--crossing-triangles--2x--reference--phase-0" &&
                first.frames[0u].relativeBmpPath.generic_string() ==
                    "captures/crossing-triangles/2x/reference/phase-0.bmp",
            "leading Reference job ordering differs");
        const AdaptiveMsaaValidationCaptureJob& firstCandidate =
            plan.captureJobs[AdaptiveMsaaValidationReferenceJobCount];
        Require(
            firstCandidate.id ==
                "capture--crossing-triangles--2x--adaptive-g-buffer",
            "leading candidate job ordering differs");
        Require(
            plan.captureJobs[AdaptiveMsaaValidationReferenceJobCount + 1u].id ==
                "capture--crossing-triangles--2x--compact-visibility",
            "candidate mode ordering differs");
        Require(
            plan.captureJobs.back().id ==
                "capture--screen-space-visibility--16x--compact-visibility",
            "terminal candidate job ordering differs");

        std::array<size_t, AdaptiveMsaaValidationReferenceJobCount> reuse{};
        for (const AdaptiveMsaaValidationComparison& comparison :
            plan.comparisons)
        {
            Require(
                ids.insert(comparison.id).second,
                "comparison ID is not unique");
            Require(
                comparison.referenceCaptureJobIndex < reuse.size(),
                "comparison did not select a narrow Reference job");
            ++reuse[comparison.referenceCaptureJobIndex];
            const AdaptiveMsaaValidationCaptureJob& reference =
                plan.captureJobs[comparison.referenceCaptureJobIndex];
            const AdaptiveMsaaValidationCaptureJob& candidate =
                plan.captureJobs[comparison.candidateCaptureJobIndex];
            Require(
                reference.implementation ==
                        AdaptiveMsaaValidationImplementation::Reference &&
                    reference.caseId == candidate.caseId &&
                    reference.caseId == comparison.caseId &&
                    reference.sampleCount == candidate.sampleCount &&
                    reference.sampleCount == comparison.sampleCount,
                "Reference reuse crossed a case or sample count");
        }
        Require(
            std::all_of(
                reuse.begin(), reuse.end(), [](size_t count) { return count == 2u; }),
            "each narrow Reference job must serve exactly two candidate modes");

        const AdaptiveMsaaValidationPlanCase& movingShadow =
            plan.recipe.cases[11u];
        Require(
            !movingShadow.settings.taa && movingShadow.settings.flashlight &&
                movingShadow.settings.flashlightShadows &&
                movingShadow.settings.directionalShadows &&
                !movingShadow.settings.screenSpaceVisibility,
            "per-case settings were not retained");
        Require(
            movingShadow.roi.x == 520u && movingShadow.roi.y == 500u &&
                movingShadow.roi.width == 430u &&
                movingShadow.roi.height == 350u,
            "per-case ROI was not retained");
    }

    void ValidateRecipeRejections(
        const std::filesystem::path& recipePath,
        const AdaptiveMsaaValidationPlanResult& accepted)
    {
        TemporaryDirectory temporary;
        size_t sequence = 0u;
        const Json::Value original = ReadJson(recipePath);

        Json::Value mutation = original;
        mutation["schema"] = 2u;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidSchema);

        mutation = original;
        mutation["suiteId"] = "uvsr-adaptive-msaa-image-diff-v2";
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidSuite);

        mutation = original;
        mutation["width"] = 1919u;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidResolution);

        mutation = original;
        mutation["unexpected"] = true;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidRootFields);

        mutation = original;
        mutation.removeMember("camera");
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidRootFields);

        mutation = original;
        mutation["phases"][1u] = mutation["phases"][0u];
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidPhases);

        mutation = original;
        std::swap(mutation["phases"][0u], mutation["phases"][1u]);
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidPhases);

        mutation = original;
        mutation["phases"].resize(1u);
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidPhases);

        mutation = original;
        mutation["camera"]["position"][0u] = 0.01;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidCamera);

        mutation = original;
        mutation["camera"]["extra"] = 0u;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidCamera);

        mutation = original;
        mutation["cases"].resize(14u);
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidCaseCount);

        mutation = original;
        std::swap(mutation["cases"][0u], mutation["cases"][1u]);
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidCaseOrder);

        mutation = original;
        mutation["cases"][1u]["id"] = mutation["cases"][0u]["id"];
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidCaseOrder);

        mutation = original;
        mutation["cases"][0u]["id"] = "../crossing-triangles";
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidCaseId);

        mutation = original;
        mutation["cases"][0u]["extra"] = true;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidCaseFields);

        mutation = original;
        mutation["cases"][0u]["roi"]["width"] = 0u;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidRoi);

        mutation = original;
        mutation["cases"][0u]["roi"]["x"] = 1919u;
        mutation["cases"][0u]["roi"]["width"] = 2u;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidRoi);

        mutation = original;
        mutation["cases"][0u]["roi"]["extra"] = 0u;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidRoi);

        mutation = original;
        mutation["cases"][0u]["nodes"] = Json::Value(Json::arrayValue);
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidTargets);

        mutation = original;
        mutation["cases"][0u]["nodes"].append(
            mutation["cases"][0u]["nodes"][0u]);
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidTargets);

        mutation = original;
        mutation["cases"][0u]["settings"]["taa"] = 0u;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidSettings);

        mutation = original;
        mutation["cases"][0u]["settings"].removeMember("flashlight");
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidSettings);

        mutation = original;
        mutation["cases"][0u]["settings"]["extra"] = false;
        ExpectRecipeFailure(
            temporary,
            mutation,
            sequence,
            AdaptiveMsaaValidationPlanReason::InvalidSettings);

        const std::filesystem::path duplicateKeyPath =
            temporary.Path() / "duplicate-key.json";
        WriteText(duplicateKeyPath, "{\"schema\":1,\"schema\":1}");
        const AdaptiveMsaaValidationPlanResult duplicateKey =
            LoadAdaptiveMsaaValidationPlan(duplicateKeyPath);
        Require(
            !duplicateKey.Succeeded() &&
                duplicateKey.status.reason ==
                    AdaptiveMsaaValidationPlanReason::JsonParseFailed,
            "duplicate JSON keys did not fail during parsing");

        const AdaptiveMsaaValidationPlanResult invalidExpectedHash =
            LoadAdaptiveMsaaValidationPlan(recipePath, "ABC");
        Require(
            !invalidExpectedHash.Succeeded() &&
                invalidExpectedHash.status.reason ==
                    AdaptiveMsaaValidationPlanReason::ExpectedHashInvalid,
            "noncanonical expected recipe hash was accepted");

        std::string mismatchedHash = accepted.plan.recipe.sha256;
        mismatchedHash[0u] = mismatchedHash[0u] == 'a' ? 'b' : 'a';
        const AdaptiveMsaaValidationPlanResult mismatch =
            LoadAdaptiveMsaaValidationPlan(recipePath, mismatchedHash);
        Require(
            !mismatch.Succeeded() &&
                mismatch.status.reason ==
                    AdaptiveMsaaValidationPlanReason::ExpectedHashMismatch,
            "mismatched expected recipe hash was accepted");

        const AdaptiveMsaaValidationPlanResult matched =
            LoadAdaptiveMsaaValidationPlan(
                recipePath,
                accepted.plan.recipe.sha256);
        Require(matched.Succeeded(), "matching recipe hash was rejected");

        const AdaptiveMsaaValidationPlanResult missing =
            LoadAdaptiveMsaaValidationPlan(
                temporary.Path() / "missing.json");
        Require(
            !missing.Succeeded() &&
                missing.status.reason ==
                    AdaptiveMsaaValidationPlanReason::FileMissing,
            "missing recipe did not fail closed");
        const AdaptiveMsaaValidationPlanResult emptyPath =
            LoadAdaptiveMsaaValidationPlan(std::filesystem::path{});
        Require(
            !emptyPath.Succeeded() &&
                emptyPath.status.reason ==
                    AdaptiveMsaaValidationPlanReason::PathMissing,
            "empty recipe path did not fail closed");
        const AdaptiveMsaaValidationPlanResult directory =
            LoadAdaptiveMsaaValidationPlan(temporary.Path());
        Require(
            !directory.Succeeded() &&
                directory.status.reason ==
                    AdaptiveMsaaValidationPlanReason::FileNotRegular,
            "directory recipe did not fail closed");
    }

    void ValidatePlanRejections(const AdaptiveMsaaValidationPlan& accepted)
    {
        AdaptiveMsaaValidationPlan mutation = accepted;
        mutation.captureJobs[0u].sampleCount = 1u;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidSampleCount);

        mutation = accepted;
        mutation.captureJobs[0u].implementation =
            AdaptiveMsaaValidationImplementation::AdaptiveGBuffer;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidImplementation);

        mutation = accepted;
        mutation.captureJobs[AdaptiveMsaaValidationReferenceJobCount]
            .implementation = AdaptiveMsaaValidationImplementation::Reference;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidImplementation);

        mutation = accepted;
        mutation.captureJobs[0u].implementation =
            static_cast<AdaptiveMsaaValidationImplementation>(99u);
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidImplementation);

        mutation = accepted;
        mutation.captureJobs[0u].id = mutation.captureJobs[1u].id;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::DuplicateId);

        mutation = accepted;
        mutation.captureJobs[0u].frames[0u].relativeBmpPath =
            "../escaped.bmp";
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::UnsafePath);

        mutation = accepted;
        mutation.captureJobs[0u].frames[0u].relativeBmpPath =
            mutation.captureJobs[1u].frames[0u].relativeBmpPath;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::DuplicateId);

        mutation = accepted;
        mutation.captureJobs[0u].frames[0u].captureId =
            mutation.captureJobs[1u].frames[0u].captureId;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::DuplicateId);

        mutation = accepted;
        mutation.captureJobs[0u].frames[0u].relativeBmpPath =
            "captures/crossing-triangles/2x/reference/other.bmp";
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidCaptureJob);

        mutation = accepted;
        mutation.comparisons[0u].id = mutation.comparisons[1u].id;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::DuplicateId);

        mutation = accepted;
        mutation.comparisons[0u].id = mutation.captureJobs[0u].id;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::DuplicateId);

        mutation = accepted;
        mutation.comparisons[0u].referenceCaptureJobIndex = 1u;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidComparison);

        mutation = accepted;
        mutation.comparisons[0u].candidateImplementation =
            AdaptiveMsaaValidationImplementation::Reference;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidImplementation);

        mutation = accepted;
        mutation.comparisons[0u].sampleCount = 32u;
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidSampleCount);

        mutation = accepted;
        mutation.captureJobs.pop_back();
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidCaptureJob);

        mutation = accepted;
        mutation.comparisons.pop_back();
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidComparison);

        mutation = accepted;
        std::swap(mutation.comparisons[0u], mutation.comparisons[1u]);
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidComparison);

        mutation = accepted;
        std::swap(mutation.captureJobs[0u], mutation.captureJobs[1u]);
        ExpectPlanFailure(
            mutation,
            AdaptiveMsaaValidationPlanReason::InvalidComparison);
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(
            !AdaptiveMsaaValidationPlanStatus{}.Succeeded() &&
                !AdaptiveMsaaValidationPlanResult{}.Succeeded(),
            "default validation-plan state did not fail closed");
        Require(
            argc == 2,
            "usage: adaptive_msaa_validation_plan_tests <image-diff-recipe>");
        const std::filesystem::path recipePath =
            std::filesystem::path(argv[1]).lexically_normal();
        const AdaptiveMsaaValidationPlanResult accepted =
            LoadAdaptiveMsaaValidationPlan(
                recipePath,
                AdaptiveMsaaValidationRecipeSha256);
        Require(
            accepted.Succeeded(),
            std::string("canonical validation recipe failed: ") +
                GetAdaptiveMsaaValidationPlanReasonName(
                    accepted.status.reason));
        ValidateGeneratedGraph(accepted.plan);

        const AdaptiveMsaaValidationPlanResult regenerated =
            LoadAdaptiveMsaaValidationPlan(
                recipePath,
                AdaptiveMsaaValidationRecipeSha256);
        Require(
            regenerated.Succeeded() &&
                PlanFingerprint(regenerated.plan) ==
                    PlanFingerprint(accepted.plan),
            "validation plan regeneration was not deterministic");

        ValidateRecipeRejections(recipePath, accepted);
        ValidatePlanRejections(accepted.plan);

        Require(
            std::string(GetAdaptiveMsaaValidationPlanImplementationName(
                AdaptiveMsaaValidationImplementation::Reference)) ==
                    "reference" &&
                std::string(GetAdaptiveMsaaValidationPlanImplementationName(
                    AdaptiveMsaaValidationImplementation::AdaptiveGBuffer)) ==
                    "adaptive-g-buffer" &&
                std::string(GetAdaptiveMsaaValidationPlanImplementationName(
                    AdaptiveMsaaValidationImplementation::CompactVisibility)) ==
                    "compact-visibility" &&
                std::string(GetAdaptiveMsaaValidationPlanImplementationName(
                    static_cast<AdaptiveMsaaValidationImplementation>(99u))) ==
                    "invalid",
            "implementation names differ from receipt vocabulary");
    }
    catch (const std::exception& error)
    {
        std::cerr << "Adaptive MSAA validation plan tests failed: "
            << error.what() << '\n';
        return 1;
    }

    std::cout << "Adaptive MSAA validation plan tests passed\n";
    return 0;
}
