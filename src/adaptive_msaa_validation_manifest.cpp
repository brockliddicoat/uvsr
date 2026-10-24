#include "adaptive_msaa_validation_manifest.h"

#include <donut/core/json.h>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace uvsr::adaptive_msaa
{
    namespace
    {
        struct FileIdentity
        {
            DWORD volumeSerialNumber = 0u;
            DWORD fileIndexHigh = 0u;
            DWORD fileIndexLow = 0u;

            [[nodiscard]] bool operator<(const FileIdentity& other) const
                noexcept
            {
                return std::tie(
                        volumeSerialNumber,
                        fileIndexHigh,
                        fileIndexLow) <
                    std::tie(
                        other.volumeSerialNumber,
                        other.fileIndexHigh,
                        other.fileIndexLow);
            }
        };

        std::atomic<uint64_t> g_TemporaryCounter{ 1u };

        [[nodiscard]] AdaptiveMsaaValidationManifestStatus Fail(
            AdaptiveMsaaValidationManifestReason reason,
            uint32_t systemError = 0u,
            size_t itemIndex = 0u,
            uint32_t phaseIndex = 0u,
            bool evidenceIsReceipt = false)
        {
            AdaptiveMsaaValidationManifestStatus status;
            status.reason = reason;
            status.planReason = AdaptiveMsaaValidationPlanReason::None;
            status.itemIndex = itemIndex;
            status.phaseIndex = phaseIndex;
            status.evidenceIsReceipt = evidenceIsReceipt;
            status.systemError = systemError;
            return status;
        }

        [[nodiscard]] AdaptiveMsaaValidationManifestStatus PlanFailure(
            const AdaptiveMsaaValidationPlanStatus& planStatus)
        {
            AdaptiveMsaaValidationManifestStatus status = Fail(
                AdaptiveMsaaValidationManifestReason::InvalidPlan,
                planStatus.systemError,
                planStatus.itemIndex);
            status.planReason = planStatus.reason;
            return status;
        }

        [[nodiscard]] AdaptiveMsaaValidationManifestStatus Success(
            size_t byteCount)
        {
            AdaptiveMsaaValidationManifestStatus status;
            status.reason = AdaptiveMsaaValidationManifestReason::None;
            status.planReason = AdaptiveMsaaValidationPlanReason::None;
            status.byteCount = byteCount;
            return status;
        }

        [[nodiscard]] bool IsStrictlyBelowRoot(
            const std::filesystem::path& path,
            const std::filesystem::path& prefix)
        {
            const auto PathComponentEqual = [](
                const std::filesystem::path& left,
                const std::filesystem::path& right)
            {
                const std::wstring leftString = left.native();
                const std::wstring rightString = right.native();
                return CompareStringOrdinal(
                        leftString.data(),
                        static_cast<int>(leftString.size()),
                        rightString.data(),
                        static_cast<int>(rightString.size()),
                        TRUE) == CSTR_EQUAL;
            };

            auto pathIterator = path.begin();
            for (auto prefixIterator = prefix.begin();
                prefixIterator != prefix.end();
                ++prefixIterator, ++pathIterator)
            {
                if (pathIterator == path.end() ||
                    !PathComponentEqual(*pathIterator, *prefixIterator))
                {
                    return false;
                }
            }
            return pathIterator != path.end();
        }

        [[nodiscard]] bool GetIdentity(
            const std::filesystem::path& path,
            FileIdentity& identity,
            uint32_t& systemError)
        {
            HANDLE file = CreateFileW(
                path.c_str(),
                FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (file == INVALID_HANDLE_VALUE)
            {
                systemError = GetLastError();
                return false;
            }

            BY_HANDLE_FILE_INFORMATION information{};
            const bool succeeded = GetFileInformationByHandle(
                file,
                &information) != FALSE;
            systemError = succeeded ? 0u : GetLastError();
            CloseHandle(file);
            if (!succeeded)
                return false;

            identity.volumeSerialNumber = information.dwVolumeSerialNumber;
            identity.fileIndexHigh = information.nFileIndexHigh;
            identity.fileIndexLow = information.nFileIndexLow;
            return true;
        }

        [[nodiscard]] AdaptiveMsaaValidationManifestStatus ValidateEvidence(
            const std::filesystem::path& canonicalRoot,
            const std::filesystem::path& relativePath,
            size_t itemIndex,
            uint32_t phaseIndex,
            bool isReceipt,
            std::set<FileIdentity>& identities,
            const FileIdentity* destinationIdentity)
        {
            std::filesystem::path candidate;
            try
            {
                candidate = canonicalRoot / relativePath;
            }
            catch (...)
            {
                return Fail(
                    AdaptiveMsaaValidationManifestReason::UnsafeEvidencePath,
                    0u,
                    itemIndex,
                    phaseIndex,
                    isReceipt);
            }

            std::error_code error;
            const std::filesystem::file_status linkStatus =
                std::filesystem::symlink_status(candidate, error);
            if (error || !std::filesystem::exists(linkStatus))
            {
                return Fail(
                    AdaptiveMsaaValidationManifestReason::EvidenceMissing,
                    static_cast<uint32_t>(error.value()),
                    itemIndex,
                    phaseIndex,
                    isReceipt);
            }
            if (std::filesystem::is_symlink(linkStatus) ||
                !std::filesystem::is_regular_file(linkStatus))
            {
                return Fail(
                    AdaptiveMsaaValidationManifestReason::
                        EvidenceNotRegularFile,
                    static_cast<uint32_t>(error.value()),
                    itemIndex,
                    phaseIndex,
                    isReceipt);
            }

            const uintmax_t fileSize =
                std::filesystem::file_size(candidate, error);
            if (error)
            {
                return Fail(
                    AdaptiveMsaaValidationManifestReason::
                        EvidenceNotRegularFile,
                    static_cast<uint32_t>(error.value()),
                    itemIndex,
                    phaseIndex,
                    isReceipt);
            }
            if (fileSize == 0u)
            {
                return Fail(
                    AdaptiveMsaaValidationManifestReason::EvidenceEmpty,
                    0u,
                    itemIndex,
                    phaseIndex,
                    isReceipt);
            }

            const std::filesystem::path canonicalCandidate =
                std::filesystem::canonical(candidate, error);
            if (error || canonicalCandidate.empty() ||
                !IsStrictlyBelowRoot(canonicalCandidate, canonicalRoot))
            {
                return Fail(
                    AdaptiveMsaaValidationManifestReason::UnsafeEvidencePath,
                    static_cast<uint32_t>(error.value()),
                    itemIndex,
                    phaseIndex,
                    isReceipt);
            }

            FileIdentity identity;
            uint32_t systemError = 0u;
            if (!GetIdentity(canonicalCandidate, identity, systemError))
            {
                return Fail(
                    AdaptiveMsaaValidationManifestReason::
                        EvidenceNotRegularFile,
                    systemError,
                    itemIndex,
                    phaseIndex,
                    isReceipt);
            }
            if ((destinationIdentity != nullptr &&
                    !(identity < *destinationIdentity) &&
                    !(*destinationIdentity < identity)) ||
                !identities.insert(identity).second)
            {
                return Fail(
                    destinationIdentity != nullptr &&
                            !(identity < *destinationIdentity) &&
                            !(*destinationIdentity < identity)
                        ? AdaptiveMsaaValidationManifestReason::
                            DestinationCollision
                        : AdaptiveMsaaValidationManifestReason::
                            EvidenceCollision,
                    0u,
                    itemIndex,
                    phaseIndex,
                    isReceipt);
            }
            return Success(0u);
        }

        [[nodiscard]] Json::Value MakeRoiJson(
            const AdaptiveMsaaValidationPlanRoi& roi)
        {
            Json::Value value(Json::objectValue);
            value["x"] = roi.x;
            value["y"] = roi.y;
            value["width"] = roi.width;
            value["height"] = roi.height;
            return value;
        }

        [[nodiscard]] Json::Value MakeManifestJson(
            const AdaptiveMsaaValidationPlan& plan)
        {
            Json::Value root(Json::objectValue);
            root["schema"] = AdaptiveMsaaValidationManifestSchema;
            root["suiteId"] = plan.recipe.suiteId;

            Json::Value phases(Json::arrayValue);
            for (double phase : plan.recipe.phases)
                phases.append(phase);
            root["phases"] = std::move(phases);

            Json::Value runs(Json::arrayValue);
            for (const AdaptiveMsaaValidationComparison& comparison :
                plan.comparisons)
            {
                const AdaptiveMsaaValidationCaptureJob& reference =
                    plan.captureJobs[
                        comparison.referenceCaptureJobIndex];
                const AdaptiveMsaaValidationCaptureJob& candidate =
                    plan.captureJobs[
                        comparison.candidateCaptureJobIndex];
                const AdaptiveMsaaValidationPlanCase& validationCase =
                    plan.recipe.cases[candidate.caseIndex];

                Json::Value run(Json::objectValue);
                run["implementation"] =
                    GetAdaptiveMsaaValidationPlanImplementationName(
                        comparison.candidateImplementation);
                run["sampleCount"] = comparison.sampleCount;
                run["case"] = comparison.caseId;
                run["recipeSha256"] = plan.recipe.sha256;
                run["roi"] = MakeRoiJson(validationCase.roi);

                Json::Value frames(Json::arrayValue);
                for (size_t phaseIndex = 0u;
                    phaseIndex < AdaptiveMsaaValidationPhaseCount;
                    ++phaseIndex)
                {
                    Json::Value frame(Json::objectValue);
                    frame["phaseIndex"] = static_cast<uint32_t>(phaseIndex);
                    frame["referenceReceipt"] =
                        reference.frames[phaseIndex]
                            .relativeReceiptPath.generic_string();
                    frame["candidateReceipt"] =
                        candidate.frames[phaseIndex]
                            .relativeReceiptPath.generic_string();
                    frames.append(std::move(frame));
                }
                run["frames"] = std::move(frames);
                runs.append(std::move(run));
            }
            root["runs"] = std::move(runs);
            return root;
        }

        [[nodiscard]] AdaptiveMsaaValidationManifestStatus Serialize(
            const AdaptiveMsaaValidationPlan& plan,
            std::string& serialized)
        {
            try
            {
                Json::StreamWriterBuilder builder;
                builder["indentation"] = "";
                builder["commentStyle"] = "None";
                builder["enableYAMLCompatibility"] = false;
                builder["dropNullPlaceholders"] = false;
                builder["emitUTF8"] = true;
                serialized = Json::writeString(
                    builder,
                    MakeManifestJson(plan));
                serialized.push_back('\n');
            }
            catch (...)
            {
                serialized.clear();
                return Fail(
                    AdaptiveMsaaValidationManifestReason::
                        SerializationFailed);
            }
            return Success(0u);
        }

        [[nodiscard]] AdaptiveMsaaValidationManifestStatus AtomicWrite(
            const std::filesystem::path& destination,
            const std::string& bytes)
        {
            std::filesystem::path temporary;
            HANDLE file = INVALID_HANDLE_VALUE;
            for (uint32_t attempt = 0u; attempt < 64u; ++attempt)
            {
                const uint64_t counter = g_TemporaryCounter.fetch_add(
                    1u,
                    std::memory_order_relaxed);
                if (counter == 0u)
                    continue;
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
                        AdaptiveMsaaValidationManifestReason::
                            TemporaryNameExhausted);
                }

                file = CreateFileW(
                    temporary.c_str(),
                    GENERIC_WRITE,
                    0u,
                    nullptr,
                    CREATE_NEW,
                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
                    nullptr);
                if (file != INVALID_HANDLE_VALUE)
                    break;
                const DWORD createError = GetLastError();
                if (createError != ERROR_FILE_EXISTS &&
                    createError != ERROR_ALREADY_EXISTS)
                {
                    return Fail(
                        AdaptiveMsaaValidationManifestReason::
                            AtomicCreateFailed,
                        createError);
                }
            }
            if (file == INVALID_HANDLE_VALUE)
            {
                return Fail(
                    AdaptiveMsaaValidationManifestReason::
                        TemporaryNameExhausted);
            }

            AdaptiveMsaaValidationManifestStatus status = Success(0u);
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
                        AdaptiveMsaaValidationManifestReason::
                            AtomicWriteFailed,
                        GetLastError());
                    break;
                }
                offset += written;
            }
            if (status.Succeeded() && !FlushFileBuffers(file))
            {
                status = Fail(
                    AdaptiveMsaaValidationManifestReason::AtomicFlushFailed,
                    GetLastError());
            }
            if (!CloseHandle(file) && status.Succeeded())
            {
                status = Fail(
                    AdaptiveMsaaValidationManifestReason::AtomicFlushFailed,
                    GetLastError());
            }
            if (status.Succeeded() && !MoveFileExW(
                    temporary.c_str(),
                    destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                status = Fail(
                    AdaptiveMsaaValidationManifestReason::AtomicReplaceFailed,
                    GetLastError());
            }
            if (!status.Succeeded())
                DeleteFileW(temporary.c_str());
            else
                status = Success(bytes.size());
            return status;
        }
    }

    const char* GetAdaptiveMsaaValidationManifestReasonName(
        AdaptiveMsaaValidationManifestReason reason) noexcept
    {
#define UVSR_MANIFEST_REASON_NAME(name) \
    case AdaptiveMsaaValidationManifestReason::name: return #name
        switch (reason)
        {
        UVSR_MANIFEST_REASON_NAME(None);
        UVSR_MANIFEST_REASON_NAME(NotInitialized);
        UVSR_MANIFEST_REASON_NAME(InvalidPlan);
        UVSR_MANIFEST_REASON_NAME(OutputRootMissing);
        UVSR_MANIFEST_REASON_NAME(OutputRootNotAbsolute);
        UVSR_MANIFEST_REASON_NAME(OutputRootNotFound);
        UVSR_MANIFEST_REASON_NAME(OutputRootNotDirectory);
        UVSR_MANIFEST_REASON_NAME(OutputRootCanonicalizationFailed);
        UVSR_MANIFEST_REASON_NAME(OutputRootEnumerationFailed);
        UVSR_MANIFEST_REASON_NAME(UnsafeEvidencePath);
        UVSR_MANIFEST_REASON_NAME(EvidenceMissing);
        UVSR_MANIFEST_REASON_NAME(EvidenceNotRegularFile);
        UVSR_MANIFEST_REASON_NAME(EvidenceEmpty);
        UVSR_MANIFEST_REASON_NAME(EvidenceCollision);
        UVSR_MANIFEST_REASON_NAME(DestinationCollision);
        UVSR_MANIFEST_REASON_NAME(StaleTemporaryFile);
        UVSR_MANIFEST_REASON_NAME(SerializationFailed);
        UVSR_MANIFEST_REASON_NAME(TemporaryNameExhausted);
        UVSR_MANIFEST_REASON_NAME(AtomicCreateFailed);
        UVSR_MANIFEST_REASON_NAME(AtomicWriteFailed);
        UVSR_MANIFEST_REASON_NAME(AtomicFlushFailed);
        UVSR_MANIFEST_REASON_NAME(AtomicReplaceFailed);
        UVSR_MANIFEST_REASON_NAME(AllocationFailed);
        default:
            return "Unknown";
        }
#undef UVSR_MANIFEST_REASON_NAME
    }

    AdaptiveMsaaValidationManifestWriteResult
        WriteAdaptiveMsaaValidationManifest(
            const std::filesystem::path& outputRoot,
            const AdaptiveMsaaValidationPlan& plan)
    {
        AdaptiveMsaaValidationManifestWriteResult result;
        const AdaptiveMsaaValidationPlanStatus planStatus =
            ValidateAdaptiveMsaaValidationPlan(plan);
        if (!planStatus.Succeeded())
        {
            result.status = PlanFailure(planStatus);
            return result;
        }
        if (outputRoot.empty())
        {
            result.status = Fail(
                AdaptiveMsaaValidationManifestReason::OutputRootMissing);
            return result;
        }
        if (!outputRoot.is_absolute())
        {
            result.status = Fail(
                AdaptiveMsaaValidationManifestReason::
                    OutputRootNotAbsolute);
            return result;
        }

        try
        {
            std::error_code error;
            const std::filesystem::file_status rootLinkStatus =
                std::filesystem::symlink_status(outputRoot, error);
            if (error || !std::filesystem::exists(rootLinkStatus))
            {
                result.status = Fail(
                    AdaptiveMsaaValidationManifestReason::OutputRootNotFound,
                    static_cast<uint32_t>(error.value()));
                return result;
            }
            if (!std::filesystem::is_directory(rootLinkStatus))
            {
                result.status = Fail(
                    AdaptiveMsaaValidationManifestReason::
                        OutputRootNotDirectory);
                return result;
            }

            const std::filesystem::path canonicalRoot =
                std::filesystem::canonical(outputRoot, error);
            if (error || canonicalRoot.empty())
            {
                result.status = Fail(
                    AdaptiveMsaaValidationManifestReason::
                        OutputRootCanonicalizationFailed,
                    static_cast<uint32_t>(error.value()));
                return result;
            }
            const std::filesystem::path destination =
                canonicalRoot /
                std::filesystem::path(
                    std::string(AdaptiveMsaaValidationManifestFileName));

            const std::string temporaryPrefix =
                std::string(AdaptiveMsaaValidationManifestFileName) +
                ".tmp.";
            for (std::filesystem::directory_iterator iterator(
                    canonicalRoot,
                    error),
                end;
                !error && iterator != end;
                iterator.increment(error))
            {
                const std::string filename =
                    iterator->path().filename().generic_string();
                if (filename.compare(
                        0u,
                        temporaryPrefix.size(),
                        temporaryPrefix) == 0)
                {
                    result.status = Fail(
                        AdaptiveMsaaValidationManifestReason::
                            StaleTemporaryFile);
                    return result;
                }
            }
            if (error)
            {
                result.status = Fail(
                    AdaptiveMsaaValidationManifestReason::
                        OutputRootEnumerationFailed,
                    static_cast<uint32_t>(error.value()));
                return result;
            }

            FileIdentity destinationIdentity;
            FileIdentity* destinationIdentityPointer = nullptr;
            const DWORD destinationAttributes =
                GetFileAttributesW(destination.c_str());
            const bool destinationExists =
                destinationAttributes != INVALID_FILE_ATTRIBUTES;
            const DWORD destinationError =
                destinationExists ? ERROR_SUCCESS : GetLastError();
            if (!destinationExists &&
                destinationError != ERROR_FILE_NOT_FOUND &&
                destinationError != ERROR_PATH_NOT_FOUND)
            {
                result.status = Fail(
                    AdaptiveMsaaValidationManifestReason::
                        DestinationCollision,
                    destinationError);
                return result;
            }
            if (destinationExists)
            {
                if ((destinationAttributes &
                        (FILE_ATTRIBUTE_DIRECTORY |
                            FILE_ATTRIBUTE_REPARSE_POINT)) != 0u)
                {
                    result.status = Fail(
                        AdaptiveMsaaValidationManifestReason::
                            DestinationCollision);
                    return result;
                }
                uint32_t systemError = 0u;
                if (!GetIdentity(
                        destination,
                        destinationIdentity,
                        systemError))
                {
                    result.status = Fail(
                        AdaptiveMsaaValidationManifestReason::
                            DestinationCollision,
                        systemError);
                    return result;
                }
                destinationIdentityPointer = &destinationIdentity;
            }

            std::set<FileIdentity> evidenceIdentities;
            for (size_t itemIndex = 0u;
                itemIndex < plan.captureJobs.size();
                ++itemIndex)
            {
                const AdaptiveMsaaValidationCaptureJob& job =
                    plan.captureJobs[itemIndex];
                for (const AdaptiveMsaaValidationCaptureFrame& frame :
                    job.frames)
                {
                    result.status = ValidateEvidence(
                        canonicalRoot,
                        frame.relativeBmpPath,
                        itemIndex,
                        frame.phaseIndex,
                        false,
                        evidenceIdentities,
                        destinationIdentityPointer);
                    if (!result.status.Succeeded())
                        return result;
                    result.status = ValidateEvidence(
                        canonicalRoot,
                        frame.relativeReceiptPath,
                        itemIndex,
                        frame.phaseIndex,
                        true,
                        evidenceIdentities,
                        destinationIdentityPointer);
                    if (!result.status.Succeeded())
                        return result;
                }
            }

            std::string serialized;
            result.status = Serialize(plan, serialized);
            if (!result.status.Succeeded())
                return result;
            result.status = AtomicWrite(destination, serialized);
            if (!result.status.Succeeded())
                return result;
            result.manifestPath = destination;
            return result;
        }
        catch (...)
        {
            result.status = Fail(
                AdaptiveMsaaValidationManifestReason::AllocationFailed);
            result.manifestPath.clear();
            return result;
        }
    }
}
