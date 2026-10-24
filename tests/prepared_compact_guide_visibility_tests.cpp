#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr <<
                "Prepared compact guide/visibility validation failed: "
                << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    std::string ReadSource(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot open " + path.generic_string());
        std::string source{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
        Require(!source.empty(),
            "source is empty: " + path.generic_string());
        return source;
    }

    std::string Canonicalize(std::string_view source)
    {
        std::string canonical;
        canonical.reserve(source.size());
        for (const char character : source)
        {
            const unsigned char byte =
                static_cast<unsigned char>(character);
            if (!std::isspace(byte))
            {
                canonical.push_back(
                    static_cast<char>(std::tolower(byte)));
            }
        }
        return canonical;
    }

    std::string ExtractFunction(
        std::string_view source,
        std::string_view signature)
    {
        const size_t signatureOffset = source.find(signature);
        Require(signatureOffset != std::string_view::npos,
            "missing function " + std::string(signature));
        const size_t bodyOffset = source.find('{', signatureOffset);
        Require(bodyOffset != std::string_view::npos,
            "missing function body for " + std::string(signature));
        uint32_t depth = 0u;
        for (size_t offset = bodyOffset; offset < source.size(); ++offset)
        {
            if (source[offset] == '{')
                ++depth;
            else if (source[offset] == '}' && --depth == 0u)
            {
                return std::string(source.substr(
                    signatureOffset,
                    offset - signatureOffset + 1u));
            }
        }
        Require(false,
            "unterminated function body for " + std::string(signature));
        return {};
    }

    void RequireRecordOnlyOperations(
        std::string_view function,
        const std::string& label)
    {
        const std::string canonical = Canonicalize(function);
        constexpr std::string_view Forbidden[] = {
            "ensureresources(",
            "getorcreate",
            "createbindingset(",
            "createbuffer(",
            "createtexture(",
            "createshader(",
            "createcomputepipeline(",
            "getchildview(",
            "getnumchildviews(",
            "getviewextent(",
            "getsubresources(",
            "fillplanarviewconstants("
        };
        for (const std::string_view token : Forbidden)
        {
            Require(canonical.find(token) == std::string::npos,
                label + " must not perform " + std::string(token));
        }
        Require(canonical.find("validatedforsubmission") !=
                std::string::npos,
            label + " must require the final validation latch");
        Require(canonical.find("setcomputestate(") !=
                std::string::npos,
            label + " must consume a captured compute state");
        Require(canonical.find("dispatch(") != std::string::npos,
            label + " must emit captured dispatches");
    }
}

int main(int argc, char** argv)
{
    Require(argc == 2,
        "expected the UVSR source directory as the only argument");
    const std::filesystem::path sourceDirectory = argv[1];
    const std::string pbrHeader = ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_pass.h");
    const std::string pbrSource = ReadSource(
        sourceDirectory / "src/pbr_deferred_lighting_pass.cpp");
    const std::string visibilityHeader = ReadSource(
        sourceDirectory / "src/screen_space_visibility.h");
    const std::string visibilitySource = ReadSource(
        sourceDirectory / "src/screen_space_visibility.cpp");

    const std::string canonicalPbrHeader = Canonicalize(pbrHeader);
    Require(canonicalPbrHeader.find(
            "pbrdeferredlightingpreparedsubmission("
            "constpbrdeferredlightingpreparedsubmission&)=delete") !=
            std::string::npos,
        "the PBR prepared token must be move-only");
    Require(canonicalPbrHeader.find(
            "preparesinglesamplesubmission(") != std::string::npos &&
        canonicalPbrHeader.find(
            "revalidatepreparedsubmission(") != std::string::npos &&
        canonicalPbrHeader.find(
            "recordpreparedsubmission(") != std::string::npos,
        "the PBR transaction API must expose prepare/revalidate/record");

    const std::string canonicalVisibilityHeader =
        Canonicalize(visibilityHeader);
    Require(canonicalVisibilityHeader.find(
            "screenspacevisibilitypreparedsubmission("
            "constscreenspacevisibilitypreparedsubmission&)=delete") !=
            std::string::npos,
        "the visibility prepared token must be move-only");
    Require(canonicalVisibilityHeader.find("neutraldisabled") !=
            std::string::npos,
        "disabled visibility must have an explicit valid neutral state");
    Require(canonicalVisibilityHeader.find("preparesubmission(") !=
            std::string::npos &&
        canonicalVisibilityHeader.find(
            "revalidatepreparedsubmission(") != std::string::npos &&
        canonicalVisibilityHeader.find(
            "recordpreparedsubmission(") != std::string::npos,
        "the visibility transaction API must expose prepare/revalidate/record");

    const std::string pbrPrepare = Canonicalize(ExtractFunction(
        pbrSource,
        "PbrDeferredLightingPass::PrepareSingleSampleSubmission"));
    Require(pbrPrepare.find("getorcreatebindingset(") !=
            std::string::npos,
        "PBR binding creation must finish during preparation");
    Require(pbrPrepare.find("fillplanarviewconstants(") !=
            std::string::npos,
        "PBR view constants must be captured during preparation");

    const std::string visibilityPrepare = Canonicalize(ExtractFunction(
        visibilitySource,
        "ScreenSpaceVisibilityPass::PrepareSubmission"));
    Require(visibilityPrepare.find("ensureresources(") !=
            std::string::npos,
        "visibility resources must be ensured during preparation");
    Require(visibilityPrepare.find("getorcreateadvancedpipeline(") !=
            std::string::npos,
        "visibility dynamic pipelines must be selected during preparation");
    Require(visibilityPrepare.find("createbindingset(") !=
            std::string::npos,
        "visibility bindings must be created during preparation");

    RequireRecordOnlyOperations(
        ExtractFunction(
            pbrSource,
            "PbrDeferredLightingPass::RecordPreparedSubmission"),
        "PBR prepared recording");
    RequireRecordOnlyOperations(
        ExtractFunction(
            visibilitySource,
            "ScreenSpaceVisibilityPass::RecordPreparedSubmission"),
        "visibility prepared recording");

    const std::string visibilityRecord = Canonicalize(ExtractFunction(
        visibilitySource,
        "ScreenSpaceVisibilityPass::RecordPreparedSubmission"));
    Require(visibilityRecord.find("noisepayload.data()") !=
            std::string::npos,
        "visibility recording must upload only the captured noise payload");
    Require(visibilityRecord.find("neutraldisabled") !=
            std::string::npos,
        "the neutral visibility token must consume without recording");

    std::cout <<
        "UVSR prepared compact guide/visibility validation passed\n";
    return EXIT_SUCCESS;
}
