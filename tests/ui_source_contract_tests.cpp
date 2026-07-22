#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    int g_FailureCount = 0;

    void Fail(const std::string& message)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++g_FailureCount;
    }

    void ExpectContains(
        std::string_view source,
        std::string_view required,
        const char* contract)
    {
        if (source.find(required) == std::string_view::npos)
        {
            Fail(std::string(contract) + " must contain '" +
                std::string(required) + "'.");
        }
    }

    void ExpectAbsent(
        std::string_view source,
        std::string_view forbidden,
        const char* contract)
    {
        if (source.find(forbidden) != std::string_view::npos)
        {
            Fail(std::string(contract) + " must not contain '" +
                std::string(forbidden) + "'.");
        }
    }

    void ExpectOrdered(
        std::string_view source,
        std::string_view first,
        std::string_view second,
        const char* contract)
    {
        const size_t firstPosition = source.find(first);
        const size_t secondPosition = source.find(second);
        if (firstPosition == std::string_view::npos ||
            secondPosition == std::string_view::npos ||
            firstPosition >= secondPosition)
        {
            Fail(std::string(contract) + " must place '" +
                std::string(first) + "' before '" +
                std::string(second) + "'.");
        }
    }

    std::string_view ExtractSection(
        std::string_view source,
        std::string_view beginAnchor,
        std::string_view endAnchor,
        const char* contract)
    {
        const size_t begin = source.find(beginAnchor);
        if (begin == std::string_view::npos)
        {
            Fail(std::string(contract) + " is missing begin anchor '" +
                std::string(beginAnchor) + "'.");
            return {};
        }

        const size_t end = source.find(endAnchor, begin + beginAnchor.size());
        if (end == std::string_view::npos)
        {
            Fail(std::string(contract) + " is missing end anchor '" +
                std::string(endAnchor) + "'.");
            return {};
        }

        return source.substr(begin, end - begin);
    }

    void ExpectDrawerContract(
        std::string_view source,
        std::string_view beginAnchor,
        std::string_view endAnchor,
        std::string_view bodyId,
        const char* contract)
    {
        const std::string_view section =
            ExtractSection(source, beginAnchor, endAnchor, contract);
        ExpectContains(section, "BeginDrawerBody(", contract);
        ExpectContains(section, bodyId, contract);

        const size_t endBody = section.rfind("EndDrawerBody();");
        const size_t siblingSpacing = section.rfind("ImGui::Spacing();");
        if (endBody == std::string_view::npos)
            Fail(std::string(contract) + " must end its animated body.");
        if (siblingSpacing == std::string_view::npos ||
            siblingSpacing < endBody)
        {
            Fail(std::string(contract) +
                " must place sibling spacing after its animated body.");
        }
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return {};
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }

    size_t CountOccurrences(
        std::string_view source,
        std::string_view value)
    {
        size_t count = 0;
        size_t position = 0;
        while ((position = source.find(value, position)) !=
            std::string_view::npos)
        {
            ++count;
            position += value.size();
        }
        return count;
    }

    std::string RemoveAsciiWhitespace(std::string_view source)
    {
        std::string compact;
        compact.reserve(source.size());
        for (const char character : source)
        {
            if (!std::isspace(static_cast<unsigned char>(character)))
                compact.push_back(character);
        }
        return compact;
    }
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: uvsr_ui_source_contract_tests <source-root>\n";
        return 2;
    }

    const std::filesystem::path sourcePath =
        std::filesystem::path(argv[1]) / "src" / "uvsr.cpp";
    const std::string source = ReadFile(sourcePath);
    if (source.empty())
    {
        std::cerr << "FAIL: could not read " << sourcePath << '\n';
        return 2;
    }
    const std::string compactSource = RemoveAsciiWhitespace(source);
    const std::filesystem::path uiAnimationPath =
        std::filesystem::path(argv[1]) / "src" / "ui_animation.h";
    const std::string uiAnimationSource = ReadFile(uiAnimationPath);
    if (uiAnimationSource.empty())
    {
        std::cerr << "FAIL: could not read " << uiAnimationPath << '\n';
        return 2;
    }
    const std::filesystem::path agentsPath =
        std::filesystem::path(argv[1]) / "AGENTS.md";
    const std::filesystem::path uiReferencePath =
        std::filesystem::path(argv[1]) /
        "docs" /
        "ui-integration-agent-procedure.md";
    const std::string agentsSource = ReadFile(agentsPath);
    const std::string uiReferenceSource = ReadFile(uiReferencePath);
    if (agentsSource.empty() || uiReferenceSource.empty())
    {
        std::cerr << "FAIL: could not read versioned UI guidance\n";
        return 2;
    }
    ExpectContains(
        agentsSource,
        "Agent policy version: `2026-07-22.1`.",
        "current agent policy version");
    ExpectContains(
        agentsSource,
        "tools/update_readme_line_counts.cmd --write",
        "README line-count refresh policy");
    ExpectContains(
        agentsSource,
        "tools/update_readme_line_counts.cmd --check",
        "README line-count verification policy");
    ExpectContains(
        uiReferenceSource,
        "UI reference version: `2026-07-22.1`.",
        "current UI reference version");
    ExpectContains(
        uiReferenceSource,
        "## Reference Revision History",
        "UI reference version history");
    ExpectContains(
        uiReferenceSource,
        "Only a dropdown inside an\n  animated nested section places its "
        "reset icon",
        "nested-dropdown-only reset placement guidance");
    ExpectContains(
        uiReferenceSource,
        "leaving every un-nested dropdown\n  exactly where it was",
        "un-nested dropdown layout preservation guidance");
    const std::filesystem::path donutAppOverridePath =
        std::filesystem::path(argv[1]) /
        "overrides" / "donut-app.patch";
    const std::string donutAppOverride = ReadFile(donutAppOverridePath);
    if (donutAppOverride.empty())
    {
        std::cerr << "FAIL: could not read " << donutAppOverridePath << '\n';
        return 2;
    }

    const std::filesystem::path readmePath =
        std::filesystem::path(argv[1]) / "README.md";
    const std::string readmeSource = ReadFile(readmePath);
    if (readmeSource.empty())
    {
        std::cerr << "FAIL: could not read " << readmePath << '\n';
        return 2;
    }
    ExpectContains(
        readmeSource,
        "<!-- uvsr-codebase-size:start -->",
        "README line-count block");
    ExpectContains(
        readmeSource,
        "**First-Party Lines of Code:**",
        "README first-party line count");
    ExpectContains(
        readmeSource,
        "**Third-Party Lines of Code:**",
        "README third-party line count");

    const std::filesystem::path cmakePath =
        std::filesystem::path(argv[1]) / "CMakeLists.txt";
    const std::string cmakeSource = ReadFile(cmakePath);
    if (cmakeSource.empty())
    {
        std::cerr << "FAIL: could not read " << cmakePath << '\n';
        return 2;
    }
    const std::filesystem::path imguiOverridePath =
        std::filesystem::path(argv[1]) / "overrides" / "imgui-ui.patch";
    const std::string imguiOverride = ReadFile(imguiOverridePath);
    if (imguiOverride.empty())
    {
        std::cerr << "FAIL: could not read " << imguiOverridePath << '\n';
        return 2;
    }
    const std::filesystem::path imguiDropdownRollPath =
        std::filesystem::path(argv[1]) /
        "overrides" /
        "imgui-dropdown-roll.patch";
    const std::string imguiDropdownRoll =
        ReadFile(imguiDropdownRollPath);
    if (imguiDropdownRoll.empty())
    {
        std::cerr << "FAIL: could not read "
                  << imguiDropdownRollPath << '\n';
        return 2;
    }
    ExpectContains(
        cmakeSource,
        "overrides/imgui-dropdown-roll.patch",
        "ordered ImGui dropdown-roll staging");
    ExpectContains(
        cmakeSource,
        "overrides/donut-app.patch",
        "tracked Donut app override staging");
    ExpectContains(
        cmakeSource,
        "UVSR_DONUT_APP_OVERRIDE_DIR",
        "tracked Donut app override routing");
    ExpectContains(
        cmakeSource,
        "\"${UVSR_DONUT_APP_OVERRIDE_DIR}/include\"",
        "renderer Donut app override include precedence");
    if (CountOccurrences(cmakeSource, "src/app/UserInterfaceUtils.cpp") < 2)
    {
        Fail("Donut Material Editor override must be staged and replace the "
            "pristine target source.");
    }
    ExpectContains(
        donutAppOverride,
        "+        std::atomic_bool m_SceneLoaded;",
        "thread-safe Donut scene-loaded override");
    ExpectContains(
        donutAppOverride,
        "+        bool showMaterialDomain = true);",
        "Material Editor visibility override contract");
    ExpectContains(
        donutAppOverride,
        "+        int domainIndex = int(material->domain);",
        "Material Editor domain storage safety");
    ExpectOrdered(
        cmakeSource,
        "overrides/imgui-ui.patch",
        "overrides/imgui-dropdown-roll.patch",
        "ordered ImGui dropdown-roll staging");
    ExpectContains(
        cmakeSource,
        "uvsr_imgui_dropdown_roll_tests",
        "actual patched-ImGui dropdown lifecycle target");
    for (const std::string_view translationUnit : {
            std::string_view("imgui.cpp"),
            std::string_view("imgui_demo.cpp"),
            std::string_view("imgui_draw.cpp"),
            std::string_view("imgui_tables.cpp"),
            std::string_view("imgui_widgets.cpp") })
    {
        if (CountOccurrences(cmakeSource, translationUnit) < 2)
        {
            Fail("ImGui override staging and source replacement must both "
                "include '" + std::string(translationUnit) + "'.");
        }
    }

    const std::filesystem::path backdropShaderPath =
        std::filesystem::path(argv[1]) /
        "src" /
        "backdrop_blur_ps.hlsl";
    const std::string backdropShader =
        ReadFile(backdropShaderPath);
    if (backdropShader.empty())
    {
        std::cerr << "FAIL: could not read "
                  << backdropShaderPath << '\n';
        return 2;
    }
    const std::filesystem::path temporalAaPath =
        std::filesystem::path(argv[1]) /
        "src" /
        "taa_miniengine.cpp";
    const std::string temporalAaSource = ReadFile(temporalAaPath);
    const std::filesystem::path sharpenShaderPath =
        std::filesystem::path(argv[1]) /
        "src" /
        "taa_miniengine_sharpen_cs.hlsl";
    const std::string sharpenShader = ReadFile(sharpenShaderPath);
    if (temporalAaSource.empty() || sharpenShader.empty())
    {
        std::cerr << "FAIL: could not read temporal AA sharpening sources\n";
        return 2;
    }

    const std::string_view disabledWrappedTextHelper = ExtractSection(
        source,
        "static void DrawDisabledTextWrapped(const char* text)",
        "static bool DrawCollapsingHeader(",
        "wrapped disabled text helper");
    ExpectContains(
        disabledWrappedTextHelper,
        "ImGui::PushTextWrapPos(0.f);",
        "wrapped disabled text helper");
    ExpectContains(
        disabledWrappedTextHelper,
        "ImGui::TextDisabled(\"%s\", text);",
        "wrapped disabled text helper");
    ExpectContains(
        disabledWrappedTextHelper,
        "ImGui::PopTextWrapPos();",
        "wrapped disabled text helper");

    const std::string_view loading = ExtractSection(
        source,
        "if (sceneLoading)",
        "m_WasSceneLoading = false;",
        "launch loading UI");
    const std::string_view sceneLoadingTransition = ExtractSection(
        loading,
        "if (!m_WasSceneLoading)",
        "BeginFullScreenWindow();",
        "scene-loading dropdown cancellation");
    ExpectContains(
        sceneLoadingTransition,
        "CancelDeferredDropdownUiActions();",
        "scene-loading dropdown cancellation");
    ExpectContains(loading, "LoadingDots", "launch loading UI");
    ExpectContains(loading, "please wait%s", "launch loading UI");
    ExpectContains(
        loading,
        "\"Loading scene: %s, please wait%s\\n\"",
        "launch loading UI punctuation");
    ExpectAbsent(loading, "##LoadingProgress", "launch loading UI");
    ExpectAbsent(loading, "loadingBar", "launch loading UI");
    ExpectAbsent(loading, "\"Launch:", "launch loading UI");
    ExpectAbsent(
        loading,
        "programLaunchMilliseconds",
        "launch loading UI");
    ExpectAbsent(
        loading,
        "m_DisplayedLoadingProgress",
        "launch loading UI");
    ExpectAbsent(source, "m_DisplayedLoadingProgress", "renderer UI state");
    ExpectAbsent(source, "m_ProgramLaunchTime", "renderer UI state");

    ExpectContains(
        source,
        "ImGui::SetNextWindowCollapsed(false, ImGuiCond_Once)",
        "Settings launch state");
    const std::string_view generalHeader = ExtractSection(
        source,
        "const bool generalOpen = DrawCollapsingHeader(",
        "if (generalOpen)",
        "General launch drawer");
    ExpectContains(
        generalHeader,
        "ImGuiTreeNodeFlags_DefaultOpen",
        "General launch drawer");
    for (const std::pair<std::string_view, std::string_view>& drawer : {
            std::pair<std::string_view, std::string_view>{
                "const bool indirectLightingOpen = DrawCollapsingHeader(",
                "if (indirectLightingOpen)"},
            {
                "const bool bufferConfigurationOpen = DrawCollapsingHeader(",
                "if (bufferConfigurationOpen)"},
            {
                "const bool visibilityStatisticsOpen = DrawCollapsingHeader(",
                "if (visibilityStatisticsOpen)"},
            {
                "const bool antiAliasingOpen = DrawCollapsingHeader(",
                "if (antiAliasingOpen)"},
            {
                "const bool skyOpen = DrawCollapsingHeader(",
                "if (skyOpen)"},
            {
                "const bool lightsOpen = DrawCollapsingHeader(",
                "if (lightsOpen)"} })
    {
        const std::string_view header = ExtractSection(
            source,
            drawer.first,
            drawer.second,
            "top-level Settings launch drawer");
        ExpectAbsent(
            header,
            "ImGuiTreeNodeFlags_DefaultOpen",
            "top-level Settings launch drawer");
    }
    ExpectContains(
        source,
        "ApplyWindowAppearance(",
        "Settings launch grow-and-fade");
    ExpectContains(
        source,
        "m_SettingsAppearance",
        "Settings launch appearance state");
    ExpectContains(
        source,
        "bool                                ShowUI = false",
        "Escape-only initial Settings state");
    ExpectContains(
        source,
        "m_ui.ShowUI = !m_ui.ShowUI",
        "Escape Settings toggle");
    ExpectContains(
        source,
        "m_SettingsAppearance,\n            m_ui.ShowUI",
        "Settings open-and-close appearance target");
    ExpectContains(
        source,
        "settingsWindowFlags |= ImGuiWindowFlags_NoInputs",
        "noninteractive Settings close animation");
    ExpectContains(
        source,
        "for (ImDrawCmd& command : drawList->CmdBuffer)",
        "Settings appearance clip-rectangle transform");
    ExpectAbsent(
        source,
        "m_SettingsEntranceStarted",
        "removed automatic Settings entrance gate");
    ExpectAbsent(
        source,
        "RequiredResponsiveSceneFrames",
        "removed automatic Settings frame threshold");
    ExpectContains(
        imguiOverride,
        "Draw the status outline before the matching title outline",
        "collapsed Settings outline ordering");
    ExpectContains(
        imguiOverride,
        "window->WindowBorderSize = 0.0f",
        "collapsed Settings standard border suppression");
    ExpectContains(
        imguiOverride,
        "ImRect collapsed_title_rect = title_bar_rect",
        "collapsed Settings inset title outline");
    ExpectContains(
        source,
        "std::array<UiBackdropRect, 3>",
        "independent Settings and material backdrop masks");
    ExpectContains(
        source,
        "UiBackdropRect& titleBackdrop =",
        "collapsed Settings title blur mask");
    ExpectContains(
        source,
        "UiBackdropRect& statusBackdrop =",
        "collapsed Settings status blur mask");
    ExpectContains(
        source,
        "UiBackdropRect& bodyBackdrop =",
        "expanded Settings body blur mask");
    ExpectContains(
        source,
        "constexpr size_t settingsBackdropCount = 2u",
        "split expanded Settings backdrop masks");
    ExpectAbsent(
        imguiOverride,
        "Draw this outline last so its rounded top corners remain visible",
        "removed collapsed Settings outline overlap");
    ExpectContains(
        source,
        "backdrop.shadowBlur = 10.f",
        "Settings shadow blur");
    ExpectContains(
        source,
        "backdrop.shadowOpacity = 0.34f",
        "Settings shadow opacity");
    ExpectContains(
        source,
        "backdrop.shadowOffsetY = 3.f",
        "Settings shadow offset");
    ExpectContains(
        source,
        "TrackSettingsAppearanceDrawList(",
        "Settings child appearance tracking");
    ExpectContains(
        source,
        "for (ImDrawList* drawList :\n            g_SettingsAppearanceDrawLists)",
        "Settings child appearance transform");
    ExpectContains(
        backdropShader,
        "#elif COMPOSITE == 2",
        "analytic Settings shadow permutation");
    ExpectContains(
        backdropShader,
        "if (panelDistance <= 0.0)",
        "outside-only Settings shadow");
    ExpectContains(
        backdropShader,
        "g_BackdropBlur.shadowOpacity",
        "Settings shadow alpha");

    ExpectDrawerContract(
        source,
        "const bool generalOpen = DrawCollapsingHeader(",
        "const bool indirectLightingOpen = DrawCollapsingHeader(",
        "##GeneralBody",
        "General drawer");
    ExpectDrawerContract(
        source,
        "const bool indirectLightingOpen = DrawCollapsingHeader(",
        "const bool bufferConfigurationOpen = DrawCollapsingHeader(",
        "##VisibilityBody",
        "Visibility drawer");
    ExpectDrawerContract(
        source,
        "const bool bufferConfigurationOpen = DrawCollapsingHeader(",
        "const bool visibilityStatisticsOpen = DrawCollapsingHeader(",
        "##BuffersBody",
        "Buffers drawer");
    ExpectDrawerContract(
        source,
        "const bool visibilityStatisticsOpen = DrawCollapsingHeader(",
        "const bool antiAliasingOpen = DrawCollapsingHeader(",
        "##StatisticsBody",
        "Statistics drawer");
    ExpectDrawerContract(
        source,
        "const bool antiAliasingOpen = DrawCollapsingHeader(",
        "const bool skyOpen = DrawCollapsingHeader(",
        "##AliasingBody",
        "Aliasing drawer");
    ExpectDrawerContract(
        source,
        "const bool skyOpen = DrawCollapsingHeader(",
        "const auto& lights = m_app->GetScene()->GetSceneGraph()->GetLights();",
        "##SkyBody",
        "Sky drawer");
    ExpectDrawerContract(
        source,
        "const bool lightsOpen = DrawCollapsingHeader(",
        "constexpr float ActionButtonCount = 4.f;",
        "##LightsBody",
        "Lights drawer");

    const std::string_view general = ExtractSection(
        source,
        "const bool generalOpen = DrawCollapsingHeader(",
        "const bool indirectLightingOpen = DrawCollapsingHeader(",
        "General drawer");
    ExpectContains(
        general,
        "DrawDeferredDropdownOption(",
        "General deferred dropdowns");

    const std::string_view visibility = ExtractSection(
        source,
        "const bool indirectLightingOpen = DrawCollapsingHeader(",
        "const bool bufferConfigurationOpen = DrawCollapsingHeader(",
        "Visibility drawer");
    ExpectContains(
        visibility,
        "DrawDeferredDropdownOption(",
        "Visibility deferred dropdowns");
    ExpectContains(
        visibility,
        "BeginAnimatedTreeNode(",
        "Visibility drawer");
    ExpectContains(visibility, "BeginRoundedCombo(", "Visibility drawer");
    ExpectContains(visibility, "DrawSliderFloat(", "Visibility drawer");
    ExpectContains(
        visibility,
        "BeginAnimatedToggleRegion(",
        "Visibility toggle-gated regions");
    const size_t visibilityEnabledRegion =
        visibility.find("\"##VisibilityEnabledControls\"");
    const size_t samplingResolution =
        visibility.find("\"Sampling Resolution\"");
    const size_t visibilityProfile =
        visibility.find("\"Profile\"");
    if (visibilityEnabledRegion == std::string_view::npos ||
        samplingResolution == std::string_view::npos ||
        visibilityProfile == std::string_view::npos ||
        visibilityEnabledRegion > samplingResolution ||
        visibilityEnabledRegion > visibilityProfile)
    {
        Fail("Visibility enabled region must own Sampling Resolution and "
            "Profile.");
    }
    ExpectContains(
        visibility,
        "\"Distribution Exponent\"",
        "Visibility distribution label");
    ExpectAbsent(
        visibility,
        "\"Radial Distribution Exponent\"",
        "Visibility distribution label");
    for (const std::string_view attribution : {
            std::string_view("(Bespoke)"),
            std::string_view("(Therrien)"),
            std::string_view("(Kleber)"),
            std::string_view("(cdrinmatane)"),
            std::string_view("Intel Edge-Guided") })
    {
        ExpectAbsent(
            visibility,
            attribution,
            "Visibility UI attribution");
    }
    ExpectAbsent(visibility, "ImGui::BeginCombo(", "Visibility drawer");
    ExpectAbsent(visibility, "ImGui::TreeNodeEx(", "Visibility drawer");
    ExpectAbsent(visibility, "ImGui::SliderFloat(", "Visibility drawer");
    ExpectAbsent(visibility, "ImGui::SliderInt(", "Visibility drawer");

    const std::string_view buffers = ExtractSection(
        source,
        "const bool bufferConfigurationOpen = DrawCollapsingHeader(",
        "const bool visibilityStatisticsOpen = DrawCollapsingHeader(",
        "Buffers drawer");
    ExpectContains(
        buffers,
        "DrawDeferredDropdownOption(",
        "Buffers deferred dropdowns");
    ExpectContains(buffers, "BeginRoundedCombo(", "Buffers drawer");
    ExpectContains(buffers, "PushID(\"BufferControls\")", "Buffers drawer");
    ExpectAbsent(buffers, "ImGui::BeginCombo(", "Buffers drawer");

    const std::string_view statistics = ExtractSection(
        source,
        "const bool visibilityStatisticsOpen = DrawCollapsingHeader(",
        "const bool antiAliasingOpen = DrawCollapsingHeader(",
        "Statistics drawer");
    ExpectContains(
        statistics,
        "DrawDeferredDropdownOption(",
        "Statistics deferred dropdowns");
    ExpectContains(statistics, "BeginRoundedCombo(", "Statistics drawer");
    ExpectContains(
        statistics,
        "BeginAnimatedTreeNode(\"Resource Footprint\")",
        "Statistics drawer");
    ExpectContains(statistics, "DrawSliderInt(", "Statistics drawer");
    ExpectContains(
        statistics,
        "DrawCenteredActionButton(",
        "Statistics drawer");
    ExpectContains(
        statistics,
        "\"Run Current With Motion\"",
        "Statistics drawer");
    ExpectContains(
        statistics,
        "StartAntiAliasingMotionTest()",
        "Statistics drawer");
    ExpectContains(
        statistics,
        "PushID(\"StatisticsControls\")",
        "Statistics drawer");
    ExpectContains(
        statistics,
        "StatisticsEffect::AntiAliasing",
        "Anti-Aliasing statistics selector");
    ExpectContains(
        statistics,
        "\"Geometry\"",
        "Geometry statistics selector label");
    ExpectAbsent(
        statistics,
        "Geometry / G-Buffer",
        "Geometry statistics selector label");
    ExpectContains(
        statistics,
        "\"##AntiAliasingLiveStatistics\"",
        "Anti-Aliasing statistics table");
    ExpectContains(
        statistics,
        "PresentStructuralBody(m_ui.AntiAliasing)",
        "staged Anti-Aliasing statistics presentation");
    const std::string_view statisticsAliasingPresentation = ExtractSection(
        statistics,
        "const AntiAliasingSettings& statisticsAliasing =",
        "if (ImGui::BeginTable(",
        "Anti-Aliasing statistics structural presentation");
    ExpectContains(
        statisticsAliasingPresentation,
        "ShowStructuralBody();",
        "Anti-Aliasing statistics structural presentation");
    ExpectOrdered(
        statisticsAliasingPresentation,
        "ShowStructuralBody();",
        "\"##StatisticsAliasingMethodBreakdown\"",
        "Anti-Aliasing statistics structural presentation");
    ExpectContains(
        statisticsAliasingPresentation,
        "showAliasingStatistics))",
        "Anti-Aliasing statistics structural animation target");
    const std::string_view statisticsRendererReadyPolicy = ExtractSection(
        statisticsAliasingPresentation,
        "const bool statisticsRendererReady =",
        "const bool temporalStatisticsActive =",
        "Anti-Aliasing statistics renderer-ready policy");
    ExpectContains(
        statisticsRendererReadyPolicy,
        "AliasingPresentationPhase::AwaitPopupRollUp",
        "Anti-Aliasing statistics popup-roll presentation policy");
    ExpectContains(
        statisticsRendererReadyPolicy,
        "AliasingPresentationPhase::CollapseCommitted",
        "Anti-Aliasing statistics renderer-ready policy");
    ExpectContains(
        statistics,
        "##StatisticsAliasingMethodBreakdown",
        "animated Anti-Aliasing statistics technique swap");
    ExpectContains(
        statistics,
        "statisticsRendererReady",
        "Anti-Aliasing statistics renderer-ready value gate");
    ExpectContains(
        statistics,
        "ImGui::TextDisabled(\"--\")",
        "pending Anti-Aliasing statistics value");
    ExpectContains(
        statistics,
        "\"Temporal AA Total\"",
        "relocated TAA statistics");
    ExpectContains(
        statistics,
        "\"CMAA2 Total\"",
        "relocated CMAA2 statistics");
    ExpectContains(
        statistics,
        "\"##BenchmarkCancelControls\"",
        "running-only Cancel animation");
    ExpectAbsent(
        statistics,
        "Ready to test",
        "removed idle benchmark readiness text");
    ExpectAbsent(
        statistics,
        "Export Last Run",
        "removed Visibility benchmark export UI");
    ExpectAbsent(
        statistics,
        "OpenVisibilityBenchmarkFolder",
        "removed Visibility benchmark folder UI");
    ExpectContains(
        statistics,
        "DrawDisabledTextWrapped(benchmarkBlockedReason.c_str())",
        "Statistics unavailable reason color and wrapping");
    ExpectAbsent(
        statistics,
        "ImGui::TextWrapped(\"%s\", benchmarkBlockedReason.c_str())",
        "Statistics unavailable reason color and wrapping");
    ExpectAbsent(statistics, "ImGui::BeginCombo(", "Statistics drawer");
    ExpectAbsent(statistics, "ImGui::TreeNodeEx(", "Statistics drawer");
    ExpectAbsent(statistics, "ImGui::SliderFloat(", "Statistics drawer");
    ExpectAbsent(statistics, "ImGui::SliderInt(", "Statistics drawer");
    ExpectAbsent(statistics, "ImGui::Button(", "Statistics drawer");

    const std::string_view aliasing = ExtractSection(
        source,
        "const bool antiAliasingOpen = DrawCollapsingHeader(",
        "const bool skyOpen = DrawCollapsingHeader(",
        "Aliasing drawer");
    ExpectContains(
        aliasing,
        "DrawDeferredDropdownOption(",
        "Aliasing deferred dropdowns");
    ExpectContains(
        aliasing,
        "g_DeferredAliasingUiPresentation.PresentSelectors(",
        "Aliasing staged selector presentation");
    ExpectContains(
        aliasing,
        "PresentStructuralBody(m_ui.AntiAliasing)",
        "Aliasing staged method-body presentation");
    ExpectContains(
        aliasing,
        "DrawDisabledTextWrapped(",
        "Aliasing unavailable reason color and wrapping");
    ExpectContains(
        aliasing,
        "Temporal anti-aliasing is paused until visibility\\n",
        "Aliasing visibility-conflict reason line break");
    ExpectContains(
        aliasing,
        "Temporal Reconstruction is disabled.",
        "Aliasing visibility-conflict reason second line");
    ExpectContains(
        aliasing,
        "Temporal anti-aliasing requires deferred UVSR PBR\\n",
        "Aliasing renderer requirement reason line break");
    ExpectContains(
        aliasing,
        "motion and depth.",
        "Aliasing renderer requirement reason second line");
    const std::string_view aliasingMethodBodyPresentation = ExtractSection(
        aliasing,
        "AntiAliasingSettings& settings =",
        "const AntiAliasingPreset selectedImplementation =",
        "Aliasing method-body structural presentation");
    ExpectContains(
        aliasingMethodBodyPresentation,
        "ShowStructuralBody();",
        "Aliasing method-body structural presentation");
    ExpectOrdered(
        aliasingMethodBodyPresentation,
        "ShowStructuralBody();",
        "\"##AliasingMethodDependentControls\"",
        "Aliasing method-body structural presentation");
    ExpectContains(
        aliasingMethodBodyPresentation,
        "showAliasingMethodDependentBody))",
        "Aliasing method-body structural animation target");
    ExpectContains(
        aliasing,
        "g_DeferredAliasingUiPresentation.CommitTo(",
        "Aliasing staged commit");
    ExpectContains(
        aliasing,
        "##AliasingMethodDependentControls",
        "Aliasing two-phase method body");
    const std::string_view aliasingMethodControl = ExtractSection(
        aliasing,
        "const bool methodComboOpen = BeginRoundedCombo(",
        "std::string qualityPreview =",
        "Aliasing Method staged presentation");
    ExpectContains(
        aliasingMethodControl,
        "g_DeferredAliasingUiPresentation.Stage(",
        "Aliasing Method selection staging");
    ExpectContains(
        aliasingMethodControl,
        "QueueDeferredControlUiAction(",
        "Aliasing Method reset staging");
    const std::string_view aliasingMethodSelection = ExtractSection(
        aliasingMethodControl,
        "candidateLabel += \"###MethodCandidate\";",
        "if (selected)",
        "Aliasing Method selection staging");
    ExpectOrdered(
        aliasingMethodSelection,
        "g_DeferredAliasingUiPresentation.Stage(",
        "true,",
        "Aliasing Method structural selection staging");
    const size_t aliasingMethodResetBegin =
        aliasingMethodControl.find("\"Aliasing Method\",");
    const std::string_view aliasingMethodReset =
        aliasingMethodResetBegin != std::string_view::npos
            ? aliasingMethodControl.substr(aliasingMethodResetBegin)
            : std::string_view{};
    if (aliasingMethodReset.empty())
        Fail("Aliasing Method reset staging is missing its reset block.");
    ExpectOrdered(
        aliasingMethodReset,
        "g_DeferredAliasingUiPresentation.Stage(",
        "true,",
        "Aliasing Method structural reset staging");
    const std::string_view aliasingQualityControl = ExtractSection(
        aliasing,
        "std::string qualityPreview =",
        "const AntiAliasingPreset selectedImplementation =",
        "Aliasing Quality staged presentation");
    ExpectContains(
        aliasingQualityControl,
        "g_DeferredAliasingUiPresentation.Stage(",
        "Aliasing Quality selection staging");
    ExpectContains(
        aliasingQualityControl,
        "QueueDeferredControlUiAction(",
        "Aliasing Quality reset staging");
    const std::string_view aliasingQualitySelection = ExtractSection(
        aliasingQualityControl,
        "candidateLabel += \"###QualityCandidate\";",
        "if (selected)",
        "Aliasing Quality selection staging");
    const std::string_view aliasingQualitySelectionStage = ExtractSection(
        aliasingQualitySelection,
        "g_DeferredAliasingUiPresentation.Stage(",
        "[candidate](AntiAliasingSettings& staged)",
        "Aliasing Quality nonstructural selection staging");
    ExpectContains(
        aliasingQualitySelectionStage,
        "false,",
        "Aliasing Quality nonstructural selection staging");
    const size_t aliasingQualityResetBegin =
        aliasingQualityControl.find("\"Aliasing Quality\",");
    const std::string_view aliasingQualityReset =
        aliasingQualityResetBegin != std::string_view::npos
            ? aliasingQualityControl.substr(aliasingQualityResetBegin)
            : std::string_view{};
    if (aliasingQualityReset.empty())
        Fail("Aliasing Quality reset staging is missing its reset block.");
    const std::string_view aliasingQualityResetStage = ExtractSection(
        aliasingQualityReset,
        "g_DeferredAliasingUiPresentation.Stage(",
        "[defaultQuality](AntiAliasingSettings& staged)",
        "Aliasing Quality nonstructural reset staging");
    ExpectContains(
        aliasingQualityResetStage,
        "false,",
        "Aliasing Quality nonstructural reset staging");
    ExpectAbsent(
        aliasing,
        "Run Current With Motion",
        "Aliasing drawer");
    ExpectAbsent(
        aliasing,
        "Run 45-Degree Motion Test",
        "Aliasing drawer");
    ExpectAbsent(
        aliasing,
        "StartAntiAliasingMotionTest()",
        "Aliasing drawer");
    ExpectAbsent(
        aliasing,
        "\"Temporal AA Total\"",
        "Aliasing statistics duplication");
    ExpectAbsent(
        aliasing,
        "\"CMAA2 Total\"",
        "Aliasing statistics duplication");
    ExpectAbsent(
        aliasing,
        "\"Intel CMAA2",
        "Aliasing UI attribution");
    ExpectAbsent(
        aliasing,
        "\"MiniEngine",
        "Aliasing UI attribution");
    ExpectContains(
        aliasing,
        "\"##AliasingEnabledControls\"",
        "Aliasing enabled collapse region");
    ExpectAbsent(
        aliasing,
        "drawMutexOption",
        "Aliasing mutex row collapse");
    ExpectAbsent(
        aliasing,
        "History Strength (Mutex)",
        "Aliasing history mutex row collapse");
    ExpectAbsent(
        source,
        "BeginAnimatedMutex",
        "dropdown selection close fading");
    for (const std::string_view removedPerformanceControl : {
            std::string_view("\"Execution Path\""),
            std::string_view("\"Compute Kernel\""),
            std::string_view("\"LDS Layout\""),
            std::string_view("\"Shared-Work Reuse\""),
            std::string_view("\"Early History Rejection\""),
            std::string_view("\"Pass Fusion\""),
            std::string_view("\"Cache Blocking\""),
            std::string_view("\"Debug View\"") })
    {
        ExpectAbsent(
            aliasing,
            removedPerformanceControl,
            "removed Aliasing developer dropdown");
    }
    ExpectContains(
        aliasing,
        "\"Stable Interior\"",
        "retained Stable Interior control");
    ExpectAbsent(
        aliasing,
        "\"Developer Performance Overrides\"",
        "removed Aliasing developer performance drawer");
    ExpectOrdered(
        aliasing,
        "drawStableInteriorControl();",
        "\"Sharpness###Sharpness\"",
        "Stable Interior control order");
    ExpectAbsent(
        aliasing,
        "(Preset)",
        "Aliasing inherited-value labels");
    ExpectAbsent(
        aliasing,
        "\" (\" + result",
        "indirect Aliasing inherited-value suffixes");
    ExpectContains(
        aliasing,
        "result = inheritedOrAutoValue;",
        "plain Aliasing inherited-value previews");
    ExpectContains(
        aliasing,
        "const ValueType presentationValue =",
        "read-only redundant Aliasing enum presentation");
    ExpectContains(
        aliasing,
        "const bool redundantInheritedMorphology =",
        "read-only redundant Aliasing morphology presentation");
    ExpectContains(
        aliasingMethodControl,
        "NormalizeRedundantAntiAliasingOverrides(",
        "explicit staged Aliasing normalization");
    ExpectContains(
        source,
        "MiniEngineTaaSharpenEnabled = false;",
        "default-disabled Aliasing sharpness");
    ExpectContains(
        sharpenShader,
        "#if TAA_SHARPEN_INPUT_PREMULTIPLIED",
        "separate temporal and presentation sharpen input contracts");
    ExpectContains(
        sharpenShader,
        "float3 normalizedColor = Color.rgb;",
        "resolved presentation sharpen input");
    ExpectContains(
        temporalAaSource,
        "m_PresentationSharpenPipeline",
        "resolved presentation sharpen pipeline");
    ExpectContains(
        temporalAaSource,
        "state.pipeline = m_PresentationSharpenPipeline;",
        "CMAA2-compatible presentation sharpen dispatch");
    for (const std::string_view morphologyStrength : {
            std::string_view("\"Conservative Low\""),
            std::string_view("\"Conservative Medium\""),
            std::string_view("\"Conservative High\""),
            std::string_view("\"Conservative Ultra\"") })
    {
        ExpectContains(
            aliasing,
            morphologyStrength,
            "CMAA2 morphology strength");
    }
    for (const std::string_view removedExport : {
            std::string_view("VisibilityBenchmarkExportPaths"),
            std::string_view("ExportVisibilityBenchmark"),
            std::string_view("ExportLastVisibilityBenchmark"),
            std::string_view("--benchmark-output") })
    {
        ExpectAbsent(
            source,
            removedExport,
            "removed Visibility benchmark export implementation");
    }

    const std::string_view lights = ExtractSection(
        source,
        "const bool lightsOpen = DrawCollapsingHeader(",
        "constexpr float ActionButtonCount = 4.f;",
        "Lights drawer");
    ExpectContains(
        lights,
        "DrawDeferredDropdownOption(",
        "Lights deferred dropdowns");

    ExpectContains(
        source,
        "AdvancePixelZoomMode(m_ui.PixelZoom)",
        "pixel zoom controls");
    ExpectContains(
        source,
        "GetPixelZoomButtonLabel(m_ui.PixelZoom)",
        "pixel zoom controls");
    ExpectContains(
        source,
        "ImGui::CalcTextSize(\"Zoom\")",
        "constant pixel zoom footer label");
    ExpectContains(
        source,
        "if (pixelZoomRequested && pixelZoomOpacity > 0.f)",
        "conditional crosshair");
    ExpectContains(
        source,
        "if (pixelZoomPassActive && m_PixelZoomPass)",
        "conditional pixel zoom pass");
    ExpectContains(
        source,
        "SmoothPixelZoomVisibility(m_PixelZoomVisibility)",
        "pixel zoom fade");
    ExpectContains(
        source,
        "ResolveAnimatedPixelZoomLayout(",
        "pixel zoom window scale");
    ExpectContains(
        source,
        "ResolvePixelZoomLevelTransitionScale(",
        "pixel zoom level transition scale");
    ExpectContains(
        source,
        "ShouldSwitchPixelZoomLevel(",
        "exact pixel zoom level midpoint");
    ExpectContains(
        source,
        "m_PendingPixelZoom",
        "pixel zoom level transition state");
    ExpectContains(
        source,
        "GetPixelZoomAreaLabel(m_RenderedPixelZoom)",
        "pixel zoom area descriptor");
    ExpectContains(
        source,
        "230.f * pixelZoomOpacity",
        "pixel zoom descriptor fade");
    ExpectContains(
        source,
        "150.f * pixelZoomOpacity",
        "pixel zoom descriptor shadow fade");
    ExpectContains(
        source,
        "zoomLabelLayout.panelMinY +",
        "pixel zoom descriptor animated placement");
    ExpectOrdered(
        source,
        "m_PixelZoomPass->Composite(",
        "imgui_nvrhi->render(framebuffer);",
        "pixel zoom descriptor composition order");
    ExpectContains(
        source,
        "selectedProfileName += \" (Custom)\"",
        "visibility preset-origin custom label");
    ExpectContains(
        source,
        "DrawPresetResetIcon(",
        "shared animated per-setting preset reset controls");
    ExpectContains(
        uiAnimationSource,
        "ShouldPlaceUiResetInNestedDropdownGutter(",
        "nested-dropdown-only reset placement policy");
    ExpectContains(
        uiAnimationSource,
        "return isDropdown && nestedDepth > 0u;",
        "nested-dropdown-only reset placement policy");
    ExpectContains(
        uiAnimationSource,
        "ResolveNestedDropdownResetOffset(",
        "nested reset gutter geometry");

    const std::string_view resetPlacementHelpers = ExtractSection(
        source,
        "enum class SettingsResetIconPlacement",
        "struct DeferredDropdownUiPayload",
        "reset icon placement helpers");
    ExpectContains(
        resetPlacementHelpers,
        "Trailing",
        "unchanged default reset placement");
    ExpectContains(
        resetPlacementHelpers,
        "NestedDropdownGutter",
        "nested dropdown reset placement");
    ExpectContains(
        resetPlacementHelpers,
        "ResolveNestedDropdownResetOffset(",
        "nested reset gutter geometry");
    ExpectContains(
        resetPlacementHelpers,
        "DrawNestedDropdownResetIcon(",
        "explicit nested dropdown reset helper");
    ExpectContains(
        resetPlacementHelpers,
        "nestedDropdownGutterRequested",
        "explicit nested reset gutter request");
    ExpectContains(
        resetPlacementHelpers,
        "nestedDropdownGutterAvailable",
        "nested reset gutter ownership check");
    ExpectContains(
        resetPlacementHelpers,
        "ImGui::GetCurrentWindow() ==\n"
        "                g_NestedDrawerAnimationContexts.back().bodyWindow",
        "nested reset gutter child ownership check");
    ExpectContains(
        resetPlacementHelpers,
        "context.indentSpacing",
        "captured nested reset gutter geometry");
    if (CountOccurrences(
            source,
            "SettingsResetIconPlacement::Trailing") != 1)
    {
        Fail(
            "only the ordinary reset wrapper may select the unchanged "
            "trailing placement.");
    }
    if (CountOccurrences(
            source,
            "SettingsResetIconPlacement::NestedDropdownGutter") != 2)
    {
        Fail(
            "nested gutter placement must remain confined to its rendering "
            "branch and explicit nested-dropdown wrapper.");
    }
    if (CountOccurrences(
            source,
            "DrawNestedDropdownResetIcon(") != 8)
    {
        Fail(
            "nested reset placement must have one helper definition and "
            "exactly seven allowlisted source call sites.");
    }
    constexpr const char* nestedResetCalls[] = {
        "DrawNestedDropdownResetIcon(\"VisibilityEstimator\",",
        "DrawNestedDropdownResetIcon(\"VisibilityNoisePattern\",",
        "DrawNestedDropdownResetIcon(\"VisibilityExactSampleCount\",",
        "DrawNestedDropdownResetIcon(\"VisibilityReconstructionMethod\",",
        "DrawNestedDropdownResetIcon(\"VisibilityFinalApplication\",",
        "DrawNestedDropdownResetIcon(\"AliasingSubpixelMorphology\",",
        "DrawNestedDropdownResetIcon(label,"
    };
    for (const char* nestedResetCall : nestedResetCalls)
    {
        ExpectContains(
            compactSource,
            nestedResetCall,
            "nested dropdown reset source allowlist");
    }
    constexpr const char* preservedTrailingDropdownResetCalls[] = {
        "DrawPresetResetIcon(\"VisibilitySamplingResolution\",",
        "DrawPresetResetIcon(\"VisibilityProfile\",",
        "DrawPresetResetIcon(\"AliasingMethod\",",
        "DrawPresetResetIcon(\"AliasingQuality\","
    };
    for (const char* trailingResetCall :
        preservedTrailingDropdownResetCalls)
    {
        ExpectContains(
            compactSource,
            trailingResetCall,
            "preserved un-nested dropdown reset placement");
    }

    const std::string_view animatedNestedSection = ExtractSection(
        source,
        "struct NestedDrawerAnimationContext",
        "struct UiToggleRegionAnimationState",
        "nested child gutter ownership");
    if (CountOccurrences(
            animatedNestedSection,
            "ImGui::Indent(indentSpacing);") != 1 ||
        CountOccurrences(
            animatedNestedSection,
            "ImGui::Unindent(context.indentSpacing);") != 1)
    {
        Fail(
            "the animated nested child must own exactly one balanced "
            "internal indent gutter.");
    }
    ExpectOrdered(
        animatedNestedSection,
        "ImGui::BeginChild(",
        "ImGui::Indent(indentSpacing);",
        "nested gutter inside the animated child");
    ExpectOrdered(
        animatedNestedSection,
        "ImGui::Indent(indentSpacing);",
        "g_NestedDrawerAnimationContexts.push_back",
        "nested gutter before body composition");
    ExpectContains(
        animatedNestedSection,
        "ImGuiWindow* bodyWindow = nullptr;",
        "nested child window ownership");
    ExpectContains(
        animatedNestedSection,
        "float indentSpacing = 0.f;",
        "captured nested indent geometry");
    ExpectOrdered(
        animatedNestedSection,
        "g_NestedDrawerAnimationContexts.pop_back();",
        "ImGui::Unindent(context.indentSpacing);",
        "nested gutter lifetime balance");
    ExpectOrdered(
        animatedNestedSection,
        "ImGui::Unindent(context.indentSpacing);",
        "ImGui::EndChild();",
        "nested gutter inside the animated child");
    ExpectContains(
        source,
        "DefaultStatisticsEffect",
        "Statistics effect default reset");
    ExpectContains(
        source,
        "DefaultBenchmarkWarmupFrames",
        "Statistics benchmark defaults");
    ExpectContains(
        source,
        "\"Sky Brightness\"",
        "Sky per-setting default reset");
    ExpectContains(
        source,
        "struct LightDefaultState",
        "scene-loaded light defaults");
    ExpectContains(
        source,
        "\"Camera Mode\"",
        "General camera default reset");
    ExpectContains(
        source,
        "overridesPointer->morphologyQuality = -1",
        "independent morphology reset");
    ExpectContains(
        source,
        "morphologyQuality == candidateQuality",
        "independent morphology selection state");
    ExpectContains(
        source,
        "MarkScreenSpaceVisibilityQualityCustom(visibility)",
        "visibility preset-origin preservation");
    ExpectContains(
        source,
        "ReconcileScreenSpaceVisibilityQualityPreset(",
        "visibility preset restoration reconciliation");
    ExpectContains(
        source,
        "return \"Depth Normal\"",
        "visibility Depth Normal label");
    ExpectContains(
        source,
        "return \"Fused Depth Normal\"",
        "visibility fused Depth Normal label");
    ExpectContains(
        source,
        "DWMWA_EXTENDED_FRAME_BOUNDS",
        "balanced visible startup margins");
    ExpectContains(
        source,
        "return \"Unpacked Offline\"",
        "unpacked offline noise label");
    ExpectContains(
        source,
        "return \"Packed Offline\"",
        "packed offline noise label");
    ExpectAbsent(
        source,
        "return \"Spacetime Noise\"",
        "retired unpacked noise label");
    ExpectAbsent(
        cmakeSource,
        "src/smaa.cpp",
        "removed SMAA build source");
    ExpectContains(
        source,
        "UiLayoutAnimationDurationSeconds = 0.18f",
        "shared capped layout animation clock");
    ExpectContains(
        source,
        "BeginSettingsScrollStability();",
        "Settings viewport anchor capture");
    ExpectContains(
        source,
        "EndSettingsScrollStability();",
        "Settings viewport anchor correction");
    ExpectContains(
        source,
        "GetSettingsBodyMinimumHeight(",
        "Settings viewport height retention during animated scrolling");
    const std::string_view settingsBodyMinimumPolicy = ExtractSection(
        source,
        "static float GetSettingsBodyMinimumHeight(",
        "static void MarkSettingsLayoutAnimationActive()",
        "Settings viewport minimum policy");
    ExpectContains(
        settingsBodyMinimumPolicy,
        "ShouldRetainUiViewportHeight(",
        "scroll-activity-only Settings viewport retention");
    ExpectAbsent(
        settingsBodyMinimumPolicy,
        "layoutAnimatingLastFrame",
        "smooth unscrolled Settings background collapse");
    ExpectContains(
        source,
        "ImGuiChildFlags_AutoResizeY",
        "content-following SettingsBody height");
    ExpectContains(
        source,
        "ImGuiWindowFlags_AlwaysVerticalScrollbar",
        "single stable Settings scroll owner");
    ExpectContains(
        source,
        "window->ScrollTarget.y < FLT_MAX",
        "pending wheel-target composition");
    if (CountOccurrences(
            source,
            "EnsureAnimatedChildLayoutSubmission(") < 4)
    {
        Fail(
            "offscreen animated layout submission must cover top-level, "
            "nested-tree, and toggle-region children.");
    }
    ExpectContains(
        source,
        "g_NestedDrawerAnimationContexts.push_back",
        "stacked nested animation contexts");
    ExpectContains(
        source,
        "g_UiToggleRegionAnimationContexts.push_back",
        "stacked toggle-region animation contexts");
    const std::string_view animatedToggleRegion = ExtractSection(
        source,
        "static bool BeginAnimatedToggleRegion(",
        "static void EndAnimatedToggleRegion()",
        "animated toggle-region transition tracking");
    ExpectContains(
        animatedToggleRegion,
        "bool targetChangedThisFrame = false;",
        "animated toggle-region target transition");
    ExpectContains(
        animatedToggleRegion,
        "targetChangedThisFrame = true;",
        "animated toggle-region target transition");
    ExpectContains(
        animatedToggleRegion,
        "if (targetChangedThisFrame ||",
        "same-frame layout activity marking");
    ExpectOrdered(
        animatedToggleRegion,
        "if (targetChangedThisFrame ||",
        "MarkSettingsLayoutAnimationActive();",
        "same-frame layout activity marking");
    ExpectContains(
        source,
        "FreezeAnimatedToggleVisualValues()",
        "retained slider presentation during exit");
    ExpectContains(
        source,
        "historyPresetSettings.enabled = true",
        "AA history display independent of execution bypass");
    ExpectAbsent(
        source,
        "measurementFreezeUntil",
        "removed wheel-delayed nested measurement");
    ExpectAbsent(
        source,
        "ImGui::GetFrameHeight() * openAmount",
        "removed one-row nested measurement proxy");

    const std::string_view deferredDropdownQueue = ExtractSection(
        source,
        "struct DeferredDropdownUiPayload",
        "static ImVec4 LerpUiColor(",
        "deferred dropdown commit queue");
    ExpectContains(
        deferredDropdownQueue,
        "using DeferredAliasingUiPresentation =",
        "Aliasing structural presentation alias");
    ExpectContains(
        deferredDropdownQueue,
        "DeferredUiStructuralPresentation<AntiAliasingSettings>",
        "Aliasing structural presentation alias");

    const std::string_view structuralPresentationPolicy = ExtractSection(
        uiAnimationSource,
        "enum class DeferredUiStructuralPresentationPhase",
        "struct UiDrawerHeightDeltas",
        "generic two-phase presentation policy");
    ExpectContains(
        structuralPresentationPolicy,
        "AwaitPopupRollUp",
        "generic popup roll-up wait phase");
    ExpectContains(
        structuralPresentationPolicy,
        "CollapseCommitted",
        "generic committed-body collapse phase");
    ExpectContains(
        structuralPresentationPolicy,
        "ExpandStaged",
        "generic staged-body expansion phase");
    ExpectContains(
        structuralPresentationPolicy,
        "ReadyToCommit",
        "generic stable commit phase");
    ExpectContains(
        structuralPresentationPolicy,
        "m_Phase == Phase::ReadyToCommit",
        "generic ready-to-commit predicate");
    ExpectContains(
        structuralPresentationPolicy,
        "m_Phase != Phase::CollapseCommitted",
        "generic committed-body collapse target");
    ExpectContains(
        structuralPresentationPolicy,
        "PresentSelectors(",
        "generic staged selector presentation");
    ExpectContains(
        structuralPresentationPolicy,
        "PresentStructuralBody(",
        "generic committed-then-staged structural body");
    ExpectContains(
        structuralPresentationPolicy,
        "m_Phase == Phase::AwaitPopupRollUp ||",
        "generic hidden committed-to-staged swap");
    const std::string_view structuralPhaseAdvancePolicy = ExtractSection(
        structuralPresentationPolicy,
        "void Advance(\n            int frame,",
        "void SkipInvisibleAnimation(int frame)",
        "generic staged structural advancement policy");
    ExpectContains(
        structuralPhaseAdvancePolicy,
        "!popupTransitionIdle",
        "exact popup-transition gate");
    ExpectContains(
        structuralPhaseAdvancePolicy,
        "else if (!layoutStable)",
        "generic phase layout-stability gate");
    ExpectContains(
        structuralPhaseAdvancePolicy,
        "m_Phase = Phase::ExpandStaged",
        "generic hidden staged-body swap");
    ExpectContains(
        structuralPhaseAdvancePolicy,
        "m_Phase = Phase::ReadyToCommit",
        "generic expanded-body ready phase");
    const std::string_view structuralCommitPolicy = ExtractSection(
        structuralPresentationPolicy,
        "bool CommitTo(Value& committed)",
        "void Cancel()",
        "generic structural commit policy");
    ExpectContains(
        structuralCommitPolicy,
        "if (m_Phase != Phase::ReadyToCommit)",
        "generic premature structural commit rejection");
    ExpectContains(
        deferredDropdownQueue,
        "DeferredUiActionQueue<ImGuiID, DeferredDropdownUiPayload> actions;",
        "keyed deferred dropdown queue");
    ExpectContains(
        deferredDropdownQueue,
        "ImGuiID transitionComboId = 0;",
        "originating dropdown transition identity");
    ExpectContains(
        deferredDropdownQueue,
        "state.actions.Upsert(",
        "keyed deferred dropdown replacement callback");
    ExpectContains(
        deferredDropdownQueue,
        "std::move(action)",
        "keyed deferred dropdown replacement callback");
    ExpectContains(
        deferredDropdownQueue,
        "UpdateUiDropdownIdleStartFrame(",
        "deferred dropdown idle arming");
    ExpectContains(
        deferredDropdownQueue,
        "ShouldCommitDeferredDropdownActions(",
        "deferred dropdown quiet-frame commit");
    ExpectContains(
        deferredDropdownQueue,
        "actions.Drain(",
        "deferred dropdown reentrant-safe flush");
    ExpectContains(
        deferredDropdownQueue,
        "static void CancelDeferredDropdownUiActions()",
        "deferred dropdown cancellation helper");
    ExpectContains(
        deferredDropdownQueue,
        "ImGui::FinishComboPopupTransition(",
        "canceled dropdown popup transition cleanup");
    ExpectContains(
        deferredDropdownQueue,
        "g_DeferredDropdownUiState = {};",
        "deferred dropdown cancellation helper");
    ExpectContains(
        deferredDropdownQueue,
        "DeferredUiStructuralPresentation<AntiAliasingSettings>",
        "Aliasing deferred presentation storage");
    ExpectContains(
        deferredDropdownQueue,
        "g_DeferredAliasingUiPresentation.Cancel();",
        "Aliasing deferred presentation cancellation");
    ExpectAbsent(
        deferredDropdownQueue,
        "SetScrollY",
        "dropdown queue scroll isolation");
    ExpectAbsent(
        deferredDropdownQueue,
        "ScrollTarget",
        "dropdown queue scroll isolation");
    ExpectAbsent(
        source,
        "PendingAliasingUiAction",
        "removed one-frame Aliasing apply gate");
    ExpectAbsent(
        source,
        "ApplyPendingAliasingUiAction",
        "removed drawer-local Aliasing apply");
    ExpectContains(
        source,
        "GetDeferredDropdownPreview(comboId)",
        "immediate pending dropdown preview");
    const std::string_view deferredDropdownOption = ExtractSection(
        source,
        "static bool DrawDeferredDropdownOption(",
        "static void ApplyExpandedWordSpacing(",
        "deferred dropdown selection wrapper");
    ExpectContains(
        deferredDropdownOption,
        "if (!activated || selected)",
        "active dropdown option no-op");
    ExpectContains(
        deferredDropdownOption,
        "QueueDeferredDropdownUiAction(",
        "deferred dropdown wrapper queue routing");

    const std::string_view sliderFloat = ExtractSection(
        source,
        "static bool DrawSliderFloat(",
        "static bool DrawSliderInt(",
        "float slider helper");
    const std::string_view sliderInt = ExtractSection(
        source,
        "static bool DrawSliderInt(",
        "static bool DrawCenteredActionButton(",
        "integer slider helper");
    ExpectAbsent(
        sliderFloat,
        "QueueDeferred",
        "float slider immediate interaction");
    ExpectAbsent(
        sliderInt,
        "QueueDeferred",
        "integer slider immediate interaction");

    const std::string_view buildUi = ExtractSection(
        source,
        "virtual void buildUI(void) override",
        "static bool TryParseUint32Argument(",
        "renderer UI composition");
    const std::string_view compositionIdlePolicy = ExtractSection(
        buildUi,
        "const auto deferredDropdownCompositionIdle =",
        "if (!m_ui.ShowUI && m_SettingsAppearance <= 0.f)",
        "deferred dropdown composition-idle policy");
    ExpectAbsent(
        compositionIdlePolicy,
        "ImGui::IsPopupOpen(",
        "unrelated popup dropdown starvation");
    ExpectAbsent(
        compositionIdlePolicy,
        "ImGuiPopupFlags_AnyPopupLevel",
        "unrelated popup dropdown starvation");
    ExpectContains(
        compositionIdlePolicy,
        "IsPixelZoomCompositionIdle(",
        "pixel-zoom dropdown commit gate");
    ExpectContains(
        compositionIdlePolicy,
        "g_DeferredAliasingUiPresentation.ReadyForCommit()",
        "Aliasing two-phase commit gate");
    ExpectContains(
        compositionIdlePolicy,
        "IsDeferredDropdownPopupTransitionActive()",
        "originating dropdown transition commit gate");

    const std::string_view settingsWindowFlagPolicy = ExtractSection(
        buildUi,
        "ImGuiWindowFlags settingsWindowFlags =",
        "ImGui::Begin(\n            \"Settings\"",
        "Settings root input policy");
    ExpectAbsent(
        settingsWindowFlagPolicy,
        "HasDeferredDropdownUiActions()",
        "pending dropdown root input capture");
    const std::string_view settingsBodyFlagPolicy = ExtractSection(
        buildUi,
        "ImGui::BeginChild(\n            \"##SettingsBody\"",
        "ImDrawList* settingsBodyDrawList =",
        "Settings body input policy");
    ExpectAbsent(
        settingsBodyFlagPolicy,
        "HasDeferredDropdownUiActions()",
        "pending dropdown child input capture");
    ExpectAbsent(
        settingsBodyFlagPolicy,
        "ImGuiWindowFlags_NoInputs",
        "pending dropdown child input capture");
    const std::string_view settingsBody = ExtractSection(
        buildUi,
        "ImGui::BeginChild(\n            \"##SettingsBody\"",
        "EndSettingsScrollStability();",
        "Settings body pending input barrier");
    ExpectContains(
        settingsBody,
        "const bool deferredDropdownInputBlocked =",
        "non-dimming pending dropdown input barrier");
    ExpectContains(
        settingsBody,
        "ImGuiStyleVar_DisabledAlpha, 1.f",
        "non-dimming pending dropdown input barrier");
    if (CountOccurrences(
            settingsBody,
            "if (deferredDropdownInputBlocked)") != 2)
    {
        Fail(
            "the pending dropdown input barrier must begin and end exactly "
            "once around SettingsBody controls.");
    }
    const size_t pendingBarrierClose =
        settingsBody.rfind("if (deferredDropdownInputBlocked)");
    const size_t settingsFooter =
        settingsBody.find("constexpr float ActionButtonCount = 4.f;");
    if (pendingBarrierClose == std::string_view::npos ||
        settingsFooter == std::string_view::npos ||
        pendingBarrierClose <= settingsFooter)
    {
        Fail(
            "the pending dropdown input barrier must close after the "
            "Settings footer controls.");
    }

    const std::string_view hiddenSettingsCommit = ExtractSection(
        buildUi,
        "if (!m_ui.ShowUI && m_SettingsAppearance <= 0.f)",
        "const float settingsAppearanceOpacity =",
        "hidden Settings dropdown commit point");
    ExpectOrdered(
        hiddenSettingsCommit,
        "FinishDeferredDropdownPopupTransition();",
        "g_DeferredAliasingUiPresentation.SkipInvisibleAnimation(",
        "hidden popup transition cleanup");
    ExpectOrdered(
        hiddenSettingsCommit,
        "g_DeferredAliasingUiPresentation.SkipInvisibleAnimation(",
        "TryApplyDeferredDropdownUiActions(",
        "hidden Aliasing transition bypass");
    if (CountOccurrences(
            hiddenSettingsCommit,
            "TryApplyDeferredDropdownUiActions(") != 1)
    {
        Fail(
            "fully hidden Settings must expose exactly one deferred "
            "dropdown commit point.");
    }
    ExpectOrdered(
        buildUi,
        "EndSettingsScrollStability();",
        "// Commit only after every UI window has finished composing.",
        "end-of-composition dropdown commit point");
    const std::string_view aliasingPhaseAdvance = ExtractSection(
        buildUi,
        "EndSettingsScrollStability();",
        "DrawSettingsScrollEdgeFades();",
        "Aliasing phase advancement");
    ExpectOrdered(
        aliasingPhaseAdvance,
        "const bool settingsScrollIdle =",
        "g_DeferredAliasingUiPresentation.Advance(",
        "Aliasing phase advancement");
    ExpectOrdered(
        aliasingPhaseAdvance,
        "const bool settingsLayoutIdle =",
        "g_DeferredAliasingUiPresentation.Advance(",
        "Aliasing phase advancement");
    ExpectContains(
        aliasingPhaseAdvance,
        "settingsLayoutIdle && settingsScrollIdle",
        "Aliasing phase idle barrier");
    ExpectContains(
        aliasingPhaseAdvance,
        "!IsDeferredDropdownPopupTransitionActive()",
        "Aliasing popup-roll phase barrier");
    const std::string_view settingsCommitPoint = ExtractSection(
        source,
        "// Commit only after every UI window has finished composing.",
        "static bool TryParseUint32Argument(",
        "end-of-composition dropdown commit point");
    ExpectContains(
        settingsCommitPoint,
        "TryApplyDeferredDropdownUiActions(",
        "end-of-composition dropdown commit point");
    if (CountOccurrences(
            settingsCommitPoint,
            "TryApplyDeferredDropdownUiActions(") != 1)
    {
        Fail(
            "visible Settings composition must expose exactly one deferred "
            "dropdown commit point.");
    }
    const std::string_view materialEditor = ExtractSection(
        buildUi,
        "if (m_ui.ShowMaterialEditor)",
        "// Commit only after every UI window has finished composing.",
        "Material Editor deferred domain selection");
    ExpectContains(
        materialEditor,
        "DrawDeferredDropdownOption(",
        "Material Editor deferred domain selection");
    ExpectContains(
        materialEditor,
        "const bool deferredMaterialInputBlocked =",
        "Material Editor pending input barrier");
    ExpectContains(
        materialEditor,
        "const std::shared_ptr<Scene> scene =",
        "Material Editor stable scene capture");
    ExpectContains(
        materialEditor,
        "material->dirty |=",
        "Material Editor retained dirty state");
    ExpectAbsent(
        materialEditor,
        "MaterialEditor(material.get(), true)",
        "Material Editor immediate domain mutation");
    ExpectContains(
        donutAppOverride,
        "+                &domainIndex,",
        "Material Editor domain storage safety");
    const std::string_view settingsControls = ExtractSection(
        source,
        "const bool generalOpen = DrawCollapsingHeader(",
        "constexpr float ActionButtonCount = 4.f;",
        "Settings dropdown controls");
    if (CountOccurrences(
            settingsControls,
            "ImGui::Selectable(") != 1)
    {
        Fail(
            "Settings must retain exactly one raw Selectable for the "
            "disabled Visibility reconstruction exception.");
    }
    if (CountOccurrences(visibility, "ImGui::Selectable(") != 1 ||
        CountOccurrences(general, "ImGui::Selectable(") != 0 ||
        CountOccurrences(buffers, "ImGui::Selectable(") != 0 ||
        CountOccurrences(statistics, "ImGui::Selectable(") != 0 ||
        CountOccurrences(aliasing, "ImGui::Selectable(") != 0 ||
        CountOccurrences(lights, "ImGui::Selectable(") != 0)
    {
        Fail(
            "the sole raw Settings Selectable must remain scoped to the "
            "disabled Visibility reconstruction row.");
    }
    ExpectContains(
        imguiOverride,
        "\"##ComboHighlightFrame\"",
        "dropdown highlight lifecycle reset");
    ExpectContains(
        imguiDropdownRoll,
        "UvsrComboPopupRollDuration = 0.18f",
        "dropdown roll duration constant");
    ExpectContains(
        imguiDropdownRoll,
        "UvsrComboPopupMaximumDelta = 1.0f / 30.0f",
        "dropdown roll slow-frame cap");
    ExpectContains(
        imguiDropdownRoll,
        "ApplyComboPopupRollClip(g.CurrentWindow)",
        "fixed-layout geometric dropdown roll");
    ExpectContains(
        imguiDropdownRoll,
        "IsComboPopupTransitionActive(ImGuiID combo_id)",
        "exact dropdown transition query");
    ExpectContains(
        imguiDropdownRoll,
        "FinishComboPopupTransition(ImGuiID combo_id)",
        "exact hidden-owner dropdown cleanup");
    if (CountOccurrences(
            imguiDropdownRoll,
            "+        FinishComboPopupTransition(id);") != 2)
    {
        Fail(
            "BeginCombo must finish the exact popup for both skipped-window "
            "and clipped-item owner paths.");
    }
    ExpectContains(
        imguiDropdownRoll,
        "-        ImGuiStyleVar_Alpha,",
        "removed dropdown alpha fade");
    ExpectContains(
        imguiDropdownRoll,
        "-            ImHashStr(\"##ComboPopupPendingSelectable\")",
        "removed delayed early-click replay");
    ExpectContains(
        imguiDropdownRoll,
        "combo_popup_interaction_blocked",
        "dropdown transition input suppression");
    ExpectOrdered(
        imguiDropdownRoll,
        "+    const bool combo_popup_interaction_blocked =",
        "+        pressed = ButtonBehavior(",
        "pre-ButtonBehavior dropdown input gate");
    ExpectContains(
        imguiDropdownRoll,
        "+        disabled_item || combo_popup_interaction_blocked",
        "hidden-row hover and tooltip suppression");
    ExpectContains(
        imguiDropdownRoll,
        "+    else if (g.ActiveId == id)",
        "retained early-click ownership cleanup");
    ExpectContains(
        imguiDropdownRoll,
        "+        ClearActiveID();",
        "retained early-click discard");
    ExpectOrdered(
        imguiDropdownRoll,
        "+        // Popup placement may move with a scrolling owner.",
        "+            ImHashStr(\"##ComboPopupRollFromBottom\")",
        "per-lifecycle dropdown roll direction latch");
    ExpectContains(
        source,
        "transitionComboLastSubmittedFrame",
        "exact dropdown owner submission tracking");
    ExpectContains(
        source,
        "FinishUnsubmittedDeferredDropdownPopupTransition();",
        "clipped dropdown owner cleanup");
    ExpectOrdered(
        source,
        "material->dirty |=",
        "FinishUnsubmittedDeferredDropdownPopupTransition();",
        "clipped dropdown cleanup after every popup owner composes");
    ExpectOrdered(
        source,
        "FinishUnsubmittedDeferredDropdownPopupTransition();",
        "TryApplyDeferredDropdownUiActions(\n            deferredDropdownCompositionIdle(",
        "clipped dropdown cleanup before end-of-composition commit");
    ExpectContains(
        structuralPhaseAdvancePolicy,
        "!popupTransitionIdle || !layoutStable",
        "popup-roll and layout-stability joint gate");
    ExpectContains(
        imguiDropdownRoll,
        "else\n+        {\n+            CloseCurrentPopup();",
        "generic popup native dismissal retention");

    if (g_FailureCount != 0)
    {
        std::cerr << g_FailureCount
                  << " UI source contract check(s) failed.\n";
        return 1;
    }

    std::cout << "UVSR UI source contracts passed.\n";
    return 0;
}
