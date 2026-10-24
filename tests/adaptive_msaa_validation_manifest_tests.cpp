#include "adaptive_msaa_validation_manifest.h"

#include <donut/core/json.h>

#include <Windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace
{
    using namespace uvsr::adaptive_msaa;

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            static std::atomic<uint64_t> counter{ 1u };
            m_Path = std::filesystem::temp_directory_path() /
                ("uvsr-adaptive-msaa-validation-manifest-tests-" +
                    std::to_string(GetCurrentProcessId()) + "-" +
                    std::to_string(GetTickCount64()) + "-" +
                    std::to_string(counter.fetch_add(1u)));
            std::filesystem::create_directories(m_Path);
            m_Path = std::filesystem::canonical(m_Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_Path, error);
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& GetPath() const noexcept
        {
            return m_Path;
        }

    private:
        std::filesystem::path m_Path;
    };

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void WriteBytes(
        const std::filesystem::path& path,
        const std::string& bytes)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(
            path,
            std::ios::binary | std::ios::trunc);
        Require(bool(output), "could not create test evidence: " +
            path.generic_string());
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        Require(bool(output), "could not write test evidence: " +
            path.generic_string());
    }

    [[nodiscard]] std::string ReadBytes(
        const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(bool(input), "could not read test output: " +
            path.generic_string());
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }

    [[nodiscard]] size_t PopulateEvidence(
        const std::filesystem::path& root,
        const AdaptiveMsaaValidationPlan& plan)
    {
        size_t evidenceCount = 0u;
        for (const AdaptiveMsaaValidationCaptureJob& job : plan.captureJobs)
        {
            for (const AdaptiveMsaaValidationCaptureFrame& frame : job.frames)
            {
                WriteBytes(root / frame.relativeBmpPath, "BM-test-image\n");
                WriteBytes(
                    root / frame.relativeReceiptPath,
                    "{\"test\":true}\n");
                evidenceCount += 2u;
            }
        }
        return evidenceCount;
    }

    [[nodiscard]] Json::Value ParseJson(
        const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        Require(bool(input), "could not open manifest JSON");
        Json::CharReaderBuilder builder;
        builder["collectComments"] = false;
        builder["allowComments"] = false;
        builder["failIfExtra"] = true;
        Json::Value root;
        std::string errors;
        Require(
            Json::parseFromStream(builder, input, &root, &errors),
            "could not parse manifest JSON: " + errors);
        return root;
    }

    void RequireExactFields(
        const Json::Value& value,
        std::initializer_list<const char*> expected,
        const std::string& context)
    {
        Require(value.isObject(), context + " is not an object");
        const std::vector<std::string> memberNames = value.getMemberNames();
        std::set<std::string> actual(
            memberNames.begin(),
            memberNames.end());
        std::set<std::string> required;
        for (const char* field : expected)
            required.insert(field);
        Require(actual == required, context + " has unexpected fields");
    }

    [[nodiscard]] bool HasManifestTemporary(
        const std::filesystem::path& root)
    {
        const std::string prefix =
            std::string(AdaptiveMsaaValidationManifestFileName) + ".tmp.";
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator(root))
        {
            const std::string filename =
                entry.path().filename().generic_string();
            if (filename.compare(0u, prefix.size(), prefix) == 0)
                return true;
        }
        return false;
    }

    void VerifyManifest(
        const Json::Value& root,
        const AdaptiveMsaaValidationPlan& plan)
    {
        RequireExactFields(
            root,
            { "schema", "suiteId", "phases", "runs" },
            "manifest");
        Require(
            root["schema"].isUInt() &&
                root["schema"].asUInt() ==
                    AdaptiveMsaaValidationManifestSchema,
            "manifest schema is not exactly 2");
        Require(
            root["suiteId"].asString() == plan.recipe.suiteId,
            "manifest suite ID differs from the plan");
        Require(
            root["phases"].isArray() &&
                root["phases"].size() ==
                    AdaptiveMsaaValidationPhaseCount,
            "manifest phase count is not exactly two");
        for (size_t phaseIndex = 0u;
            phaseIndex < AdaptiveMsaaValidationPhaseCount;
            ++phaseIndex)
        {
            Require(
                root["phases"][static_cast<Json::ArrayIndex>(phaseIndex)]
                        .asDouble() == plan.recipe.phases[phaseIndex],
                "manifest phase value differs from the recipe");
        }

        const Json::Value& runs = root["runs"];
        Require(
            runs.isArray() &&
                runs.size() == AdaptiveMsaaValidationComparisonCount,
            "manifest does not contain exactly 120 runs");
        std::set<std::tuple<std::string, uint32_t, std::string>> identities;
        std::vector<size_t> referenceReuse(
            plan.captureJobs.size(),
            0u);
        for (size_t runIndex = 0u;
            runIndex < plan.comparisons.size();
            ++runIndex)
        {
            const AdaptiveMsaaValidationComparison& comparison =
                plan.comparisons[runIndex];
            const AdaptiveMsaaValidationCaptureJob& reference =
                plan.captureJobs[
                    comparison.referenceCaptureJobIndex];
            const AdaptiveMsaaValidationCaptureJob& candidate =
                plan.captureJobs[
                    comparison.candidateCaptureJobIndex];
            const AdaptiveMsaaValidationPlanCase& validationCase =
                plan.recipe.cases[candidate.caseIndex];
            const Json::Value& run =
                runs[static_cast<Json::ArrayIndex>(runIndex)];

            RequireExactFields(
                run,
                { "implementation", "sampleCount", "case",
                    "recipeSha256", "roi", "frames" },
                "manifest run");
            const std::string implementation =
                GetAdaptiveMsaaValidationPlanImplementationName(
                    comparison.candidateImplementation);
            Require(
                run["implementation"].asString() == implementation,
                "run implementation differs from the comparison");
            Require(
                run["sampleCount"].asUInt() == comparison.sampleCount,
                "run sample count differs from the comparison");
            Require(
                run["case"].asString() == comparison.caseId,
                "run case differs from the comparison");
            Require(
                run["recipeSha256"].asString() == plan.recipe.sha256,
                "run recipe hash differs from the plan");
            Require(
                identities.emplace(
                    implementation,
                    comparison.sampleCount,
                    comparison.caseId).second,
                "manifest contains a duplicate run identity");
            ++referenceReuse[comparison.referenceCaptureJobIndex];

            const Json::Value& roi = run["roi"];
            RequireExactFields(
                roi,
                { "x", "y", "width", "height" },
                "manifest run ROI");
            Require(
                roi["x"].asUInt() == validationCase.roi.x &&
                    roi["y"].asUInt() == validationCase.roi.y &&
                    roi["width"].asUInt() == validationCase.roi.width &&
                    roi["height"].asUInt() == validationCase.roi.height,
                "run ROI differs from the recipe case");

            const Json::Value& frames = run["frames"];
            Require(
                frames.isArray() &&
                    frames.size() == AdaptiveMsaaValidationPhaseCount,
                "run frame count is not exactly two");
            for (size_t phaseIndex = 0u;
                phaseIndex < AdaptiveMsaaValidationPhaseCount;
                ++phaseIndex)
            {
                const Json::Value& frame = frames[
                    static_cast<Json::ArrayIndex>(phaseIndex)];
                RequireExactFields(
                    frame,
                    { "phaseIndex", "referenceReceipt",
                        "candidateReceipt" },
                    "manifest frame");
                const std::string referencePath =
                    frame["referenceReceipt"].asString();
                const std::string candidatePath =
                    frame["candidateReceipt"].asString();
                Require(
                    frame["phaseIndex"].asUInt() == phaseIndex,
                    "frame phase index is not ordered zero, one");
                Require(
                    referencePath ==
                        reference.frames[phaseIndex]
                            .relativeReceiptPath.generic_string(),
                    "reference receipt link differs from the plan");
                Require(
                    candidatePath ==
                        candidate.frames[phaseIndex]
                            .relativeReceiptPath.generic_string(),
                    "candidate receipt link differs from the plan");
                Require(
                    !std::filesystem::path(referencePath).is_absolute() &&
                        !std::filesystem::path(candidatePath).is_absolute(),
                    "manifest receipt links must remain relative");
            }
        }
        Require(
            identities.size() == AdaptiveMsaaValidationComparisonCount,
            "manifest run matrix is incomplete");
        for (size_t index = 0u;
            index < AdaptiveMsaaValidationReferenceJobCount;
            ++index)
        {
            Require(
                referenceReuse[index] == 2u,
                "reference receipts were not reused narrowly by both modes");
        }
    }

    void ExpectFailure(
        const AdaptiveMsaaValidationManifestWriteResult& result,
        AdaptiveMsaaValidationManifestReason expected,
        const std::string& context)
    {
        Require(!result.Succeeded(), context + " unexpectedly succeeded");
        Require(
            result.status.reason == expected,
            context + " returned " +
                GetAdaptiveMsaaValidationManifestReasonName(
                    result.status.reason) +
                ", expected " +
                GetAdaptiveMsaaValidationManifestReasonName(expected));
        Require(
            result.manifestPath.empty(),
            context + " exposed a success path on failure");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "expected the real validation recipe path");
        const AdaptiveMsaaValidationPlanResult loaded =
            LoadAdaptiveMsaaValidationPlan(
                std::filesystem::absolute(
                    std::filesystem::u8path(argv[1])),
                AdaptiveMsaaValidationRecipeSha256);
        Require(
            loaded.Succeeded(),
            std::string("could not load the real validation recipe: ") +
                GetAdaptiveMsaaValidationPlanReasonName(
                    loaded.status.reason));
        const AdaptiveMsaaValidationPlan& plan = loaded.plan;

        const AdaptiveMsaaValidationManifestStatus defaultStatus;
        Require(
            !defaultStatus.Succeeded() &&
                defaultStatus.reason ==
                    AdaptiveMsaaValidationManifestReason::NotInitialized,
            "default manifest status did not fail closed");
        for (uint32_t value = 0u;
            value <= static_cast<uint32_t>(
                AdaptiveMsaaValidationManifestReason::AllocationFailed);
            ++value)
        {
            Require(
                std::string(GetAdaptiveMsaaValidationManifestReasonName(
                    static_cast<AdaptiveMsaaValidationManifestReason>(
                        value))) != "Unknown",
                "manifest reason is missing its diagnostic name");
        }

        TemporaryDirectory temporary;
        const std::filesystem::path& root = temporary.GetPath();
        Require(
            PopulateEvidence(root, plan) ==
                AdaptiveMsaaValidationCaptureJobCount *
                    AdaptiveMsaaValidationPhaseCount * 2u,
            "test fixture did not create all 720 evidence files");
        const std::filesystem::path manifestPath =
            root / std::filesystem::path(
                std::string(AdaptiveMsaaValidationManifestFileName));

        WriteBytes(manifestPath, "prior-manifest\n");
        AdaptiveMsaaValidationManifestWriteResult write =
            WriteAdaptiveMsaaValidationManifest(root, plan);
        Require(
            write.Succeeded(),
            std::string("manifest write failed: ") +
                GetAdaptiveMsaaValidationManifestReasonName(
                    write.status.reason));
        Require(
            write.manifestPath == manifestPath,
            "writer returned the wrong fixed manifest path");
        const std::string firstBytes = ReadBytes(manifestPath);
        Require(
            !firstBytes.empty() && firstBytes.back() == '\n' &&
                firstBytes.find("prior-manifest") == std::string::npos &&
                firstBytes.find("\n  ") == std::string::npos,
            "manifest is not compact deterministic JSON");
        Require(
            write.status.byteCount == firstBytes.size(),
            "manifest status byte count differs from the output");
        Require(!HasManifestTemporary(root),
            "successful replacement left a temporary file");
        VerifyManifest(ParseJson(manifestPath), plan);

        write = WriteAdaptiveMsaaValidationManifest(root, plan);
        Require(write.Succeeded(), "deterministic rewrite failed");
        Require(
            ReadBytes(manifestPath) == firstBytes,
            "identical plans did not serialize deterministically");
        Require(!HasManifestTemporary(root),
            "deterministic rewrite left a temporary file");

        AdaptiveMsaaValidationPlan invalidPlan = plan;
        invalidPlan.comparisons.pop_back();
        AdaptiveMsaaValidationManifestWriteResult failure =
            WriteAdaptiveMsaaValidationManifest(root, invalidPlan);
        ExpectFailure(
            failure,
            AdaptiveMsaaValidationManifestReason::InvalidPlan,
            "invalid plan");
        Require(
            failure.status.planReason ==
                AdaptiveMsaaValidationPlanReason::InvalidComparison,
            "invalid plan did not preserve the plan reason");

        ExpectFailure(
            WriteAdaptiveMsaaValidationManifest({}, plan),
            AdaptiveMsaaValidationManifestReason::OutputRootMissing,
            "empty output root");
        ExpectFailure(
            WriteAdaptiveMsaaValidationManifest(
                std::filesystem::path("relative-output-root"),
                plan),
            AdaptiveMsaaValidationManifestReason::OutputRootNotAbsolute,
            "relative output root");
        ExpectFailure(
            WriteAdaptiveMsaaValidationManifest(root / "missing-root", plan),
            AdaptiveMsaaValidationManifestReason::OutputRootNotFound,
            "missing output root");
        const std::filesystem::path rootFile = root / "not-a-directory";
        WriteBytes(rootFile, "file\n");
        ExpectFailure(
            WriteAdaptiveMsaaValidationManifest(rootFile, plan),
            AdaptiveMsaaValidationManifestReason::OutputRootNotDirectory,
            "non-directory output root");

        const AdaptiveMsaaValidationCaptureFrame& firstFrame =
            plan.captureJobs[0u].frames[0u];
        const std::filesystem::path firstBmp =
            root / firstFrame.relativeBmpPath;
        const std::filesystem::path firstReceipt =
            root / firstFrame.relativeReceiptPath;

        std::filesystem::remove(firstReceipt);
        failure = WriteAdaptiveMsaaValidationManifest(root, plan);
        ExpectFailure(
            failure,
            AdaptiveMsaaValidationManifestReason::EvidenceMissing,
            "missing receipt");
        Require(
            failure.status.itemIndex == 0u &&
                failure.status.phaseIndex == 0u &&
                failure.status.evidenceIsReceipt,
            "missing receipt diagnostics identify the wrong evidence");
        WriteBytes(firstReceipt, "{\"test\":true}\n");

        WriteBytes(firstBmp, "");
        failure = WriteAdaptiveMsaaValidationManifest(root, plan);
        ExpectFailure(
            failure,
            AdaptiveMsaaValidationManifestReason::EvidenceEmpty,
            "empty BMP");
        Require(
            failure.status.itemIndex == 0u &&
                failure.status.phaseIndex == 0u &&
                !failure.status.evidenceIsReceipt,
            "empty BMP diagnostics identify the wrong evidence");
        WriteBytes(firstBmp, "BM-test-image\n");

        std::filesystem::remove(firstReceipt);
        std::filesystem::create_directory(firstReceipt);
        failure = WriteAdaptiveMsaaValidationManifest(root, plan);
        ExpectFailure(
            failure,
            AdaptiveMsaaValidationManifestReason::EvidenceNotRegularFile,
            "non-regular receipt");
        std::filesystem::remove(firstReceipt);
        WriteBytes(firstReceipt, "{\"test\":true}\n");

        std::filesystem::remove(firstReceipt);
        Require(
            CreateHardLinkW(firstReceipt.c_str(), firstBmp.c_str(), nullptr) !=
                FALSE,
            "could not construct hard-link evidence collision");
        failure = WriteAdaptiveMsaaValidationManifest(root, plan);
        ExpectFailure(
            failure,
            AdaptiveMsaaValidationManifestReason::EvidenceCollision,
            "aliased evidence");
        std::filesystem::remove(firstReceipt);
        WriteBytes(firstReceipt, "{\"test\":true}\n");

        std::filesystem::remove(manifestPath);
        Require(
            CreateHardLinkW(manifestPath.c_str(), firstBmp.c_str(), nullptr) !=
                FALSE,
            "could not construct destination collision");
        failure = WriteAdaptiveMsaaValidationManifest(root, plan);
        ExpectFailure(
            failure,
            AdaptiveMsaaValidationManifestReason::DestinationCollision,
            "destination/evidence collision");
        std::filesystem::remove(manifestPath);

        const std::filesystem::path staleTemporary =
            root /
            (std::string(AdaptiveMsaaValidationManifestFileName) +
                ".tmp.stale");
        WriteBytes(staleTemporary, "stale\n");
        failure = WriteAdaptiveMsaaValidationManifest(root, plan);
        ExpectFailure(
            failure,
            AdaptiveMsaaValidationManifestReason::StaleTemporaryFile,
            "stale temporary");
        std::filesystem::remove(staleTemporary);

        std::filesystem::create_directory(manifestPath);
        failure = WriteAdaptiveMsaaValidationManifest(root, plan);
        ExpectFailure(
            failure,
            AdaptiveMsaaValidationManifestReason::DestinationCollision,
            "directory destination collision");
        std::filesystem::remove(manifestPath);

        WriteBytes(manifestPath, "preserve-this-manifest\n");
        const DWORD originalAttributes =
            GetFileAttributesW(manifestPath.c_str());
        Require(
            originalAttributes != INVALID_FILE_ATTRIBUTES &&
                SetFileAttributesW(
                    manifestPath.c_str(),
                    originalAttributes | FILE_ATTRIBUTE_READONLY) != FALSE,
            "could not make the prior manifest read-only");
        failure = WriteAdaptiveMsaaValidationManifest(root, plan);
        const DWORD restoreError = SetFileAttributesW(
            manifestPath.c_str(),
            originalAttributes);
        Require(restoreError != FALSE,
            "could not restore prior manifest attributes");
        ExpectFailure(
            failure,
            AdaptiveMsaaValidationManifestReason::AtomicReplaceFailed,
            "read-only atomic replacement");
        Require(
            ReadBytes(manifestPath) == "preserve-this-manifest\n",
            "failed atomic replacement modified the prior manifest");
        Require(!HasManifestTemporary(root),
            "failed atomic replacement left a temporary file");

        write = WriteAdaptiveMsaaValidationManifest(root, plan);
        Require(write.Succeeded(), "final atomic replacement failed");
        VerifyManifest(ParseJson(manifestPath), plan);
        Require(!HasManifestTemporary(root),
            "final atomic replacement left a temporary file");

        std::cout <<
            "adaptive MSAA validation manifest tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr <<
            "adaptive MSAA validation manifest tests failed: " <<
            exception.what() << '\n';
        return 1;
    }
}
