#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        std::ostringstream contents;
        contents << stream.rdbuf();
        std::string text = contents.str();
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        return text;
    }

    std::string_view ExtractSection(
        std::string_view source,
        std::string_view begin,
        std::string_view end)
    {
        const size_t beginPosition = source.find(begin);
        if (beginPosition == std::string_view::npos)
            return {};
        const size_t endPosition = source.find(
            end, beginPosition + begin.size());
        if (endPosition == std::string_view::npos)
            return {};
        return source.substr(
            beginPosition, endPosition - beginPosition);
    }

    bool ExpectContains(
        std::string_view source,
        std::string_view required,
        const char* contract)
    {
        if (source.find(required) != std::string_view::npos)
            return true;
        std::cerr << "FAIL: " << contract << " must contain '"
                  << required << "'.\n";
        return false;
    }

    bool ExpectAbsent(
        std::string_view source,
        std::string_view forbidden,
        const char* contract)
    {
        if (source.find(forbidden) == std::string_view::npos)
            return true;
        std::cerr << "FAIL: " << contract << " must not contain '"
                  << forbidden << "'.\n";
        return false;
    }

    bool ExpectOrdered(
        std::string_view source,
        std::string_view first,
        std::string_view second,
        const char* contract)
    {
        const size_t firstPosition = source.find(first);
        const size_t secondPosition = source.find(second);
        if (firstPosition != std::string_view::npos &&
            secondPosition != std::string_view::npos &&
            firstPosition < secondPosition)
        {
            return true;
        }
        std::cerr << "FAIL: " << contract << " must place '"
                  << first << "' before '" << second << "'.\n";
        return false;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: uvsr_renderer_source_contract_tests <root>\n";
        return 2;
    }

    const std::filesystem::path root = argv[1];
    const std::string viewer = ReadFile(root / "src/uvsr.cpp");
    const std::string cmaa = ReadFile(root / "src/cmaa2.cpp");
    const std::string visibility =
        ReadFile(root / "src/screen_space_visibility.cpp");
    bool passed = true;

    const std::string_view refresh = ExtractSection(
        viewer,
        "void RefreshAntiAliasingTargetPasses(bool sampleCountChanged)",
        "void CreateRenderPasses()");
    passed &= ExpectContains(
        refresh,
        "m_PbrDeferredLightingPass->ResetBindingCache();",
        "AA-only target refresh");
    passed &= ExpectContains(
        refresh,
        "m_ScreenSpaceVisibilityPass->ResetBindingCache();",
        "AA-only target refresh");
    passed &= ExpectContains(
        refresh,
        "m_ScreenSpaceVisibilityPass->ResetHistory();",
        "AA-only target refresh history invalidation");
    passed &= ExpectContains(
        refresh,
        "m_Cmaa2Pass->UpdateSourceColor(",
        "CMAA2 resource retention");
    passed &= ExpectAbsent(
        refresh,
        "std::make_unique<ScreenSpaceVisibilityPass>",
        "AA-only target refresh");
    passed &= ExpectAbsent(
        refresh,
        "std::make_unique<PbrDeferredLightingPass>",
        "AA-only target refresh");

    passed &= ExpectContains(
        viewer,
        "sameNonAaTopology && antiAliasingTopologyChanged",
        "AA-only topology classification");
    passed &= ExpectContains(
        viewer,
        "RefreshAntiAliasingTargetPasses(\n"
        "                    antiAliasingSampleCountChanged);",
        "AA-only refresh dispatch");
    passed &= ExpectContains(
        cmaa,
        "void Cmaa2Pass::UpdateSourceColor(",
        "CMAA2 source rebinding");
    passed &= ExpectContains(
        cmaa,
        "RebuildBindingSet(sourceColor);",
        "CMAA2 source rebinding");

    const std::string_view sceneLoaded = ExtractSection(
        viewer,
        "virtual void SceneLoaded() override",
        "void SetWhiteWorldMode(WhiteWorldMode mode)");
    passed &= ExpectOrdered(
        sceneLoaded,
        "sceneInitialCamera->VerticalFovDegrees;",
        "PointThirdPersonCameraAt(",
        "descriptor camera FOV framing");
    passed &= ExpectOrdered(
        sceneLoaded,
        "PointThirdPersonCameraAt(",
        "ApplySceneInitialCamera(*sceneInitialCamera);",
        "descriptor camera pose application");

    const std::string_view applyVisibilityQualityPreset = ExtractSection(
        visibility,
        "void ApplyScreenSpaceVisibilityQualityPreset(",
        "bool MatchesScreenSpaceVisibilityQualityPreset(");
    passed &= ExpectContains(
        applyVisibilityQualityPreset,
        "const VisibilityQualityReconstructionPreset reconstructionPreset =\n"
        "            GetVisibilityQualityReconstructionPreset(quality);",
        "visibility quality reconstruction recipe lookup");
    passed &= ExpectContains(
        applyVisibilityQualityPreset,
        "settings.reconstruction.spatialEnabled =\n"
        "            reconstructionPreset.spatialEnabled;",
        "visibility quality spatial enable application");
    passed &= ExpectContains(
        applyVisibilityQualityPreset,
        "settings.reconstruction.spatialFilter =\n"
        "            reconstructionPreset.spatialFilter;",
        "visibility quality spatial filter application");
    passed &= ExpectContains(
        applyVisibilityQualityPreset,
        "settings.reconstruction.spatialRadius =\n"
        "            reconstructionPreset.spatialRadius;",
        "visibility quality spatial radius application");

    if (!passed)
        return 1;
    std::cout << "UVSR renderer source contracts passed.\n";
    return 0;
}
