#include "adaptive_msaa_validation_capture.h"
#include "adaptive_msaa_texture_qualification.h"

#include <donut/core/json.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace uvsr::adaptive_msaa;

namespace
{
    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            static uint64_t sequence = 0u;
            m_Path = std::filesystem::temp_directory_path() /
                ("uvsr-adaptive-msaa-receipt-" +
                    std::to_string(GetCurrentProcessId()) + "-" +
                    std::to_string(++sequence));
            std::filesystem::create_directories(m_Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(m_Path, ignored);
        }

        const std::filesystem::path& Path() const
        {
            return m_Path;
        }

    private:
        std::filesystem::path m_Path;
    };

    [[nodiscard]] std::vector<uint8_t> MakeBmpBytes(uint8_t marker)
    {
        // The receipt writer deliberately owns hashing, not BMP decoding. Keep
        // a recognizable BITMAPFILEHEADER prefix and deterministic payload.
        return {
            uint8_t('B'), uint8_t('M'), 18u, 0u, 0u, 0u, 0u, 0u, 0u,
            0u, 14u, 0u, 0u, 0u, marker, 0x55u, 0xaau, 0xffu
        };
    }

    void WriteBytes(
        const std::filesystem::path& path,
        const std::vector<uint8_t>& bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        assert(output);
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        assert(output);
    }

    [[nodiscard]] std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        assert(input);
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }

    [[nodiscard]] Json::Value ReadJson(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        assert(input);
        Json::Value root;
        input >> root;
        assert(input && root.isObject());
        return root;
    }

    [[nodiscard]] std::string LowerHex(
        const TextureQualificationSha256& digest)
    {
        static constexpr char Digits[] = "0123456789abcdef";
        std::string result(digest.size() * 2u, '0');
        for (size_t index = 0u; index < digest.size(); ++index)
        {
            result[index * 2u] = Digits[digest[index] >> 4u];
            result[index * 2u + 1u] = Digits[digest[index] & 0x0fu];
        }
        return result;
    }

    [[nodiscard]] std::string ExpectedDigest(
        const std::vector<uint8_t>& bytes)
    {
        TextureQualificationSha256 digest{};
        assert(ComputeTextureQualificationSha256(
                bytes.data(), bytes.size(), digest) ==
            TextureQualificationReason::None);
        return LowerHex(digest);
    }

    [[nodiscard]] AdaptiveMsaaValidationReceipt MakeReceipt()
    {
        const std::string digest(64u, '1');
        AdaptiveMsaaValidationReceipt receipt;
        receipt.captureId = "capture-0001";
        receipt.suiteId = "adaptive-msaa-reference-error";
        receipt.caseId = "crossing-triangles-16x";
        receipt.recipeSha256 = digest;
        receipt.phaseIndex = 0u;
        receipt.phase = 0.25;
        receipt.imagePath = "caller-cannot-bind-this-path";
        receipt.imageSha256 = "caller-cannot-bind-this-hash";
        receipt.sourceCommit =
            "578496047c4753077e36f39a68df8c2c323cee58";
        receipt.sourceDiffSha256 = std::string(64u, '2');
        receipt.rendererBuildManifestSha256 = std::string(64u, '3');
        receipt.rendererExecutableSha256 = std::string(64u, '4');
        receipt.sceneName = "Adaptive MSAA Crossing Triangles";
        receipt.sceneDescriptorSha256 = std::string(64u, '5');
        receipt.sceneGltfSha256 = std::string(64u, '6');
        receipt.cameraSha256 = std::string(64u, '7');
        receipt.settingsSha256 = std::string(64u, '8');
        receipt.width = 4u;
        receipt.height = 4u;
        receipt.viewport = { 0u, 0u, 4u, 4u };
        receipt.frozenSeed = 0x123456789abcdef0ull;
        receipt.topologyEpoch = 1u;
        receipt.topologyRevision = 1u;
        receipt.lodRevision = 1u;
        receipt.deformationRevision = 1u;
        receipt.materialRevision = 1u;
        receipt.requestedImplementation = "compact-visibility";
        receipt.resolvedImplementation = "compact-visibility";
        receipt.appliedImplementation = "compact-visibility";
        receipt.requestedSampleCount = 16u;
        receipt.resolvedSampleCount = 8u;
        receipt.appliedSampleCount = 8u;
        receipt.selectedPathSucceeded = true;
        receipt.submittedMainViewTriangles = 3u;
        receipt.diagnosticsProducerProducedThisFrame = true;
        receipt.diagnosticsReadbackToken = 7u;
        receipt.diagnosticsCompletedToken = 7u;
        receipt.diagnosticsDispatchComplete = true;
        receipt.diagnosticsValidityMask = 0x3fu;
        receipt.groupHistogram[1] = 10u;
        receipt.referenceLightingEvaluations = 40u;
        receipt.performedLightingEvaluations = 24u;
        receipt.lightingEvaluationsSaved = 16u;
        receipt.fallbackReasonCounts[17u] = 2u;
        receipt.invalidKeyCount = 0u;
        receipt.staleKeyCount = 0u;
        receipt.mappingFailureCount = 0u;
        receipt.compactShadeSucceeded = true;
        receipt.compactShadeDispatchedViewCount = 1u;
        receipt.compactReconstructDrawCount = 1u;
        receipt.compactReconstructTriangleCount = 2u;
        receipt.compactFallbackDrawCount = 1u;
        receipt.compactFallbackTriangleCount = 1u;
        receipt.preparationTopology = "separated";
        receipt.workListObservationCount = 2u;
        receipt.workListCapacityTotal = 32u;
        receipt.workListOccupancyTotal = 12u;
        receipt.workListPeakOccupancy = 8u;
        receipt.workListOverflowCount = 0u;
        receipt.roi = { 1u, 1u, 2u, 2u };
        return receipt;
    }

    [[nodiscard]] bool HasTemporarySibling(
        const std::filesystem::path& receiptPath)
    {
        const std::string prefix = receiptPath.filename().string() + ".tmp.";
        for (const auto& entry :
            std::filesystem::directory_iterator(receiptPath.parent_path()))
        {
            if (entry.path().filename().string().rfind(prefix, 0u) == 0u)
                return true;
        }
        return false;
    }

    void TestCanonicalReceiptAndImageBinding()
    {
        TemporaryDirectory temporary;
        const std::filesystem::path bmp = temporary.Path() / "frame.bmp";
        const std::vector<uint8_t> bytes = MakeBmpBytes(0x11u);
        WriteBytes(bmp, bytes);

        AdaptiveMsaaValidationReceipt receipt = MakeReceipt();
        const AdaptiveMsaaValidationCaptureWriteResult result =
            WriteAdaptiveMsaaValidationReceipt(bmp, receipt);
        assert(result.Succeeded());
        assert(result.receiptPath ==
            DeriveAdaptiveMsaaValidationReceiptPath(
                std::filesystem::canonical(bmp)));
        assert(result.canonicalImagePath ==
            std::filesystem::canonical(bmp).generic_u8string());
        assert(result.imageSha256 == ExpectedDigest(bytes));
        assert(!HasTemporarySibling(result.receiptPath));

        const std::string text = ReadText(result.receiptPath);
        assert(result.status.byteCount == text.size());
        assert(!text.empty() && text.back() == '\n');
        assert(text.find("\n", 0u) == text.size() - 1u);
        const Json::Value root = ReadJson(result.receiptPath);
        assert(root["schema"].asUInt() == 2u);
        assert(root["kind"].asString() ==
            "uvsr-adaptive-msaa-validation-frame");
        assert(root["case"].asString() == receipt.caseId);
        assert(root["phase"].isDouble() &&
            root["phase"].asDouble() == receipt.phase);
        assert(root["imagePath"].asString() == result.canonicalImagePath);
        assert(root["imageSha256"].asString() == result.imageSha256);
        assert(root["imagePath"].asString() != receipt.imagePath);
        assert(root["imageSha256"].asString() != receipt.imageSha256);
        assert(root["requestedSampleCount"].asUInt() == 16u);
        assert(root["resolvedSampleCount"].asUInt() == 8u);
        assert(root["appliedSampleCount"].asUInt() == 8u);
        assert(root["groupHistogram"].isArray() &&
            root["groupHistogram"].size() == 17u);
        assert(root["fallbackReasonCounts"].isArray() &&
            root["fallbackReasonCounts"].size() == 24u &&
            root["fallbackReasonCounts"][17u].asUInt64() == 2u);
        assert(root["viewport"]["maxX"].asUInt() == 4u);
        assert(root["roi"]["width"].asUInt() == 2u);

        const char* requiredFields[] = {
            "schema", "kind", "captureId", "suiteId", "case",
            "recipeSha256", "phaseIndex", "phase", "imagePath",
            "imageSha256", "sourceCommit", "sourceDiffSha256",
            "rendererBuildManifestSha256", "rendererExecutableSha256",
            "sceneName", "sceneDescriptorSha256", "sceneGltfSha256",
            "cameraSha256", "settingsSha256", "width", "height",
            "viewport", "frozenSeed", "topologyEpoch", "topologyRevision",
            "lodRevision", "deformationRevision", "materialRevision",
            "requestedImplementation", "resolvedImplementation",
            "appliedImplementation", "requestedSampleCount",
            "resolvedSampleCount", "appliedSampleCount",
            "selectedPathSucceeded", "submittedMainViewTriangles",
            "diagnosticsProducerProducedThisFrame",
            "diagnosticsReadbackToken", "diagnosticsCompletedToken",
            "diagnosticsDispatchComplete", "diagnosticsValidityMask",
            "groupHistogram", "referenceLightingEvaluations",
            "performedLightingEvaluations", "lightingEvaluationsSaved",
            "fallbackReasonCounts",
            "invalidKeyCount", "staleKeyCount", "mappingFailureCount",
            "errorBudgetRejectionReasonCounts",
            "errorBudgetRejectedGroupCount",
            "errorBudgetRejectedSampleCount",
            "compactShadeSucceeded", "compactShadeDispatchedViewCount",
            "compactReconstructDrawCount",
            "compactReconstructTriangleCount", "compactFallbackDrawCount",
            "compactFallbackTriangleCount", "preparationTopology"
        };
        for (const char* field : requiredFields)
            assert(root.isMember(field));
        for (const char* field : {
                "workListObservationCount", "workListCapacityTotal",
                "workListOccupancyTotal", "workListPeakOccupancy",
                "workListOverflowCount", "roi" })
        {
            assert(root.isMember(field));
        }
        assert(root.getMemberNames().size() ==
            std::size(requiredFields) + 6u);
    }

    void TestAtomicReplacementAndCleanup()
    {
        TemporaryDirectory temporary;
        const std::filesystem::path bmp = temporary.Path() / "replace.bmp";
        const std::vector<uint8_t> firstBytes = MakeBmpBytes(0x22u);
        WriteBytes(bmp, firstBytes);
        AdaptiveMsaaValidationReceipt receipt = MakeReceipt();
        AdaptiveMsaaValidationCaptureWriteResult first =
            WriteAdaptiveMsaaValidationReceipt(bmp, receipt);
        assert(first.Succeeded());
        const std::string firstText = ReadText(first.receiptPath);

        const std::vector<uint8_t> secondBytes = MakeBmpBytes(0x33u);
        WriteBytes(bmp, secondBytes);
        receipt.captureId = "capture-0002";
        receipt.phaseIndex = 1u;
        AdaptiveMsaaValidationCaptureWriteResult second =
            WriteAdaptiveMsaaValidationReceipt(bmp, receipt);
        assert(second.Succeeded() && second.receiptPath == first.receiptPath);
        assert(second.imageSha256 == ExpectedDigest(secondBytes));
        assert(second.imageSha256 != first.imageSha256);
        const std::string secondText = ReadText(second.receiptPath);
        assert(secondText != firstText);
        const Json::Value root = ReadJson(second.receiptPath);
        assert(root["captureId"].asString() == "capture-0002");
        assert(root["phaseIndex"].asUInt() == 1u);

        AdaptiveMsaaValidationReceipt invalid = receipt;
        invalid.recipeSha256 = "not-a-digest";
        const AdaptiveMsaaValidationCaptureWriteResult rejected =
            WriteAdaptiveMsaaValidationReceipt(bmp, invalid);
        assert(!rejected.Succeeded() && rejected.status.reason ==
            AdaptiveMsaaValidationCaptureReason::InvalidHash);
        assert(ReadText(second.receiptPath) == secondText);
        assert(!HasTemporarySibling(second.receiptPath));
    }

    void TestMissingAndInvalidBmp()
    {
        TemporaryDirectory temporary;
        AdaptiveMsaaValidationReceipt receipt = MakeReceipt();
        const std::filesystem::path missing = temporary.Path() / "missing.bmp";
        AdaptiveMsaaValidationCaptureWriteResult result =
            WriteAdaptiveMsaaValidationReceipt(missing, receipt);
        assert(!result.Succeeded() && result.status.reason ==
            AdaptiveMsaaValidationCaptureReason::ImageMissing);
        assert(!std::filesystem::exists(
            DeriveAdaptiveMsaaValidationReceiptPath(missing)));

        const std::filesystem::path wrongExtension =
            temporary.Path() / "frame.png";
        WriteBytes(wrongExtension, MakeBmpBytes(0x44u));
        result = WriteAdaptiveMsaaValidationReceipt(wrongExtension, receipt);
        assert(!result.Succeeded() && result.status.reason ==
            AdaptiveMsaaValidationCaptureReason::BmpPathRequired);

        const std::filesystem::path invalidBmp =
            temporary.Path() / "invalid.bmp";
        WriteBytes(invalidBmp, { 0u, 1u, 2u });
        result = WriteAdaptiveMsaaValidationReceipt(invalidBmp, receipt);
        assert(!result.Succeeded() && result.status.reason ==
            AdaptiveMsaaValidationCaptureReason::ImageReadFailed);
        assert(!HasTemporarySibling(
            DeriveAdaptiveMsaaValidationReceiptPath(invalidBmp)));
    }

    void ExpectInvalid(
        const AdaptiveMsaaValidationReceipt& receipt,
        AdaptiveMsaaValidationCaptureReason expected)
    {
        const AdaptiveMsaaValidationCaptureStatus status =
            ValidateAdaptiveMsaaValidationReceipt(receipt);
        assert(!status.Succeeded() && status.reason == expected);
    }

    void TestStrictValidation()
    {
        TemporaryDirectory temporary;
        const std::filesystem::path bmp = temporary.Path() / "valid.bmp";
        WriteBytes(bmp, MakeBmpBytes(0x55u));
        AdaptiveMsaaValidationReceipt receipt = MakeReceipt();
        const AdaptiveMsaaValidationCaptureWriteResult written =
            WriteAdaptiveMsaaValidationReceipt(bmp, receipt);
        assert(written.Succeeded());
        receipt.imagePath = written.canonicalImagePath;
        receipt.imageSha256 = written.imageSha256;
        assert(ValidateAdaptiveMsaaValidationReceipt(receipt).Succeeded());

        AdaptiveMsaaValidationReceipt invalid = receipt;
        invalid.schema = 1u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidSchema);
        invalid = receipt;
        invalid.captureId = "Uppercase";
        ExpectInvalid(invalid, AdaptiveMsaaValidationCaptureReason::InvalidToken);
        invalid = receipt;
        invalid.phaseIndex = 2u;
        ExpectInvalid(invalid, AdaptiveMsaaValidationCaptureReason::InvalidToken);
        invalid = receipt;
        invalid.phase = std::numeric_limits<double>::quiet_NaN();
        ExpectInvalid(invalid, AdaptiveMsaaValidationCaptureReason::InvalidToken);
        invalid = receipt;
        invalid.phase = 1.01;
        ExpectInvalid(invalid, AdaptiveMsaaValidationCaptureReason::InvalidToken);
        invalid = receipt;
        invalid.recipeSha256[0] = 'A';
        ExpectInvalid(invalid, AdaptiveMsaaValidationCaptureReason::InvalidHash);
        invalid = receipt;
        invalid.sourceCommit.pop_back();
        ExpectInvalid(invalid, AdaptiveMsaaValidationCaptureReason::InvalidHash);
        invalid = receipt;
        invalid.requestedSampleCount = 1u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidSampleCount);
        invalid = receipt;
        invalid.width = 0u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidDimensions);
        invalid = receipt;
        invalid.appliedImplementation = "Compact-Visibility";
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidImplementation);
        invalid = receipt;
        invalid.resolvedSampleCount = 16u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidSampleCount);
        invalid = receipt;
        invalid.roi.width = 0u;
        ExpectInvalid(invalid, AdaptiveMsaaValidationCaptureReason::InvalidRoi);
        invalid = receipt;
        invalid.roi.x = 3u;
        ExpectInvalid(invalid, AdaptiveMsaaValidationCaptureReason::InvalidRoi);
        invalid = receipt;
        invalid.viewport.maxX = 5u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidViewport);
        invalid = receipt;
        ++invalid.lightingEvaluationsSaved;
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidLightingWork);
        invalid = receipt;
        invalid.diagnosticsValidityMask |= 1u << 6u;
        invalid.errorBudgetRejectionReasonCounts[2u] = 1u;
        invalid.errorBudgetRejectedGroupCount = 1u;
        invalid.errorBudgetRejectedSampleCount = 0u;
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid.errorBudgetRejectedSampleCount = 2u;
        assert(ValidateAdaptiveMsaaValidationReceipt(invalid).Succeeded());
        invalid.errorBudgetRejectionReasonCounts[1u] = 1u;
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid = receipt;
        invalid.workListPeakOccupancy = 13u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidWorkList);
        invalid = receipt;
        invalid.workListOverflowCount = 3u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidWorkList);
        invalid = receipt;
        invalid.preparationTopology = "inline";
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidWorkList);
        invalid = receipt;
        invalid.preparationTopology = "inline";
        invalid.workListObservationCount = 0u;
        invalid.workListCapacityTotal = 0u;
        invalid.workListOccupancyTotal = 0u;
        invalid.workListPeakOccupancy = 0u;
        invalid.workListOverflowCount = 0u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidWorkList);
        invalid = receipt;
        invalid.preparationTopology = "Separated";
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::
                InvalidPreparationTopology);
        invalid = receipt;
        invalid.compactReconstructDrawCount = 3u;
        invalid.compactReconstructTriangleCount = 2u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidCompactWork);
        invalid = receipt;
        invalid.diagnosticsCompletedToken = 6u;
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid = receipt;
        invalid.diagnosticsValidityMask &= ~(1u << 2u);
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid.fallbackReasonCounts.fill(0u);
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid = receipt;
        invalid.fallbackReasonCounts[0u] = 1u;
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid = receipt;
        invalid.fallbackReasonCounts[17u] =
            invalid.referenceLightingEvaluations + 1u;
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid = receipt;
        invalid.fallbackReasonCounts[1u] = 1u;
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid.invalidKeyCount = 1u;
        assert(ValidateAdaptiveMsaaValidationReceipt(invalid).Succeeded());
        invalid = receipt;
        invalid.fallbackReasonCounts[4u] = 1u;
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid.staleKeyCount = 1u;
        assert(ValidateAdaptiveMsaaValidationReceipt(invalid).Succeeded());
        invalid = receipt;
        invalid.fallbackReasonCounts[13u] = 1u;
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid.mappingFailureCount = 1u;
        assert(ValidateAdaptiveMsaaValidationReceipt(invalid).Succeeded());
        invalid = receipt;
        invalid.diagnosticsValidityMask &= ~(1u << 1u);
        ExpectInvalid(
            invalid,
            AdaptiveMsaaValidationCaptureReason::InvalidDiagnostics);
        invalid = receipt;
        invalid.topologyEpoch = 0u;
        ExpectInvalid(
            invalid, AdaptiveMsaaValidationCaptureReason::InvalidRevision);
    }

    void TestReceiptPathDerivation()
    {
        assert(DeriveAdaptiveMsaaValidationReceiptPath(
                std::filesystem::path("frame.bmp")) ==
            std::filesystem::path("frame.bmp.adaptive-msaa.json"));
        assert(DeriveAdaptiveMsaaValidationReceiptPath({}).empty());
    }
}

int main()
{
    TestReceiptPathDerivation();
    TestCanonicalReceiptAndImageBinding();
    TestAtomicReplacementAndCleanup();
    TestMissingAndInvalidBmp();
    TestStrictValidation();
    return 0;
}
